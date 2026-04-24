#pragma once
#include <Arduino.h>

// 在 AP 模式下调用：初始化 BluFi BLE 配网并开始广播（设备名称由内部获取）
void blufiInit();

// 关闭 BluFi（可选，STA 连接后调用）
void blufiDeinit();
