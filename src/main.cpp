#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>

#include "wifi/wifi_manager.h"
#include "logger.h"
#include "config/config.h"
#include "sim/sim.h"
#include "sms/sms.h"
#include "push/push.h"
#include "email/email.h"
#include "http/http_server.h"

// Serial port mapping
#define TXD 3
#define RXD 4
#define MODEM_EN_PIN 5

AsyncWebServer server(80);

// Shared state (extern-referenced by handler_api_status.cpp)
bool timeSynced = false;

// ---------- helpers ----------

static void blinkShort(unsigned long gap = 500) {
  digitalWrite(LED_BUILTIN, LOW);
  delay(50);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(gap);
}

static void modemPowerCycle() {
  pinMode(MODEM_EN_PIN, OUTPUT);
  LOG("SIM", "EN 拉低：关闭模组");
  digitalWrite(MODEM_EN_PIN, LOW);
  delay(1200);
  LOG("SIM", "EN 拉高：开启模组");
  digitalWrite(MODEM_EN_PIN, HIGH);
  delay(6000);
}

// ---------- Arduino entry points ----------

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(1500);

  Serial1.setRxBufferSize(500);
  Serial1.begin(115200, SERIAL_8N1, RXD, TXD);

  // Modem cold start
  while (Serial1.available()) Serial1.read();
  modemPowerCycle();
  while (Serial1.available()) Serial1.read();

  initConcatBuffer();
  loadConfig();
  loadRebootSchedule(rebootSchedule);

  simInit();

  // WiFi
  wifiManagerInit();

  // NTP — only in STA mode (requires internet access)
  if (wifiManagerGetMode() == WIFI_MODE_STA_CONNECTED) {
    LOG("WiFi", "正在同步NTP时间...");
    configTime(0, 0, "ntp.ntsc.ac.cn", "ntp.aliyun.com", "pool.ntp.org");
    int ntpRetry = 0;
    while (time(nullptr) < 100000 && ntpRetry < 100) { delay(100); ntpRetry++; }
    if (time(nullptr) >= 100000) {
      timeSynced = true;
      LOG("WiFi", "NTP时间同步成功，UTC: %ld", (long)time(nullptr));
    } else {
      LOG("WiFi", "NTP时间同步失败，将使用设备时间");
    }
  } else {
    LOG("WiFi", "AP模式，跳过NTP同步。请访问 %s 配置WiFi", getDeviceUrl().c_str());
  }

  // LittleFS — formatOnFail=true 保证首次烧录或分区损坏时自动格式化
  if (!LittleFS.begin(true)) {
    LOG("HTTP", "LittleFS 挂载失败，HTML 页面不可用");
  } else {
    LOG("HTTP", "LittleFS 挂载成功");
  }

  setupHttpServer(server);

  digitalWrite(LED_BUILTIN, LOW);

  if (wifiManagerGetMode() == WIFI_MODE_STA_CONNECTED && isConfigValid()) {
    LOG("WiFi", "配置有效，发送启动通知...");
    String subject = "短信转发器已启动";
    String body = "设备已启动\n设备地址: " + getDeviceUrl();
    sendEmailNotification(subject.c_str(), body.c_str());
  }
}

void loop() {
  {
    static unsigned long lastUrlPrint = 0;
    if (millis() - lastUrlPrint >= 3000) {
      lastUrlPrint = millis();
      LOG("HTTP", "⚠️ 请访问 %s 进行配置", getDeviceUrl().c_str());
    }
  }
  checkConcatTimeout();
  if (Serial.available()) Serial1.write(Serial.read());
  checkSerial1URC();
  simTick();
  rebootTick();
}
