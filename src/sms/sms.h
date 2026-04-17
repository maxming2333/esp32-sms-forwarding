#pragma once
#include <Arduino.h>

constexpr int MAX_CONCAT_PARTS    = 10;
constexpr unsigned long CONCAT_TIMEOUT_MS = 300000;  // 5分钟
constexpr int MAX_CONCAT_MESSAGES = 5;

struct SmsPart {
  bool   valid;
  String text;
};

struct ConcatSms {
  bool         inUse;
  int          refNumber;
  String       sender;
  String       timestamp;
  int          totalParts;
  int          receivedParts;
  unsigned long firstPartTime;
  SmsPart      parts[MAX_CONCAT_PARTS];
};

void initConcatBuffer();
void checkConcatTimeout();
bool sendSMSPDU(const char* phoneNumber, const char* message);

// URC 路由入口（由 SIM reader task 的 URC 回调调用）
void smsHandleCMTHeader();                     ///< +CMT: 头部到达，切换到等待 PDU 状态
void smsHandlePDU(const String& line);         ///< CMT 之后的 PDU 数据行处理
