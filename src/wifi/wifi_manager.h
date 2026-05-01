#pragma once
#include <Arduino.h>

// WiFi 工作模式枚举
enum WiFiMode {
  WIFI_MODE_UNINITIALIZED,    // 尚未初始化
  WIFI_MODE_STA_CONNECTED,    // STA 已连接到 AP，已获取 IP
  WIFI_MODE_AP_ACTIVE,        // 已启动 AP 模式（配网）
  WIFI_MODE_STA_FAILED,       // STA 全部 SSID 重试失败（保留枚举，目前直接走 AP 兜底）
  WIFI_MODE_RECONNECTING      // STA 断线后正在轮询重连
};

// WiFi 重连参数
constexpr int           WIFI_RECONNECT_ATTEMPTS_PER_SSID = 5;       // 每条 SSID 最多重试次数
constexpr unsigned long WIFI_RECONNECT_INTERVAL_MS       = 5000;    // 单次重连超时（毫秒）
// AP 模式下后台 WiFi 扫描间隔（业界标准 30s，与 Android 后台扫描一致）
constexpr unsigned long WIFI_AP_RESCAN_INTERVAL_MS       = 30000;

// 重连成功回调类型（STA 重连成功时触发）
typedef void (*WifiReconnectCallback)();

// WiFi 管理器：单实例（设备只有一个 WiFi 子系统），全静态成员封装。
// 职责：
//   - STA 模式启动/重连/扫描排序（信号强度优先）
//   - AP 模式启动 + BluFi 配网 + 后台扫描自动恢复 STA
//   - 设备名/设备 ID/IP/URL 等基础信息派发
class WifiManager {
public:
  // 初始化 WiFi：扫描并按信号强度依次尝试已配置 SSID；全部失败则进入 AP 模式。
  static void     init();

  // 周期 tick：处理 STA 断线重连状态机、AP 模式后台扫描状态机。
  static void     tick();

  // 当前工作模式
  static WiFiMode mode();

  // 初始化是否完成（STA 已获取 IP 或 AP 模式已激活均算完成）
  static bool     isInitDone();

  // 当前 IP（STA 时为 DHCP 地址；AP 时固定 192.168.4.1）
  static String   ip();

  // 设备访问 URL，例如 "http://192.168.4.1/"
  static String   deviceUrl();

  // 设备唯一 ID（取 eFuse MAC 后三字节，6 位大写十六进制）
  static String   deviceId();

  // 设备名称，格式 "SMS-Forwarder-<deviceId>"
  static String   deviceName();

  // 注册 STA 重连成功回调
  static void     setReconnectCallback(WifiReconnectCallback cb);
};
