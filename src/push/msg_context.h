#pragma once
#include <Arduino.h>

// 消息上下文：用户在自定义模板中可引用的全部字段。
// 占位符规则：模板中使用 `{字段名}` 引用；未识别的占位符按原样保留，方便排查。
struct MessageContext {
  String from;            // 来源（短信发件人 / 来电号码 / SIM 事件源）
  String message;         // 消息正文
  String timestamp;       // 原始时间戳（短信用 SCTS，其它用本地时间）
  String date;            // 本地化时间字符串 "YYYY-MM-DD HH:mm:ss%z"
  String deviceId;        // 设备唯一 ID（MAC 后三字节）
  String carrier;         // 运营商
  String to;              // 接收方号码（本机号码）
  String simSlot;         // SIM 槽位（当前固定 "1"，预留多卡）
  String signal;          // 信号强度
  String remark;          // 设备备注（用户在 Web 界面配置）
  String uptime;          // 设备已运行时长（人类可读）
  String channelName;     // 当前推送渠道名称
  String channelType;     // 当前推送渠道类型
  String deviceName;      // 设备名 "SMS-Forwarder-<deviceId>"
  String messageType;     // 消息类型（{message_type}；兼容旧别名 {trigger_type}）
};

// MsgContext：模板渲染工具类。
// 用于将 MessageContext 中的字段填入用户自定义的标题/内容模板，
// 并提供 deviceId 缓存、uptime 友好格式化等小工具。
class MsgContext {
public:
  // 将 millis() 时长格式化为中文可读字符串，例如 "3天 2小时 5分钟"。
  static String formatUptime(unsigned long ms);

  // 渲染模板：替换已识别的 `{字段名}` 占位符；未识别占位符原样保留。
  // 兼容旧别名（例如 {trigger_type} → {message_type}）。
  static String render(const String& tmpl, const MessageContext& ctx);
};
