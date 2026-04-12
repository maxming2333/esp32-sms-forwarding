#pragma once
#include <ESPAsyncWebServer.h>

void sendSmsController(AsyncWebServerRequest* request);
void pingController(AsyncWebServerRequest* request);
void queryController(AsyncWebServerRequest* request);
void flightModeController(AsyncWebServerRequest* request);
void atCommandController(AsyncWebServerRequest* request);

void resetTokenController(AsyncWebServerRequest* request);
void resetConfigController(AsyncWebServerRequest* request, uint8_t* data,
                           size_t len, size_t index, size_t total);
