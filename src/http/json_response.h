#pragma once
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// HTTP JSON 响应辅助类：减少 controller 中重复的 JSON 拼装代码。
//
// 统一返回结构（所有"状态型"端点都遵循）：
//   - 成功：{"ok": true,  "message": "..."}（message 可省略）
//   - 失败：{"ok": false, "error":   "..."}
// 数据型端点（如 /api/config、/api/status）仍使用 AsyncJsonResponse 自由组装。
class JsonResp {
public:
  // 发送原始 JSON body 字符串
  static void send(AsyncWebServerRequest* req, int code, const String& body) {
    req->send(code, "application/json", body);
  }

  // 发送 {"ok": true, "message": msg}（msg 为空则只返回 {"ok":true}）
  // 使用 ArduinoJson 序列化以正确转义任意字符（含换行/控制符），
  // 适合承载 HTML 片段等复杂消息体。
  static void ok(AsyncWebServerRequest* req, const String& msg = "") {
    JsonDocument doc;
    doc["ok"] = true;
    if (msg.length() > 0) {
      doc["message"] = msg;
    }
    String body;
    serializeJson(doc, body);
    req->send(200, "application/json", body);
  }

  // 发送 {"ok": false, "error": msg}
  static void err(AsyncWebServerRequest* req, int code, const String& msg) {
    JsonDocument doc;
    doc["ok"]    = false;
    doc["error"] = msg;
    String body;
    serializeJson(doc, body);
    req->send(code, "application/json", body);
  }

  // 发送 {"ok": true, "message": msg}，并在 delayMs 后调度 ESP.restart()。
  // 用于"保存配置后自动重启"的常见场景；任务栈 2 KiB 足够。
  static void okWithReboot(AsyncWebServerRequest* req, const String& msg,
                           uint32_t delayMs = 2000) {
    ok(req, msg);
    uint32_t* d = new uint32_t(delayMs);
    xTaskCreate([](void* p) {
      uint32_t ms = *(uint32_t*)p;
      delete (uint32_t*)p;
      vTaskDelay(pdMS_TO_TICKS(ms));
      ESP.restart();
      vTaskDelete(nullptr);
    }, "restart", 2048, d, 1, nullptr);
  }

  // 通过 lambda 填充 JsonObject 根节点构造响应；code 默认 200。
  template <typename Fn>
  static void build(AsyncWebServerRequest* req, Fn fill, int code = 200) {
    AsyncJsonResponse* r = new AsyncJsonResponse();
    JsonObject root = r->getRoot().to<JsonObject>();
    fill(root);
    r->setLength();
    r->setCode(code);
    req->send(r);
  }
};
