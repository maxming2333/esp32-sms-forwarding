#include "Scheduler.h"
#include "config/AppConfig.h"
#include "sim/SimManager.h"
#include <esp_task_wdt.h>
#include <time.h>

void checkScheduledReboot() {
  if (!config.autoRebootEnabled) return;

  struct tm ti;
  if (!getLocalTime(&ti) || ti.tm_year < (2020 - 1900)) {
    // Time not synced yet — attempt re-sync periodically
    static unsigned long lastSyncTry = 0;
    if (millis() - lastSyncTry > 300000UL) {
      lastSyncTry = millis();
      Serial.println("[Scheduler] 时间未同步，重试NTP...");
      configTime(8 * 3600, 0, "ntp.aliyun.com", "ntp.ntsc.ac.cn", "pool.ntp.org");
    }
    return;
  }

  // Parse "HH:MM" target time
  int colonIdx = config.autoRebootTime.indexOf(':');
  if (colonIdx == -1) return;
  int targetHour = config.autoRebootTime.substring(0, colonIdx).toInt();
  int targetMin  = config.autoRebootTime.substring(colonIdx + 1).toInt();

  // Trigger once per minute window (within first 5 seconds of the target minute)
  if (ti.tm_hour == targetHour && ti.tm_min == targetMin && ti.tm_sec < 5) {
    Serial.println("[Scheduler] ⏰ 触发定时重启 (" + config.autoRebootTime + ")");
    // Feed watchdog before the potentially-slow resetModule()
    esp_task_wdt_reset();
    resetModule();
    Serial.println("[Scheduler] 重启ESP32...");
    delay(1000);
    ESP.restart();
  }
}

// ── Traffic keep-alive ────────────────────────────────────────────────────────
// Sends AT+MPING batches every N hours to consume ≈sizeKb of traffic,
// keeping the cellular data connection alive.
void checkTrafficKeep() {
  if (!config.trafficKeepEnabled) return;
  if (!simInitialized) return;
  if (config.trafficKeepIntervalHours <= 0) return;

  static unsigned long lastTrafficMs = 0;

  // On very first call just record time — wait a full interval before triggering.
  if (lastTrafficMs == 0) {
    lastTrafficMs = millis();
    return;
  }

  unsigned long intervalMs = (unsigned long)config.trafficKeepIntervalHours * 3600000UL;
  if (millis() - lastTrafficMs < intervalMs) return;

  // Update timestamp before doing the work so any WDT reset / reboot won't
  // cause an immediate re-trigger on the next boot (millis resets to 0 anyway,
  // so the first-call guard above will protect us).
  lastTrafficMs = millis();

  Serial.printf("[Traffic] ⏰ 开始定时消耗流量，目标: %d KB\n",
                config.trafficKeepSizeKb);

  // Activate PDP context (best-effort, ignore failure)
  while (Serial1.available()) Serial1.read();
  sendATCommand("AT+CGACT=1,1", 10000);
  delay(300);

  // Each AT+MPING batch sends 30 packets.
  // Estimate ~100 bytes of actual IP traffic per ping round-trip → 30 pkts ≈ 3 KB.
  // Calculate batches needed, capped at 10 (≈30 s × 10 = ~5 min max blocking).
  int bytesTarget = config.trafficKeepSizeKb * 1024;
  int bytesPerBatch = 30 * 100;  // 30 packets × ~100 bytes each
  int batches = (bytesTarget + bytesPerBatch - 1) / bytesPerBatch;
  if (batches < 1)  batches = 1;
  if (batches > 10) batches = 10;

  int successCount = 0;
  for (int b = 0; b < batches; b++) {
    esp_task_wdt_reset();
    while (Serial1.available()) Serial1.read();

    Serial1.println("AT+MPING=\"8.8.8.8\",30,1");
    unsigned long t = millis();
    String resp;
    while (millis() - t < 35000) {
      while (Serial1.available()) resp += (char)Serial1.read();
      if (resp.indexOf("+MPING:") >= 0 || resp.indexOf("ERROR") >= 0) break;
      delay(10);
    }

    bool batchOk = resp.indexOf("+MPING:") >= 0;
    if (batchOk) successCount++;
    Serial.printf("[Traffic] 批次 %d/%d %s\n", b + 1, batches,
                  batchOk ? "✅" : "⚠️ 超时/错误");
  }

  Serial.printf("[Traffic] 完成，约消耗 %d KB 流量 (%d/%d 批次成功)\n",
                config.trafficKeepSizeKb, successCount, batches);
}
