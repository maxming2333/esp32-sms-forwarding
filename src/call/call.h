/**
 * @file call.h
 * @brief 来电事件处理模块 — 公共 API 契约
 *
 * 该模块负责：
 * - 接收 SIM reader task 路由来的 RING / +CLIP URC
 * - 管理 5 秒 CLIP 等待窗口（超时后以"未知号码"发通知）
 * - 30 秒防抖（同一号码不重复推送）
 * - 黑名单过滤
 * - 通过 sendPushNotification(MSG_TYPE_CALL) 发出来电通知
 *
 * 依赖:
 *   push/push.h, config/config.h, sms/phone_utils.h, logger.h
 *
 * 线程安全性:
 *   callHandleRING() 和 callHandleCLIP() 由 SIM reader task 调用（非 loop() 任务）。
 *   callTick() 由 loop() 任务调用。
 *   两者访问共享状态 s_pending / s_callerNumber / s_clipWaitUntilMs。
 *   由于 ESP32-C3 为单核，且 reader task 与 loop() 不会真正并发（抢占调度），
 *   共享状态通过 volatile + 原子 millis() 比较保证一致性；无需额外互斥锁。
 */

#pragma once
#include <Arduino.h>

// ---------- 具名常量 ----------

/** 同一号码来电防抖窗口（毫秒）。硬编码，不提供 Web 配置界面。 */
constexpr unsigned long CALL_DEDUP_MS     = 30000;

/** RING 触发后等待 +CLIP 的最大时间（毫秒）。超时则以"未知号码"发通知。 */
constexpr unsigned long CALL_CLIP_WAIT_MS = 5000;

// ---------- 模块初始化 ----------

/**
 * @brief 初始化来电模块内部状态。
 *        在 setup() 中、simInit() 之后、simStartReaderTask() 之前调用。
 */
void callInit();

// ---------- URC 路由入口（由 SIM reader task 调用） ----------

/**
 * @brief 处理 RING URC。
 *
 *        - 若防抖窗口内（距上次通知 < CALL_DEDUP_MS），忽略本次 RING
 *        - 否则：置 s_pending = true，s_callerNumber = "未知号码"，
 *          记录 s_clipWaitUntilMs = millis() + CALL_CLIP_WAIT_MS
 */
void callHandleRING();

/**
 * @brief 处理 +CLIP URC，提取并记录主叫号码。
 *
 * @param line  原始 +CLIP 行，格式: +CLIP: "号码",129,,,"",0
 *
 *        - 若 s_pending == true，解析号码并立即触发通知（提前结束等待窗口）
 *        - 若 s_pending == false，丢弃（RING 尚未到达或已处理）
 */
void callHandleCLIP(const String& line);

// ---------- 周期性驱动（由 loop() 调用） ----------

/**
 * @brief 检查 CLIP 等待窗口是否超时，超时则以"未知号码"触发来电通知。
 *        应在每次 loop() 迭代中无条件调用（调用开销极低）。
 */
void callTick();
