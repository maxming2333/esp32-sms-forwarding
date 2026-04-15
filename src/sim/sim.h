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

// SIM 就绪后，查询并缓存运营商/本机号码/信号强度（由 main.cpp 调用）
void simFetchInfo();

// 以下 getter 返回缓存值，若未就绪则返回"未知"
String simGetCarrier();
String simGetPhoneNumber();
String simGetSignal();
