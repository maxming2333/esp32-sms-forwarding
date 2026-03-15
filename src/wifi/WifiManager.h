#pragma once
#include <Arduino.h>
#include <WiFi.h>

// ── Globals (defined in WifiManager.cpp) ─────────────────────────────────────
extern bool timeSynced;

// ── API ───────────────────────────────────────────────────────────────────────
void     wifiInit(const String& ssid, const String& pass);  // connect STA or start AP
void     ntpSync();
String   getDeviceUrl();

