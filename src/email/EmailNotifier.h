#pragma once
#include <Arduino.h>

// ── API ───────────────────────────────────────────────────────────────────────
// Initialise the SMTP client (call once in setup after WiFi is up)
void emailInit();

// Send an email notification; silently skips if config is incomplete
void emailNotify(const char* subject, const char* body);

