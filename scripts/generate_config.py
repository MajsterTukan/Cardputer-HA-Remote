#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


TYPE_MAP = {
    "light": "Light",
    "switch": "Switch",
    "cover": "Cover",
    "scene": "Scene",
    "script": "Script",
    "button": "Button",
    "sensor": "Sensor",
    "binary_sensor": "BinarySensor",
}

ENTITY_RE = re.compile(
    r"^(light|switch|cover|scene|script|button|sensor|binary_sensor)\.[a-z0-9_]+$"
)
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")
CLIMATE_ROLES = {"temperature", "humidity"}


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def yaml_single_quote(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def load_and_validate(project_dir: Path) -> dict:
    source = project_dir / "config" / "devices.json"
    try:
        data = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"Nie mozna odczytac {source}: {exc}") from exc

    version = data.get("app_version", "")
    if not isinstance(version, str) or not VERSION_RE.fullmatch(version):
        raise ValueError("app_version musi miec format X.Y.Z, np. 0.2.0.")

    project_url = data.get("project_url", "")
    if not isinstance(project_url, str) or len(project_url) > 36:
        raise ValueError("project_url musi byc tekstem o dlugosci do 36 znakow.")

    base = data.get("mqtt_base_topic", "")
    if not isinstance(base, str) or not base or base.startswith("/") or base.endswith("/"):
        raise ValueError("mqtt_base_topic musi byc niepusty i nie moze zaczynac/konczyc sie '/'.")
    if any(char in base for char in ("#", "+", " ")):
        raise ValueError("mqtt_base_topic nie moze zawierac spacji ani wildcardow MQTT (#, +).")
    if len(base) > 64:
        raise ValueError("mqtt_base_topic moze miec maksymalnie 64 znaki.")

    screen = data.get("screen", {})
    brightness = screen.get("brightness", 110)
    timeout = screen.get("sleep_after_seconds", 90)
    sound = screen.get("sound_feedback", True)
    if not isinstance(brightness, int) or not 1 <= brightness <= 255:
        raise ValueError("screen.brightness musi byc liczba 1..255.")
    if not isinstance(timeout, int) or not 0 <= timeout <= 86400:
        raise ValueError("screen.sleep_after_seconds musi byc liczba 0..86400.")
    if not isinstance(sound, bool):
        raise ValueError("screen.sound_feedback musi byc true albo false.")

    rooms = data.get("rooms")
    if not isinstance(rooms, list) or not rooms:
        raise ValueError("Lista rooms nie moze byc pusta.")
    if len(rooms) > 24:
        raise ValueError("Maksymalna liczba pomieszczen to 24.")

    seen_entities: set[str] = set()
    total_devices = 0
    for room_index, room in enumerate(rooms):
        if not isinstance(room, dict):
            raise ValueError(f"rooms[{room_index}] musi byc obiektem.")
        room_name = room.get("name")
        if not isinstance(room_name, str) or not room_name.strip():
            raise ValueError(f"rooms[{room_index}].name nie moze byc puste.")
        if len(room_name) > 24:
            raise ValueError(f"Nazwa pomieszczenia '{room_name}' jest za dluga (maks. 24 znaki).")

        devices = room.get("devices")
        if not isinstance(devices, list) or not devices:
            raise ValueError(f"Pomieszczenie '{room_name}' musi miec co najmniej jedno urzadzenie.")

        climate_roles_in_room: set[str] = set()
        for device_index, device in enumerate(devices):
            if not isinstance(device, dict):
                raise ValueError(f"Urzadzenie {room_name}[{device_index}] musi byc obiektem.")
            name = device.get("name")
            entity_id = device.get("entity_id")
            device_type = device.get("type")
            if not isinstance(name, str) or not name.strip():
                raise ValueError(f"Urzadzenie {room_name}[{device_index}] nie ma nazwy.")
            if len(name) > 32:
                raise ValueError(f"Nazwa urzadzenia '{name}' jest za dluga (maks. 32 znaki).")
            if device_type not in TYPE_MAP:
                raise ValueError(
                    f"Nieobslugiwany typ '{device_type}'. Dozwolone: {', '.join(TYPE_MAP)}."
                )
            if not isinstance(entity_id, str) or not ENTITY_RE.fullmatch(entity_id):
                raise ValueError(f"Nieprawidlowe entity_id: {entity_id!r}.")
            if len(entity_id) > 80:
                raise ValueError(f"entity_id '{entity_id}' jest za dlugie (maks. 80 znakow).")
            if entity_id.split(".", 1)[0] != device_type:
                raise ValueError(
                    f"Typ '{device_type}' nie pasuje do domeny encji '{entity_id}'."
                )
            if entity_id in seen_entities:
                raise ValueError(f"Encja '{entity_id}' wystepuje wiecej niz raz.")

            climate_role = device.get("climate_role")
            if climate_role is not None:
                if climate_role not in CLIMATE_ROLES:
                    raise ValueError(
                        f"Nieprawidlowe climate_role '{climate_role}' dla encji '{entity_id}'."
                    )
                if device_type != "sensor":
                    raise ValueError(
                        f"climate_role mozna przypisac tylko do typu sensor: '{entity_id}'."
                    )
                if climate_role in climate_roles_in_room:
                    raise ValueError(
                        f"Pomieszczenie '{room_name}' ma wiecej niz jeden czujnik "
                        f"climate_role='{climate_role}'."
                    )
                climate_roles_in_room.add(climate_role)

            seen_entities.add(entity_id)
            total_devices += 1

    if total_devices > 64:
        raise ValueError("Maksymalna liczba urzadzen to 64.")

    return data


def render_header(data: dict) -> str:
    devices: list[tuple[str, str, str, int]] = []
    rooms: list[tuple[str, int, int, int | None, int | None]] = []
    first = 0
    for room_index, room in enumerate(data["rooms"]):
        room_devices = room["devices"]
        temperature_device: int | None = None
        humidity_device: int | None = None
        for local_index, device in enumerate(room_devices):
            global_index = first + local_index
            if device.get("climate_role") == "temperature":
                temperature_device = global_index
            elif device.get("climate_role") == "humidity":
                humidity_device = global_index
            devices.append(
                (device["name"], device["entity_id"], TYPE_MAP[device["type"]], room_index)
            )
        rooms.append(
            (
                room["name"],
                first,
                len(room_devices),
                temperature_device,
                humidity_device,
            )
        )
        first += len(room_devices)

    screen = data.get("screen", {})
    brightness = screen.get("brightness", 110)
    timeout_ms = screen.get("sleep_after_seconds", 90) * 1000
    sound = "true" if screen.get("sound_feedback", True) else "false"

    device_lines = "\n".join(
        f"    {{{cpp_string(name)}, {cpp_string(entity)}, DeviceType::{kind}, {room_index}}},"
        for name, entity, kind, room_index in devices
    )
    room_lines = "\n".join(
        "    {"
        f"{cpp_string(name)}, {first_device}, {count}, "
        f"{temperature if temperature is not None else 'NO_DEVICE'}, "
        f"{humidity if humidity is not None else 'NO_DEVICE'}"
        "},"
        for name, first_device, count, temperature, humidity in rooms
    )

    return f"""#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

enum class DeviceType : uint8_t {{
    Light,
    Switch,
    Cover,
    Scene,
    Script,
    Button,
    Sensor,
    BinarySensor,
}};

struct DeviceConfig {{
    const char* name;
    const char* entityId;
    DeviceType type;
    uint8_t roomIndex;
}};

struct RoomConfig {{
    const char* name;
    uint8_t firstDevice;
    uint8_t deviceCount;
    uint8_t temperatureDevice;
    uint8_t humidityDevice;
}};

namespace AppConfig {{

inline constexpr char APP_VERSION[] = {cpp_string(data["app_version"])};
inline constexpr char PROJECT_URL[] = {cpp_string(data["project_url"])};
inline constexpr char MQTT_BASE_TOPIC[] = {cpp_string(data["mqtt_base_topic"])};
inline constexpr uint8_t SCREEN_BRIGHTNESS = {brightness};
inline constexpr uint32_t SCREEN_SLEEP_MS = {timeout_ms}UL;
inline constexpr bool SOUND_FEEDBACK = {sound};
inline constexpr uint8_t NO_DEVICE = 0xFF;

inline constexpr DeviceConfig DEVICES[] = {{
{device_lines}
}};

inline constexpr RoomConfig ROOMS[] = {{
{room_lines}
}};

inline constexpr size_t DEVICE_COUNT = sizeof(DEVICES) / sizeof(DEVICES[0]);
inline constexpr size_t ROOM_COUNT = sizeof(ROOMS) / sizeof(ROOMS[0]);

static_assert(DEVICE_COUNT > 0 && DEVICE_COUNT <= 64, "Invalid device count");
static_assert(ROOM_COUNT > 0 && ROOM_COUNT <= 24, "Invalid room count");

}}
"""


def render_home_assistant(data: dict) -> str:
    entities = [
        device["entity_id"]
        for room in data["rooms"]
        for device in room["devices"]
    ]
    base = data["mqtt_base_topic"]
    entity_lines = "\n".join(f"          - {yaml_single_quote(entity)}" for entity in entities)
    trigger_entity_lines = "\n".join(
        f"          - {yaml_single_quote(entity)}" for entity in entities
    )
    jinja_entity_lines = ",\n".join(
        f"            {yaml_single_quote(entity)}" for entity in entities
    )

    return f"""automation:
  - id: cardputer_remote_handle_command
    alias: "Cardputer - wykonaj polecenie"
    mode: queued
    max: 10
    trigger:
      - platform: mqtt
        topic: "{base}/command"
    variables:
      entity_id: >-
        {{{{ trigger.payload_json.entity_id
            if trigger.payload_json is defined and trigger.payload_json.entity_id is defined
            else '' }}}}
      command: >-
        {{{{ trigger.payload_json.command
            if trigger.payload_json is defined and trigger.payload_json.command is defined
            else '' }}}}
    condition:
      - condition: template
        value_template: >-
          {{{{ entity_id in [
{jinja_entity_lines}
          ] }}}}
    action:
      - choose:
          - conditions: "{{{{ command == 'turn_on' and entity_id.startswith('light.') }}}}"
            sequence:
              - service: light.turn_on
                target:
                  entity_id: "{{{{ entity_id }}}}"
          - conditions: "{{{{ command == 'turn_off' and entity_id.startswith('light.') }}}}"
            sequence:
              - service: light.turn_off
                target:
                  entity_id: "{{{{ entity_id }}}}"
          - conditions: "{{{{ command == 'turn_on' and entity_id.startswith('switch.') }}}}"
            sequence:
              - service: switch.turn_on
                target:
                  entity_id: "{{{{ entity_id }}}}"
          - conditions: "{{{{ command == 'turn_off' and entity_id.startswith('switch.') }}}}"
            sequence:
              - service: switch.turn_off
                target:
                  entity_id: "{{{{ entity_id }}}}"
          - conditions: "{{{{ command == 'open' and entity_id.startswith('cover.') }}}}"
            sequence:
              - service: cover.open_cover
                target:
                  entity_id: "{{{{ entity_id }}}}"
          - conditions: "{{{{ command == 'stop' and entity_id.startswith('cover.') }}}}"
            sequence:
              - service: cover.stop_cover
                target:
                  entity_id: "{{{{ entity_id }}}}"
          - conditions: "{{{{ command == 'close' and entity_id.startswith('cover.') }}}}"
            sequence:
              - service: cover.close_cover
                target:
                  entity_id: "{{{{ entity_id }}}}"
          - conditions: "{{{{ command == 'activate' and entity_id.startswith('scene.') }}}}"
            sequence:
              - service: scene.turn_on
                target:
                  entity_id: "{{{{ entity_id }}}}"
          - conditions: "{{{{ command == 'activate' and entity_id.startswith('script.') }}}}"
            sequence:
              - service: script.turn_on
                target:
                  entity_id: "{{{{ entity_id }}}}"
          - conditions: "{{{{ command == 'press' and entity_id.startswith('button.') }}}}"
            sequence:
              - service: button.press
                target:
                  entity_id: "{{{{ entity_id }}}}"

  - id: cardputer_remote_publish_changes
    alias: "Cardputer - publikuj zmiany stanow"
    mode: queued
    max: 20
    trigger:
      - platform: state
        entity_id:
{trigger_entity_lines}
    action:
      - service: mqtt.publish
        data:
          topic: "{base}/state/{{{{ trigger.entity_id }}}}"
          qos: 1
          retain: true
          payload: >-
            {{{{ {{
              'state': trigger.to_state.state if trigger.to_state is not none else 'unavailable',
              'name': trigger.to_state.attributes.get('friendly_name', trigger.entity_id) if trigger.to_state is not none else trigger.entity_id,
              'brightness': trigger.to_state.attributes.get('brightness') if trigger.to_state is not none else none,
              'position': trigger.to_state.attributes.get('current_position') if trigger.to_state is not none else none,
              'unit': trigger.to_state.attributes.get('unit_of_measurement') if trigger.to_state is not none else none
            }} | to_json }}}}

  - id: cardputer_remote_publish_snapshot
    alias: "Cardputer - publikuj pelny stan"
    mode: restart
    trigger:
      - platform: homeassistant
        event: start
      - platform: mqtt
        topic: "{base}/request_state"
    variables:
      entities:
{entity_lines}
    action:
      - repeat:
          for_each: "{{{{ entities }}}}"
          sequence:
            - service: mqtt.publish
              data:
                topic: "{base}/state/{{{{ repeat.item }}}}"
                qos: 1
                retain: true
                payload: >-
                  {{{{ {{
                    'state': states(repeat.item),
                    'name': state_attr(repeat.item, 'friendly_name') or repeat.item,
                    'brightness': state_attr(repeat.item, 'brightness'),
                    'position': state_attr(repeat.item, 'current_position'),
                    'unit': state_attr(repeat.item, 'unit_of_measurement')
                  }} | to_json }}}}
"""


def expected_outputs(project_dir: Path) -> dict[Path, str]:
    data = load_and_validate(project_dir)
    return {
        project_dir / "include" / "generated_config.h": render_header(data),
        project_dir / "home_assistant" / "cardputer_remote.yaml": render_home_assistant(data),
    }


def generate(project_dir: Path, check_only: bool = False) -> bool:
    outputs = expected_outputs(project_dir)
    stale: list[Path] = []
    for path, content in outputs.items():
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if current == content:
            continue
        stale.append(path)
        if not check_only:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8", newline="\n")

    if stale:
        verb = "Nieaktualne" if check_only else "Wygenerowane"
        print(f"{verb}: " + ", ".join(str(path.relative_to(project_dir)) for path in stale))
    return not stale


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="Sprawdz, ale nie zapisuj plikow.")
    parser.add_argument(
        "--project-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    args = parser.parse_args()
    try:
        up_to_date = generate(args.project_dir.resolve(), check_only=args.check)
    except ValueError as exc:
        print(f"BLAD: {exc}", file=sys.stderr)
        return 2
    return 0 if (up_to_date or not args.check) else 1


if __name__ == "__main__":
    raise SystemExit(main())
