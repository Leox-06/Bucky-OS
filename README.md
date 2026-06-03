# 🦆 Bucky-OS: Wireless BLE & Wi-Fi HID Payload Injector

Bucky-OS is an advanced, text-based standalone operating framework for ESP32 microcontrollers. It turns your ESP32 into a secure wireless Human Interface Device (HID) emulator capable of simulating a high-accuracy Bluetooth keyboard and mouse with full live-streaming capabilities.

You can interactively manage your automation scripts, chain multiple payloads, or take live control over the target machine wirelessly via a password-protected Wi-Fi Access Point using Telnet (e.g., Termius) or directly via a USB cable.

---

## ✨ Key Features

* **Dual-Channel Access:** Control and configure the system over a standard USB Serial connection or wirelessly via a Telnet terminal (Port 23).
* **Secure Access:** Protected local Wi-Fi Access Point utilizing WPA2 authentication.
* **Case-Insensitive DuckyScript Interpreter:** Enhanced engine allows script execution regardless of character casing while keeping type-out payloads (`STRING`) perfectly intact.
* **Payload Chaining:** Execute single scripts or chain multiple standalone payload files in a sequential queue (`run payload1 payload2 payload3`) with automatic safe-recovery delays.
* **Live Remote Control (HID Stream):** Real-time remote input streaming. Use your terminal as a live keyboard or mouse using `WASD` or hardware Arrow keys, featuring an *Asymmetric Stepping* algorithm (+15/-14 pixels) for pixel-perfect micro-adjustments.
* **On-board File System (LittleFS):** Create, read, and organize directories and extensionless script files directly on the ESP32 internal flash memory.
* **Interactive TUI Dashboard:** A responsive Text User Interface featuring a live visual directory tree, device connection status, and a real-time storage utilization progress bar.
* **Multi-Identity Spoofing:** Switch dynamically between 3 customizable Bluetooth profiles. Changing your target profile automatically alters the hardware MAC address and BLE device name to safely cycle identities.
* **Hardware Trigger:** Bind your favorite script to the physical onboard `BOOT` button (GPIO 0) for instant standalone execution.

---

## 🚀 Quick Start Guide

### 1. Connection
1. Power up your ESP32 device.
2. Scan for Wi-Fi networks on your smartphone or PC and connect to:
   * **SSID:** `DIRECT-Bucky`
   * **Password:** `BuckyAdmin2026!`
3. Open your favorite terminal emulation app (like **Termius** on iOS/Android or PuTTY on PC).
4. Establish a new **Telnet** connection using:
   * **Host/IP:** `192.168.4.1`
   * **Port:** `23`
5. *Alternative:* Leave it plugged into your PC and open any Serial Monitor at `115200` baud rate.

### 2. Basic Workflow
Type `help` in the command line interface to view all options. To create an automation macro:
1. Type `write my_payload` to open the interactive text editor.
2. Enter your keystroke sequences line by line (Supports empty lines and any casing!).
3. Type `END` on a new line to save the script to the file system.
4. Pair your target computer to the ESP32's Bluetooth identifier (`Bucky_1`).
5. Run the file by typing `run my_payload`, chain multiple files using `run init bypass payload`, or arm it to the hardware button using `set my_payload`.
6. Take manual control over the target machine anytime by typing `live`.

---

## 🖥️ CLI Commands Reference

### Target & Bluetooth Controls
* `help` : Displays the interactive help menu.
* `scan` : Performs a 5-second active environmental scan for nearby BLE devices.
* `target <1-3>` : Switches the current hardware profile identity and reboots the chip to spoof a new MAC/Name combination.
* `rename <new_name>` : Customizes the Bluetooth advertising name of the active profile slot.
* `run <file1> [file2]...` : Executes a single script or chains multiple scripts sequentially over the active BLE link.
* `live` : Enters Live Control Mode for interactive real-time remote Keyboard/Mouse streaming.

### File System Operations
* `mkdir <folder>` : Creates a new directory.
* `rmdir <folder>` : Removes an empty directory.
* `cat <file>` : Reads out and displays the exact contents of a file.
* `write <file>` : Opens the interactive environment editor to write or overwrite a file line-by-line (supports empty spacing).
* `rm <file>` : Deletes a file from local storage.
* `set <file>` : Arms a script file to execute immediately whenever the physical `BOOT` button is pressed.

### System Controls
* `reboot` / `restart` : Performs a clean soft-reset of the Bucky-OS device.

---

## 🔴 Live Control Mode

When entering `live` mode, Bucky-OS transforms your terminal input into an instantaneous HID input pipeline. 

* **Toggle Mode:** Press `#` to switch between **Keyboard** and **Mouse** controls.
* **Exit Live Mode:** Press `~` at any time to safely exit and return to the Bucky-OS CLI prompt.

### Mouse Mode Controls
You can map terminal strokes to mouse actions natively using either `WASD` or your terminal's hardware **Arrow Keys**:
* **Movement:** `W` / `Up` (Up), `S` / `Down` (Down), `A` / `Left` (Left), `D` / `Right` (Right).
* **Precision Mechanics:** Features an *Asymmetric Stepping* configuration (+15 pixels on positive movements, -14 pixels on negative movements). Toggling directions backward and forward shifts the cursor by exactly **1 pixel**, enabling absolute target selection.
* **Actions:** * `Q` : Left Mouse Click
  * `E` : Right Mouse Click
  * `C` : Middle Mouse Click
  * `R` : Scroll Wheel UP
  * `F` : Scroll Wheel DOWN

---

## 📝 Scripting Syntax (Ducky-style Case-Insensitive)

When inside the `write` editor environment, you can build automation flows. Commands are case-insensitive (`string` or `STRING`), but typed parameters inside text commands retain their original case formatting.

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `STRING` | `[text]` | Types the text string natively with a built-in 15ms safety key spacing. |
| `STRINGLN` | `[text]` | Types the text string and automatically appends an Enter stroke. |
| `ENTER` | *None* | Presses and releases the Enter/Return key. |
| `SPACE` | *None* | Presses and releases the Spacebar. |
| `TAB` / `ESC` | *None* | Presses and releases the Tab or Escape key. |
| `DELAY` | `[ms]` | Halts script execution for the specified milliseconds (e.g., `DELAY 500`). |
| `DEFAULTDELAY` | `[ms]` | Sets a global automatic delay applied after every line execution. |
| `GUI` / `WINDOWS` | `[key]` | Holds down the GUI key and strikes the paired letter (e.g., `GUI r`). |
| `CTRL` / `ALT` / `SHIFT` | `[key]` | Holds down the specified modifier and strikes the paired letter. |
| `MOUSE_MOVE` | `[x] [y]` | Moves the mouse cursor relatively along axes (e.g., `MOUSE_MOVE 100 -50`). |
| `MOUSE_CLICK`| `[LEFT/RIGHT/MIDDLE]`| Simulates a structural click of the selected mouse button. |
| `MOUSE_SCROLL`| `[value]` | Rotates the mouse scroll wheel (positive numbers scroll up, negative down). |

### Script Example:
```text
DEFAULTDELAY 200
GUI r
STRINGLN cmd
DELAY 500
STRINGLN echo Hello from Bucky-OS!
MOUSE_MOVE 50 50
MOUSE_CLICK LEFT
MOUSE_SCROLL -2