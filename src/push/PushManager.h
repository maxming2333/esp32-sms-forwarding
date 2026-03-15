#pragma once
#include "config/AppConfig.h"

// Dispatch to the enabled push channels
void pushAll(const char* sender, const char* message, const char* timestamp);

// Dispatch to a single channel; returns HTTP code or -1
int  pushOne(const PushChannel& ch, const char* sender, const char* message,
             const char* timestamp, const char* devicePhone);

