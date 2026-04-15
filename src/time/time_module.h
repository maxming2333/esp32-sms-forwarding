#pragma once
#include <Arduino.h>

// 初始化时间模块（设置默认时区 UTC+8）
void timeModuleInit();

// 通过 SIM 模组 AT+CCLK? 同步 NITZ 时间（SIM 就绪后调用）
void timeModuleSyncFromSIM();

// 通过 NTP 同步时间（WiFi 就绪后调用）
void timeModuleSyncNTP();

// 返回当前本地时间字符串（格式：YYYY-MM-DD HH:mm:ss）
// 若时间未同步则返回 "未知"
String timeModuleGetDateStr();
