#pragma once
#include <Arduino.h>
#include <Preferences.h>

// ── Hardware pins ────────────────────────────────────────────────────────────
#define TXD           3
#define RXD           4
#define MODEM_EN_PIN  5
#ifndef LED_BUILTIN
#  define LED_BUILTIN 8
#endif

// ── Web management defaults ──────────────────────────────────────────────────
#define DEFAULT_WEB_USER "admin"
#define DEFAULT_WEB_PASS "admin123"

// ── Push channel limits ──────────────────────────────────────────────────────
#define MAX_PUSH_CHANNELS 5

// ── Push type enumeration ────────────────────────────────────────────────────
// NOTE: Values are exported to the Vue frontend via script/pre_build.py
enum PushType {
  PUSH_TYPE_NONE        = 0,
  PUSH_TYPE_POST_JSON   = 1,
  PUSH_TYPE_BARK        = 2,
  PUSH_TYPE_GET         = 3,
  PUSH_TYPE_DINGTALK    = 4,
  PUSH_TYPE_PUSHPLUS    = 5,
  PUSH_TYPE_SERVERCHAN  = 6,
  PUSH_TYPE_CUSTOM      = 7,
  PUSH_TYPE_FEISHU      = 8,
  PUSH_TYPE_GOTIFY      = 9,
  PUSH_TYPE_TELEGRAM    = 10,
  PUSH_TYPE_WORK_WEIXIN = 11,
  PUSH_TYPE_SMS         = 12,
};

// ── Structs ──────────────────────────────────────────────────────────────────
struct PushChannel {
  bool     enabled;
  PushType type;
  String   name;
  String   url;
  String   key1;
  String   key2;
  String   customBody;
};

struct Config {
  String      smtpServer;
  int         smtpPort;
  String      smtpUser;
  String      smtpPass;
  String      smtpSendTo;
  String      adminPhone;
  PushChannel pushChannels[MAX_PUSH_CHANNELS];
  String      webUser;
  String      webPass;
  String      wifiSSID;
  String      wifiPass;
  String      numberBlackList;
};

// ── Global instances (defined in AppConfig.cpp) ──────────────────────────────
extern Config config;
extern bool   configValid;

// ── API ──────────────────────────────────────────────────────────────────────
void saveConfig();
void loadConfig();
bool isConfigValid();
bool isPushChannelValid(const PushChannel& ch);
bool isInNumberBlackList(const char* sender);

