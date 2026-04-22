#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <esp_task_wdt.h>

#include "wifi/wifi_manager.h"
#include "logger.h"
#include "config/config.h"
#include "sim/sim.h"
#include "call/call.h"
#include "time/time_module.h"
#include "sms/sms.h"
#include "push/push.h"
#include "push/push_retry.h"
#include "http/http_server.h"
#include "ota/ota_manager.h"

// Serial port mapping
#define TXD 3
#define RXD 4
#define MODEM_EN_PIN 5

AsyncWebServer server(80);

// 记录 SIM 信息是否已抓取（在 loop 中 SIM_READY 后执行一次）
static bool s_simInfoFetched = false;

// ---------- helpers ----------

static void blinkShort(unsigned long gap = 500) {
  digitalWrite(LED_BUILTIN, LOW);
  delay(50);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(gap);
}

static void delayWithWdt(unsigned long ms) {
  unsigned long elapsed = 0;
  while (elapsed < ms) {
    unsigned long step = min((unsigned long)500, ms - elapsed);
    delay(step);
    esp_task_wdt_reset();
    elapsed += step;
  }
}

static void modemPowerCycle() {
  pinMode(MODEM_EN_PIN, OUTPUT);
  LOG("SIM", "EN 拉低：关闭模组");
  digitalWrite(MODEM_EN_PIN, LOW);
  delayWithWdt(1200);
  LOG("SIM", "EN 拉高：开启模组");
  digitalWrite(MODEM_EN_PIN, HIGH);
  delayWithWdt(6000);
}

// ---------- Arduino entry points ----------

void setup() {
  // 立即喂狗：框架初始化可能已消耗部分 TWDT 窗口
  esp_task_wdt_reset();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delayWithWdt(1500);  // 替换裸 delay：此时尚未有任何输出，必须喂狗

  Serial1.setRxBufferSize(500);
  Serial1.begin(115200, SERIAL_8N1, RXD, TXD);

  // Modem cold start
  while (Serial1.available()) Serial1.read();
  modemPowerCycle();
  while (Serial1.available()) Serial1.read();

  // 时区初始化（须在 NTP/SIM 时间同步前调用）
  timeModuleInit();

  initConcatBuffer();
  loadConfig();
  loadRebootSchedule(rebootSchedule);
  esp_task_wdt_reset();

  simInit();
  esp_task_wdt_reset();

  // WiFi
  wifiManagerSetReconnectCallback([]{ timeModuleSyncNTP(); });
  wifiManagerInit();
  esp_task_wdt_reset();

  // 时间同步：STA 模式下优先使用 NTP；AP 模式下等 SIM 就绪后从 NITZ 同步
  if (wifiManagerGetMode() == WIFI_MODE_STA_CONNECTED) {
    timeModuleSyncNTP();
    if (timeModuleIsTimeSynced()) {
      LOG("WiFi", "NTP时间同步成功，UTC: %ld", (long)time(nullptr));
    } else {
      LOG("WiFi", "NTP时间同步失败，将在SIM就绪后通过NITZ同步");
    }
  } else {
    LOG("WiFi", "AP模式，跳过NTP同步。请访问 %s 配置WiFi", getDeviceUrl().c_str());
  }

  // LittleFS — formatOnFail=true 保证首次烧录或分区损坏时自动格式化
  // 格式化操作可能耗时数秒，需在前后喂狗
  esp_task_wdt_reset();
  if (!LittleFS.begin(true)) {
    LOG("HTTP", "LittleFS 挂载失败，HTML 页面不可用");
  } else {
    LOG("HTTP", "LittleFS 挂载成功");
  }
  esp_task_wdt_reset();

  setupHttpServer(server);
  otaInit();

  callInit();
  pushRetryInit();
  simStartReaderTask();

  digitalWrite(LED_BUILTIN, LOW);

  if (wifiManagerGetMode() == WIFI_MODE_STA_CONNECTED && isConfigValid()) {
    LOG("WiFi", "配置有效，发送启动通知...");
    sendPushNotification("设备", "设备已启动\n设备地址: " + getDeviceUrl(), timeModuleGetDateStr(), MSG_TYPE_SIM);
  }
}

void loop() {
  {
    static unsigned long lastUrlPrint = 0;
    if (millis() - lastUrlPrint >= 3000) {
      lastUrlPrint = millis();
      LOG("HTTP", "⚠️ 当前号码: %s，请访问 %s 进行配置", simGetPhoneNum().c_str(), getDeviceUrl().c_str());
    }
  }
  checkConcatTimeout();
  callTick();
  simTick();
  pushRetryTick();
  timeModuleTick();
  wifiManagerTick();

  // SIM 就绪后抓取运营商/信号，并在 NTP 未同步时从 SIM NITZ 同步时间
  if (!s_simInfoFetched && simGetState() == SIM_READY) {
    s_simInfoFetched = true;
    simFetchInfo();
    if (!timeModuleIsTimeSynced()) {
      timeModuleSyncFromSIM();
    }
  }

  rebootTick();
}
