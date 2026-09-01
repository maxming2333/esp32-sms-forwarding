#include "at_bridge.h"
#include "../logger/logger.h"

namespace {

// 单次搬运的字节数上限。115200 波特下 1ms 约 11.5 字节，256 足够吸收突发，
// 也远小于 Serial1 的 2048 RX 缓冲。
constexpr size_t      BRIDGE_CHUNK      = 256;
constexpr uint32_t    BRIDGE_TASK_STACK = 3072;
// 优先级高于 loop（1），保证透传延迟稳定；低于 SIM reader task 的 3 无意义
// （该模式下 reader task 不会启动），这里取 4 以免被 HTTP 处理拖慢。
constexpr UBaseType_t BRIDGE_TASK_PRIO  = 4;

TaskHandle_t s_task = nullptr;

void atBridgeTask(void*) {
  uint8_t buf[BRIDGE_CHUNK];
  for (;;) {
    // ── USB → 模组 ──
    size_t n = 0;
    while (Serial.available() && n < BRIDGE_CHUNK) {
      buf[n++] = (uint8_t)Serial.read();
    }
    if (n > 0) Serial1.write(buf, n);

    // ── 模组 → USB ──
    // Serial1 必须无条件排空，否则 RX 缓冲溢出会丢字节；
    // 但 USB 未连接时不能往 HWCDC 写（会阻塞到超时，拖死整个任务），此时直接丢弃。
    n = 0;
    while (Serial1.available() && n < BRIDGE_CHUNK) {
      buf[n++] = (uint8_t)Serial1.read();
    }
    if (n > 0 && Serial) Serial.write(buf, n);

    vTaskDelay(1);
  }
}

}  // namespace

void AtBridge::start() {
  if (s_task != nullptr) return;

  // 关闭串口日志：USB 通道必须是纯 AT 流，否则主机侧工具会被日志行干扰。
  // 日志仍写入内存缓冲与 LittleFS，可从网页 /api/logs 查看。
  Logger::setSerialEnabled(false);

  if (xTaskCreate(atBridgeTask, "at_bridge", BRIDGE_TASK_STACK,
                  nullptr, BRIDGE_TASK_PRIO, &s_task) != pdPASS) {
    s_task = nullptr;
    Logger::setSerialEnabled(true);   // 启动失败则恢复串口日志，便于排查
    LOG("ATBR", "透传任务创建失败，已恢复串口日志");
    return;
  }
  LOG("ATBR", "USB AT 透传已启动，固件不再访问模组");
}

bool AtBridge::active() {
  return s_task != nullptr;
}
