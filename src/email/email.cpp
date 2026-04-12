#include "email.h"
#include "config/config.h"
#include "logger.h"
#include <WiFiClientSecure.h>
#define ENABLE_SMTP
#define ENABLE_DEBUG
#include <ReadyMail.h>

static WiFiClientSecure ssl_client;
static SMTPClient smtp(ssl_client);

void sendEmailNotification(const char* subject, const char* body) {
  if (config.smtpServer.length() == 0 || config.smtpUser.length() == 0 ||
      config.smtpPass.length() == 0   || config.smtpSendTo.length() == 0) {
    LOG("Email", "邮件配置不完整，跳过发送");
    return;
  }

  ssl_client.setInsecure();

  auto statusCallback = [](SMTPStatus status) {
    LOG("Email", "%s", status.text.c_str());
  };

  smtp.connect(config.smtpServer.c_str(), config.smtpPort, statusCallback);
  if (smtp.isConnected()) {
    smtp.authenticate(config.smtpUser.c_str(), config.smtpPass.c_str(), readymail_auth_password);
    SMTPMessage msg;
    String from = "sms notify <"; from += config.smtpUser; from += ">";
    msg.headers.add(rfc822_from, from.c_str());
    String to = "your_email <"; to += config.smtpSendTo; to += ">";
    msg.headers.add(rfc822_to, to.c_str());
    msg.headers.add(rfc822_subject, subject);
    msg.text.body(body);
    msg.timestamp = time(nullptr);
    smtp.send(msg);
    LOG("Email", "邮件发送完成");
  } else {
    LOG("Email", "邮件服务器连接失败");
  }
}
