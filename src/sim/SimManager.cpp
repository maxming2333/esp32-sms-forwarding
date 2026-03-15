#include "SimManager.h"
#include "config/AppConfig.h"
#include "wifi/WifiManager.h"
#include "email/EmailNotifier.h"

bool   simPresent       = false;
bool   simInitialized   = false;
String devicePhoneNumber = "未知号码";

// ── LED ───────────────────────────────────────────────────────────────────────
void blinkShort(unsigned long gapMs) {
  digitalWrite(LED_BUILTIN, LOW);
  delay(50);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(gapMs);
}

// ── Modem power control ───────────────────────────────────────────────────────
void modemPowerCycle() {
  pinMode(MODEM_EN_PIN, OUTPUT);
  Serial.println("[SIM] EN 拉低：关闭模组");
  digitalWrite(MODEM_EN_PIN, LOW);
  delay(1200);
  Serial.println("[SIM] EN 拉高：开启模组");
  digitalWrite(MODEM_EN_PIN, HIGH);
  delay(6000);  // wait for full boot
}

void resetModule() {
  Serial.println("[SIM] 硬重启模组（EN断电）...");
  modemPowerCycle();
  while (Serial1.available()) Serial1.read();
  bool ok = false;
  for (int i = 0; i < 10; i++) {
    if (sendATandWaitOK("AT", 1000)) { ok = true; break; }
    Serial.println("[SIM] AT未响应，继续等待...");
  }
  Serial.println(ok ? "[SIM] 模组AT恢复正常" : "[SIM] 模组AT仍未响应");
}

// ── AT primitives ─────────────────────────────────────────────────────────────
bool sendATandWaitOK(const char* cmd, unsigned long timeoutMs) {
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  unsigned long t = millis();
  String resp;
  while (millis() - t < timeoutMs) {
    while (Serial1.available()) {
      resp += (char)Serial1.read();
      if (resp.indexOf("OK")    >= 0) return true;
      if (resp.indexOf("ERROR") >= 0) return false;
    }
  }
  return false;
}

String sendATCommand(const char* cmd, unsigned long timeoutMs) {
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  unsigned long t = millis();
  String resp;
  while (millis() - t < timeoutMs) {
    while (Serial1.available()) {
      resp += (char)Serial1.read();
      if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0) {
        delay(50);
        while (Serial1.available()) resp += (char)Serial1.read();
        return resp;
      }
    }
  }
  return resp;
}

// ── SIM detection ─────────────────────────────────────────────────────────────
bool checkSIMPresent() {
  while (Serial1.available()) Serial1.read();
  Serial1.println("AT+CPIN?");
  unsigned long t = millis();
  String resp;
  while (millis() - t < 3000) {
    while (Serial1.available()) resp += (char)Serial1.read();
    if (resp.indexOf("+CPIN:") >= 0 || resp.indexOf("ERROR") >= 0) {
      delay(50);
      while (Serial1.available()) resp += (char)Serial1.read();
      break;
    }
  }
  Serial.println("[SIM] CPIN? → " + resp);
  return resp.indexOf("READY") >= 0;
}

// ── Network registration ──────────────────────────────────────────────────────
bool waitCEREG() {
  Serial1.println("AT+CEREG?");
  unsigned long t = millis();
  String resp;
  while (millis() - t < 2000) {
    while (Serial1.available()) resp += (char)Serial1.read();
    if (resp.indexOf("+CEREG:") >= 0) {
      if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) return true;
      if (resp.indexOf(",0") >= 0 || resp.indexOf(",2") >= 0 ||
          resp.indexOf(",3") >= 0 || resp.indexOf(",4") >= 0) return false;
    }
  }
  return false;
}

// ── Full SIM-dependent init ───────────────────────────────────────────────────
bool initSIMDependent() {
  Serial.println("========== 开始SIM卡初始化 ==========");

  int retry = 0;

  // Only attempt PDP deactivation when a SIM is actually present.
  // Without a SIM the modem returns CME ERROR immediately and retrying is pointless.
  if (!checkSIMPresent()) {
    Serial.println("[SIM] ⚠️ 未检测到SIM卡，跳过CGACT");
  } else {
    // Disable PDP to avoid unexpected data charges
    retry = 0;
    while (true) {
      String cgactResp = sendATCommand("AT+CGACT=0,1", 5000);
      if (cgactResp.indexOf("OK") >= 0) break;
      // CME/CMS ERROR means the modem rejected the command (e.g. no context active).
      // There is no point retrying – just move on.
      if (cgactResp.indexOf("ERROR") >= 0) {
        cgactResp.trim();
        Serial.println("[SIM] ⚠️ CGACT 返回错误，跳过: " + cgactResp);
        break;
      }
      Serial.println("[SIM] CGACT=0 失败，重试...");
      blinkShort();
      if (++retry >= 10) { Serial.println("[SIM] ⚠️ CGACT 多次失败，继续"); break; }
    }
  }

  // Enable new-SMS URCs (direct PDU to serial)
  retry = 0;
  while (!sendATandWaitOK("AT+CNMI=2,2,0,0,0", 1000)) {
    blinkShort();
    if (++retry >= 15) {
      Serial.println("[SIM] ❌ CNMI 设置失败，初始化中止");
      return false;
    }
  }
  Serial.println("[SIM] CNMI参数设置完成");

  // Set PDU mode
  retry = 0;
  while (!sendATandWaitOK("AT+CMGF=0", 1000)) {
    blinkShort();
    if (++retry >= 15) {
      Serial.println("[SIM] ❌ CMGF 设置失败，初始化中止");
      return false;
    }
  }
  Serial.println("[SIM] PDU模式设置完成");

  // Wait for LTE registration (≤60 s)
  retry = 0;
  while (!waitCEREG()) {
    Serial.println("[SIM] 等待网络注册...");
    blinkShort();
    if (++retry >= 40) {
      Serial.println("[SIM] ❌ 网络注册超时");
      return false;
    }
    if (!checkSIMPresent()) {
      Serial.println("[SIM] ⚠️ SIM在等待期间被拔出");
      return false;
    }
  }
  Serial.println("[SIM] 网络已注册");

  // Query own number
  Serial.println("[SIM] 尝试获取本机号码...");
  String cnumResp = sendATCommand("AT+CNUM", 2000);
  Serial.println("[SIM] CNUM响应: " + cnumResp);
  if (cnumResp.indexOf("+CNUM:") >= 0) {
    int a = cnumResp.indexOf(",\"");
    if (a >= 0) {
      int b = cnumResp.indexOf("\"", a + 2);
      if (b > a) devicePhoneNumber = cnumResp.substring(a + 2, b);
    }
  }
  if (devicePhoneNumber != "未知号码" && devicePhoneNumber.length() > 0) {
    // Auto-detect succeeded – write back to manualPhone so the UI echoes it
    Serial.println("[SIM] 本机号码（自动获取）: " + devicePhoneNumber);
    if (config.manualPhone != devicePhoneNumber) {
      config.manualPhone = devicePhoneNumber;
      saveConfig();
      Serial.println("[SIM] 本机号码已回写至配置");
    }
  } else if (config.manualPhone.length() > 0) {
    // Fall back to the manually configured number
    devicePhoneNumber = config.manualPhone;
    Serial.println("[SIM] 使用手动配置的本机号码: " + devicePhoneNumber);
  } else {
    Serial.println("[SIM] 无法获取本机号码，请在管理页面手动配置");
  }

  simInitialized = true;
  Serial.println("========== SIM卡初始化完成 ==========");

  if (configValid) {
    String body = "SIM卡初始化成功\n本机号码: " + devicePhoneNumber
                + "\n设备地址: " + getDeviceUrl();
    emailNotify("短信转发器：SIM卡已就绪", body.c_str());
  }
  return true;
}

