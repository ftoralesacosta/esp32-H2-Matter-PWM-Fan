# Seeed Studio XIAO ESP32-C6 Matter Fan Controller - Project Findings & Architecture

## Executive Actionable Summary
* **Root Cause of Disconnects:** A stale `sdkconfig` file committed to version control silently overrode the `sdkconfig.defaults.esp32c6` configuration. This caused the firmware to compile with OpenThread incorrectly configured (or completely disabled), leading to silent parent loss and 9-minute MLE re-attach gaps.
* **EMI Hypothesis Debunked:** Motor noise (Conducted EMI) is NOT the cause of the Thread drops. The device disconnects due to software configuration and stack re-entrancy issues even when the fan is unpowered. 
* **Immediate Actions Required:**
  * Run `git rm --cached sdkconfig` and add `sdkconfig` to `.gitignore` to prevent configuration drift.
  * Revert aggressive MRP retry intervals in `sdkconfig.defaults.esp32c6` back to SDK defaults to allow standard packet recovery.
  * Refactor the PWM debounce timer to update the LEDC driver directly without re-entering the Matter stack (`attribute::update`), or remove the debounce entirely.

---

## 1. Hardware Overview & Pin Mapping

The project is currently built on the **Seeed Studio XIAO ESP32-C6** development board. 
*(Note: Initial development and troubleshooting was also extensively conducted using the **ESP32-H2-DevKitM-1-N4S**. Both boards exhibited identical Thread "No Response" drops, which helped confirm the root cause was related to software configuration rather than a silicon defect).*

> [!WARNING]
> **DO NOT confuse physical pin labels with the chip's internal GPIO numbers.** The Seeed Studio XIAO form factor uses a multiplexed pinout where board silkscreen numbers do NOT map 1-to-1 with ESP32-C6 GPIOs.

### Seeed Studio XIAO ESP32-C6 Pin Map

| Silkscreen Label | Function | ESP32-C6 GPIO | Role in this Project |
| :---: | :---: | :---: | :--- |
| **D2** | Analog/Digital | **GPIO2** | **IoT Button Input** (Active High) |
| **D3** | Digital | **GPIO21** | **Noctua Fan PWM Output (LEDC)** |
| **User LED** | Onboard LED | **GPIO15** | Status Indicator |
| **RF Switch Power** | Antenna Power | **GPIO3** | Internal Antenna Power (Do not drive/use!) |
| **RF Switch Port** | Antenna Select | **GPIO14** | Internal Antenna Switch |
*(Unused pins omitted for brevity)*

### PWM Fan Wiring (Noctua Standard)
* **Control Pin**: Connect the fan's PWM control wire (usually Blue) to the physical pin labeled **`3` (or `D3`)**, which corresponds to **`GPIO21`** in software.
* **LEDC Driver Details**: Configured as low-speed LEDC, 10-bit duty cycle resolution (0 to 1023), running at **25 kHz** (the Noctua PWM frequency spec).

---

## 2. Core Matter & Thread Architecture

### Thread Device Role Configuration
* The firmware must be explicitly configured with a stable Thread role to prevent the device from isolating itself. 
* **Recommended Configuration (FTD):** Assuming a stable Apple TV/HomePod Border Router is present, configuring the device as a Full Thread Device (FTD) with the radio always on is the most robust option for a mains-powered fan.
* **Configuration:** Update `sdkconfig.defaults.esp32c6` to include:
  ```ini
  CONFIG_OPENTHREAD_ENABLED=y
  CONFIG_OPENTHREAD_FTD=y
  CONFIG_OPENTHREAD_RX_ON_WHEN_IDLE=y
  CONFIG_ENABLE_PERSIST_SUBSCRIPTIONS=y
  ```
* **Crucial Timing Fix:** Do NOT manually override the Thread MRP (Message Reliability Protocol) retry timers. Remove all `CONFIG_MRP_*` overrides from the defaults file. Forcing aggressive 2000ms intervals widens the recovery gap during packet loss, exacerbating the silent disconnects.

---

## 3. Critical Developer Workflows

### Configuration Management (The `sdkconfig` Trap)
* **The Issue:** Editing `sdkconfig.defaults.esp32c6` will have absolutely no effect if a generated `sdkconfig` already exists in the project root. ESP-IDF prioritizes the existing cache, which is what caused historical builds to silently drift away from the intended MTD/FTD settings.
* **The Fix:** The `sdkconfig` file must never be committed to git. 
* **Command:** Always clear the cache before targeting the C6:
  ```bash
  rm -f sdkconfig
  idf.py set-target esp32c6
  ```

---

## 4. Thread Disconnect & Troubleshooting History

### A. The "No Response" Configuration Drift
* **The Symptom:** The device operates normally, successfully resuming CASE sessions and streaming `ReportData`, but suddenly enters a ~9-minute wall-clock gap with zero serial output. It then abruptly attempts a Thread MLE re-attach (`Attach attempt 0, BetterParent`).
* **The Cause:** Version control drift. A stale `sdkconfig` was compiling the firmware with OpenThread either fully disabled or acting as a sleepy end device with broken polling intervals, causing silent parent loss during idle periods. 

### B. The Motor EMI Hypothesis (DEBUNKED)
* **Previous Assumption:** It was hypothesized that conducted high-frequency switching noise from the Noctua fan motor was blinding the 2.4GHz Thread receiver.
* **The Truth:** Log analysis confirms the exact same MLE disconnect signature occurs even when the 12V fan power is entirely disconnected. The connection drops are purely protocol and configuration-based. Complex hardware isolation (optocouplers) is unnecessary for network stability.

### C. The Debounce Timer Re-entrancy Bug
* **The Issue:** A 300ms software debounce timer (`debounce_timer_callback`) was implemented in `app_driver.cpp` to prevent Matter packet storms during rapid slider adjustments in Apple Home.
* **The Failure:** The timer's callback incorrectly invoked `attribute::update(...)`. For `PercentSetting` writes, this re-entered the driver's update path, attempting to stop and start the exact timer that was currently executing. This created a severe race condition and fragile state management.
* **The Fix:** If debouncing is used, the callback must only execute the pure LEDC hardware update (`app_driver_fan_set_speed()`) and bypass the Matter stack. Alternatively, the debounce timer can be removed entirely, as a properly configured FTD/MED can safely handle rapid LEDC writes without network saturation.

### D. The Data Race Crash
* **The Issue:** A debug task (`print_ip_addresses_task`) in `app_main.cpp` ran an infinite loop calling OpenThread APIs (`otIp6GetUnicastAddresses`) without acquiring the stack lock.
* **The Fix:** The task was deleted. OpenThread is strictly single-threaded and non-reentrant. Any concurrent FreeRTOS access without `esp_openthread_lock_acquire()` causes memory corruption and SRP client crashes.

### E. RF Switch / Antenna Path Disabled in Software
* **The Discovery:** The XIAO ESP32-C6 utilizes an onboard RF Switch chip (`FM8625H`) to toggle between the ceramic antenna and the u.FL connector. A generic ESP-IDF build leaves this switch floating, resulting in extreme packet fragmentation (`error:NoAck`).
* **The Software Fix:**
  - Configure `GPIO 3` (`RF_SWITCH_EN`) to `LOW` to enable the switch.
  - Configure `GPIO 14` (`RF_ANT_SELECT`) to `LOW` to select the ceramic antenna.

---

## 5. Git & Workspace Rules
* **Configuration Exclusion:** `sdkconfig` MUST be added to `.gitignore` and removed from the Git cache to prevent future configuration drift across development machines.
* **Agent Configurations**: The `.agents/` folder (containing pairing rules, prompts, and this findings file) is tracked in Git and synced across multiple host machines.

---

## 6. Custom C++ vs. ESPHome Matter-over-Thread Comparison

### Custom C++ Setup (Current)
* **Pros:**
  * **Surgical Network Tuning:** Direct access to `sdkconfig.defaults.esp32c6` allows for precise subscription persistence and standard MRP management.
  * **Efficiency:** Minimal footprint and direct hardware access via the ESP-IDF LEDC API.
* **Cons:**
  * **High Complexity:** Requires writing and maintaining dense C++ code and synchronized stack locks.
  * **Configuration Cache Issues:** The ESP-IDF build system easily caches `sdkconfig`, requiring vigilant workspace hygiene to apply configuration changes.

### ESPHome Setup
* **Pros:**
  * **YAML Simplicity:** Zero C++ boilerplate. The entire device configuration is defined in a single, human-readable YAML file.
  * **Automated Matter Mapping:** Automatically handles the creation of Matter clusters, endpoints, and commissioning credentials.
* **Cons:**
  * **No Low-Level Tuning:** Cannot easily modify the underlying OpenThread settings.
  * **Experimental Status:** The ESPHome `matter` component is still in active development.

