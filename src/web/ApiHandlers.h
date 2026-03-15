#pragma once
#include <WebServer.h>

// HTTP Basic Auth check — sends 401 and returns false if not authenticated
bool checkAuth();

// ── HTML page handlers ───────────────────────────────────────────────────────
void handleWebApp();         // serves the Vue SPA (all "page" routes)
void handleNotFound();       // 404 for any unknown route (favicon.ico, etc.)

// ── JSON API handlers ────────────────────────────────────────────────────────
void handleGetConfig();      // GET  /api/config
void handlePostConfig();     // POST /api/config  (also legacy /save)
void handleGetStatus();      // GET  /api/status
void handleGetSysInfo();     // GET  /api/sysinfo
void handleSendSms();        // POST /api/sendsms  (also legacy /sendsms)
void handleQuery();          // GET  /api/query?type=…
void handleFlightMode();     // GET  /api/flight?action=…
void handleATCommand();      // GET  /api/at?cmd=…
void handlePing();           // POST /api/ping
void handleTestPush();       // POST /api/test_push
void handleReboot();         // POST /api/reboot

