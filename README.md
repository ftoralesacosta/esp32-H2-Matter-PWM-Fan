# Matter PWM Fan Controller (Seeed Studio XIAO ESP32-C6)

A Matter-over-Thread PWM fan controller built with ESP-IDF and ESP-Matter. Runs on a Seeed
Studio XIAO ESP32-C6 and drives a standard 4-pin Noctua-style PWM fan, exposed to Apple Home
(or any Matter controller) as a Fan device with speed control and a physical toggle button.

For deep architecture notes, hardware pin mapping, and a running troubleshooting log, see
[`.agents/FINDINGS.md`](.agents/FINDINGS.md) - read it before changing `sdkconfig.defaults*`
or the Thread/mDNS configuration, since several non-obvious footguns are documented there.

## Hardware

* **Board:** Seeed Studio XIAO ESP32-C6
* **Fan PWM output:** physical pin `D3` / GPIO21 (LEDC, low-speed mode, 10-bit duty, 25 kHz)
* **Toggle button:** physical pin `D2` / GPIO2 (active high)
* **Onboard RF switch:** GPIO3 (enable, drive LOW) + GPIO14 (antenna select, LOW = ceramic) -
  required on this board or the radio gets no usable antenna path

See `.agents/FINDINGS.md` for the full pin table and wiring notes.

## 1. Environment Setup

Requires ESP-IDF and ESP-Matter set up per the
[ESP-Matter getting started guide](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html),
with `IDF_PATH` and `ESP_MATTER_PATH` sourced/exported in your shell.

## 2. Build & Flash

Always build through `clean_build_and_flash.sh` rather than calling `idf.py` directly - it
sources the ESP-IDF/ESP-Matter environments, clears any stale `sdkconfig` (which would
otherwise silently override `sdkconfig.defaults.esp32c6`), sets the target to `esp32c6`, and
erases + flashes after a successful build:

```bash
./clean_build_and_flash.sh
```

The board has 4MB of flash; the custom partition table (`partitions.csv`) is sized for it.

## 3. Commissioning

* **Manual setup code:** `20202021`
* **Discriminator:** `3840`

The device advertises over BLE for commissioning, then over Thread (via SRP registration to
your Thread Border Router) once joined. If commissioning hangs or the accessory shows "No
Response" after previously pairing, see the troubleshooting workflows in
`.agents/FINDINGS.md` before re-flashing - most historical failures traced back to
configuration drift (a stale committed `sdkconfig`) or Thread mDNS/SRP misconfiguration, not
hardware.

## 4. Post-Commissioning Behavior

* Fan speed is controlled via the Fan Control cluster's `PercentSetting` attribute (0-100%),
  debounced 300ms before being applied to the LEDC output and synced back to
  `PercentCurrent`/`FanMode`.
* The physical button toggles the fan between off and its default speed.
* `FanMode`'s `MultiSpeed` feature bit is set so Apple Home shows a speed slider rather than
  just an on/off switch.
