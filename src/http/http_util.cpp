#include "http_util.h"

// 模块内共享安全客户端，供 httpClientBegin() 顺序调用使用
static WiFiClientSecure s_secureClient;

void httpConfigureTls(WiFiClientSecure& client) {
    // 全局 TLS 策略：跳过服务器证书验证（适用于内网/自签名证书场景）
    // 若需启用验证，改为：client.setCACert(root_ca_cert);
    client.setInsecure();
}

void httpClientBegin(HTTPClient& http, const String& url) {
    if (url.startsWith("https://")) {
        httpConfigureTls(s_secureClient);
        http.begin(s_secureClient, url);
    } else {
        http.begin(url);
    }
}
