#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

void setupHttpServer(AsyncWebServer& server);
void refreshAuthCredentials();
