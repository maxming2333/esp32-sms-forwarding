#include "call.h"
#include "push/push.h"
#include "sms/phone_utils.h"
#include "sim/sim_dispatcher.h"
#include "call_events.h"
#include "time/time_sync.h"
#include "../logger/logger.h"

volatile bool          Call::s_pending          = false;
volatile bool          Call::s_dispatchPending  = false;
String                 Call::s_callerNumber     = "未知号码";
volatile unsigned long Call::s_clipWaitUntilMs  = 0;
unsigned long          Call::s_lastNotifyMs     = 0;
bool                   Call::s_clccAttempted    = false;
unsigned long          Call::s_clccAttemptMs    = 0;

void Call::dispatch(const String& callerNum) {
  if (phoneMatchesBlacklist(callerNum)) {
    LOG("CALL", "黑名单拦截来电，号码: %s", callerNum.c_str());
    return;
  }
  // 记录在推送之前、黑名单之后:与固件自身的推送严格同源,不会出现「推送了但外部
  // 系统查不到」或反之。黑名单拦截的来电两边都不出现。
  CallEvents::record(callerNum);
  String ts = TimeSync::dateStr();
  Push::send(callerNum, "来电号码: " + callerNum + "\n时间: " + ts, ts, MsgTypeInfo(MSG_TYPE_CALL));
  s_lastNotifyMs = millis();
  LOG("CALL", "来电通知已发送，号码: %s", callerNum.c_str());
}

void Call::init() {
  CallEvents::init();
  s_pending         = false;
  s_dispatchPending = false;
  s_callerNumber    = "未知号码";
  s_clipWaitUntilMs = 0;
  s_lastNotifyMs    = 0;
  s_clccAttempted   = false;
  s_clccAttemptMs   = 0;
}

void Call::handleRING() {
  if (millis() - s_lastNotifyMs < DEDUP_MS) {
    LOG("CALL", "防抖：忽略 RING（%lu ms 内已通知）", DEDUP_MS);
    return;
  }
  s_pending         = true;
  s_dispatchPending = false;
  s_callerNumber    = "未知号码";
  s_clipWaitUntilMs = millis() + CLIP_WAIT_MS;
  s_clccAttempted   = false;
  s_clccAttemptMs   = millis() + CLCC_DELAY_MS;
  LOG("CALL", "RING 检测，等待 +CLIP（%lu ms），%lu ms 后主动查询 AT+CLCC", CLIP_WAIT_MS, CLCC_DELAY_MS);
}

void Call::handleCLIP(const String& line) {
  String num;
  int q1 = line.indexOf('"');
  int q2 = (q1 >= 0) ? line.indexOf('"', q1 + 1) : -1;
  if (q1 >= 0 && q2 > q1) {
    num = line.substring(q1 + 1, q2);
  }

  // +CLIP: 在没有先导 RING 时曾被整条丢弃,于是一次响铃指示没被认出就丢掉整通来电。
  // 而 +CLIP: 本身已经带齐了一条通知需要的全部信息(谁、什么时候),所以这里把它
  // 当作来电本身,而不是丢掉。
  //
  // 模组每个响铃周期都会重发 RING 与 +CLIP:,所以必须套用与 RING 相同的去抖窗口,
  // 否则一通电话会变成好几条记录。
  if (!s_pending) {
    if (millis() - s_lastNotifyMs < DEDUP_MS) {
      return;
    }
    s_callerNumber    = num.length() > 0 ? num : String("号码保密");
    s_dispatchPending = true;
    LOG("CALL", "收到 +CLIP 但此前无 RING，按来电处理，号码: %s", s_callerNumber.c_str());
    return;
  }

  if (num.length() > 0 || s_callerNumber.length() == 0) {
    s_callerNumber = num.length() > 0 ? num : String("号码保密");
  }
  s_pending         = false;
  s_dispatchPending = true;
}

// handleCLCC 接收未经请求的 +CLCC:。
//
// 实测本模组不发 +CLIP:,而是在 RING 之后主动报一条 +CLCC:,所以这里是这块硬件上
// 唯一的即时号码来源 —— 没有它就只能等 3 秒后自己去查,短促来电会查空。
void Call::handleCLCC(const String& line) {
  String num = parseCLCC(line);
  if (num.length() == 0) {
    return;
  }
  // 与 handleCLIP 同样的去抖:模组每个响铃周期都会重发,一通电话不能变成好几条。
  if (!s_pending) {
    if (millis() - s_lastNotifyMs < DEDUP_MS) {
      return;
    }
    s_callerNumber    = num;
    s_dispatchPending = true;
    LOG("CALL", "收到主动 +CLCC 但此前无 RING，按来电处理，号码: %s", num.c_str());
    return;
  }
  s_callerNumber    = num;
  s_pending         = false;
  s_dispatchPending = true;
  LOG("CALL", "从主动 +CLCC 获取来电号码: %s", num.c_str());
}

// handleCallEnd 在通话结束上报到达时收口。
//
// 没有它,一通两秒就挂断的来电要等满 10 秒窗口才被记录 —— 号码其实早就拿到了。
// 已经有号码就立即派发;没有的话也不再等,主叫方已经走了,再等也不会有 +CLCC。
void Call::handleCallEnd(const String& line) {
  if (!s_pending) {
    return;
  }
  s_pending         = false;
  s_dispatchPending = true;
  LOG("CALL", "通话结束（%s），立即以已知号码派发: %s", line.c_str(), s_callerNumber.c_str());
}

String Call::parseCLCC(const String& resp) {
  int pos = 0;
  while (true) {
    int clccIdx = resp.indexOf("+CLCC:", pos);
    if (clccIdx < 0) {
      break;
    }
    int commaAfterIdx = resp.indexOf(',', clccIdx + 6);
    if (commaAfterIdx < 0) {
      break;
    }
    int commaAfterDir = resp.indexOf(',', commaAfterIdx + 1);
    if (commaAfterDir < 0) {
      break;
    }
    int q1 = resp.indexOf('"', commaAfterDir);
    int q2 = (q1 >= 0) ? resp.indexOf('"', q1 + 1) : -1;
    if (q1 >= 0 && q2 > q1) {
      String num = resp.substring(q1 + 1, q2);
      if (num.length() > 0) {
        return num;
      }
    }
    pos = clccIdx + 6;
  }
  return "";
}

void Call::tick() {
  if (s_dispatchPending) {
    s_dispatchPending = false;
    dispatch(s_callerNumber);
    return;
  }
  if (!s_pending) {
    return;
  }
  unsigned long now = millis();

  if (!s_clccAttempted && now >= s_clccAttemptMs) {
    s_clccAttempted = true;
    LOG("CALL", "主动发送 AT+CLCC 查询来电号码");
    String resp;
    SimDispatcher::sendCommand("AT+CLCC", 3000, &resp, true);
    LOG("CALL", "AT+CLCC 响应: %s", resp.c_str());
    String num = parseCLCC(resp);
    if (num.length() > 0) {
      LOG("CALL", "AT+CLCC 成功获取来电号码: %s", num.c_str());
      s_callerNumber = num;
      s_pending = false;
      dispatch(s_callerNumber);
      return;
    }
    LOG("CALL", "AT+CLCC 未获取到号码（可能已挂断或格式不符），继续等待 +CLIP");
  }

  if (now >= s_clipWaitUntilMs) {
    s_pending = false;
    dispatch(s_callerNumber);
  }
}
