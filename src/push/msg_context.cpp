#include "msg_context.h"
#include <Arduino.h>
#include "wifi/wifi_manager.h"

String msgContextGetDeviceId() {
  return getDeviceId();
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
