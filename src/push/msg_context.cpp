#include "msg_context.h"
#include <Arduino.h>

static String s_deviceId = "";

String msgContextGetDeviceId() {
  if (s_deviceId.length() > 0) return s_deviceId;

  // 使用 ESP32 eFuse MAC 低 32 位作为设备 ID
  uint64_t mac = ESP.getEfuseMac();
  uint32_t low32 = (uint32_t)(mac & 0xFFFFFFFF);
  char buf[9];
  snprintf(buf, sizeof(buf), "%08X", low32);
  s_deviceId = String(buf);
  return s_deviceId;
}

String renderTemplate(const String& tmpl, const MessageContext& ctx) {
  String result = tmpl;
  result.replace("{sender}",     ctx.sender.length()    > 0 ? ctx.sender    : "未知");
  result.replace("{message}",    ctx.message);
  result.replace("{timestamp}",  ctx.timestamp.length() > 0 ? ctx.timestamp : "0");
  result.replace("{date}",       ctx.date.length()      > 0 ? ctx.date      : "未知");
  result.replace("{device_id}",  ctx.deviceId.length()  > 0 ? ctx.deviceId  : "未知");
  result.replace("{carrier}",    ctx.carrier.length()   > 0 ? ctx.carrier   : "未知");
  result.replace("{sim_number}", ctx.simNumber.length() > 0 ? ctx.simNumber : "未知");
  result.replace("{sim_slot}",   ctx.simSlot.length()   > 0 ? ctx.simSlot   : "SIM1");
  result.replace("{signal}",     ctx.signal.length()    > 0 ? ctx.signal    : "未知");
  return result;
}
