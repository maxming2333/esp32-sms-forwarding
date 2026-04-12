#include "wifi_manager.h"
#include <WiFi.h>
#include "config/config.h"
#include "logger.h"

static WiFiMode s_mode = WIFI_MODE_UNINITIALIZED;

static void enterAPMode() {
  WiFi.softAP("SMS-Forwarder-AP");
  s_mode = WIFI_MODE_AP_ACTIVE;
  LOG("WiFi", "AP模式启动，SSID: SMS-Forwarder-AP，IP: 192.168.4.1");
}

void wifiManagerInit() {
  if (config.wifiSsid.length() == 0) {
    LOG("WiFi", "SSID未配置，直接进入AP模式");
    enterAPMode();
    return;
  }

  LOG("WiFi", "准备连接到WiFi: %s", config.wifiSsid.c_str());
  // 扫描所有信道以连接信号最强的 AP，防止在 mesh 组网这类场景中连接到弱 AP
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);

  for (int attempt = 1; attempt <= 3; attempt++) {
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPass.c_str(), 0, nullptr, true);

    unsigned long start = millis();
    while (millis() - start < 5000) {
      if (WiFi.status() == WL_CONNECTED) {
        s_mode = WIFI_MODE_STA_CONNECTED;
        LOG("WiFi", "第 %d/3 次连接成功，IP: %s", attempt, WiFi.localIP().toString().c_str());
        return;
      }
      delay(100);
    }

    LOG("WiFi", "第 %d/3 次连接超时，重置WiFi后重试", attempt);
    WiFi.disconnect(true);
    delay(500);
  }

  LOG("WiFi", "3次连接全部失败，切换到AP模式");
  enterAPMode();
}

WiFiMode wifiManagerGetMode() {
  return s_mode;
}

String wifiManagerGetIP() {
  if (s_mode == WIFI_MODE_STA_CONNECTED) {
    return WiFi.localIP().toString();
  }
  return IPAddress(192, 168, 4, 1).toString();
}

String getDeviceUrl() {
  return "http://" + wifiManagerGetIP() + "/";
}
