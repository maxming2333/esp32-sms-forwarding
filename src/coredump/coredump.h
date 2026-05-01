#pragma once
#include <Arduino.h>
#include <esp_partition.h>
#include <time.h>

// Coredump 模块：负责崩溃信息持久化与查询。
// - 启动时从 NVS 恢复崩溃时间/版本（用于诊断上一次崩溃）
// - panic 重启时利用 RTC_DATA_ATTR 保留的最近时间戳估算崩溃时刻
// - 提供 ELF 下载链接生成（与 GitHub Release 的固件 tag 对齐）
class Coredump {
public:
  // 初始化：从 NVS 恢复崩溃信息；若上次为 panic 重启，则将 RTC 中保留的最近时间戳与当前固件版本写入 NVS。
  static void init();

  // 周期调用：记录最新已知墙上时钟，用于崩溃时刻估算（带速率限制，避免 NVS 频繁写入）。
  static void updateLastKnownTime(time_t t);

  // 当前 coredump 分区是否包含数据（即上一次崩溃是否产生了 coredump）。
  static bool hasData();

  // 上一次崩溃的时间戳（0 表示未知）。
  static time_t crashTime();

  // 上一次崩溃时运行的固件版本（空字符串表示未知）。
  static String crashVersion();

  // 基于崩溃版本构造 ELF 下载 URL（崩溃版本未知时返回空串）。
  static String elfUrl();

  // coredump 分区句柄（找不到分区返回 nullptr）。
  static const esp_partition_t* partition();

  // 扫描 coredump 分区已使用的字节数（带缓存，避免重复扫描整分区）。
  static size_t usedSize(const esp_partition_t* part);

private:
  static time_t s_crashTime;       // 上次崩溃时间（NVS 持久化）
  static String s_crashVersion;    // 上次崩溃时固件版本（NVS 持久化）
  static size_t scanUsed(const esp_partition_t* part);  // 实际分区扫描实现
};
