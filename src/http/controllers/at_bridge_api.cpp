#include "at_bridge_api.h"
#include "../json_response.h"
#include "../body_accumulator.h"
#include "../../sim/sim_dispatcher.h"
#include "../../sim/at_bridge.h"
#include "../../logger/logger.h"
#include <ArduinoJson.h>
#include <esp_random.h>

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
static constexpr unsigned long BRIDGE_TIMEOUT_MAX_MS     = 180000;
static constexpr unsigned long BRIDGE_TIMEOUT_DEFAULT_MS = 5000;

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
    } else if (requestedTimeout > static_cast<long long>(BRIDGE_TIMEOUT_MAX_MS)) {
      timeoutMs = BRIDGE_TIMEOUT_MAX_MS;
    } else {
      timeoutMs = static_cast<unsigned long>(requestedTimeout);
    }
  }

  // 阻塞调用可能持续到 timeoutMs，因此前后各续期一次：期间对端不会再有请求，
  // 只靠调用前的续期会让长命令一结束就撞上过期。
  renewSession();
  String raw;
  SimDispatcher::sendCommand(command.c_str(), static_cast<uint32_t>(timeoutMs), &raw, false);
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
