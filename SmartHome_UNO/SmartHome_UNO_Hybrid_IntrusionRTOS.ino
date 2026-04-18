#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <string.h>

#define ENABLE_SERIAL 1
#define ENABLE_DEBUG_SERIAL_OUTPUT 0
#define ENABLE_LCD 1

#if ENABLE_LCD
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#endif

/*
  Hybrid Uno sketch:
  - Only the intrusion module uses FreeRTOS primitives (2 tasks + 1 queue + 1 mutex).
  - All other functions run in loop() with non-blocking millis()-based logic.
  - The button on D2 arms/disarms the anti-theft system.
  - The hardware Serial port is used as a UART link from ESP32 to Uno.
  - A valid RFID scan on ESP32 temporarily suspends anti-theft for 10 seconds.

  Important for Arduino_FreeRTOS on AVR:
  loop() is executed by the idle hook, so it must stay non-blocking.
*/

namespace Pin {
constexpr uint8_t ANTI_THEFT_BUTTON = 2;
constexpr uint8_t BUZZER = 4;
constexpr uint8_t LIGHT_SENSOR_DO = 7;
constexpr uint8_t LASER_ENABLE = 8;
constexpr uint8_t SMOKE_SENSOR_AO = A0;
constexpr uint8_t FIRE_SENSOR_AO = A1;
}

#ifndef MS_TO_TICKS
#define MS_TO_TICKS(ms) ((TickType_t) (((ms) + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS))
#endif

constexpr uint32_t SERIAL_BAUD_RATE = 9600;

constexpr uint16_t BUTTON_DEBOUNCE_MS = 50;
constexpr uint16_t LOOP_SENSOR_MS = 100;
constexpr uint16_t SERIAL_HEARTBEAT_MS = 1000;
constexpr uint16_t LCD_UPDATE_MS = 500;
constexpr uint16_t LCD_PAGE_MS = 2000;

constexpr uint16_t INTRUSION_SAMPLE_MS = 50;
constexpr uint16_t INTRUSION_MANAGER_MS = 50;
constexpr uint16_t LASER_STABILIZE_MS = 500;
constexpr uint16_t INTRUSION_HOLD_MS = 5000;
constexpr uint16_t ANTI_THEFT_SUSPEND_MS = 10000;
constexpr bool ANTI_THEFT_START_ENABLED = true;
constexpr uint16_t ESP32_LINK_TIMEOUT_MS = 5000;
constexpr size_t ESP32_RX_LINE_SIZE = 80;
constexpr size_t ESP32_UID_SIZE = 24;

// Most LM393 light sensor modules drive DO LOW when the sensor is illuminated.
// If your module behaves the opposite way, change LOW to HIGH.
constexpr uint8_t LASER_BEAM_PRESENT_LEVEL = LOW;

constexpr bool SMOKE_TRIGGER_WHEN_ABOVE = true;
constexpr uint16_t SMOKE_THRESHOLD = 1500;
constexpr bool FIRE_TRIGGER_WHEN_ABOVE = false;
constexpr uint16_t FIRE_THRESHOLD = 300;

#if ENABLE_LCD
constexpr uint8_t LCD_I2C_ADDRESS = 0x27;
LiquidCrystal_I2C gLcd(LCD_I2C_ADDRESS, 16, 2);
#endif

struct IntrusionEvent {
  bool beamBroken;
  uint8_t currentLevel;
};

struct IntrusionState {
  bool enabled;
  bool beamArmed;
  bool beamBroken;
  bool alarmActive;
  uint8_t referenceLevel;
  uint8_t currentLevel;
};

struct Esp32State {
  bool wifiConnected;
  bool mqttConnected;
  bool wifiPortalActive;
  bool gateOpen;
  bool entryDoorOpen;
  bool climateValid;
  bool lastAccessGranted;
  int16_t temperatureDeciC;
  uint16_t humidityPctDeci;
  uint8_t authorizedCardCount;
  char lastUid[ESP32_UID_SIZE];
  unsigned long lastStateMs;
  unsigned long lastAccessMs;
};

enum AlarmMode : uint8_t {
  ALARM_MODE_NONE = 0,
  ALARM_MODE_INTRUSION,
  ALARM_MODE_SMOKE,
  ALARM_MODE_FIRE
};

QueueHandle_t gIntrusionQueue = nullptr;
SemaphoreHandle_t gIntrusionMutex = nullptr;
IntrusionState gIntrusionState = {ANTI_THEFT_START_ENABLED, false, false, false, HIGH, HIGH};
volatile bool gAntiTheftEnabled = ANTI_THEFT_START_ENABLED;
Esp32State gEsp32State = {false, false, false, false, false, false, false, -1, 0, 0, "", 0, 0};
unsigned long gAntiTheftSuspendUntilMs = 0;

void taskIntrusionSensor(void *pvParameters);
void taskIntrusionManager(void *pvParameters);

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

bool analogTriggered(uint16_t reading, uint16_t threshold, bool triggerWhenAbove) {
  return triggerWhenAbove ? (reading >= threshold) : (reading <= threshold);
}

bool timeReached(unsigned long nowMs, unsigned long targetMs) {
  return static_cast<long>(nowMs - targetMs) >= 0;
}

bool isEsp32LinkOnline(unsigned long nowMs) {
  const unsigned long lastSeenMs =
    gEsp32State.lastStateMs > gEsp32State.lastAccessMs ? gEsp32State.lastStateMs : gEsp32State.lastAccessMs;

  return lastSeenMs > 0 && (nowMs - lastSeenMs) <= ESP32_LINK_TIMEOUT_MS;
}

bool parseFlagToken(const char *token) {
  return token != nullptr && atoi(token) != 0;
}

void processEsp32StateFrame(char *payload, unsigned long nowMs) {
  char *savePtr = nullptr;
  char *token = strtok_r(payload, ",", &savePtr);
  uint8_t fieldIndex = 0;

  while (token != nullptr) {
    switch (fieldIndex) {
      case 0:
        gEsp32State.wifiConnected = parseFlagToken(token);
        break;
      case 1:
        gEsp32State.mqttConnected = parseFlagToken(token);
        break;
      case 2:
        gEsp32State.wifiPortalActive = parseFlagToken(token);
        break;
      case 3:
        gEsp32State.gateOpen = parseFlagToken(token);
        break;
      case 4:
        gEsp32State.entryDoorOpen = parseFlagToken(token);
        break;
      case 5:
        gEsp32State.climateValid = parseFlagToken(token);
        break;
      case 6:
        gEsp32State.temperatureDeciC = static_cast<int16_t>(atoi(token));
        break;
      case 7:
        gEsp32State.humidityPctDeci = static_cast<uint16_t>(atoi(token));
        break;
      case 8:
        gEsp32State.authorizedCardCount = static_cast<uint8_t>(atoi(token));
        break;
      case 9:
        gEsp32State.lastAccessGranted = parseFlagToken(token);
        break;
    }

    ++fieldIndex;
    token = strtok_r(nullptr, ",", &savePtr);
  }

  if (fieldIndex >= 10) {
    gEsp32State.lastStateMs = nowMs;
  }
}

void processEsp32RfidFrame(char *payload, unsigned long nowMs) {
  char *savePtr = nullptr;
  char *grantedToken = strtok_r(payload, ",", &savePtr);
  char *uidToken = strtok_r(nullptr, ",", &savePtr);

  if (grantedToken == nullptr) {
    return;
  }

  const bool granted = parseFlagToken(grantedToken);
  gEsp32State.lastAccessGranted = granted;
  copyText(gEsp32State.lastUid, sizeof(gEsp32State.lastUid), uidToken == nullptr ? "" : uidToken);
  gEsp32State.lastAccessMs = nowMs;

  if (granted) {
    gAntiTheftSuspendUntilMs = nowMs + ANTI_THEFT_SUSPEND_MS;
  }
}

void processEsp32Serial(unsigned long nowMs) {
#if ENABLE_SERIAL
  static char lineBuffer[ESP32_RX_LINE_SIZE];
  static size_t lineLength = 0;

  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());

    if (incoming == '\r') {
      continue;
    }

    if (incoming == '\n') {
      lineBuffer[lineLength] = '\0';

      if (lineLength > 0) {
        if (strncmp(lineBuffer, "@STATE,", 7) == 0) {
          processEsp32StateFrame(lineBuffer + 7, nowMs);
        } else if (strncmp(lineBuffer, "@RFID,", 6) == 0) {
          processEsp32RfidFrame(lineBuffer + 6, nowMs);
        }
      }

      lineLength = 0;
      continue;
    }

    if (lineLength + 1U < sizeof(lineBuffer)) {
      lineBuffer[lineLength++] = incoming;
    } else {
      lineLength = 0;
    }
  }
#else
  (void) nowMs;
#endif
}

void lcdPrintRow(uint8_t row, const char *text) {
#if ENABLE_LCD
  char padded[17];
  snprintf(padded, sizeof(padded), "%-16s", text == nullptr ? "" : text);
  gLcd.setCursor(0, row);
  gLcd.print(padded);
#else
  (void) row;
  (void) text;
#endif
}

void setIntrusionSensorFields(
  bool enabled,
  bool beamArmed,
  bool beamBroken,
  uint8_t referenceLevel,
  uint8_t currentLevel
) {
  if (xSemaphoreTake(gIntrusionMutex, portMAX_DELAY) == pdTRUE) {
    gIntrusionState.enabled = enabled;
    gIntrusionState.beamArmed = beamArmed;
    gIntrusionState.beamBroken = beamBroken;
    gIntrusionState.referenceLevel = referenceLevel;
    gIntrusionState.currentLevel = currentLevel;
    xSemaphoreGive(gIntrusionMutex);
  }
}

void setIntrusionAlarmField(bool alarmActive) {
  if (xSemaphoreTake(gIntrusionMutex, portMAX_DELAY) == pdTRUE) {
    gIntrusionState.alarmActive = alarmActive;
    xSemaphoreGive(gIntrusionMutex);
  }
}

IntrusionState readIntrusionState() {
  static IntrusionState lastSnapshot = {ANTI_THEFT_START_ENABLED, false, false, false, HIGH, HIGH};

  if (xSemaphoreTake(gIntrusionMutex, 0) == pdTRUE) {
    lastSnapshot = gIntrusionState;
    xSemaphoreGive(gIntrusionMutex);
  }

  return lastSnapshot;
}

void setBuzzerOutput(bool on) {
  digitalWrite(Pin::BUZZER, on ? HIGH : LOW);
}

void updateBuzzer(AlarmMode mode, unsigned long nowMs) {
  static AlarmMode currentMode = ALARM_MODE_NONE;
  static bool outputOn = false;
  static unsigned long phaseStartedMs = 0;

  uint16_t onMs = 0;
  uint16_t offMs = 0;

  switch (mode) {
    case ALARM_MODE_FIRE:
      onMs = 300;
      offMs = 100;
      break;

    case ALARM_MODE_SMOKE:
      onMs = 180;
      offMs = 180;
      break;

    case ALARM_MODE_INTRUSION:
      onMs = 100;
      offMs = 300;
      break;

    case ALARM_MODE_NONE:
      break;
  }

  if (mode != currentMode) {
    currentMode = mode;
    outputOn = false;
    phaseStartedMs = nowMs;
    setBuzzerOutput(false);
  }

  if (mode == ALARM_MODE_NONE) {
    setBuzzerOutput(false);
    return;
  }

  const unsigned long phaseDuration = outputOn ? onMs : offMs;

  if ((nowMs - phaseStartedMs) >= phaseDuration) {
    outputOn = !outputOn;
    phaseStartedMs = nowMs;
    setBuzzerOutput(outputOn);
  }
}

void setup() {
  pinMode(Pin::ANTI_THEFT_BUTTON, INPUT_PULLUP);
  pinMode(Pin::BUZZER, OUTPUT);
  pinMode(Pin::LIGHT_SENSOR_DO, INPUT);
  pinMode(Pin::LASER_ENABLE, OUTPUT);

  digitalWrite(Pin::BUZZER, LOW);
  digitalWrite(Pin::LASER_ENABLE, ANTI_THEFT_START_ENABLED ? HIGH : LOW);

#if ENABLE_LCD
  gLcd.init();
  gLcd.backlight();
  gLcd.clear();
  gLcd.setCursor(0, 0);
  gLcd.print(F("UNO Hybrid"));
  gLcd.setCursor(0, 1);
  gLcd.print(F("Booting..."));
#endif

  gIntrusionQueue = xQueueCreate(1, sizeof(IntrusionEvent));
  gIntrusionMutex = xSemaphoreCreateMutex();

  if (gIntrusionQueue == nullptr || gIntrusionMutex == nullptr) {
    pinMode(LED_BUILTIN, OUTPUT);
    while (true) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(100);
    }
  }

  xTaskCreate(taskIntrusionSensor, "IntrSens", 96, nullptr, 2, nullptr);
  xTaskCreate(taskIntrusionManager, "IntrMgr", 96, nullptr, 2, nullptr);
}

void loop() {
  static bool serialInitialized = false;
  static uint8_t lastButtonReading = HIGH;
  static uint8_t stableButtonState = HIGH;
  static unsigned long lastButtonChangeMs = 0;
  static unsigned long lastSensorSampleMs = 0;
  static unsigned long lastSerialPrintMs = 0;
  static unsigned long lastLcdUpdateMs = 0;
  static unsigned long lastLcdPageMs = 0;
  static uint8_t lcdPage = 0;
  static uint16_t smokeRaw = 0;
  static uint16_t fireRaw = 0;
  static bool smokeActive = false;
  static bool fireActive = false;
  static bool antiTheftRequested = ANTI_THEFT_START_ENABLED;

  static bool previousAntiTheftEnabled = ANTI_THEFT_START_ENABLED;
  static bool previousBeamBroken = false;
  static bool previousIntrusionAlarm = false;
  static bool previousSmokeActive = false;
  static bool previousFireActive = false;
  static bool previousBeamArmed = false;

  if (!serialInitialized) {
#if ENABLE_SERIAL
    Serial.begin(SERIAL_BAUD_RATE);
#endif
    serialInitialized = true;
  }

  const unsigned long nowMs = millis();
  processEsp32Serial(nowMs);

  const uint8_t buttonReading = digitalRead(Pin::ANTI_THEFT_BUTTON);

  if (buttonReading != lastButtonReading) {
    lastButtonReading = buttonReading;
    lastButtonChangeMs = nowMs;
  }

  if ((nowMs - lastButtonChangeMs) >= BUTTON_DEBOUNCE_MS && buttonReading != stableButtonState) {
    stableButtonState = buttonReading;

    if (stableButtonState == LOW) {
      antiTheftRequested = !antiTheftRequested;
    }
  }

  const bool antiTheftSuspendActive =
    antiTheftRequested && !timeReached(nowMs, gAntiTheftSuspendUntilMs);
  const bool antiTheftEnabled = antiTheftRequested && !antiTheftSuspendActive;
  gAntiTheftEnabled = antiTheftEnabled;

  const IntrusionState intrusion = readIntrusionState();

  if ((nowMs - lastSensorSampleMs) >= LOOP_SENSOR_MS) {
    lastSensorSampleMs = nowMs;
    smokeRaw = analogRead(Pin::SMOKE_SENSOR_AO);
    fireRaw = analogRead(Pin::FIRE_SENSOR_AO);
    smokeActive = analogTriggered(smokeRaw, SMOKE_THRESHOLD, SMOKE_TRIGGER_WHEN_ABOVE);
    fireActive = analogTriggered(fireRaw, FIRE_THRESHOLD, FIRE_TRIGGER_WHEN_ABOVE);
  }

  const bool intrusionAlarmActive = antiTheftEnabled && intrusion.alarmActive;

  AlarmMode alarmMode = ALARM_MODE_NONE;
  if (fireActive) {
    alarmMode = ALARM_MODE_FIRE;
  } else if (smokeActive) {
    alarmMode = ALARM_MODE_SMOKE;
  } else if (intrusionAlarmActive) {
    alarmMode = ALARM_MODE_INTRUSION;
  }
  updateBuzzer(alarmMode, nowMs);

#if ENABLE_SERIAL && ENABLE_DEBUG_SERIAL_OUTPUT
  const bool stateChanged =
    intrusion.enabled != previousAntiTheftEnabled ||
    intrusion.beamArmed != previousBeamArmed ||
    intrusion.beamBroken != previousBeamBroken ||
    intrusionAlarmActive != previousIntrusionAlarm ||
    smokeActive != previousSmokeActive ||
    fireActive != previousFireActive;

  const bool heartbeatDue = (nowMs - lastSerialPrintMs) >= SERIAL_HEARTBEAT_MS;

  if ((stateChanged || heartbeatDue) && Serial.availableForWrite() >= 48) {
    Serial.print(F("AntiTheft="));
    Serial.print(intrusion.enabled ? F("ON") : F("OFF"));
    Serial.print(F(" | Beam="));
    if (!intrusion.enabled) {
      Serial.print(F("OFF"));
    } else if (!intrusion.beamArmed) {
      Serial.print(F("ARMING"));
    } else {
      Serial.print(intrusion.beamBroken ? F("CUT") : F("OK"));
    }
    Serial.print(F(" | IntrusionAlarm="));
    Serial.print(intrusionAlarmActive ? F("ON") : F("OFF"));
    Serial.print(F(" | SmokeRaw="));
    Serial.print(smokeRaw);
    Serial.print(F(" | FireRaw="));
    Serial.print(fireRaw);
    Serial.print(F(" | Smoke="));
    Serial.print(smokeActive ? F("ON") : F("OFF"));
    Serial.print(F(" | Fire="));
    Serial.println(fireActive ? F("ON") : F("OFF"));

    previousAntiTheftEnabled = intrusion.enabled;
    previousBeamArmed = intrusion.beamArmed;
    previousBeamBroken = intrusion.beamBroken;
    previousIntrusionAlarm = intrusionAlarmActive;
    previousSmokeActive = smokeActive;
    previousFireActive = fireActive;
    lastSerialPrintMs = nowMs;
  }
#else
  (void) previousAntiTheftEnabled;
  (void) previousBeamBroken;
  (void) previousIntrusionAlarm;
  (void) previousSmokeActive;
  (void) previousFireActive;
  (void) previousBeamArmed;
  (void) lastSerialPrintMs;
#endif

#if ENABLE_LCD
  if ((nowMs - lastLcdPageMs) >= LCD_PAGE_MS) {
    lastLcdPageMs = nowMs;
    lcdPage = static_cast<uint8_t>((lcdPage + 1U) % 3U);
  }

  if ((nowMs - lastLcdUpdateMs) >= LCD_UPDATE_MS) {
    char line0[17];
    char line1[17];
    lastLcdUpdateMs = nowMs;

    if (fireActive) {
      lcdPrintRow(0, "ALERT: FIRE");
      snprintf(line1, sizeof(line1), "S:%u F:%u", smokeRaw, fireRaw);
      lcdPrintRow(1, line1);
      return;
    }

    if (smokeActive) {
      lcdPrintRow(0, "ALERT: SMOKE");
      snprintf(line1, sizeof(line1), "S:%u F:%u", smokeRaw, fireRaw);
      lcdPrintRow(1, line1);
      return;
    }

    if (intrusionAlarmActive) {
      lcdPrintRow(0, "ALERT: INTRUDER");
      snprintf(line1, sizeof(line1), "Beam:%s", intrusion.beamBroken ? "CUT" : "ERR");
      lcdPrintRow(1, line1);
      return;
    }

    const unsigned long suspendRemainingMs =
      antiTheftSuspendActive ? (gAntiTheftSuspendUntilMs - nowMs) : 0;
    const unsigned long suspendRemainingSec =
      antiTheftSuspendActive ? ((suspendRemainingMs + 999UL) / 1000UL) : 0UL;

    if (lcdPage == 0) {
      if (antiTheftSuspendActive) {
        snprintf(line0, sizeof(line0), "AT:HOLD %2lus", suspendRemainingSec);
      } else {
        snprintf(line0, sizeof(line0), "AT:%s AL:%s",
          antiTheftRequested ? "ON" : "OFF",
          intrusionAlarmActive ? "ON" : "OFF");
      }

      const char *beamText = !antiTheftEnabled ? "OFF" : (!intrusion.beamArmed ? "ARM" : (intrusion.beamBroken ? "CUT" : "OK"));
      snprintf(line1, sizeof(line1), "B:%s S:%u F:%u", beamText, smokeActive ? 1U : 0U, fireActive ? 1U : 0U);
    } else if (lcdPage == 1) {
      if (!isEsp32LinkOnline(nowMs)) {
        snprintf(line0, sizeof(line0), "ESP: OFFLINE");
        snprintf(line1, sizeof(line1), "Wait UART...");
      } else {
        snprintf(line0, sizeof(line0), "Wi:%u Mq:%u P:%u",
          gEsp32State.wifiConnected ? 1U : 0U,
          gEsp32State.mqttConnected ? 1U : 0U,
          gEsp32State.wifiPortalActive ? 1U : 0U);
        snprintf(line1, sizeof(line1), "Gt:%u Dr:%u Cd:%02u",
          gEsp32State.gateOpen ? 1U : 0U,
          gEsp32State.entryDoorOpen ? 1U : 0U,
          gEsp32State.authorizedCardCount);
      }
    } else {
      if (!isEsp32LinkOnline(nowMs)) {
        snprintf(line0, sizeof(line0), "ESP data empty");
        snprintf(line1, sizeof(line1), "RF link check");
      } else {
        if (gEsp32State.climateValid) {
          snprintf(line0, sizeof(line0), "T:%d.%d H:%u.%u",
            gEsp32State.temperatureDeciC / 10,
            abs(gEsp32State.temperatureDeciC % 10),
            gEsp32State.humidityPctDeci / 10,
            gEsp32State.humidityPctDeci % 10);
        } else {
          snprintf(line0, sizeof(line0), "T:--.- H:--.-");
        }

        if (gEsp32State.lastUid[0] != '\0') {
          const size_t uidLength = strlen(gEsp32State.lastUid);
          const char *uidTail = gEsp32State.lastUid + (uidLength > 4 ? uidLength - 4 : 0);
          snprintf(line1, sizeof(line1), "RF:%s U:%s",
            gEsp32State.lastAccessGranted ? "OK" : "NO",
            uidTail);
        } else {
          snprintf(line1, sizeof(line1), "RF: --");
        }
      }
    }
    lcdPrintRow(0, line0);
    lcdPrintRow(1, line1);
  }
#endif
}

void taskIntrusionSensor(void *pvParameters) {
  (void) pvParameters;

  const uint8_t referenceLevel = LASER_BEAM_PRESENT_LEVEL;
  bool lastEnabled = false;
  bool lastBeamBroken = false;
  bool beamArmed = false;
  TickType_t armedAtTick = 0;
  TickType_t lastWakeTime = xTaskGetTickCount();

  setIntrusionSensorFields(
    ANTI_THEFT_START_ENABLED,
    false,
    false,
    referenceLevel,
    digitalRead(Pin::LIGHT_SENSOR_DO)
  );

  for (;;) {
    const bool enabled = gAntiTheftEnabled;
    const uint8_t currentLevel = digitalRead(Pin::LIGHT_SENSOR_DO);

    if (!enabled) {
      if (lastEnabled) {
        digitalWrite(Pin::LASER_ENABLE, LOW);
        lastBeamBroken = false;
        beamArmed = false;
      }

      setIntrusionSensorFields(false, false, false, referenceLevel, currentLevel);
      lastEnabled = false;
      vTaskDelayUntil(&lastWakeTime, MS_TO_TICKS(INTRUSION_SAMPLE_MS));
      continue;
    }

    if (!lastEnabled) {
      digitalWrite(Pin::LASER_ENABLE, HIGH);
      armedAtTick = xTaskGetTickCount() + MS_TO_TICKS(LASER_STABILIZE_MS);
      beamArmed = false;
      lastBeamBroken = false;
      setIntrusionSensorFields(true, false, false, referenceLevel, currentLevel);
      lastEnabled = true;
      vTaskDelayUntil(&lastWakeTime, MS_TO_TICKS(INTRUSION_SAMPLE_MS));
      continue;
    }

    if (!beamArmed) {
      const TickType_t nowTick = xTaskGetTickCount();
      if (static_cast<int16_t>(nowTick - armedAtTick) >= 0) {
        beamArmed = true;
        lastBeamBroken = (currentLevel != referenceLevel);

        setIntrusionSensorFields(true, true, lastBeamBroken, referenceLevel, currentLevel);

        if (lastBeamBroken) {
          IntrusionEvent event;
          event.beamBroken = true;
          event.currentLevel = currentLevel;
          xQueueOverwrite(gIntrusionQueue, &event);
        }
      } else {
        setIntrusionSensorFields(true, false, false, referenceLevel, currentLevel);
      }

      vTaskDelayUntil(&lastWakeTime, MS_TO_TICKS(INTRUSION_SAMPLE_MS));
      continue;
    }

    const bool beamBroken = (currentLevel != referenceLevel);

    setIntrusionSensorFields(true, true, beamBroken, referenceLevel, currentLevel);

    if (beamBroken != lastBeamBroken) {
      IntrusionEvent event;
      event.beamBroken = beamBroken;
      event.currentLevel = currentLevel;
      xQueueOverwrite(gIntrusionQueue, &event);
      lastBeamBroken = beamBroken;
    }

    vTaskDelayUntil(&lastWakeTime, MS_TO_TICKS(INTRUSION_SAMPLE_MS));
  }
}

void taskIntrusionManager(void *pvParameters) {
  (void) pvParameters;

  bool beamBroken = false;
  TickType_t holdUntilTick = 0;

  for (;;) {
    if (!gAntiTheftEnabled) {
      beamBroken = false;
      holdUntilTick = 0;
      xQueueReset(gIntrusionQueue);
      setIntrusionAlarmField(false);
      vTaskDelay(MS_TO_TICKS(INTRUSION_MANAGER_MS));
      continue;
    }

    IntrusionEvent event;

    if (xQueueReceive(gIntrusionQueue, &event, MS_TO_TICKS(INTRUSION_MANAGER_MS)) == pdPASS) {
      beamBroken = event.beamBroken;

      if (beamBroken) {
        holdUntilTick = xTaskGetTickCount() + MS_TO_TICKS(INTRUSION_HOLD_MS);
      }
    }

    const TickType_t now = xTaskGetTickCount();
    const bool alarmActive = beamBroken || (static_cast<int16_t>(holdUntilTick - now) > 0);
    setIntrusionAlarmField(alarmActive);
  }
}
