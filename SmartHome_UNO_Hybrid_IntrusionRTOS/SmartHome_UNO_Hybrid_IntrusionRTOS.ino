#include <Arduino.h>
#include <Arduino_FreeRTOS.h>
#include <queue.h>
#include <semphr.h>

#define ENABLE_SERIAL 1
#define ENABLE_LCD 1

#if ENABLE_LCD
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#endif

/*
  Hybrid Uno sketch:
  - Only the intrusion module uses FreeRTOS primitives (2 tasks + 1 queue + 1 mutex).
  - All other functions run in loop() with non-blocking millis()-based logic.
  - The button on D2 now arms/disarms the anti-theft system.

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

constexpr uint16_t INTRUSION_SAMPLE_MS = 50;
constexpr uint16_t INTRUSION_MANAGER_MS = 50;
constexpr uint16_t LASER_STABILIZE_MS = 500;
constexpr uint16_t INTRUSION_HOLD_MS = 5000;
constexpr bool ANTI_THEFT_START_ENABLED = true;

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

void taskIntrusionSensor(void *pvParameters);
void taskIntrusionManager(void *pvParameters);

bool analogTriggered(uint16_t reading, uint16_t threshold, bool triggerWhenAbove) {
  return triggerWhenAbove ? (reading >= threshold) : (reading <= threshold);
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
  static uint16_t smokeRaw = 0;
  static uint16_t fireRaw = 0;
  static bool smokeActive = false;
  static bool fireActive = false;
  static bool antiTheftEnabled = ANTI_THEFT_START_ENABLED;

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
  const uint8_t buttonReading = digitalRead(Pin::ANTI_THEFT_BUTTON);
  const IntrusionState intrusion = readIntrusionState();

  if (buttonReading != lastButtonReading) {
    lastButtonReading = buttonReading;
    lastButtonChangeMs = nowMs;
  }

  if ((nowMs - lastButtonChangeMs) >= BUTTON_DEBOUNCE_MS && buttonReading != stableButtonState) {
    stableButtonState = buttonReading;

    if (stableButtonState == LOW) {
      antiTheftEnabled = !antiTheftEnabled;
      gAntiTheftEnabled = antiTheftEnabled;
    }
  }

  if ((nowMs - lastSensorSampleMs) >= LOOP_SENSOR_MS) {
    lastSensorSampleMs = nowMs;
    smokeRaw = analogRead(Pin::SMOKE_SENSOR_AO);
    fireRaw = analogRead(Pin::FIRE_SENSOR_AO);
    smokeActive = analogTriggered(smokeRaw, SMOKE_THRESHOLD, SMOKE_TRIGGER_WHEN_ABOVE);
    fireActive = analogTriggered(fireRaw, FIRE_THRESHOLD, FIRE_TRIGGER_WHEN_ABOVE);
  }

  AlarmMode alarmMode = ALARM_MODE_NONE;
  if (fireActive) {
    alarmMode = ALARM_MODE_FIRE;
  } else if (smokeActive) {
    alarmMode = ALARM_MODE_SMOKE;
  } else if (intrusion.alarmActive) {
    alarmMode = ALARM_MODE_INTRUSION;
  }
  updateBuzzer(alarmMode, nowMs);

#if ENABLE_SERIAL
  const bool stateChanged =
    intrusion.enabled != previousAntiTheftEnabled ||
    intrusion.beamArmed != previousBeamArmed ||
    intrusion.beamBroken != previousBeamBroken ||
    intrusion.alarmActive != previousIntrusionAlarm ||
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
    Serial.print(intrusion.alarmActive ? F("ON") : F("OFF"));
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
    previousIntrusionAlarm = intrusion.alarmActive;
    previousSmokeActive = smokeActive;
    previousFireActive = fireActive;
    lastSerialPrintMs = nowMs;
  }
#endif

#if ENABLE_LCD
  if ((nowMs - lastLcdUpdateMs) >= LCD_UPDATE_MS) {
    lastLcdUpdateMs = nowMs;

    gLcd.setCursor(0, 0);
    if (fireActive) {
      gLcd.print(F("ALERT: FIRE    "));
    } else if (smokeActive) {
      gLcd.print(F("ALERT: SMOKE   "));
    } else if (intrusion.alarmActive) {
      gLcd.print(F("ALERT: INTRUDER"));
    } else if (!intrusion.enabled) {
      gLcd.print(F("SECURITY: OFF  "));
    } else if (!intrusion.beamArmed) {
      gLcd.print(F("LASER ARMING   "));
    } else {
      gLcd.print(F("SECURITY: ON   "));
    }

    gLcd.setCursor(0, 1);
    gLcd.print(F("S:"));
    gLcd.print(smokeRaw);
    gLcd.print(F(" F:"));
    gLcd.print(fireRaw);
    gLcd.print(F("   "));
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
