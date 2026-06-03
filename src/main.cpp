#include <Arduino.h>
#include <WiFi.h>
#include <BleCombo.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <LittleFS.h>
#include <esp_system.h>
#include <esp_mac.h>

#include "BuckyParser.h" // Includes Parser logic and ANSI Color definitions (C_RED, C_GREEN, etc.)

// =========================================================
// HARDWARE & PIN DEFINITIONS
// =========================================================
#define BTN_PIN 0       // Default ESP32 BOOT button
#define BTN_LEVEL LOW

// =========================================================
// NETWORK & BLE GLOBALS
// =========================================================
const char* ssid = "bucky";
const char* password = "BuckyAdmin2026!"; // WPA2 Password for security
WiFiServer telnetServer(23);
WiFiClient client;
BLEScan* pBLEScan;

// =========================================================
// FILE SYSTEM GLOBALS
// =========================================================
const char* ACTIVE_CONFIG_FILE = "/active_config";
const char* TARGET_CONFIG_FILE = "/target_profile";
const char* NAMES_CONFIG_FILE  = "/names_config";

File editorFile;
bool readingScript = false;
String scriptTargetName = "";

// =========================================================
// STATE & CLI GLOBALS
// =========================================================
String serialCommandBuffer = "";
String telnetCommandBuffer = "";

int currentProfile = 1;
String profileNames[3] = {"Bucky_1", "Bucky_2", "Bucky_3"};

bool liveMode = false;
bool liveMouseMode = false;

// =========================================================
// HELPER: ANSI FILTER & DUAL PRINTING
// =========================================================

String stripANSI(String input) {
    String output = "";
    bool inANSI = false;
    for (int i = 0; i < input.length(); i++) {
        char c = input.charAt(i);
        if (c == '\033') { 
            inANSI = true; 
        } else if (inANSI) {
            // ANSI codes always end with a letter
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                inANSI = false; 
            }
        } else {
            output += c; 
        }
    }
    return output;
}

void printDual(const String& msg) {
    Serial.print(stripANSI(msg)); 
    if (client && client.connected()) {
        client.print(msg); 
    }
}

void printlnDual(const String& msg) {
    Serial.println(stripANSI(msg)); 
    if (client && client.connected()) {
        client.println(msg); 
    }
}

void printPrompt() {
    printDual(String("\r") + String(C_CYAN) + "root@bucky:~# " + String(C_RESET));
}

// =========================================================
// CONFIGURATION MANAGEMENT
// =========================================================

String getActiveScript() {
    if (!LittleFS.exists(ACTIVE_CONFIG_FILE)) return "";
    File f = LittleFS.open(ACTIVE_CONFIG_FILE, "r");
    if (!f) return "";
    String activeName = f.readStringUntil('\n');
    activeName.trim();
    f.close();
    return activeName;
}

bool setActiveScript(String filename) {
    if (!filename.startsWith("/")) filename = "/" + filename;
    if (!LittleFS.exists(filename)) return false;
    File f = LittleFS.open(ACTIVE_CONFIG_FILE, "w");
    if (!f) return false;
    f.println(filename);
    f.close();
    return true;
}

bool isSystemFile(String name) {
    return (name == "active_config" || name == "target_profile" || name == "names_config");
}

// =========================================================
// UI & DASHBOARD RENDERING
// =========================================================

void printTree(String path, String prefix) {
    File dir = LittleFS.open(path);
    if (!dir || !dir.isDirectory()) return;

    File file = dir.openNextFile();
    while (file) {
        String fname = String(file.name());
        int lastSlash = fname.lastIndexOf('/');
        String shortName = (lastSlash >= 0) ? fname.substring(lastSlash + 1) : fname;
        if (isSystemFile(shortName)) file = dir.openNextFile();
        else break;
    }

    while (file) {
        String fname = String(file.name());
        int lastSlash = fname.lastIndexOf('/');
        String shortName = (lastSlash >= 0) ? fname.substring(lastSlash + 1) : fname;

        File nextValidFile;
        File temp = dir.openNextFile();
        while (temp) {
            String tempName = String(temp.name());
            int ts = tempName.lastIndexOf('/');
            String tShortName = (ts >= 0) ? tempName.substring(ts + 1) : tempName;
            if (!isSystemFile(tShortName)) {
                nextValidFile = temp;
                break;
            }
            temp = dir.openNextFile();
        }

        bool isThisLast = !nextValidFile;
        String active = getActiveScript();
        String fullPath = path;
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += shortName;

        String connector = isThisLast ? "└── " : "├── ";
        String line = String(C_GRAY) + prefix + connector + String(C_RESET);

        if (file.isDirectory()) {
            printlnDual("  " + line + C_BLUE + shortName + C_RESET);
            String newPrefix = prefix + (isThisLast ? "    " : "│   ");
            printTree(fullPath, newPrefix); 
        } else {
            line += shortName + C_GRAY + " (" + String(file.size()) + "b)" + C_RESET;
            if (fullPath == active || String("/") + shortName == active) {
                line += String(C_RED) + "  <-- (ACTIVE)" + String(C_RESET);
            }
            printlnDual("  " + line);
        }
        file = nextValidFile; 
    }
}

String getStorageProgressBar() {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    
    if (total == 0) return "[Storage Error]";

    int percentage = (used * 100) / total;
    int barWidth = 10;
    int filledBlocks = percentage / 10; 

    String bar = "[";
    for (int i = 0; i < barWidth; i++) {
        bar += (i < filledBlocks) ? "█" : "░";
    }
    bar += "] " + String(percentage) + "% (" + String(used / 1024) + "KB / " + String(total / 1024) + "KB)";
    return bar;
}

void printDashboard() {
    printDual(C_CLEAR); 
    
    printlnDual(String(C_YELLOW) + "      ,~~.");
    printlnDual("     (  9 )-_,");
    printlnDual("(\\___ )=='-'");
    printlnDual(" \\ .   ) )" + String(C_CYAN) + "    Bucky OS - Payload Injector");
    printlnDual(String(C_YELLOW) + "  \\ `-' /" + String(C_RESET) + "     ID: " + String(C_GREEN) + "[" + String(currentProfile) + "] " + profileNames[currentProfile-1] + String(C_RESET));
    printlnDual(String(C_YELLOW) + "   `~j-'" + String(C_RESET) + "      BLE Target : " + String(Keyboard.isConnected() ? String(C_GREEN)+"[CONNECTED]" : String(C_RED)+"[WAITING]"));
    printlnDual(String(C_YELLOW) + "    \"=:" + String(C_RESET) + "      Storage    : " + String(C_CYAN) + getStorageProgressBar() + String(C_RESET)); 
    
    printlnDual(String(C_CYAN) + "[ ROOT FILE SYSTEM ]" + C_RESET);
    printlnDual(String(C_BLUE) + "  /" + C_RESET);
    
    bool hasFiles = false;
    File root = LittleFS.open("/");
    File f = root.openNextFile();
    while(f) {
        String fn = String(f.name());
        int ls = fn.lastIndexOf('/');
        String sn = (ls >= 0) ? fn.substring(ls + 1) : fn;
        if (!isSystemFile(sn)) { hasFiles = true; break; }
        f = root.openNextFile();
    }

    if (!hasFiles) printlnDual(String(C_GRAY) + "  └── [ Empty ]" + C_RESET);
    else printTree("/", "");
    
    printlnDual(String(C_GRAY) + "------------------------------------------------" + C_RESET);
}

// =========================================================
// COMMAND PROCESSING LOGIC
// =========================================================

String getParam(String cmd, int offset) {
    if (cmd.length() <= offset) return "";
    String p = cmd.substring(offset);
    p.trim();
    return p;
}

void processCommand(String command) {
    command.trim();

    // --- 1. TEXT EDITOR INTERCEPTOR (Direct Disk Streaming) ---
    if (readingScript) {
        // Create a temporary uppercase string to ensure case-insensitive exit check
        String exitCheck = command;
        exitCheck.toUpperCase();

        if (exitCheck == "END") {
            readingScript = false;
            editorFile.close();
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] File " + scriptTargetName + " saved successfully!" + C_RESET);
        } else {
            // Write the original command line to flash memory (preserving original casing)
            if (editorFile) {
                editorFile.println(command);
            }
        }
        if (!readingScript) printPrompt();
        return;
    }

    // --- 2. EMPTY COMMAND FALLBACK ---
    if (command.length() == 0) { 
        printDashboard(); 
        printPrompt(); 
        return; 
    }

    // Action execution: Refresh UI before printing command output
    printDashboard();
    printlnDual(String(C_GRAY) + ">>> " + command + C_RESET + "\n");

    // --- 3. COMMAND ROUTING ---
    if (command == "help") {
        printlnDual(String(C_CYAN) + "[ TARGET & BLUETOOTH ]" + C_RESET);
        printlnDual(String(C_GRAY) + "  target <1-3>  : Switch BT identity to attack a different PC");
        printlnDual("  rename <name> : Change Bluetooth name of current identity");
        printlnDual("  scan          : Scan for nearby BLE targets (5s)");
        printlnDual("  run <files>   : Run one or multiple scripts chained via BLE");
        printlnDual(String("  live          : Enter Live Control Mode (Remote Keyboard/Mouse)\n") + C_RESET);
        
        printlnDual(String(C_CYAN) + "[ FOLDERS & FILES ]" + C_RESET);
        printlnDual(String(C_GRAY) + "  mkdir <dir>   : Create a new folder");
        printlnDual("  rmdir <dir>   : Delete an empty folder");
        printlnDual("  cat <file>    : Read script content");
        printlnDual("  write <file>  : Create/overwrite a script (Opens editor)");
        printlnDual("  rm <file>     : Delete a script");
        printlnDual(String("  set <file>    : Arm a script for the physical BOOT button\n") + C_RESET);

        printlnDual(String(C_CYAN) + "[ SYSTEM ]" + C_RESET);
        printlnDual(String(C_GRAY) + "  reboot        : Restart the Bucky OS device" + C_RESET);
    } 
    else if (command.startsWith("target ")) {
        int t = getParam(command, 7).toInt();
        if (t >= 1 && t <= 3) {
            File f = LittleFS.open(TARGET_CONFIG_FILE, "w");
            if(f) {
                f.println(t);
                f.close();
                printlnDual(String(C_GREEN) + "[+] Target Profile changed to: " + String(t) + C_RESET);
                printlnDual(String(C_YELLOW) + "[*] Rebooting ESP32... Reconnect in 2s." + C_RESET);
                delay(1000);
                ESP.restart(); 
            }
        } else {
            printlnDual(String(C_RED) + "[-] Invalid profile. Please choose 1, 2, or 3." + C_RESET);
        }
    }
    else if (command.startsWith("rename ")) {
        String newName = getParam(command, 7);
        if (newName.length() > 0 && newName.length() <= 30) {
            profileNames[currentProfile - 1] = newName;
            File f = LittleFS.open(NAMES_CONFIG_FILE, "w");
            if(f) {
                for(int i=0; i<3; i++) f.println(profileNames[i]);
                f.close();
                printlnDual(String(C_GREEN) + "[+] Identity " + String(currentProfile) + " renamed to: " + newName + C_RESET);
                printlnDual(String(C_YELLOW) + "[*] Rebooting ESP32 to apply BLE name..." + C_RESET);
                delay(1000);
                ESP.restart(); 
            }
        } else {
            printlnDual(String(C_RED) + "[-] Invalid name." + C_RESET);
        }
    }
    else if (command == "live") {
        if (!Keyboard.isConnected()) {
            printlnDual(String(C_RED) + "[-] Error: BLE target not connected." + C_RESET);
            return;
        }
        liveMode = true;
        liveMouseMode = false;
        printlnDual(String(C_CLEAR));
        printlnDual(String(C_YELLOW) + ">>> 🔴 LIVE CONTROL MODE ACTIVE <<<" + C_RESET);
        printlnDual(String(C_CYAN) + "[ KEYBOARD MODE ]" + C_RESET);
        printlnDual(String(C_GRAY) + "  Type normally. Inputs are sent instantly." + C_RESET);
        printlnDual(String(C_YELLOW) + "  Press '#' to toggle MOUSE mode." + C_RESET);
        printlnDual(String(C_RED) + "  Press '~' to EXIT Live Mode." + C_RESET);
    }
    else if (command == "reboot" || command == "restart") {
        printlnDual(String(C_YELLOW) + "[*] Rebooting Bucky OS..." + C_RESET);
        printlnDual(String(C_GRAY) + "Connection will be lost. Reconnect in a few seconds." + C_RESET);
        delay(1000); 
        ESP.restart();
    }
    else if (command.startsWith("set ")) {
        String fn = getParam(command, 4);
        if (setActiveScript(fn)) {
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] Armed for physical button: " + fn + C_RESET);
        } else {
            printlnDual(String(C_RED) + "[-] Error: Script not found." + C_RESET);
        }
    }
    else if (command.startsWith("mkdir ")) {
        String dir = getParam(command, 6);
        if (!dir.startsWith("/")) dir = "/" + dir;
        if (LittleFS.mkdir(dir)) {
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] Folder created: " + dir + C_RESET);
        } else printlnDual(String(C_RED) + "[-] Failed to create folder." + C_RESET);
    }
    else if (command.startsWith("rmdir ")) {
        String dir = getParam(command, 6);
        if (!dir.startsWith("/")) dir = "/" + dir;
        if (LittleFS.rmdir(dir)) {
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] Folder deleted: " + dir + C_RESET);
        } else printlnDual(String(C_RED) + "[-] Failed to delete folder (Must be empty)." + C_RESET);
    }
    else if (command.startsWith("cat ")) {
        String fn = getParam(command, 4);
        if (!fn.startsWith("/")) fn = "/" + fn;
        if (!LittleFS.exists(fn)) { 
            printlnDual(String(C_RED) + "[-] File not found." + C_RESET); 
        } else {
            File f = LittleFS.open(fn, "r");
            while (f.available()) {
                char ch = f.read();
                Serial.write(ch);
                if (client && client.connected()) client.write(ch);
            }
            f.close();
        }
    } 
    else if (command.startsWith("write ")) {
        scriptTargetName = getParam(command, 6);
        if (scriptTargetName.length() > 0) {
            if (!scriptTargetName.startsWith("/")) scriptTargetName = "/" + scriptTargetName;
            
            editorFile = LittleFS.open(scriptTargetName, "w");
            if (!editorFile) {
                printlnDual(String(C_RED) + "[-] FS Error: Cannot create file." + C_RESET);
                return;
            }

            readingScript = true;
            printlnDual(String(C_CYAN) + "=== 📝 EDITOR: " + scriptTargetName + " ===" + C_RESET);
            printlnDual(String(C_YELLOW) + "[ TEXT & TYPING ]" + C_RESET);
            printlnDual(String(C_GRAY) + "  STRING <txt>   : Types text (e.g., STRING Hello World)");
            printlnDual("  STRINGLN <txt> : Types text and presses Enter");
            
            printlnDual(String(C_YELLOW) + "[ SPECIAL KEYS & COMBOS ]" + C_RESET);
            printlnDual(String(C_GRAY) + "  ENTER, SPACE, TAB, ESC, BACKSPACE, UP, DOWN, LEFT, RIGHT");
            printlnDual("  CTRL, ALT, SHIFT, GUI (e.g., CTRL SHIFT ESC, GUI r)");
            
            printlnDual(String(C_YELLOW) + "[ DELAYS & MOUSE ]" + C_RESET);
            printlnDual(String(C_GRAY) + "  DELAY <ms>       : Single pause (e.g., DELAY 1000)");
            printlnDual("  DEFAULTDELAY <ms>: Auto-pause between every line");
            printlnDual(String("  MOUSE_MOVE <x> <y> / MOUSE_CLICK [LEFT/RIGHT/MIDDLE]") + C_RESET);
            
            printlnDual(String(C_CYAN) + "------------------------------------------------" + C_RESET);
            printlnDual(String(C_GREEN) + "-> Type manually or paste your script." + C_RESET);
            printlnDual(String(C_GREEN) + "-> Type 'END' on a new line to SAVE and exit." + C_RESET);
        } else {
            printlnDual(String(C_RED) + "[-] Usage: write <filename>" + C_RESET);
        }
    }
    else if (command.startsWith("rm ")) {
        String fn = getParam(command, 3);
        if (!fn.startsWith("/")) fn = "/" + fn;
        if (LittleFS.remove(fn)) {
            if (getActiveScript() == fn) LittleFS.remove(ACTIVE_CONFIG_FILE); 
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] File deleted: " + fn + C_RESET);
        } else {
            printlnDual(String(C_RED) + "[-] Delete failed." + C_RESET);
        }
    } 
    else if (command.startsWith("run ")) {
        if (Keyboard.isConnected()) {
            String args = command.substring(4);
            args.trim();
            
            int start = 0;
            int spaceIdx;
            int scriptCount = 0;
            
            // Payload Chaining Logic
            do {
                spaceIdx = args.indexOf(' ', start);
                String file = (spaceIdx == -1) ? args.substring(start) : args.substring(start, spaceIdx);
                file.trim();
                
                if (file.length() > 0) {
                    scriptCount++;
                    if (scriptCount > 1) {
                        printlnDual(String(C_CYAN) + "--- Chaining Next Script (" + String(scriptCount) + ") ---" + C_RESET);
                        delay(500); // Target PC buffer recovery time
                    }
                    BuckyParser::runScript(file); // Executed via BuckyParser library
                }
                start = spaceIdx + 1;
            } while (spaceIdx != -1);
            
            if (scriptCount > 1) {
                printlnDual(String(C_GREEN) + "[+] Chain completed: " + String(scriptCount) + " scripts executed." + C_RESET);
            }
            
        } else {
            printlnDual(String(C_RED) + "[-] Error: BLE target not connected." + C_RESET);
        }
    }
    else if (command == "scan") {
        printlnDual(String(C_YELLOW) + "[*] Scanning for BLE Devices (5s)..." + C_RESET);
        BLEScanResults foundDevices = pBLEScan->start(5, false);
        printlnDual(String(C_GREEN) + "[+] Found " + String(foundDevices.getCount()) + " devices:" + C_RESET);
        for (int i = 0; i < foundDevices.getCount(); i++) {
            BLEAdvertisedDevice d = foundDevices.getDevice(i);
            String dName = d.getName().c_str();
            if (dName.length() == 0) dName = "[Hidden/Unknown]";
            printlnDual("  [" + String(i) + "] " + dName + " | " + String(C_GRAY) + String(d.getAddress().toString().c_str()) + C_RESET);
        }
        pBLEScan->clearResults();
    } 
    else {
        // Direct single/chained command execution (e.g. typing "GUI r; DELAY 500; STRING cmd")
        if (Keyboard.isConnected()) {
            int start = 0;
            int end = command.indexOf(';');
            while (start < command.length()) {
                String sub = (end == -1) ? command.substring(start) : command.substring(start, end);
                BuckyParser::executeCommand(sub); 
                start = (end == -1) ? command.length() : end + 1;
                end = command.indexOf(';', start);
            }
            printlnDual(String(C_GREEN) + "[+] Command executed." + C_RESET);
        } else {
            printlnDual(String(C_RED) + "[-] BLE disconnected or unknown command." + C_RESET);
        }
    }
    
    if (!readingScript) printPrompt();
}

// =========================================================
// HELPER: LIVE MODE INTERCEPTOR (DRY)
// =========================================================

bool handleLiveModeInput(char c) {
    if (!liveMode) return false;

    // State memory to intercept 3-byte ANSI arrow key sequences (e.g., ESC [ A)
    static int ansiState = 0;

    // --- 1. ANSI ESCAPE SEQUENCE INTERCEPTOR (For Arrow Keys) ---
    if (ansiState == 1) {
        if (c == '[') {
            ansiState = 2; // Second byte confirmed, waiting for direction
            return true;
        } else {
            ansiState = 0; // False alarm, it was just a raw ESC key press
            if (!liveMouseMode) Keyboard.write(KEY_ESC);
            // Continue processing 'c' normally
        }
    }
    else if (ansiState == 2) {
        ansiState = 0; // Reset state
        int step = 15; // Movement step in pixels
        
        if (liveMouseMode) {
            // Mouse Mode: Arrows move the cursor
            if (c == 'A') Mouse.move(0, -step + 1, 0);      // Up Arrow
            else if (c == 'B') Mouse.move(0, step, 0);  // Down Arrow
            else if (c == 'C') Mouse.move(step, 0, 0);  // Right Arrow
            else if (c == 'D') Mouse.move(-step + 1, 0, 0); // Left Arrow
        } else {
            // Keyboard Mode: Send arrow keycodes to target PC
            if (c == 'A') Keyboard.write(KEY_UP_ARROW);
            else if (c == 'B') Keyboard.write(KEY_DOWN_ARROW);
            else if (c == 'C') Keyboard.write(KEY_RIGHT_ARROW);
            else if (c == 'D') Keyboard.write(KEY_LEFT_ARROW);
        }
        return true; 
    }

    if (c == 27) { // 27 is the ASCII code for ESC (\033), start of sequence
        ansiState = 1;
        return true;
    }

    // --- 2. STANDARD LIVE MODE CONTROLS ---
    if (c == '~') { // EXIT KEY
        liveMode = false;
        printDashboard();
        printPrompt();
    } 
    else if (c == '#') { // TOGGLE MOUSE/KEYBOARD KEY
        liveMouseMode = !liveMouseMode;
        if (liveMouseMode) {
            printlnDual(String(C_CYAN) + "\r\n[ MOUSE MODE ] WASD/Arrows=Move, Q=LClick, E=RClick, C=Mid, R/F=Scroll" + C_RESET);
        } else {
            printlnDual(String(C_CYAN) + "\r\n[ KEYBOARD MODE ]" + C_RESET);
        }
    } 
    else if (liveMouseMode) {
        // --- MOUSE CONTROLS (WASD & Actions) ---
        int step = 15; 
        if (c == 'w' || c == 'W') Mouse.move(0, -step, 0);
        else if (c == 's' || c == 'S') Mouse.move(0, step, 0);
        else if (c == 'a' || c == 'A') Mouse.move(-step, 0, 0);
        else if (c == 'd' || c == 'D') Mouse.move(step, 0, 0);
        else if (c == 'q' || c == 'Q') Mouse.click(MOUSE_LEFT);
        else if (c == 'e' || c == 'E') Mouse.click(MOUSE_RIGHT);
        else if (c == 'c' || c == 'C') Mouse.click(MOUSE_MIDDLE);
        else if (c == 'r' || c == 'R') Mouse.move(0, 0, 1);  // Scroll UP
        else if (c == 'f' || c == 'F') Mouse.move(0, 0, -1); // Scroll DOWN
    } 
    else {
        // --- KEYBOARD CONTROLS ---
        if (c == '\r') { /* ignore raw carriage returns */ }
        else if (c == '\n') Keyboard.write(KEY_RETURN);
        else if (c == '\b' || c == 127) Keyboard.write(KEY_BACKSPACE);
        else Keyboard.write(c);
    }
    return true; 
}

// =========================================================
// SETUP & MAIN LOOP
// =========================================================

void setup() {
    Serial.begin(115200);
    delay(1000); 
    
    LittleFS.begin(true);
    pinMode(BTN_PIN, INPUT_PULLUP);

    // Read stored identities
    if (LittleFS.exists(NAMES_CONFIG_FILE)) {
        File f = LittleFS.open(NAMES_CONFIG_FILE, "r");
        for (int i = 0; i < 3; i++) {
            if (f.available()) {
                String n = f.readStringUntil('\n');
                n.trim();
                if (n.length() > 0) profileNames[i] = n;
            }
        }
        f.close();
    }

    if (LittleFS.exists(TARGET_CONFIG_FILE)) {
        File f = LittleFS.open(TARGET_CONFIG_FILE, "r");
        currentProfile = f.readStringUntil('\n').toInt();
        f.close();
    }
    if (currentProfile < 1 || currentProfile > 3) currentProfile = 1;
    
    // MAC Address Spoofing to allow quick reconnection to different profiles
    uint8_t custom_mac[6];
    esp_read_mac(custom_mac, ESP_MAC_WIFI_STA); 
    custom_mac[5] += currentProfile; 
    esp_base_mac_addr_set(custom_mac);

    // Initialize BLE Server Stack
    BLEDevice::init(profileNames[currentProfile - 1].c_str());
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);

    Keyboard.begin();
    Mouse.begin();

    // Initialize Wi-Fi AP securely
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password, 1, 1); 
    
    // Start Telnet Service
    telnetServer.begin();
    telnetServer.setNoDelay(true);
    
    printDashboard();
    printPrompt();
}

void loop() {
    // 1. WI-FI CLIENT CONNECTION MANAGEMENT
    if (telnetServer.hasClient()) {
        if (!client || !client.connected()) {
            if (client) client.stop();
            client = telnetServer.available();
            telnetCommandBuffer = ""; // Reset memory upon login
            printDashboard(); 
            printPrompt();
        } else {
            telnetServer.available().stop(); // Reject secondary connections
        }
    }

    // 2. TELNET MANAGEMENT (Wi-Fi)
    if (client && client.connected()) {
        while (client.available()) {
            char c = client.read();
            
            // Route character to Live Mode if active. Skip normal CLI parsing if consumed.
            if (handleLiveModeInput(c)) continue; 
            
            if (c == '\r') continue; 

            // ENTER KEY
            if (c == '\n') {
                processCommand(telnetCommandBuffer);
                telnetCommandBuffer = "";            
                
                if (readingScript) printDual("\r"); // Maintain cursor alignment in editor
            }
            // DELETE/BACKSPACE KEY
            else if (c == '\b' || c == 127) {
                if (telnetCommandBuffer.length() > 0) {
                    telnetCommandBuffer.remove(telnetCommandBuffer.length() - 1);
                    
                    client.print("\033[2K\r");
                    Serial.print("\033[2K\r");
                    
                    if (!readingScript) printPrompt();
                    printDual(telnetCommandBuffer);
                }
            }
            // NORMAL CHARACTERS
            else if (c >= 32 && c <= 126) {
                telnetCommandBuffer += c;
                Serial.print(c); 
            }
        }
    }

    // 3. SERIAL MANAGEMENT (USB Cable)
    while (Serial.available() > 0) {
        char c = Serial.read();
        
        // Route character to Live Mode if active.
        if (handleLiveModeInput(c)) continue;

        if (c == '\r') continue; 

        // ENTER KEY
        if (c == '\n') {
            processCommand(serialCommandBuffer); 
            serialCommandBuffer = "";            
            
            if (readingScript) printDual("\r\n"); 
        }
        // DELETE/BACKSPACE KEY
        else if (c == '\b' || c == 0x7F) {
            if (serialCommandBuffer.length() > 0) {
                serialCommandBuffer.remove(serialCommandBuffer.length() - 1);
                
                client.print("\033[2K\r");
                Serial.print("\033[2K\r");
                
                if (!readingScript) printPrompt();
                printDual(serialCommandBuffer);
            }
        }
        // NORMAL CHARACTERS
        else if (c >= 32 && c <= 126) {
            serialCommandBuffer += c;
            Serial.print(c); 
            if (client && client.connected()) client.print(c); 
        }
    }

    // 4. PHYSICAL BOOT BUTTON (Payload Trigger)
    static bool lastHigh = true;
    bool high = (digitalRead(BTN_PIN) != BTN_LEVEL);
    if (lastHigh && !high) {
        String activeScript = getActiveScript();
        
        printDashboard(); 
        if (activeScript == "") {
            printlnDual(String(C_RED) + "\n[-] No script set for button." + C_RESET);
        } else if (!Keyboard.isConnected()) {
            printlnDual(String(C_RED) + "\n[-] BLE Target not connected." + C_RESET);
        } else {
            BuckyParser::runScript(activeScript);
        }
        printPrompt();
    }
    lastHigh = high;

    delay(10); // CPU yielding for RTOS stability
}