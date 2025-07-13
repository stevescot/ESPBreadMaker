#pragma once
#include "globals.h"
#include <ESPAsyncWebServer.h>

// Register all web API endpoints
void registerWebEndpoints(AsyncWebServer& server);
