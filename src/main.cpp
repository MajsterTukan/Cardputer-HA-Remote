#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "generated_config.h"
#include "secrets.h"


namespace {

constexpr uint16_t COLOR_BG = 0x0861;
constexpr uint16_t COLOR_HEADER = 0x0018;
constexpr uint16_t COLOR_CLIMATE = 0x086B;
constexpr uint16_t COLOR_SELECTED = 0x22AA;
constexpr uint16_t COLOR_FOOTER = 0x1082;
constexpr uint16_t COLOR_WHITE = 0xFFFF;
constexpr uint16_t COLOR_MUTED = 0xAD55;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_YELLOW = 0xFFE0;
constexpr uint16_t COLOR_ORANGE = 0xFD20;
constexpr uint16_t COLOR_RED = 0xF800;
constexpr uint16_t COLOR_CYAN = 0x07FF;
constexpr uint16_t COLOR_GREY = 0x8410;

constexpr uint8_t VISIBLE_ROWS = 4;
constexpr uint8_t HEADER_HEIGHT = 38;
constexpr uint8_t DEVICE_ROW_HEIGHT = 19;
constexpr uint32_t SPLASH_MS = 1'800;
constexpr uint32_t WIFI_RETRY_MS = 10'000;
constexpr uint32_t MQTT_RETRY_MS = 5'000;
constexpr uint32_t HEADER_REFRESH_MS = 5'000;
constexpr uint32_t CACHE_QUIET_MS = 120'000;
constexpr uint32_t CACHE_MAX_DELAY_MS = 900'000;
constexpr uint32_t COMMAND_PENDING_MS = 3'000;
constexpr uint32_t TOAST_MS = 1'600;

struct RuntimeState {
    char state[20] = "unknown";
    char detail[18] = "";
    bool fresh = false;
    bool cacheDirty = false;
    uint32_t pendingUntil = 0;
};

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Preferences preferences;

M5Canvas uiCanvas(&M5Cardputer.Display);

RuntimeState runtimeStates[AppConfig::DEVICE_COUNT];
uint8_t selectedByRoom[AppConfig::ROOM_COUNT] = {};

uint8_t currentRoom = 0;
bool screenAwake = true;
bool uiDirty = true;
bool credentialsReady = false;
bool cacheHasDirtyEntries = false;

uint32_t lastInteractionAt = 0;
uint32_t lastWiFiAttemptAt = 0;
uint32_t lastMqttAttemptAt = 0;
uint32_t lastHeaderRefreshAt = 0;
uint32_t firstCacheDirtyAt = 0;
uint32_t lastCacheChangeAt = 0;
uint32_t toastUntil = 0;
uint32_t commandSequence = 0;

wl_status_t previousWiFiStatus = WL_IDLE_STATUS;
bool previousMqttStatus = false;
int batteryLevel = -1;

char clientId[48] = {};
char commandTopic[128] = {};
char requestTopic[128] = {};
char statusTopic[128] = {};
char statePrefix[128] = {};
char stateWildcard[132] = {};
char toastText[48] = {};

template <size_t N>
void copyText(char (&destination)[N], const char* source) {
    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }
    std::strncpy(destination, source, N - 1);
    destination[N - 1] = '\0';
}

bool containsPlaceholder(const char* value) {
    return value == nullptr || value[0] == '\0' || std::strstr(value, "CHANGE_ME") != nullptr;
}

bool validateCredentials() {
    return !containsPlaceholder(WIFI_SSID) && !containsPlaceholder(WIFI_PASSWORD) &&
           !containsPlaceholder(MQTT_HOST) && !containsPlaceholder(MQTT_USER) &&
           !containsPlaceholder(MQTT_PASSWORD);
}

size_t selectedDeviceIndex() {
    const RoomConfig& room = AppConfig::ROOMS[currentRoom];
    return room.firstDevice + selectedByRoom[currentRoom];
}

bool stateEquals(size_t deviceIndex, const char* expected) {
    return strcasecmp(runtimeStates[deviceIndex].state, expected) == 0;
}

bool keyInWord(const Keyboard_Class::KeysState& keys, char wanted) {
    const int normalizedWanted = std::tolower(static_cast<unsigned char>(wanted));
    for (const char value : keys.word) {
        if (std::tolower(static_cast<unsigned char>(value)) == normalizedWanted) {
            return true;
        }
    }
    return false;
}

String clippedText(const char* input, int maxWidth) {
    String result = input == nullptr ? "" : input;
    if (uiCanvas.textWidth(result) <= maxWidth) {
        return result;
    }
    while (result.length() > 1 && uiCanvas.textWidth(result + "...") > maxWidth) {
        result.remove(result.length() - 1);
    }
    return result + "...";
}

int centeredTextX(const String& text) {
    return std::max(0, (uiCanvas.width() - uiCanvas.textWidth(text)) / 2);
}

void feedback(bool success) {
    if (!AppConfig::SOUND_FEEDBACK) {
        return;
    }
    M5Cardputer.Speaker.tone(success ? 2400 : 520, success ? 28 : 65);
}

void showToast(const char* text, bool success = true) {
    copyText(toastText, text);
    toastUntil = millis() + TOAST_MS;
    uiDirty = true;
    feedback(success);
}

uint16_t colorForState(size_t deviceIndex) {
    const char* state = runtimeStates[deviceIndex].state;
    if (strcasecmp(state, "on") == 0 || strcasecmp(state, "open") == 0 ||
        strcasecmp(state, "home") == 0) {
        return COLOR_GREEN;
    }
    if (strcasecmp(state, "opening") == 0) {
        return COLOR_YELLOW;
    }
    if (strcasecmp(state, "closing") == 0) {
        return COLOR_ORANGE;
    }
    if (strcasecmp(state, "unavailable") == 0 || strcasecmp(state, "unknown") == 0) {
        return COLOR_RED;
    }
    return COLOR_GREY;
}

String formattedState(size_t deviceIndex) {
    const DeviceConfig& device = AppConfig::DEVICES[deviceIndex];
    const RuntimeState& runtime = runtimeStates[deviceIndex];

    if (runtime.pendingUntil != 0 && static_cast<int32_t>(runtime.pendingUntil - millis()) > 0) {
        return "...";
    }

    String value;
    switch (device.type) {
        case DeviceType::Sensor:
            value = runtime.state;
            if (runtime.detail[0] != '\0') {
                value += " ";
                value += runtime.detail;
            }
            break;
        case DeviceType::Scene:
        case DeviceType::Button:
            value = "GOTOWA";
            break;
        default:
            value = runtime.state;
            value.toUpperCase();
            if (runtime.detail[0] != '\0') {
                value += " ";
                value += runtime.detail;
            }
            break;
    }

    if (!runtime.fresh) {
        value = "~" + value;
    }
    return value;
}

bool refreshBattery() {
    const int level = M5.Power.getBatteryLevel();
    const int newLevel = (level >= 0 && level <= 100) ? level : -1;
    const bool changed = newLevel != batteryLevel;
    batteryLevel = newLevel;
    return changed;
}

String climateValue(uint8_t deviceIndex, const char* suffix) {
    if (deviceIndex == AppConfig::NO_DEVICE || deviceIndex >= AppConfig::DEVICE_COUNT) {
        return "--";
    }

    const RuntimeState& runtime = runtimeStates[deviceIndex];
    if (runtime.state[0] == '\0' || strcasecmp(runtime.state, "unknown") == 0 ||
        strcasecmp(runtime.state, "unavailable") == 0) {
        return "--";
    }

    String value = runtime.fresh ? "" : "~";
    value += runtime.state;
    value += suffix;
    return value;
}

void drawHeader() {
    auto& display = uiCanvas;
    display.fillRect(0, 0, display.width(), HEADER_HEIGHT, COLOR_HEADER);
    display.setTextSize(1);
    display.setTextColor(COLOR_WHITE, COLOR_HEADER);
    display.setCursor(7, 7);
    display.print(clippedText(AppConfig::ROOMS[currentRoom].name, 105));

    char roomCounter[12];
    std::snprintf(roomCounter, sizeof(roomCounter), "%u/%u", currentRoom + 1,
                  static_cast<unsigned>(AppConfig::ROOM_COUNT));
    display.setTextColor(COLOR_MUTED, COLOR_HEADER);
    display.setCursor(116, 7);
    display.print(roomCounter);

    const bool mqttOnline = mqtt.connected();
    const bool wifiOnline = WiFi.status() == WL_CONNECTED;
    const char* connection = mqttOnline ? "MQ" : (wifiOnline ? "WF" : "--");
    display.setTextColor(mqttOnline ? COLOR_GREEN : (wifiOnline ? COLOR_YELLOW : COLOR_RED),
                         COLOR_HEADER);
    display.setCursor(157, 7);
    display.print(connection);

    display.setTextColor(COLOR_WHITE, COLOR_HEADER);
    display.setCursor(202, 7);
    if (batteryLevel >= 0) {
        display.printf("%d%%", batteryLevel);
    } else {
        display.print("BAT?");
    }

    const RoomConfig& room = AppConfig::ROOMS[currentRoom];
    String climate = "TEMP ";
    climate += climateValue(room.temperatureDevice, " C");
    climate += "   |   HUMI ";
    climate += climateValue(room.humidityDevice, " %");

    display.fillRect(0, 22, display.width(), HEADER_HEIGHT - 22, COLOR_CLIMATE);
    display.drawFastHLine(0, 22, display.width(), COLOR_CYAN);
    display.setTextColor(COLOR_WHITE, COLOR_CLIMATE);
    display.setCursor(centeredTextX(climate), 27);
    display.print(climate);
}

void drawFooter(size_t selectedIndex) {
    auto& display = uiCanvas;
    constexpr int footerY = 116;
    display.fillRect(0, footerY, display.width(), display.height() - footerY, COLOR_FOOTER);
    display.setTextSize(1);

    if (toastUntil != 0 && static_cast<int32_t>(toastUntil - millis()) > 0) {
        display.setTextColor(COLOR_CYAN, COLOR_FOOTER);
        display.setCursor(6, 122);
        display.print(clippedText(toastText, 228));
        return;
    }

    display.setTextColor(COLOR_MUTED, COLOR_FOOTER);
    display.setCursor(5, 122);
    switch (AppConfig::DEVICES[selectedIndex].type) {
        case DeviceType::Cover:
            display.print("1 UP   2 STOP   3 DOWN");
            break;
        case DeviceType::Sensor:
        case DeviceType::BinarySensor:
            display.print("A/D ROOM   W/S CHOICE");
            break;
        default:
            display.print("A/D ROOM  W/S  CHOICE");
            break;
    }

    const RoomConfig& room = AppConfig::ROOMS[currentRoom];
    const uint8_t pageCount = (room.deviceCount + VISIBLE_ROWS - 1) / VISIBLE_ROWS;
    if (pageCount > 1) {
        const uint8_t page = selectedByRoom[currentRoom] / VISIBLE_ROWS + 1;
        display.fillRect(211, 117, 29, 18, COLOR_FOOTER);
        display.setTextColor(COLOR_CYAN, COLOR_FOOTER);
        display.setCursor(216, 122);
        display.printf("%u/%u", page, pageCount);
    }
}

void drawUi() {
    if (!screenAwake) {
        return;
    }

    auto& display = uiCanvas;
    display.fillScreen(COLOR_BG);
    display.setTextFont(1);
    display.setTextSize(1);
    drawHeader();

    const RoomConfig& room = AppConfig::ROOMS[currentRoom];
    const uint8_t selectedLocal = selectedByRoom[currentRoom];
    const uint8_t firstVisible = (selectedLocal / VISIBLE_ROWS) * VISIBLE_ROWS;

    for (uint8_t row = 0; row < VISIBLE_ROWS; ++row) {
        const uint8_t localIndex = firstVisible + row;
        if (localIndex >= room.deviceCount) {
            break;
        }

        const size_t globalIndex = room.firstDevice + localIndex;
        const int y = HEADER_HEIGHT + 1 + row * DEVICE_ROW_HEIGHT;
        const bool selected = localIndex == selectedLocal;
        const uint16_t rowBackground = selected ? COLOR_SELECTED : COLOR_BG;
        display.fillRect(3, y, display.width() - 6, DEVICE_ROW_HEIGHT - 1, rowBackground);

        const uint16_t dotColor = colorForState(globalIndex);
        display.fillCircle(9, y + 9, 3, dotColor);
        if (!runtimeStates[globalIndex].fresh) {
            display.drawCircle(9, y + 9, 5, COLOR_YELLOW);
        }

        display.setTextColor(selected ? COLOR_WHITE : COLOR_MUTED, rowBackground);
        display.setCursor(17, y + 5);
        display.print(clippedText(AppConfig::DEVICES[globalIndex].name, 124));

        const String value = clippedText(formattedState(globalIndex).c_str(), 80);
        const int valueX = std::max(148, display.width() - 7 - display.textWidth(value));
        display.setTextColor(dotColor, rowBackground);
        display.setCursor(valueX, y + 5);
        display.print(value);
    }

    drawFooter(selectedDeviceIndex());
    display.pushSprite(0, 0);
    uiDirty = false;
}

void drawSplashScreen() {
    auto& display = uiCanvas;
    display.fillScreen(COLOR_BG);

    display.drawRoundRect(5, 5, display.width() - 10, display.height() - 10, 8, COLOR_CYAN);
    display.drawRoundRect(8, 8, display.width() - 16, display.height() - 16, 6,
                          COLOR_HEADER);
    display.fillRoundRect(18, 17, 46, 10, 5, COLOR_SELECTED);
    display.setTextSize(1);
    display.setTextColor(COLOR_WHITE, COLOR_SELECTED);
    display.setCursor(27, 19);
    display.print("SMART");

    display.setTextSize(2);
    display.setTextColor(COLOR_WHITE, COLOR_BG);
    const String title = "HOME ASSISTANT";
    display.setCursor(centeredTextX(title), 35);
    display.print(title);

    display.setTextColor(COLOR_CYAN, COLOR_BG);
    const String subtitle = "REMOTE CONTROL";
    display.setCursor(centeredTextX(subtitle), 57);
    display.print(subtitle);

    const String version = String("v") + AppConfig::APP_VERSION;
    display.setTextSize(1);
    const int versionWidth = display.textWidth(version) + 18;
    const int versionX = (display.width() - versionWidth) / 2;
    display.fillRoundRect(versionX, 84, versionWidth, 17, 8, COLOR_SELECTED);
    display.setTextColor(COLOR_WHITE, COLOR_SELECTED);
    display.setCursor(versionX + 9, 89);
    display.print(version);

    if (AppConfig::PROJECT_URL[0] != '\0') {
        const String projectUrl = AppConfig::PROJECT_URL;
        display.setTextColor(COLOR_MUTED, COLOR_BG);
        display.setCursor(centeredTextX(projectUrl), 113);
        display.print(projectUrl);
    }

    display.pushSprite(0, 0);
}

void drawConfigurationRequired() {
    auto& display = uiCanvas;
    display.fillScreen(COLOR_BG);
    display.setTextFont(1);
    display.setTextSize(1);
    display.setTextColor(COLOR_RED, COLOR_BG);
    display.setCursor(8, 12);
    display.print("NO CONFIGURATION");
    display.setTextColor(COLOR_WHITE, COLOR_BG);
    display.setCursor(8, 36);
    display.print("Edit file:");
    display.setTextColor(COLOR_CYAN, COLOR_BG);
    display.setCursor(8, 51);
    display.print("include/secrets.h");
    display.setTextColor(COLOR_MUTED, COLOR_BG);
    display.setCursor(8, 78);
    display.print("Enter WiFi and MQTT data,");
    display.setCursor(8, 91);
    display.print("then upload again.");
    display.pushSprite(0, 0);
}

void markCacheDirty(size_t index) {
    runtimeStates[index].cacheDirty = true;
    const uint32_t now = millis();
    if (!cacheHasDirtyEntries) {
        firstCacheDirtyAt = now;
    }
    cacheHasDirtyEntries = true;
    lastCacheChangeAt = now;
}

void loadCache() {
    preferences.begin("ha-remote", false);
    for (size_t index = 0; index < AppConfig::DEVICE_COUNT; ++index) {
        char stateKey[8];
        char detailKey[8];
        std::snprintf(stateKey, sizeof(stateKey), "s%02u", static_cast<unsigned>(index));
        std::snprintf(detailKey, sizeof(detailKey), "d%02u", static_cast<unsigned>(index));
        const String cachedState = preferences.getString(stateKey, "unknown");
        const String cachedDetail = preferences.getString(detailKey, "");
        copyText(runtimeStates[index].state, cachedState.c_str());
        copyText(runtimeStates[index].detail, cachedDetail.c_str());
        runtimeStates[index].fresh = false;
    }
}

void saveCacheIfDue() {
    if (!cacheHasDirtyEntries) {
        return;
    }
    const uint32_t now = millis();
    const bool quietLongEnough = now - lastCacheChangeAt >= CACHE_QUIET_MS;
    const bool waitingTooLong = now - firstCacheDirtyAt >= CACHE_MAX_DELAY_MS;
    if (!quietLongEnough && !waitingTooLong) {
        return;
    }

    for (size_t index = 0; index < AppConfig::DEVICE_COUNT; ++index) {
        if (!runtimeStates[index].cacheDirty) {
            continue;
        }
        char stateKey[8];
        char detailKey[8];
        std::snprintf(stateKey, sizeof(stateKey), "s%02u", static_cast<unsigned>(index));
        std::snprintf(detailKey, sizeof(detailKey), "d%02u", static_cast<unsigned>(index));
        preferences.putString(stateKey, runtimeStates[index].state);
        preferences.putString(detailKey, runtimeStates[index].detail);
        runtimeStates[index].cacheDirty = false;
    }
    cacheHasDirtyEntries = false;
}

void updateStateFromMqtt(size_t index, JsonDocument& document) {
    const char* newState = document["state"] | "unknown";
    char newDetail[18] = {};

    const DeviceType type = AppConfig::DEVICES[index].type;
    if (type == DeviceType::Cover && !document["position"].isNull()) {
        const int position = std::clamp(document["position"].as<int>(), 0, 100);
        std::snprintf(newDetail, sizeof(newDetail), "%d%%", position);
    } else if (type == DeviceType::Light && !document["brightness"].isNull()) {
        const int brightness = std::clamp(document["brightness"].as<int>(), 0, 255);
        std::snprintf(newDetail, sizeof(newDetail), "%d%%", (brightness * 100 + 127) / 255);
    } else if (type == DeviceType::Sensor) {
        copyText(newDetail, document["unit"] | "");
    }

    RuntimeState& runtime = runtimeStates[index];
    const bool changed = std::strcmp(runtime.state, newState) != 0 ||
                         std::strcmp(runtime.detail, newDetail) != 0;
    copyText(runtime.state, newState);
    copyText(runtime.detail, newDetail);
    runtime.fresh = true;
    runtime.pendingUntil = 0;
    if (changed) {
        markCacheDirty(index);
    }
    uiDirty = true;
}

void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
    const size_t prefixLength = std::strlen(statePrefix);
    if (std::strncmp(topic, statePrefix, prefixLength) != 0) {
        return;
    }
    const char* entityId = topic + prefixLength;

    size_t index = AppConfig::DEVICE_COUNT;
    for (size_t candidate = 0; candidate < AppConfig::DEVICE_COUNT; ++candidate) {
        if (std::strcmp(AppConfig::DEVICES[candidate].entityId, entityId) == 0) {
            index = candidate;
            break;
        }
    }
    if (index == AppConfig::DEVICE_COUNT) {
        return;
    }

    JsonDocument document;
    const DeserializationError error = deserializeJson(document, payload, length);
    if (error) {
        Serial.printf("MQTT JSON error for %s: %s\n", entityId, error.c_str());
        return;
    }
    updateStateFromMqtt(index, document);
}

void startWiFi() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DEVICE_NAME);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWiFiAttemptAt = millis();
}

void maintainWiFi() {
    const wl_status_t currentStatus = WiFi.status();
    if (currentStatus != previousWiFiStatus) {
        previousWiFiStatus = currentStatus;
        uiDirty = true;
    }
    if (currentStatus == WL_CONNECTED) {
        return;
    }
    const uint32_t now = millis();
    if (now - lastWiFiAttemptAt >= WIFI_RETRY_MS) {
        lastWiFiAttemptAt = now;
        WiFi.reconnect();
    }
}

void maintainMqtt() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    if (mqtt.connected()) {
        mqtt.loop();
        return;
    }

    const uint32_t now = millis();
    if (now - lastMqttAttemptAt < MQTT_RETRY_MS) {
        return;
    }
    lastMqttAttemptAt = now;

    const bool connected = mqtt.connect(clientId, MQTT_USER, MQTT_PASSWORD, statusTopic, 1, true,
                                        "offline");
    if (!connected) {
        Serial.printf("MQTT connect failed, state=%d\n", mqtt.state());
        return;
    }

    mqtt.subscribe(stateWildcard, 1);
    mqtt.publish(statusTopic, "online", true);
    mqtt.publish(requestTopic, clientId, false);
    Serial.println("MQTT connected");
    uiDirty = true;
}

void sendCommand(size_t deviceIndex, const char* command) {
    if (!mqtt.connected()) {
        showToast("BRAK POLACZENIA MQTT", false);
        return;
    }

    JsonDocument document;
    document["entity_id"] = AppConfig::DEVICES[deviceIndex].entityId;
    document["command"] = command;
    document["source"] = clientId;
    document["sequence"] = ++commandSequence;

    char payload[256];
    const size_t length = serializeJson(document, payload, sizeof(payload));
    if (length == 0 || !mqtt.publish(commandTopic, payload, false)) {
        showToast("NIE UDALO SIE WYSLAC", false);
        return;
    }

    runtimeStates[deviceIndex].pendingUntil = millis() + COMMAND_PENDING_MS;
    char message[48];
    std::snprintf(message, sizeof(message), "SENT: %s", command);
    showToast(message, true);
}

void runPrimaryAction() {
    const size_t index = selectedDeviceIndex();
    switch (AppConfig::DEVICES[index].type) {
        case DeviceType::Light:
        case DeviceType::Switch:
            sendCommand(index, stateEquals(index, "on") ? "turn_off" : "turn_on");
            break;
        case DeviceType::Cover:
            sendCommand(index, "stop");
            break;
        case DeviceType::Scene:
        case DeviceType::Script:
            sendCommand(index, "activate");
            break;
        case DeviceType::Button:
            sendCommand(index, "press");
            break;
        case DeviceType::Sensor:
        case DeviceType::BinarySensor:
            showToast("READ ONLY ENTITY", false);
            break;
    }
}

void changeRoom(int delta) {
    const int count = static_cast<int>(AppConfig::ROOM_COUNT);
    currentRoom = static_cast<uint8_t>((static_cast<int>(currentRoom) + delta + count) % count);
    uiDirty = true;
}

void changeSelection(int delta) {
    const uint8_t count = AppConfig::ROOMS[currentRoom].deviceCount;
    int next = static_cast<int>(selectedByRoom[currentRoom]) + delta;
    next = (next + count) % count;
    selectedByRoom[currentRoom] = static_cast<uint8_t>(next);
    uiDirty = true;
}

void wakeScreen() {
    screenAwake = true;
    M5Cardputer.Display.setBrightness(AppConfig::SCREEN_BRIGHTNESS);
    uiDirty = true;
}

void handleKeyboard() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return;
    }

    const Keyboard_Class::KeysState keys = M5Cardputer.Keyboard.keysState();
    lastInteractionAt = millis();
    if (!screenAwake) {
        wakeScreen();
        return;
    }

    if (keys.left || keyInWord(keys, 'a')) {
        changeRoom(-1);
        return;
    }
    if (keys.right || keyInWord(keys, 'd')) {
        changeRoom(1);
        return;
    }
    if (keys.up || keyInWord(keys, 'w')) {
        changeSelection(-1);
        return;
    }
    if (keys.down || keyInWord(keys, 's')) {
        changeSelection(1);
        return;
    }

    const size_t index = selectedDeviceIndex();
    if (AppConfig::DEVICES[index].type == DeviceType::Cover) {
        if (keyInWord(keys, '1')) {
            sendCommand(index, "open");
            return;
        }
        if (keyInWord(keys, '2')) {
            sendCommand(index, "stop");
            return;
        }
        if (keyInWord(keys, '3')) {
            sendCommand(index, "close");
            return;
        }
    }

    if (keys.enter) {
        runPrimaryAction();
        return;
    }
    if (keyInWord(keys, 'r')) {
        if (mqtt.connected()) {
            mqtt.publish(requestTopic, clientId, false);
            showToast("REFRESHING STATES");
        } else {
            showToast("NO MQTT CONNECTION", false);
        }
    }
}

void maintainScreen() {
    const uint32_t now = millis();
    if (screenAwake && AppConfig::SCREEN_SLEEP_MS > 0 &&
        now - lastInteractionAt >= AppConfig::SCREEN_SLEEP_MS) {
        M5Cardputer.Display.setBrightness(0);
        screenAwake = false;
    }

    if (toastUntil != 0 && static_cast<int32_t>(now - toastUntil) >= 0) {
        toastUntil = 0;
        uiDirty = true;
    }

    bool pendingExpired = false;
    for (RuntimeState& runtime : runtimeStates) {
        if (runtime.pendingUntil != 0 && static_cast<int32_t>(now - runtime.pendingUntil) >= 0) {
            runtime.pendingUntil = 0;
            pendingExpired = true;
        }
    }
    uiDirty = uiDirty || pendingExpired;
}

void prepareTopicsAndClientId() {
    const uint64_t chipId = ESP.getEfuseMac();
    std::snprintf(clientId, sizeof(clientId), "%s-%04X", DEVICE_NAME,
                  static_cast<unsigned>(chipId & 0xFFFF));
    std::snprintf(commandTopic, sizeof(commandTopic), "%s/command", AppConfig::MQTT_BASE_TOPIC);
    std::snprintf(requestTopic, sizeof(requestTopic), "%s/request_state",
                  AppConfig::MQTT_BASE_TOPIC);
    std::snprintf(statusTopic, sizeof(statusTopic), "%s/status", AppConfig::MQTT_BASE_TOPIC);
    std::snprintf(statePrefix, sizeof(statePrefix), "%s/state/", AppConfig::MQTT_BASE_TOPIC);
    std::snprintf(stateWildcard, sizeof(stateWildcard), "%s+", statePrefix);
}

}

void setup() {
    Serial.begin(115200);

    auto config = M5.config();
    M5Cardputer.begin(config, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(AppConfig::SCREEN_BRIGHTNESS);
    M5Cardputer.Display.setTextWrap(false);
    M5Cardputer.Speaker.setVolume(48);

    uiCanvas.setPsram(false);
    uiCanvas.setColorDepth(16);
    if (uiCanvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height()) ==
        nullptr) {
        M5Cardputer.Display.fillScreen(COLOR_BG);
        M5Cardputer.Display.setTextColor(COLOR_RED, COLOR_BG);
        M5Cardputer.Display.setCursor(8, 20);
        M5Cardputer.Display.print("UI BUFFER MEM ERR");
        while (true) {
            delay(1'000);
        }
    }

    drawSplashScreen();
    delay(SPLASH_MS);

    prepareTopicsAndClientId();
    loadCache();
    refreshBattery();
    lastInteractionAt = millis();

    credentialsReady = validateCredentials();
    if (!credentialsReady) {
        drawConfigurationRequired();
        return;
    }

    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(768);
    mqtt.setKeepAlive(20);
    mqtt.setSocketTimeout(1);

    startWiFi();
    drawUi();
}

void loop() {
    M5Cardputer.update();
    handleKeyboard();
    maintainScreen();

    if (credentialsReady) {
        maintainWiFi();
        maintainMqtt();
        saveCacheIfDue();

        const bool mqttStatus = mqtt.connected();
        if (mqttStatus != previousMqttStatus) {
            if (!mqttStatus) {
                for (RuntimeState& runtime : runtimeStates) {
                    runtime.fresh = false;
                }
            }
            previousMqttStatus = mqttStatus;
            uiDirty = true;
        }
    }

    const uint32_t now = millis();
    if (now - lastHeaderRefreshAt >= HEADER_REFRESH_MS) {
        lastHeaderRefreshAt = now;
        uiDirty = refreshBattery() || uiDirty;
    }

    if (uiDirty && screenAwake) {
        if (credentialsReady) {
            drawUi();
        } else {
            drawConfigurationRequired();
            uiDirty = false;
        }
    }
    delay(3);
}
