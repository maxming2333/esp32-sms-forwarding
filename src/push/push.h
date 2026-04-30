#pragma once
#include <Arduino.h>
#include "config/config.h"

// 消息类型描述结构体：调用方构建后直接传入 push 函数
// 内部通过 type 做路由判断，typeLabel 仅在 MSG_TYPE_SMS 时有意义
struct MsgTypeInfo {
  MsgType type     = MSG_TYPE_SMS;
  String  typeLabel;  // 可选，如 "Class 0（即显短信）"

  MsgTypeInfo() = default;
  MsgTypeInfo(MsgType t) : type(t) {}
  MsgTypeInfo(MsgType t, const String& cls) : type(t), typeLabel(cls) {}

  // 序列化为人类可读字符串（供模板占位符 {message_type} 使用）
  String toString() const {
    if (type == MSG_TYPE_CALL) return "来电";
    if (type == MSG_TYPE_SIM)  return "SIM事件";
    if (type == MSG_TYPE_SMS)  return typeLabel.length() > 0 ? ("短信：" + typeLabel) : "短信";
    char typeBuf[32];
    snprintf(typeBuf, sizeof(typeBuf), "未知消息(type=%d)", (int)type);
    return typeLabel.length() > 0 ? (String(typeBuf) + "：" + typeLabel) : String(typeBuf);
  }
};

void sendPushNotification(const String& sender, const String& message, const String& timestamp, const MsgTypeInfo& msgType);
bool sendPushChannel(int channelIdx, const String& sender, const String& message, const String& timestamp, const MsgTypeInfo& msgType);
