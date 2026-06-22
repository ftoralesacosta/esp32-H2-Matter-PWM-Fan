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

### C. Root Cause Confirmed: Conducted EMI from the Fan Motor

Through a series of systematic, clean-room experiments, we have **100% isolated and confirmed the root cause** of the Thread connection drops:

1. **The Proof (The Clean Run):** 
   * *Setup:* The ESP32-C6 was powered by a clean, battery-powered MacBook (completely floating), placed next to the Apple TV, with the **12V fan power supply completely unplugged from the wall** (drawing 0 current, generating 0 noise).
   * *Result:* **100% Stable!** The device stayed online, responsive, and perfectly connected in HomeKit for **12+ minutes** (and would stay online indefinitely). It successfully received commands to turn on, off, and change speeds without a single packet drop or disconnection.
   * *Conclusion:* **Conducted high-frequency switching noise from the running brushless DC fan motor is the sole cause of the failure.** When the motor spins, its electrical noise travels back along the shared GND and PWM lines into the ESP's ground plane. Because the onboard ceramic antenna uses the ESP's ground plane as its RF counterpoise, this noise **blinds the 2.4GHz Thread receiver**, leading to packet loss, parent eviction, and a permanent "No Response" lockout.

2. **The Debounce Timer Success (Verified):**
   * The new software debounce timer implemented in `app_driver.cpp` was verified in the logs. When the user adjusts the speed slider (e.g., setting it to 57%), the driver successfully aggregates the rapid flurry of write requests and updates the hardware and database **exactly once** 300ms later. This completely eliminates the Matter "packet storm" and protects the Thread network from congestion during slider movements.

---

### D. Recommended Hardware Solutions to Fix the Motor Noise

To allow the fan to run without blinding the ESP32's radio, you need to filter or isolate the conducted electrical noise. Here are the three best ways to do this, in order of effectiveness:

#### 1. Optocoupler Isolation (The Ultimate, Bulletproof Fix)
* **How it works:** Instead of sharing a ground and connecting the ESP's GPIO pin directly to the fan board, you use a cheap **optocoupler** (like a PC817 or EL817) to transmit the PWM signal.
* **The Setup & Pin Mapping:**
  * **Pin 1 (Anode):** Connect to the **ESP32's PWM pin (GPIO 21)** through a current-limiting resistor.
  * **Pin 2 (Cathode):** Connect to the **ESP32's GND pin**.
  * **Pin 3 (Emitter):** Connect to the **fan board's Ground** (12V supply ground).
  * **Pin 4 (Collector):** Connect to the **fan board's PWM input pin**.
  * *Critical Rule:* **The ESP Ground and the Fan Board Ground must remain completely isolated and never touch each other with a wire.**
* **Resistor Range (for Pin 1):**
  * **Safe Range:** **$100\Omega$ to $1\text{k}\Omega$ ($1,000\Omega$)**.
  * *Ideal values:* **$220\Omega$ to $330\Omega$** (e.g. **$300\Omega$** is perfect).
  * *Why?* Under $100\Omega$ draws too much current, risking damage to the ESP's GPIO. Over $1\text{k}\Omega$ limits the current too much, slowing down the optocoupler's switching response for the 25kHz PWM signal.
* **Why it's best:** There is **no physical electrical connection** between the ESP and the noisy fan board. The signal is transmitted purely by light inside the optocoupler. This completely immunizes the ESP's ground plane and antenna from any motor noise. This is the professional standard for driving high-noise motors from microcontrollers.

#### 2. Decoupling & Bulk Capacitors (Easy to Add)
* **How it works:** Place a low-ESR **electrolytic capacitor (100µF to 470µF)** in parallel with a small **ceramic capacitor (0.1µF)** directly across the 12V and GND terminals where the power supply enters the fan board.
* **Why it helps:** The capacitors act as a local energy reservoir, smoothing out the voltage spikes and shunting the high-frequency motor switching noise to ground before it can travel back along the wires.

#### 3. Ferrite Bead / Choke (Simple Clip-on)
* **How it works:** Wrap the GND, 12V, and PWM wires together 3 to 4 times through a **ferrite toroid**, or clip a **snap-on ferrite bead** over the wire bundle near the fan board.
* **Why it helps:** The ferrite bead acts as a high-frequency resistor, blocking high-frequency noise from propagating down the wires toward the ESP.

---

## 5. Git & Workspace Rules
* **Agent Configurations**: The `.agents/` folder (containing pairing rules, prompts, and this findings file) is **tracked in Git** and must be pushed to GitHub to sync rules and findings across the user's multiple flashing/development Macs.
* **Auto-Push Policy**: All code, configuration, and documentation changes must be **automatically staged, committed, and pushed** to GitHub immediately to ensure they are available for the user on their flashing machine.
