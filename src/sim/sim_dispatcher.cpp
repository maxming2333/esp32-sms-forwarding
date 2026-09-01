#include "sim_dispatcher.h"
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <strings.h>
#include <new>
#include "../logger/logger.h"

// ---------- 文件级私有状态与辅助函数（匿名命名空间） ----------

namespace {

QueueHandle_t     s_queue             = nullptr;
TaskHandle_t      s_task              = nullptr;
SemaphoreHandle_t s_directTxnMutex    = nullptr;
SimUrcCallback    s_urcCb             = nullptr;
SimCmdSlot*       s_activeCmd         = nullptr;
unsigned long     s_cmdStartMs        = 0;
volatile bool     s_pauseRequested    = false;
volatile bool     s_readerPaused      = false;
bool              s_drainAfterTimeout = false;
unsigned long     s_lastRxMs          = 0;

// CMT PDU 行检测状态（是否等待 PDU 数据行）
bool              s_waitingPdu        = false;

bool isFinalOkLine(const String& line) {
    String s = line;
    s.trim();
    return s.equals("OK");
}

bool isFinalErrorLine(const String& line) {
    String s = line;
    s.trim();
    return s.equals("ERROR") || s.startsWith("+CME ERROR") || s.startsWith("+CMS ERROR");
}

// ---------- 内部 URC 识别 ----------

bool isUrcLine(const String& line) {
    if (line.equals("RING"))                   return true;
    if (line.startsWith("+CLIP:"))             return true;
    if (line.startsWith("+CMT:"))              return true;
    if (s_waitingPdu)                          return true;
    if (line.indexOf("+CPIN:") >= 0)           return true;
    if (line.startsWith("+SIMCARD:"))          return true;
    if (line.startsWith("+CUSD:"))             return true;
    return false;
}

// ---------- 已发出命令的「自身信息响应」识别 ----------

// 从命令推导它自己的信息响应前缀：AT+CPIN? → "+CPIN:"，AT+CGDCONT=1 → "+CGDCONT:"。
// 只做纯语法推导，不含任何命令知识表。
void expectedInfoPrefix(const char* cmd, char* out, size_t outSize) {
    if (out == nullptr || outSize < 3) return;
    out[0] = '\0';
    if (cmd == nullptr) return;
    const char* p = cmd;
    while (*p == ' ') p++;
    if (strncasecmp(p, "AT", 2) != 0) return;
    p += 2;
    if (*p != '+' && *p != '#' && *p != '$' && *p != '^' && *p != '&') return;
    size_t i = 0;
    out[i++] = *p++;
    while (*p != '\0' && *p != '=' && *p != '?' && *p != ';' && i + 2 < outSize) {
        out[i++] = *p++;
    }
    out[i++] = ':';
    out[i]   = '\0';
}

// 活跃命令的信息响应，是否被 isUrcLine() 误判为主动上报。
//
// 背景：+CPIN: 既是主动上报（插卡就绪）也是 AT+CPIN? 的信息响应。isUrcLine() 只看
// 行本身，因此在 AT+CPIN? 在途时会把它自己的应答当成 URC 吞掉，调用方只拿到 "OK"，
// SIM 插卡状态查询因此永远得不到结论。判据用「该行前缀是否等于本命令自身的响应
// 前缀」——这是唯一无需命令知识表就能区分的信号。
//
// 等待 PDU 数据行时一律不认定为信息响应：那一行属于上一条 +CMT: 上报。
bool solicitedInfoLine(const SimCmdSlot* slot, const String& line) {
    if (slot == nullptr || s_waitingPdu) return false;
    char prefix[40];
    expectedInfoPrefix(slot->cmd, prefix, sizeof(prefix));
    if (prefix[0] == '\0') return false;
    return line.startsWith(prefix);
}

// ---------- 内部 URC 路由 ----------

void routeURC(const String& line) {
    if (s_urcCb == nullptr) return;

    if (line.equals("RING")) {
        s_urcCb(SimUrcType::RING, line);
        return;
    }
    if (line.startsWith("+CLIP:")) {
        s_urcCb(SimUrcType::CLIP, line);
        return;
    }
    if (line.startsWith("+CMT:")) {
        s_waitingPdu = true;
        s_urcCb(SimUrcType::CMT, line);
        return;
    }
    if (s_waitingPdu) {
        s_waitingPdu = false;
        s_urcCb(SimUrcType::CMT_PDU, line);
        return;
    }
    if (line.indexOf("+CPIN: READY") >= 0) {
        s_urcCb(SimUrcType::CPIN_READY, line);
        return;
    }
    if (line.indexOf("+CPIN: NOT INSERTED") >= 0 || line.startsWith("+SIMCARD:0")) {
        s_urcCb(SimUrcType::SIM_REMOVE, line);
        return;
    }
    if (line.startsWith("+CUSD:")) {
        s_urcCb(SimUrcType::CUSD, line);
        return;
    }
}

void appendResponseLine(SimCmdSlot* slot, const String& line) {
    size_t existing = strnlen(slot->respBuf, SIM_RESP_BUF_SIZE);
    if (existing >= SIM_RESP_BUF_SIZE - 1) return;

    size_t remaining = (SIM_RESP_BUF_SIZE - 1) - existing;
    size_t copyLen = line.length();
    if (copyLen > remaining) copyLen = remaining;
    if (copyLen > 0) {
        memcpy(slot->respBuf + existing, line.c_str(), copyLen);
        existing += copyLen;
        slot->respBuf[existing] = '\0';
    }

    if (existing < SIM_RESP_BUF_SIZE - 1) {
        slot->respBuf[existing++] = '\n';
        slot->respBuf[existing] = '\0';
    }
}

// ---------- SIM reader task ----------

void simReaderTask(void*) {
    String lineBuf;
    // 预分配：SMS PDU hex 串典型约 340 字符；AT+CSIM 的长响应行可达约 530 字符，
    // 按 SIM_LINE_BUF_MAX 预留避免反复扩容
    lineBuf.reserve(SIM_LINE_BUF_MAX);

    for (;;) {
        if (s_pauseRequested && s_activeCmd == nullptr) {
            s_readerPaused = true;
            while (s_pauseRequested) {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            s_readerPaused = false;
        }

        // 读取 Serial1 字符，按行处理
        while (Serial1.available()) {
            char c = (char)Serial1.read();
            s_lastRxMs = millis();
            if (c == '\n') {
                String line = lineBuf;
                lineBuf = "";

                if (line.length() == 0) continue;

                if (s_activeCmd != nullptr) {
                    // T015: 有活跃指令时先检查是否为 URC 行；但本命令自身的信息
                    // 响应不能被当作 URC 吞掉（见 solicitedInfoLine 注释）。
                    if (isUrcLine(line) && !solicitedInfoLine(s_activeCmd, line)) {
                        LOG("SIMDSP", "[URC-during-cmd] %s", line.c_str());
                        routeURC(line);
                    } else {
                        appendResponseLine(s_activeCmd, line);

                        if (isFinalOkLine(line)) {
                            s_activeCmd->isOk = true;
                            xSemaphoreGive(s_activeCmd->doneSem);
                            s_activeCmd = nullptr;
                        } else if (isFinalErrorLine(line)) {
                            s_activeCmd->isOk = false;
                            xSemaphoreGive(s_activeCmd->doneSem);
                            s_activeCmd = nullptr;
                        }
                    }
                } else {
                    routeURC(line);
                }
            } else if (c != '\r') {
                lineBuf += c;
                if (lineBuf.length() > SIM_LINE_BUF_MAX) {
                    LOG("SIMDSP", "串口行超过 %u 字节，已丢弃", (unsigned)SIM_LINE_BUF_MAX);
                    lineBuf = "";
                    s_waitingPdu = false;
                }
            }
        }

        // 取下一条命令（若当前无活跃命令）
        if (s_activeCmd == nullptr && !s_pauseRequested) {
            if (s_drainAfterTimeout) {
                if (millis() - s_lastRxMs < SIM_TIMEOUT_DRAIN_QUIET_MS) {
                    vTaskDelay(pdMS_TO_TICKS(5));
                    continue;
                }
                s_drainAfterTimeout = false;
            }
            SimCmdSlot* ptr = nullptr;
            if (xQueueReceive(s_queue, &ptr, 0) == pdTRUE && ptr != nullptr) {
                s_activeCmd   = ptr;
                s_cmdStartMs  = millis();
                Serial1.println(s_activeCmd->cmd);
            }
        }

        // 超时检测
        if (s_activeCmd != nullptr &&
            millis() - s_cmdStartMs > s_activeCmd->timeoutMs) {
            LOG("SIMDSP", "AT 指令超时: %.96s", s_activeCmd->cmd);
            s_activeCmd->isOk = false;
            xSemaphoreGive(s_activeCmd->doneSem);
            s_activeCmd = nullptr;
            s_drainAfterTimeout = true;
            s_lastRxMs = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

}  // namespace

// ---------- 公共 API 实现 ----------

void SimDispatcher::registerUrcCallback(SimUrcCallback cb) {
    s_urcCb = cb;
}

void SimDispatcher::start() {
    s_queue = xQueueCreate(SIM_CMD_QUEUE_SIZE, sizeof(SimCmdSlot*));
    if (s_queue == nullptr) {
        LOG("SIMDSP", "SimDispatcher::start: 队列创建失败");
        return;
    }
    s_directTxnMutex = xSemaphoreCreateMutex();
    if (s_directTxnMutex == nullptr) {
        LOG("SIMDSP", "SimDispatcher::start: 直接事务互斥锁创建失败");
        vQueueDelete(s_queue);
        s_queue = nullptr;
        return;
    }
    xTaskCreate(simReaderTask, "sim_reader", SIM_READER_TASK_STACK,
                nullptr, SIM_READER_TASK_PRIORITY, &s_task);
}

bool SimDispatcher::sendCommand(const char* cmd, unsigned long timeoutMs,
                    String* outResp, bool prio) {
    if (s_queue == nullptr) return false;
    if (cmd == nullptr) return false;

    size_t cmdLen = strlen(cmd);
    if (cmdLen >= SIM_CMD_BUF_SIZE) {
        // AT 指令超长会被静默截断 → 模组返回 ERROR 难以排查；改为直接拒绝
        LOG("SIMDSP", "AT 指令超长（%u ≥ %u），拒绝执行: %.64s...",
            (unsigned)cmdLen, (unsigned)SIM_CMD_BUF_SIZE, cmd);
        return false;
    }

    // 堆分配：缓冲放大到能容纳完整 APDU 后 SimCmdSlot 约 1.2KB，
    // 继续放在调用方栈上会给 async_tcp / loopTask 带来近 1KB 的额外栈压力。
    SimCmdSlot* slot = new (std::nothrow) SimCmdSlot();
    if (slot == nullptr) {
        LOG("SIMDSP", "AT 指令槽分配失败（堆不足）: %.64s", cmd);
        return false;
    }

    memcpy(slot->cmd, cmd, cmdLen);
    slot->cmd[cmdLen] = '\0';
    slot->timeoutMs   = timeoutMs;
    slot->respBuf[0]  = '\0';
    slot->isOk        = false;
    slot->priority    = prio;

    slot->doneSem = xSemaphoreCreateBinary();
    if (slot->doneSem == nullptr) {
        delete slot;
        return false;
    }

    BaseType_t sent;
    if (prio) {
        sent = xQueueSendToFront(s_queue, &slot, pdMS_TO_TICKS(100));
    } else {
        sent = xQueueSendToBack(s_queue, &slot, pdMS_TO_TICKS(100));
    }

    if (sent != pdTRUE) {
        vSemaphoreDelete(slot->doneSem);
        delete slot;
        return false;
    }

    // 必须一直等到 reader task 给信号量，不可自行超时：
    // 若 SimDispatcher::sendCommand 提前返回并 delete 掉 slot，
    // reader task 之后再 xSemaphoreGive(s_activeCmd->doneSem) 将访问
    // 悬空指针，导致崩溃。reader task 内部已有 timeoutMs 超时机制，
    // 最终一定会 Give 信号量（OK / ERROR / 超时三路均有 Give）。
    // reader task 在三条路径上都是「先解引用、再 Give、最后把 s_activeCmd 置空」，
    // Give 之后不再解引用，因此本函数被唤醒后 delete 是安全的。
    //
    // 但不能用 portMAX_DELAY 一次性长睡：本函数会在 async_tcp 任务上下文中被
    // HTTP 控制器调用（/at、/ping、/flight），而 async_tcp 已订阅 TWDT，
    // Arduino-ESP32 的 TWDT 默认超时为 5s。一条 5000ms 超时的 AT 指令
    // （如 AT+COPS=? 全网扫描）会让该任务整整 5s 不喂狗，直接触发
    // "Task watchdog got triggered" 而 abort 重启。
    // 因此改为分段等待：每 200ms 醒一次喂狗，但**永不提前返回**。
    // 注：未订阅 TWDT 的任务（如 sim_reader）调用 esp_task_wdt_reset()
    // 会返回 ESP_ERR_NOT_FOUND，无副作用，故忽略返回值。
    while (xSemaphoreTake(slot->doneSem, pdMS_TO_TICKS(200)) != pdTRUE) {
        esp_task_wdt_reset();
    }
    vSemaphoreDelete(slot->doneSem);

    bool ok = slot->isOk;
    if (outResp != nullptr) {
        *outResp = String(slot->respBuf);
    }
    delete slot;
    return ok;
}

bool SimDispatcher::pauseReader(unsigned long timeoutMs) {
    // Reader task 不存在时一律拒绝独占。
    // 旧实现在此返回 true（语义是「没什么要暂停的，可以直接用串口」），但在
    // USB AT 透传模式下 SimDispatcher 根本不会启动，调用方拿到 true 后会裸写
    // Serial1（/ping 的 AT+MPING、短信发送的 AT+CMGS），从而与透传任务抢串口、
    // 污染送给 USB 主机的 AT 流。
    // 正常流程中 startReaderTask() 之后 s_task 必然非空，且在此之前没有任何
    // 调用点，因此改为返回 false 不影响既有路径。
    if (s_task == nullptr) return false;
    if (s_directTxnMutex == nullptr) return false;
    if (xSemaphoreTake(s_directTxnMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        LOG("SIMDSP", "等待直接串口事务锁超时");
        return false;
    }
    s_pauseRequested = true;
    unsigned long start = millis();
    while (!s_readerPaused) {
        if (millis() - start >= timeoutMs) {
            s_pauseRequested = false;
            xSemaphoreGive(s_directTxnMutex);
            LOG("SIMDSP", "等待 reader 暂停超时");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return true;
}

void SimDispatcher::resumeReader() {
    s_pauseRequested = false;
    if (s_directTxnMutex != nullptr) {
        xSemaphoreGive(s_directTxnMutex);
    }
}

bool SimDispatcher::running() {
    return s_queue != nullptr;
}
