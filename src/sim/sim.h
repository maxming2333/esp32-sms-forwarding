#pragma once
#include <Arduino.h>

enum SimState {
  SIM_UNKNOWN        = 0,
  SIM_NOT_INSERTED   = 1,
  SIM_NOT_READY      = 2,
  SIM_INITIALIZING   = 3,
  SIM_READY          = 4,
  SIM_INIT_FAILED    = 5
};

void simInit();
SimState simGetState();
void simHandleURC(const String& line);
void simTick();

// SIM 就绪后，查询并缓存运营商/信号强度（由 main.cpp 调用）
void simFetchInfo();

// 以下 getter 返回缓存值，若未就绪则返回"未知"
String simGetCarrier();
String simGetSignal();
String simGetPhoneNum();

// 本机号码是否已成功查询（就绪后首次获取或重试成功后返回 true）
bool simIsNumberReady();

/**
 * @brief 注册 URC 回调后启动 SIM reader task。
 *        在 setup() 末尾所有模块初始化完成后调用。
 *        注意：simQueryPhoneNumber() 声明位于 sim/sim_dispatcher.h。
 */
void simStartReaderTask();
