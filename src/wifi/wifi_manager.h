#pragma once
#include <Arduino.h>

enum WiFiMode {
  WIFI_MODE_UNINITIALIZED,
  WIFI_MODE_STA_CONNECTED,
  WIFI_MODE_AP_ACTIVE,
  WIFI_MODE_STA_FAILED,
  WIFI_MODE_RECONNECTING
};

// WiFi 重连常量
constexpr int WIFI_RECONNECT_ATTEMPTS_PER_SSID = 5;
constexpr unsigned long WIFI_RECONNECT_INTERVAL_MS = 5000;

// 重连成功回调类型
typedef void (*WifiReconnectCallback)();

void wifiManagerInit();
void wifiManagerTick();
WiFiMode wifiManagerGetMode();
String wifiManagerGetIP();
String getDeviceUrl();
String getDeviceId();    // 返回设备唯一 ID（eFuse MAC 低 32 位，8 位大写十六进制）
String getDeviceName();  // 返回设备名称，格式：SMS-Forwarder-<DeviceId>

void wifiManagerSetReconnectCallback(WifiReconnectCallback cb);
