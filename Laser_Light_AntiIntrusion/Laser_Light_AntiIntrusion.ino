#include <Arduino_FreeRTOS.h>

/*
  Laser + Light Sensor anti-intrusion alarm using FreeRTOS

  Default pin map for Arduino Uno R3
  D4 -> buzzer
  D7 -> light sensor DO
  D8 -> laser enable (optional)

  How it works
  - The laser points at the light sensor
  - If the beam is broken, the alarm is triggered
  - The alarm remains active for a short hold time even if the beam restores
  - FreeRTOS splits sensing and alarm output into separate tasks
*/

const uint8_t PIN_BUZZER = 4;
const uint8_t PIN_LIGHT_DO = 7;
const uint8_t PIN_LASER_ENABLE = 8;

const bool USE_LASER_CONTROL_PIN = true;
const bool BUZZER_ACTIVE_HIGH = true;
const uint8_t LIGHT_SENSOR_BROKEN_LEVEL = HIGH;

const uint8_t SENSOR_CONFIRM_COUNT = 3;
const uint16_t SENSOR_SAMPLE_MS = 20;
const uint16_t ALARM_BEEP_INTERVAL_MS = 150;
const uint16_t HEARTBEAT_MS = 1000;
const uint8_t ALARM_HOLD_TICKS = 100;  // 100 x 100ms = 10s

volatile uint8_t gBeamBroken = 0;
volatile uint8_t gAlarmActive = 0;
volatile uint8_t gAlarmHoldCounter = 0;

void TaskBeamMonitor(void *pvParameters);
void TaskAlarmOutput(void *pvParameters);
void TaskHeartbeat(void *pvParameters);
void setBuzzer(bool enabled);

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println(F("Laser anti-intrusion FreeRTOS ready."));
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LIGHT_DO, INPUT);

  if (USE_LASER_CONTROL_PIN) {
    pinMode(PIN_LASER_ENABLE, OUTPUT);
    digitalWrite(PIN_LASER_ENABLE, HIGH);
  }

  setBuzzer(false);

  Serial.println(F("Laser anti-intrusion FreeRTOS ready."));
  Serial.println(F("Beam broken -> alarm active."));

  xTaskCreate(TaskBeamMonitor, "Beam", 160, NULL, 3, NULL);
  xTaskCreate(TaskAlarmOutput, "Alarm", 160, NULL, 2, NULL);
  xTaskCreate(TaskHeartbeat, "Log", 128, NULL, 1, NULL);
}

void loop() {
  // Scheduler is started automatically by Arduino_FreeRTOS.
}

void TaskBeamMonitor(void *pvParameters) {
  (void) pvParameters;

  uint8_t brokenCounter = 0;
  uint8_t lastAlarmState = 0;

  for (;;) {
    const uint8_t lightRaw = digitalRead(PIN_LIGHT_DO);
    const bool beamBrokenNow = (lightRaw == LIGHT_SENSOR_BROKEN_LEVEL);

    if (beamBrokenNow) {
      if (brokenCounter < SENSOR_CONFIRM_COUNT) {
        brokenCounter++;
      }
    } else if (brokenCounter > 0) {
      brokenCounter--;
    }

    gBeamBroken = (brokenCounter >= SENSOR_CONFIRM_COUNT);

    if (gBeamBroken) {
      gAlarmActive = 1;
      gAlarmHoldCounter = ALARM_HOLD_TICKS;
    } else if (gAlarmHoldCounter > 0) {
      gAlarmHoldCounter--;
      gAlarmActive = 1;
    } else {
      gAlarmActive = 0;
    }

    if (gAlarmActive && !lastAlarmState) {
      Serial.println(F("ALERT: Intrusion detected. Beam broken."));
    } else if (!gAlarmActive && lastAlarmState) {
      Serial.println(F("Alarm cleared."));
    }
    lastAlarmState = gAlarmActive;

    vTaskDelay(pdMS_TO_TICKS(SENSOR_SAMPLE_MS));
  }
}

void TaskAlarmOutput(void *pvParameters) {
  (void) pvParameters;

  bool buzzerState = false;

  for (;;) {
    if (gAlarmActive) {
      buzzerState = !buzzerState;
      setBuzzer(buzzerState);
    } else {
      buzzerState = false;
      setBuzzer(false);
    }

    vTaskDelay(pdMS_TO_TICKS(ALARM_BEEP_INTERVAL_MS));
  }
}

void TaskHeartbeat(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    Serial.print(F("Beam:"));
    Serial.print(gBeamBroken);
    Serial.print(F(" Alarm:"));
    Serial.print(gAlarmActive);
    Serial.print(F(" Hold:"));
    Serial.println(gAlarmHoldCounter);

    vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_MS));
  }
}

void setBuzzer(bool enabled) {
  const uint8_t level = enabled
      ? (BUZZER_ACTIVE_HIGH ? HIGH : LOW)
      : (BUZZER_ACTIVE_HIGH ? LOW : HIGH);
  digitalWrite(PIN_BUZZER, level);
}
