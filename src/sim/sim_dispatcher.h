#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// ---------- Dispatcher 常量 ----------

// 命令队列容量；溢出时 sendCommand() 会返回 false（极少触发，通常并发不会超过 2~3）
constexpr int          SIM_CMD_QUEUE_SIZE        = 16;
// Reader Task 栈大小；包含 PDU 解析 + URC 回调链路开销，4096 字节实测充足
constexpr uint32_t     SIM_READER_TASK_STACK     = 4096;
// Reader Task 优先级（高于普通 loop，避免长 HTTP 处理时阻塞 SIM URC）
constexpr UBaseType_t  SIM_READER_TASK_PRIORITY  = 3;

// ---------- 缓冲尺寸 ----------
// 尺寸由「透传完整 APDU」这一最大用例决定（AT+CSIM，用于把 APDU 中继给 SIM 卡）：
//   ISO 7816-4 短格式 APDU 上限 = CLA/INS/P1/P2/Lc(5) + 数据(255) + Le(1) = 261 字节
//   → hex 编码 522 字符
//   命令侧：AT+CSIM=522,"<522 hex>"  = 13 + 522 + 1 = 536 字符（+NUL）
//   响应侧：+CSIM: 516,"<516 hex>"   = 12 + 516 + 1 = 529 字符，再加 "\nOK\n"
// 旧值 cmd[64] / resp[256] / line[512] 会让 AT+CSIM 的 AUTHENTICATE(93 字符) 被
// 直接拒绝、长响应行被丢弃，因此三者一起放大并留出余量。
constexpr size_t        SIM_CMD_BUF_SIZE           = 576;
constexpr size_t        SIM_RESP_BUF_SIZE          = 640;

// 大响应容量上限。用途:AT+CMGL 全量列举短信存储。
//
// 一条 160 字符 GSM7 短信的 TPDU 约 152 字节 → 304 个 hex 字符,加 SMSC 与
// "+CMGL: n,s,,len" 头行约 340 字节;SIM 的 EF_SMS 常见 40 槽,满载约 13.6KB。
// 默认的 640 字节在**攒到 2 条正常长度短信**时就会截断,而截断会破坏整个转录
// (最后一行不完整、拿不到终止状态),上层判为非法响应并拒绝整批 —— 于是既读不
// 出来也排空不掉,只会越攒越死。
//
// 24KB 给 40 槽满载留了近一倍余量。
constexpr size_t        SIM_RESP_LARGE_BUF_SIZE    = 24576;
constexpr size_t        SIM_LINE_BUF_MAX           = 768;
constexpr unsigned long SIM_TIMEOUT_DRAIN_QUIET_MS = 300;

// 单条 AT 命令上下文。
// 注意：由 sendCommand() 在**堆上**分配（缓冲放大后本结构约 1.2KB，放在调用方
// 栈上会给 async_tcp / loopTask 带来近 1KB 的额外栈压力）。同步信号量在槽内创建。
struct SimCmdSlot {
  char              cmd[SIM_CMD_BUF_SIZE];        // AT 命令字符串
  unsigned long     timeoutMs;                    // 超时时间
  // 响应缓冲单独堆分配,容量由调用方按命令指定（截断时保留前 respCap-1 字节）。
  // 不做成固定大数组:队列深 16,若每槽都带 24KB 缓冲最坏情况要 380KB+，直接耗尽
  // RAM。只有全量列举这类命令需要大缓冲,其余仍用 640 字节。
  char*             respBuf;
  size_t            respCap;
  SemaphoreHandle_t doneSem;                      // 完成信号量（Reader Task → 调用方）
  bool              isOk;                         // OK / ERROR
  bool              priority;                     // 优先命令（插队到队头）

  // 析构里释放响应缓冲,这样现有的每一条 `delete slot` 路径都自动正确,不需要在
  // 四个错误分支里各加一次 free。
  ~SimCmdSlot() { delete[] respBuf; }
};

// URC 类型枚举（dispatcher 只做最小解析，业务字段交回上层）
enum class SimUrcType : uint8_t {
  RING       = 0,    // 来电响铃
  CLIP       = 1,    // 来电号码
  CMT        = 2,    // 文本短信头
  CMT_PDU    = 3,    // PDU 短信内容行
  CPIN_READY = 4,    // SIM 卡就绪
  SIM_REMOVE = 5,    // SIM 卡拔出
  CUSD       = 6,    // USSD 应答
  CMTI       = 7,    // 新短信已入存储的指示（瘦模式；本固件不消费，仅记录）
  MATREADY   = 8,    // 模组自身重启完成、AT 接口就绪
  CLCC       = 9,    // 来电详情。实测本模组用未经请求的 +CLCC: 代替 +CLIP: 上报主叫
  CALL_END   = 10,   // 通话结束（NO CARRIER / BUSY / NO ANSWER）
};

// URC 回调签名：dispatcher 在 Reader Task 上下文回调
using SimUrcCallback = void (*)(SimUrcType type, const String& line);

// SimDispatcher：纯通讯层。
// 职责：
//   - 持有 Serial1 的读写权；任何对 SIM 模组的字节级访问都必须经此类
//   - FIFO 命令队列 + Reader Task 模型，串行化所有 AT 调度，避免响应混淆
//   - URC 解析与回调分发（不含业务逻辑）
//   - 对外提供 pauseReader/resumeReader 以便特殊场景（如 OTA 期）独占 UART
// **不含**任何业务字段（号码/状态/运营商等），那些归 Sim 类。
class SimDispatcher {
public:
  // 注册 URC 回调；必须在 start() 之前调用。当前仅支持单回调。
  static void   registerUrcCallback(SimUrcCallback cb);

  // 创建队列 + 启动 Reader Task；应在 Sim::init() 成功后调用一次。
  static void   start();

  // start() 是否已成功执行（队列与任务均已就绪）。
  static bool   running();

  // 发送 AT 命令并等待响应。线程安全（队列 + 二值信号量）。
  // 返回 true 表示 OK；false 表示 ERROR/超时/队列满。start() 之前调用必失败。
  // - outResp：可选输出，截取到 respBuf 容量内的响应文本（含状态行）
  // - prio：true 时插入队头，用于关键控制命令
  // respCap:响应缓冲容量,默认 SIM_RESP_BUF_SIZE。需要容纳全量短信列举等大响应
  // 时传 SIM_RESP_LARGE_BUF_SIZE;截断会破坏整个转录,因此宁可多分配也不要截断。
  static bool   sendCommand(const char* cmd, unsigned long timeoutMs,
                            String* outResp = nullptr, bool prio = false,
                            size_t respCap = SIM_RESP_BUF_SIZE);

  // 在 reader 暂停期间,把裸读到的一行交给 URC 识别与路由;是主动上报则返回 true,
  // 调用方不应把它计入命令响应。
  //
  // 为什么必须有这个:pauseReader() 期间任何 RING / +CLIP / +CMTI 都落在调用方的
  // 裸读循环里,不转交就被当成响应字节吞掉并丢弃 —— 出站短信最长占用 30s,这段时间
  // 来的电话推送会直接消失。
  //
  // 安全前提:回调在调用方(async_tcp)上下文执行,因此所有 URC handler 都不得发送
  // AT 命令 —— reader 正停着,sendCommand 会永久阻塞。现有 handler 均只置标志位或
  // 入队(来电的 AT+CLCC 是延迟到 tick 里发的),满足该前提。新增 handler 必须保持。
  static bool   routeIfUrc(const String& line);

  // 暂停 Reader Task，调用方可直接 Serial1.read/write（必须配对 resumeReader）。
  // 返回 false 表示在 timeoutMs 内未能确认 Reader Task 让出 UART。
  static bool   pauseReader(unsigned long timeoutMs = 10000);
  static void   resumeReader();
};
