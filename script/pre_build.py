#!/usr/bin/env python3
"""
pre_build.py — PlatformIO pre-build script
Reads src/config/AppConfig.h to extract PushType enum values and other
constants, then writes web/esp32_config.json so the Vue frontend can
consume device-side data at build time.

Registered in platformio.ini:
    extra_scripts = pre:script/pre_build.py
"""

import re
import json
import os

Import("env")  # noqa: F821 — PlatformIO injects this

PROJECT_DIR = env.subst("$PROJECT_DIR")  # noqa: F821
HEADER      = os.path.join(PROJECT_DIR, "src", "config", "AppConfig.h")
OUT_JSON    = os.path.join(PROJECT_DIR, "web", "esp32_config.json")

# ── Push type metadata (labels / hints / field visibility) ───────────────────
# Kept here so the Python script is the single source of truth for UI metadata.
PUSH_TYPE_META = {
    1:  dict(key="PUSH_TYPE_POST_JSON",   label="POST JSON（通用格式）",
             hint='POST JSON格式：{"sender":"发送者号码","message":"短信内容","timestamp":"时间","device":"本机号码"}',
             showUrl=True,  urlLabel="推送URL/Webhook",
             showKey1=False, showKey2=False, showCustomBody=False),
    2:  dict(key="PUSH_TYPE_BARK",        label="Bark（iOS推送）",
             hint='Bark格式：POST {"title":"发送者号码","body":"短信内容"}',
             showUrl=True,  urlLabel="Bark推送URL",
             showKey1=False, showKey2=False, showCustomBody=False),
    3:  dict(key="PUSH_TYPE_GET",         label="GET请求（参数在URL中）",
             hint="GET请求格式：URL?sender=xxx&message=xxx&timestamp=xxx&device=xxx",
             showUrl=True,  urlLabel="GET请求URL",
             showKey1=False, showKey2=False, showCustomBody=False),
    4:  dict(key="PUSH_TYPE_DINGTALK",    label="钉钉机器人",
             hint="填写Webhook地址，如需加签请填Secret（key1）",
             showUrl=True,  urlLabel="Webhook地址",
             showKey1=True,  key1Label="Secret（加签密钥，可选）", key1Placeholder="SEC...",
             showKey2=False, showCustomBody=False),
    5:  dict(key="PUSH_TYPE_PUSHPLUS",    label="PushPlus",
             hint="填写Token，URL留空使用默认 http://www.pushplus.plus/send",
             showUrl=True,  urlLabel="推送URL（可留空）",
             showKey1=True,  key1Label="Token", key1Placeholder="pushplus的token",
             showKey2=True,  key2Label="发送渠道", key2Placeholder="wechat(默认), extension, app",
             showCustomBody=False),
    6:  dict(key="PUSH_TYPE_SERVERCHAN",  label="Server酱",
             hint="填写SendKey，URL留空使用默认",
             showUrl=True,  urlLabel="推送URL（可留空）",
             showKey1=True,  key1Label="SendKey", key1Placeholder="SCT...",
             showKey2=False, showCustomBody=False),
    7:  dict(key="PUSH_TYPE_CUSTOM",      label="自定义模板",
             hint="在请求体模板中使用 {sender} {message} {timestamp} {device} 占位符",
             showUrl=True,  urlLabel="POST URL",
             showKey1=False, showKey2=False, showCustomBody=True),
    8:  dict(key="PUSH_TYPE_FEISHU",      label="飞书机器人",
             hint="填写Webhook地址，如需签名验证请填Secret（key1）",
             showUrl=True,  urlLabel="Webhook地址",
             showKey1=True,  key1Label="Secret（签名密钥，可选）", key1Placeholder="飞书机器人签名密钥",
             showKey2=False, showCustomBody=False),
    9:  dict(key="PUSH_TYPE_GOTIFY",      label="Gotify",
             hint="填写服务器地址（如 http://gotify.example.com），Token填写应用Token",
             showUrl=True,  urlLabel="服务器地址",
             showKey1=True,  key1Label="Token（应用Token）", key1Placeholder="A...",
             showKey2=False, showCustomBody=False),
    10: dict(key="PUSH_TYPE_TELEGRAM",    label="Telegram Bot",
             hint="key1=Chat ID，key2=Bot Token，URL留空使用官方API",
             showUrl=True,  urlLabel="API基础URL（可留空）",
             showKey1=True,  key1Label="Chat ID",    key1Placeholder="123456789",
             showKey2=True,  key2Label="Bot Token",  key2Placeholder="12345678:ABC...",
             showCustomBody=False),
    11: dict(key="PUSH_TYPE_WORK_WEIXIN", label="企业微信机器人",
             hint="填写Webhook地址",
             showUrl=True,  urlLabel="Webhook地址",
             showKey1=False, showKey2=False, showCustomBody=False),
    12: dict(key="PUSH_TYPE_SMS",         label="短信转发",
             hint='URL字段填写目标手机号，国际号码用 "+国家码" 前缀，如 +8612345678900',
             showUrl=True,  urlLabel="目标手机号",
             showKey1=False, showKey2=False, showCustomBody=False),
}

def parse_header(path):
    """Extract #define constants and enum values from a C++ header."""
    defines = {}
    enum_values = {}

    if not os.path.exists(path):
        print(f"[pre_build] ⚠️  Header not found: {path}")
        return defines, enum_values

    with open(path, encoding="utf-8") as f:
        text = f.read()

    # #define NAME value
    for m in re.finditer(r'#define\s+(\w+)\s+(\d+)', text):
        defines[m.group(1)] = int(m.group(2))

    # enum values:  NAME = value,
    for m in re.finditer(r'(\w+)\s*=\s*(\d+)', text):
        enum_values[m.group(1)] = int(m.group(2))

    return defines, enum_values

def main():
    defines, enum_vals = parse_header(HEADER)

    max_ch   = defines.get("MAX_PUSH_CHANNELS", 5)
    web_user = None  # keep as string literal
    web_pass = None

    # Re-read header for string #defines
    if os.path.exists(HEADER):
        with open(HEADER, encoding="utf-8") as f:
            raw = f.read()
        m = re.search(r'#define\s+DEFAULT_WEB_USER\s+"([^"]+)"', raw)
        if m: web_user = m.group(1)
        m = re.search(r'#define\s+DEFAULT_WEB_PASS\s+"([^"]+)"', raw)
        if m: web_pass = m.group(1)

    push_types = []
    for value, meta in sorted(PUSH_TYPE_META.items()):
        entry = {"value": value, **meta}
        push_types.append(entry)

    out = {
        "MAX_PUSH_CHANNELS": max_ch,
        "DEFAULT_WEB_USER":  web_user or "admin",
        "DEFAULT_WEB_PASS":  web_pass or "admin123",
        "PUSH_TYPES":        push_types,
    }

    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)

    print(f"[pre_build] ✅ Generated {OUT_JSON}")
    print(f"[pre_build]    MAX_PUSH_CHANNELS={max_ch}, push types={len(push_types)}")

main()

