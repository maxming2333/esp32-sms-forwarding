#pragma once
#include <Arduino.h>
#include <WebServer.h>

extern WebServer server;

// Register all routes and start the server on port 80
void webServerInit();

// Must be called every loop()
inline void webServerTick() { server.handleClient(); }

