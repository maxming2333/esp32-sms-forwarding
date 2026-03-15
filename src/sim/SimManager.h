#pragma once
#include <Arduino.h>

#define SERIAL_BUFFER_SIZE 500

// ── Globals (defined in SimManager.cpp) ──────────────────────────────────────
extern bool   simPresent;
extern bool   simInitialized;
extern String devicePhoneNumber;

// ── LED / blink ───────────────────────────────────────────────────────────────
void blinkShort(unsigned long gapMs = 500);

// ── Modem power ───────────────────────────────────────────────────────────────
void modemPowerCycle();
void resetModule();

// ── AT command primitives ─────────────────────────────────────────────────────
bool   sendATandWaitOK(const char* cmd, unsigned long timeoutMs);
String sendATCommand(const char* cmd, unsigned long timeoutMs);

// ── SIM detection & init ──────────────────────────────────────────────────────
bool checkSIMPresent();
bool initSIMDependent();   // returns true on success
bool waitCEREG();

