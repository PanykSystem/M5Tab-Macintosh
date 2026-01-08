/**
 * @file main.cpp
 * Hello World - M5Stack Tab5 Demo
 * 
 * Features:
 * - Attractive Hello World display
 * - WiFi connection with visual progress
 * - Web server with device stats
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <WiFi.h>
#include "config.h"
#include "ui/ui_init.h"
#include "web_server.h"

// ============================================================================
// Display State
// ============================================================================

String wifiStatusText = "Initializing...";
String ipAddressText = "";
bool wifiConnected = false;

// ============================================================================
// Display Functions
// ============================================================================

void drawScreen() {
    M5Canvas* canvas = ui_get_canvas();
    
    // Clear background with dark color
    canvas->fillScreen(COLOR_BG_DARK);
    
    // Draw "HELLO WORLD" - large and centered
    canvas->setTextDatum(MC_DATUM);  // Middle center
    canvas->setTextColor(COLOR_ACCENT_CYAN);
    canvas->setTextSize(1);
    
    // Use the largest built-in font for the main title
    canvas->setFont(&fonts::FreeSansBold24pt7b);
    canvas->drawString("HELLO WORLD", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 80);
    
    // Draw decorative line under the title
    int lineY = DISPLAY_HEIGHT / 2 - 20;
    canvas->fillRect(DISPLAY_WIDTH / 2 - 200, lineY, 400, 3, COLOR_ACCENT_CYAN);
    
    // Draw WiFi status
    canvas->setFont(&fonts::FreeSans18pt7b);
    canvas->setTextColor(COLOR_TEXT_DIM);
    canvas->drawString(wifiStatusText, DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 + 60);
    
    // Draw IP address if connected
    if (wifiConnected && ipAddressText.length() > 0) {
        canvas->setTextColor(COLOR_ACCENT_GREEN);
        canvas->setFont(&fonts::FreeSansBold18pt7b);
        canvas->drawString(ipAddressText, DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 + 120);
        
        // Draw web server hint
        canvas->setFont(&fonts::FreeSans12pt7b);
        canvas->setTextColor(COLOR_TEXT_DIM);
        canvas->drawString("Web server running on port 80", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 + 180);
    }
    
    // Push to display
    ui_push();
}

void updateWifiStatus(const String& status) {
    wifiStatusText = status;
    drawScreen();
}

// ============================================================================
// WiFi Connection
// ============================================================================

void configureSdioPins() {
    Serial.println("[WiFi] Configuring SDIO pins for ESP32-C6...");
    WiFi.setPins(SDIO2_CLK, SDIO2_CMD, SDIO2_D0, SDIO2_D1, SDIO2_D2, SDIO2_D3, SDIO2_RST);
    Serial.println("[WiFi] SDIO pins configured");
}

bool connectToWifi() {
    Serial.printf("[WiFi] Connecting to %s...\n", WIFI_SSID);
    updateWifiStatus("Connecting to " + String(WIFI_SSID) + "...");
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    unsigned long startTime = millis();
    int dotCount = 0;
    
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("\n[WiFi] Connection timeout!");
            updateWifiStatus("Connection failed - timeout");
            return false;
        }
        
        // Update display with animated dots
        dotCount = (dotCount + 1) % 4;
        String dots = "";
        for (int i = 0; i < dotCount + 1; i++) {
            dots += ".";
        }
        updateWifiStatus("Connecting to " + String(WIFI_SSID) + dots);
        
        Serial.print(".");
        delay(500);
    }
    
    Serial.println();
    Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    
    wifiConnected = true;
    ipAddressText = WiFi.localIP().toString();
    updateWifiStatus("Connected to " + String(WIFI_SSID));
    
    return true;
}

// ============================================================================
// Setup
// ============================================================================

void setup() {
    // Initialize M5Stack Tab5
    auto cfg = M5.config();
    M5.begin(cfg);
    
    Serial.begin(115200);
    delay(500);
    
    Serial.println("\n\n========================================");
    Serial.println("   Hello World - M5Stack Tab5");
    Serial.println("========================================\n");
    
    // Configure display orientation (landscape)
    M5.Display.setRotation(3);
    M5.Display.fillScreen(TFT_BLACK);
    
    Serial.printf("[App] Display: %dx%d\n", M5.Display.width(), M5.Display.height());
    Serial.printf("[App] Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("[App] Free PSRAM: %d bytes\n", ESP.getFreePsram());
    
    // Initialize UI canvas
    Serial.println("[App] Initializing display...");
    ui_init();
    
    // Initial draw
    drawScreen();
    
    // Configure WiFi SDIO pins (required for Tab5)
    configureSdioPins();
    
    // Connect to WiFi
    if (connectToWifi()) {
        // Start web server
        Serial.println("[App] Starting web server...");
        webServer.begin();
        Serial.println("[App] Web server started on port 80");
    } else {
        Serial.println("[App] WiFi connection failed, web server not started");
    }
    
    Serial.println("[App] Setup complete\n");
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
    // Update M5Stack (handles touch, buttons)
    M5.update();
    
    // Small delay to prevent CPU hogging
    delay(10);
}
