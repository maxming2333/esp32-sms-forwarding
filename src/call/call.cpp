#include "call.h"
#include "push/push.h"
#include "email/email.h"
#include "sms/phone_utils.h"
#include "sim/sim_dispatcher.h"
#include "logger.h"
#include <time.h>

// ---------- 模块内部静态变量 ----------

static volatile bool          s_pending          = false;
static String                 s_callerNumber     = "未知号码";
static volatile unsigned long s_clipWaitUntilMs  = 0;
static unsigned long          s_lastNotifyMs     = 0;

// ---------- 内部：发送来电通知 ----------

static void dispatchCallNotification(const String& callerNum) {
    if (phoneMatchesBlacklist(callerNum)) {
        LOG("Call", "黑名单拦截来电，号码: %s", callerNum.c_str());
        return;
    }

    // 获取本机号码
    String selfNum = simQueryPhoneNumber(3000);

    // 格式化时间戳
    time_t now = time(nullptr);
    char ts[20];
    struct tm t;
    localtime_r(&now, &t);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &t);

    String message = "来电号码: " + callerNum + "\n时间: " + String(ts);
    String subject = "来电通知: " + callerNum;

    sendPushNotification(callerNum, message, String(ts), MSG_TYPE_CALL);
    sendEmailNotification(subject.c_str(), message.c_str());

    s_lastNotifyMs = millis();
    LOG("Call", "来电通知已发送，号码: %s", callerNum.c_str());
}

// ---------- 公共 API ----------

void callInit() {
    s_pending         = false;
    s_callerNumber    = "未知号码";
    s_clipWaitUntilMs = 0;
    s_lastNotifyMs    = 0;
}

void callHandleRING() {
    if (millis() - s_lastNotifyMs < CALL_DEDUP_MS) {
        LOG("Call", "防抖：忽略 RING（%lu ms 内已通知）", CALL_DEDUP_MS);
        return;
    }
    s_pending         = true;
    s_callerNumber    = "未知号码";
    s_clipWaitUntilMs = millis() + CALL_CLIP_WAIT_MS;
    LOG("Call", "RING 检测，等待 +CLIP");
}

void callHandleCLIP(const String& line) {
    if (!s_pending) return;

    // 解析 +CLIP: "号码",129,...
    int q1 = line.indexOf('"');
    int q2 = (q1 >= 0) ? line.indexOf('"', q1 + 1) : -1;
    if (q1 >= 0 && q2 > q1) {
        String num = line.substring(q1 + 1, q2);
        if (num.length() > 0) {
            s_callerNumber = num;
        }
    }

    s_pending = false;
    dispatchCallNotification(s_callerNumber);
}

void callTick() {
    if (s_pending && millis() >= s_clipWaitUntilMs) {
        s_pending = false;
        dispatchCallNotification("未知号码");
    }
}
