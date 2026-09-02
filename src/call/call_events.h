#pragma once
#include <Arduino.h>

// 来电事件缓冲:供外部系统(Simplus)获知「谁在什么时候打来过」。
//
// 为什么必须缓冲:RING / +CLIP 是纯实时主动上报,模组和 SIM 卡里**没有任何存储**
// (EF_SMS 只存短信),那一刻没人记下来就永久消失。短信可以靠轮询模组存储补取,来电
// 不行 —— 所以这层缓冲是外部系统能感知来电的唯一途径。
//
// 只记录「发生过一次来电」这一事实,不涉及接听、挂断、媒体或通话状态机。
//
// 容量有限且满时丢最旧:来电是瞬时事件,保留最近的比保留最早的有用。溢出会被计数并
// 随查询一起返回,这样消费方能知道自己漏了多少,而不是以为一切正常。
class CallEvents {
public:
  // 缓冲条数。按外部系统 2 秒轮询计算,32 条足以覆盖长时间离线后的追补;
  // 每条约 40 字节,总计约 1.3KB。
  static constexpr size_t CAPACITY = 32;

  struct Event {
    uint32_t      sequence;    // 单调递增,重启归零,消费方据此做游标
    unsigned long observedMs;  // 记录时刻的 millis()
    time_t        observedAt;  // 记录时刻的墙钟时间;未同步时为 0
    char          number[24];  // 主叫号码,或占位文本
  };

  static void init();

  // 记录一次来电。由 Call::dispatch() 调用 —— 那是推送的单一汇聚点,因此这里的
  // 记录与固件自身的推送严格同源,不会出现「推送了但外部系统查不到」。
  static void record(const String& callerNumber);

  // 取出 sequence 大于 after 的事件,最多 max 条,按 sequence 升序写入 out。
  // 返回实际写入条数。
  static size_t since(uint32_t after, Event* out, size_t max);

  // 已产生的事件总数(含已被覆盖的),与 dropped 一起让消费方判断是否漏读。
  static uint32_t latestSequence();

  // 因缓冲满而被覆盖丢弃的事件数。
  static uint32_t dropped();
};
