// GET request with URL params  ?sender=…&message=…&timestamp=…&device=…
#include "PushChannels.h"
#include "utils/Utils.h"
#include <HTTPClient.h>

int pushGet(const PushChannel& ch, const char* sender, const char* msg, const char* ts, const char* dev) {
  String url = ch.url;
  url += (url.indexOf('?') == -1) ? "?" : "&";
  url += "sender="    + urlEncode(String(sender));
  url += "&message="  + urlEncode(String(msg));
  url += "&timestamp="+ urlEncode(formatTimestamp(ts));
  url += "&device="   + urlEncode(String(dev));
  Serial.println("[PushGet] " + url);
  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  http.end();
  return code;
}

