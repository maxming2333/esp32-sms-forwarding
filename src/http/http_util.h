#pragma once
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>

// 将全局 TLS 策略应用到指定 WiFiClientSecure 实例。
// 修改此函数即可一次性切换全项目所有对外 HTTPS 请求的证书验证行为：
//   - 当前策略: client.setInsecure()  —— 跳过服务器证书验证
//   - 启用验证: client.setCACert(root_ca_cert)
void httpConfigureTls(WiFiClientSecure& client);

// 根据 URL scheme 初始化 HTTPClient（自动处理 HTTP / HTTPS）。
// HTTPS 时使用模块内部共享的 WiFiClientSecure 并应用全局 TLS 策略。
// 适用于顺序执行的场景（如推送）。并发场景请自行创建 WiFiClientSecure
// 并直接调用 httpConfigureTls() + http.begin(client, url)。
// 调用方必须在使用完毕后调用 http.end()。
void httpClientBegin(HTTPClient& http, const String& url);
