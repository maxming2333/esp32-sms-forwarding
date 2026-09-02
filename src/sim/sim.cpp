#include "sim.h"
#include "sim_dispatcher.h"
#include "call/call.h"
#include "sms/sms.h"
#include "../logger/logger.h"
#include "config/config.h"
#include "push/push.h"
#include "time/time_sync.h"
#include <esp_task_wdt.h>

// ---------- internal state ----------

static SimState s_state      = SIM_UNKNOWN;
static bool     s_needReinit = false;

// SIM info cache (populated after SIM_READY)
static String   s_carrier    = "未知";
static String   s_signal     = "未知";
static String   s_phoneNum   = "未知";

// ---------- US021: 本机号码重试状态 ----------
static bool          s_numberReady   = false;
static unsigned long s_numRetryNext  = 0;

// ---------- US2: 数据流量状态机 ----------

enum TrafficState {
  TS_IDLE,
  TS_PENDING,
  TS_WAIT_RETRY,
  TS_DONE,
  TS_TIMED_OUT
};

struct TrafficSM {
  TrafficState  state       = TS_IDLE;
  unsigned long triggerMs   = 0;
  unsigned long lastActionMs = 0;
};

static TrafficSM s_tsm;

// ---------- C1: 网络注册等待状态机（非阻塞）----------
//
// 为什么不再同步等待：
//   境外漫游卡实测从 CFUN=1 到驻留小区需要 60s 以上，注册总耗时可达 90s。
//   若在 Sim::init() 里同步轮询，setup() 会被整段拖住，WiFi 与 HTTP 都要排在
//   后面才能启动（实测开机到拿到 IP 接近 1 分钟）。因此把等待拆成状态机，
//   由 Sim::tick() 驱动，init() 只负责发起。

enum RegPhase : uint8_t {
  RP_IDLE = 0,   // 未启动
  RP_WAITING,    // 轮询中
  RP_DONE,       // 已注册
  RP_FAILED      // 超时或被网络明确拒绝
};

struct RegSM {
  RegPhase      phase      = RP_IDLE;
  int           attempts   = 0;
  unsigned long nextPollMs = 0;
  int           termCause  = -1;  // 上一轮读到的终态拒绝原因
  int           termHits   = 0;   // 该原因连续出现次数
};

static RegSM s_rsm;

// 注册轮询上限：45 次 × 2000ms = 90s。
// 旧实现是 5 次 × 2000ms = 10s（日志里却写着 30 次），对境外漫游卡远远不够。
constexpr int           SIM_REG_MAX_ATTEMPTS     = 45;
constexpr unsigned long SIM_REG_POLL_INTERVAL_MS = 2000;

// ---------- T005: AT helpers ----------

static bool sendATandWaitOK(const char* cmd, unsigned long timeout) {
  if (SimDispatcher::running()) {
    return SimDispatcher::sendCommand(cmd, timeout, nullptr, false);
  }
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  unsigned long start = millis();
  String resp;
  while (millis() - start < timeout) {
    while (Serial1.available()) {
      char c = Serial1.read(); resp += c;
      if (resp.indexOf("OK")    >= 0) return true;
      if (resp.indexOf("ERROR") >= 0) return false;
    }
  }
  return false;
}

static bool runInitStep(const char* cmd, unsigned long timeout, int maxRetry, const char* stepName) {
  for (int i = 0; i < maxRetry; i++) {
    if (sendATandWaitOK(cmd, timeout)) {
      LOG("SIM", "%s 成功", stepName);
      return true;
    }
    LOG("SIM", "%s 失败，重试 %d/%d", stepName, i + 1, maxRetry);
    delay(300);
    esp_task_wdt_reset();
  }
  LOG("SIM", "%s 最终失败", stepName);
  return false;
}

// ---------- 通用 AT 交互（同时返回响应文本，兼容调度器未启动时的裸串口路径）----------

static bool atExchange(const char* cmd, unsigned long timeoutMs, String* outResp) {
  if (SimDispatcher::running()) {
    return SimDispatcher::sendCommand(cmd, timeoutMs, outResp, false);
  }
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  String resp;
  bool ok = false;
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (Serial1.available()) resp += (char)Serial1.read();
    if (resp.indexOf("OK") >= 0)    { ok = true;  break; }
    if (resp.indexOf("ERROR") >= 0) { ok = false; break; }
    delay(5);
    esp_task_wdt_reset();
  }
  if (outResp != nullptr) *outResp = resp;
  return ok;
}

// ---------- 模组会话新鲜度：等待 AT 就绪 ----------

static bool waitAtReady(unsigned long timeoutMs) {
  unsigned long start = millis();
  int attempt = 0;
  while (millis() - start < timeoutMs) {
    attempt++;
    if (atExchange("AT", 500, nullptr)) {
      LOG("SIM", "模组 AT 就绪（第 %d 次探测，耗时 %lu ms）", attempt, millis() - start);
      return true;
    }
    esp_task_wdt_reset();
    delay(200);
    esp_task_wdt_reset();
  }
  LOG("SIM", "模组 AT 在 %lu ms 内未就绪", timeoutMs);
  return false;
}

// ---------- 模组会话新鲜度：检测卡会话是否干净 ----------
//
// 判据：MANAGE CHANNEL open（00 70 00 00 01）返回的通道号是否为 1。
// 卡会话干净时首个逻辑通道必然是 1；返回 2/3 说明上次运行残留的通道仍在，
// 返回 6A81 说明通道池已被残留占满。该判据直接测的就是我们关心的东西，
// 不依赖 EN 引脚是否生效、也不依赖对模组启动耗时的猜测。
// 检测用的通道会立即关闭（close 必须用 5 字节形式，4 字节 Case-1 会被 AT 层 ERROR）。
//
// 返回：0 = 干净，1 = 有残留，-1 = 无法判断（指令失败 / 模组不支持 AT+CSIM）
static int probeCardSession() {
  String resp;
  if (!atExchange("AT+CSIM=10,\"0070000001\"", 3000, &resp)) {
    LOG("SIM", "会话检查: MANAGE CHANNEL 指令失败，无法判断");
    return -1;
  }

  int q1 = resp.indexOf('"');
  int q2 = (q1 >= 0) ? resp.indexOf('"', q1 + 1) : -1;
  if (q1 < 0 || q2 <= q1) {
    LOG("SIM", "会话检查: 响应无法解析");
    return -1;
  }
  String hex = resp.substring(q1 + 1, q2);
  hex.trim();
  hex.toUpperCase();

  if (hex.indexOf("6A81") >= 0) {
    LOG("SIM", "会话检查: 逻辑通道已耗尽(6A81)，存在残留");
    return 1;
  }
  if (hex.length() >= 6 && hex.endsWith("9000")) {
    int ch = (int)strtol(hex.substring(0, 2).c_str(), nullptr, 16);
    // 用完立即关闭，避免检测本身制造泄漏
    char closeCmd[40];
    snprintf(closeCmd, sizeof(closeCmd), "AT+CSIM=10,\"007080%02X00\"", ch);
    atExchange(closeCmd, 3000, nullptr);
    if (ch == 1) {
      LOG("SIM", "会话检查: 卡会话干净（首个逻辑通道 = 1）");
      return 0;
    }
    LOG("SIM", "会话检查: 检测到残留逻辑通道（本次分配到 %d）", ch);
    return 1;
  }

  LOG("SIM", "会话检查: 未预期的响应 %.32s", hex.c_str());
  return -1;
}

// ---------- Sim::ensureFreshModemSession ----------

void Sim::ensureFreshModemSession() {
  bool needReset = true;

  if (waitAtReady(8000)) {
    if (probeCardSession() == 0) {
      needReset = false;   // EN 引脚确实复位了模组，无需再动
    }
  } else {
    LOG("SIM", "模组无响应，直接尝试软复位");
  }

  if (!needReset) return;

  // 软复位，每次启动最多执行一次（不循环重试，避免启动被拖死）
  LOG("SIM", "模组未被真正复位，执行软复位 AT+MREBOOT");
  atExchange("AT+MREBOOT", 3000, nullptr);
  delay(500);
  esp_task_wdt_reset();

  if (!waitAtReady(15000)) {
    LOG("SIM", "软复位后模组仍未就绪，继续启动（SIM 可能不可用）");
    return;
  }
  if (probeCardSession() == 0) {
    LOG("SIM", "软复位完成，卡会话已重置");
  } else {
    LOG("SIM", "软复位后卡会话仍有残留，继续启动");
  }
}

// ---------- US2: simTrafficTick — 数据流量控制（通过调度器发送 AT 指令）----------

static void simTrafficTick() {
  switch (s_tsm.state) {
    case TS_PENDING: {
      // 检查总超时
      if (millis() - s_tsm.triggerMs > 300000) {
        LOG("SIM", "数据流量: 总超时 5 分钟，放弃重试");
        s_tsm.state = TS_TIMED_OUT;
        break;
      }
      String cmd = "AT+CGACT=";
      cmd += config.dataTraffic ? "1" : "0";
      cmd += ",1";
      LOG("SIM", "数据流量: 发送 %s", cmd.c_str());
      bool ok = SimDispatcher::sendCommand(cmd.c_str(), 6000, nullptr, false);
      if (ok) {
        LOG("SIM", "数据流量: AT+CGACT 成功");
        s_tsm.state = TS_DONE;
      } else {
        // 指令失败时查询当前状态，若上下文已处于目标状态则视为成功
        // （软重启后上下文可能已被关闭，再次关闭会返回 ERROR）
        String cgactResp;
        bool alreadyInDesiredState = false;
        if (SimDispatcher::sendCommand("AT+CGACT?", 3000, &cgactResp, false)) {
          String desiredPattern = String("+CGACT: 1,") + (config.dataTraffic ? "1" : "0");
          if (cgactResp.indexOf(desiredPattern) >= 0) {
            alreadyInDesiredState = true;
          }
        }
        if (alreadyInDesiredState) {
          LOG("SIM", "数据流量: 上下文已处于目标状态，无需重试");
          s_tsm.state = TS_DONE;
        } else {
          LOG("SIM", "数据流量: AT+CGACT 失败或超时，3s 后重试");
          s_tsm.lastActionMs = millis();
          s_tsm.state        = TS_WAIT_RETRY;
        }
      }
      break;
    }
    case TS_WAIT_RETRY: {
      if (millis() - s_tsm.triggerMs > 300000) {
        LOG("SIM", "数据流量: 总超时 5 分钟，放弃重试");
        s_tsm.state = TS_TIMED_OUT;
      } else if (millis() - s_tsm.lastActionMs >= 3000) {
        s_tsm.state = TS_PENDING;
      }
      break;
    }
    case TS_IDLE:
    case TS_DONE:
    case TS_TIMED_OUT:
    default:
      break;
  }
}

// ---------- T006 helper: 网络注册状态查询（CREG/CGREG 兜底用）----------

static bool queryRegState(const char* cmd, const char* prefix) {
  String resp;
  if (SimDispatcher::running()) {
    SimDispatcher::sendCommand(cmd, 2000, &resp, false);
  } else {
    Serial1.println(cmd);
    unsigned long start = millis();
    while (millis() - start < 2000) {
      while (Serial1.available()) { char c = Serial1.read(); resp += c; }
      if (resp.indexOf(prefix) >= 0) break;
    }
  }
  int pfx = resp.indexOf(prefix);
  if (pfx < 0) return false;

  // 响应格式为 "+CxREG: <n>,<stat>[,...]"，必须精确取出 <stat> 字段再比较数值，
  // 不能用子串匹配（例如 ",1" 会误命中 ",11" ",15" 等两位数 stat，导致误判为已注册）
  int commaIdx = resp.indexOf(',', pfx);
  if (commaIdx < 0) return false;
  int statStart = commaIdx + 1;
  int statEnd   = statStart;
  while (statEnd < (int)resp.length() && isDigit(resp[statEnd])) statEnd++;
  if (statEnd == statStart) return false;
  int stat = resp.substring(statStart, statEnd).toInt();
  return stat == 1 || stat == 5;  // 1=已注册(本地网)，5=已注册(漫游)
}

// ---------- B1/B5 helper: EMM 拒绝原因（3GPP TS 24.301 §9.9.3.9）----------

static const char* emmCauseDesc(int cause) {
  switch (cause) {
    case 3:  return "非法 UE";
    case 6:  return "非法 ME（IMEI 不被接受）";
    case 7:  return "不允许 EPS 业务（该订阅未开通数据）";
    case 8:  return "不允许 EPS 与非 EPS 业务";
    case 11: return "PLMN 不允许接入";
    case 12: return "跟踪区不允许";
    case 13: return "该跟踪区不允许漫游";
    case 14: return "本 PLMN 不允许 EPS 业务（无数据漫游权限）";
    case 15: return "跟踪区内无合适小区";
    case 16: return "MSC 暂不可达";
    case 17: return "网络故障";
    case 18: return "CS 域不可用";
    case 19: return "ESM 失败（PDN/APN 请求被拒）";
    case 22: return "网络拥塞";
    default: return "未列举";
  }
}

// 网络已明确拒绝该卡接入，重试不会改变结果，应立即判定失败。
// 其余原因（15 无合适小区、16 MSC 不可达、17 网络故障、19 ESM 失败、22 拥塞等）
// 属于可能自愈的瞬态情形，继续重试到超时为止。
static bool isTerminalRejectCause(int cause) {
  switch (cause) {
    case 3:   // 非法 UE
    case 6:   // 非法 ME
    case 7:   // 不允许 EPS 业务
    case 8:   // 不允许 EPS 与非 EPS 业务
    case 11:  // PLMN 不允许
    case 14:  // 本 PLMN 不允许 EPS 业务
      return true;
    default:
      return false;
  }
}

// ---------- B1/B4 helper: 查询 CEREG，同时取出 <stat> 与 EMM <reject_cause> ----------
//
// 读命令响应格式（<n>=3/5 时才带原因）：
//   +CEREG: <n>,<stat>[,<tac>,<ci>,<AcT>[,<cause_type>,<reject_cause>]]
// 主动上报（URC）少一个 <n> 字段：
//   +CEREG: <stat>,<tac>,<ci>,<AcT>[,<cause_type>,<reject_cause>]
// "+CEREG:" 不在 SimDispatcher 的 URC 过滤名单里，所以响应缓冲中可能同时混入
// 一条 URC 和一条读命令响应，需要按字段个数区分两种形态。
//
// outStat / outCause 无法取得时置 -1。返回 false 表示整行都没解析出来。
// outFields 回传切出的字段个数，供调用方在多行之间挑选可信度最高的一行。
static bool parseCeregLine(const String& line, int* outStat, int* outCause, int* outFields) {
  int pfx = line.indexOf("+CEREG:");
  if (pfx < 0) return false;

  String body = line.substring(pfx + 7);
  int nl = body.indexOf('\n');
  if (nl >= 0) body = body.substring(0, nl);
  body.trim();

  // 按逗号切分，最多 7 段
  String f[7];
  int nField = 0;
  int start  = 0;
  for (int i = 0; i <= (int)body.length() && nField < 7; i++) {
    if (i == (int)body.length() || body[i] == ',') {
      f[nField] = body.substring(start, i);
      f[nField].trim();
      nField++;
      start = i + 1;
    }
  }
  if (nField == 0) return false;
  if (outFields) *outFields = nField;

  int statIdx, causeIdx;
  if (nField >= 7) {
    // 读命令响应且带原因：<n>,<stat>,<tac>,<ci>,<AcT>,<cause_type>,<reject_cause>
    statIdx = 1; causeIdx = 6;
  } else if (nField == 6) {
    // URC 且带原因：<stat>,<tac>,<ci>,<AcT>,<cause_type>,<reject_cause>
    statIdx = 0; causeIdx = 5;
  } else if (nField >= 2) {
    // 读命令响应但未带原因（<n>=0/1/2/4）
    statIdx = 1; causeIdx = -1;
  } else {
    // 仅 <stat> 的精简 URC
    statIdx = 0; causeIdx = -1;
  }

  const String& statStr = f[statIdx];
  if (statStr.length() == 0) return false;
  for (int i = 0; i < (int)statStr.length(); i++) {
    if (!isDigit(statStr[i])) return false;
  }
  if (outStat) *outStat = statStr.toInt();

  if (outCause) {
    *outCause = -1;
    if (causeIdx >= 0 && f[causeIdx].length() > 0) {
      bool allDigit = true;
      for (int i = 0; i < (int)f[causeIdx].length(); i++) {
        if (!isDigit(f[causeIdx][i])) { allDigit = false; break; }
      }
      if (allDigit) *outCause = f[causeIdx].toInt();
    }
  }
  return true;
}

// 发一次 AT+CEREG? 并解析。缓冲里可能同时混入 CEREG 主动上报（少一个 <n> 字段）
// 和读命令响应，因此在所有 "+CEREG:" 行中挑字段数最多的那一行——字段数为 7 才是
// 带 reject cause 的读命令响应，可信度最高；字段数相同时取靠后的一行。
static bool queryCeregDetail(int* outStat, int* outCause) {
  String resp;
  if (SimDispatcher::running()) {
    SimDispatcher::sendCommand("AT+CEREG?", 2000, &resp, false);
  } else {
    Serial1.println("AT+CEREG?");
    unsigned long start = millis();
    while (millis() - start < 2000) {
      while (Serial1.available()) { char c = Serial1.read(); resp += c; }
      if (resp.indexOf("+CEREG:") >= 0) break;
    }
  }

  bool found      = false;
  int  bestStat   = -1;
  int  bestCause  = -1;
  int  bestFields = -1;
  int  searchPos  = 0;
  while (true) {
    int pfx = resp.indexOf("+CEREG:", searchPos);
    if (pfx < 0) break;
    int lineEnd = resp.indexOf('\n', pfx);
    String line = (lineEnd < 0) ? resp.substring(pfx) : resp.substring(pfx, lineEnd);
    searchPos   = (lineEnd < 0) ? resp.length() : lineEnd + 1;

    int stat = -1, cause = -1, nField = 0;
    if (parseCeregLine(line, &stat, &cause, &nField) && nField >= bestFields) {
      bestStat   = stat;
      bestCause  = cause;
      bestFields = nField;
      found      = true;
    }
  }

  if (!found) return false;
  if (outStat)  *outStat  = bestStat;
  if (outCause) *outCause = bestCause;
  return true;
}

// ---------- T006 helper: 模组配置（必须在 CFUN=1 射频恢复后调用，
//            CFUN=4→1 切换会重置这些非持久化设置）----------

static bool runModemConfig() {
  // 强制自动选网模式，避免模组残留上一张卡的手动选网/漫游 PLMN 锁定状态，
  // 导致换卡后无法重新搜网注册；部分模组该指令耗时较长，失败不阻断初始化
  runInitStep("AT+COPS=0", 5000, 1, "COPS自动选网");

  // B1: 打开带 EMM reject cause 的注册状态上报。
  // 只有 <n>=3/5 会在 +CEREG 中附带 <cause_type>,<reject_cause>，
  // 注册失败时这是唯一能区分"搜不到网 / 被网络拒绝 / APN 请求被拒"的信息。
  // 部分模组不支持 <n>=3，降级到 <n>=2（仅状态，无原因）。
  if (!runInitStep("AT+CEREG=3", 1000, 2, "CEREG上报(带拒绝原因)")) {
    runInitStep("AT+CEREG=2", 1000, 2, "CEREG上报(降级为仅状态)");
  }

  // B2: 定义空 APN 的默认 PDP 上下文。
  // LTE 的 Attach Request 会捎带 PDN Connectivity Request，模组默认使用固件内置
  // APN（通常是国内 APN）。漫游时该 APN 未被订阅授权会导致整个 Attach 被拒
  // （实测 ML307C 上表现为 EMM cause 19 ESM failure），留空让网络下发最安全。
  // 注意：该配置**不写入模组 NVM**，模组重启即丢失（实测 AT+MREBOOT 后 cause
  // 会从 15 退回 19），因此必须放在每次初始化都会执行的路径里。
  runInitStep("AT+CGDCONT=1,\"IP\",\"\"", 1000, 2, "默认PDP上下文(空APN)");

  // 短信消费归属由「胖/瘦模式」决定，见 Config::thinModeEnabled。
  //   胖模式 CNMI=2,2：新短信直投 TE 成为 +CMT: URC，**不进模组存储**，由本固件
  //                    解析并推送。此模式下外部系统的 AT+CMGL 永远是空的。
  //   瘦模式 CNMI=2,1：新短信落入存储，只给一条 +CMTI: 指示。本固件不消费，
  //                    外部系统轮询 CMGL/CMGR 取走并 CMGD 删除。
  const char* cnmi = config.thinModeEnabled ? "AT+CNMI=2,1,0,0,0" : "AT+CNMI=2,2,0,0,0";
  if (!runInitStep(cnmi, 1000, 3, "CNMI")) return false;
  if (!runInitStep("AT+CMGF=0", 1000, 3, "CMGF")) return false;
  runInitStep("AT+CLIP=1", 1000, 3, "CLIP");   // 启用主叫号码上报，失败不阻断初始化
  runInitStep("AT+CUSD=1", 1000, 3, "CUSD");   // 启用 USSD 主动上报，失败不阻断初始化
  return true;
}

// ---------- C1: 注册等待状态机 ----------

static void startRegWait() {
  s_rsm.phase      = RP_WAITING;
  s_rsm.attempts   = 0;
  s_rsm.nextPollMs = millis();
  LOG("SIM", "等待网络注册（最多 %d 次 × %lu ms ≈ %lu s）",
      SIM_REG_MAX_ATTEMPTS, SIM_REG_POLL_INTERVAL_MS,
      (unsigned long)SIM_REG_MAX_ATTEMPTS * SIM_REG_POLL_INTERVAL_MS / 1000);
}

static void onRegSuccess() {
  s_rsm.phase = RP_DONE;
  // 网络就绪后尝试 SIM 时间同步
  TimeSync::syncFromSIM();
  s_state            = SIM_READY;
  s_tsm.state        = TS_PENDING;
  s_tsm.triggerMs    = millis();
  s_tsm.lastActionMs = millis();
  LOG("SIM", "网络已注册，SIM 初始化成功");
}

static void onRegFailure(const char* reason) {
  s_rsm.phase = RP_FAILED;
  s_state     = SIM_INIT_FAILED;
  LOG("SIM", "SIM 初始化失败：%s", reason);
}

// 由 Sim::tick() 每轮调用；仅在 RP_WAITING 阶段做事，单次最多发 1~3 条 AT 指令。
static void regWaitTick() {
  if (s_rsm.phase != RP_WAITING) return;
  if ((long)(millis() - s_rsm.nextPollMs) < 0) return;
  s_rsm.nextPollMs = millis() + SIM_REG_POLL_INTERVAL_MS;

  // B4: CEREG 优先。ML307 系列为 LTE only 产品，实测 AT+CREG? / AT+CGREG? /
  // AT+CEER 均返回 ERROR，只有 CEREG 有效；且 CEREG 这一条就能同时拿到
  // <stat> 与 reject cause，正常路径每轮只需一次 AT 往返。
  int  stat  = -1;
  int  cause = -1;
  bool gotCereg   = queryCeregDetail(&stat, &cause);
  bool registered = gotCereg && (stat == 1 || stat == 5);

  if (!gotCereg) {
    // CEREG 不可用（非 LTE 模组）时退回 2G/3G 的电路域/分组域注册状态
    registered = queryRegState("AT+CREG?", "+CREG:")
              || queryRegState("AT+CGREG?", "+CGREG:");
  }

  if (registered) { onRegSuccess(); return; }

  char reason[128];

  // B5: 被网络明确拒绝时提前失败，不再重试到超时。
  // 要求同一个终态原因连续出现两轮才判定，避免正常卡遇到一次瞬态异常上报就被判死。
  if (isTerminalRejectCause(cause)) {
    if (cause == s_rsm.termCause) {
      s_rsm.termHits++;
    } else {
      s_rsm.termCause = cause;
      s_rsm.termHits  = 1;
    }
    if (s_rsm.termHits >= 2) {
      snprintf(reason, sizeof(reason), "网络拒绝注册（EMM cause %d: %s）",
               cause, emmCauseDesc(cause));
      onRegFailure(reason);
      return;
    }
  } else {
    s_rsm.termCause = -1;
    s_rsm.termHits  = 0;
  }

  s_rsm.attempts++;
  if (s_rsm.attempts >= SIM_REG_MAX_ATTEMPTS) {
    if (cause >= 0) {
      snprintf(reason, sizeof(reason), "网络注册超时（stat=%d，EMM cause %d: %s）",
               stat, cause, emmCauseDesc(cause));
    } else {
      snprintf(reason, sizeof(reason), "网络注册超时（stat=%d）", stat);
    }
    onRegFailure(reason);
    return;
  }

  if (cause >= 0) {
    LOG("SIM", "等待网络注册... %d/%d（stat=%d，EMM cause %d: %s）",
        s_rsm.attempts, SIM_REG_MAX_ATTEMPTS, stat, cause, emmCauseDesc(cause));
  } else {
    LOG("SIM", "等待网络注册... %d/%d（stat=%d）",
        s_rsm.attempts, SIM_REG_MAX_ATTEMPTS, stat);
  }
}

// ---------- T006: Sim::init ----------

void Sim::init() {
  // 检测 SIM 卡是否存在（AT+CPIN? 3000ms）
  while (Serial1.available()) Serial1.read();
  Serial1.println("AT+CPIN?");
  unsigned long start = millis();
  String resp;
  bool gotResponse = false;
  while (millis() - start < 3000) {
    while (Serial1.available()) {
      char c = Serial1.read(); resp += c;
    }
    if (resp.indexOf("+CPIN: READY") >= 0) {
      gotResponse = true; break;
    }
    if (resp.indexOf("ERROR") >= 0 || resp.indexOf("NOT INSERTED") >= 0) {
      break;
    }
  }

  if (!gotResponse || resp.indexOf("+CPIN: READY") < 0) {
    s_state = SIM_NOT_INSERTED;
    LOG("SIM", "未检测到 SIM 卡，跳过初始化");
    return;
  }

  LOG("SIM", "SIM 卡就绪，开始初始化");
  s_state = SIM_INITIALIZING;

  // 步骤1: 所有准备完成，恢复射频上线
  // 注意：CFUN=4→1 切换会重置 CNMI/CMGF/CLIP 等非持久化设置，
  //       因此 runModemConfig 必须放在 CFUN=1 之后立即执行
  sendATandWaitOK("AT+CFUN=1", 3000);
  LOG("SIM", "射频已恢复 (CFUN=1)");

  delay(300);

  // 步骤2: 查询本机号码（AT+CNUM 读 SIM 卡 EF 文件，不需要射频）
  {
    String num = Sim::queryPhoneNumber(3000);
    if (num.length() > 0) {
      s_phoneNum    = num;
      s_numberReady = true;
      LOG("SIM", "首次号码查询成功: %s", num.c_str());
    } else {
      s_numberReady  = false;
      s_numRetryNext = millis() + SIM_NUMBER_RETRY_INTERVAL_MS;
      LOG("SIM", "首次号码查询失败，%lu ms 后再试", SIM_NUMBER_RETRY_INTERVAL_MS);
    }
  }

  // 步骤3: 重新配置模组参数（CFUN=1 会重置这些设置，必须在此补全）
  if (!runModemConfig()) {
    s_state = SIM_INIT_FAILED;
    LOG("SIM", "SIM 初始化失败（模组配置阶段）");
    return;
  }

  // 步骤4: 发起网络注册等待后立即返回，不在此阻塞。
  // 注册最长可达 90s（境外漫游卡实测 >60s 才驻留小区），若在此同步等待会把
  // setup() 后续的 WiFi/HTTP 一起拖住。轮询交给 Sim::tick() → regWaitTick()，
  // 注册成功/失败时才会把 s_state 推进到 SIM_READY / SIM_INIT_FAILED。
  startRegWait();
}

// ---------- Sim::state ----------

SimState Sim::state() {
  return s_state;
}

// ---------- Sim::handleURC ----------

void Sim::handleURC(const String& line) {
  if (line.indexOf("+CPIN: READY") >= 0) {
    if (s_state != SIM_READY && s_state != SIM_INITIALIZING) {
      s_needReinit = true;
      LOG("SIM", "检测到 SIM 就绪 URC，等待重新初始化");
      if (config.simNotifyEnabled) {
        Push::send("设备", "SIM 卡已就绪，设备将重新初始化 SIM 模块", TimeSync::dateStr(), MsgTypeInfo(MSG_TYPE_SIM));
      }
    }
    return;
  }

  if (line.indexOf("+CPIN: NOT INSERTED") >= 0 || line.indexOf("+SIMCARD:0") >= 0) {
    SimState prev = s_state;
    s_state      = SIM_NOT_INSERTED;
    s_needReinit = false;
    s_tsm        = TrafficSM{};
    s_rsm        = RegSM{};   // 停止注册轮询，避免对着已拔出的卡继续发 AT
    LOG("SIM", "SIM 卡已拔出，状态已清除");
    if (config.simNotifyEnabled && prev == SIM_READY) {
      Push::send("设备", "SIM 卡已拔出，当前状态：未插入", TimeSync::dateStr(), MsgTypeInfo(MSG_TYPE_SIM));
    }
    return;
  }
}

// ---------- Sim::tick ----------

void Sim::tick() {
  if (s_needReinit) {
    s_needReinit = false;
    s_state      = SIM_INITIALIZING;
    LOG("SIM", "开始热插入重新初始化");
    // 与开机路径一致：配置完成后交给注册状态机轮询，不在 loop 里阻塞 90s
    if (runModemConfig()) {
      startRegWait();
    } else {
      s_rsm.phase = RP_FAILED;
      s_state     = SIM_INIT_FAILED;
      LOG("SIM", "热插入初始化失败（模组配置阶段）");
    }
  }

  regWaitTick();
  simTrafficTick();

  // T008: 本机号码重试查询（独立于 SIM 状态，只要调度器在线且号码未就绪就持续重试）
  if (!s_numberReady && SimDispatcher::running() && millis() >= s_numRetryNext) {
    LOG("SIM", "本机号码重试查询...");
    String num = Sim::queryPhoneNumber(3000);
    if (num.length() > 0) {
      s_phoneNum     = num;
      s_numberReady  = true;
      s_numRetryNext = ULONG_MAX;
      LOG("SIM", "本机号码更新: %s", num.c_str());
    } else {
      s_numberReady  = false;
      s_numRetryNext = millis() + SIM_NUMBER_RETRY_INTERVAL_MS;
      LOG("SIM", "本机号码重试失败，%lu ms 后再试", SIM_NUMBER_RETRY_INTERVAL_MS);
    }
  }
}

// ---------- Sim::fetchInfo ----------

static String normalizeCarrier(const String& raw) {
  String lower = raw;
  lower.trim();
  lower.toLowerCase();
  if (lower == "cmcc" || lower == "china mobile" || lower == "46000" ||
      lower == "46002" || lower == "46007" || lower == "46008" || lower == "46020") {
    return "中国移动";
  }
  if (lower == "cucc" || lower == "china unicom" || lower == "chn-unicom" ||
      lower == "46001" || lower == "46006" || lower == "46009") {
    return "中国联通";
  }
  if (lower == "ctcc" || lower == "china telecom" || lower == "chn-ct" ||
      lower == "46003" || lower == "46005" || lower == "46011") {
    return "中国电信";
  }
  return raw;
}

void Sim::fetchInfo() {
  {
    String resp;
    SimDispatcher::sendCommand("AT+COPS?", 3000, &resp, false);
    int start = resp.indexOf("+COPS:");
    if (start >= 0) {
      int q1 = resp.indexOf('"', start);
      int q2 = (q1 >= 0) ? resp.indexOf('"', q1 + 1) : -1;
      if (q1 >= 0 && q2 > q1) {
        s_carrier = normalizeCarrier(resp.substring(q1 + 1, q2));
        LOG("SIM", "运营商: %s", s_carrier.c_str());
      }
    }
  }
  {
    String resp;
    SimDispatcher::sendCommand("AT+CSQ", 2000, &resp, false);
    int start = resp.indexOf("+CSQ:");
    if (start >= 0) {
      int csq = -1;
      sscanf(resp.c_str() + start + 5, " %d", &csq);
      if (csq >= 0 && csq != 99) {
        int dbm = -113 + 2 * csq;
        char buf[24];
        snprintf(buf, sizeof(buf), "%ddBm", dbm);
        s_signal = String(buf);
        LOG("SIM", "信号强度: %s", s_signal.c_str());
      } else {
        s_signal = "未知";
      }
    }
  }
  {
    String num = Sim::queryPhoneNumber(3000);
    if (num.length() > 0) {
      s_phoneNum     = num;
      s_numberReady  = true;
      s_numRetryNext = ULONG_MAX;
      LOG("SIM", "本机号码: %s", s_phoneNum.c_str());
    } else {
      s_numberReady  = false;
      s_numRetryNext = millis() + SIM_NUMBER_RETRY_INTERVAL_MS;
    }
  }
}

// ---------- Getter functions ----------

String Sim::carrier()  { return s_carrier; }
String Sim::signal()   { return s_signal; }
String Sim::phoneNum() { return s_phoneNum; }
bool Sim::isNumberReady() { return s_numberReady; }

// ---------- URC 路由（由 SIM reader task 回调调用） ----------

static void onUrc(SimUrcType type, const String& line) {
  switch (type) {
    case SimUrcType::RING:       Call::handleRING();          break;
    case SimUrcType::CLIP:       Call::handleCLIP(line);      break;
    case SimUrcType::CMT:        Sms::handleCMTHeader();      break;
    case SimUrcType::CMT_PDU:    Sms::handlePDU(line);        break;
    case SimUrcType::CUSD:       Sms::handleUSSD(line);       break;
    case SimUrcType::CPIN_READY: Sim::handleURC(line);        break;
    // 瘦模式：短信已入存储，消费方是外部系统。这里只留一条记录便于排查，
    // 绝不读取或删除——两个消费者会导致短信随机丢向其中一方。
    case SimUrcType::CMTI:
      LOG("SIM", "瘦模式：新短信已入模组存储（%s），等待外部系统取走", line.c_str());
      break;
    // 模组自己重启了。CNMI / CMGF / CGDCONT 等都不写入模组 NVM，重启即丢失：
    // 若不重新下发，瘦模式会静默退回默认上报方式，外部系统再也收不到短信，而两侧
    // 日志都正常。复用热插入重新初始化路径（不能在 reader task 上下文里发 AT）。
    case SimUrcType::MATREADY:
      LOG("SIM", "检测到模组重启（%s），将重新下发模组配置", line.c_str());
      s_needReinit = true;
      break;
    case SimUrcType::SIM_REMOVE: Sim::handleURC(line);        break;
    default:                                                 break;
  }
}

// ---------- Sim::startReaderTask ----------

void Sim::startReaderTask() {
  Sms::startProcTask();          // 先启动 sms_proc 任务和队列
  SimDispatcher::registerUrcCallback(onUrc);
  SimDispatcher::start();
  LOG("SIM", "SIM reader task 已启动");
}

// ---------- Sim::queryPhoneNumber ----------
// 业务方法：通过 AT+CNUM 查询本机 MSISDN，并解析模组返回。
// 调度仅由 SimDispatcher 负责（保证串口互斥与队列），解析归属于业务模块。

String Sim::queryPhoneNumber(unsigned long timeoutMs) {
  String resp;
  if (!SimDispatcher::sendCommand("AT+CNUM", timeoutMs, &resp, false)) {
    return "";
  }

  int start = resp.indexOf("+CNUM:");
  if (start < 0) {
    return "";
  }

  // +CNUM: "","13900001234",129
  // 第一对引号为 alpha 名称（可为空），第二对为实际号码
  int q1 = resp.indexOf('"', start);
  int q2 = (q1 >= 0) ? resp.indexOf('"', q1 + 1) : -1;
  int q3 = (q2 >= 0) ? resp.indexOf('"', q2 + 1) : -1;
  int q4 = (q3 >= 0) ? resp.indexOf('"', q3 + 1) : -1;
  if (q3 >= 0 && q4 > q3) {
    return resp.substring(q3 + 1, q4);
  }

  // 兜底：部分模组 alpha 字段无引号，格式为 +CNUM: ,"13900001234",type
  int idx = resp.indexOf(",\"", start);
  if (idx >= 0) {
    int ei = resp.indexOf('"', idx + 2);
    if (ei > idx + 2) {
      return resp.substring(idx + 2, ei);
    }
  }
  return "";
}
