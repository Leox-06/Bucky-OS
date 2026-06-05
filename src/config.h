#ifndef BUCKY_CONFIG_H
#define BUCKY_CONFIG_H

#include <Arduino.h>

namespace BuckyConfig {
    // Wi-Fi and Telnet configuration
    static const char WIFI_SSID[] = "bucky";
    static const char WIFI_PASSWORD[] = "BuckyAdmin2026!";
    static constexpr uint16_t TELNET_PORT = 23;
    static constexpr uint8_t WIFI_CHANNEL = 1;
    static constexpr bool WIFI_HIDDEN_SSID = false;

    // Hardware pin definitions
    static constexpr uint8_t BTN_PIN = 0;        // Default ESP32 BOOT button
    static constexpr uint8_t BTN_ACTIVE_LEVEL = LOW;

    // File system and persistent settings
    static const char CONFIG_FILE_PATH[] = "/config";
    static const char DEFAULT_LAYOUT[] = "US";
    static const char DEFAULT_PROFILE_NAMES[3][16] = {
        "Bucky_1",
        "Bucky_2",
        "Bucky_3"
    };

    // BLE state labels (for UI and configuration persistence)
    static const char BLE_STATUS_CONNECTED[] = "[CONNECTED]";
    static const char BLE_STATUS_WAITING[] = "[WAITING]";

    // Mouse control (asymmetric dithering for sub-pixel precision)
    static constexpr int8_t MOUSE_STEP_FORWARD = 15;   // +15 pixels when moving positive
    static constexpr int8_t MOUSE_STEP_BACK = -14;     // -14 pixels when moving negative
    // Net bias: +1 per cycle allows sub-pixel targeting via manual dithering

    // Input buffer limits (anti-DoS, prevent heap exhaustion)
    static constexpr size_t MAX_COMMAND_LENGTH = 256;   // Max CLI command bytes
    static constexpr size_t MAX_LIVE_CMD_TOKEN = 32;    // Max {bracketed} token length
    static constexpr size_t FILE_READ_BUFFER_SIZE = 128;     // Chunk size for file operations

    // Telnet & connection management
    static constexpr uint16_t TELNET_IDLE_TIMEOUT_MS = 60000;  // 60 sec idle before drop
    static constexpr uint8_t TELNET_WRITE_RETRIES = 3;         // Retry stalled writes
    static constexpr uint16_t TELNET_WRITE_BUFFER_SIZE = 512;  // Max Telnet TX buffer

    // UI & Dashboard configuration
    static constexpr size_t STORAGE_BAR_WIDTH = 10;     // Progress bar width in chars
    static constexpr uint16_t DASHBOARD_REFRESH_DELAY = 200;  // ms between redraws
    static constexpr bool SHOW_LOGO_ON_STARTUP = true; // ASCII duck logo
}

#endif // BUCKY_CONFIG_H
