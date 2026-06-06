/**
 * ============================================================================
 * BUCKY-OS (v1.0.0) - Commercial Release
 * Open-Source Payload Injector & Remote Administration Tool
 * ============================================================================
 */

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

#include "BuckyParser.h"
#include "config.h"

// =========================================================
// NETWORK & BLE GLOBALS
// =========================================================
WiFiServer telnetServer(BuckyConfig::TELNET_PORT);
WiFiClient client;
BLEScan* pBLEScan;
static unsigned long lastTelnetActivity = 0;

// =========================================================
// FILE SYSTEM & SYSTEM STATE GLOBALS
// =========================================================
File editorFile;
bool readingScript = false;
String scriptTargetName = "";

String armedScript = "";
int currentProfile = 1;
String profileNames[3]; // Initialized in loadConfiguration()
String fsCache = "";    // Cached File System Tree

// =========================================================
// CLI & INTERACTIVE GLOBALS
// =========================================================
String serialCommandBuffer = "";
String telnetCommandBuffer = "";
bool liveMode = false;
bool liveMouseMode = false;

// =========================================================
// HELPER: DUAL PRINTING (NON-BLOCKING DIRECT TRANSMISSION)
// =========================================================

void printDual(const String& msg) {
    Serial.print(msg); 
    if (client && client.connected() && msg.length() > 0) {
        client.write((const uint8_t*)msg.c_str(), msg.length());
    }
}

void printlnDual(const String& msg) {
    Serial.print(msg); Serial.print("\r\n");
    if (client && client.connected()) {
        String fullMsg = msg + "\r\n";
        client.write((const uint8_t*)fullMsg.c_str(), fullMsg.length());
    }
}

void printPrompt() {
    printDual(String("\r") + F(C_CYAN "root@bucky:~# " C_RESET));
}

// =========================================================
// CENTRALIZED CONFIGURATION SYSTEM
// =========================================================

String getActiveScript() {
    return armedScript;
}

void saveConfiguration() {
    File f = LittleFS.open(BuckyConfig::CONFIG_FILE_PATH, "w");
    if (f) {
        f.println(String("LAYOUT=") + BuckyParser::currentLayout);
        f.println(String("ARMED_SCRIPT=") + armedScript);
        f.println(String("CURRENT_PROFILE=") + String(currentProfile));
        f.println(String("PROFILE_1=") + profileNames[0]);
        f.println(String("PROFILE_2=") + profileNames[1]);
        f.println(String("PROFILE_3=") + profileNames[2]);
        f.close();
    }
}

void loadConfiguration() {
    BuckyParser::currentLayout = BuckyConfig::DEFAULT_LAYOUT;
    armedScript = "";
    currentProfile = 1;
    for(int i=0; i<3; i++) profileNames[i] = BuckyConfig::DEFAULT_PROFILE_NAMES[i];

    if (LittleFS.exists(BuckyConfig::CONFIG_FILE_PATH)) {
        File f = LittleFS.open(BuckyConfig::CONFIG_FILE_PATH, "r");
        if (f) {
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                
                int eqIdx = line.indexOf("=");
                if (eqIdx == -1) continue;
                
                String key = line.substring(0, eqIdx);
                String val = line.substring(eqIdx + 1);
                
                if (key == "LAYOUT") {
                    if (val == BuckyConfig::LAYOUT_IT || val == BuckyConfig::LAYOUT_US) BuckyParser::currentLayout = val;
                }
                else if (key == "ARMED_SCRIPT") armedScript = val;
                else if (key == "CURRENT_PROFILE") {
                    int p = val.toInt();
                    if (p >= 1 && p <= 3) currentProfile = p;
                }
                else if (key == "PROFILE_1") profileNames[0] = val;
                else if (key == "PROFILE_2") profileNames[1] = val;
                else if (key == "PROFILE_3") profileNames[2] = val;
            }
            f.close();
        } else {
            printlnDual(String(F(C_RED "[-] Error opening config file for reading." C_RESET)));
        }
    }
}

bool setActiveScript(String filename) {
    if (!filename.startsWith("/")) filename = "/" + filename;
    if (!LittleFS.exists(filename)) return false;
    armedScript = filename;
    saveConfiguration(); 
    return true;
}

bool isSystemFile(String name) {
    return (name == "config");
}

// =========================================================
// UI & DASHBOARD RENDERING (ANTI-LEAK & FLASH OPTIMIZED)
// =========================================================

void buildTree(String path, String prefix, String& output) {
    File dir = LittleFS.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
    }

    std::vector<File> files;
    File file = dir.openNextFile();
    while (file) {
        String fname = String(file.name());
        int lastSlash = fname.lastIndexOf('/');
        String shortName = (lastSlash >= 0) ? fname.substring(lastSlash + 1) : fname;

        if (!isSystemFile(shortName)) files.push_back(file);
        else file.close();
        file = dir.openNextFile();
    }
    dir.close();

    String activeScript = getActiveScript();
    for (size_t i = 0; i < files.size(); ++i) {
        File currentFile = files[i];
        String fname = String(currentFile.name());
        int lastSlash = fname.lastIndexOf('/');
        String shortName = (lastSlash >= 0) ? fname.substring(lastSlash + 1) : fname;

        bool isLast = (i == files.size() - 1);
        String connector = isLast ? "└── " : "├── ";
        String linePrefix = String(C_GRAY) + prefix + connector + String(C_RESET);

        String fullPath = path;
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += shortName;

        if (currentFile.isDirectory()) {
            output += "  " + linePrefix + C_BLUE + shortName + C_RESET + "\r\n";
            String newPrefix = prefix + (isLast ? "    " : "│   ");
            currentFile.close();
            buildTree(fullPath, newPrefix, output); 
        } else {
            String fileDetails = shortName + C_GRAY + " (" + String(currentFile.size()) + "b)" + C_RESET;
            if (fullPath == activeScript || String("/") + shortName == activeScript) {
                fileDetails += String(C_RED) + "  <-- (ACTIVE)" + String(C_RESET);
            }
            output += "  " + linePrefix + fileDetails + "\r\n";
            currentFile.close();
        }
    }
}

void refreshFSCache() {
    fsCache = "";
    fsCache.reserve(1024);
    
    bool hasFiles = false;
    File root = LittleFS.open("/");
    if (root) {
        File f = root.openNextFile();
        while (f) {
            String fn = String(f.name());
            int ls = fn.lastIndexOf('/');
            String sn = (ls >= 0) ? fn.substring(ls + 1) : fn;
            if (!isSystemFile(sn)) {
                hasFiles = true;
                f.close();
                break; 
            }
            f.close(); 
            f = root.openNextFile();
        }
        root.close(); 
    }

    if (!hasFiles) {
        fsCache = String(C_GRAY) + "  └── [ Empty ]\r\n" + C_RESET;
    } else {
        buildTree("/", "", fsCache);
        fsCache += String(C_GRAY) + "------------------------------------------------\r\n" + C_RESET;
    }
}

String getStorageProgressBar() {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    
    if (total == 0) return F("[Storage Error]");

    int percentage = (used * 100) / total;
    int filledBlocks = percentage / 10;

    String bar = "[";
    for (int i = 0; i < BuckyConfig::STORAGE_BAR_WIDTH; i++) {
        bar += (i < filledBlocks) ? "█" : "░";
    }
    bar += "] ";
    bar += percentage;
    bar += "% (";
    bar += (used / 1024);
    bar += "KB / ";
    bar += (total / 1024);
    bar += "KB)";
    return bar;
}

void printDashboard() {
    String out = "";
    out.reserve(3072);

    out += F(C_CLEAR);
    out += F(C_YELLOW "      ,~~\r\n");
    out += F("     (  9 )-_,\r\n");
    out += F("(\\___ )=='-'\r\n");
    out += F(" \\ .   ) )" C_CYAN "    Bucky OS - Payload Injector\r\n");

    out += F(C_YELLOW "  \\ `-' /" C_RESET "    ID: " C_GREEN "[");
    out += currentProfile;
    out += "] ";
    out += profileNames[currentProfile - 1];
    out += F(C_RESET "\r\n");

    out += F(C_YELLOW "   `~j-'" C_RESET "      BLE Target : ");
    if (Keyboard.isConnected()) {
        out += F(C_GREEN "[CONNECTED]" C_RESET "\r\n");
    } else {
        out += F(C_RED "[WAITING]" C_RESET "\r\n");
    }

    String layoutInfo = (BuckyParser::currentLayout == BuckyConfig::LAYOUT_IT) ? F("IT (Windows, iOS/iPadOS)") : F("US (Android, macOS)");
    out += F(C_YELLOW "    \"=:" C_RESET "        Layout     : " C_CYAN);
    out += layoutInfo;
    out += F(C_RESET "\r\n");

    out += F("                Storage    : " C_CYAN);
    out += getStorageProgressBar();
    out += F(C_RESET "\r\n");

    out += F(C_CYAN "[ ROOT FILE SYSTEM ]\r\n" C_RESET);
    out += F(C_BLUE "  /\r\n" C_RESET);

    if (fsCache == "") refreshFSCache();
    out += fsCache;

    printDual(out);
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

    if (readingScript) {
        String exitCheck = command;
        exitCheck.toUpperCase();

        if (exitCheck == "END") {
            readingScript = false;
            editorFile.close();
            refreshFSCache();
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] File " + scriptTargetName + " saved successfully!" + C_RESET);
        } else {
            if (editorFile) editorFile.println(command);
        }
        if (!readingScript) printPrompt();
        return;
    }

    if (command.length() == 0) { 
        printDashboard(); 
        printPrompt(); 
        return; 
    }

    printDashboard();
    printlnDual(String(C_GRAY) + ">>> " + command + C_RESET + "\n");

    if (command == "help") {
        String out = "";
        out.reserve(2560); 
        
        out += F(C_CYAN "=== BUCKY-OS COMMAND REFERENCE ===\r\n\r\n" C_RESET);
        
        out += F(C_YELLOW "[ QUICK START ]\r\n" C_RESET);
        out += F(C_GRAY "  1. Connect your PC/Phone to WiFi 'bucky' (Pass: BuckyAdmin2026!)\r\n");
        out += F("  2. Pair the Target OS to BLE device 'Bucky_1'\r\n");
        out += F("  3. Run a script: run example\r\n\r\n" C_RESET);

        out += F(C_CYAN "[ BLE & IDENTITY MANAGEMENT ]\r\n" C_RESET);
        out += F(C_GRAY "  target <1-3>  : Switch profile (Spoofs MAC & BLE Name)\r\n");
        out += F("  rename <name> : Rename identity (e.g., rename Magic Keyboard)\r\n");
        out += F("  scan          : Discover nearby BLE targets\r\n");
        out += F("  layout <it/us>: Set keyboard layout (IT = Win/iOS, US = Mac/Android)\r\n\r\n" C_RESET);
        
        out += F(C_CYAN "[ FILE SYSTEM & EXECUTION ]\r\n" C_RESET);
        out += F(C_GRAY "  write <file>  : Built-in DuckyScript editor\r\n");
        out += F("  cat <file>    : View script content\r\n");
        out += F("  run <f1> <f2> : Run scripts (e.g., run init.txt payload.txt)\r\n");
        out += F("  set <file>    : Arm script for physical BOOT button\r\n");
        out += F("  mkdir/rmdir   : Manage directories\r\n");
        out += F("  rm <file>     : Delete script\r\n\r\n" C_RESET);

        out += F(C_CYAN "[ REAL-TIME CONTROL ]\r\n" C_RESET);
        out += F(C_GRAY "  live          : Remote HID Mouse & Keyboard Mode\r\n\r\n" C_RESET);

        out += F(C_CYAN "[ SYSTEM ]\r\n" C_RESET);
        out += F(C_GRAY "  reboot        : Soft reset | version: v1.0.0" C_RESET);
        
        printlnDual(out); 
    }
    else if (command.startsWith("target ")) {
        int t = getParam(command, 7).toInt();
        if (t >= 1 && t <= 3) {
            currentProfile = t;
            saveConfiguration(); 
            printlnDual(String(C_GREEN) + "[+] Target Profile changed to: " + String(t) + C_RESET);
            printlnDual(String(C_YELLOW) + "[*] Rebooting ESP32... Reconnect in 2s." + C_RESET);
            delay(1000);
            ESP.restart(); 
        } else printlnDual(String(C_RED) + "[-] Invalid profile. Use 1, 2, or 3 (e.g., target 2)." + C_RESET);
    }
    else if (command.startsWith("rename ")) {
        String newName = getParam(command, 7);
        if (newName.length() > 0 && newName.length() <= 30) {
            profileNames[currentProfile - 1] = newName;
            saveConfiguration(); 
            printlnDual(String(C_GREEN) + "[+] Identity updated. Rebooting..." + C_RESET);
            delay(1000);
            ESP.restart(); 
        } else printlnDual(String(C_RED) + "[-] Invalid name. Must be 1-30 characters (e.g., rename MyKeyboard)." + C_RESET);
    }
    else if (command == "live") {
        if (!Keyboard.isConnected()) {
            printlnDual(String(C_RED) + "[-] Error: BLE target not connected." + C_RESET);
            return;
        }
        liveMode = true;
        liveMouseMode = false;
        
        String out = "";
        out.reserve(1024);
        out += C_CLEAR;
        out += F(C_YELLOW ">>> 🔴 LIVE CONTROL MODE ACTIVE <<<\r\n\r\n" C_RESET);
        out += F(C_CYAN "[ DUCKYSCRIPT MACROS ]\r\n" C_RESET);
        out += F(C_GRAY "  Use {...} to execute complex hardware scancodes instantly.\r\n");
        out += F("  Examples: {ctrl+alt+delete}, {gui+r}, {alt+tab}\r\n");
        out += F("  Keys    : {enter}, {esc}, {tab}, {backspace}, {up}, {down}\r\n\r\n" C_RESET);
        
        out += F(C_CYAN "[ HOTKEYS ]\r\n" C_RESET);
        out += F(C_GRAY "  #  : Toggle MOUSE MODE (WASD = Move, Q/E/C = Click, R/F = Scroll)\r\n");
        out += F("  ~  : EXIT Live Mode\r\n\r\n" C_RESET);
        
        out += F(C_CYAN "[ LITERAL SYMBOLS ]\r\n" C_RESET);
        out += F(C_GRAY "  To type literal hotkey symbols, wrap them: {#} or {~}\r\n\r\n" C_RESET);
        out += F(C_GREEN "Ready. All keystrokes are being routed to target..." C_RESET);
        
        printlnDual(out);
    }
    else if (command.startsWith("layout ")) {
        String lay = getParam(command, 7);
        lay.toUpperCase();
        if (lay == "IT" || lay == "US") {
            BuckyParser::currentLayout = lay;
            saveConfiguration(); 
            printlnDual(String(C_GREEN) + "[+] Target layout switched to: " + BuckyParser::currentLayout + C_RESET);
        } else printlnDual(String(C_RED) + "[-] Invalid layout. Use 'it' or 'us'." + C_RESET);
    }
    else if (command == "reboot" || command == "restart") {
        printlnDual(String(C_YELLOW) + "[*] Rebooting Bucky OS..." + C_RESET);
        delay(1000); 
        ESP.restart();
    }
    else if (command.startsWith("set ")) {
        String fn = getParam(command, 4);
        if (setActiveScript(fn)) {
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] Armed for physical BOOT button." + C_RESET);
        } else printlnDual(String(C_RED) + "[-] Error: Script not found." + C_RESET);
    }
    else if (command.startsWith("mkdir ")) {
        String dir = getParam(command, 6);
        if (!dir.startsWith("/")) dir = "/" + dir;
        if (LittleFS.mkdir(dir)) {
            refreshFSCache();
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] Folder created." + C_RESET);
        } else printlnDual(String(C_RED) + "[-] Failed to create folder. (Does it already exist?)" + C_RESET);
    }
    else if (command.startsWith("rmdir ")) {
        String dir = getParam(command, 6);
        if (!dir.startsWith("/")) dir = "/" + dir;
        if (LittleFS.rmdir(dir)) {
            refreshFSCache();
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] Folder deleted." + C_RESET);
        } else printlnDual(String(C_RED) + "[-] Failed to delete folder (Must be empty and exist)." + C_RESET);
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
            String out = "";
            out.reserve(1024);
            
            out += String(C_CYAN) + "=== 📝 EDITOR: " + scriptTargetName + " ===\r\n\r\n" + C_RESET;
            out += F(C_YELLOW "[ COMMANDS ]\r\n" C_RESET);
            out += F(C_GRAY "  REM <txt>      : Comment (ignored by parser)\r\n");
            out += F("  STRING <txt>   : Inject text payload\r\n");
            out += F("  STRINGLN <txt> : Inject text and press RETURN\r\n\r\n" C_RESET);
            
            out += F(C_YELLOW "[ HARDWARE CONTROLS ]\r\n" C_RESET);
            out += F(C_GRAY "  CTRL, ALT, SHIFT, GUI (e.g., CTRL SHIFT ESC, GUI r)\r\n");
            out += F("  ENTER, SPACE, TAB, ESC, BACKSPACE, UP, DOWN, LEFT, RIGHT\r\n\r\n" C_RESET);
            
            out += F(C_YELLOW "[ DELAYS & MOUSE ]\r\n" C_RESET);
            out += F(C_GRAY "  DELAY <ms>       : Single pause (e.g., DELAY 1000)\r\n");
            out += F("  DEFAULTDELAY <ms>: Global pause between lines\r\n");
            out += F("  MOUSE_MOVE <x> <y> / MOUSE_CLICK [LEFT/RIGHT/MIDDLE]\r\n\r\n" C_RESET);
            
            out += F(C_GREEN "-> Editor active. Paste your DuckyScript payload.\r\n");
            out += F("-> Type 'END' on a new line to SAVE and close the editor." C_RESET);
            
            printlnDual(out);
        } else printlnDual(String(C_RED) + "[-] Usage: write <filename>" + C_RESET);
    }
    else if (command.startsWith("rm ")) {
        String fn = getParam(command, 3);
        if (!fn.startsWith("/")) fn = "/" + fn;
        if (LittleFS.remove(fn)) {
            if (armedScript == fn) {
                armedScript = "";
                saveConfiguration();
            }
            refreshFSCache();
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] File deleted." + C_RESET);
        } else printlnDual(String(C_RED) + "[-] Delete failed. (Does the file exist?)" + C_RESET);
    } 
    else if (command.startsWith("run ")) {
        if (Keyboard.isConnected()) {
            String args = command.substring(4);
            args.trim();
            
            int start = 0;
            int spaceIdx;
            int scriptCount = 0;
            
            do {
                spaceIdx = args.indexOf(' ', start);
                String file = (spaceIdx == -1) ? args.substring(start) : args.substring(start, spaceIdx);
                file.trim();
                
                if (file.length() > 0) {
                    scriptCount++;
                    if (scriptCount > 1) {
                        printlnDual(String(C_CYAN) + "--- Chaining Next Script ---" + C_RESET);
                        delay(500); 
                    }
                    BuckyParser::runScript(file); 
                }
                start = spaceIdx + 1;
            } while (spaceIdx != -1);
            
            if (scriptCount > 1) {
                printlnDual(String(C_GREEN) + "[+] Chain completed." + C_RESET);
            }
        } else printlnDual(String(C_RED) + "[-] Error: BLE target not connected." + C_RESET);
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
        } else printlnDual(String(C_RED) + "[-] BLE disconnected or unknown command." + C_RESET);
    }
    if (!readingScript) printPrompt();
}

// =========================================================
// HELPER: BRACKETED LIVE MODE INTERCEPTOR
// =========================================================

bool handleLiveModeInput(char c) {
    if (!liveMode) return false;

    static char tokenBuf[BuckyConfig::MAX_LIVE_CMD_TOKEN];
    static uint8_t tokenLen = 0;
    static bool inBracket = false;

    // UTF-8 Euro symbol parser
    static int utf8State = 0;
    if (c == (char)0xE2) { utf8State = 1; return true; } 
    else if (utf8State == 1 && c == (char)0x82) { utf8State = 2; return true; } 
    else if (utf8State == 2 && c == (char)0xAC) { 
        utf8State = 0; 
        BuckyParser::typeChar((char)0x80); 
        return true; 
    }
    else { utf8State = 0; }

    if (inBracket) {
        if (c == '}') {
            tokenBuf[tokenLen] = '\0';
            String token = String(tokenBuf);

            // Literal escapes
            if (token == "#") BuckyParser::typeChar('#'); 
            else if (token == "~") BuckyParser::typeChar('~'); 
            else {
                // Route to Ducky parser (e.g., {ctrl+c} -> ctrl c)
                token.replace('+', ' '); 
                BuckyParser::executeCommand(token); 
            }
            inBracket = false;
            tokenLen = 0;
        } 
        else if (tokenLen < BuckyConfig::MAX_LIVE_CMD_TOKEN - 1) {
            tokenBuf[tokenLen++] = c;
        } 
        else {
            inBracket = false; 
            tokenLen = 0;
        }
        return true;
    }

    if (c == '{') {
        inBracket = true;
        tokenLen = 0;
        return true;
    }

    // --- SYSTEM HOTKEYS ---
    if (c == '~') { 
        liveMode = false;
        printDashboard();
        printPrompt();
        return true;
    } 
    else if (c == '#') { 
        liveMouseMode = !liveMouseMode;
        if (liveMouseMode) {
            printlnDual(String("\r\n") + C_CYAN + "[ MOUSE MODE ] WASD=Move, Q=LClick, E=RClick, C=Mid, R/F=Scroll" + C_RESET);
        } else {
            printlnDual(String("\r\n") + C_CYAN + "[ KEYBOARD MODE ]" + C_RESET);
        }
        return true;
    }

    // --- MOUSE MODE ---
    if (liveMouseMode) {
        int stepF = BuckyConfig::MOUSE_STEP_FORWARD;
        int stepB = BuckyConfig::MOUSE_STEP_BACK; 
        if (c == 'w' || c == 'W') Mouse.move(0, stepB, 0); 
        else if (c == 's' || c == 'S') Mouse.move(0, stepF, 0); 
        else if (c == 'a' || c == 'A') Mouse.move(stepB, 0, 0); 
        else if (c == 'd' || c == 'D') Mouse.move(stepF, 0, 0); 
        else if (c == 'q' || c == 'Q') Mouse.click(MOUSE_LEFT);
        else if (c == 'e' || c == 'E') Mouse.click(MOUSE_RIGHT);
        else if (c == 'c' || c == 'C') Mouse.click(MOUSE_MIDDLE);
        else if (c == 'r' || c == 'R') Mouse.move(0, 0, 1);  
        else if (c == 'f' || c == 'F') Mouse.move(0, 0, -1); 
        return true;
    } 
    
    // --- DIRECT TYPING ---
    if (c == '\r') { /* ignore */ }
    else if (c == '\n') Keyboard.write(KEY_RETURN);
    else if (c == '\b' || c == 127) Keyboard.write(KEY_BACKSPACE);
    else {
        BuckyParser::typeChar(c);
    }
    return true; 
}

// =========================================================
// SETUP & MAIN LOOP
// =========================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Buffer pre-allocation to prevent heap fragmentation
    telnetCommandBuffer.reserve(BuckyConfig::MAX_COMMAND_LENGTH + 10);
    serialCommandBuffer.reserve(BuckyConfig::MAX_COMMAND_LENGTH + 10);
    
    LittleFS.begin(true);
    loadConfiguration(); 
    pinMode(BuckyConfig::BTN_PIN, INPUT_PULLUP);
    
    // Profile-based MAC Spoofing
    uint8_t custom_mac[6];
    esp_read_mac(custom_mac, ESP_MAC_WIFI_STA); 
    custom_mac[5] += currentProfile; 
    esp_base_mac_addr_set(custom_mac);

    BLEDevice::init(profileNames[currentProfile - 1].c_str());
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);

    Keyboard.begin();
    Mouse.begin();

    WiFi.mode(WIFI_AP);
    WiFi.softAP(BuckyConfig::WIFI_SSID, BuckyConfig::WIFI_PASSWORD, BuckyConfig::WIFI_CHANNEL, BuckyConfig::WIFI_HIDDEN_SSID); 
    
    telnetServer.begin();
    telnetServer.setNoDelay(true);
    delay(50);
    
    printDashboard();
    printPrompt();
}

void loop() {
    // 1. INCOMING CONNECTION MANAGEMENT
    if (telnetServer.hasClient()) {
        if (!client || !client.connected()) {
            if (client) client.stop();
            client = telnetServer.available();
            
            client.setNoDelay(true); // Disable Nagle's Algorithm for instant TCP transmission
            delay(50); // Allow TCP Window Negotiation to settle
            
            telnetCommandBuffer = ""; 
            lastTelnetActivity = millis();
            printDashboard(); 
            printPrompt();
        } else {
            telnetServer.available().stop(); 
        }
    }

    // Anti-leak socket purger
    if (client && !client.connected()) {
        client.stop(); 
        telnetCommandBuffer = "";
    }

    // 2. TELNET CLIENT DATA STREAM
    if (client && client.connected()) {
        if (millis() - lastTelnetActivity > BuckyConfig::TELNET_IDLE_TIMEOUT_MS) {
            printlnDual(String("\r\n") + C_RED + "[-] Idle Timeout. Disconnecting." + C_RESET);
            client.stop();
            telnetCommandBuffer = "";
        }

        while (client.available()) {
            lastTelnetActivity = millis(); 
            char c = client.read();
            if (handleLiveModeInput(c)) continue; 

            if (c == '\r') continue; 

            if (c == '\n') {
                processCommand(telnetCommandBuffer);
                telnetCommandBuffer = "";            
                if (readingScript) printDual("\r\n");
            }
            else if (c == '\b' || c == 127) {
                if (telnetCommandBuffer.length() > 0) {
                    telnetCommandBuffer.remove(telnetCommandBuffer.length() - 1);
                    client.print("\033[2K\r");
                    if (!readingScript) client.print("\r\033[96mroot@bucky:~# \033[0m");
                    client.print(telnetCommandBuffer);
                    Serial.print("\b \b"); 
                }
            }
            else if (c >= 32 && c <= 126) {
                if (telnetCommandBuffer.length() < BuckyConfig::MAX_COMMAND_LENGTH) {
                    telnetCommandBuffer += c;
                    Serial.print(c); 
                }
            }
            delay(1);
        }
    }

    // 3. SERIAL USB DATA STREAM
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (handleLiveModeInput(c)) continue;

        if (c == '\r') continue; 

        if (c == '\n') {
            processCommand(serialCommandBuffer); 
            serialCommandBuffer = "";            
            if (readingScript) printDual("\r\n"); 
        }
        else if (c == '\b' || c == 0x7F) {
            if (serialCommandBuffer.length() > 0) {
                serialCommandBuffer.remove(serialCommandBuffer.length() - 1);
                Serial.print("\b \b");
                if (client && client.connected()) {
                    client.print("\033[2K\r");
                    if (!readingScript) client.print("\r\033[96mroot@bucky:~# \033[0m");
                    client.print(serialCommandBuffer);
                }
            }
        }
        else if (c >= 32 && c <= 126) {
            if (serialCommandBuffer.length() < BuckyConfig::MAX_COMMAND_LENGTH) {
                serialCommandBuffer += c;
                Serial.print(c); 
                if (client && client.connected()) client.print(c); 
            }
        }
        delay(1);
    }

    // 4. PHYSICAL HARDWARE TRIGGER
    static bool lastHigh = true;
    bool high = (digitalRead(BuckyConfig::BTN_PIN) != BuckyConfig::BTN_ACTIVE_LEVEL);
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

    delay(10); 
}