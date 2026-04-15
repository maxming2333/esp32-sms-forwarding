#include "sms.h"
#include "sim/sim.h"
#include "config/config.h"
#include "push/push.h"
#include "email/email.h"
#include "logger.h"
#include <pdulib.h>
#include <WiFi.h>

#define SERIAL_BUFFER_SIZE 500

// ---------- call notification state ----------

static bool          s_callPending        = false;
static String        s_callCallerNumber   = "";
static unsigned long s_callLastNotifyMs   = 0;
constexpr unsigned long CALL_DEDUP_MS     = 30000;

static PDU pdu = PDU(4096);
static ConcatSms concatBuffer[MAX_CONCAT_MESSAGES];

// ---------- helpers ----------

static bool isAdmin(const char* sender) {
  if (config.adminPhone.length() == 0) return false;
  String s = String(sender);
  String a = config.adminPhone;
  if (s.startsWith("+86")) s = s.substring(3);
  if (a.startsWith("+86")) a = a.substring(3);
  return s.equals(a);
}

static String assembleConcatSms(int slot) {
  String result;
  for (int i = 0; i < concatBuffer[slot].totalParts; i++) {
    if (concatBuffer[slot].parts[i].valid) {
      result += concatBuffer[slot].parts[i].text;
    } else {
      result += "[缺失分段" + String(i + 1) + "]";
    }
  }
  return result;
}

static void clearConcatSlot(int slot) {
  concatBuffer[slot].inUse         = false;
  concatBuffer[slot].receivedParts = 0;
  concatBuffer[slot].sender        = "";
  concatBuffer[slot].timestamp     = "";
  for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
    concatBuffer[slot].parts[j].valid = false;
    concatBuffer[slot].parts[j].text  = "";
  }
}

static int findOrCreateConcatSlot(int refNumber, const char* sender, int totalParts) {
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    if (concatBuffer[i].inUse &&
        concatBuffer[i].refNumber == refNumber &&
        concatBuffer[i].sender.equals(sender)) {
      return i;
    }
  }
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    if (!concatBuffer[i].inUse) {
      concatBuffer[i].inUse         = true;
      concatBuffer[i].refNumber     = refNumber;
      concatBuffer[i].sender        = String(sender);
      concatBuffer[i].totalParts    = totalParts;
      concatBuffer[i].receivedParts = 0;
      concatBuffer[i].firstPartTime = millis();
      for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
        concatBuffer[i].parts[j].valid = false;
        concatBuffer[i].parts[j].text  = "";
      }
      return i;
    }
  }
  // 找最老的槽位覆盖
  int oldestSlot = 0;
  unsigned long oldestTime = concatBuffer[0].firstPartTime;
  for (int i = 1; i < MAX_CONCAT_MESSAGES; i++) {
    if (concatBuffer[i].firstPartTime < oldestTime) {
      oldestTime = concatBuffer[i].firstPartTime;
      oldestSlot = i;
    }
  }
  LOG("SMS", "长短信缓存已满，覆盖最老的槽位");
  concatBuffer[oldestSlot].inUse         = true;
  concatBuffer[oldestSlot].refNumber     = refNumber;
  concatBuffer[oldestSlot].sender        = String(sender);
  concatBuffer[oldestSlot].totalParts    = totalParts;
  concatBuffer[oldestSlot].receivedParts = 0;
  concatBuffer[oldestSlot].firstPartTime = millis();
  for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
    concatBuffer[oldestSlot].parts[j].valid = false;
    concatBuffer[oldestSlot].parts[j].text  = "";
  }
  return oldestSlot;
}

// forward declaration
static void processSmsContent(const char* sender, const char* text, const char* timestamp);

bool sendSMSPDU(const char* phoneNumber, const char* message) {
  LOG("SMS", "准备发送短信到 %s", phoneNumber);
  pdu.setSCAnumber();
  int pduLen = pdu.encodePDU(phoneNumber, message);
  if (pduLen < 0) {
    LOG("SMS", "PDU编码失败，错误码: %d", pduLen);
    return false;
  }
  String cmgsCmd = "AT+CMGS="; cmgsCmd += pduLen;
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmgsCmd);

  unsigned long start = millis();
  bool gotPrompt = false;
  while (millis() - start < 5000) {
    if (Serial1.available()) {
      char c = Serial1.read();
      if (c == '>') { gotPrompt = true; break; }
    }
  }
  if (!gotPrompt) { LOG("SMS", "未收到>提示符"); return false; }

  Serial1.print(pdu.getSMS());
  Serial1.write(0x1A);

  start = millis();
  String resp;
  while (millis() - start < 30000) {
    while (Serial1.available()) {
      char c = Serial1.read(); resp += c;
      if (resp.indexOf("OK")    >= 0) { LOG("SMS", "短信发送成功"); return true; }
      if (resp.indexOf("ERROR") >= 0) { LOG("SMS", "短信发送失败"); return false; }
    }
  }
  LOG("SMS", "短信发送超时");
  return false;
}

// forward declaration for blacklist check
static bool phoneMatchesBlacklist(const String& incoming);

// ---------- blacklist helpers ----------

// 从本机号码中提取区号（如 "86"），用于对短号码进行区号补齐比对。
// 若本机号码未知或长度不足以推断区号，返回空字符串。
static String extractAreaCode() {
  String phone = simGetPhoneNumber();
  if (phone.length() == 0 || phone.equals("未知")) return "";
  if (phone.startsWith("+")) phone = phone.substring(1);
  if (phone.startsWith("00")) phone = phone.substring(2);
  if ((int)phone.length() <= 11) return "";
  return phone.substring(0, phone.length() - 11);
}

static String normalizePhone(const String& raw) {
  String s = raw;
  s.trim();
  if (s.startsWith("+")) s = s.substring(1);
  if (s.startsWith("00")) s = s.substring(2);
  String areaCode = extractAreaCode();
  if (areaCode.length() > 0 && (int)s.length() <= 11) {
    s = areaCode + s;
  }
  return s;
}

static bool phoneMatchesBlacklist(const String& incoming) {
  String normIn = normalizePhone(incoming);
  for (int i = 0; i < config.blacklistCount; i++) {
    if (normIn.equals(normalizePhone(config.blacklist[i]))) {
      return true;
    }
  }
  return false;
}

static void processCallNotification(const String& callerNum) {
  if (phoneMatchesBlacklist(callerNum)) {
    LOG("SMS", "黑名单拦截来电，号码: %s", callerNum.c_str());
    s_callPending = false;
    return;
  }

  // Build notification content
  time_t now = time(nullptr);
  char ts[20];
  struct tm t;
  localtime_r(&now, &t);
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &t);

  String message = "来电号码: " + callerNum + "\n时间: " + String(ts);
  String subject = "来电通知: " + callerNum;

  sendPushNotification(callerNum, message, String(ts), MSG_TYPE_CALL);
  sendEmailNotification(subject.c_str(), message.c_str());

  s_callLastNotifyMs = millis();
  s_callPending      = false;
  LOG("SMS", "来电通知已发送，号码: %s", callerNum.c_str());
}

static void processAdminCommand(const char* sender, const char* text) {
  String cmd = String(text); cmd.trim();
  LOG("SMS", "处理管理员命令: %s", cmd.c_str());

  if (cmd.startsWith("SMS:")) {
    int fc = cmd.indexOf(':');
    int sc = cmd.indexOf(':', fc + 1);
    if (sc > fc + 1) {
      String targetPhone = cmd.substring(fc + 1, sc); targetPhone.trim();
      String smsContent  = cmd.substring(sc + 1);     smsContent.trim();
      bool ok = sendSMSPDU(targetPhone.c_str(), smsContent.c_str());
      String subject = ok ? "短信发送成功" : "短信发送失败";
      String body = "命令: " + cmd + "\n目标号码: " + targetPhone + "\n结果: " + (ok ? "成功" : "失败");
      sendEmailNotification(subject.c_str(), body.c_str());
    } else {
      LOG("SMS", "SMS命令格式错误");
      sendEmailNotification("命令执行失败", "SMS命令格式错误，正确格式: SMS:号码:内容");
    }
  } else if (cmd.equals("RESET")) {
    sendEmailNotification("重启命令已执行", "收到RESET命令，即将重启ESP32...");
    delay(2000);
    ESP.restart();
  } else {
    LOG("SMS", "未知命令: %s", cmd.c_str());
  }
}

static void processSmsContent(const char* sender, const char* text, const char* timestamp) {
  LOG("SMS", "=== 处理短信内容 ===");
  LOG("SMS", "发送者: %s", sender);
  LOG("SMS", "时间戳: %s", timestamp);
  LOG("SMS", "内容: %s", text);

  // 黑名单检查
  if (phoneMatchesBlacklist(String(sender))) {
    LOG("SMS", "黑名单拦截短信，号码: %s", sender);
    return;
  }

  if (isAdmin(sender)) {
    String smsText = String(text); smsText.trim();
    if (smsText.startsWith("SMS:") || smsText.equals("RESET")) {
      processAdminCommand(sender, text);
      return;
    }
  }

  sendPushNotification(String(sender), String(text), String(timestamp), MSG_TYPE_SMS);

  String subject; subject += "短信"; subject += sender; subject += ","; subject += text;
  String body;    body    += "来自："; body += sender; body += "，时间："; body += timestamp; body += "，内容："; body += text;
  sendEmailNotification(subject.c_str(), body.c_str());
}

static String readSerialLine(HardwareSerial& port) {
  static char lineBuf[SERIAL_BUFFER_SIZE];
  static int linePos = 0;
  while (port.available()) {
    char c = port.read();
    if (c == '\n') {
      lineBuf[linePos] = 0;
      String res = String(lineBuf);
      linePos = 0;
      return res;
    } else if (c != '\r') {
      if (linePos < SERIAL_BUFFER_SIZE - 1) lineBuf[linePos++] = c;
      else linePos = 0;
    }
  }
  return "";
}

static bool isHexString(const String& str) {
  if (str.length() == 0) return false;
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
      return false;
  }
  return true;
}

// ---------- public API ----------

void initConcatBuffer() {
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    concatBuffer[i].inUse         = false;
    concatBuffer[i].receivedParts = 0;
    for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
      concatBuffer[i].parts[j].valid = false;
      concatBuffer[i].parts[j].text  = "";
    }
  }
}

void checkSerial1URC() {
  static enum { IDLE, WAIT_PDU } state = IDLE;

  String line = readSerialLine(Serial1);
  if (line.length() == 0) return;

  // SIM URC dispatch — must come before CMT processing
  if (line.startsWith("+CPIN:") || line.startsWith("+SIMCARD:")) {
    simHandleURC(line);
    return;
  }

  if (state == IDLE) {
    // 来电通知: RING URC
    if (line.equals("RING")) {
      if (millis() - s_callLastNotifyMs >= CALL_DEDUP_MS) {
        s_callPending      = true;
        s_callCallerNumber = "未知号码";
        LOG("SMS", "检测到来电RING，等待+CLIP号码");
      }
      return;
    }

    // 来电通知: +CLIP URC，提取主叫号码
    if (line.startsWith("+CLIP:")) {
      int q1 = line.indexOf('"');
      int q2 = line.indexOf('"', q1 + 1);
      if (q1 >= 0 && q2 > q1) {
        s_callCallerNumber = line.substring(q1 + 1, q2);
      }
      if (s_callPending) {
        processCallNotification(s_callCallerNumber);
      }
      return;
    }

    if (line.startsWith("+CMT:")) {
      LOG("SMS", "检测到+CMT，等待PDU数据...");
      state = WAIT_PDU;
    }
  } else if (state == WAIT_PDU) {
    if (line.length() == 0) return;

    if (isHexString(line)) {
      LOG("SMS", "收到PDU数据: %s", line.c_str());

      if (!pdu.decodePDU(line.c_str())) {
        LOG("SMS", "PDU解析失败！");
      } else {
        LOG("SMS", "PDU解析成功: 发送者=%s 时间=%s 内容=%s",
                pdu.getSender(), pdu.getTimeStamp(), pdu.getText());

        int* concatInfo = pdu.getConcatInfo();
        int refNumber   = concatInfo[0];
        int partNumber  = concatInfo[1];
        int totalParts  = concatInfo[2];

        LOG("SMS", "长短信信息: 参考号=%d 当前=%d 总计=%d", refNumber, partNumber, totalParts);

        if (totalParts > 1 && partNumber > 0) {
          LOG("SMS", "收到长短信分段 %d/%d", partNumber, totalParts);
          int slot = findOrCreateConcatSlot(refNumber, pdu.getSender(), totalParts);
          int partIndex = partNumber - 1;
          if (partIndex >= 0 && partIndex < MAX_CONCAT_PARTS) {
            if (!concatBuffer[slot].parts[partIndex].valid) {
              concatBuffer[slot].parts[partIndex].valid = true;
              concatBuffer[slot].parts[partIndex].text  = String(pdu.getText());
              concatBuffer[slot].receivedParts++;
              if (concatBuffer[slot].receivedParts == 1) {
                concatBuffer[slot].timestamp = String(pdu.getTimeStamp());
              }
              LOG("SMS", "已缓存分段 %d，当前已收到 %d/%d", partNumber, concatBuffer[slot].receivedParts, totalParts);
            } else {
              LOG("SMS", "分段 %d 已存在，跳过", partNumber);
            }
          }
          if (concatBuffer[slot].receivedParts >= totalParts) {
            LOG("SMS", "长短信已收齐，开始合并转发");
            String fullText = assembleConcatSms(slot);
            processSmsContent(concatBuffer[slot].sender.c_str(),
                              fullText.c_str(),
                              concatBuffer[slot].timestamp.c_str());
            clearConcatSlot(slot);
          }
        } else {
          processSmsContent(pdu.getSender(), pdu.getText(), pdu.getTimeStamp());
        }
      }
      state = IDLE;
    } else {
      LOG("SMS", "收到非PDU数据，返回IDLE状态");
      state = IDLE;
    }
  }
}

void checkConcatTimeout() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    if (concatBuffer[i].inUse && (now - concatBuffer[i].firstPartTime >= CONCAT_TIMEOUT_MS)) {
      LOG("SMS", "[告警] 长短信超时，丢弃不完整消息（参考号=%d，已收到=%d/%d）",
              concatBuffer[i].refNumber, concatBuffer[i].receivedParts, concatBuffer[i].totalParts);
      clearConcatSlot(i);
    }
  }
}
