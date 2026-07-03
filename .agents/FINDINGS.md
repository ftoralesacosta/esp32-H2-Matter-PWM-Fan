# Seeed Studio XIAO ESP32-C6 Matter Fan Controller - Project Findings & Architecture

This document contains architectural findings, hardware mappings, and troubleshooting
workflows for the Matter PWM Fan Controller.

Future AI agents starting a new session MUST read this document before touching config,
because several earlier conclusions in this file were WRONG and have been corrected. Read
the "Corrected Root Cause" section first.

---

## 0. Corrected Root Cause (supersedes earlier EMI / data-race conclusions)

> [!IMPORTANT]
> The Thread disconnects are a **software/configuration** problem, not motor EMI. Every
> disconnect test was performed with the fan physically disconnected. The device drops
> from Thread regardless of the fan. Do not reintroduce the "conducted EMI is the sole
> cause" claim.

### Primary cause: committed `sdkconfig` overriding the defaults
`sdkconfig.defaults.*` only seeds a build when there is no existing `sdkconfig`. A stale
`sdkconfig` was committed to the repo and silently overrode the defaults. As a result the
firmware that actually shipped did NOT match `sdkconfig.defaults.esp32c6`:

* The committed `sdkconfig` had `# CONFIG_OPENTHREAD_ENABLED is not set` and
  `# CONFIG_OPENTHREAD_RX_ON_WHEN_IDLE is not set`.
* `sdkconfig.old` shows a prior FTD build (`CONFIG_OPENTHREAD_FTD=y`, MTD not set).
* Meanwhile `sdkconfig.defaults.esp32c6` claimed MTD + RX_ON_WHEN_IDLE.

Three different device-role stories across three files. Editing the defaults did nothing
because the committed `sdkconfig` won. This is why "switching to MTD" never took effect and
the device kept behaving like an FTD that partitions itself as leader.

**Fix applied:**
* `sdkconfig` and `sdkconfig.old` are now git-ignored and must be removed from tracking
  (`git rm --cached sdkconfig sdkconfig.old`). Never commit a generated `sdkconfig` again.
* Device role is now set deliberately in `sdkconfig.defaults.esp32c6` (see Section 2).
* Always build via `clean_build_and_flash.sh`, which does `rm -f sdkconfig` before
  `idf.py set-target esp32c6` so the defaults are actually applied.

### Secondary cause: over-aggressive MRP retry intervals
The C6 defaults forced every MRP retry interval to 2000ms with MAX_RETRANS=3. Long retry
intervals widen the recovery window after a dropped packet. The runtime log
(`extra_monitor_log.txt`) shows the device healthy through ~4.8s uptime, then a ~9-minute
silent gap, then `OPENTHREAD:[N] Mle: Attach attempt 0, BetterParent` — i.e. it lost its
parent and re-attached, with no crash and no reboot (uptime kept climbing). That is a
link/timing signature, not RF noise (which would show `NoAck` bursts and 6LoWPAN fragment
failures) and not a reboot loop. The MRP overrides have been removed so the faster SDK
defaults apply.

### What was genuinely fixed earlier and should stay fixed
* **Deleting `print_ip_addresses_task`** (Section G, old doc): correct and necessary. That
  task called `otIp6GetUnicastAddresses()` without the OpenThread stack lock, a real data
  race. Keep it deleted. It was NOT, however, "the ultimate fix" — drops continued after it,
  so it was one bug among several.
* **RF switch init** (`init_rf_switch()` in app_main.cpp): correctly drives GPIO3=LOW
  (enable switch) and GPIO14=LOW (select ceramic antenna). Keep it.
* **FeatureMap = 1 (MultiSpeed)**: correctly set so the Home app shows a speed slider.

### Debunked claims removed from this document
* "Conducted EMI from the fan motor is the sole cause" — FALSE (fan was disconnected).
* "Debounce timer verified working" — the cited log line is not reproducible in the repo
  logs, and the timer had a re-entrancy bug (see Section 3). Now fixed.
* The tangled MED vs SED vs MTD narrative — MTD is a build-time device type; RX_ON_WHEN_IDLE
  is a radio behavior. They are independent. See Section 2 for the correct combination.

---

## 1. Hardware Overview & Pin Mapping

Built on the **Seeed Studio XIAO ESP32-C6**. (Earlier work also used the
**ESP32-H2-DevKitM-1-N4S**; both showed the same drops, which is consistent with the
shared software/config root cause above, not a per-board silicon defect.)

> [!WARNING]
> **DO NOT confuse physical pin labels with the chip's internal GPIO numbers.** The XIAO
> form factor uses a multiplexed pinout where silkscreen numbers do NOT map 1-to-1 to GPIOs.

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
| **RF Switch Power** | Antenna Power | **GPIO3** | RF switch enable (drive LOW) |
| **RF Switch Port** | Antenna Select | **GPIO14** | Antenna select (LOW = ceramic) |

> [!NOTE]
> The onboard status LED turning off after commissioning is NORMAL. Do not treat "LED went
> dark" as evidence of a power/USB shutdown — in past testing the logs kept showing activity
> with the LED off. LED state is not a reliable liveness signal.

### PWM Fan Wiring (Noctua Standard)
* **Control Pin**: Fan PWM wire (blue) to physical pin **D3** = **GPIO21** in software.
* **LEDC**: low-speed mode, 10-bit duty (0-1023), **25 kHz** (Noctua spec).

---

## 2. Core Matter & Thread Architecture

### Thread Device Role (current: FTD, radio always on)
Configured in `sdkconfig.defaults.esp32c6`:
```ini
CONFIG_OPENTHREAD_ENABLED=y
CONFIG_OPENTHREAD_FTD=y
# CONFIG_OPENTHREAD_MTD is not set
CONFIG_OPENTHREAD_RX_ON_WHEN_IDLE=y
```
* **FTD vs MTD:** FTD is the current choice (project preference) and is stable as long as a
  healthy border router (Apple TV/HomePod) is present. The old "detaches -> leader ->
  isolated partition" symptom was caused by the config drift in Section 0, not by FTD per se.
* **Fallback:** If leader-partition drops ever recur, switch to a Minimal End Device:
  `CONFIG_OPENTHREAD_FTD=n` / `CONFIG_OPENTHREAD_MTD=y`, keeping `RX_ON_WHEN_IDLE=y`. A
  non-sleepy MED keeps the radio on (low latency) and cannot become leader.
* **Do NOT** set `RX_ON_WHEN_IDLE=n` (sleepy) for this mains-powered controller; polling
  latency hurts CASE resumption and large wildcard reads.

### MRP timing
The four `CONFIG_MRP_*` overrides (all 2000ms) have been removed. Use SDK defaults unless
there is a measured reason to change them; if you do, re-add the lines deliberately and
document why here.

---

## 3. Application Driver Notes

### Debounce timer (fixed)
`app_driver.cpp` debounces rapid `PercentSetting` writes (300ms) before applying LEDC duty
and syncing `PercentCurrent`/`FanMode`.

* **Bug that was fixed:** the debounce callback calls `attribute::update()`, which re-enters
  the attribute-update callback path. That re-entry previously hit the same
  `esp_timer_stop`/`esp_timer_start_once` logic and re-armed the timer from inside its own
  callback, so it effectively never settled. A `driver_initiated_update` guard now short-
  circuits driver-issued writes so only genuine user writes (re)arm the timer.
* If you ever simplify: for a fan slider it is acceptable to drop the debounce entirely and
  apply LEDC duty immediately. The "packet storm" it guards against is not a real problem on
  a correctly configured non-sleepy device.

---

## 4. Critical Developer Workflows

### Target selection & clean builds
Always build through `clean_build_and_flash.sh`. It removes the stale `sdkconfig`, sets the
target to `esp32c6` (so `sdkconfig.defaults.esp32c6` is merged), builds, erases flash, and
flashes. Setting the target explicitly matters: without it ESP-IDF defaults to `esp32` and
skips the C6 defaults.

### Verifying the flashed config actually matches intent
After flashing, confirm in the boot log that OpenThread is enabled and the MLE role settles
to `child`/`router` (FTD) rather than `disabled` or `leader`. Do not trust the defaults file
alone — verify against the running device.

### Stabilizing HomeKit connection failures
If the chip shows "Not Responding", or logs `SRP update error: ... duplicated` or
`unknown session`, the border router likely holds stale fabrics/sessions from a previous
flash. Clear NVS and re-pair:
```bash
idf.py erase-flash
idf.py flash monitor
```
Remove the accessory from Apple Home and re-add with Manual Setup Code `20202021`,
Discriminator `3840`.

### Diagnosing a drop
Capture the serial log ACROSS the disconnect; the informative part is the gap. Distinguish:
* Role goes to `leader` -> FTD partition problem -> switch to MTD fallback.
* `detached -> child` re-attach after a silent gap -> link/timing -> MRP/defaults change
  should help; check border-router health and RSSI.
External checks from a Mac on the same network remain useful: `ping6` the device's OMR
address, `dns-sd -B _matter._tcp` to confirm the border router's advertising proxy, and
`dns-sd -L`/`-G v6` to confirm hostname resolution.

---

## 5. Git & Workspace Rules
* `.agents/` is tracked in Git and synced across the user's flashing/dev Macs.
* **NEVER commit `sdkconfig` or `sdkconfig.old`** — they are generated and override the
  defaults. They are now in `.gitignore`. Run `git rm --cached sdkconfig sdkconfig.old` once
  to stop tracking them.
* Auto-push policy: code/config/doc changes are staged, committed, and pushed immediately so
  they are available on the flashing machine.
