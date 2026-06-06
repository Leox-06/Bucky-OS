#ifndef BUCKY_CONFIG_H
#define BUCKY_CONFIG_H

#include <Arduino.h>

namespace BuckyConfig {

    // --- NETWORK & CONNECTIVITY ---
    static const char WIFI_SSID[] = "bucky";
    static const char WIFI_PASSWORD[] = "BuckyAdmin2026!";     // Default WPA2 access point password
    static constexpr uint16_t TELNET_PORT = 23;
    static constexpr uint8_t WIFI_CHANNEL = 1;
    static constexpr bool WIFI_HIDDEN_SSID = false;

    // --- HARDWARE PINOUT ---
    static constexpr uint8_t BTN_PIN = 0;                      // Default ESP32 BOOT button (Payload Trigger)
    static constexpr uint8_t BTN_ACTIVE_LEVEL = LOW;

    // --- FILE SYSTEM & PERSISTENCE ---
    static const char CONFIG_FILE_PATH[] = "/config";          // Unified registry file
    static const char DEFAULT_LAYOUT[] = "US";
    static const char DEFAULT_PROFILE_NAMES[3][16] = {
        "Bucky_1",
        "Bucky_2",
        "Bucky_3"
    };

    // --- HID MOUSE CALIBRATION ---
    // Asymmetric dithering for sub-pixel precision across different OS acceleration curves
    static constexpr int8_t MOUSE_STEP_FORWARD = 15;           // +15 pixels when moving positive
    static constexpr int8_t MOUSE_STEP_BACK = -14;             // -14 pixels when moving negative

    // --- MEMORY & BUFFER LIMITS ---
    // Strict bounds to prevent heap fragmentation, buffer overflows, and DoS
    static constexpr size_t MAX_COMMAND_LENGTH = 256;          // Max CLI command input size
    static constexpr size_t MAX_LIVE_CMD_TOKEN = 32;           // Max {bracketed} token length in Live Mode
    static constexpr size_t FILE_READ_BUFFER_SIZE = 128;       // Memory chunk size for safe script execution

    // --- TIMEOUTS & UI ---
    static constexpr uint16_t TELNET_IDLE_TIMEOUT_MS = 60000;  // Drop dead connections after 60 seconds
    static constexpr size_t STORAGE_BAR_WIDTH = 10;            // Character width of the dashboard storage bar
}

#endif // BUCKY_CONFIG_H