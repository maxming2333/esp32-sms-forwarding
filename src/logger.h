#pragma once
#include <Arduino.h>
#include <time.h>

// Internal implementation — prints one log line: timestamp + module + message
inline void _logPrint(const char* module, const char* fmt, ...) {
  char ts[20];
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &t);

  va_list args;
  va_start(args, fmt);
  char msg[256];
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);

  Serial.printf("[%s] [%-6s] %s\r\n", ts, module, msg);
}

// Usage: LOG("WiFi", "Connected to %s", ssid)
#define LOG(module, fmt, ...) _logPrint((module), (fmt), ##__VA_ARGS__)
