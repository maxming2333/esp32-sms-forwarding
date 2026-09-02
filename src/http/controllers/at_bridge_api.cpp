#include "at_bridge_api.h"
#include "../json_response.h"
#include "../body_accumulator.h"
#include "../../sim/sim_dispatcher.h"
#include "../../sim/at_bridge.h"
#include "../../logger/logger.h"
#include <ArduinoJson.h>
#include <esp_random.h>
#include <esp_task_wdt.h>

// 会话有效期。每条命令前后都会续期，因此只在「对端消失」时才真正到期。
// 取 30s 是为了让一次丢包造成的泄漏窗口足够短：对端的 DELETE 是 best-effort，
// 若没有自动回收，一次丢包就会永久占住串口。
static constexpr uint32_t BRIDGE_SESSION_TTL_MS = 30000;

// 请求体上限。契约里最大的一条命令是透传 APDU 的 AT+CSIM=522,"<522 hex>"，
// 加上 token 与超时字段，2 KiB 有充足余量。
static constexpr size_t BRIDGE_BODY_MAX_BYTES = 2048;

// 命令长度上限由 dispatcher 的命令缓冲决定。契约允许对端发到 1024 字节，本桥
// 只能接受 SIM_CMD_BUF_SIZE-1；越界必须拒绝而不是截断——截断后的 AT 命令可能
// 是另一条合法命令。
static constexpr size_t BRIDGE_CMD_MAX_LEN = SIM_CMD_BUF_SIZE - 1;

static constexpr unsigned long BRIDGE_TIMEOUT_MIN_MS     = 100;
static constexpr unsigned long BRIDGE_TIMEOUT_DEFAULT_MS = 5000;

// 桥自己的阻塞上限，**故意小于契约允许的调用方超时**。
//
// 这两个 handler 运行在 async_tcp 单任务上下文，等待期间整个 HTTP 服务器停摆。
// 对端的短信派发预算是 120s（Simplus agentapi.SMSDispatchTimeout），照它阻塞会让
// 本设备近两分钟完全无响应——实测表现为该请求本身超时、且紧随其后的请求全部失败，
// 一次回环测试 5 次里 4 次因此失败。
//
// 因此无论调用方要多久，桥只占用自己这么长时间，超出即回 504（结果不确定）。
// 契约规定对端必须把 exchange 的 504 视为「已发出但结果未知」而不得重试，所以这个
// 上限是安全的：它把不确定性显式化，而不是把设备拖死。
// 30s 与固件原生短信路径（sms.cpp）一致，那条路径已长期验证。
constexpr unsigned long BRIDGE_EXCHANGE_MAX_MS = 30000;

// 普通命令的上限。Simplus 侧最长的短信命令是列举（10s），探测里最长的是 CFUN（12s），
// APDU 透传 5s，20s 留有余量。
constexpr unsigned long BRIDGE_COMMAND_MAX_MS = 20000;

// 会话状态。HTTP 处理全部在 async_tcp 单任务上下文，因此不需要额外互斥。
static char          s_session[17]     = {0};
static unsigned long s_sessionDeadline = 0;

static bool sessionAlive() {
  if (s_session[0] == '\0') return false;
  // 有符号差值比较，millis() 回绕时依然正确。
  return static_cast<long>(millis() - s_sessionDeadline) < 0;
}

static void renewSession() { s_sessionDeadline = millis() + BRIDGE_SESSION_TTL_MS; }

static void dropSession() {
  s_session[0]      = '\0';
  s_sessionDeadline = 0;
}

bool atBridgeSessionActive() { return sessionAlive(); }

// ── POST /at/session ────────────────────────────────────────────────
void atBridgeSessionOpenController(AsyncWebServerRequest* request) {
  // USB 透传模式下固件根本不启动 dispatcher，串口归 USB 主机所有。
  if (AtBridge::active() || !SimDispatcher::running()) {
    JsonResp::err(request, 503, "模组 AT 通道当前不可用");
    return;
  }
  if (sessionAlive()) {
    JsonResp::err(request, 409, "AT 会话已被占用");
    return;
  }
  // token 形态必须落在对端的 ^[A-Za-z0-9._~-]{1,128}$ 内；16 位十六进制满足。
  snprintf(s_session, sizeof(s_session), "%08x%08x",
           static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random()));
  renewSession();
  LOG("ATBRG", "远程 AT 会话已开启");
  JsonResp::build(request, [](JsonObject root) {
    root["session"]     = s_session;
    root["expiresInMs"] = BRIDGE_SESSION_TTL_MS;
  });
}

// ── POST /at/command ────────────────────────────────────────────────
void atBridgeCommandController(AsyncWebServerRequest* request, uint8_t* data,
                               size_t len, size_t index, size_t total) {
  const char* body = nullptr;
  if (!httpAccumulateBody(request, data, len, index, total, BRIDGE_BODY_MAX_BYTES, &body)) return;
  if (body == nullptr) return;

  JsonDocument doc;
  bool malformed = deserializeJson(doc, body) != DeserializationError::Ok || !doc.is<JsonObject>();
  String   session;
  String   command;
  long long requestedTimeout = 0;
  if (!malformed) {
    session          = String(doc["session"] | "");
    command          = String(doc["command"] | "");
    requestedTimeout = doc["timeoutMs"] | 0LL;
  }
  httpReleaseAccumulatedBody(request);

  if (malformed) {
    JsonResp::err(request, 400, "请求格式错误");
    return;
  }
  if (!sessionAlive() || session != s_session) {
    // 契约要求陈旧 token 一律拒绝而不是服务。这正是 APDU 序列的隔离依据。
    JsonResp::err(request, 410, "AT 会话已失效");
    return;
  }
  if (command.length() == 0 || command.length() > BRIDGE_CMD_MAX_LEN ||
      command.indexOf('\r') >= 0 || command.indexOf('\n') >= 0) {
    JsonResp::err(request, 400, "AT 命令为空、越界或含换行");
    return;
  }

  unsigned long timeoutMs = BRIDGE_TIMEOUT_DEFAULT_MS;
  if (requestedTimeout > 0) {
    if (requestedTimeout < static_cast<long long>(BRIDGE_TIMEOUT_MIN_MS)) {
      timeoutMs = BRIDGE_TIMEOUT_MIN_MS;
    } else if (requestedTimeout > static_cast<long long>(BRIDGE_COMMAND_MAX_MS)) {
      timeoutMs = BRIDGE_COMMAND_MAX_MS;
    } else {
      timeoutMs = static_cast<unsigned long>(requestedTimeout);
    }
  }

  // 阻塞调用可能持续到 timeoutMs，因此前后各续期一次：期间对端不会再有请求，
  // 只靠调用前的续期会让长命令一结束就撞上过期。
  renewSession();
  String raw;
  // 用大响应容量:对端会发 AT+CMGL 全量列举短信存储,默认 640 字节在攒到 2 条正常
  // 长度短信时就截断,而截断会破坏整个转录并让对端拒绝整批 —— 既读不出也排空不掉。
  SimDispatcher::sendCommand(command.c_str(), static_cast<uint32_t>(timeoutMs), &raw, false,
                             SIM_RESP_LARGE_BUF_SIZE);
  renewSession();

  if (raw.length() == 0) {
    // 没拿到终止状态。契约要求返回 504，绝不能回一个缺少终止状态的 200——
    // 对端会把那种响应判为失败，且丢掉了「是超时」这条信息。
    JsonResp::err(request, 504, "模组无响应");
    return;
  }

  AsyncJsonResponse* response = new AsyncJsonResponse();
  JsonArray lines = response->getRoot().to<JsonObject>()["lines"].to<JsonArray>();
  int cursor = 0;
  while (cursor < static_cast<int>(raw.length())) {
    int breakAt = cursor;
    while (breakAt < static_cast<int>(raw.length()) &&
           raw[breakAt] != '\r' && raw[breakAt] != '\n') {
      breakAt++;
    }
    String line = raw.substring(cursor, breakAt);
    line.trim();
    if (line.length() > 0) lines.add(line);
    cursor = breakAt + 1;
  }
  response->setLength();
  request->send(response);
}

// ── DELETE /at/session ──────────────────────────────────────────────
void atBridgeSessionCloseController(AsyncWebServerRequest* request, uint8_t* data,
                                   size_t len, size_t index, size_t total) {
  const char* body = nullptr;
  if (!httpAccumulateBody(request, data, len, index, total, BRIDGE_BODY_MAX_BYTES, &body)) return;
  if (body == nullptr) return;

  JsonDocument doc;
  String session;
  if (deserializeJson(doc, body) == DeserializationError::Ok && doc.is<JsonObject>()) {
    session = String(doc["session"] | "");
  }
  httpReleaseAccumulatedBody(request);

  // 只有持有当前 token 的一方能释放，否则任何已认证调用方都能抢锁。
  // 无论是否匹配都回 204：对端把关闭当作 best-effort，不需要区分。
  if (sessionAlive() && session == s_session) {
    dropSession();
    LOG("ATBRG", "远程 AT 会话已释放");
  }
  request->send(204);
}


// ── POST /at/exchange ───────────────────────────────────────────────
//
// 3GPP TS 27.005 里有一类命令不是「请求/响应」：AT+CMGS 先回一个 '>' 提示符，
// 调用方再写入 PDU 载荷。这条端点把那种交互作为一个原子操作暴露出去。
//
// 状态码承载了对端最关心的一件事——载荷到底发出去没有：
//   412  确定没写出去（没等到提示符、模组先回了终止状态、串口拿不到）
//        对端据此判定「操作无副作用」，可以安全重试
//   200  拿到终止状态，响应行在 body 里
//   504  载荷已写出但没等到终止状态 → 结果不确定，对端不得重试
// 把 412 和 504 混为一谈会导致短信重复发送，所以这个区分不是锦上添花。

static constexpr size_t        BRIDGE_PAYLOAD_MAX_BYTES = 1024;
static constexpr unsigned long BRIDGE_PROMPT_TIMEOUT_MS = 5000;

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

void atBridgeExchangeController(AsyncWebServerRequest* request, uint8_t* data,
                                size_t len, size_t index, size_t total) {
  const char* body = nullptr;
  if (!httpAccumulateBody(request, data, len, index, total, BRIDGE_BODY_MAX_BYTES, &body)) return;
  if (body == nullptr) return;

  JsonDocument doc;
  bool malformed = deserializeJson(doc, body) != DeserializationError::Ok || !doc.is<JsonObject>();
  String    session;
  String    command;
  String    payloadHex;
  long long requestedTimeout = 0;
  if (!malformed) {
    session          = String(doc["session"] | "");
    command          = String(doc["command"] | "");
    payloadHex       = String(doc["payloadHex"] | "");
    requestedTimeout = doc["timeoutMs"] | 0LL;
  }
  httpReleaseAccumulatedBody(request);

  if (malformed) { JsonResp::err(request, 400, "请求格式错误"); return; }
  if (!sessionAlive() || session != s_session) {
    JsonResp::err(request, 410, "AT 会话已失效");
    return;
  }
  if (command.length() == 0 || command.length() > BRIDGE_CMD_MAX_LEN ||
      command.indexOf('\r') >= 0 || command.indexOf('\n') >= 0) {
    JsonResp::err(request, 400, "AT 命令为空、越界或含换行");
    return;
  }
  if (payloadHex.length() < 2 || payloadHex.length() % 2 != 0 ||
      payloadHex.length() / 2 > BRIDGE_PAYLOAD_MAX_BYTES) {
    JsonResp::err(request, 400, "载荷长度非法");
    return;
  }

  const size_t payloadLen = payloadHex.length() / 2;
  uint8_t* payload = (uint8_t*)malloc(payloadLen);
  if (payload == nullptr) { JsonResp::err(request, 500, "内存不足"); return; }
  for (size_t i = 0; i < payloadLen; i++) {
    int hi = hexNibble(payloadHex[2 * i]), lo = hexNibble(payloadHex[2 * i + 1]);
    if (hi < 0 || lo < 0) {
      free(payload);
      JsonResp::err(request, 400, "载荷不是合法十六进制");
      return;
    }
    payload[i] = (uint8_t)((hi << 4) | lo);
  }

  unsigned long timeoutMs = BRIDGE_TIMEOUT_DEFAULT_MS;
  if (requestedTimeout > 0) {
    if (requestedTimeout < (long long)BRIDGE_TIMEOUT_MIN_MS)        timeoutMs = BRIDGE_TIMEOUT_MIN_MS;
    else if (requestedTimeout > (long long)BRIDGE_EXCHANGE_MAX_MS) timeoutMs = BRIDGE_EXCHANGE_MAX_MS;
    else                                                            timeoutMs = (unsigned long)requestedTimeout;
  }

  renewSession();
  // 提示符交互必须裸写 Serial1：dispatcher 的命令槽只认「命令 → 终止状态」，
  // 表达不了中途的 '>'。因此独占 reader，失败即视为「未发出」。
  if (!SimDispatcher::pauseReader()) {
    free(payload);
    JsonResp::err(request, 412, "SIM 串口忙，载荷未发出");
    return;
  }
  while (Serial1.available()) Serial1.read();
  Serial1.println(command);

  unsigned long start = millis();
  bool gotPrompt = false, earlyTerminal = false;
  String pre;
  while (millis() - start < BRIDGE_PROMPT_TIMEOUT_MS) {
    esp_task_wdt_reset();
    if (!Serial1.available()) {
      // 让出 CPU：本回调运行在 async_tcp 任务上下文，纯自旋会触发该任务看门狗。
      delay(5);
      continue;
    }
    char c = Serial1.read();
    if (c == '>') { gotPrompt = true; break; }
    pre += c;
    if (pre.length() > 256) pre.remove(0, pre.length() - 256);
    // 模组在提示符之前就给出终止状态 = 命令被拒，载荷从未被邀请写入。
    if (pre.indexOf("ERROR") >= 0) { earlyTerminal = true; break; }
  }
  if (!gotPrompt) {
    SimDispatcher::resumeReader();
    free(payload);
    renewSession();
    JsonResp::err(request, 412, earlyTerminal ? "模组在提示符前返回错误，载荷未发出" : "未收到提示符，载荷未发出");
    return;
  }

  Serial1.write(payload, payloadLen);
  free(payload);

  // 载荷已出。从这里开始任何失败都是「结果不确定」，绝不能回 412。
  start = millis();
  String raw;
  bool terminal = false;
  while (millis() - start < timeoutMs) {
    esp_task_wdt_reset();
    if (!Serial1.available()) { delay(5); continue; }
    while (Serial1.available()) {
      raw += (char)Serial1.read();
      if (raw.length() > 1024) raw.remove(0, raw.length() - 1024);
    }
    if (raw.indexOf("\nOK") >= 0 || raw.indexOf("ERROR") >= 0) { terminal = true; break; }
  }
  SimDispatcher::resumeReader();
  renewSession();

  if (!terminal) { JsonResp::err(request, 504, "载荷已发出但未收到终止状态"); return; }

  AsyncJsonResponse* response = new AsyncJsonResponse();
  JsonArray lines = response->getRoot().to<JsonObject>()["lines"].to<JsonArray>();
  int cursor = 0;
  while (cursor < (int)raw.length()) {
    int breakAt = cursor;
    while (breakAt < (int)raw.length() && raw[breakAt] != '\r' && raw[breakAt] != '\n') breakAt++;
    String line = raw.substring(cursor, breakAt);
    line.trim();
    if (line.length() > 0 && line != command) lines.add(line);
    cursor = breakAt + 1;
  }
  response->setLength();
  request->send(response);
}
