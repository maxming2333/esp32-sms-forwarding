#pragma once
#include <Arduino.h>
#include <mbedtls/md.h>
#include <base64.h>

// JSON / URL helpers
String jsonEscape(const String& str);
String urlEncode(const String& str);

// Time helpers
String  formatTimestamp(const char* ts);   // "YYMMDDHHMMSS" → "YY/MM/DD HH:MM:SS"
int64_t getUtcMillis();                    // UTC timestamp in milliseconds

// Crypto helpers
String dingtalkSign(const String& secret, int64_t timestampMs);

// SMS helpers
String extractVerifyCode(const char* text);  // extract 4+ digit code

