#include "msg_context.h"
#include <Arduino.h>

// 将 millis() 时长按粒度降级输出，例如：
//   < 1 分钟  → "不足1分钟"
//   < 1 小时  → "X分钟"
//   < 1 天    → "X小时Y分"
//   >= 1 天   → "X天Y小时"
String MsgContext::formatUptime(unsigned long ms) {
  unsigned long seconds = ms / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours   = minutes / 60;
  unsigned long days    = hours / 24;

  if (ms < 60000UL) {
    return "不足1分钟";
  }
  if (ms < 3600000UL) {
    return String(minutes) + "分钟";
  }
  if (ms < 86400000UL) {
    return String(hours) + "小时" + String(minutes % 60) + "分";
  }
  return String(days) + "天" + String(hours % 24) + "小时";
}

// 模板渲染：按字段名替换占位符。
// 替换顺序无关紧要（占位符各自独立）；空值统一回退为 "未知"，
// 避免推送内容出现孤立的 "null" / 空白字段。
String MsgContext::render(const String& tmpl, const MessageContext& ctx) {
  String result = tmpl;
  result.replace("{remark}",          ctx.remark);
  // {message_type} 为新名称；{trigger_type} 为向后兼容的旧别名（用户旧模板可能仍在用）
  result.replace("{message_type}",    ctx.messageType.length() > 0 ? ctx.messageType : "未知");
  result.replace("{trigger_type}",    ctx.messageType.length() > 0 ? ctx.messageType : "未知");
  result.replace("{uptime}",          ctx.uptime);
  result.replace("{channel_name}",    ctx.channelName);
  result.replace("{channel_type}",    ctx.channelType);
  // {from} 与 {sender} 互为别名（不同渠道的旧模板用法）
  result.replace("{from}",            ctx.from.length()       > 0 ? ctx.from       : "未知");
  result.replace("{sender}",          ctx.from.length()       > 0 ? ctx.from       : "未知");
  // {message_content} 与 {message} 互为别名
  result.replace("{message_content}", ctx.message);
  result.replace("{message}",         ctx.message);
  result.replace("{timestamp}",       ctx.timestamp.length()  > 0 ? ctx.timestamp  : "0");
  result.replace("{date}",            ctx.date.length()       > 0 ? ctx.date       : "未知");
  result.replace("{device_id}",       ctx.deviceId.length()   > 0 ? ctx.deviceId   : "未知");
  result.replace("{device_name}",     ctx.deviceName.length() > 0 ? ctx.deviceName : "未知");
  result.replace("{carrier}",         ctx.carrier.length()    > 0 ? ctx.carrier    : "未知");
  // {to} 与 {sim_number} 互为别名（接收方 = 本机 SIM 号码）
  result.replace("{to}",              ctx.to.length()         > 0 ? ctx.to         : "未知");
  result.replace("{sim_number}",      ctx.to.length()         > 0 ? ctx.to         : "未知");
  result.replace("{sim_slot}",        ctx.simSlot.length()    > 0 ? ctx.simSlot    : "SIM1");
  result.replace("{signal}",          ctx.signal.length()     > 0 ? ctx.signal     : "未知");
  return result;
}
