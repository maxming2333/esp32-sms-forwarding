#pragma once
#include <ESPAsyncWebServer.h>

void configController(AsyncWebServerRequest* request);
void configExportController(AsyncWebServerRequest* request);
void configImportController(AsyncWebServerRequest* request, uint8_t* data,
                            size_t len, size_t index, size_t total);
