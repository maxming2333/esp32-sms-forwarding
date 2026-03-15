#pragma once
#include <Arduino.h>
#include <pdulib.h>

extern PDU pdu;

// Encode message as PDU and send via Serial1
// Returns true on success
bool smsSend(const char* phoneNumber, const char* message);

