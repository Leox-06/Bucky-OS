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
String profileNames[3] = {"Bucky_1", "Bucky_2", "Bucky_3"};

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
    printDual(String("\r") + C_CYAN + "root@bucky:~# " + C_RESET);
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
                
                int eqIdx = line.indexOf('=');
                if (eqIdx == -1) continue;
                
                String key = line.substring(0, eqIdx);
                String val = line.substring(eqIdx + 1);
                
                if (key == "LAYOUT") {
                    if (val == "IT" || val == "US") BuckyParser::currentLayout = val;
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
// UI & DASHBOARD RENDERING (ANTI-LEAK VERSION)
// =========================================================

void printTree(String path, String prefix) {
    File dir = LittleFS.open(path);
    if (!dir || !dir.isDirectory()) { if(dir) dir.close(); return; }

    File file = dir.openNextFile();
    while (file) {
        String fname = String(file.name());
        int lastSlash = fname.lastIndexOf('/');
        String shortName = (lastSlash >= 0) ? fname.substring(lastSlash + 1) : fname;
        if (isSystemFile(shortName)) {
            file.close(); // Chiudi prima di sovrascrivere
            file = dir.openNextFile();
        } else {
            break;
        }
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
            temp.close(); // Chiudi i file di sistema nascosti
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
            file.close(); // Chiudi la cartella corrente prima di entrare nella ricorsione!
            printTree(fullPath, newPrefix); 
        } else {
            line += shortName + C_GRAY + " (" + String(file.size()) + "b)" + C_RESET;
            if (fullPath == active || String("/") + shortName == active) {
                line += String(C_RED) + "  <-- (ACTIVE)" + String(C_RESET);
            }
            printlnDual("  " + line);
            file.close(); // Chiudi il file appena stampato!
        }
        
        file = nextValidFile; 
        delay(1);
    }
    dir.close(); // Chiudi la directory principale del livello
}

String getStorageProgressBar() {
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    
    if (total == 0) return "[Storage Error]";

    int percentage = (used * 100) / total;
    int filledBlocks = percentage / 10; 

    String bar = "[";
    for (int i = 0; i < BuckyConfig::STORAGE_BAR_WIDTH; i++) {
        bar += (i < filledBlocks) ? "█" : "░";
    }
    bar += "] " + String(percentage) + "% (" + String(used / 1024) + "KB / " + String(total / 1024) + "KB)";
    return bar;
}

void printDashboard() {
    String out = "";
    out.reserve(2048); // Riserva lo spazio per la grafica ASCII
    
    out += C_CLEAR; 
    out += String(C_YELLOW) + "      ,~~.\r\n";
    out += "     (  9 )-_,\r\n";
    out += "(\\___ )=='-'\r\n";
    out += " \\ .   ) )" + String(C_CYAN) + "    Bucky OS - Payload Injector\r\n";
    out += String(C_YELLOW) + "  \\ `-' /" + String(C_RESET) + "    ID: " + String(C_GREEN) + "[" + String(currentProfile) + "] " + profileNames[currentProfile-1] + String(C_RESET) + "\r\n";
    out += String(C_YELLOW) + "   `~j-'" + String(C_RESET) + "      BLE Target : " + String(Keyboard.isConnected() ? String(C_GREEN)+"[CONNECTED]" : String(C_RED)+"[WAITING]") + "\r\n";
    
    String layoutInfo = (BuckyParser::currentLayout == "IT") ? "IT (Windows, iOS/iPadOS)" : "US (Android, macOS)";
    out += String(C_YELLOW) + "    \"=:" + String(C_RESET) + "        Layout     : " + String(C_CYAN) + layoutInfo + String(C_RESET) + "\r\n";
    out += "                Storage    : " + String(C_CYAN) + getStorageProgressBar() + String(C_RESET) + "\r\n"; 
    
    out += String(C_CYAN) + "[ ROOT FILE SYSTEM ]\r\n" + C_RESET;
    out += String(C_BLUE) + "  /\r\n" + C_RESET;
    
    printDual(out); // Spara la prima parte della UI
    
    // La stampa dell'albero (printTree) rimane ricorsiva, ma avendo liberato il pool
    // adesso verrà elaborata senza mandare in blocco lwIP.
    bool hasFiles = false;
    File root = LittleFS.open("/");
    if (root) {
        File f = root.openNextFile();
        while(f) {
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

    if (!hasFiles) printlnDual(String(C_GRAY) + "  └── [ Empty ]" + C_RESET);
    else {
        printTree("/", "");
        printlnDual(String(C_GRAY) + "------------------------------------------------" + C_RESET);
    }
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
        out.reserve(1024); // Pre-alloca la memoria per massima velocità
        
        out += String(C_CYAN) + "[ TARGET & BLUETOOTH ]\r\n" + C_RESET;
        out += String(C_GRAY) + "  target <1-3>  : Switch BT identity to attack a different PC\r\n";
        out += "  rename <name> : Change Bluetooth name of current identity\r\n";
        out += "  scan          : Scan for nearby BLE targets (5s)\r\n";
        out += "  run <files>   : Run one or multiple scripts chained via BLE\r\n";
        out += "  live          : Enter Live Control Mode (Remote Keyboard/Mouse)\r\n";
        out += String("  layout <it/us>: Switch character mapping for target OS layout\r\n\r\n") + C_RESET;
        
        out += String(C_CYAN) + "[ FOLDERS & FILES ]\r\n" + C_RESET;
        out += String(C_GRAY) + "  mkdir <dir>   : Create a new folder\r\n";
        out += "  rmdir <dir>   : Delete an empty folder\r\n";
        out += "  cat <file>    : Read script content\r\n";
        out += "  write <file>  : Create/overwrite a script (Opens editor)\r\n";
        out += "  rm <file>     : Delete a script\r\n";
        out += String("  set <file>    : Arm a script for the physical BOOT button\r\n\r\n") + C_RESET;

        out += String(C_CYAN) + "[ SYSTEM ]\r\n" + C_RESET;
        out += String(C_GRAY) + "  reboot        : Restart the Bucky OS device" + C_RESET;
        
        printlnDual(out); // Spedisce l'intera guida in un unico pacchetto di rete
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
        } else printlnDual(String(C_RED) + "[-] Invalid profile. Please choose 1, 2, or 3." + C_RESET);
    }
    else if (command.startsWith("rename ")) {
        String newName = getParam(command, 7);
        if (newName.length() > 0 && newName.length() <= 30) {
            profileNames[currentProfile - 1] = newName;
            saveConfiguration(); 
            printlnDual(String(C_GREEN) + "[+] Identity updated. Rebooting..." + C_RESET);
            delay(1000);
            ESP.restart(); 
        } else printlnDual(String(C_RED) + "[-] Invalid name." + C_RESET);
    }
    else if (command == "live") {
        if (!Keyboard.isConnected()) {
            printlnDual(String(C_RED) + "[-] Error: BLE target not connected." + C_RESET);
            return;
        }
        liveMode = true;
        liveMouseMode = false;
        printDual(C_CLEAR);
        printlnDual(String(C_YELLOW) + ">>> 🔴 LIVE CONTROL MODE ACTIVE <<<" + C_RESET);
        printlnDual(String(C_CYAN) + "[ SHORTCUTS & MACROS ]" + C_RESET);
        printlnDual(String(C_GRAY) + "  Use {...} for combos: {ctrl+c}, {gui+r}, {alt+tab}" + C_RESET);
        printlnDual(String(C_GRAY) + "  Single keys: {enter}, {tab}, {up}, {down}" + C_RESET);
        printlnDual(String(C_GRAY) + "  Press '#' to toggle Mouse Mode (WASD/Clicks)" + C_RESET);
        printlnDual(String(C_GRAY) + "  Press '~' to EXIT Live Mode" + C_RESET);
        printlnDual(String(C_GRAY) + "  To type literal symbols, use: {#} and {~}" + C_RESET + "\n");
        printlnDual(String(C_GREEN) + "Ready. Type normally..." + C_RESET);
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
            printlnDual(String(C_GREEN) + "[+] Armed for physical button." + C_RESET);
        } else printlnDual(String(C_RED) + "[-] Error: Script not found." + C_RESET);
    }
    else if (command.startsWith("mkdir ")) {
        String dir = getParam(command, 6);
        if (!dir.startsWith("/")) dir = "/" + dir;
        if (LittleFS.mkdir(dir)) {
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] Folder created." + C_RESET);
        } else printlnDual(String(C_RED) + "[-] Failed to create folder." + C_RESET);
    }
    else if (command.startsWith("rmdir ")) {
        String dir = getParam(command, 6);
        if (!dir.startsWith("/")) dir = "/" + dir;
        if (LittleFS.rmdir(dir)) {
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] Folder deleted." + C_RESET);
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
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] File deleted." + C_RESET);
        } else printlnDual(String(C_RED) + "[-] Delete failed." + C_RESET);
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

            // Controllo escape letterali
            if (token == "#") BuckyParser::typeChar('#'); 
            else if (token == "~") BuckyParser::typeChar('~'); 
            else {
                // Sostituiamo il + con lo spazio ma NON convertiamo in maiuscolo
                // Così {ctrl+c} diventa "ctrl c" (evitando l'effetto Shift+C)
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

    // --- TASTI RAPIDI CLASSICI ---
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

    // --- MODALITÀ MOUSE ---
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
    
    // --- TASTIERA DIRETTA ---
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

    telnetCommandBuffer.reserve(BuckyConfig::MAX_COMMAND_LENGTH + 10);
    serialCommandBuffer.reserve(BuckyConfig::MAX_COMMAND_LENGTH + 10);
    
    LittleFS.begin(true);
    loadConfiguration(); 
    pinMode(BuckyConfig::BTN_PIN, INPUT_PULLUP);
    
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
    // 1. GESTIONE CONNESSIONI IN INGRESSO
    if (telnetServer.hasClient()) {
        if (!client || !client.connected()) {
            if (client) client.stop();
            client = telnetServer.available();
            
            // --- MODIFICA CRITICA HARDWARE ---
            client.setNoDelay(true); // Forza l'ESP32 a sparare i dati Wi-Fi all'istante senza fare buffering interno!
            
            telnetCommandBuffer = ""; 
            lastTelnetActivity = millis();
            printDashboard(); 
            printPrompt();
        } else {
            telnetServer.available().stop(); 
        }
    }

    // CRITICO ANTI-LEAK: Se il client ha chiuso la sessione o è caduta, killa subito il socket a livello hardware!
    if (client && !client.connected()) {
        client.stop(); // Libera istantaneamente il descrittore fd di rete
        telnetCommandBuffer = "";
    }

    // 2. GESTIONE FLUSSO DATI CLIENT TELNET
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

    // 3. GESTIONE SERIALE USB
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

    // 4. GESTIONE PULSANTE FISICO
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