# Cardputer ADV Smart Home Remote v0.2.0

**English** | [Polski](README_PL.md)

A portable Home Assistant remote for the M5Stack Cardputer ADV. The firmware displays rooms and live entity states, controls lights, switches, covers, scenes, scripts, and buttons, and keeps the latest known states in non-volatile memory.

The project communicates through MQTT. The Cardputer does not store a Home Assistant administrator token and does not call the Home Assistant API directly. The included Home Assistant package publishes selected entity states and only executes commands for entities on an explicit allowlist.

## Features

- rooms and entities configured in a single `config/devices.json` file;
- startup screen with the application version and project address;
- fixed, centered temperature and humidity bar for every room;
- flicker-free UI rendering through an off-screen frame buffer;
- live states delivered by Home Assistant over MQTT;
- ON/OFF control for lights and switches;
- open, stop, and close controls for covers;
- scene, script, and button activation;
- read-only sensor and binary sensor display;
- NVS state cache; the `~` prefix marks a cached value not yet confirmed after reconnecting;
- automatic Wi-Fi and MQTT reconnection;
- retained MQTT LWT status: `online` or `offline`;
- screen sleep without disconnecting the remote;
- configuration generator that keeps the firmware and Home Assistant entity lists synchronized.

## Controls

| Key               | Action                                                           |
| ----------------- | ---------------------------------------------------------------- |
| `Fn` + Left/Right | Previous/next room                                               |
| `Fn` + Up/Down    | Previous/next entity                                             |
| `A` / `D`         | Alternative room navigation                                      |
| `W` / `S`         | Alternative entity navigation                                    |
| `Enter`           | Light/switch: ON/OFF; scene/script/button: activate; cover: STOP |
| `1` / `2` / `3`   | Cover: open / stop / close                                       |
| `R`               | Request a full state refresh                                     |

The first key press after screen sleep only wakes the display. It does not execute an action, preventing an accidental press from switching a device or moving a cover.

## Requirements

- M5Stack Cardputer ADV;
- Home Assistant with a working MQTT integration and Mosquitto broker;
- PlatformIO IDE for Visual Studio Code or PlatformIO Core;
- 2.4 GHz Wi-Fi access to the MQTT broker;
- valid Home Assistant entity IDs.

## 1. Configure rooms and entities

Open `config/devices.json`. Replace the supplied example entries with entity IDs from your Home Assistant installation.

The application version and project address shown on the startup screen are configured at the top of the file:

```json
"app_version": "0.2.0",
"project_url": "github.com/MajsterTukan/Cardputer-HA-Remote"
```

Example room:

```json
{
  "name": "Bathroom",
  "devices": [
    { "name": "Light", "entity_id": "light.bathroom", "type": "light" },
    { "name": "Cover", "entity_id": "cover.bathroom", "type": "cover" },
    {
      "name": "Temperature",
      "entity_id": "sensor.bathroom_temperature",
      "type": "sensor",
      "climate_role": "temperature"
    },
    {
      "name": "Humidity",
      "entity_id": "sensor.bathroom_humidity",
      "type": "sensor",
      "climate_role": "humidity"
    }
  ]
}
```

The optional `climate_role` field selects the sensor displayed in the room climate bar. Supported values are `temperature` and `humidity`. If a sensor is not assigned or its state is unavailable, the UI displays `--` without moving the remaining screen elements.

Supported entity types:

| `type`          | Required entity domain | Control         |
| --------------- | ---------------------- | --------------- |
| `light`         | `light.*`              | ON/OFF          |
| `switch`        | `switch.*`             | ON/OFF          |
| `cover`         | `cover.*`              | OPEN/STOP/CLOSE |
| `scene`         | `scene.*`              | Activate        |
| `script`        | `script.*`             | Activate        |
| `button`        | `button.*`             | Press           |
| `sensor`        | `sensor.*`             | Read only       |
| `binary_sensor` | `binary_sensor.*`      | Read only       |

The generator rejects duplicate entities, invalid domains, unsupported types, more than 64 devices, or more than 24 rooms.

Use short ASCII display names when possible. The default compact M5GFX font does not render every international character correctly.

## 2. Configure Wi-Fi and MQTT

Create your private configuration from the template:

```bash
cp include/secrets.example.h include/secrets.h
```

PowerShell equivalent:

```powershell
Copy-Item include/secrets.example.h include/secrets.h
```

Edit `include/secrets.h`:

```cpp
inline constexpr char WIFI_SSID[]     = "YOUR_WIFI_NAME";
inline constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

inline constexpr char MQTT_HOST[]     = "192.168.1.50";
inline constexpr unsigned MQTT_PORT   = 1883;
inline constexpr char MQTT_USER[]     = "cardputer";
inline constexpr char MQTT_PASSWORD[] = "A_STRONG_DEDICATED_PASSWORD";
```

Create a dedicated MQTT account for the remote. Do not use a Home Assistant administrator account. `include/secrets.h` is excluded by `.gitignore`.

## 3. Generate the firmware and Home Assistant configuration

Run from the project root:

```bash
python scripts/generate_config.py
```

The command creates or updates:

- `include/generated_config.h` for the firmware;
- `home_assistant/cardputer_remote.yaml` for Home Assistant.

PlatformIO also runs the generator automatically before every build.

## 4. Install the Home Assistant package

### Option A: packages directory

Add this to the existing `homeassistant:` section in `/config/configuration.yaml`:

```yaml
homeassistant:
  packages: !include_dir_named packages
```

Do not create a second `homeassistant:` section.

Then:

1. Create `/config/packages` if it does not exist.
2. Copy `home_assistant/cardputer_remote.yaml` to `/config/packages/cardputer_remote.yaml`.
3. Validate the Home Assistant configuration.
4. Restart Home Assistant.

After changing `config/devices.json`, generate the files again and copy the updated package to Home Assistant.

### Option B: single file in `/config`

If `cardputer_remote.yaml` is stored directly in `/config`, assign it a package name:

```yaml
homeassistant:
  packages:
    cardputer_remote: !include cardputer_remote.yaml
```

Do not use `packages: !include cardputer_remote.yaml`. That form loads the file one level too high and causes Home Assistant to report `expected a mapping`.

## 5. Build and upload

From a PlatformIO terminal:

```bash
pio run
pio run --target upload
```

You can also use the **Build** and **Upload** actions in the PlatformIO extension for Visual Studio Code.

If the Cardputer is not detected in upload mode:

1. Set the side power switch to `OFF`.
2. Hold the `G0` button.
3. Connect or reconnect the USB cable.
4. Release `G0` after the device is detected.
5. Upload the firmware and return the switch to `ON`.

Serial diagnostics:

```bash
pio device monitor
```

The compiled image is available at `.pio/build/cardputer-adv/firmware.bin`.

## MQTT topics

| Topic                              | Direction          | Purpose                                    |
| ---------------------------------- | ------------------ | ------------------------------------------ |
| `home/cardputer/command`           | Cardputer → HA     | JSON command                               |
| `home/cardputer/state/<entity_id>` | HA → Cardputer     | Retained JSON state                        |
| `home/cardputer/request_state`     | Cardputer → HA     | Full snapshot request                      |
| `home/cardputer/status`            | Cardputer → broker | Retained `online`/`offline` status and LWT |

Example command:

```json
{
  "entity_id": "light.living_room_main",
  "command": "turn_on",
  "source": "cardputer-adv-remote-A12B",
  "sequence": 7
}
```

## Security

- Never expose MQTT port `1883` to the internet.
- Keep the broker on the local network. An IoT VLAN should only allow the Cardputer to reach DNS, DHCP, and the MQTT broker.
- Use a dedicated MQTT account and restrict it to `home/cardputer/#` when broker ACLs are available.
- The Home Assistant package contains an entity allowlist. Replacing `entity_id` in an MQTT message cannot control an entity outside that list.
- ON/OFF actions are idempotent instead of using `toggle`, so replaying a message does not reverse the state a second time.
- The current release uses plain MQTT inside the LAN and is not intended for public internet access.

## Troubleshooting

| Symptom                             | Most likely cause                                                    |
| ----------------------------------- | -------------------------------------------------------------------- |
| `NO CONFIGURATION`                  | A `CHANGE_ME` value remains in `secrets.h`                           |
| `WF` but no `MQ`                    | Incorrect broker address, port, username, or password                |
| State starts with `~`               | Cached value is displayed; no current MQTT state has arrived         |
| Entities display `UNKNOWN`          | Invalid entity IDs, missing HA package, or missing retained snapshot |
| Command is sent but nothing happens | Entity does not exist or its type does not match `devices.json`      |
| Keys do not respond                 | Verify the M5Cardputer dependency and Cardputer ADV target           |
| Home Assistant rejects the YAML     | Regenerate the file and verify indentation in `configuration.yaml`   |

## Project structure

```text
config/devices.json                    rooms and entity definitions
home_assistant/cardputer_remote.yaml   generated Home Assistant package
include/generated_config.h             generated C++ configuration
include/secrets.example.h               private network configuration template
scripts/generate_config.py              validator and generator
src/main.cpp                            remote firmware
platformio.ini                          PlatformIO environment
README.md                               English documentation
README_PL.md                            Polish documentation
```
