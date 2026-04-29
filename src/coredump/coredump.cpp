#include "coredump.h"
#include "logger.h"
#include "ota/ota_manager.h"
#include <Preferences.h>
#include <esp_system.h>
#include <sys/time.h>
#include <time.h>

// ── 内部状态 ──────────────────────────────────────────────────
// RTC 内存：panic 重启后保留，断电清零。记录最后已知 wall-clock 供崩溃时间估算。
RTC_DATA_ATTR static time_t s_rtcLastKnownTime = 0;
// 最后一次崩溃时间（panic 重启时从 RTC 写入 NVS，断电后从 NVS 加载）
static time_t s_crashTime    = 0;
// 最后一次崩溃时的固件版本（panic 重启时写入 NVS，断电后从 NVS 加载）
static String s_crashVersion = "";

// ── 内部帮助函数 ───────────────────────────────────────────────
// 扫描 coredump 分区末尾，返回实际使用字节数（0 = 分区为空）
static size_t scanCoredumpUsedSize(const esp_partition_t* part) {
  uint8_t buf[256];
  for (size_t offset = part->size; offset > 0;) {
    size_t chunk = (offset >= sizeof(buf)) ? sizeof(buf) : offset;
    offset -= chunk;
    if (esp_partition_read(part, offset, buf, chunk) != ESP_OK) return 0;
    for (int i = static_cast<int>(chunk) - 1; i >= 0; --i) {
      if (buf[i] != 0xFF) return offset + static_cast<size_t>(i) + 1;
    }
  }
  return 0;
}

// ── 公共 API ───────────────────────────────────────────────────
const esp_partition_t* coredumpGetPartition() {
  return esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA,
    ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
    nullptr
  );
}

size_t coredumpGetUsedSize(const esp_partition_t* part) {
  return scanCoredumpUsedSize(part);
}

void coredumpUpdateLastKnownTime(time_t t) {
  s_rtcLastKnownTime = t;
  // NVS 写入限流：仅当时间变化超过 60 秒才刷新，减少 flash 写入
  static time_t s_lastNvsWrite = 0;
  if (t - s_lastNvsWrite >= 60) {
    Preferences prefs;
    if (prefs.begin("sms_config", false)) {
      prefs.putLong("cdLastTs", (long)t);
      prefs.end();
    }
    s_lastNvsWrite = t;
  }
}

void coredumpInit() {
  Preferences prefs;
  prefs.begin("sms_config", false);

  // 断电重启时 RTC 归零，从 NVS 恢复最后已知时间
  if (s_rtcLastKnownTime == 0) {
    long saved = prefs.getLong("cdLastTs", 0);
    if (saved > 0) s_rtcLastKnownTime = (time_t)saved;
  }

  // 若系统时钟仍在 1970 附近，用 NVS 保存的近似时间先行恢复（NTP/NITZ 同步后精确覆盖）
  if (time(nullptr) < 1577836800L && s_rtcLastKnownTime > 1577836800L) {
    struct timeval tv;
    tv.tv_sec  = s_rtcLastKnownTime;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    LOG("Time", "从 NVS 恢复系统时间（近似）: %ld", (long)s_rtcLastKnownTime);
  }

  // 从 NVS 加载上次崩溃时间和固件版本
  long savedCrash = prefs.getLong("cdCrashTs", 0);
  if (savedCrash > 0) s_crashTime = (time_t)savedCrash;
  s_crashVersion = prefs.getString("cdCrashVer", "");

  // panic 重启：RTC 时间即为崩溃前最后已知时间，写入 NVS 使其与 coredump 文件对应。
  // 连续崩溃时 coredump 文件被覆盖，此处同步覆盖崩溃时间和版本，保持二者一致。
  if (esp_reset_reason() == ESP_RST_PANIC && s_rtcLastKnownTime > 0) {
    s_crashTime    = s_rtcLastKnownTime;
    s_crashVersion = otaGetVersion();
    prefs.putLong("cdCrashTs",    (long)s_crashTime);
    prefs.putString("cdCrashVer", s_crashVersion);
    LOG("Coredump", "检测到 panic 重启，崩溃时间: %ld，版本: %s",
        (long)s_crashTime, s_crashVersion.c_str());
  }

  prefs.end();
}

bool coredumpHasData() {
  const esp_partition_t* part = coredumpGetPartition();
  if (!part) return false;
  return scanCoredumpUsedSize(part) > 0;
}

time_t coredumpGetCrashTime() {
  return s_crashTime;
}

String coredumpGetCrashVersion() {
  return s_crashVersion;
}
