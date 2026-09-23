#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

enum class DeviceType : uint8_t {
    Light,
    Switch,
    Cover,
    Scene,
    Script,
    Button,
    Sensor,
    BinarySensor,
};

struct DeviceConfig {
    const char* name;
    const char* entityId;
    DeviceType type;
    uint8_t roomIndex;
};

struct RoomConfig {
    const char* name;
    uint8_t firstDevice;
    uint8_t deviceCount;
    uint8_t temperatureDevice;
    uint8_t humidityDevice;
};

namespace AppConfig {

inline constexpr char APP_VERSION[] = "0.2.0";
inline constexpr char PROJECT_URL[] = "github.com/MajsterTukan";
inline constexpr char MQTT_BASE_TOPIC[] = "home/cardputer";
inline constexpr uint8_t SCREEN_BRIGHTNESS = 110;
inline constexpr uint32_t SCREEN_SLEEP_MS = 90000UL;
inline constexpr bool SOUND_FEEDBACK = true;
inline constexpr uint8_t NO_DEVICE = 0xFF;

inline constexpr DeviceConfig DEVICES[] = {
    {"Swiatlo glowne", "light.salon_glowne", DeviceType::Light, 0},
    {"LED TV", "light.salon_led_tv", DeviceType::Light, 0},
    {"Roleta taras", "cover.salon_taras", DeviceType::Cover, 0},
    {"Temperatura", "sensor.salon_temperature", DeviceType::Sensor, 0},
    {"Swiatlo glowne", "light.kuchnia_glowne", DeviceType::Light, 1},
    {"Blat", "light.kuchnia_blat", DeviceType::Light, 1},
    {"Roleta", "cover.kuchnia", DeviceType::Cover, 1},
    {"Temperatura", "sensor.kuchnia_temperature", DeviceType::Sensor, 1},
    {"Swiatlo glowne", "light.sypialnia_glowne", DeviceType::Light, 2},
    {"Lampka", "light.sypialnia_lampka", DeviceType::Light, 2},
    {"Roleta", "cover.sypialnia", DeviceType::Cover, 2},
    {"Temperatura", "sensor.sypialnia_temperature", DeviceType::Sensor, 2},
    {"Swiatlo", "light.korytarz", DeviceType::Light, 3},
    {"Obecnosc", "binary_sensor.korytarz_presence", DeviceType::BinarySensor, 3},
    {"Dobranoc", "scene.dobranoc", DeviceType::Scene, 4},
    {"Wszystko OFF", "script.wszystko_off", DeviceType::Script, 4},
};

inline constexpr RoomConfig ROOMS[] = {
    {"Salon", 0, 4, 3, NO_DEVICE},
    {"Kuchnia", 4, 4, 7, NO_DEVICE},
    {"Sypialnia", 8, 4, 11, NO_DEVICE},
    {"Korytarz", 12, 2, NO_DEVICE, NO_DEVICE},
    {"Dom", 14, 2, NO_DEVICE, NO_DEVICE},
};

inline constexpr size_t DEVICE_COUNT = sizeof(DEVICES) / sizeof(DEVICES[0]);
inline constexpr size_t ROOM_COUNT = sizeof(ROOMS) / sizeof(ROOMS[0]);

static_assert(DEVICE_COUNT > 0 && DEVICE_COUNT <= 64, "Invalid device count");
static_assert(ROOM_COUNT > 0 && ROOM_COUNT <= 24, "Invalid room count");

}
