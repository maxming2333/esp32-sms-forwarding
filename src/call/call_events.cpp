#include "call_events.h"
#include "../logger/logger.h"
#include <time.h>
#include <esp_random.h>

namespace {

CallEvents::Event s_ring[CallEvents::CAPACITY];
size_t            s_count    = 0;   // 环内有效条数,上限 CAPACITY
size_t            s_head     = 0;   // 下一个写入位置
uint32_t          s_sequence = 0;   // 已产生事件总数,也是最后一条的 sequence
char              s_bootId[17] = {0};

}  // namespace

void CallEvents::init() {
  snprintf(s_bootId, sizeof(s_bootId), "%08x%08x",
           (unsigned)esp_random(), (unsigned)esp_random());
  s_count    = 0;
  s_head     = 0;
  s_sequence = 0;
}

void CallEvents::record(const String& callerNumber) {
  Event& slot = s_ring[s_head];
  s_head      = (s_head + 1) % CAPACITY;
  // 不在这里统计「覆盖了多少条」:桥不知道消费方读到哪了,环满后每次覆盖都累加会在
  // 正常运行下虚增。丢失量由消费方用 oldestSequence 与自己的游标推导。
  if (s_count < CAPACITY) s_count++;

  slot.sequence   = ++s_sequence;
  slot.observedMs = millis();
  // 时间未同步时留 0 而不是填一个假时间戳;消费方据此回落到自己的接收时刻。
  time_t now      = time(nullptr);
  slot.observedAt = (now > 946684800) ? now : 0;  // 2000-01-01 之前视为未同步

  const char* source = callerNumber.length() > 0 ? callerNumber.c_str() : "未知号码";
  strlcpy(slot.number, source, sizeof(slot.number));

  LOG("CALLEV", "记录来电事件 seq=%lu 号码=%s（缓冲 %u/%u）",
      (unsigned long)slot.sequence, slot.number, (unsigned)s_count, (unsigned)CAPACITY);
}

size_t CallEvents::since(uint32_t after, Event* out, size_t max) {
  if (out == nullptr || max == 0 || s_count == 0) return 0;
  // 环内最旧一条的 sequence。
  uint32_t oldest = s_sequence - (uint32_t)s_count + 1;
  uint32_t from   = (after + 1 > oldest) ? after + 1 : oldest;
  if (from > s_sequence) return 0;

  size_t written = 0;
  for (uint32_t sequence = from; sequence <= s_sequence && written < max; sequence++) {
    // 环内位置:s_head 指向下一个写入点,因此最旧一条在 s_head - s_count。
    size_t offset = (size_t)(sequence - oldest);
    size_t index  = (s_head + CAPACITY - s_count + offset) % CAPACITY;
    out[written++] = s_ring[index];
  }
  return written;
}

uint32_t CallEvents::latestSequence() { return s_sequence; }

uint32_t CallEvents::oldestSequence() {
  if (s_count == 0) return 0;
  return s_sequence - (uint32_t)s_count + 1;
}

const char* CallEvents::bootId() { return s_bootId; }
