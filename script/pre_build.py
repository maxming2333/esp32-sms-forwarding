#!/usr/bin/env python3
"""
pre_build.py — PlatformIO pre-build script
Reads src/config/AppConfig.h for scalar constants and
src/config/PushTypeMeta.def for push-type enum entries, then writes
web/esp32_config.json so the Vue frontend can consume device-side data
at build time.

UI metadata (labels, hints, field visibility) is intentionally NOT kept
here — it belongs in the Vue component (PushChannelEditor.vue).

Registered in platformio.ini:
    extra_scripts = pre:script/pre_build.py
"""

import re
import json
import os
import subprocess
from datetime import datetime

Import("env")  # noqa: F821 — PlatformIO injects this

PROJECT_DIR = env.subst("$PROJECT_DIR")  # noqa: F821
HEADER      = os.path.join(PROJECT_DIR, "src", "config", "AppConfig.h")
DEF_FILE    = os.path.join(PROJECT_DIR, "src", "config", "PushTypeMeta.def")
OUT_JSON    = os.path.join(PROJECT_DIR, "web", "esp32_config.json")


def parse_header_defines(path):
    """Extract #define NAME integer/string constants from a C++ header."""
    int_defines = {}
    str_defines = {}

    if not os.path.exists(path):
        print(f"[pre_build] ⚠️  Header not found: {path}")
        return int_defines, str_defines

    with open(path, encoding="utf-8") as f:
        text = f.read()

    for m in re.finditer(r'#define\s+(\w+)\s+(\d+)', text):
        int_defines[m.group(1)] = int(m.group(2))

    for m in re.finditer(r'#define\s+(\w+)\s+"([^"]*)"', text):
        str_defines[m.group(1)] = m.group(2)

    return int_defines, str_defines


def parse_push_type_def(path):
    """
    Parse PushTypeMeta.def and return a list of {key, value} dicts.
    Recognises lines of the form:
        PUSH_TYPE_DEF(KEY_NAME, 42)
    Comment lines and blank lines are skipped automatically by the regex.
    """
    entries = []

    if not os.path.exists(path):
        print(f"[pre_build] ⚠️  Def file not found: {path}")
        return entries

    with open(path, encoding="utf-8") as f:
        text = f.read()

    for m in re.finditer(r'PUSH_TYPE_DEF\s*\(\s*(\w+)\s*,\s*(\d+)\s*\)', text):
        entries.append({"key": m.group(1), "value": int(m.group(2))})

    return entries


def get_git_commit(cwd):
    """Return short git commit hash, or 'unknown' if git is unavailable."""
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=cwd, stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        return "unknown"


def main():
    int_defs, str_defs = parse_header_defines(HEADER)
    push_types = parse_push_type_def(DEF_FILE)

    git_commit = get_git_commit(PROJECT_DIR)
    build_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    out = {
        "MAX_PUSH_CHANNELS": int_defs.get("MAX_PUSH_CHANNELS", 0),
        "DEFAULT_WEB_USER":  str_defs.get("DEFAULT_WEB_USER",  ""),
        "DEFAULT_WEB_PASS":  str_defs.get("DEFAULT_WEB_PASS",  ""),
        "PUSH_TYPES":        push_types,
        "GIT_COMMIT":        git_commit,
        "BUILD_TIME":        build_time,
        "REPO_URL":          "https://github.com/maxming2333/sms_forwarding",
        "AUTHOR":            "maxming2333",
    }

    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)

    print(f"[pre_build] ✅ Generated {OUT_JSON}")
    print(f"[pre_build]    MAX_PUSH_CHANNELS={out['MAX_PUSH_CHANNELS']}, "
          f"push types={len(push_types)}")


main()
