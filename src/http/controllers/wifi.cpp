#include "wifi.h"
#include "config/config.h"
#include "logger.h"
#include <ArduinoJson.h>

void wifiGetController(AsyncWebServerRequest* request) {
  JsonDocument doc;
  doc["ssid"]     = config.wifiSsid;
  doc["password"] = config.wifiPass;
  String body;
  serializeJson(doc, body);
  request->send(200, "application/json", body);
}

void wifiPostController(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
  // Accumulate body if chunked; process only when complete
  if (index + len < total) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, data, len);
  if (err || !doc["ssid"].is<const char*>()) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"缺少ssid参数\"}");
    return;
  }

  config.wifiSsid = doc["ssid"].as<String>();
  if (doc["password"].is<const char*>()) {
    String pw = doc["password"].as<String>();
    if (pw.length() > 0) config.wifiPass = pw;
  }
  saveConfig();
  LOG("HTTP", "WiFi配置已更新，SSID: %s", config.wifiSsid.c_str());

  request->send(200, "application/json",
    "{\"ok\":true,\"reboot\":true,\"message\":\"WiFi配置已保存，设备即将重启\"}");
  delay(500);
  ESP.restart();
}
