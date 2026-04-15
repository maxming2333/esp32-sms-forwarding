#pragma once
#include <Arduino.h>

struct MessageContext {
  String sender;
  String message;
  String timestamp;
  String date;
  String deviceId;
  String carrier;
  String simNumber;
  String simSlot;
  String signal;
};

// 获取设备唯一 ID（首次调用时读取并缓存）
String msgContextGetDeviceId();

// 渲染消息模板：替换所有已识别占位符，未识别占位符原样保留
String renderTemplate(const String& tmpl, const MessageContext& ctx);
