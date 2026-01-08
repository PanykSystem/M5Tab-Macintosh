/**
 * @file web_server.cpp
 * Simple Web Server Implementation - Device Stats Page
 */

#include "web_server.h"
#include <WiFi.h>

// Global instance
WebServerManager webServer;

// ============================================================================
// Constructor / Destructor
// ============================================================================

WebServerManager::WebServerManager()
    : server(nullptr)
    , isRunning(false)
{
}

WebServerManager::~WebServerManager() {
    stop();
}

// ============================================================================
// Initialization
// ============================================================================

void WebServerManager::begin() {
    if (isRunning) {
        return;
    }
    
    Serial.println("[Web] Starting web server...");
    
    server = new AsyncWebServer(WEB_SERVER_PORT);
    
    // Setup routes
    setupRoutes();
    
    server->begin();
    isRunning = true;
    
    Serial.printf("[Web] Server started on port %d\n", WEB_SERVER_PORT);
}

void WebServerManager::stop() {
    if (!isRunning) {
        return;
    }
    
    if (server) {
        server->end();
        delete server;
        server = nullptr;
    }
    
    isRunning = false;
    Serial.println("[Web] Server stopped");
}

// ============================================================================
// Route Setup
// ============================================================================

void WebServerManager::setupRoutes() {
    // Main stats page
    server->on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        String html = generateStatsPage();
        request->send(200, "text/html", html);
    });
    
    // Health check
    server->on("/health", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "OK");
    });
    
    // 404 handler
    server->onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not Found");
    });
}

// ============================================================================
// Stats Page Generation
// ============================================================================

String WebServerManager::generateStatsPage() {
    // Gather device stats
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    uint32_t freePsram = ESP.getFreePsram();
    uint32_t totalPsram = ESP.getPsramSize();
    uint32_t flashSize = ESP.getFlashChipSize();
    uint32_t sketchSize = ESP.getSketchSize();
    uint32_t freeSketchSpace = ESP.getFreeSketchSpace();
    uint32_t cpuFreq = ESP.getCpuFreqMHz();
    const char* chipModel = ESP.getChipModel();
    uint8_t chipCores = ESP.getChipCores();
    uint32_t uptimeSeconds = millis() / 1000;
    
    // Calculate percentages
    float heapUsedPercent = 100.0f * (totalHeap - freeHeap) / totalHeap;
    float psramUsedPercent = 0;
    if (totalPsram > 0) {
        psramUsedPercent = 100.0f * (totalPsram - freePsram) / totalPsram;
    }
    float flashUsedPercent = 100.0f * sketchSize / flashSize;
    
    // Format uptime
    uint32_t hours = uptimeSeconds / 3600;
    uint32_t minutes = (uptimeSeconds % 3600) / 60;
    uint32_t seconds = uptimeSeconds % 60;
    
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="refresh" content="5">
    <title>M5Stack Tab5 - Device Stats</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, sans-serif;
            background: linear-gradient(135deg, #0d1117 0%, #161b22 50%, #0d1117 100%);
            color: #e6edf3;
            min-height: 100vh;
            padding: 40px 20px;
        }
        
        .container {
            max-width: 800px;
            margin: 0 auto;
        }
        
        header {
            text-align: center;
            margin-bottom: 40px;
        }
        
        h1 {
            font-size: 2.5rem;
            color: #58a6ff;
            margin-bottom: 10px;
            text-shadow: 0 0 30px rgba(88, 166, 255, 0.3);
        }
        
        .subtitle {
            color: #8b949e;
            font-size: 1.1rem;
        }
        
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        
        .stat-card {
            background: rgba(22, 27, 34, 0.8);
            border: 1px solid #30363d;
            border-radius: 12px;
            padding: 24px;
            backdrop-filter: blur(10px);
        }
        
        .stat-card h3 {
            color: #8b949e;
            font-size: 0.85rem;
            text-transform: uppercase;
            letter-spacing: 1px;
            margin-bottom: 12px;
        }
        
        .stat-value {
            font-size: 2rem;
            font-weight: 600;
            color: #58a6ff;
            margin-bottom: 8px;
        }
        
        .stat-detail {
            color: #8b949e;
            font-size: 0.9rem;
        }
        
        .progress-bar {
            height: 8px;
            background: #21262d;
            border-radius: 4px;
            margin-top: 12px;
            overflow: hidden;
        }
        
        .progress-fill {
            height: 100%;
            border-radius: 4px;
            transition: width 0.3s ease;
        }
        
        .progress-fill.green { background: linear-gradient(90deg, #238636, #3fb950); }
        .progress-fill.yellow { background: linear-gradient(90deg, #9e6a03, #d29922); }
        .progress-fill.red { background: linear-gradient(90deg, #da3633, #f85149); }
        
        .chip-info {
            background: rgba(22, 27, 34, 0.8);
            border: 1px solid #30363d;
            border-radius: 12px;
            padding: 24px;
            margin-bottom: 20px;
        }
        
        .chip-info h2 {
            color: #58a6ff;
            margin-bottom: 16px;
            font-size: 1.2rem;
        }
        
        .chip-details {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 16px;
        }
        
        .chip-detail {
            text-align: center;
        }
        
        .chip-detail .label {
            color: #8b949e;
            font-size: 0.8rem;
            text-transform: uppercase;
            margin-bottom: 4px;
        }
        
        .chip-detail .value {
            color: #e6edf3;
            font-size: 1.1rem;
            font-weight: 500;
        }
        
        footer {
            text-align: center;
            color: #8b949e;
            font-size: 0.85rem;
            margin-top: 30px;
        }
        
        .refresh-note {
            color: #3fb950;
            margin-bottom: 8px;
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>Hello World</h1>
            <p class="subtitle">M5Stack Tab5 Device Statistics</p>
        </header>
        
        <div class="chip-info">
            <h2>System Information</h2>
            <div class="chip-details">
                <div class="chip-detail">
                    <div class="label">Chip</div>
                    <div class="value">)rawliteral" + String(chipModel) + R"rawliteral(</div>
                </div>
                <div class="chip-detail">
                    <div class="label">CPU Cores</div>
                    <div class="value">)rawliteral" + String(chipCores) + R"rawliteral(</div>
                </div>
                <div class="chip-detail">
                    <div class="label">CPU Frequency</div>
                    <div class="value">)rawliteral" + String(cpuFreq) + R"rawliteral( MHz</div>
                </div>
                <div class="chip-detail">
                    <div class="label">Uptime</div>
                    <div class="value">)rawliteral" + String(hours) + "h " + String(minutes) + "m " + String(seconds) + R"rawliteral(s</div>
                </div>
            </div>
        </div>
        
        <div class="stats-grid">
            <div class="stat-card">
                <h3>Heap Memory (RAM)</h3>
                <div class="stat-value">)rawliteral" + String(freeHeap / 1024) + R"rawliteral( KB</div>
                <div class="stat-detail">Free of )rawliteral" + String(totalHeap / 1024) + R"rawliteral( KB total</div>
                <div class="progress-bar">
                    <div class="progress-fill )rawliteral" + 
                        String(heapUsedPercent < 70 ? "green" : (heapUsedPercent < 90 ? "yellow" : "red")) + 
                        R"rawliteral(" style="width: )rawliteral" + String(heapUsedPercent) + R"rawliteral(%"></div>
                </div>
            </div>
            
            <div class="stat-card">
                <h3>PSRAM</h3>
                <div class="stat-value">)rawliteral" + String(freePsram / 1024 / 1024) + R"rawliteral( MB</div>
                <div class="stat-detail">Free of )rawliteral" + String(totalPsram / 1024 / 1024) + R"rawliteral( MB total</div>
                <div class="progress-bar">
                    <div class="progress-fill )rawliteral" + 
                        String(psramUsedPercent < 70 ? "green" : (psramUsedPercent < 90 ? "yellow" : "red")) + 
                        R"rawliteral(" style="width: )rawliteral" + String(psramUsedPercent) + R"rawliteral(%"></div>
                </div>
            </div>
            
            <div class="stat-card">
                <h3>Flash Storage</h3>
                <div class="stat-value">)rawliteral" + String(flashSize / 1024 / 1024) + R"rawliteral( MB</div>
                <div class="stat-detail">Sketch uses )rawliteral" + String(sketchSize / 1024) + R"rawliteral( KB ()rawliteral" + String((int)flashUsedPercent) + R"rawliteral(%)</div>
                <div class="progress-bar">
                    <div class="progress-fill )rawliteral" + 
                        String(flashUsedPercent < 70 ? "green" : (flashUsedPercent < 90 ? "yellow" : "red")) + 
                        R"rawliteral(" style="width: )rawliteral" + String(flashUsedPercent) + R"rawliteral(%"></div>
                </div>
            </div>
            
            <div class="stat-card">
                <h3>WiFi</h3>
                <div class="stat-value">)rawliteral" + WiFi.localIP().toString() + R"rawliteral(</div>
                <div class="stat-detail">RSSI: )rawliteral" + String(WiFi.RSSI()) + R"rawliteral( dBm</div>
            </div>
        </div>
        
        <footer>
            <div class="refresh-note">Auto-refreshing every 5 seconds</div>
            <div>M5Stack Tab5 Hello World Demo</div>
        </footer>
    </div>
</body>
</html>
)rawliteral";
    
    return html;
}
