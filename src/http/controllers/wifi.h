#pragma once
#ifndef CONTROLLERS_WIFI_H
#define CONTROLLERS_WIFI_H

#include <ESPAsyncWebServer.h>

void wifiGetController(AsyncWebServerRequest* request);
void wifiPostController(AsyncWebServerRequest* request, uint8_t* data,
                        size_t len, size_t index, size_t total);

#endif  // CONTROLLERS_WIFI_H
