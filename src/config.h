/**
 * @file config.h
 * Hello World Configuration
 * M5Stack Tab5 Demo Application
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// Tab5 Hardware Configuration
// ============================================================================

// WiFi SDIO pins for ESP32-C6 communication
// The Tab5 uses ESP32-P4 which has no native WiFi - WiFi is provided
// by the ESP32-C6 co-processor via SDIO bus
#define SDIO2_CLK GPIO_NUM_12
#define SDIO2_CMD GPIO_NUM_13
#define SDIO2_D0  GPIO_NUM_11
#define SDIO2_D1  GPIO_NUM_10
#define SDIO2_D2  GPIO_NUM_9
#define SDIO2_D3  GPIO_NUM_8
#define SDIO2_RST GPIO_NUM_15

// Display dimensions
#define SCREEN_WIDTH  1280
#define SCREEN_HEIGHT 720

// ============================================================================
// WiFi Configuration
// ============================================================================

#define WIFI_SSID               "SvensHaus"
#define WIFI_PASSWORD           "montreal19"
#define WIFI_CONNECT_TIMEOUT_MS 15000

// ============================================================================
// Web Server Configuration
// ============================================================================

#define WEB_SERVER_PORT         80

#endif // CONFIG_H
