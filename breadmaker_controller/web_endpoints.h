#pragma once
#include "globals.h"
#include <WebServer.h>

// Register all web API endpoints
void registerWebEndpoints(WebServer& server);

// Debug endpoints for troubleshooting
void debugEndpoints(WebServer& server);

// Performance metrics tracking (call from main loop)
void updatePerformanceMetrics();
