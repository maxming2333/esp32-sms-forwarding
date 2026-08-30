#pragma once
#include <Arduino.h>
#include "sim_dispatcher.h"

// SIM 卡运行状态枚举
enum SimState {
  SIM_UNKNOWN      = 0,    // 未知（启动初值）
  SIM_NOT_INSERTED = 1,    // 未插卡
  SIM_NOT_READY    = 2,    // 已插卡但未就绪（等待 PIN/网络注册等）
  SIM_INITIALIZING = 3,    // 正在初始化（执行 AT 序列）
  SIM_READY        = 4,    // 完全就绪，可以收发短信/电话
  SIM_INIT_FAILED  = 5     // 初始化失败
};

constexpr unsigned long SIM_NUMBER_RETRY_INTERVAL_MS = 5000;

// SIM 业务层：负责 SIM 生命周期、状态机、缓存信息（号码/运营商/信号）。
// 与底层 AT 通讯解耦：所有 AT 调度（互斥/队列/Reader Task）由 SimDispatcher 提供，
// 本类仅在其上构建业务方法（如 queryPhoneNumber、fetchInfo）。
class Sim {
public:
  // 确保模组处于「刚复位」的干净状态。必须在 init() 之前调用一次。
  //
  // 背景：main.cpp 的 modemPowerCycle() 通过 GPIO 拉动模组 EN 引脚，但实测在部分
  // 核心板上它并不会真正复位模组——上一次运行残留的逻辑通道会跨 ESP32 重启存活。
  // 本方法用「MANAGE CHANNEL open 返回的通道号是否为 1」直接检测卡会话是否干净，
  // 不干净则用 AT+MREBOOT 软复位兜底（实测复位到 SIM 可用约 2.3s）。
  // 顺带把残留逻辑通道清掉，并保证返回时 AT 接口已就绪。
  static void     ensureFreshModemSession();

  // 初始化 SIM 子系统：UART、GPIO、状态变量；不会立刻发 AT。
  static void     init();

  // 周期 tick：驱动状态机，必要时重试初始化、刷新信号强度等。
  static void     tick();

  // 主动拉取一次 SIM 信息（运营商/信号/号码），通常 SIM_READY 后调用。
  static void     fetchInfo();

  // 注册 URC 回调到 SimDispatcher，并启动 Reader Task。
  // 应在 setup() 末尾、所有模块初始化完毕后调用。
  static void     startReaderTask();

  // 状态与缓存值（READY 之前一律返回 "未知"）
  static SimState state();
  static String   carrier();
  static String   signal();
  static String   phoneNum();
  static bool     isNumberReady();

  // URC 路由入口：dispatcher 解析出的 URC 类型 → 转发到 SMS / Call / Sim 等模块。
  static void     handleURC(const String& line);

  // 业务方法：通过 AT+CNUM 查询本机号码（阻塞，最长 timeoutMs 毫秒）。
  // 内部经 SimDispatcher::sendCommand 与互斥队列交互；安全可在任意上下文调用，
  // 但**不要**在 Reader Task 自身的 URC 回调里调用，否则会死锁。
  static String   queryPhoneNumber(unsigned long timeoutMs = 3000);
};
