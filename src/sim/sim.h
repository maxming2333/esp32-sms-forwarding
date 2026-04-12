#pragma once
#include <Arduino.h>

enum SimState {
  SIM_UNKNOWN        = 0,
  SIM_NOT_INSERTED   = 1,
  SIM_NOT_READY      = 2,
  SIM_INITIALIZING   = 3,
  SIM_READY          = 4,
  SIM_INIT_FAILED    = 5
};

void simInit();
SimState simGetState();
void simHandleURC(const String& line);
void simTick();
