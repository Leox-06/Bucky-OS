#include "BuckyParser.h"
#include "config.h"

int BuckyParser::defaultDelay = 0;
String BuckyParser::currentLayout = "US";

static uint8_t getKeycode(const String& key) {
    String keyName = key;
    keyName.toUpperCase();

    if (keyName == "ENTER" || keyName == "RETURN") return KEY_RETURN;
    if (keyName == "SPACE") return ' ';
    if (keyName == "TAB") return KEY_TAB;
    if (keyName == "ESC" || keyName == "ESCAPE") return KEY_ESC;
    if (keyName == "BACKSPACE") return KEY_BACKSPACE;
    if (keyName == "DELETE") return KEY_DELETE;
    if (keyName == "INSERT") return KEY_INSERT;
    if (keyName == "CAPSLOCK") return KEY_CAPS_LOCK;

    if (keyName == "UP" || keyName == "UPARROW") return KEY_UP_ARROW;
    if (keyName == "DOWN" || keyName == "DOWNARROW") return KEY_DOWN_ARROW;
    if (keyName == "LEFT" || keyName == "LEFTARROW") return KEY_LEFT_ARROW;
    if (keyName == "RIGHT" || keyName == "RIGHTARROW") return KEY_RIGHT_ARROW;
    if (keyName == "PAGEUP") return KEY_PAGE_UP;
    if (keyName == "PAGEDOWN") return KEY_PAGE_DOWN;
    if (keyName == "HOME") return KEY_HOME;
    if (keyName == "END") return KEY_END;

    if (keyName == "CTRL" || keyName == "CONTROL") return KEY_LEFT_CTRL;
    if (keyName == "SHIFT") return KEY_LEFT_SHIFT;
    if (keyName == "ALT") return KEY_LEFT_ALT;
    if (keyName == "GUI" || keyName == "WINDOWS" || keyName == "WIN") return KEY_LEFT_GUI;

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

    if (keyName.length() == 1) {
        return key.charAt(0); 
    }
    return 0;
}

void BuckyParser::typeChar(char c) {
    if (currentLayout == "US") {
        Keyboard.write(c);
        return;
    }

    bool handled = true;

    if (c == '@') { Keyboard.press(KEY_RIGHT_ALT); Keyboard.press(';'); }      
    else if (c == '#') { Keyboard.press(KEY_RIGHT_ALT); Keyboard.press('\''); } 
    else if (c == '[') { Keyboard.press(KEY_RIGHT_ALT); Keyboard.press('['); }  
    else if (c == ']') { Keyboard.press(KEY_RIGHT_ALT); Keyboard.press(']'); }  
    else if (c == '{') { Keyboard.press(KEY_RIGHT_ALT); Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press('['); } 
    else if (c == '}') { Keyboard.press(KEY_RIGHT_ALT); Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press(']'); } 
    else if (c == (char)0x80) { Keyboard.press(KEY_RIGHT_ALT); Keyboard.press('e'); }
    
    else if (c == '\\') { Keyboard.press('`'); }
    else if (c == '+') { Keyboard.press(']'); }                                 
    else if (c == '*') { Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press(']'); } 
    else if (c == '"') { Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press('2'); } 
    else if (c == '<') { Keyboard.press(236); }                                 
    else if (c == '>') { Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press(236); } 
    else if (c == '\'') { Keyboard.press('-'); }                                
    else if (c == '-') { Keyboard.press('/'); }                                 
    
    else if (c == '&') { Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press('6'); } 
    else if (c == '/') { Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press('7'); } 
    else if (c == '(') { Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press('8'); } 
    else if (c == ')') { Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press('9'); } 
    else if (c == '=') { Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press('0'); } 
    else if (c == '?') { Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press('-'); } 
    else if (c == '^') { Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press('='); } 
    else if (c == '_') { Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press('/'); } 
    else if (c == ':') { Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press('.'); } 
    else if (c == ';') { Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press(','); } 
    else if (c == '|') { Keyboard.press(KEY_LEFT_SHIFT); Keyboard.press('`'); } 
    
    else { handled = false; }

    if (handled) {
        delay(15);
        Keyboard.releaseAll(); 
    } else {
        Keyboard.write(c);
    }
}

void BuckyParser::executeCommand(const String& rawCommand) {
    String command = rawCommand;
    command.trim();
    
    if (command.length() == 0 || command.startsWith("//")) return;

    int firstSpace = command.indexOf(' ');
    String cmdWord = (firstSpace == -1) ? command : command.substring(0, firstSpace);
    cmdWord.toUpperCase();

    if (cmdWord == "REM") return;

    String payload = (firstSpace != -1) ? command.substring(firstSpace + 1) : "";

    if (cmdWord == "DEFAULTDELAY" || cmdWord == "DEFAULT_DELAY") {
        defaultDelay = payload.toInt();
        return;
    }

    if (cmdWord == "STRING") {
        payload.replace("€", "\x80"); 
        for (int i = 0; i < payload.length(); i++) {
            BuckyParser::typeChar(payload.charAt(i));
            delay(15); 
        }
    }
    else if (cmdWord == "STRINGLN") {
        payload.replace("€", "\x80"); 
        for (int i = 0; i < payload.length(); i++) {
            BuckyParser::typeChar(payload.charAt(i));
            delay(15);
        }
        Keyboard.write(KEY_RETURN);
    }
    else if (cmdWord == "DELAY") {
        delay(payload.toInt());
    }
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
        Mouse.move(0, 0, scrollVal); 
    }
    else {
        int start = 0;
        int spaceIdx;
        bool pressedSomething = false;
        
        do {
            spaceIdx = command.indexOf(' ', start);
            String token = (spaceIdx == -1) ? command.substring(start) : command.substring(start, spaceIdx);
            token.trim();
            
            if (token.length() > 0) {
                uint8_t keycode = (token.length() == 1) ? token.charAt(0) : getKeycode(token);
                if (keycode != 0) {
                    Keyboard.press(keycode);
                    pressedSomething = true;
                }
            }
            start = spaceIdx + 1;
        } while (spaceIdx != -1);
        
        if (pressedSomething) {
            delay(50);
            Keyboard.releaseAll();
        } else {
            printDual(String(C_RED) + "[-] Unknown Syntax: " + command + C_RESET + "\r\n");
        }
    }
    
    if (defaultDelay > 0) delay(defaultDelay);
}

void BuckyParser::runScript(const String& filename) {
    String safeFilename = filename;
    if (!safeFilename.startsWith("/")) safeFilename = "/" + safeFilename;
    
    if (!LittleFS.exists(safeFilename)) {
        printDual(String(C_RED) + "[-] Error: File not found -> " + safeFilename + C_RESET + "\r\n");
        return;
    }
    
    File f = LittleFS.open(safeFilename, "r");
    printDual(String(C_YELLOW) + "[*] Executing script: " + safeFilename + C_RESET + "\r\n");
    
    defaultDelay = 0; 
    
    char lineBuf[BuckyConfig::FILE_READ_BUFFER_SIZE];
    int idx = 0;
    
    while (f.available()) {
        char c = f.read();
        if (c == '\n' || idx >= sizeof(lineBuf) - 1) {
            lineBuf[idx] = '\0';
            String line = String(lineBuf);
            line.trim();
            if (line.length() > 0) {
                printDual(String(C_GRAY) + "> " + line + C_RESET + "\r\n");
                executeCommand(line);
            }
            idx = 0;
        } else if (c != '\r') {
            lineBuf[idx++] = c;
        }
    }
    
    if (idx > 0) {
        lineBuf[idx] = '\0';
        String line = String(lineBuf);
        line.trim();
        if (line.length() > 0) {
            printDual(String(C_GRAY) + "> " + line + C_RESET + "\r\n");
            executeCommand(line);
        }
    }
    f.close();
    Keyboard.releaseAll(); 
    printDual(String(C_GREEN) + "[+] Execution completed." + C_RESET + "\r\n");
}