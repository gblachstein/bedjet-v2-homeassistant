# BedJet V2 → Home Assistant

Control a **BedJet V2** from Home Assistant using an ESP32 as a Bluetooth-to-MQTT bridge.

> **A friendly heads-up:** I'm not a developer, just a BedJet V2 owner who really wanted it in Home Assistant. AI tools did a lot of the heavy lifting in writing and debugging the firmware and the Home Assistant config. It works well in my setup, but expect some rough edges. Issues and pull requests from people who know this stuff better are very welcome!

The Home Assistant BedJet integration (core and HACS) only supports the **BedJet 3**. The V2 uses a different BLE protocol, so this project puts an ESP32 near the bed to translate between it and MQTT:

```
BedJet V2  <--BLE-->  ESP32 (this firmware)  <--Wi-Fi/MQTT-->  MQTT broker  <-->  Home Assistant
```

## Features

| Entity | What it does |
|---|---|
| `climate.bedjet_v2` | Off / Cool / Heat / Auto (= Turbo), target temp 66–104 °F, fan speed 5–100 % as fan modes, current temp |
| `fan.bedjet_fan` | On/off + fan percentage slider |
| `number.bedjet_timer` | Set run timer, 0–720 min |
| `button.bedjet_timer_add_15m` | Adds 15 min to the running timer |
| `sensor.bedjet_time_remaining` | Timer remaining as `H:MM` |
| `switch.bedjet_turbo_mode` | Turbo on/off |
| `switch.bedjet_beep` | Mute/unmute the BedJet beeps |
| `switch.bedjet_ble_connection` | Releases the Bluetooth link so the phone app can connect |
| `sensor.bedjet_esp32_ip` | The ESP32's IP (for OTA updates) |
| `script.reset_bedjet` | Drops and re-establishes the BLE link to unstick the bridge |

## Repository layout

```
firmware/bedjet_v2_bridge/
  bedjet_v2_bridge.ino     ESP32 sketch
  config.example.h         settings template, copy to config.h
homeassistant/packages/
  bedjet_v2.yaml           all HA entities + reset script
```

## Requirements

- An **ESP32** dev board within good Bluetooth range of the BedJet. I used an [ESP-WROOM-32 ESP32 ESP-32S development board (Amazon)](https://a.co/d/0crXcJcD). Any classic ESP32-WROOM-32 DevKit should work. The ESP32-S2 has no Bluetooth, so it won't work.
- **Arduino IDE** 2.x with the **esp32 by Espressif** board package, plus the **PubSubClient** library by Nick O'Leary (install it from the Library Manager).
- An **MQTT broker**, such as the Mosquitto add-on in Home Assistant, and an MQTT username and password for the ESP32.
- The **MQTT integration** set up in Home Assistant.
- Your BedJet V2's **Bluetooth MAC address**. The device usually shows up as `BEDJET`. Two easy ways to find it:
  - **In Home Assistant:** if your HA host has Bluetooth or you run an ESPHome Bluetooth proxy, go to **Settings → Devices & Services → Bluetooth → Configure → Advertisement monitor** and look for `BEDJET`.
  - **On your phone:** use a BLE scanner app such as nRF Connect.

  Close the BedJet phone app first. The BedJet usually stops advertising while something is connected to it.

## Installation

### 1. Flash the ESP32

1. Clone or download this repo and open `firmware/bedjet_v2_bridge/bedjet_v2_bridge.ino` in the Arduino IDE.
2. Copy `config.example.h` to `config.h` in the same folder and fill in:
   - `BEDJET_MAC`: your BedJet's MAC, e.g. `"A1:B2:C3:D4:E5:F6"`
   - `WIFI_SSID` / `WIFI_PASSWORD`: a 2.4 GHz network
   - `MQTT_HOST` / `MQTT_PORT` / `MQTT_USER` / `MQTT_PASSWORD`: your broker. With the Mosquitto add-on, `MQTT_HOST` is your Home Assistant IP.
   - Optional: `MQTT_CLIENT_ID` and `OTA_HOSTNAME` (must be unique if you run more than one bridge), and `OTA_PASSWORD`.

   `config.h` is git-ignored, so if you fork or push this repo, your Wi-Fi and MQTT passwords stay on your machine.
3. Board: **ESP32 Dev Module**. Partition scheme: **Minimal SPIFFS (1.9MB APP with OTA)**, or **Huge APP** if you don't need OTA. BLE + Wi-Fi usually doesn't fit the default partition ("Sketch too big").
4. Upload, then open the Serial Monitor at **115200** baud. You should see:
   ```
   WiFi Connected! IP: ...
   Connecting to MQTT Broker...Connected!
   Connecting to BedJet BLE... Subscribed to BedJet TX Channel... Connected and Ready!
   ```
5. Later updates can go over Wi-Fi. In the Arduino IDE, pick the `bedjet_esp32` network port.

### 2. Add the entities to Home Assistant

**Option A – Package (recommended).** If you don't already have packages enabled, add this to `configuration.yaml`:

```yaml
homeassistant:
  packages: !include_dir_named packages
```

Then copy `homeassistant/packages/bedjet_v2.yaml` into `/config/packages/`.

**Option B – Paste directly.** Copy the `mqtt:` section of `bedjet_v2.yaml` into `configuration.yaml`, merging it under any existing `mqtt:` key. Then create the script from the `script:` section in **Settings → Automations & Scenes → Scripts**, using YAML mode.

Restart Home Assistant. Developer Tools → YAML → **Check configuration** first is a good idea.

### 3. Verify

- In **Settings → Devices & Services → MQTT → Configure → Listen to a topic**, subscribe to `home/bedjet/#`. A status message should arrive every few seconds.
- Change the temperature or mode from the `climate.bedjet_v2` card. The BedJet should respond within a second or two.

## How it works (MQTT contract)

**Status:** the ESP32 publishes to `home/bedjet/status` about every 3–4 s while connected:

```json
{"mode":"COOL","fan":40,"temp":78.8,"target":71.6,"beep":"ON","timer":465}
```

`mode` is one of `OFF` / `COOL` / `HEAT` / `TURBO`, or `DISABLED` while BLE is released. Temperatures are in °F and `timer` is minutes remaining.

**Retained topics:** `home/bedjet/ip` and `home/bedjet/ble` (`ON`/`OFF`).

**Commands** go to `home/bedjet/command` (case-insensitive):

| Payload | Effect |
|---|---|
| `COOL` / `HEAT` / `TURBO` | Switch mode / power on |
| `OFF` | Power off (ignored if already off) |
| `TEMP 72` | Target temp in °F (clamped 66–109) |
| `FAN 40` | Fan %, rounded to the nearest 5 (5–100) |
| `TIMER 120`, `TIMER +15`, `TIMER -15` | Set or adjust the run timer in minutes (1–720) |
| `BEEP ON` / `BEEP OFF` | Unmute / mute beeps |
| `BLE OFF` / `BLE ON` | Release / reclaim the Bluetooth link |

## What's not covered

The bridge only implements the BLE commands that were worked out for everyday control. Missing:

- **Memory presets (M1 / M2 / M3).** There's no recall or save of the BedJet's stored presets. You can recreate them with Home Assistant scripts that send mode, temp, fan and timer.
- **Dry mode / other special modes.** Only Cool, Heat and Turbo are mapped, and only if your unit has them. Anything else the BedJet reports shows as `UNKNOWN` / off.
- **Sleep programs and scheduling on the BedJet itself.** Use Home Assistant automations instead, e.g. start Heat 20 min before bedtime.
- **Celsius.** The firmware and YAML are °F only. HA will still convert for display if your system is metric, but the command range is hard-coded in °F.
- **Multiple BedJets.** Each unit needs its own ESP32, and the MQTT topics and entity names are hard-coded. A second bridge needs different topics in the sketch and a second copy of the YAML.
- **Availability / health reporting.** There's no MQTT Last Will or availability topic, and no BLE signal strength or connection-age sensor.
- **Phone app at the same time.** BLE allows one connection, so it's HA *or* the app (see below).
- **BedJet firmware updates, pairing and settings.** Do these from the official app with the bridge's BLE switched off.
- **MQTT discovery / HA device.** Entities are defined in YAML rather than auto-discovered, so they don't appear grouped under a device.

## Notes and limitations

- **One Bluetooth connection at a time.** While the ESP32 is connected, the BedJet phone app can't connect. Turn off `switch.bedjet_ble_connection` to use the app, then turn it back on. While it's off, HA shows the BedJet as off and ignores commands.
- **Beep control is flaky.** The beep switch seems to stop working after a while. I haven't cared enough to dig into why yet, so PRs are welcome.
- **"Auto" in the climate card means Turbo.** The V2 has no real auto mode.
- **Temp, fan and timer commands resend the full state.** If no timer is running, the bridge sets an 8-hour timer when you change temp or fan.
- **Automatic reconnects.** After an ESP32 reboot, the bridge retries the BedJet connection every 20 s.
- **No MQTT availability topic yet.** If the ESP32 goes offline, HA keeps showing the last values instead of `unavailable`.
- **Hard-coded topics.** Everything uses `home/bedjet/...`. If you change the topics in the sketch, update the YAML to match.
- **Unofficial.** This project isn't affiliated with or endorsed by BedJet. Use at your own risk.

## Credits

The BedJet V2 BLE protocol (service/characteristic UUIDs, packet layout, checksum and status decoding) was reverse-engineered by **[@roycamp](https://github.com/roycamp)**. See **[roycamp/bedjet-protocol](https://github.com/roycamp/bedjet-protocol)**. This bridge builds on that work.

## License

Released under the [MIT License](LICENSE).
