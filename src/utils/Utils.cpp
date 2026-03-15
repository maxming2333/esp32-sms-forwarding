#include "Utils.h"

// ── JSON escape ───────────────────────────────────────────────────────────────
String jsonEscape(const String& str) {
  String r;
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    switch (c) {
      case '"':  r += "\\\""; break;
      case '\\': r += "\\\\"; break;
      case '\n': r += "\\n";  break;
      case '\r': r += "\\r";  break;
      case '\t': r += "\\t";  break;
      default:   r += c;
    }
  }
  return r;
}

// ── URL encode ────────────────────────────────────────────────────────────────
String urlEncode(const String& str) {
  String enc;
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == ' ') {
      enc += '+';
    } else if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      enc += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      enc += buf;
    }
  }
  return enc;
}

// ── Timestamp formatter ───────────────────────────────────────────────────────
String formatTimestamp(const char* ts) {
  if (!ts || strlen(ts) < 12) return String(ts ? ts : "");
  char out[20];
  snprintf(out, sizeof(out), "%.2s/%.2s/%.2s %.2s:%.2s:%.2s",
           ts, ts+2, ts+4, ts+6, ts+8, ts+10);
  return String(out);
}

// ── UTC milliseconds ──────────────────────────────────────────────────────────
int64_t getUtcMillis() {
  struct timeval tv;
  if (gettimeofday(&tv, nullptr) == 0)
    return (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
  return (int64_t)time(nullptr) * 1000LL;
}

// ── DingTalk HMAC-SHA256 sign ─────────────────────────────────────────────────
String dingtalkSign(const String& secret, int64_t timestampMs) {
  String str2sign = String(timestampMs) + "\n" + secret;
  uint8_t result[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, (const uint8_t*)secret.c_str(), secret.length());
  mbedtls_md_hmac_update(&ctx, (const uint8_t*)str2sign.c_str(), str2sign.length());
  mbedtls_md_hmac_finish(&ctx, result);
  mbedtls_md_free(&ctx);
  return urlEncode(base64::encode(result, 32));
}

// ── Verification code extractor ───────────────────────────────────────────────
String extractVerifyCode(const char* text) {
  if (!text) return "";
  String cur;
  for (int i = 0; text[i]; i++) {
    if (text[i] >= '0' && text[i] <= '9') {
      cur += text[i];
    } else {
      if (cur.length() >= 4) return cur;
      cur = "";
    }
  }
  return (cur.length() >= 4) ? cur : "";
}

