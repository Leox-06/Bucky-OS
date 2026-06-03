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
// FILE SYSTEM & SYSTEM STATE GLOBALS
// =========================================================
File editorFile;
bool readingScript = false;
String scriptTargetName = "";

// RAM tracking for persistent system parameters
String armedScript = ""; 
int currentProfile = 1;
String profileNames[3] = {"Bucky_1", "Bucky_2", "Bucky_3"};

// =========================================================
// CLI & INTERACTIVE GLOBALS
// =========================================================
String serialCommandBuffer = "";
String telnetCommandBuffer = "";
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
        if (msg.length() > 0) {
            size_t bytesSent = client.write((const uint8_t*)msg.c_str(), msg.length());
            if (bytesSent == 0) {
                client.stop(); // Drop stalled/overflowed zombie connections
            }
        }
    }
}

void printlnDual(const String& msg) {
    Serial.println(stripANSI(msg)); 
    if (client && client.connected()) {
        String fullMsg = msg + "\r\n"; // Telnet compliance line termination
        size_t bytesSent = client.write((const uint8_t*)fullMsg.c_str(), fullMsg.length());
        if (bytesSent == 0) {
            client.stop(); 
        }
    }
}

void printPrompt() {
    printDual(String("\r") + String(C_CYAN) + "root@bucky:~# " + String(C_RESET));
}

// =========================================================
// CENTRALIZED CONFIGURATION SYSTEM (Extensionless properties map)
// =========================================================

String getActiveScript() {
    return armedScript;
}

void saveConfiguration() {
    File f = LittleFS.open("/config", "w");
    if (f) {
        f.println("LAYOUT=" + BuckyParser::currentLayout);
        f.println("ARMED_SCRIPT=" + armedScript);
        f.println("CURRENT_PROFILE=" + String(currentProfile));
        f.println("PROFILE_1=" + profileNames[0]);
        f.println("PROFILE_2=" + profileNames[1]);
        f.println("PROFILE_3=" + profileNames[2]);
        f.close();
    }
}

void loadConfiguration() {
    // Set fallback defaults in case config doesn't exist yet
    BuckyParser::currentLayout = "US";
    armedScript = "";
    currentProfile = 1;
    profileNames[0] = "Bucky_1";
    profileNames[1] = "Bucky_2";
    profileNames[2] = "Bucky_3";

    if (LittleFS.exists("/config")) {
        File f = LittleFS.open("/config", "r");
        if (f) {
            while (f.available()) {
                String line = f.readStringUntil('\n');
                line.trim();
                
                int eqIdx = line.indexOf('=');
                if (eqIdx == -1) continue;
                
                String key = line.substring(0, eqIdx);
                String val = line.substring(eqIdx + 1);
                
                if (key == "LAYOUT") {
                    if (val == "IT" || val == "US") BuckyParser::currentLayout = val;
                }
                else if (key == "ARMED_SCRIPT") {
                    armedScript = val;
                }
                else if (key == "CURRENT_PROFILE") {
                    int p = val.toInt();
                    if (p >= 1 && p <= 3) currentProfile = p;
                }
                else if (key == "PROFILE_1") profileNames[0] = val;
                else if (key == "PROFILE_2") profileNames[1] = val;
                else if (key == "PROFILE_3") profileNames[2] = val;
            }
            f.close();
        }
    }
}

bool setActiveScript(String filename) {
    if (!filename.startsWith("/")) filename = "/" + filename;
    if (!LittleFS.exists(filename)) return false;
    armedScript = filename;
    saveConfiguration(); // Direct write to registry
    return true;
}

bool isSystemFile(String name) {
    // Clean architecture: only one registry file to hide from TUI
    return (name == "config");
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
    
    String layoutInfo = (BuckyParser::currentLayout == "IT") ? "IT (Windows, iOS/iPadOS)" : "US (Android, macOS)";
    printlnDual(String(C_YELLOW) + "    \"=:" + String(C_RESET) + "         Layout     : " + String(C_CYAN) + layoutInfo + String(C_RESET));
    printlnDual("                Storage    : " + String(C_CYAN) + getStorageProgressBar() + String(C_RESET)); 
    
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

    // --- 1. TEXT EDITOR INTERCEPTOR ---
    if (readingScript) {
        String exitCheck = command;
        exitCheck.toUpperCase();

        if (exitCheck == "END") {
            readingScript = false;
            editorFile.close();
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] File " + scriptTargetName + " saved successfully!" + C_RESET);
        } else {
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

    printDashboard();
    printlnDual(String(C_GRAY) + ">>> " + command + C_RESET + "\n");

    // --- 3. COMMAND ROUTING ---
    if (command == "help") {
        printlnDual(String(C_CYAN) + "[ TARGET & BLUETOOTH ]" + C_RESET);
        printlnDual(String(C_GRAY) + "  target <1-3>  : Switch BT identity to attack a different PC");
        printlnDual("  rename <name> : Change Bluetooth name of current identity");
        printlnDual("  scan          : Scan for nearby BLE targets (5s)");
        printlnDual("  run <files>   : Run one or multiple scripts chained via BLE");
        printlnDual("  live          : Enter Live Control Mode (Remote Keyboard/Mouse)");
        printlnDual(String("  layout <it/us>: Switch character mapping for target OS layout\n") + C_RESET);
        
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
            currentProfile = t;
            saveConfiguration(); // Saved to unified config registry
            printlnDual(String(C_GREEN) + "[+] Target Profile changed to: " + String(t) + C_RESET);
            printlnDual(String(C_YELLOW) + "[*] Rebooting ESP32... Reconnect in 2s." + C_RESET);
            delay(1000);
            ESP.restart(); 
        } else {
            printlnDual(String(C_RED) + "[-] Invalid profile. Please choose 1, 2, or 3." + C_RESET);
        }
    }
    else if (command.startsWith("rename ")) {
        String newName = getParam(command, 7);
        if (newName.length() > 0 && newName.length() <= 30) {
            profileNames[currentProfile - 1] = newName;
            saveConfiguration(); // Saved to unified config registry
            printlnDual(String(C_GREEN) + "[+] Identity " + String(currentProfile) + " renamed to: " + newName + C_RESET);
            printlnDual(String(C_YELLOW) + "[*] Rebooting ESP32 to apply BLE name..." + C_RESET);
            delay(1000);
            ESP.restart(); 
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
    else if (command.startsWith("layout ")) {
        String lay = getParam(command, 7);
        lay.toUpperCase();
        if (lay == "IT" || lay == "US") {
            BuckyParser::currentLayout = lay;
            saveConfiguration(); 
            printlnDual(String(C_GREEN) + "[+] Target layout switched and saved to: " + BuckyParser::currentLayout + C_RESET);
        } else {
            printlnDual(String(C_RED) + "[-] Invalid layout. Use 'it' or 'us'." + C_RESET);
        }
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
            if (armedScript == fn) {
                armedScript = "";
                saveConfiguration();
            }
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
            
            do {
                spaceIdx = args.indexOf(' ', start);
                String file = (spaceIdx == -1) ? args.substring(start) : args.substring(start, spaceIdx);
                file.trim();
                
                if (file.length() > 0) {
                    scriptCount++;
                    if (scriptCount > 1) {
                        printlnDual(String(C_CYAN) + "--- Chaining Next Script (" + String(scriptCount) + ") ---" + C_RESET);
                        delay(500); 
                    }
                    BuckyParser::runScript(file); 
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
// HELPER: LIVE MODE INTERCEPTOR
// =========================================================

bool handleLiveModeInput(char c) {
    if (!liveMode) return false;

    static int ansiState = 0;

    if (ansiState == 1) {
        if (c == '[') {
            ansiState = 2; 
            return true;
        } else {
            ansiState = 0; 
            if (!liveMouseMode) Keyboard.write(KEY_ESC);
        }
    }
    else if (ansiState == 2) {
        ansiState = 0; 
        int step = 15; 
        
        if (liveMouseMode) {
            if (c == 'A') Mouse.move(0, -step + 5, 0);      
            else if (c == 'B') Mouse.move(0, step, 0);  
            else if (c == 'C') Mouse.move(step, 0, 0);  
            else if (c == 'D') Mouse.move(-step + 5, 0, 0); 
        } else {
            if (c == 'A') Keyboard.write(KEY_UP_ARROW);
            else if (c == 'B') Keyboard.write(KEY_DOWN_ARROW);
            else if (c == 'C') Keyboard.write(KEY_RIGHT_ARROW);
            else if (c == 'D') Keyboard.write(KEY_LEFT_ARROW);
        }
        return true; 
    }

    if (c == 27) { 
        ansiState = 1;
        return true;
    }

    static int utf8State = 0;
    if (c == (char)0xE2) { utf8State = 1; return true; } // Byte 1 dell'Euro
    else if (utf8State == 1 && c == (char)0x82) { utf8State = 2; return true; } // Byte 2 dell'Euro
    else if (utf8State == 2 && c == (char)0xAC) { 
        utf8State = 0; 
        BuckyParser::typeChar((char)0x80); // Lancia il token Euro al dattilografo
        return true; 
    }
    else { utf8State = 0; }

    if (c == '~') { 
        liveMode = false;
        printDashboard();
        printPrompt();
    } 
    else if (c == '#') { 
        liveMouseMode = !liveMouseMode;
        if (liveMouseMode) {
            printlnDual(String(C_CYAN) + "\r\n[ MOUSE MODE ] WASD/Arrows=Move, Q=LClick, E=RClick, C=Mid, R/F=Scroll" + C_RESET);
        } else {
            printlnDual(String(C_CYAN) + "\r\n[ KEYBOARD MODE ]" + C_RESET);
        }
    } 
    else if (liveMouseMode) {
        int step = 15; 
        if (c == 'w' || c == 'W') Mouse.move(0, -step, 0);
        else if (c == 's' || c == 'S') Mouse.move(0, step, 0);
        else if (c == 'a' || c == 'A') Mouse.move(-step, 0, 0);
        else if (c == 'd' || c == 'D') Mouse.move(step, 0, 0);
        else if (c == 'q' || c == 'Q') Mouse.click(MOUSE_LEFT);
        else if (c == 'e' || c == 'E') Mouse.click(MOUSE_RIGHT);
        else if (c == 'c' || c == 'C') Mouse.click(MOUSE_MIDDLE);
        else if (c == 'r' || c == 'R') Mouse.move(0, 0, 1);  
        else if (c == 'f' || c == 'F') Mouse.move(0, 0, -1); 
    } 
    else {
        if (c == '\r') { /* ignore */ }
        else if (c == '\n') Keyboard.write(KEY_RETURN);
        else if (c == '\b' || c == 127) Keyboard.write(KEY_BACKSPACE);
        else {
            BuckyParser::typeChar(c);
        }
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
    loadConfiguration(); // Single properties loader
    pinMode(BTN_PIN, INPUT_PULLUP);
    
    // MAC Address Spoofing according to active registry profile
    uint8_t custom_mac[6];
    esp_read_mac(custom_mac, ESP_MAC_WIFI_STA); 
    custom_mac[5] += currentProfile; 
    esp_base_mac_addr_set(custom_mac);

    // Initialize BLE Server Stack using data from single configuration file
    BLEDevice::init(profileNames[currentProfile - 1].c_str());
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);

    Keyboard.begin();
    Mouse.begin();

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password, 1, 1); 
    
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
            telnetCommandBuffer = ""; 
            printDashboard(); 
            printPrompt();
        } else {
            telnetServer.available().stop(); 
        }
    }

    // Variabili di stato per "mangiare" i codici delle freccette nella CLI
    static int telnetAnsiState = 0;
    static int serialAnsiState = 0;

    // 2. TELNET MANAGEMENT (Wi-Fi)
    if (client && client.connected()) {
        while (client.available()) {
            char c = client.read();
            if (handleLiveModeInput(c)) continue; 

            // --- FILTRO FRECCETTE (Ignora ANSI Escape Sequences nella CLI) ---
            if (c == 27) { telnetAnsiState = 1; continue; }
            if (telnetAnsiState == 1 && c == '[') { telnetAnsiState = 2; continue; }
            if (telnetAnsiState == 2) {
                if ((c >= 'A' && c <= 'D') || c == 'H' || c == 'F' || c == '~') telnetAnsiState = 0; 
                continue; 
            }

            if (c == '\r') continue; 

            if (c == '\n') {
                processCommand(telnetCommandBuffer);
                telnetCommandBuffer = "";            
                if (readingScript) printDual("\r"); 
            }
            else if (c == '\b' || c == 127) {
                if (telnetCommandBuffer.length() > 0) {
                    telnetCommandBuffer.remove(telnetCommandBuffer.length() - 1);
                    
                    // Ridisegna pulito su Telnet usando ANSI
                    client.print("\033[2K\r");
                    if (!readingScript) client.print("\r\033[96mroot@bucky:~# \033[0m");
                    client.print(telnetCommandBuffer);
                    
                    // Usa il trucco "Erase" classico per aggiornare lo schermo Seriale
                    Serial.print("\b \b"); 
                }
            }
            else if (c >= 32 && c <= 126) {
                telnetCommandBuffer += c;
                Serial.print(c); 
            }
        }
    }

    // 3. SERIAL MANAGEMENT (USB Cable)
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (handleLiveModeInput(c)) continue;

        // --- FILTRO FRECCETTE (Ignora ANSI Escape Sequences nella CLI) ---
        if (c == 27) { serialAnsiState = 1; continue; }
        if (serialAnsiState == 1 && c == '[') { serialAnsiState = 2; continue; }
        if (serialAnsiState == 2) {
            if ((c >= 'A' && c <= 'D') || c == 'H' || c == 'F' || c == '~') serialAnsiState = 0; 
            continue; 
        }

        if (c == '\r') continue; 

        if (c == '\n') {
            processCommand(serialCommandBuffer); 
            serialCommandBuffer = "";            
            if (readingScript) printDual("\r\n"); 
        }
        else if (c == '\b' || c == 0x7F) {
            if (serialCommandBuffer.length() > 0) {
                serialCommandBuffer.remove(serialCommandBuffer.length() - 1);
                
                // Usa il trucco "Erase" classico per cancellare la lettera sul Serial Monitor
                Serial.print("\b \b");
                
                // Ridisegna pulito su Telnet (se connesso) per mantenerli in sincrono
                if (client && client.connected()) {
                    client.print("\033[2K\r");
                    if (!readingScript) client.print("\r\033[96mroot@bucky:~# \033[0m");
                    client.print(serialCommandBuffer);
                }
            }
        }
        else if (c >= 32 && c <= 126) {
            serialCommandBuffer += c;
            Serial.print(c); 
            if (client && client.connected()) client.print(c); 
        }
    }

    // 4. PHYSICAL BOOT BUTTON (Payload Trigger via central RAM state variable)
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

    delay(10); 
}