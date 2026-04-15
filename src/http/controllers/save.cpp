#include "save.h"
#include "../http_server.h"
#include "config/config.h"
#include "email/email.h"
#include "wifi/wifi_manager.h"
#include "logger.h"
#include <ArduinoJson.h>
#include <WiFi.h>

#define DEFAULT_WEB_USER "admin"
#define DEFAULT_WEB_PASS "admin123"

void saveController(AsyncWebServerRequest* request) {
  // Read new credentials
  String newWebUser = request->hasParam("webUser", true) ? request->getParam("webUser", true)->value() : "";
  String newWebPass = request->hasParam("webPass", true) ? request->getParam("webPass", true)->value() : "";

  if (newWebUser.length() == 0) newWebUser = DEFAULT_WEB_USER;
  if (newWebPass.length() == 0) newWebPass = DEFAULT_WEB_PASS;

  config.webUser    = newWebUser;
  config.webPass    = newWebPass;
  config.smtpServer = request->hasParam("smtpServer", true) ? request->getParam("smtpServer", true)->value() : "";
  config.smtpPort   = request->hasParam("smtpPort",   true) ? request->getParam("smtpPort",   true)->value().toInt() : 465;
  if (config.smtpPort == 0) config.smtpPort = 465;
  config.smtpUser   = request->hasParam("smtpUser",   true) ? request->getParam("smtpUser",   true)->value() : "";
  config.smtpPass   = request->hasParam("smtpPass",   true) ? request->getParam("smtpPass",   true)->value() : "";
  config.smtpSendTo = request->hasParam("smtpSendTo", true) ? request->getParam("smtpSendTo", true)->value() : "";
  config.adminPhone = request->hasParam("adminPhone", true) ? request->getParam("adminPhone", true)->value() : "";
  config.simNotifyEnabled = request->hasParam("simNotifyEnabled", true);
  config.dataTraffic       = request->hasParam("dataTraffic",      true);

  if (request->hasParam("pushStrategy", true)) {
    int ps = request->getParam("pushStrategy", true)->value().toInt();
    if (ps == 0 || ps == 1) config.pushStrategy = (PushStrategy)ps;
  }

  // 读取用户配置的推送通道数量，限制在有效范围内
  int pushCount = request->hasParam("pushCount", true)
                  ? request->getParam("pushCount", true)->value().toInt()
                  : MAX_PUSH_CHANNELS;
  if (pushCount < 1) pushCount = 1;
  if (pushCount > MAX_PUSH_CHANNELS) {
    request->send(400, "application/json", "{\"ok\":false,\"message\":\"pushCount 超过最大限制\"}");
    return;
  }
  config.pushCount = pushCount;

  for (int i = 0; i < pushCount; i++) {
    String idx = String(i);
    config.pushChannels[i].enabled    = request->hasParam("push" + idx + "en",   true);
    config.pushChannels[i].type       = (PushType)(request->hasParam("push" + idx + "type", true) ? request->getParam("push" + idx + "type", true)->value().toInt() : 1);
    config.pushChannels[i].url        = request->hasParam("push" + idx + "url",  true) ? request->getParam("push" + idx + "url",  true)->value() : "";
    config.pushChannels[i].name       = request->hasParam("push" + idx + "name", true) ? request->getParam("push" + idx + "name", true)->value() : "";
    config.pushChannels[i].key1       = request->hasParam("push" + idx + "key1", true) ? request->getParam("push" + idx + "key1", true)->value() : "";
    config.pushChannels[i].key2       = request->hasParam("push" + idx + "key2", true) ? request->getParam("push" + idx + "key2", true)->value() : "";
    config.pushChannels[i].customBody = request->hasParam("push" + idx + "body", true) ? request->getParam("push" + idx + "body", true)->value() : "";
    if (config.pushChannels[i].name.length() == 0) {
      config.pushChannels[i].name = "通道" + String(i + 1);
    }
  }
  // 清除用户删除的通道槽位
  for (int i = pushCount; i < MAX_PUSH_CHANNELS; i++) {
    config.pushChannels[i] = PushChannel{};
  }

  saveConfig();
  refreshAuthCredentials();

  // RebootSchedule
  rebootSchedule.enabled   = request->hasParam("rbEnabled",   true);
  rebootSchedule.mode      = request->hasParam("rbMode",      true) ? (uint8_t)request->getParam("rbMode", true)->value().toInt() : 0;
  {
    int h = request->hasParam("rbHour",   true) ? request->getParam("rbHour",   true)->value().toInt() : 3;
    int m = request->hasParam("rbMinute", true) ? request->getParam("rbMinute", true)->value().toInt() : 0;
    int interval = request->hasParam("rbIntervalH", true) ? request->getParam("rbIntervalH", true)->value().toInt() : 24;
    rebootSchedule.hour      = (uint8_t)constrain(h, 0, 23);
    rebootSchedule.minute    = (uint8_t)constrain(m, 0, 59);
    rebootSchedule.intervalH = (uint16_t)constrain(interval, 1, 168);
  }
  saveRebootSchedule(rebootSchedule);

  LOG("HTTP", "配置已保存，发送成功响应");

  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "配置保存成功";
  String body;
  serializeJson(doc, body);
  request->send(200, "application/json", body);

  if (isConfigValid()) {
    String subject = "短信转发器配置已更新";
    String body = "设备配置已更新\n设备地址: " + getDeviceUrl();
    sendEmailNotification(subject.c_str(), body.c_str());
  }
}
