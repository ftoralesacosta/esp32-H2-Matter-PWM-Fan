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

* **Proximity Battery Test (COMPLETED):** 
  * *Setup:* The ESP was powered by a portable battery pack (completely floating) and placed directly next to the Apple TV (Thread Border Router). 
  * *Behavior:* The chip worked initially, but then stopped working after a few minutes, even when the user was NOT adjusting the slider.
  * *Findings:* The onboard LED went dark, confirming the **USB Power Bank Auto-Shutdown** occurred.
* **Proximity USB-PD Wall Brick Test (COMPLETED):**
  * *Setup:* The ESP was powered by a USB-PD wall charger block (no auto-shutdown) directly next to the Apple TV, with the 12V wall-powered fan running.
  * *Behavior:* The onboard LED turned off after a while (normal status behavior once commissioned), but the board remained **fully powered and running** (VBUS = 5V, Pin 3 = 1.7V average, fan spinning at 50% speed). However, HomeKit still went to **"No Response"** shortly after pairing.

### C. Active Hypotheses for the Proximity Failures

1. **Conducted EMI / Ground Noise (PRIMARY SUSPECT):** 
   * *The Mechanism:* Although the USB-PD wall block and battery power break the laptop ground loop, the ESP still shares a physical GND wire with the 12V wall-powered fan board. The brushless DC fan motor generates significant high-frequency electrical switching noise (EMI) on the shared GND and PWM lines. This noise travels directly into the ESP's ground plane, swamping the onboard ceramic antenna's RF counterpoise and **blinding the 2.4GHz receiver**. 
   * *Why it fits:* The chip is fully powered and running the fan at 50% (1.7V on Pin 3), but because its receiver is blinded by the motor's conducted noise, it cannot hear the Apple TV's keep-alive packets or commands, leading to a permanent "No Response" lockout.
   * *Verification Test:* Keep the ESP powered by the USB-PD wall block next to the Apple TV, but **unplug the 12V fan power supply from the wall** (so the fan is completely unpowered and silent). If the ESP/accessory remains online and responsive in HomeKit indefinitely, it **proves 100%** that conducted motor noise is swamping the radio.
2. **Thread Stack / Supervision Timeouts:** The OpenThread stack running on the ESP-IDF might be failing its Child Supervision poll requests or losing its parent router lease due to minor packet loss, failing to re-attach properly.

### D. Question: "Is it possible it connects via Bluetooth and Thread isn't working?"
* **Answer:** **No, it is not possible for HomeKit control to run over Bluetooth.** 
* **The Matter Protocol Flow:** Under the Matter specification, Bluetooth (BLE) is **only** used for the initial pairing handshake (commissioning) to send the Thread credentials to the board. Once the board successfully joins the Thread network, the BLE connection is **permanently terminated** (which we see in the logs: `Closing BLE GATT connection` and `BLE GAP connection terminated`).
* **Conclusion:** From that point on, your Apple Home app and Apple TV communicate with the fan **exclusively over Thread (IPv6/UDP)**. The fact that the fan responded to speed changes initially means **Thread was 100% working**. The subsequent "No Response" is due to the connection dropping later (blinded by noise), not because it was secretly using Bluetooth.

---

## 5. Git & Workspace Rules
* **Agent Configurations**: The `.agents/` folder (containing pairing rules, prompts, and this findings file) is **tracked in Git** and must be pushed to GitHub to sync rules and findings across the user's multiple flashing/development Macs.
* **Auto-Push Policy**: All code, configuration, and documentation changes must be **automatically staged, committed, and pushed** to GitHub immediately to ensure they are available for the user on their flashing machine.
