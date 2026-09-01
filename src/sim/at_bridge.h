#pragma once
#include <Arduino.h>

// USB ↔ 模组 UART 的透明 AT 透传（「USB AT 透传模式」）。
//
// 用途：把 ML307x 的 AT 接口原样暴露到 ESP32 的 USB CDC 端口，让主机上的工具
// （screen / minicom / pySim-shell / SWu-IKEv2 的 -m 串口模式等）直接驱动模组与
// SIM 卡，不经过本固件的任何解析。主要面向调试与外部 VoWiFi 方案取卡。
//
// 为什么是「开机模式」而不是运行时热切换：
//   SimDispatcher::sendCommand() 永不超时地等待 reader task 给信号量（槽的生命周期
//   不变式要求它不能提前返回）。若在运行期用 pauseReader() 独占 UART，任何仍在发
//   AT 的调用方（Sim::tick 的注册轮询、/at、/query 等）都会永久挂住——喂着狗不崩，
//   但卡死。因此本模式在 setup() 阶段就决定：启用时根本不启动 SimDispatcher，
//   冲突从源头上不存在。
//
// 启用后的行为：
//   - 固件不再主动访问 Serial1（不收短信、不收来电、不查询模组状态）
//   - 串口日志被关闭，USB 通道是纯 AT 流；日志仍进内存缓冲与文件，可从网页查看
//   - WiFi / HTTP / 网页完全不受影响，可随时从网页关掉开关并重启回退
class AtBridge {
public:
  // 启动透传任务。应在 setup() 中确认 config.atBridgeEnabled 为真后调用一次，
  // 且**不要**再调用 Sim::init() / Sim::startReaderTask()。
  static void start();

  // 透传任务是否已启动。
  static bool active();
};
