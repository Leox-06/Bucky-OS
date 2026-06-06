# Bucky-OS v1.0.0 🦫

**Bucky-OS** is a professional, open-source firmware for ESP32 designed for Remote Administration and Payload Injection via Bluetooth Low Energy (BLE). It transforms an ESP32 into a powerful HID (Human Interface Device) capable of executing DuckyScript payloads or being controlled in real-time.

## 🚀 Quick Start

1. **Flash the firmware** using PlatformIO to your ESP32.
2. **Power up** the device. It will create a WiFi Access Point:
   - **SSID:** `bucky`
   - **Password:** `BuckyAdmin2026!`
3. **Connect** to the WiFi and open a Telnet client:
   ```bash
   telnet 192.168.4.1
   ```
4. **Pair the Target:** On the target machine (Windows, Mac, Linux, Android, iOS), search for Bluetooth devices and connect to `Bucky_1`.
5. **Execute:** Once connected, type `help` in the Telnet terminal to see available commands.

## 🛠 Features

- **Multi-Profile Identity:** Support for 3 hardware profiles with MAC Address spoofing and customizable BLE names.
- **DuckyScript Interpreter:** Run complex automated payloads from the internal flash memory.
- **Live Control Mode:** Real-time remote HID Keyboard and Mouse control through your terminal.
- **Dual Interface:** Full CLI access via both Telnet (WiFi) and Serial (USB).
- **Intelligent Cache:** High-speed file system navigation with zero-latency dashboard rendering.
- **Layout Support:** Native support for Italian (IT) and US English keyboard layouts.

## ⌨️ Command Reference

| Command | Description |
|---------|-------------|
| `help` | Show the interactive command reference and Quick Start guide. |
| `target <1-3>` | Switch between hardware profiles (Identity Spoofing). |
| `rename <name>` | Change the current BLE device name. |
| `scan` | Discover nearby BLE devices. |
| `write <file>` | Open the built-in editor to create or modify DuckyScripts. |
| `run <f1> [f2...]` | Execute one or more scripts in sequence. |
| `live` | Enter real-time Remote HID mode (Mouse & Keyboard). |
| `set <file>` | Arm a script for execution via the physical BOOT button. |
| `reboot` | Soft reset the Bucky-OS system. |

## 📝 DuckyScript Basics

Inside the `write` editor, you can use standard commands:
- `REM`: Comments (ignored).
- `STRING <text>`: Types the text.
- `STRINGLN <text>`: Types text and presses Enter.
- `DELAY <ms>`: Pause execution.
- `GUI r`, `CTRL ALT DEL`, etc.: Hardware key combinations.

## ⚖️ License & Disclaimer

Bucky-OS is provided for **educational and authorized security testing purposes only**. The authors are not responsible for any misuse or damage caused by this software. Use it responsibly.

---
*Developed with ❤️ for the security community.*
