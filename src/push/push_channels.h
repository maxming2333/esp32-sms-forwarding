#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config/config.h"

// Internal per-channel send functions
bool sendPostJson(const PushChannel& ch, const String& sender, const String& message, const String& timestamp);
bool sendBark(const PushChannel& ch, const String& sender, const String& message, const String& timestamp);
bool sendGet(const PushChannel& ch, const String& sender, const String& message, const String& timestamp);
bool sendDingtalk(const PushChannel& ch, const String& sender, const String& message, const String& timestamp);
bool sendPushPlus(const PushChannel& ch, const String& sender, const String& message, const String& timestamp);
bool sendServerChan(const PushChannel& ch, const String& sender, const String& message, const String& timestamp);
bool sendCustom(const PushChannel& ch, const String& sender, const String& message, const String& timestamp);
bool sendFeishu(const PushChannel& ch, const String& sender, const String& message, const String& timestamp);
bool sendGotify(const PushChannel& ch, const String& sender, const String& message, const String& timestamp);
bool sendTelegram(const PushChannel& ch, const String& sender, const String& message, const String& timestamp);
bool sendWechatWork(const PushChannel& ch, const String& sender, const String& message, const String& timestamp);
bool sendSmsPush(const PushChannel& ch, const String& sender, const String& message, const String& timestamp);
