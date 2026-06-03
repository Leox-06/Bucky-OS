#include "BuckyParser.h"

int BuckyParser::defaultDelay = 0;

// =========================================================
// KEY DICTIONARY (Universal Mapping)
// =========================================================

static uint8_t getKeycode(const String& key) {
    String keyName = key;
    keyName.toUpperCase(); // Ensure case-insensitive matching

    // System and Control Keys
    if (keyName == "ENTER" || keyName == "RETURN") return KEY_RETURN;
    if (keyName == "SPACE") return ' ';
    if (keyName == "TAB") return KEY_TAB;
    if (keyName == "ESC" || keyName == "ESCAPE") return KEY_ESC;
    if (keyName == "BACKSPACE") return KEY_BACKSPACE;
    if (keyName == "DELETE") return KEY_DELETE;
    if (keyName == "INSERT") return KEY_INSERT;
    if (keyName == "CAPSLOCK") return KEY_CAPS_LOCK;
    // if (keyName == "PRINTSCREEN") return KEY_PRTSC;

    // Directional Arrows and Navigation
    if (keyName == "UP" || keyName == "UPARROW") return KEY_UP_ARROW;
    if (keyName == "DOWN" || keyName == "DOWNARROW") return KEY_DOWN_ARROW;
    if (keyName == "LEFT" || keyName == "LEFTARROW") return KEY_LEFT_ARROW;
    if (keyName == "RIGHT" || keyName == "RIGHTARROW") return KEY_RIGHT_ARROW;
    if (keyName == "PAGEUP") return KEY_PAGE_UP;
    if (keyName == "PAGEDOWN") return KEY_PAGE_DOWN;
    if (keyName == "HOME") return KEY_HOME;
    if (keyName == "END") return KEY_END;

    // Modifiers (CTRL, ALT, SHIFT, GUI/WIN)
    if (keyName == "CTRL" || keyName == "CONTROL") return KEY_LEFT_CTRL;
    if (keyName == "SHIFT") return KEY_LEFT_SHIFT;
    if (keyName == "ALT") return KEY_LEFT_ALT;
    if (keyName == "GUI" || keyName == "WINDOWS" || keyName == "WIN") return KEY_LEFT_GUI;

    // Function Keys (F1 - F12)
    if (keyName == "F1") return KEY_F1;
    if (keyName == "F2") return KEY_F2;
    if (keyName == "F3") return KEY_F3;
    if (keyName == "F4") return KEY_F4;
    if (keyName == "F5") return KEY_F5;
    if (keyName == "F6") return KEY_F6;
    if (keyName == "F7") return KEY_F7;
    if (keyName == "F8") return KEY_F8;
    if (keyName == "F9") return KEY_F9;
    if (keyName == "F10") return KEY_F10;
    if (keyName == "F11") return KEY_F11;
    if (keyName == "F12") return KEY_F12;

    // Handle single characters (e.g., "r" in "GUI r", or "c" in "CTRL c")
    if (keyName.length() == 1) {
        // Return original character to preserve case sensitivity for single strokes
        return key.charAt(0); 
    }

    return 0; // Unrecognized key
}

// =========================================================
// EXECUTION ENGINE (Case-Insensitive)
// =========================================================

void BuckyParser::executeCommand(const String& rawCommand) {
    String command = rawCommand;
    command.trim();
    
    // Ignore empty lines and comments
    if (command.length() == 0 || command.startsWith("//")) return;

    // Extract the primary command word and force it to uppercase
    int firstSpace = command.indexOf(' ');
    String cmdWord = (firstSpace == -1) ? command : command.substring(0, firstSpace);
    cmdWord.toUpperCase();

    // Ignore REM comments
    if (cmdWord == "REM") return;

    // Safely extract the payload (everything after the first space)
    String payload = (firstSpace != -1) ? command.substring(firstSpace + 1) : "";

    // 1. Handle DEFAULTDELAY
    if (cmdWord == "DEFAULTDELAY" || cmdWord == "DEFAULT_DELAY") {
        defaultDelay = payload.toInt();
        return;
    }

    // 2. Special Commands: STRING
    if (cmdWord == "STRING") {
        // Payload maintains original case sensitivity
        for (int i = 0; i < payload.length(); i++) {
            Keyboard.write(payload.charAt(i));
            delay(15); 
        }
    }
    // 3. Special Commands: STRINGLN
    else if (cmdWord == "STRINGLN") {
        for (int i = 0; i < payload.length(); i++) {
            Keyboard.write(payload.charAt(i));
            delay(15);
        }
        Keyboard.write(KEY_RETURN);
    }
    // 4. Special Commands: DELAY
    else if (cmdWord == "DELAY") {
        delay(payload.toInt());
    }
    // 5. Special Commands: MOUSE CONTROLS
    else if (cmdWord == "MOUSE_MOVE") {
        int spaceIdx = payload.indexOf(' ');
        if (spaceIdx != -1) {
            int dx = payload.substring(0, spaceIdx).toInt();
            int dy = payload.substring(spaceIdx + 1).toInt();
            Mouse.move(dx, dy, 0);
        }
    } 
    else if (cmdWord == "MOUSE_CLICK") {
        String button = payload;
        button.toUpperCase();
        button.trim();
        if (button == "RIGHT") Mouse.click(MOUSE_RIGHT);
        else if (button == "MIDDLE") Mouse.click(MOUSE_MIDDLE);
        else Mouse.click(MOUSE_LEFT);
    }
    else if (cmdWord == "MOUSE_SCROLL") {
        int scrollVal = payload.toInt();
        // 3rd parameter handles the scroll wheel (positive = up, negative = down)
        Mouse.move(0, 0, scrollVal); 
    }
    // 6. COMBINATION ENGINE (Any other input: e.g., "CTRL SHIFT ESC", "GUI r", "UP")
    else {
        int start = 0;
        int spaceIdx;
        bool pressedSomething = false;
        
        // Split the command by spaces and press all valid keys simultaneously
        do {
            spaceIdx = command.indexOf(' ', start);
            String token = (spaceIdx == -1) ? command.substring(start) : command.substring(start, spaceIdx);
            token.trim();
            
            if (token.length() > 0) {
                // Single tokens (e.g. 'c') pass directly; getKeycode handles uppercase conversion internally
                uint8_t keycode = (token.length() == 1) ? token.charAt(0) : getKeycode(token);
                
                if (keycode != 0) {
                    Keyboard.press(keycode);
                    pressedSomething = true;
                }
            }
            start = spaceIdx + 1;
        } while (spaceIdx != -1);
        
        // Release keys after a short delay to ensure OS registration
        if (pressedSomething) {
            delay(50);
            Keyboard.releaseAll();
        } else {
            // Triggered if the user inputs an unknown syntax/command
            printlnDual(String(C_RED) + "[-] Unknown Syntax: " + command + String(C_RESET));
        }
    }
    
    // Apply global delay if configured
    if (defaultDelay > 0) delay(defaultDelay);
}

// =========================================================
// SCRIPT RUNNER
// =========================================================
void BuckyParser::runScript(const String& filename) {
    String safeFilename = filename;
    if (!safeFilename.startsWith("/")) safeFilename = "/" + safeFilename;
    
    if (!LittleFS.exists(safeFilename)) {
        printlnDual(String(C_RED) + "[-] Error: File not found -> " + safeFilename + String(C_RESET));
        return;
    }
    
    File f = LittleFS.open(safeFilename, "r");
    printlnDual(String(C_YELLOW) + "[*] Executing script: " + safeFilename + String(C_RESET));
    
    defaultDelay = 0; // Reset delay for safety on new script execution
    
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            printlnDual(String(C_GRAY) + "> " + line + String(C_RESET));
            executeCommand(line);
        }
    }
    f.close();
    
    // Final safety release to prevent stuck keys after execution
    Keyboard.releaseAll(); 
    printlnDual(String(C_GREEN) + "[+] Execution completed." + String(C_RESET));
}