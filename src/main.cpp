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

#define BTN_PIN 0
#define BTN_LEVEL LOW

const char* ssid = "DIRECT-Bucky";
WiFiServer telnetServer(23);
WiFiClient client;
BLEScan* pBLEScan;

String scriptBuffer = "";
bool readingScript = false;
String scriptTargetName = "";

const char* ACTIVE_CONFIG_FILE = "/active_config.txt";
const char* TARGET_CONFIG_FILE = "/target_profile.txt";
const char* NAMES_CONFIG_FILE  = "/names_config.txt";

String serialCommandBuffer = "";
String telnetCommandBuffer = "";

int currentProfile = 1;
String profileNames[3] = {"Bucky_1", "Bucky_2", "Bucky_3"};

// ---------------------------------------------------------
// PROFESSIONAL ANSI COLORS
// ---------------------------------------------------------
#define C_RED     "\033[91m"
#define C_GREEN   "\033[92m"
#define C_YELLOW  "\033[93m"
#define C_BLUE    "\033[94m"
#define C_CYAN    "\033[96m"
#define C_GRAY    "\033[90m"
#define C_RESET   "\033[0m"
#define C_CLEAR   "\033[2J\033[H" // Clears the screen and moves the cursor to the top

// ---------------------------------------------------------
// SYNCHRONIZED PRINT AND COLOR FILTER
// ---------------------------------------------------------

String stripANSI(String input) {
    String output = "";
    bool inANSI = false;
    for (int i = 0; i < input.length(); i++) {
        char c = input.charAt(i);
        if (c == '\033') { 
            inANSI = true; // Start of color code
        } else if (inANSI) {
            // ANSI codes always end with a letter (e.g., 'm' for colors, 'J' or 'H' to clear the screen)
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                inANSI = false; // End of code, resume reading text
            }
        } else {
            output += c; // Adds only normal characters
        }
    }
    return output;
}

void printDual(const String& msg) {
    Serial.print(stripANSI(msg)); // Prints clean text to USB
    if (client && client.connected()) {
        client.print(msg);        // Sends colored text via Wi-Fi
    }
}

void printlnDual(const String& msg) {
    Serial.println(stripANSI(msg)); // Prints clean text to USB
    if (client && client.connected()) {
        client.println(msg);        // Sends colored text via Wi-Fi
    }
}

void printPrompt() {
    // The String("\r") forces the cursor to jump to the left margin before writing
    printDual(String("\r") + String(C_CYAN) + "root@bucky:~# " + String(C_RESET));
}

// ---------------------------------------------------------
// CONFIGURATION MANAGEMENT
// ---------------------------------------------------------
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

// ---------------------------------------------------------
// FILE SYSTEM TREE WITH COLORS
// ---------------------------------------------------------
bool isSystemFile(String name) {
    return (name == "active_config.txt" || name == "target_profile.txt" || name == "names_config.txt");
}

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

    // Calculate occupancy percentage
    int percentage = (used * 100) / total;
    
    // Configure graphical width of the bar (10 total blocks)
    int barWidth = 10;
    int filledBlocks = percentage / 10; // Each block represents 10%

    String bar = "[";
    for (int i = 0; i < barWidth; i++) {
        if (i < filledBlocks) {
            bar += "█"; // Filled block
        } else {
            bar += "░"; // Empty block
        }
    }
    bar += "] ";

    // Show percentage and space in KB (Kilobytes)
    bar += String(percentage) + "% (" + String(used / 1024) + "KB / " + String(total / 1024) + "KB)";
    return bar;
}

// ---------------------------------------------------------
// FIXED DASHBOARD ON TOP
// ---------------------------------------------------------
void printDashboard() {
    printDual(C_CLEAR); // Clears everything and goes to top
    
    // Duck Drawing and Info (Updated with Phantom OS and Storage Bar)
    printlnDual(String(C_YELLOW) + "      ,~~.");
    printlnDual("     (  9 )-_,");
    printlnDual("(\\___ )=='-'");
    printlnDual(" \\ .   ) )" + String(C_CYAN) + "    Bucky OS - Payload Injector");
    printlnDual(String(C_YELLOW) + "  \\ `-' /" + String(C_RESET) + "     ID: " + String(C_GREEN) + "[" + String(currentProfile) + "] " + profileNames[currentProfile-1] + String(C_RESET));
    printlnDual(String(C_YELLOW) + "   `~j-'" + String(C_RESET) + "      BLE Target : " + String(Keyboard.isConnected() ? String(C_GREEN)+"[CONNECTED]" : String(C_RED)+"[WAITING]"));
    printlnDual(String(C_YELLOW) + "    \"=:" + String(C_RESET) + "      Storage    : " + String(C_CYAN) + getStorageProgressBar() + String(C_RESET)); // <--- INTEGRATED HERE
    
    // Tree Drawing
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

String getParam(String cmd, int offset) {
    if (cmd.length() <= offset) return "";
    String p = cmd.substring(offset);
    p.trim();
    return p;
}

// ---------------------------------------------------------
// DUCKY SCRIPT AND MOUSE ENGINE (Maximum Accuracy)
// ---------------------------------------------------------
void executeSingleCommand(String command) {
    command.trim();
    if (command.length() == 0 || command.startsWith("//")) return;

    if (command == "ENTER") { 
        Keyboard.write(KEY_RETURN); 
    } 
    else if (command == "SPACE") { 
        Keyboard.write(' '); 
    } 
    else if (command.startsWith("STRING ")) { 
        String text = command.substring(7);
        // Writes letter by letter with a micro-delay to prevent stuck keys
        for (int i = 0; i < text.length(); i++) {
            Keyboard.write(text.charAt(i));
            delay(15); // <-- The antibug magic: 15ms between keys
        }
    } 
    else if (command.startsWith("DELAY ")) { 
        delay(command.substring(6).toInt()); 
    } 
    else if (command.startsWith("WIN ") || command.startsWith("META ")) {
        Keyboard.press(KEY_LEFT_GUI); 
        Keyboard.press(command.charAt(4)); 
        delay(100); 
        Keyboard.releaseAll();
    } 
    else if (command.startsWith("CTRL ")) {
        Keyboard.press(KEY_LEFT_CTRL); 
        Keyboard.press(command.charAt(5)); 
        delay(100); 
        Keyboard.releaseAll();
    } 
    else if (command.startsWith("ALT ")) {
        Keyboard.press(KEY_LEFT_ALT); 
        Keyboard.press(command.charAt(4)); 
        delay(100); 
        Keyboard.releaseAll();
    } 
    else if (command.startsWith("MOUSE_MOVE ")) {
        String coords = command.substring(11);
        int spaceIdx = coords.indexOf(' ');
        if (spaceIdx != -1) {
            int dx = coords.substring(0, spaceIdx).toInt();
            int dy = coords.substring(spaceIdx + 1).toInt();
            Mouse.move(dx, dy, 0);
        }
    } 
    else if (command.startsWith("MOUSE_CLICK")) {
        String button = command.substring(11);
        button.trim();
        
        if (button == "RIGHT") {
            Mouse.click(MOUSE_RIGHT);
        } else if (button == "MIDDLE") {
            Mouse.click(MOUSE_MIDDLE);
        } else {
            Mouse.click(MOUSE_LEFT); // If "LEFT" or empty, performs standard left click
        }
    }
}

void runScriptFile(String filename) {
    if (!filename.startsWith("/")) filename = "/" + filename;
    if (!LittleFS.exists(filename)) {
        printlnDual(String(C_RED) + "[-] Error: File not found -> " + filename + C_RESET);
        return;
    }
    File f = LittleFS.open(filename, "r");
    printlnDual(String(C_YELLOW) + "[*] Executing script: " + filename + C_RESET);
    
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            printlnDual(String(C_GRAY) + "> " + line + C_RESET);
            executeSingleCommand(line);
            delay(50); // <-- Line pause increased to 50ms to stabilize the PC buffer
        }
    }
    f.close();
    Keyboard.releaseAll(); // Final safety catch to release all keys
    printlnDual(String(C_GREEN) + "[+] Execution completed." + C_RESET);
}

// ---------------------------------------------------------
// CLI COMMAND PROCESSOR
// ---------------------------------------------------------
void processCommand(String command) {
    command.trim();
    if (command.length() == 0) { 
        printDashboard(); 
        printPrompt(); 
        return; 
    }

    // EDITOR MANAGEMENT
    if (readingScript) {
        if (command == "END") {
            readingScript = false;
            File f = LittleFS.open(scriptTargetName, "w");
            if (f) {
                f.print(scriptBuffer);
                f.close();
                printDashboard();
                printlnDual(String(C_GREEN) + "[+] File " + scriptTargetName + " saved successfully!" + C_RESET);
            } else {
                printDashboard();
                printlnDual(String(C_RED) + "[-] FS Error: Could not write file." + C_RESET);
            }
        } else {
            scriptBuffer += command + "\n";
        }
        if (!readingScript) printPrompt();
        return;
    }

    // ACTIONS: Clean and redraw the dashboard BEFORE printing output
    printDashboard();
    printlnDual(String(C_GRAY) + ">>> " + command + C_RESET + "\n");

    if (command == "help") {
        printlnDual(String(C_CYAN) + "[ TARGET & BLUETOOTH ]" + C_RESET);
        printlnDual("  target <1-3>  : Switch BT identity to attack a different PC");
        printlnDual("  rename <name> : Change Bluetooth name of current identity");
        printlnDual("  scan          : Scan for nearby BLE targets (5s)");
        printlnDual("  run <file>    : Run a script immediately via BLE\n");
        printlnDual(String(C_CYAN) + "[ FOLDERS & FILES ]" + C_RESET);
        printlnDual("  mkdir <dir>   : Create a new folder");
        printlnDual("  rmdir <dir>   : Delete an empty folder");
        printlnDual("  cat <file>    : Read script content");
        printlnDual("  write <file>  : Create/overwrite a script (Opens editor)");
        printlnDual("  rm <file>     : Delete a script");
        printlnDual("  set <file>    : Arm a script for the physical BOOT button");
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
    else if (command.startsWith("set ")) {
        String fn = getParam(command, 4);
        if (setActiveScript(fn)) {
            // Redraw dashboard to update the arrow (ACTIVE) immediately
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
        }
        else printlnDual(String(C_RED) + "[-] Failed to create folder." + C_RESET);
    }
    else if (command.startsWith("rmdir ")) {
        String dir = getParam(command, 6);
        if (!dir.startsWith("/")) dir = "/" + dir;
        if (LittleFS.rmdir(dir)) {
            printDashboard();
            printlnDual(String(C_GREEN) + "[+] Folder deleted: " + dir + C_RESET);
        }
        else printlnDual(String(C_RED) + "[-] Failed to delete folder (Must be empty)." + C_RESET);
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
            readingScript = true;
            scriptBuffer = "";
            printlnDual(String(C_CYAN) + "=== 📝 EDITOR: " + scriptTargetName + " ===" + C_RESET);
            printlnDual(String(C_GRAY) + "Commands : STRING <txt>, ENTER, SPACE, DELAY <ms>");
            printlnDual("           WIN/CTRL/ALT <key>, MOUSE_MOVE <x> <y>");
            printlnDual("           MOUSE_CLICK [RIGHT/LEFT/MIDDLE]" + String(C_RESET));
            printlnDual(String(C_YELLOW) + "-> Type script. Type 'END' to SAVE and exit." + C_RESET);
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
            runScriptFile(getParam(command, 4));
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
                executeSingleCommand(sub);
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

// ---------------------------------------------------------
// SETUP & LOOP
// ---------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1000); 
    
    LittleFS.begin(true);
    pinMode(BTN_PIN, INPUT_PULLUP);

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
    WiFi.softAP(ssid);
    
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
            telnetServer.available().stop(); 
        }
    }

    // 2. TELNET MANAGEMENT (Wi-Fi - Termius)
    if (client && client.connected()) {
        while (client.available()) {
            char c = client.read();
            
            if (c == '\r') continue; 

            // If we press ENTER (\n)
            if (c == '\n') {
                if (telnetCommandBuffer.length() > 0) {
                    processCommand(telnetCommandBuffer);
                    telnetCommandBuffer = "";
                    // FIX: If we are in the editor, force cursor to carriage return to the left margin
                    if (readingScript) {
                        printDual("\r");
                    }
                } else {
                    // Update the screen ONLY on a true empty enter
                    printDashboard();
                    printPrompt();
                }
            } 
            // If we press DELETE/BACKSPACE (Backspace = \b or 127)
            else if (c == '\b' || c == 127) {
                if (telnetCommandBuffer.length() > 0) {
                    telnetCommandBuffer.remove(telnetCommandBuffer.length() - 1);
                    
                    client.print("\033[2K\r");
                    Serial.print("\033[2K\r");
                    
                    // FIX: Print root prompt ONLY if editor is closed!
                    if (!readingScript) {
                        printPrompt();
                    }
                    
                    printDual(telnetCommandBuffer);
                }
            }
            // Filter to print only normal characters
            else if (c >= 32 && c <= 126) {
                telnetCommandBuffer += c;
                Serial.print(c); 
            }
        }
    }

    // 3. SERIAL MANAGEMENT (PC USB Cable)
    while (Serial.available() > 0) {
        char c = Serial.read();
        
        if (c == '\r') continue; 

        if (c == '\n') {
            if (serialCommandBuffer.length() > 0) {
                processCommand(serialCommandBuffer);
                serialCommandBuffer = "";
                // FIX: Also on USB serial, perform a clean newline in the editor
                if (readingScript) {
                    printDual("\r\n");
                }
            } else {
                printDashboard();
                printPrompt();
            }
        } 
        else if (c == '\b' || c == 0x7F) {
            if (serialCommandBuffer.length() > 0) {
                serialCommandBuffer.remove(serialCommandBuffer.length() - 1);
                
                client.print("\033[2K\r");
                Serial.print("\033[2K\r");
                
                // FIX: Print root prompt ONLY if editor is closed!
                if (!readingScript) {
                    printPrompt();
                }
                
                printDual(serialCommandBuffer);
            }
        }
        else if (c >= 32 && c <= 126) {
            serialCommandBuffer += c;
            Serial.print(c); 
            if (client && client.connected()) client.print(c); 
        }
    }

    // 4. PHYSICAL BOOT BUTTON
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
            runScriptFile(activeScript);
        }
        printPrompt();
    }
    lastHigh = high;

    delay(10);
}