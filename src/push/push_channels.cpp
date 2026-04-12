#include "push_channels.h"
#include "logger.h"
#include "sms/sms.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <base64.h>

// ---------- helpers ----------

static String urlEncode(const String& str) {
  String encoded;
  char c, code0, code1;
  for (unsigned int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encoded += '+';
    } else if (isalnum(c)) {
      encoded += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) code0 = c - 10 + 'A';
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}

static String dingtalkSign(const String& secret, int64_t timestamp) {
  String stringToSign = String(timestamp) + "\n" + secret;
  uint8_t hmacResult[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)secret.c_str(), secret.length());
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)stringToSign.c_str(), stringToSign.length());
  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);
  return urlEncode(base64::encode(hmacResult, 32));
}

static int64_t getUtcMillis() {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) == 0) {
    return (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
  }
  return (int64_t)time(nullptr) * 1000LL;
}

// ---------- channel implementations ----------

bool sendPostJson(const PushChannel& ch, const String& sender, const String& message, const String& timestamp) {
  HTTPClient http;
  http.begin(ch.url);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["sender"]    = sender;
  doc["message"]   = message;
  doc["timestamp"] = timestamp;
  String body;
  serializeJson(doc, body);

  LOG("Push", "POST JSON to %s: %s", ch.url.c_str(), body.c_str());
  int code = http.POST(body);
  LOG("Push", "响应码: %d", code);
  http.end();
  return code >= 200 && code < 300;
}

bool sendBark(const PushChannel& ch, const String& sender, const String& message, const String& timestamp) {
  HTTPClient http;
  http.begin(ch.url);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["title"] = sender;
  doc["body"]  = message;
  String body;
  serializeJson(doc, body);

  LOG("Push", "Bark to %s: %s", ch.url.c_str(), body.c_str());
  int code = http.POST(body);
  LOG("Push", "响应码: %d", code);
  http.end();
  return code >= 200 && code < 300;
}

bool sendGet(const PushChannel& ch, const String& sender, const String& message, const String& timestamp) {
  HTTPClient http;
  String url = ch.url;
  url += (url.indexOf('?') == -1) ? "?" : "&";
  url += "sender=" + urlEncode(sender);
  url += "&message=" + urlEncode(message);
  url += "&timestamp=" + urlEncode(timestamp);

  LOG("Push", "GET %s", url.c_str());
  http.begin(url);
  int code = http.GET();
  LOG("Push", "响应码: %d", code);
  http.end();
  return code >= 200 && code < 300;
}

bool sendDingtalk(const PushChannel& ch, const String& sender, const String& message, const String& timestamp) {
  HTTPClient http;
  String webhookUrl = ch.url;

  if (ch.key1.length() > 0) {
    int64_t ts = getUtcMillis();
    String sign = dingtalkSign(ch.key1, ts);
    webhookUrl += (webhookUrl.indexOf('?') == -1) ? "?" : "&";
    char tsBuf[21];
    snprintf(tsBuf, sizeof(tsBuf), "%lld", ts);
    webhookUrl += "timestamp=" + String(tsBuf) + "&sign=" + sign;
  }

  http.begin(webhookUrl);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["msgtype"] = "text";
  String content = "📱短信通知\n发送者: " + sender + "\n内容: " + message + "\n时间: " + timestamp;
  doc["text"]["content"] = content;
  String body;
  serializeJson(doc, body);

  LOG("Push", "DingTalk: %s", body.c_str());
  int code = http.POST(body);
  LOG("Push", "响应码: %d", code);
  http.end();
  return code >= 200 && code < 300;
}

bool sendPushPlus(const PushChannel& ch, const String& sender, const String& message, const String& timestamp) {
  HTTPClient http;
  String url = ch.url.length() > 0 ? ch.url : "http://www.pushplus.plus/send";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String channelValue = "wechat";
  if (ch.key2.length() > 0) {
    if (ch.key2 == "wechat" || ch.key2 == "extension" || ch.key2 == "app") {
      channelValue = ch.key2;
    } else {
      LOG("Push", "Invalid PushPlus channel '%s'. Using default 'wechat'.", ch.key2.c_str());
    }
  }

  JsonDocument doc;
  doc["token"]   = ch.key1;
  doc["title"]   = "短信来自: " + sender;
  doc["content"] = "<b>发送者:</b> " + sender + "<br><b>时间:</b> " + timestamp + "<br><b>内容:</b><br>" + message;
  doc["channel"] = channelValue;
  String body;
  serializeJson(doc, body);

  LOG("Push", "PushPlus: %s", body.c_str());
  int code = http.POST(body);
  LOG("Push", "响应码: %d", code);
  http.end();
  return code >= 200 && code < 300;
}

bool sendServerChan(const PushChannel& ch, const String& sender, const String& message, const String& timestamp) {
  HTTPClient http;
  String url = ch.url.length() > 0 ? ch.url : ("https://sctapi.ftqq.com/" + ch.key1 + ".send");
  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postData = "title=" + urlEncode("短信来自: " + sender);
  postData += "&desp=" + urlEncode("**发送者:** " + sender + "\n\n**时间:** " + timestamp + "\n\n**内容:**\n\n" + message);

  LOG("Push", "Server酱: %s", postData.c_str());
  int code = http.POST(postData);
  LOG("Push", "响应码: %d", code);
  http.end();
  return code >= 200 && code < 300;
}

bool sendCustom(const PushChannel& ch, const String& sender, const String& message, const String& timestamp) {
  if (ch.customBody.length() == 0) {
    LOG("Push", "自定义模板为空，跳过");
    return false;
  }
  HTTPClient http;
  http.begin(ch.url);
  http.addHeader("Content-Type", "application/json");

  String body = ch.customBody;
  body.replace("{sender}", sender);
  body.replace("{message}", message);
  body.replace("{timestamp}", timestamp);

  LOG("Push", "自定义: %s", body.c_str());
  int code = http.POST(body);
  LOG("Push", "响应码: %d", code);
  http.end();
  return code >= 200 && code < 300;
}

bool sendFeishu(const PushChannel& ch, const String& sender, const String& message, const String& timestamp) {
  HTTPClient http;
  http.begin(ch.url);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;

  if (ch.key1.length() > 0) {
    int64_t ts = time(nullptr);
    String stringToSign = String(ts) + "\n" + ch.key1;
    uint8_t hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char*)ch.key1.c_str(), ch.key1.length());
    mbedtls_md_hmac_update(&ctx, (const unsigned char*)stringToSign.c_str(), stringToSign.length());
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);
    doc["timestamp"] = String(ts);
    doc["sign"]      = base64::encode(hmacResult, 32);
  }

  doc["msg_type"]           = "text";
  doc["content"]["text"]    = "📱短信通知\n发送者: " + sender + "\n内容: " + message + "\n时间: " + timestamp;

  String body;
  serializeJson(doc, body);

  LOG("Push", "飞书: %s", body.c_str());
  int code = http.POST(body);
  LOG("Push", "响应码: %d", code);
  http.end();
  return code >= 200 && code < 300;
}

bool sendGotify(const PushChannel& ch, const String& sender, const String& message, const String& timestamp) {
  HTTPClient http;
  String url = ch.url;
  if (!url.endsWith("/")) url += "/";
  url += "message?token=" + ch.key1;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["title"]    = "短信来自: " + sender;
  doc["message"]  = message + "\n\n时间: " + timestamp;
  doc["priority"] = 5;
  String body;
  serializeJson(doc, body);

  LOG("Push", "Gotify: %s", body.c_str());
  int code = http.POST(body);
  LOG("Push", "响应码: %d", code);
  http.end();
  return code >= 200 && code < 300;
}

bool sendTelegram(const PushChannel& ch, const String& sender, const String& message, const String& timestamp) {
  HTTPClient http;
  String baseUrl = ch.url.length() > 0 ? ch.url : "https://api.telegram.org";
  if (baseUrl.endsWith("/")) baseUrl.remove(baseUrl.length() - 1);
  String url = baseUrl + "/bot" + ch.key2 + "/sendMessage";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["chat_id"] = ch.key1;
  doc["text"]    = "📱短信通知\n发送者: " + sender + "\n内容: " + message + "\n时间: " + timestamp;
  String body;
  serializeJson(doc, body);

  LOG("Push", "Telegram: %s", body.c_str());
  int code = http.POST(body);
  LOG("Push", "响应码: %d", code);
  http.end();
  return code >= 200 && code < 300;
}

bool sendWechatWork(const PushChannel& ch, const String& sender, const String& message, const String& timestamp) {
  HTTPClient http;
  String webhookUrl = ch.url;

  if (ch.key1.length() > 0) {
    int64_t ts = getUtcMillis();
    char tsBuf[21];
    snprintf(tsBuf, sizeof(tsBuf), "%lld", ts);
    String stringToSign = String(tsBuf) + "\n" + ch.key1;
    uint8_t hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char*)ch.key1.c_str(), ch.key1.length());
    mbedtls_md_hmac_update(&ctx, (const unsigned char*)stringToSign.c_str(), stringToSign.length());
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);
    String sign = urlEncode(base64::encode(hmacResult, 32));
    webhookUrl += (webhookUrl.indexOf('?') == -1) ? "?" : "&";
    webhookUrl += "timestamp=" + String(tsBuf) + "&sign=" + sign;
  }

  http.begin(webhookUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);

  JsonDocument doc;
  doc["msgtype"] = "text";
  doc["text"]["content"] = "📱短信通知\n发件人: " + sender + "\n内容: " + message + "\n时间: " + timestamp;
  String body;
  serializeJson(doc, body);

  LOG("Push", "企业微信: %s", body.c_str());
  int code = http.POST(body);
  LOG("Push", "响应码: %d", code);
  http.end();
  if (code < 200 || code >= 300) {
    LOG("Push", "企业微信推送失败，状态码: %d", code);
    return false;
  }
  return true;
}

bool sendSmsPush(const PushChannel& ch, const String& sender, const String& message, const String& timestamp) {
  String prefix = "[转发]发件人: " + sender + "\n内容: ";
  String content = prefix + message;
  if ((int)content.length() > 70) {
    int maxMsg = 67 - (int)prefix.length();
    if (maxMsg < 0) maxMsg = 0;
    content = prefix + message.substring(0, maxMsg) + "[截断]";
  }
  LOG("Push", "SMS备份推送到: %s", ch.url.c_str());
  bool ok = sendSMSPDU(ch.url.c_str(), content.c_str());
  if (!ok) LOG("Push", "SMS备份推送失败");
  return ok;
}
