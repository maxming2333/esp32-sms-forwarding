#include "SmsSender.h"

PDU pdu = PDU(4096);

bool smsSend(const char* phoneNumber, const char* message) {
  Serial.println("[SMS] 准备发送短信...");
  Serial.println("[SMS] 目标: " + String(phoneNumber));
  Serial.println("[SMS] 内容: " + String(message));

  pdu.setSCAnumber();
  int pduLen = pdu.encodePDU(phoneNumber, message);
  if (pduLen < 0) {
    Serial.println("[SMS] PDU编码失败，错误码: " + String(pduLen));
    return false;
  }
  Serial.println("[SMS] PDU数据: " + String(pdu.getSMS()) + " (长度=" + String(pduLen) + ")");

  // Send AT+CMGS command
  while (Serial1.available()) Serial1.read();
  Serial1.println("AT+CMGS=" + String(pduLen));

  // Wait for > prompt
  unsigned long t = millis();
  bool gotPrompt = false;
  while (millis() - t < 5000) {
    if (Serial1.available() && Serial1.read() == '>') { gotPrompt = true; break; }
  }
  if (!gotPrompt) { Serial.println("[SMS] 未收到>提示符"); return false; }

  // Send PDU + Ctrl+Z
  Serial1.print(pdu.getSMS());
  Serial1.write(0x1A);

  // Wait for OK/ERROR
  t = millis();
  String resp;
  while (millis() - t < 30000) {
    while (Serial1.available()) {
      resp += (char)Serial1.read();
      if (resp.indexOf("OK")    >= 0) { Serial.println("[SMS] 发送成功"); return true;  }
      if (resp.indexOf("ERROR") >= 0) { Serial.println("[SMS] 发送失败"); return false; }
    }
  }
  Serial.println("[SMS] 发送超时");
  return false;
}

