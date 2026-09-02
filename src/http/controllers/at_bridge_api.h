#pragma once
#include <ESPAsyncWebServer.h>

// 远程 AT 桥 HTTP 端点（Simplus `internal/atremote` 的对端）。
//
// 用途：把本机模组的 AT 接口以「会话 + 单条命令」的形式暴露给局域网内的 Simplus
// Agent，让 Agent 可以驱动一枚并没有插在它自己主机上的受支持模组。
//
// 与既有 `GET /at` 的区别：
//   - `/at` 是网页调试入口，无独占语义，谁都可以随时插一条命令进去；
//   - 本组端点先取得一个会话 token，期间拒绝网页端的任意 AT 注入。SIM AKA 是
//     「打开逻辑通道 → 交换 APDU → 关闭通道」的粘性序列，中间被别的 APDU 消费者
//     插入一条命令就会静默破坏交换结果，因此独占是必需的而不是保险措施。
//
// 契约细节（状态码、边界、JSON 形态）见 Simplus 仓库 docs/remote-at-bridge.md。
// 注意：本组端点的**成功**响应体不使用本固件的 {ok,message} 约定，而是契约规定的
// 裸对象（{"session","expiresInMs"} 与 {"lines"}）；对端会拒绝多余字段。失败响应
// 体只看状态码，因此仍沿用 {ok,error}。

// POST /at/session —— 取得模组 AT 接口的独占会话
void atBridgeSessionOpenController(AsyncWebServerRequest* request);

// POST /at/command —— 在会话内下发一条 AT 命令并返回响应行
void atBridgeCommandController(AsyncWebServerRequest* request, uint8_t* data,
                               size_t len, size_t index, size_t total);

// POST /at/exchange —— 提示符类命令（AT+CMGS）：命令 → '>' → 载荷 → 终止状态
void atBridgeExchangeController(AsyncWebServerRequest* request, uint8_t* data,
                                size_t len, size_t index, size_t total);

// DELETE /at/session —— 释放会话
void atBridgeSessionCloseController(AsyncWebServerRequest* request, uint8_t* data,
                                   size_t len, size_t index, size_t total);

// 当前是否有未过期的桥会话。网页调试入口据此拒绝插入 AT 命令。
bool atBridgeSessionActive();

// GET /events/calls?after=<seq>&limit=<n> —— 取出外部系统尚未读到的来电事件。
//
// 这不是 AT 中继:来电是纯实时上报,模组和 SIM 里没有任何存储,所以桥必须自己缓冲。
// 短信可以靠轮询模组存储补取,来电不行——这是外部系统能感知来电的唯一途径。
void callEventsController(AsyncWebServerRequest* request);
