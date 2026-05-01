#pragma once
#include <Arduino.h>

// 来电事件处理器。
// 线程说明：handleRING/handleCLIP 在 SIM reader 任务上运行；tick() 在 loop() 上运行。
// ESP32-C3 单核 + 全部采用 millis() 超时比较 → 无需显式互斥锁。
// 业务逻辑：
//   1) RING 到达 → 记录起始时间，启动 CLIP 等待窗口
//   2) +CLIP 到达 → 提取号码，取消 CLIP 超时后推送
//   3) CLIP 超时 → 主动 AT+CLCC 查询底代；仍无果则以 "未知号码" 推送
//   4) DEDUP_MS 窗口内同号重复来电只推送一次
class Call {
public:
  // 同号码去重窗口（毫秒），避免运营商重拨重复推送。
  static constexpr unsigned long DEDUP_MS      = 10000;
  // RING 后等待 +CLIP 的最大时长（毫秒），超时后以 "未知号码" 推送。
  static constexpr unsigned long CLIP_WAIT_MS  = 10000;
  // RING 后多久主动发送 AT+CLCC 查询来电详情（毫秒）。
  static constexpr unsigned long CLCC_DELAY_MS = 3000;

  static void init();

  // SIM reader 任务检测到 RING URC 时调用。
  static void handleRING();

  // SIM reader 任务检测到 +CLIP URC 时调用。
  static void handleCLIP(const String& line);

  // 每轮 loop() 调用：处理 CLIP 超时、推送派发、AT+CLCC 底代查询。
  static void tick();

private:
  static volatile bool          s_pending;            // 是否有未推送的来电
  static volatile bool          s_dispatchPending;    // 是否该立即派发推送
  static String                 s_callerNumber;       // CLIP / CLCC 解析出的来电号码
  static volatile unsigned long s_clipWaitUntilMs;    // CLIP 等待超时截止点
  static unsigned long          s_lastNotifyMs;       // 上次推送时刻（去重用）
  static bool                   s_clccAttempted;      // 本轮是否已发起 AT+CLCC
  static unsigned long          s_clccAttemptMs;      // AT+CLCC 发起时刻

  static void   dispatch(const String& callerNum);    // 实际执行推送、并更新去重状态
  static String parseCLCC(const String& resp);        // 从 +CLCC 响应中提取主叫号
};
