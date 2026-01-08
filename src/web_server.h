/**
 * @file web_server.h
 * Simple Web Server for Device Stats
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "config.h"

class WebServerManager {
public:
    WebServerManager();
    ~WebServerManager();
    
    void begin();
    void stop();
    
private:
    AsyncWebServer* server;
    bool isRunning;
    
    void setupRoutes();
    String generateStatsPage();
};

// Global instance
extern WebServerManager webServer;

#endif // WEB_SERVER_H
