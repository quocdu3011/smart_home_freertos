#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

/*
  ESP32 smart-home controller with:
  - RFID access control for the outer gate
  - MQTT control for the gate and the new entry-door servo
  - DHT11 telemetry
  - WiFi captive portal configuration when stored WiFi cannot connect
  - RFID card storage in ESP32 NVS (Preferences)

  Required libraries:
  - MFRC522
  - DHT sensor library
  - ESP32Servo
  - WiFiManager
  - PubSubClient
  - ArduinoJson
*/

namespace Pin {
constexpr uint8_t RFID_SS = 5;
constexpr uint8_t RFID_RST = 27;
constexpr uint8_t RFID_SCK = 18;
constexpr uint8_t RFID_MISO = 19;
constexpr uint8_t RFID_MOSI = 23;
constexpr uint8_t OUTER_SERVO_1 = 25;
constexpr uint8_t OUTER_SERVO_2 = 26;
constexpr uint8_t ENTRY_SERVO = 33;
constexpr uint8_t DHT11 = 16;
constexpr uint8_t BUZZER = 17;
constexpr uint8_t STATUS_LED = 2;
}

constexpr uint8_t OUTER_SERVO_1_CLOSED_ANGLE = 10;
constexpr uint8_t OUTER_SERVO_1_OPEN_ANGLE = 95;
constexpr uint8_t OUTER_SERVO_2_CLOSED_ANGLE = 170;
constexpr uint8_t OUTER_SERVO_2_OPEN_ANGLE = 85;
constexpr uint8_t ENTRY_SERVO_CLOSED_ANGLE = 10;
constexpr uint8_t ENTRY_SERVO_OPEN_ANGLE = 95;

constexpr uint32_t RFID_POLL_INTERVAL_MS = 150;
constexpr uint32_t RFID_REARM_DELAY_MS = 1200;
constexpr uint32_t DHT_READ_INTERVAL_MS = 5000;
constexpr uint32_t GATE_OPEN_HOLD_MS = 5000;
constexpr uint32_t MQTT_LOOP_DELAY_MS = 100;
constexpr uint32_t MQTT_STATE_PUBLISH_INTERVAL_MS = 5000;
constexpr uint8_t DHT_CONSECUTIVE_FAILURE_LIMIT = 3;

constexpr size_t MAX_AUTHORIZED_UIDS = 24;
constexpr size_t UID_TEXT_SIZE = 32;
constexpr size_t EVENT_TYPE_SIZE = 24;
constexpr size_t EVENT_MESSAGE_SIZE = 96;

constexpr char WIFI_AP_NAME[] = "ESP32-SmartHome-Setup";
constexpr char WIFI_AP_PASSWORD[] = "12345678";

constexpr char MQTT_BROKER[] = "broker.hivemq.com";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USERNAME[] = "";
constexpr char MQTT_PASSWORD[] = "";

constexpr char MQTT_TOPIC_BASE[] = "smarthome/esp32-devkit";
constexpr char MQTT_TOPIC_CMD[] = "smarthome/esp32-devkit/cmd";
constexpr char MQTT_TOPIC_STATE[] = "smarthome/esp32-devkit/state";
constexpr char MQTT_TOPIC_RFID_LIST[] = "smarthome/esp32-devkit/rfid/list";
constexpr char MQTT_TOPIC_EVENT[] = "smarthome/esp32-devkit/event";
constexpr char MQTT_TOPIC_AVAILABILITY[] = "smarthome/esp32-devkit/availability";

constexpr uint32_t GATE_EVENT_OPEN = 1UL << 0;
constexpr uint32_t ENTRY_EVENT_OPEN = 1UL << 0;
constexpr uint32_t ENTRY_EVENT_CLOSE = 1UL << 1;
constexpr uint32_t ENTRY_EVENT_TOGGLE = 1UL << 2;
constexpr uint32_t CONNECTIVITY_EVENT_PUBLISH_STATE = 1UL << 0;
constexpr uint32_t CONNECTIVITY_EVENT_PUBLISH_UIDS = 1UL << 1;

enum AlarmType : uint8_t {
  ALARM_INVALID_CARD = 0,
  ALARM_DHT_FAILURE = 1
};

enum AuthListUpdateResult : uint8_t {
  AUTH_LIST_OK = 0,
  AUTH_LIST_INVALID_UID,
  AUTH_LIST_ALREADY_EXISTS,
  AUTH_LIST_NOT_FOUND,
  AUTH_LIST_FULL,
  AUTH_LIST_SAVE_FAILED
};

struct AlarmEvent {
  AlarmType type;
  char uid[UID_TEXT_SIZE];
};

struct DeviceEvent {
  char type[EVENT_TYPE_SIZE];
  char message[EVENT_MESSAGE_SIZE];
  char uid[UID_TEXT_SIZE];
};

struct SharedState {
  float temperatureC;
  float humidityPct;
  uint8_t dhtFailureCount;
  bool climateValid;
  bool gateOpen;
  bool entryDoorOpen;
  bool wifiConnected;
  bool mqttConnected;
  bool wifiPortalActive;
  bool lastAccessGranted;
  size_t authorizedCardCount;
  char ipAddress[20];
  char lastRfidUid[UID_TEXT_SIZE];
  char lastEvent[EVENT_MESSAGE_SIZE];
};

MFRC522 gRfid(Pin::RFID_SS, Pin::RFID_RST);
DHT gDht(Pin::DHT11, DHT11);
Servo gOuterServo1;
Servo gOuterServo2;
Servo gEntryServo;
WiFiClient gWiFiClient;
PubSubClient gMqttClient(gWiFiClient);
WiFiManager gWiFiManager;
Preferences gPreferences;

QueueHandle_t gAlarmQueue = nullptr;
QueueHandle_t gEventQueue = nullptr;
SemaphoreHandle_t gStateMutex = nullptr;
SemaphoreHandle_t gSerialMutex = nullptr;
SemaphoreHandle_t gAuthMutex = nullptr;
TaskHandle_t gGateTaskHandle = nullptr;
TaskHandle_t gEntryDoorTaskHandle = nullptr;
TaskHandle_t gConnectivityTaskHandle = nullptr;

SharedState gState = {NAN, NAN, 0, false, false, false, false, false, false, false, 0, "", "", ""};
char gAuthorizedUids[MAX_AUTHORIZED_UIDS][UID_TEXT_SIZE];
size_t gAuthorizedUidCount = 0;
char gDeviceId[32] = "esp32-devkit";

void taskRfid(void *pvParameters);
void taskGate(void *pvParameters);
void taskEntryDoor(void *pvParameters);
void taskClimate(void *pvParameters);
void taskAlarm(void *pvParameters);
void taskConnectivity(void *pvParameters);

AuthListUpdateResult addAuthorizedUid(const char *uidText);
AuthListUpdateResult removeAuthorizedUid(const char *uidText);
void publishAuthUpdateResult(const char *action, const char *uidText, AuthListUpdateResult result);
void requestStatePublish();
void requestUidListPublish();

void copyText(char *destination, size_t destinationSize, const char *source) {
  if (destination == nullptr || destinationSize == 0) {
    return;
  }

  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }

  strncpy(destination, source, destinationSize - 1);
  destination[destinationSize - 1] = '\0';
}

void logf(const char *format, ...) {
  char buffer[192];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  if (gSerialMutex != nullptr) {
    xSemaphoreTake(gSerialMutex, portMAX_DELAY);
  }
  Serial.println(buffer);
  if (gSerialMutex != nullptr) {
    xSemaphoreGive(gSerialMutex);
  }
}

void enqueueDeviceEvent(const char *type, const char *message, const char *uidText = nullptr) {
  if (gEventQueue == nullptr) {
    return;
  }

  DeviceEvent event;
  copyText(event.type, sizeof(event.type), type);
  copyText(event.message, sizeof(event.message), message);
  copyText(event.uid, sizeof(event.uid), uidText);

  xQueueSend(gEventQueue, &event, 0);
}

void enqueueAlarm(AlarmType type, const char *uidText = nullptr) {
  if (gAlarmQueue == nullptr) {
    return;
  }

  AlarmEvent event;
  event.type = type;
  copyText(event.uid, sizeof(event.uid), uidText);
  xQueueSend(gAlarmQueue, &event, 0);
}

void setGateHardware(bool openGate) {
  gOuterServo1.write(openGate ? OUTER_SERVO_1_OPEN_ANGLE : OUTER_SERVO_1_CLOSED_ANGLE);
  gOuterServo2.write(openGate ? OUTER_SERVO_2_OPEN_ANGLE : OUTER_SERVO_2_CLOSED_ANGLE);
  digitalWrite(Pin::STATUS_LED, openGate ? HIGH : LOW);
}

void setEntryDoorHardware(bool openDoor) {
  gEntryServo.write(openDoor ? ENTRY_SERVO_OPEN_ANGLE : ENTRY_SERVO_CLOSED_ANGLE);
}

template <typename Mutator>
void updateState(Mutator mutator) {
  if (gStateMutex == nullptr) {
    return;
  }

  if (xSemaphoreTake(gStateMutex, portMAX_DELAY) == pdTRUE) {
    mutator(gState);
    xSemaphoreGive(gStateMutex);
  }
}

SharedState readStateSnapshot() {
  SharedState snapshot = gState;

  if (gStateMutex != nullptr && xSemaphoreTake(gStateMutex, portMAX_DELAY) == pdTRUE) {
    snapshot = gState;
    xSemaphoreGive(gStateMutex);
  }

  return snapshot;
}

void setClimateState(float temperatureC, float humidityPct, bool valid, uint8_t failureCount) {
  updateState([&](SharedState &state) {
    state.temperatureC = temperatureC;
    state.humidityPct = humidityPct;
    state.climateValid = valid;
    state.dhtFailureCount = failureCount;
  });
}

void setGateState(bool openGate) {
  updateState([&](SharedState &state) {
    state.gateOpen = openGate;
  });
}

bool isGateOpen() {
  return readStateSnapshot().gateOpen;
}

void setEntryDoorState(bool openDoor) {
  updateState([&](SharedState &state) {
    state.entryDoorOpen = openDoor;
  });
}

bool isEntryDoorOpen() {
  return readStateSnapshot().entryDoorOpen;
}

void setNetworkState(bool wifiConnected, bool mqttConnected, bool portalActive) {
  const String ipText = wifiConnected ? WiFi.localIP().toString() : String("");
  updateState([&](SharedState &state) {
    state.wifiConnected = wifiConnected;
    state.mqttConnected = mqttConnected;
    state.wifiPortalActive = portalActive;
    copyText(state.ipAddress, sizeof(state.ipAddress), ipText.c_str());
  });
}

void setAuthorizedCardCount(size_t count) {
  updateState([&](SharedState &state) {
    state.authorizedCardCount = count;
  });
}

void setLastAccessResult(const char *uidText, bool granted, const char *eventText) {
  updateState([&](SharedState &state) {
    copyText(state.lastRfidUid, sizeof(state.lastRfidUid), uidText);
    state.lastAccessGranted = granted;
    copyText(state.lastEvent, sizeof(state.lastEvent), eventText);
  });
}

void setLastEventText(const char *eventText) {
  updateState([&](SharedState &state) {
    copyText(state.lastEvent, sizeof(state.lastEvent), eventText);
  });
}

void formatUid(const MFRC522::Uid &uid, char *output, size_t outputSize) {
  if (outputSize == 0) {
    return;
  }

  output[0] = '\0';
  size_t position = 0;

  for (byte index = 0; index < uid.size; ++index) {
    const int written = snprintf(
      output + position,
      outputSize - position,
      index == 0 ? "%02X" : " %02X",
      uid.uidByte[index]
    );

    if (written <= 0 || static_cast<size_t>(written) >= (outputSize - position)) {
      break;
    }

    position += static_cast<size_t>(written);
  }
}

int hexNibbleValue(char character) {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'A' && character <= 'F') {
    return 10 + (character - 'A');
  }
  if (character >= 'a' && character <= 'f') {
    return 10 + (character - 'a');
  }
  return -1;
}

bool normalizeUidText(const char *input, char *output, size_t outputSize) {
  if (input == nullptr || output == nullptr || outputSize == 0) {
    return false;
  }

  uint8_t bytes[10];
  size_t byteCount = 0;
  int highNibble = -1;

  for (size_t index = 0; input[index] != '\0'; ++index) {
    const char character = input[index];
    const int nibble = hexNibbleValue(character);

    if (nibble >= 0) {
      if (highNibble < 0) {
        highNibble = nibble;
      } else {
        if (byteCount >= sizeof(bytes)) {
          return false;
        }

        bytes[byteCount++] = static_cast<uint8_t>((highNibble << 4) | nibble);
        highNibble = -1;
      }
      continue;
    }

    if (isspace(static_cast<unsigned char>(character)) != 0 || character == ':' || character == '-') {
      continue;
    }

    return false;
  }

  if (highNibble >= 0 || byteCount < 4 || byteCount > sizeof(bytes)) {
    return false;
  }

  size_t position = 0;
  output[0] = '\0';

  for (size_t index = 0; index < byteCount; ++index) {
    const int written = snprintf(
      output + position,
      outputSize - position,
      index == 0 ? "%02X" : " %02X",
      bytes[index]
    );

    if (written <= 0 || static_cast<size_t>(written) >= (outputSize - position)) {
      return false;
    }

    position += static_cast<size_t>(written);
  }

  return true;
}

bool saveAuthorizedUidsToPrefs() {
  String serialized;

  if (gAuthMutex == nullptr || xSemaphoreTake(gAuthMutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  for (size_t index = 0; index < gAuthorizedUidCount; ++index) {
    if (index > 0) {
      serialized += '\n';
    }
    serialized += gAuthorizedUids[index];
  }

  xSemaphoreGive(gAuthMutex);
  return gPreferences.putString("uids", serialized) > 0 || serialized.length() == 0;
}

void loadAuthorizedUidsFromPrefs() {
  String serialized = gPreferences.getString("uids", "");

  if (gAuthMutex == nullptr || xSemaphoreTake(gAuthMutex, portMAX_DELAY) != pdTRUE) {
    return;
  }

  gAuthorizedUidCount = 0;

  size_t start = 0;
  while (start < serialized.length() && gAuthorizedUidCount < MAX_AUTHORIZED_UIDS) {
    const int separator = serialized.indexOf('\n', start);
    const size_t end = separator >= 0 ? static_cast<size_t>(separator) : serialized.length();
    const String item = serialized.substring(start, end);

    if (item.length() > 0) {
      copyText(gAuthorizedUids[gAuthorizedUidCount], UID_TEXT_SIZE, item.c_str());
      ++gAuthorizedUidCount;
    }

    if (separator < 0) {
      break;
    }

    start = static_cast<size_t>(separator) + 1U;
  }

  const size_t count = gAuthorizedUidCount;
  xSemaphoreGive(gAuthMutex);

  setAuthorizedCardCount(count);
}

bool isAuthorizedUid(const char *uidText) {
  bool found = false;

  if (gAuthMutex != nullptr && xSemaphoreTake(gAuthMutex, portMAX_DELAY) == pdTRUE) {
    for (size_t index = 0; index < gAuthorizedUidCount; ++index) {
      if (strcmp(uidText, gAuthorizedUids[index]) == 0) {
        found = true;
        break;
      }
    }

    xSemaphoreGive(gAuthMutex);
  }

  return found;
}

AuthListUpdateResult addAuthorizedUid(const char *uidText) {
  char normalizedUid[UID_TEXT_SIZE];
  if (!normalizeUidText(uidText, normalizedUid, sizeof(normalizedUid))) {
    return AUTH_LIST_INVALID_UID;
  }

  if (gAuthMutex == nullptr || xSemaphoreTake(gAuthMutex, portMAX_DELAY) != pdTRUE) {
    return AUTH_LIST_SAVE_FAILED;
  }

  for (size_t index = 0; index < gAuthorizedUidCount; ++index) {
    if (strcmp(gAuthorizedUids[index], normalizedUid) == 0) {
      xSemaphoreGive(gAuthMutex);
      return AUTH_LIST_ALREADY_EXISTS;
    }
  }

  if (gAuthorizedUidCount >= MAX_AUTHORIZED_UIDS) {
    xSemaphoreGive(gAuthMutex);
    return AUTH_LIST_FULL;
  }

  copyText(gAuthorizedUids[gAuthorizedUidCount], UID_TEXT_SIZE, normalizedUid);
  ++gAuthorizedUidCount;
  const size_t count = gAuthorizedUidCount;
  xSemaphoreGive(gAuthMutex);

  if (!saveAuthorizedUidsToPrefs()) {
    return AUTH_LIST_SAVE_FAILED;
  }

  setAuthorizedCardCount(count);
  requestUidListPublish();
  requestStatePublish();
  return AUTH_LIST_OK;
}

AuthListUpdateResult removeAuthorizedUid(const char *uidText) {
  char normalizedUid[UID_TEXT_SIZE];
  if (!normalizeUidText(uidText, normalizedUid, sizeof(normalizedUid))) {
    return AUTH_LIST_INVALID_UID;
  }

  if (gAuthMutex == nullptr || xSemaphoreTake(gAuthMutex, portMAX_DELAY) != pdTRUE) {
    return AUTH_LIST_SAVE_FAILED;
  }

  size_t foundIndex = MAX_AUTHORIZED_UIDS;
  for (size_t index = 0; index < gAuthorizedUidCount; ++index) {
    if (strcmp(gAuthorizedUids[index], normalizedUid) == 0) {
      foundIndex = index;
      break;
    }
  }

  if (foundIndex == MAX_AUTHORIZED_UIDS) {
    xSemaphoreGive(gAuthMutex);
    return AUTH_LIST_NOT_FOUND;
  }

  for (size_t index = foundIndex; index + 1U < gAuthorizedUidCount; ++index) {
    copyText(gAuthorizedUids[index], UID_TEXT_SIZE, gAuthorizedUids[index + 1U]);
  }

  if (gAuthorizedUidCount > 0) {
    --gAuthorizedUidCount;
  }

  if (gAuthorizedUidCount < MAX_AUTHORIZED_UIDS) {
    gAuthorizedUids[gAuthorizedUidCount][0] = '\0';
  }

  const size_t count = gAuthorizedUidCount;
  xSemaphoreGive(gAuthMutex);

  if (!saveAuthorizedUidsToPrefs()) {
    return AUTH_LIST_SAVE_FAILED;
  }

  setAuthorizedCardCount(count);
  requestUidListPublish();
  requestStatePublish();
  return AUTH_LIST_OK;
}

void buzz(uint16_t onMs, uint16_t offMs, uint8_t repeatCount) {
  for (uint8_t index = 0; index < repeatCount; ++index) {
    digitalWrite(Pin::BUZZER, HIGH);
    vTaskDelay(pdMS_TO_TICKS(onMs));
    digitalWrite(Pin::BUZZER, LOW);

    if (index + 1U < repeatCount) {
      vTaskDelay(pdMS_TO_TICKS(offMs));
    }
  }
}

void flashStatusLed(uint16_t onMs, uint16_t offMs, uint8_t repeatCount) {
  const bool restoreGateLed = isGateOpen();

  for (uint8_t index = 0; index < repeatCount; ++index) {
    digitalWrite(Pin::STATUS_LED, HIGH);
    vTaskDelay(pdMS_TO_TICKS(onMs));
    digitalWrite(Pin::STATUS_LED, LOW);

    if (index + 1U < repeatCount) {
      vTaskDelay(pdMS_TO_TICKS(offMs));
    }
  }

  digitalWrite(Pin::STATUS_LED, restoreGateLed ? HIGH : LOW);
}

void requestStatePublish() {
  if (gConnectivityTaskHandle != nullptr) {
    xTaskNotify(gConnectivityTaskHandle, CONNECTIVITY_EVENT_PUBLISH_STATE, eSetBits);
  }
}

void requestUidListPublish() {
  if (gConnectivityTaskHandle != nullptr) {
    xTaskNotify(gConnectivityTaskHandle, CONNECTIVITY_EVENT_PUBLISH_UIDS, eSetBits);
  }
}

void onWifiConfigPortalStarted(WiFiManager *manager) {
  (void) manager;
  setNetworkState(false, false, true);
  setLastEventText("wifi_portal_active");
  logf("WiFi config portal started. SSID: %s", WIFI_AP_NAME);
  enqueueDeviceEvent("wifi_portal", "WiFi config portal is active.");
}

void onWifiConnected() {
  setNetworkState(true, false, false);
  setLastEventText("wifi_connected");
  const String ipText = WiFi.localIP().toString();
  logf("WiFi connected. IP: %s", ipText.c_str());
  enqueueDeviceEvent("wifi_connected", "WiFi connected.");
}

bool connectWifiWithPortal() {
  setNetworkState(false, false, false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  gWiFiManager.setAPCallback(onWifiConfigPortalStarted);
  gWiFiManager.setConfigPortalTimeout(0);

  logf("Connecting to WiFi using stored credentials or captive portal.");
  const bool connected = gWiFiManager.autoConnect(WIFI_AP_NAME, WIFI_AP_PASSWORD);

  if (connected) {
    onWifiConnected();
    return true;
  }

  logf("WiFi connection failed.");
  enqueueDeviceEvent("wifi_failed", "WiFi connection failed.");
  return false;
}

bool mqttConnectInternal(const char *clientId) {
  if (strlen(MQTT_USERNAME) > 0) {
    return gMqttClient.connect(
      clientId,
      MQTT_USERNAME,
      MQTT_PASSWORD,
      MQTT_TOPIC_AVAILABILITY,
      1,
      true,
      "offline"
    );
  }

  return gMqttClient.connect(
    clientId,
    MQTT_TOPIC_AVAILABILITY,
    1,
    true,
    "offline"
  );
}

bool publishState() {
  if (!gMqttClient.connected()) {
    return false;
  }

  const SharedState snapshot = readStateSnapshot();
  StaticJsonDocument<512> document;
  document["deviceId"] = gDeviceId;
  document["topicBase"] = MQTT_TOPIC_BASE;
  document["wifiConnected"] = snapshot.wifiConnected;
  document["mqttConnected"] = snapshot.mqttConnected;
  document["wifiPortalActive"] = snapshot.wifiPortalActive;
  document["ip"] = snapshot.ipAddress;
  document["gateOpen"] = snapshot.gateOpen;
  document["entryDoorOpen"] = snapshot.entryDoorOpen;
  document["climateValid"] = snapshot.climateValid;
  document["temperatureC"] = snapshot.temperatureC;
  document["humidityPct"] = snapshot.humidityPct;
  document["dhtFailureCount"] = snapshot.dhtFailureCount;
  document["authorizedCardCount"] = snapshot.authorizedCardCount;
  document["lastRfidUid"] = snapshot.lastRfidUid;
  document["lastAccessGranted"] = snapshot.lastAccessGranted;
  document["lastEvent"] = snapshot.lastEvent;
  document["uptimeMs"] = millis();

  char payload[640];
  const size_t payloadSize = serializeJson(document, payload, sizeof(payload));
  if (payloadSize == 0) {
    return false;
  }

  return gMqttClient.publish(MQTT_TOPIC_STATE, payload, true);
}

bool publishAuthorizedUidList() {
  if (!gMqttClient.connected()) {
    return false;
  }

  DynamicJsonDocument document(1536);
  document["deviceId"] = gDeviceId;
  JsonArray cards = document.createNestedArray("cards");

  if (gAuthMutex != nullptr && xSemaphoreTake(gAuthMutex, portMAX_DELAY) == pdTRUE) {
    for (size_t index = 0; index < gAuthorizedUidCount; ++index) {
      cards.add(gAuthorizedUids[index]);
    }
    xSemaphoreGive(gAuthMutex);
  }

  document["count"] = cards.size();

  char payload[1200];
  const size_t payloadSize = serializeJson(document, payload, sizeof(payload));
  if (payloadSize == 0) {
    return false;
  }

  return gMqttClient.publish(MQTT_TOPIC_RFID_LIST, payload, true);
}

bool publishDeviceEvent(const DeviceEvent &event) {
  if (!gMqttClient.connected()) {
    return false;
  }

  StaticJsonDocument<256> document;
  document["deviceId"] = gDeviceId;
  document["type"] = event.type;
  document["message"] = event.message;
  document["uid"] = event.uid;
  document["uptimeMs"] = millis();

  char payload[320];
  const size_t payloadSize = serializeJson(document, payload, sizeof(payload));
  if (payloadSize == 0) {
    return false;
  }

  return gMqttClient.publish(MQTT_TOPIC_EVENT, payload, false);
}

void publishAuthUpdateResult(const char *action, const char *uidText, AuthListUpdateResult result) {
  const char *message = "Unknown result.";

  switch (result) {
    case AUTH_LIST_OK:
      message = strcmp(action, "add") == 0 ? "RFID card added." : "RFID card removed.";
      break;
    case AUTH_LIST_INVALID_UID:
      message = "Invalid RFID UID format.";
      break;
    case AUTH_LIST_ALREADY_EXISTS:
      message = "RFID card already exists.";
      break;
    case AUTH_LIST_NOT_FOUND:
      message = "RFID card not found.";
      break;
    case AUTH_LIST_FULL:
      message = "RFID list is full.";
      break;
    case AUTH_LIST_SAVE_FAILED:
      message = "Failed to save RFID list.";
      break;
  }

  setLastEventText(message);
  enqueueDeviceEvent(
    strcmp(action, "add") == 0 ? "rfid_add" : "rfid_remove",
    message,
    uidText
  );
  requestStatePublish();
}

void handleMqttCommand(const JsonDocument &document) {
  const char *action = document["action"] | "";

  if (strcmp(action, "gate_open") == 0) {
    xTaskNotify(gGateTaskHandle, GATE_EVENT_OPEN, eSetBits);
    setLastEventText("gate_open_from_app");
    enqueueDeviceEvent("gate_open", "Gate open command received from app.");
    requestStatePublish();
    return;
  }

  if (strcmp(action, "entry_open") == 0) {
    xTaskNotify(gEntryDoorTaskHandle, ENTRY_EVENT_OPEN, eSetBits);
    setLastEventText("entry_open_from_app");
    enqueueDeviceEvent("entry_open", "Entry door open command received from app.");
    requestStatePublish();
    return;
  }

  if (strcmp(action, "entry_close") == 0) {
    xTaskNotify(gEntryDoorTaskHandle, ENTRY_EVENT_CLOSE, eSetBits);
    setLastEventText("entry_close_from_app");
    enqueueDeviceEvent("entry_close", "Entry door close command received from app.");
    requestStatePublish();
    return;
  }

  if (strcmp(action, "entry_toggle") == 0) {
    xTaskNotify(gEntryDoorTaskHandle, ENTRY_EVENT_TOGGLE, eSetBits);
    setLastEventText("entry_toggle_from_app");
    enqueueDeviceEvent("entry_toggle", "Entry door toggle command received from app.");
    requestStatePublish();
    return;
  }

  if (strcmp(action, "rfid_add") == 0) {
    const char *uidText = document["uid"] | "";
    const AuthListUpdateResult result = addAuthorizedUid(uidText);
    publishAuthUpdateResult("add", uidText, result);
    return;
  }

  if (strcmp(action, "rfid_remove") == 0) {
    const char *uidText = document["uid"] | "";
    const AuthListUpdateResult result = removeAuthorizedUid(uidText);
    publishAuthUpdateResult("remove", uidText, result);
    return;
  }

  if (strcmp(action, "rfid_list") == 0) {
    requestUidListPublish();
    return;
  }

  if (strcmp(action, "state_get") == 0) {
    requestStatePublish();
    return;
  }

  if (strcmp(action, "wifi_portal") == 0) {
    WiFi.disconnect(true, true);
    setLastEventText("wifi_portal_requested");
    enqueueDeviceEvent("wifi_portal", "WiFi portal requested from app.");
    return;
  }

  enqueueDeviceEvent("mqtt_unknown_command", "Unknown MQTT command received.");
}

void onMqttMessage(char *topic, byte *payload, unsigned int length) {
  if (strcmp(topic, MQTT_TOPIC_CMD) != 0) {
    return;
  }

  StaticJsonDocument<320> document;
  const DeserializationError error = deserializeJson(document, payload, length);

  if (error) {
    logf("MQTT command JSON parse failed: %s", error.c_str());
    enqueueDeviceEvent("mqtt_parse_error", "MQTT command JSON parse failed.");
    return;
  }

  handleMqttCommand(document);
}

bool connectMqttBroker() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  while (!gMqttClient.connected()) {
    logf("Connecting to MQTT broker %s:%u ...", MQTT_BROKER, MQTT_PORT);

    if (mqttConnectInternal(gDeviceId)) {
      gMqttClient.publish(MQTT_TOPIC_AVAILABILITY, "online", true);
      gMqttClient.subscribe(MQTT_TOPIC_CMD);
      setNetworkState(true, true, false);
      setLastEventText("mqtt_connected");
      enqueueDeviceEvent("mqtt_connected", "MQTT broker connected.");
      requestStatePublish();
      requestUidListPublish();
      return true;
    }

    logf("MQTT connect failed. state=%d", gMqttClient.state());
    setNetworkState(true, false, false);
    vTaskDelay(pdMS_TO_TICKS(3000));

    if (WiFi.status() != WL_CONNECTED) {
      return false;
    }
  }

  return true;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  pinMode(Pin::BUZZER, OUTPUT);
  pinMode(Pin::STATUS_LED, OUTPUT);
  digitalWrite(Pin::BUZZER, LOW);
  digitalWrite(Pin::STATUS_LED, LOW);

  gOuterServo1.setPeriodHertz(50);
  gOuterServo2.setPeriodHertz(50);
  gEntryServo.setPeriodHertz(50);
  gOuterServo1.attach(Pin::OUTER_SERVO_1, 500, 2400);
  gOuterServo2.attach(Pin::OUTER_SERVO_2, 500, 2400);
  gEntryServo.attach(Pin::ENTRY_SERVO, 500, 2400);
  setGateHardware(false);
  setEntryDoorHardware(false);

  SPI.begin(Pin::RFID_SCK, Pin::RFID_MISO, Pin::RFID_MOSI, Pin::RFID_SS);
  gRfid.PCD_Init();
  gDht.begin();

  const uint64_t chipId = ESP.getEfuseMac();
  snprintf(gDeviceId, sizeof(gDeviceId), "esp32-%06llX", static_cast<unsigned long long>(chipId & 0xFFFFFFULL));

  gStateMutex = xSemaphoreCreateMutex();
  gSerialMutex = xSemaphoreCreateMutex();
  gAuthMutex = xSemaphoreCreateMutex();
  gAlarmQueue = xQueueCreate(8, sizeof(AlarmEvent));
  gEventQueue = xQueueCreate(12, sizeof(DeviceEvent));

  if (gStateMutex == nullptr || gSerialMutex == nullptr || gAuthMutex == nullptr ||
      gAlarmQueue == nullptr || gEventQueue == nullptr) {
    Serial.println("FreeRTOS resource allocation failed.");
    while (true) {
      digitalWrite(Pin::STATUS_LED, !digitalRead(Pin::STATUS_LED));
      delay(200);
    }
  }

  gPreferences.begin("smarthome", false);
  loadAuthorizedUidsFromPrefs();

  gMqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  gMqttClient.setCallback(onMqttMessage);
  gMqttClient.setBufferSize(1536);

  logf("ESP32 MQTT smart-home controller booted.");
  logf("MQTT base topic: %s", MQTT_TOPIC_BASE);
  logf("Configured deviceId: %s", gDeviceId);

  xTaskCreatePinnedToCore(taskGate, "GateTask", 4096, nullptr, 3, &gGateTaskHandle, tskNO_AFFINITY);
  xTaskCreatePinnedToCore(taskEntryDoor, "EntryDoorTask", 3072, nullptr, 2, &gEntryDoorTaskHandle, tskNO_AFFINITY);
  xTaskCreatePinnedToCore(taskRfid, "RfidTask", 4096, nullptr, 2, nullptr, tskNO_AFFINITY);
  xTaskCreatePinnedToCore(taskClimate, "ClimateTask", 3072, nullptr, 1, nullptr, tskNO_AFFINITY);
  xTaskCreatePinnedToCore(taskAlarm, "AlarmTask", 3072, nullptr, 2, nullptr, tskNO_AFFINITY);
  xTaskCreatePinnedToCore(taskConnectivity, "ConnectivityTask", 6144, nullptr, 2, &gConnectivityTaskHandle, tskNO_AFFINITY);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

void taskRfid(void *pvParameters) {
  (void) pvParameters;

  TickType_t lastWakeTime = xTaskGetTickCount();

  for (;;) {
    if (gRfid.PICC_IsNewCardPresent() && gRfid.PICC_ReadCardSerial()) {
      char uidText[UID_TEXT_SIZE];
      formatUid(gRfid.uid, uidText, sizeof(uidText));
      logf("RFID detected: %s", uidText);

      if (isAuthorizedUid(uidText)) {
        logf("Authorized card accepted.");
        setLastAccessResult(uidText, true, "rfid_granted");
        enqueueDeviceEvent("rfid_granted", "Authorized RFID card accepted.", uidText);
        xTaskNotify(gGateTaskHandle, GATE_EVENT_OPEN, eSetBits);
      } else {
        logf("Unauthorized card rejected.");
        setLastAccessResult(uidText, false, "rfid_denied");
        enqueueDeviceEvent("rfid_denied", "Unauthorized RFID card rejected.", uidText);
        enqueueAlarm(ALARM_INVALID_CARD, uidText);
      }

      requestStatePublish();

      gRfid.PICC_HaltA();
      gRfid.PCD_StopCrypto1();
      vTaskDelay(pdMS_TO_TICKS(RFID_REARM_DELAY_MS));
    }

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(RFID_POLL_INTERVAL_MS));
  }
}

void taskGate(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    uint32_t notificationValue = 0;
    xTaskNotifyWait(0, 0xFFFFFFFFUL, &notificationValue, portMAX_DELAY);

    if ((notificationValue & GATE_EVENT_OPEN) == 0) {
      continue;
    }

    setGateState(true);
    setGateHardware(true);
    setLastEventText("gate_open");
    requestStatePublish();
    buzz(80, 60, 2);
    logf("Outer gate opened.");

    TickType_t closeDeadline = xTaskGetTickCount() + pdMS_TO_TICKS(GATE_OPEN_HOLD_MS);

    for (;;) {
      uint32_t duringOpenNotification = 0;
      const BaseType_t received = xTaskNotifyWait(
        0,
        0xFFFFFFFFUL,
        &duringOpenNotification,
        pdMS_TO_TICKS(100)
      );

      if (received == pdTRUE && (duringOpenNotification & GATE_EVENT_OPEN) != 0) {
        closeDeadline = xTaskGetTickCount() + pdMS_TO_TICKS(GATE_OPEN_HOLD_MS);
        logf("Gate-open timer extended.");
      }

      if (static_cast<int32_t>(xTaskGetTickCount() - closeDeadline) >= 0) {
        break;
      }
    }

    setGateHardware(false);
    setGateState(false);
    setLastEventText("gate_closed");
    requestStatePublish();
    logf("Outer gate closed.");
  }
}

void taskEntryDoor(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    uint32_t notificationValue = 0;
    xTaskNotifyWait(0, 0xFFFFFFFFUL, &notificationValue, portMAX_DELAY);

    bool targetOpen = isEntryDoorOpen();

    if ((notificationValue & ENTRY_EVENT_OPEN) != 0) {
      targetOpen = true;
    }
    if ((notificationValue & ENTRY_EVENT_CLOSE) != 0) {
      targetOpen = false;
    }
    if ((notificationValue & ENTRY_EVENT_TOGGLE) != 0) {
      targetOpen = !targetOpen;
    }

    setEntryDoorHardware(targetOpen);
    setEntryDoorState(targetOpen);
    setLastEventText(targetOpen ? "entry_door_open" : "entry_door_closed");
    requestStatePublish();
    logf("Entry door %s.", targetOpen ? "opened" : "closed");
  }
}

void taskClimate(void *pvParameters) {
  (void) pvParameters;

  TickType_t lastWakeTime = xTaskGetTickCount();
  uint8_t consecutiveFailures = 0;

  for (;;) {
    const float humidityPct = gDht.readHumidity();
    const float temperatureC = gDht.readTemperature();

    if (isnan(humidityPct) || isnan(temperatureC)) {
      if (consecutiveFailures < 255U) {
        ++consecutiveFailures;
      }

      setClimateState(NAN, NAN, false, consecutiveFailures);
      requestStatePublish();
      logf("DHT11 read failed (%u consecutive failures).", consecutiveFailures);

      if (consecutiveFailures == DHT_CONSECUTIVE_FAILURE_LIMIT) {
        setLastEventText("dht_failure");
        enqueueDeviceEvent("dht_failure", "DHT11 failed repeatedly.");
        enqueueAlarm(ALARM_DHT_FAILURE);
      }
    } else {
      if (consecutiveFailures > 0U) {
        logf("DHT11 recovered.");
      }

      consecutiveFailures = 0;
      setClimateState(temperatureC, humidityPct, true, consecutiveFailures);
      setLastEventText("climate_updated");
      requestStatePublish();
      logf("Climate: %.1f C, %.1f %%RH", temperatureC, humidityPct);
    }

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(DHT_READ_INTERVAL_MS));
  }
}

void taskAlarm(void *pvParameters) {
  (void) pvParameters;

  AlarmEvent event;

  for (;;) {
    if (xQueueReceive(gAlarmQueue, &event, portMAX_DELAY) != pdPASS) {
      continue;
    }

    switch (event.type) {
      case ALARM_INVALID_CARD:
        logf("Alarm: invalid RFID card %s", event.uid);
        flashStatusLed(100, 80, 3);
        buzz(120, 80, 3);
        break;

      case ALARM_DHT_FAILURE:
        logf("Alarm: DHT11 failed repeatedly.");
        flashStatusLed(250, 120, 2);
        buzz(300, 150, 2);
        break;
    }

    digitalWrite(Pin::BUZZER, LOW);
  }
}

void taskConnectivity(void *pvParameters) {
  (void) pvParameters;

  bool pendingStatePublish = true;
  bool pendingUidPublish = true;
  TickType_t lastStatePublishTick = 0;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      setNetworkState(false, false, false);

      if (!connectWifiWithPortal()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }

      pendingStatePublish = true;
      pendingUidPublish = true;
    }

    if (!gMqttClient.connected()) {
      if (!connectMqttBroker()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }

      pendingStatePublish = true;
      pendingUidPublish = true;
    }

    uint32_t notifyValue = 0;
    if (xTaskNotifyWait(0, 0xFFFFFFFFUL, &notifyValue, pdMS_TO_TICKS(MQTT_LOOP_DELAY_MS)) == pdTRUE) {
      if ((notifyValue & CONNECTIVITY_EVENT_PUBLISH_STATE) != 0) {
        pendingStatePublish = true;
      }
      if ((notifyValue & CONNECTIVITY_EVENT_PUBLISH_UIDS) != 0) {
        pendingUidPublish = true;
      }
    }

    if (WiFi.status() != WL_CONNECTED) {
      gMqttClient.disconnect();
      setNetworkState(false, false, false);
      continue;
    }

    if (!gMqttClient.connected()) {
      setNetworkState(true, false, false);
      continue;
    }

    gMqttClient.loop();

    DeviceEvent event;
    for (uint8_t attempt = 0; attempt < 4; ++attempt) {
      if (xQueueReceive(gEventQueue, &event, 0) != pdPASS) {
        break;
      }

      publishDeviceEvent(event);
    }

    const TickType_t now = xTaskGetTickCount();
    const bool periodicStatePublish =
      static_cast<int32_t>(now - lastStatePublishTick) >= static_cast<int32_t>(pdMS_TO_TICKS(MQTT_STATE_PUBLISH_INTERVAL_MS));

    if (pendingStatePublish || periodicStatePublish) {
      if (publishState()) {
        pendingStatePublish = false;
        lastStatePublishTick = now;
      }
    }

    if (pendingUidPublish) {
      if (publishAuthorizedUidList()) {
        pendingUidPublish = false;
      }
    }
  }
}
