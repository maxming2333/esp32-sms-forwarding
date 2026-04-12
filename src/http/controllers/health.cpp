#include "health.h"

void healthController(AsyncWebServerRequest* request) {
  request->send(200, "application/json", "{\"status\":\"ok\"}");
}
