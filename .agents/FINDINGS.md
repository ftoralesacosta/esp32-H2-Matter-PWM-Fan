# Seeed Studio XIAO ESP32-C6 Matter Fan Controller - Project Findings & Architecture

This document contains critical architectural findings, hardware mappings, and troubleshooting workflows compiled during the development and stabilization of the Matter PWM Fan Controller. 

Future AI agents starting a new Jetski session MUST read this document to understand the hardware-software boundaries of the project.

---

## 1. Hardware Overview & Pin Mapping

The project is built on the **Seeed Studio XIAO ESP32-C6** development board. 
> [!WARNING]
> **DO NOT confuse physical pin labels with the chip's internal GPIO numbers.** The Seeed Studio XIAO form factor uses a multiplexed pinout where board silkscreen numbers do NOT map 1-to-1 with ESP32-C6 GPIOs.

### Seeed Studio XIAO ESP32-C6 Pin Map

| Silkscreen Label | Function | ESP32-C6 GPIO | Role in this Project |
| :---: | :---: | :---: | :--- |
| **D0** | Analog/Digital | **GPIO0** | Unused |
| **D1** | Analog/Digital | **GPIO1** | Unused |
| **D2** | Analog/Digital | **GPIO2** | **IoT Button Input** (Active High) |
| **D3** | Digital | **GPIO21** | **Noctua Fan PWM Output (LEDC)** |
| **D4** | I2C SDA | **GPIO22** | Unused |
| **D5** | I2C SCL | **GPIO23** | Unused |
| **D6** | UART TX | **GPIO16** | Unused |
| **D7** | UART RX | **GPIO17** | Unused |
| **D8** | SPI SCK | **GPIO19** | Unused |
| **D9** | SPI MISO | **GPIO20** | Unused |
| **D10** | SPI MOSI | **GPIO18** | Unused |
| **User LED** | Onboard LED | **GPIO15** | Status Indicator |
| **RF Switch Power** | Antenna Power | **GPIO3** | Internal Antenna Power (Do not drive/use!) |
| **RF Switch Port** | Antenna Select | **GPIO14** | Internal Antenna Switch |

### PWM Fan Wiring (Noctua Standard)
* **Control Pin**: Connect the fan's PWM control wire (usually Blue) to the physical pin labeled **`3` (or `D3`)**, which corresponds to **`GPIO21`** in software.
* **LEDC Driver Details**: Configured as low-speed LEDC, 10-bit duty cycle resolution (0 to 1023), running at **25 kHz** (the Noctua PWM frequency spec).

---

## 2. Core Matter & Thread Architecture

### Thread Device Role (Minimal End Device - MTD)
To ensure absolute network stability and prevent the device from dropping offline, the device is configured as a **Minimal End Device (MED)** rather than a Router-eligible FTD (Full Thread Device):
* **Why?** An FTD device, when experiencing packet loss or missing ACKs from the Border Router, will detach and attempt to partition itself as a "Leader," isolating itself from the Apple Home/Matter fabric. An MTD (MED) device remains a child, resulting in a rock-solid, self-healing connection.
* **Configuration**: Configured in [sdkconfig.defaults.esp32c6](file:///Users/ftorales/Projects/esp32-H2-WPM-Fan/sdkconfig.defaults.esp32c6) via:
  ```ini
  CONFIG_OPENTHREAD_FTD=n
  CONFIG_OPENTHREAD_MTD=y
  ```

---

## 3. Critical Developer Workflows

### Target Selection
When building or compiling the project, you must explicitly set the active build target to `esp32c6`. 
* **Why?** ESP-IDF only merges target-specific defaults (like `sdkconfig.defaults.esp32c6`) if the target is set explicitly. If not set, the compiler defaults to `esp32` and silently skips the C6 defaults, leading to compilation failures on C6-specific features.
* **Command**:
  ```bash
  idf.py set-target esp32c6
  ```

### Stabilizing HomeKit Connection Failures
If the chip fails to connect, shows "Not Responding" in Apple Home, or prints `SRP update error: domain name or RRset is duplicated` or `unknown session` in the serial monitor:
1. **The Cause**: The Border Router (Apple TV/HomePod) has stale, cached pairing fabrics or secure sessions from a previous flash, and rejects the new registration.
2. **The Fix**: Clear the NVS flash on the chip to wipe old fabrics and force a clean pairing:
   ```bash
   idf.py erase-flash
   idf.py flash monitor
   ```
3. Remove the accessory from the Apple Home app and add it fresh using:
   * **Manual Setup Code**: `20202021`
   * **Discriminator**: `3840`

---

## 4. Thread Disconnect & "No Response" Troubleshooting

We have investigated an issue where the device disconnects shortly after pairing and permanently shows as **"No Response"** in HomeKit.

### A. The Permanent "No Response" Software Bug (FTD vs MTD)
* **The Symptom:** When a temporary radio drop occurs, the chip detaches and then transitions: `OPENTHREAD: Role detached -> leader`. It starts its own isolated network partition, permanently separating itself from the Apple TV Border Router. It will never recover until rebooted.
* **The Cause:** The firmware is compiled as a **Full Thread Device (FTD)** (e.g., `CONFIG_OPENTHREAD_FTD=y` in the H2 defaults or overridden by Matter). FTDs are allowed to become leaders.
* **The Fix:** Force the firmware to compile as a **Minimal Thread Device (MTD)**. MTDs are not allowed to become leaders; they will remain detached and continually scan, automatically reconnecting to the Apple TV as soon as the signal clears.
  ```ini
  CONFIG_OPENTHREAD_FTD=n
  CONFIG_OPENTHREAD_MTD=y
  ```

### B. Hardware & Electrical Hypotheses & Status
* **Voltage Mismatch (RULED OUT):** The PWM signal lines measure a perfect **3.3V** (at 100% duty cycle) and **~1.65V** (at 50% duty cycle), meaning there is no high-voltage back-feeding from the fan board.
* **MacBook Capacitive Interference (RULED OUT):** We previously hypothesized that the MacBook Air's aluminum chassis acted as a capacitive antenna, drawing ground loop noise through the USB cable. **The user found this extremely unlikely.** Furthermore, because the disconnect still occurred when the MacBook was completely disconnected (device running on battery), this theory has been **officially ruled out**.
* **Proximity Battery Test (COMPLETED):** 
  * *Setup:* The ESP was powered by a portable battery pack (completely floating) and placed directly next to the Apple TV (Thread Border Router). 
  * *Behavior:* The chip worked initially, but then stopped working after a few minutes, **even when the user was NOT adjusting the slider** (ruling out slider packet storms as the sole trigger).

### C. Active Hypotheses for the Battery-Proximity Failure
1. **USB Power Bank Auto-Shutdown (Highly Probable):** Most smartphone USB power banks have an automatic low-current shutoff. If the connected device draws less than 50mA–100mA, the power bank assumes the device is fully charged and cuts power after a few minutes. Since the ESP32-C6 is extremely low power (drawing ~20mA–40mA when running/sleeping), the power bank may have silently shut down, powering off the board entirely.
   * *Verification:* Check if the onboard LEDs on the Seeed Studio XIAO are still lit when the "No Response" state occurs.
2. **Conducted EMI / Ground Noise:** Although battery power breaks the AC mains ground loop, the ESP still shares a physical GND wire with the 12V wall-powered fan board. Brushless DC fan motors generate significant high-frequency electrical switching noise. This noise can travel directly along the GND and PWM wires into the ESP's ground plane, swamping the onboard ceramic antenna's RF counterpoise and blinding the receiver.
   * *Verification:* Test the ESP next to the Apple TV with the 12V fan power supply unplugged. If it stays online indefinitely when the fan is off and unpowered, it confirms motor/GND noise is the culprit.
3. **Thread Stack / Supervision Timeouts:** The OpenThread stack running on the ESP-IDF might be failing its Child Supervision poll requests or losing its parent router lease due to minor packet loss, failing to re-attach properly.

---

## 5. Git & Workspace Rules
* **Agent Configurations**: The `.agents/` folder (containing pairing rules, prompts, and this findings file) is **tracked in Git** and must be pushed to GitHub to sync rules and findings across the user's multiple flashing/development Macs.
* **Auto-Push Policy**: All code, configuration, and documentation changes must be **automatically staged, committed, and pushed** to GitHub immediately to ensure they are available for the user on their flashing machine.
