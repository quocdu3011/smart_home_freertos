#include <Arduino.h>

/*
  Laser + Light Sensor anti-intrusion alarm

  Default pin map for Arduino Uno R3
  D4 -> buzzer
  D7 -> light sensor DO
  D8 -> laser enable (optional)

  How it works
  - The laser points at the light sensor
  - If the beam is broken, the buzzer alarm is triggered
  - The alarm stays active for a short hold time even if the beam restores
*/

const uint8_t PIN_BUZZER = 4;
const uint8_t PIN_LIGHT_DO = 7;
const uint8_t PIN_LASER_ENABLE = 8;

const bool USE_LASER_CONTROL_PIN = true;
const bool BUZZER_ACTIVE_HIGH = true;
const uint8_t LIGHT_SENSOR_BROKEN_LEVEL = HIGH;

const uint8_t SENSOR_CONFIRM_COUNT = 3;
const uint16_t SENSOR_SAMPLE_MS = 20;
const uint16_t ALARM_HOLD_MS = 10000;
const uint16_t ALARM_BEEP_INTERVAL_MS = 150;

uint8_t gBrokenCounter = 0;
bool gAlarmActive = false;
unsigned long gAlarmUntilMs = 0;
unsigned long gLastSampleMs = 0;
unsigned long gLastBeepToggleMs = 0;
bool gBuzzerState = false;

void setBuzzer(bool enabled);

void setup() {
  Serial.begin(115200);
  delay(800);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LIGHT_DO, INPUT);

  if (USE_LASER_CONTROL_PIN) {
    pinMode(PIN_LASER_ENABLE, OUTPUT);
    digitalWrite(PIN_LASER_ENABLE, HIGH);
  }

  setBuzzer(false);

  Serial.println(F("Laser anti-intrusion alarm ready."));
  Serial.println(F("Beam broken -> alarm active."));
}

void loop() {
  const unsigned long now = millis();

  if (now - gLastSampleMs >= SENSOR_SAMPLE_MS) {
    gLastSampleMs = now;

    const uint8_t lightRaw = digitalRead(PIN_LIGHT_DO);
    const bool beamBrokenNow = (lightRaw == LIGHT_SENSOR_BROKEN_LEVEL);

    if (beamBrokenNow) {
      if (gBrokenCounter < SENSOR_CONFIRM_COUNT) {
        gBrokenCounter++;
      }
    } else if (gBrokenCounter > 0) {
      gBrokenCounter--;
    }

    if (gBrokenCounter >= SENSOR_CONFIRM_COUNT) {
      gAlarmActive = true;
      gAlarmUntilMs = now + ALARM_HOLD_MS;
    } else if (gAlarmActive && now >= gAlarmUntilMs) {
      gAlarmActive = false;
      gBuzzerState = false;
      setBuzzer(false);
      Serial.println(F("Alarm cleared."));
    }
  }

  if (gAlarmActive) {
    if (now - gLastBeepToggleMs >= ALARM_BEEP_INTERVAL_MS) {
      gLastBeepToggleMs = now;
      gBuzzerState = !gBuzzerState;
      setBuzzer(gBuzzerState);
    }
  } else if (gBuzzerState) {
    gBuzzerState = false;
    setBuzzer(false);
  }

  static bool lastAlarmState = false;
  if (gAlarmActive && !lastAlarmState) {
    Serial.println(F("ALERT: Intrusion detected. Beam broken."));
  }
  lastAlarmState = gAlarmActive;
}

void setBuzzer(bool enabled) {
  const uint8_t level = enabled
      ? (BUZZER_ACTIVE_HIGH ? HIGH : LOW)
      : (BUZZER_ACTIVE_HIGH ? LOW : HIGH);
  digitalWrite(PIN_BUZZER, level);
}
