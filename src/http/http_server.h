#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// HTTP 服务封装：负责路由注册与基本认证中间件管理。
// 业务接口由 src/http/controllers/*.cpp 中的 controller 自由函数实现，
// 此类仅做装配；保持薄封装以减少 9 个 controller 大规模改造。
class HttpServer {
public:
  // 注册全部路由到 server，并安装认证白名单中间件。
  // 仅在 setup() 中调用一次。
  static void setup(AsyncWebServer& server);

  // 重新读取 config 中的 web 用户名/密码，并刷新 basic-auth 中间件哈希。
  // 修改 config 后需调用此方法使新凭据立即生效。
  static void refreshAuthCredentials();
};
