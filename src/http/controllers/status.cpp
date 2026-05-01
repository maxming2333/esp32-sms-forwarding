#include "status.h"
#include "config/config.h"
#include "sim/sim.h"
#include "time/time_sync.h"
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static const char* simStateLabel(SimState s) {
  switch (s) {
    case SIM_UNKNOWN:      return "未检测";
    case SIM_NOT_INSERTED: return "未插入";
    case SIM_NOT_READY:    return "已插入，未就绪";
    case SIM_INITIALIZING: return "初始化中";
    case SIM_READY:        return "就绪";
    case SIM_INIT_FAILED:  return "初始化失败";
    default:               return "未知";
  }
}

void statusController(AsyncWebServerRequest* request) {
  AsyncJsonResponse* resp = new AsyncJsonResponse();
  JsonObject root = resp->getRoot();

  IPAddress localIp = WiFi.localIP();
  root["ip"]            = (localIp == IPAddress(0, 0, 0, 0)) ? WiFi.softAPIP().toString() : localIp.toString();
  root["wifiConnected"] = WiFi.isConnected();
  root["ssid"]          = WiFi.SSID();
  root["configValid"]   = ConfigStore::isValid();
  root["timeSynced"]    = TimeSync::isSynced();
  root["uptime"]        = millis() / 1000;

  SimState ss = Sim::state();
  root["simState"]      = (int)ss;
  root["simStateLabel"] = simStateLabel(ss);

  resp->setLength();
  request->send(resp);
}
