#pragma once
#include <Arduino.h>

// BluFi BLE 配网入口（仅 AP 模式下启用）。
// 协议实现位于 ESP-IDF `esp_blufi` 组件，本类仅完成：
//   - BLE 控制器/Bluedroid 初始化、esp_blufi_callbacks_t 注册
//   - 接收手机端的 SSID/Password 后保存到 NVS 并切换到 STA 模式
// AES/DH/CRC 等安全回调由 blufi_security.h 中的 C 函数提供。
class Blufi {
public:
  // 在 AP 模式下调用：初始化 BluFi BLE 配网并开始广播（设备名称由内部获取）。
  static void init();

  // 关闭 BluFi（可选，STA 连接成功后调用以释放 BLE 资源）。
  static void deinit();
};
