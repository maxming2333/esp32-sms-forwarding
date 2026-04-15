#include "sim.h"
#include "logger.h"
#include "config/config.h"
#include "email/email.h"

// ---------- internal state ----------

static SimState s_state      = SIM_UNKNOWN;
static bool     s_needReinit = false;

// SIM info cache (populated after SIM_READY)
static String   s_carrier    = "未知";
static String   s_phoneNum   = "未知";
static String   s_signal     = "未知";

// ---------- US2: 数据流量非阻塞状态机 ----------

enum TrafficState {
  TS_IDLE,
  TS_PENDING,
  TS_WAIT_RESP,
  TS_WAIT_RETRY,
  TS_DONE,
  TS_TIMED_OUT
};

struct TrafficSM {
  TrafficState  state       = TS_IDLE;
  unsigned long triggerMs   = 0;
  unsigned long lastActionMs = 0;
  String        respBuf     = "";
};

static TrafficSM s_tsm;

// ---------- T005: AT helpers ----------

static bool sendATandWaitOK(const char* cmd, unsigned long timeout) {
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
  }
  LOG("SIM", "%s 最终失败", stepName);
  return false;
}

// ---------- US2: simTrafficTick — 非阻塞数据流量状态机 ----------

static void simTrafficTick() {
  switch (s_tsm.state) {
    case TS_PENDING: {
      String cmd = "AT+CGACT=";
      cmd += config.dataTraffic ? "1" : "0";
      cmd += ",1";
      while (Serial1.available()) Serial1.read();
      Serial1.println(cmd);
      s_tsm.respBuf      = "";
      s_tsm.lastActionMs = millis();
      s_tsm.state        = TS_WAIT_RESP;
      LOG("SIM", "数据流量: 发送 %s", cmd.c_str());
      break;
    }
    case TS_WAIT_RESP: {
      while (Serial1.available()) {
        char c = Serial1.read();
        s_tsm.respBuf += c;
      }
      if (s_tsm.respBuf.indexOf("OK") >= 0) {
        LOG("SIM", "数据流量: AT+CGACT 成功");
        s_tsm.state = TS_DONE;
      } else if (s_tsm.respBuf.indexOf("ERROR") >= 0 || millis() - s_tsm.lastActionMs > 6000) {
        LOG("SIM", "数据流量: AT+CGACT 失败或超时，3s 后重试");
        s_tsm.respBuf      = "";
        s_tsm.lastActionMs = millis();
        s_tsm.state        = TS_WAIT_RETRY;
      }
      if (millis() - s_tsm.triggerMs > 300000) {
        LOG("SIM", "数据流量: 总超时 5 分钟，放弃重试");
        s_tsm.state = TS_TIMED_OUT;
      }
      break;
    }
    case TS_WAIT_RETRY: {
      if (millis() - s_tsm.triggerMs > 300000) {
        LOG("SIM", "数据流量: 总超时 5 分钟，放弃重试");
        s_tsm.state = TS_TIMED_OUT;
      } else if (millis() - s_tsm.lastActionMs >= 3000) {
        s_tsm.respBuf = "";
        s_tsm.state   = TS_PENDING;
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

// ---------- T006 helper: CEREG polling ----------

static bool waitCEREG() {
  Serial1.println("AT+CEREG?");
  unsigned long start = millis();
  String resp;
  while (millis() - start < 2000) {
    while (Serial1.available()) { char c = Serial1.read(); resp += c; }
    if (resp.indexOf("+CEREG:") >= 0) {
      if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) return true;
      if (resp.indexOf(",0") >= 0 || resp.indexOf(",2") >= 0 ||
          resp.indexOf(",3") >= 0 || resp.indexOf(",4") >= 0) return false;
    }
  }
  return false;
}

// ---------- T006 helper: core init sequence (CGACT/CNMI/CMGF/CEREG) ----------

static bool runInitSequence() {
  if (!runInitStep("AT+CNMI=2,2,0,0,0", 1000, 3, "CNMI")) return false;
  if (!runInitStep("AT+CMGF=0", 1000, 3, "CMGF")) return false;

  runInitStep("AT+CLIP=1", 1000, 3, "CLIP");  // 启用主叫号码上报，失败不阻断初始化

  // 轮询 CEREG，最多 30 次 × 2s = 60s
  for (int i = 0; i < 30; i++) {
    if (waitCEREG()) {
      LOG("SIM", "网络已注册");
      return true;
    }
    LOG("SIM", "等待网络注册... %d/30", i + 1);
  }
  LOG("SIM", "网络注册超时");
  return false;
}

// ---------- T006: simInit ----------

void simInit() {
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

  if (runInitSequence()) {
    s_state            = SIM_READY;
    s_tsm.state        = TS_PENDING;
    s_tsm.triggerMs    = millis();
    s_tsm.lastActionMs = millis();
    s_tsm.respBuf      = "";
    LOG("SIM", "SIM 初始化成功");
  } else {
    s_state = SIM_INIT_FAILED;
    LOG("SIM", "SIM 初始化失败");
  }
}

// ---------- T007: simGetState ----------

SimState simGetState() {
  return s_state;
}

// ---------- T009 + T012: simHandleURC ----------

void simHandleURC(const String& line) {
  if (line.indexOf("+CPIN: READY") >= 0) {
    if (s_state != SIM_READY && s_state != SIM_INITIALIZING) {
      s_needReinit = true;
      LOG("SIM", "检测到 SIM 就绪 URC，等待重新初始化");

      // T028: SIM 事件通知（插入）
      if (config.simNotifyEnabled) {
        sendEmailNotification("SIM 卡已插入", "SIM 卡已就绪，设备将重新初始化 SIM 模块");
      }
    }
    return;
  }

  if (line.indexOf("+CPIN: NOT INSERTED") >= 0 || line.indexOf("+SIMCARD:0") >= 0) {
    SimState prev = s_state;
    s_state      = SIM_NOT_INSERTED;
    s_needReinit = false;
    s_phoneNum   = "";
    s_tsm        = TrafficSM{};
    LOG("SIM", "SIM 卡已拔出，状态已清除");

    // T028: SIM 事件通知（拔出）
    if (config.simNotifyEnabled && prev == SIM_READY) {
      sendEmailNotification("SIM 卡已拔出", "SIM 卡已拔出，当前状态：未插入");
    }
    return;
  }
}

// ---------- T010: simTick ----------

void simTick() {
  if (s_needReinit) {
    s_needReinit = false;
    s_state      = SIM_INITIALIZING;
    LOG("SIM", "开始热插入重新初始化");

    if (runInitSequence()) {
      s_state            = SIM_READY;
      s_tsm.state        = TS_PENDING;
      s_tsm.triggerMs    = millis();
      s_tsm.lastActionMs = millis();
      s_tsm.respBuf      = "";
      LOG("SIM", "热插入初始化成功");
    } else {
      s_state = SIM_INIT_FAILED;
      LOG("SIM", "热插入初始化失败");
    }
  }

  simTrafficTick();
}

// ---------- SIM info cache fetch (called after SIM_READY) ----------

static String sendATandRead(const char* cmd, unsigned long timeout) {
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  unsigned long start = millis();
  String resp;
  while (millis() - start < timeout) {
    while (Serial1.available()) {
      char c = Serial1.read(); resp += c;
    }
    if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0) break;
  }
  return resp;
}

void simFetchInfo() {
  // AT+COPS? → +COPS: 0,0,"中国移动",7
  {
    String resp = sendATandRead("AT+COPS?", 3000);
    int start = resp.indexOf("+COPS:");
    if (start >= 0) {
      int q1 = resp.indexOf('"', start);
      int q2 = (q1 >= 0) ? resp.indexOf('"', q1 + 1) : -1;
      if (q1 >= 0 && q2 > q1) {
        s_carrier = resp.substring(q1 + 1, q2);
        LOG("SIM", "运营商: %s", s_carrier.c_str());
      }
    }
  }

  // AT+CNUM → +CNUM: "","13900001234",129
  {
    String resp = sendATandRead("AT+CNUM", 3000);
    int start = resp.indexOf("+CNUM:");
    if (start >= 0) {
      // 找第二对引号（第一对是 alpha 名称，可为空）
      int q1 = resp.indexOf('"', start);
      int q2 = (q1 >= 0) ? resp.indexOf('"', q1 + 1) : -1;
      int q3 = (q2 >= 0) ? resp.indexOf('"', q2 + 1) : -1;
      int q4 = (q3 >= 0) ? resp.indexOf('"', q3 + 1) : -1;
      if (q3 >= 0 && q4 > q3) {
        String num = resp.substring(q3 + 1, q4);
        if (num.length() > 0) {
          s_phoneNum = num;
          LOG("SIM", "本机号码: %s", s_phoneNum.c_str());
        }
      }
    }
  }

  // AT+CSQ → +CSQ: 20,0  (CSQ 99 = unknown)
  {
    String resp = sendATandRead("AT+CSQ", 2000);
    int start = resp.indexOf("+CSQ:");
    if (start >= 0) {
      int csq = -1;
      sscanf(resp.c_str() + start + 5, " %d", &csq);
      if (csq >= 0 && csq != 99) {
        // dBm ≈ -113 + 2*csq
        int dbm = -113 + 2 * csq;
        char buf[24];
        snprintf(buf, sizeof(buf), "%d (%ddBm)", csq, dbm);
        s_signal = String(buf);
        LOG("SIM", "信号强度: %s", s_signal.c_str());
      } else {
        s_signal = "未知";
      }
    }
  }
}

// ---------- Getter functions ----------

String simGetCarrier()     { return s_carrier; }
String simGetPhoneNumber() { return s_phoneNum; }
String simGetSignal()      { return s_signal; }
