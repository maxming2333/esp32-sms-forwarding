#pragma once
#include <Arduino.h>

// 时间同步模块：双源（SIM NITZ + NTP）。
// - 优先 NITZ（运营商下发，精度足够，无需联网）
// - WiFi 就绪后用 NTP 校准（pool.ntp.org / aliyun / cn.ntp.org.cn）
// - 同步成功后 dateStr() 返回带时区的本地时间字符串
class TimeSync {
public:
  // 初始化：设置默认时区为 UTC+8。
  static void init();

  // SIM 就绪后调用：通过 AT+CCLK? 获取 NITZ 时间并写入系统时钟。
  static void syncFromSIM();

  // WiFi 就绪后调用：启动 NTP 同步。
  static void syncNTP();

  // 返回本地时间字符串 "YYYY-MM-DD HH:mm:ss%z"；未同步返回 "未知"。
  static String dateStr();

  // 是否已通过 SIM 或 NTP 同步过时间。
  static bool isSynced();

  // 周期调用：未同步时每 10 s 重试一次 SIM NITZ 同步。
  static void tick();

private:
  static bool          s_synced;          // 全局同步状态
  static unsigned long s_simRetryNext;    // 下次 SIM 重试时刻
};
