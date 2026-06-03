#ifndef BUCKY_PARSER_H
#define BUCKY_PARSER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <BleCombo.h>

// =========================================================
// PROFESSIONAL ANSI COLORS FOR TELNET UI
// =========================================================
#define C_RED     "\033[91m"
#define C_GREEN   "\033[92m"
#define C_YELLOW  "\033[93m"
#define C_BLUE    "\033[94m"
#define C_CYAN    "\033[96m"
#define C_GRAY    "\033[90m"
#define C_RESET   "\033[0m"
#define C_CLEAR   "\033[2J\033[H" // Clears screen and moves cursor to home position


extern void printlnDual(const String& msg);

class BuckyParser {
public:
    static void runScript(const String& filename);

    static void executeCommand(const String& command);
private:
    static int defaultDelay;
};

#endif // BUCKY_PARSER_H