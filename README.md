# 🦆 Bucky-OS: Wireless BLE & Wi-Fi HID Payload Injector

Bucky-OS is an advanced, text-based standalone operating framework for ESP32 microcontrollers. It turns your ESP32 into a wireless Human Interface Device (HID) emulator capable of simulating a high-accuracy Bluetooth keyboard and mouse. 

You can interactively manage your automation scripts wirelessly via a local Wi-Fi Access Point using Telnet (e.g., Termius) or directly via a USB cable.

---

## ✨ Key Features

* **Dual-Channel Access:** Control and configure the system over standard USB Serial connection or wirelessly via a Telnet terminal (Port 23).
* **BLE HID Emulation:** High-speed, high-accuracy simulation of modern Bluetooth keyboards and mice with micro-delay anti-stuck key filters.
* **On-board File System (LittleFS):** Create, read, and organize directories and script files directly on the ESP32 internal flash memory.
* **Interactive TUI Dashboard:** A responsive Text User Interface featuring a live visual directory tree, device connection status, and a real-time storage utilization progress bar.
* **Multi-Identity Spoofing:** Switch dynamically between 3 customizable Bluetooth profiles. Changing your target profile automatically alters the hardware MAC address and BLE device name to safely cycle identities.
* **Hardware Trigger:** Bind your favorite script to the physical onboard `BOOT` button (GPIO 0) for instant standalone execution.

---

## 🚀 Quick Start Guide

### 1. Connection
1. Power up your ESP32 device.
2. Scan for Wi-Fi networks on your smartphone or PC and connect to:
   * **SSID:** `DIRECT-Bucky`
3. Open your favorite terminal emulation app (like **Termius** on iOS/Android or PuTTY on PC).
4. Establish a new **Telnet** connection using:
   * **Host/IP:** `192.168.4.1`
   * **Port:** `23`
5. *Alternative:* Leave it plugged into your PC and open any Serial Monitor at `115200` baud rate.

### 2. Basic Workflow
Type `help` in the command line interface to view all options. To create an automation macro:
1. Type `write my_script.txt` to open the interactive text editor.
2. Enter your keystroke sequences line by line.
3. Type `END` to save the script to the file system.
4. Pair your target computer to the ESP32's Bluetooth identifier (`Bucky_1`).
5. Run the file by typing `run my_script.txt` or arm it to the hardware button using `set my_script.txt`.

---

## 🖥️ CLI Commands Reference

### Target & Bluetooth Controls
* `help` : Displays the interactive help menu.
* `scan` : Performs a 5-second active environmental scan for nearby BLE devices.
* `target <1-3>` : Switches the current hardware profile identity and reboots the chip to spoof a new MAC/Name combination.
* `rename <new_name>` : Customizes the Bluetooth advertising name of the active profile slot.
* `run <file>` : Instantly executes the specified script file over the active BLE link.

### File System Operations
* `mkdir <folder>` : Creates a new directory.
* `rmdir <folder>` : Removes an empty directory.
* `cat <file>` : Reads out and displays the exact contents of a file.
* `write <file>` : Opens the interactive environment editor to write or overwrite a file.
* `rm <file>` : Deletes a file from local storage.
* `set <file>` : Arms a script file to execute immediately whenever the physical `BOOT` button is pressed.

---

## 📝 Scripting Syntax Syntax (Ducky-style)

When inside the `write` editor environment, you can build automation flows using the following macro commands:

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `STRING` | `[text]` | Types the text string natively with a built-in 15ms safety key spacing. |
| `ENTER` | *None* | Presses and releases the Enter/Return key. |
| `SPACE` | *None* | Presses and releases the Spacebar. |
| `DELAY` | `[ms]` | Halts script execution for the specified milliseconds (e.g., `DELAY 500`). |
| `WIN` / `META` | `[key]` | Holds down the Windows/GUI key and strikes the paired letter. |
| `CTRL` | `[key]` | Holds down the Control key and strikes the paired letter. |
| `ALT` | `[key]` | Holds down the Alt key and strikes the paired letter. |
| `MOUSE_MOVE` | `[x] [y]` | Moves the mouse cursor relatively along axes (e.g., `MOUSE_MOVE 100 -50`). |
| `MOUSE_CLICK`| `[LEFT/RIGHT/MIDDLE]`| Simulates a structural click of the selected mouse mouse button. |

### Script Example:
```text
WIN r
DELAY 500
STRING notepad
ENTER
DELAY 1000
STRING Hello World from Bucky OS!
ENTER
