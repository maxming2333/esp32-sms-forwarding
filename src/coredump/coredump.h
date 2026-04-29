#pragma once
#include <Arduino.h>
#include <esp_partition.h>
#include <time.h>

// 初始化：从 NVS 恢复崩溃信息；panic 重启时将 RTC 时间/版本写入 NVS
void   coredumpInit();

// 由 main loop 每 10 秒调用一次，记录最后已知 wall-clock（RTC + NVS 限流写入）
void   coredumpUpdateLastKnownTime(time_t t);

// 查询 coredump 分区是否有数据
bool   coredumpHasData();

// 获取上次崩溃时间（0 = 无记录）
time_t coredumpGetCrashTime();

// 获取上次崩溃时的固件版本（空串 = 无记录）
String coredumpGetCrashVersion();

// 供 HTTP 控制器使用：返回 coredump 分区指针（nullptr = 未找到）
const esp_partition_t* coredumpGetPartition();

// 供 HTTP 控制器使用：扫描分区末尾，返回实际使用字节数（0 = 分区为空）
size_t coredumpGetUsedSize(const esp_partition_t* part);
