#pragma once
#include <Arduino.h>

enum WiFiMode {
  WIFI_MODE_UNINITIALIZED,
  WIFI_MODE_STA_CONNECTED,
  WIFI_MODE_AP_ACTIVE,
  WIFI_MODE_STA_FAILED
};

void wifiManagerInit();
WiFiMode wifiManagerGetMode();
String wifiManagerGetIP();
String getDeviceUrl();
