#pragma once
#include "globals.h"
#include <WebServer.h>

// Register all web API endpoints
void registerWebEndpoints(WebServer& server);

// Performance metrics tracking (call from main loop)
void updatePerformanceMetrics();
