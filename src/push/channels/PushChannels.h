#pragma once
// All push-channel implementations share this header.
// Each channel function returns:
//   >0  : HTTP status code received
//    0  : Success (non-HTTP push, e.g. SMS forwarding)
//   -1  : Skip (disabled, incomplete config, etc.)
//   <-1 : Network error code

#include "config/AppConfig.h"

// ── Per-type send functions ───────────────────────────────────────────────────
int pushPostJson    (const PushChannel& ch, const char* sender, const char* msg, const char* ts, const char* dev);
int pushBark        (const PushChannel& ch, const char* sender, const char* msg, const char* ts, const char* dev);
int pushGet         (const PushChannel& ch, const char* sender, const char* msg, const char* ts, const char* dev);
int pushDingtalk    (const PushChannel& ch, const char* sender, const char* msg, const char* ts, const char* dev);
int pushPushplus    (const PushChannel& ch, const char* sender, const char* msg, const char* ts, const char* dev);
int pushServerchan  (const PushChannel& ch, const char* sender, const char* msg, const char* ts, const char* dev);
int pushCustom      (const PushChannel& ch, const char* sender, const char* msg, const char* ts, const char* dev);
int pushFeishu      (const PushChannel& ch, const char* sender, const char* msg, const char* ts, const char* dev);
int pushGotify      (const PushChannel& ch, const char* sender, const char* msg, const char* ts, const char* dev);
int pushTelegram    (const PushChannel& ch, const char* sender, const char* msg, const char* ts, const char* dev);
int pushWorkWeixin  (const PushChannel& ch, const char* sender, const char* msg, const char* ts, const char* dev);
int pushSmsForward  (const PushChannel& ch, const char* sender, const char* msg, const char* ts, const char* dev);

