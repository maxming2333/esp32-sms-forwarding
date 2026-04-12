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
void checkSerial1URC();
void checkConcatTimeout();
bool sendSMSPDU(const char* phoneNumber, const char* message);
