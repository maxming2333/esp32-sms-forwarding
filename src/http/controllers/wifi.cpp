#include "wifi.h"
#include "config/config.h"
#include "logger.h"
#include <ArduinoJson.h>

void wifiGetController(AsyncWebServerRequest* request) {
  JsonDocument doc;
  JsonArray arr = doc["wifiList"].to<JsonArray>();
  for (int i = 0; i < config.wifiCount; i++) {
    JsonObject entry = arr.add<JsonObject>();
    entry["ssid"]     = config.wifiList[i].ssid;
    entry["password"] = "";  // 安全起见，不回显密码
  }
  String body;
  serializeJson(doc, body);
  request->send(200, "application/json", body);
}

void wifiPostController(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
  // Accumulate body if chunked; process only when complete
  if (index + len < total) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, data, len);
  if (err || !doc["wifiList"].is<JsonArray>()) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"缺少wifiList数组\"}");
    return;
  }

  JsonArray arr = doc["wifiList"].as<JsonArray>();
  if ((int)arr.size() > MAX_WIFI_ENTRIES) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"wifiList超过最大限制（5条）\"}");
    return;
  }

  int count = 0;
  for (JsonVariant v : arr) {
    if (!v.is<JsonObject>()) continue;
    JsonObject entry = v.as<JsonObject>();
    String ssid = entry["ssid"] | String("");
    if (ssid.length() == 0) continue;
    config.wifiList[count].ssid = ssid;
    String pw = entry["password"] | String("");
    if (pw.length() > 0) config.wifiList[count].password = pw;
    count++;
  }
  config.wifiCount = count;

  saveConfig();
  LOG("HTTP", "WiFi配置已更新，共 %d 条", config.wifiCount);

  request->send(200, "application/json",
    "{\"ok\":true,\"reboot\":true,\"message\":\"WiFi配置已保存，设备即将重启\"}");
  delay(500);
  ESP.restart();
}
