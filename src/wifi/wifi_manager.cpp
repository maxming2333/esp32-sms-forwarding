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
  // 软重启（ESP.restart）后 WiFi 驱动状态可能残留，强制关闭后再初始化
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  if (config.wifiCount == 0) {
    LOG("WiFi", "未配置任何WiFi，直接进入AP模式");
    enterAPMode();
    return;
  }

  // 扫描所有信道以连接信号最强的 AP，防止在 mesh 组网这类场景中连接到弱 AP
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);

  for (int w = 0; w < config.wifiCount; w++) {
    if (config.wifiList[w].ssid.length() == 0) continue;

    const char* ssid = config.wifiList[w].ssid.c_str();
    const char* pass = config.wifiList[w].password.c_str();
    LOG("WiFi", "尝试第 %d/%d 条WiFi: %s", w + 1, config.wifiCount, ssid);

    for (int attempt = 1; attempt <= 3; attempt++) {
      WiFi.begin(ssid, pass, 0, nullptr, true);

      unsigned long start = millis();
      while (millis() - start < 3000) {
        if (WiFi.status() == WL_CONNECTED) {
          s_mode = WIFI_MODE_STA_CONNECTED;
          LOG("WiFi", "第 %d/%d 条WiFi第 %d/3 次连接成功，IP: %s", w + 1, config.wifiCount, attempt, WiFi.localIP().toString().c_str());
          return;
        }
        delay(100);
      }

      LOG("WiFi", "第 %d/%d 条WiFi第 %d/3 次连接超时", w + 1, config.wifiCount, attempt);
      WiFi.disconnect(true);
      delay(200);
    }
  }

  LOG("WiFi", "所有WiFi条目全部失败，切换到AP模式");
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
