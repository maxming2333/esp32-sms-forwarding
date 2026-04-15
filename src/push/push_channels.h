#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config/config.h"

// Internal per-channel send functions
// renderedBody: 渲染后的自定义消息内容，非空时替换内置默认负载
bool sendPostJson(const PushChannel& ch, const String& sender, const String& message, const String& timestamp, const String& renderedBody);
bool sendBark(const PushChannel& ch, const String& sender, const String& message, const String& timestamp, const String& renderedBody);
bool sendGet(const PushChannel& ch, const String& sender, const String& message, const String& timestamp, const String& renderedBody);
bool sendDingtalk(const PushChannel& ch, const String& sender, const String& message, const String& timestamp, const String& renderedBody);
bool sendPushPlus(const PushChannel& ch, const String& sender, const String& message, const String& timestamp, const String& renderedBody);
bool sendServerChan(const PushChannel& ch, const String& sender, const String& message, const String& timestamp, const String& renderedBody);
bool sendCustom(const PushChannel& ch, const String& sender, const String& message, const String& timestamp, const String& renderedBody);
bool sendFeishu(const PushChannel& ch, const String& sender, const String& message, const String& timestamp, const String& renderedBody);
bool sendGotify(const PushChannel& ch, const String& sender, const String& message, const String& timestamp, const String& renderedBody);
bool sendTelegram(const PushChannel& ch, const String& sender, const String& message, const String& timestamp, const String& renderedBody);
bool sendWechatWork(const PushChannel& ch, const String& sender, const String& message, const String& timestamp, const String& renderedBody);
bool sendSmsPush(const PushChannel& ch, const String& sender, const String& message, const String& timestamp, const String& renderedBody);
