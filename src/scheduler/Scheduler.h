#pragma once
#include <Arduino.h>

// Call once per loop() to trigger a daily scheduled reboot when configured.
void checkScheduledReboot();

// Call once per loop() to periodically consume traffic (keep connection alive).
void checkTrafficKeep();

