#include "EmailNotifier.h"
#include "config/AppConfig.h"
#include <WiFiClientSecure.h>

// These macros MUST be defined before ReadyMail.h is included.
// Keeping them here (not in the header) prevents the ReadyMail global
// from being multiply-defined across translation units.
#define ENABLE_SMTP
#define ENABLE_DEBUG
#include <ReadyMail.h>

static WiFiClientSecure  _ssl;
static SMTPClient        _smtp(_ssl);

void emailInit() {
  _ssl.setInsecure();
}

void emailNotify(const char* subject, const char* body) {
  if (config.smtpServer.length() == 0 || config.smtpUser.length() == 0 ||
      config.smtpPass.length() == 0   || config.smtpSendTo.length() == 0) {
    Serial.println("[Email] 配置不完整，跳过发送");
    return;
  }

  auto cb = [](SMTPStatus s) { Serial.println(s.text); };
  _smtp.connect(config.smtpServer.c_str(), config.smtpPort, cb);

  if (!_smtp.isConnected()) {
    Serial.println("[Email] SMTP连接失败");
    return;
  }

  _smtp.authenticate(config.smtpUser.c_str(), config.smtpPass.c_str(), readymail_auth_password);

  SMTPMessage msg;
  String from = "sms notify <" + config.smtpUser + ">";
  String to   = "your_email <" + config.smtpSendTo + ">";
  msg.headers.add(rfc822_from, from.c_str());
  msg.headers.add(rfc822_to,   to.c_str());
  msg.headers.add(rfc822_subject, subject);
  msg.text.body(body);
  msg.timestamp = time(nullptr);
  _smtp.send(msg);
  Serial.println("[Email] 发送完成");
}
