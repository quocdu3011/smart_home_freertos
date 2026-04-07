#include <Arduino_FreeRTOS.h>
#include <Servo.h>
#include <string.h>

#define USE_LCD 0

#if USE_LCD
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#endif

/*
  SmartHome Uno Safety Module

  Features
  - Inner door button and servo
  - Laser beam break intrusion detector
  - PIR analog motion detector
  - Smoke analog detector
  - Local buzzer alarm
  - Optional LCD1602 I2C status display

  This module is fully independent from the ESP32 access module.
*/

// --------------------------
// Pin definitions
// --------------------------
const uint8_t PIN_BUTTON_INNER = 2;
const uint8_t PIN_SERVO_INNER = 3;
const uint8_t PIN_BUZZER = 4;
const uint8_t PIN_LIGHT_DO = 7;
const uint8_t PIN_LASER_ENABLE = 8;
const uint8_t PIN_SMOKE_AO = A0;
const uint8_t PIN_PIR_AO = A1;

// --------------------------
// Configuration
// --------------------------
#if USE_LCD
const uint8_t LCD_I2C_ADDRESS = 0x27;
#endif

const bool USE_LASER_CONTROL_PIN = true;
const bool MOVE_SERVO_ON_BOOT = false;
const bool BUZZER_ACTIVE_HIGH = true;

const uint8_t LIGHT_SENSOR_BROKEN_LEVEL = HIGH;
const uint16_t SMOKE_THRESHOLD = 450;
const uint16_t PIR_THRESHOLD = 400;
const uint8_t SENSOR_CONFIRM_COUNT = 3;

const uint8_t INNER_SERVO_CLOSED_ANGLE = 0;
const uint8_t INNER_SERVO_OPEN_ANGLE = 90;
const uint16_t INNER_SERVO_MOVE_MS = 700;

const uint8_t SECURITY_GRACE_TICKS = 80;   // 80 x 100ms = 8s

enum DoorState : uint8_t {
  DOOR_CLOSED = 0,
  DOOR_OPENING,
  DOOR_OPEN,
  DOOR_CLOSING
};

Servo innerServo;
#if USE_LCD
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 16, 2);
#endif

volatile uint8_t gInnerDoorState = DOOR_CLOSED;
volatile uint8_t gSecurityGraceCounter = 0;

volatile uint8_t gLaserBroken = 0;
volatile uint8_t gSmokeDetected = 0;
volatile uint8_t gPirDetected = 0;
volatile uint8_t gIntrusionAlarm = 0;
volatile uint8_t gFireAlarm = 0;

void TaskDoorControl(void *pvParameters);
void TaskSafety(void *pvParameters);
void TaskDisplay(void *pvParameters);

void setInnerDoorOpen();
void setInnerDoorClosed();
void setBuzzer(bool enabled);
bool isSecurityArmed();
char doorStateSymbol(uint8_t state);
#if USE_LCD
void writeLcdLine(uint8_t row, const char *text);
#endif

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println(F("BOOT: Uno safety module starting"));

  pinMode(PIN_BUTTON_INNER, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LIGHT_DO, INPUT);

  if (USE_LASER_CONTROL_PIN) {
    pinMode(PIN_LASER_ENABLE, OUTPUT);
    digitalWrite(PIN_LASER_ENABLE, HIGH);
  }

  setBuzzer(false);

  innerServo.attach(PIN_SERVO_INNER);

  if (MOVE_SERVO_ON_BOOT) {
    Serial.println(F("BOOT: moving inner door to closed position"));
    setInnerDoorClosed();
    delay(INNER_SERVO_MOVE_MS);
  } else {
    Serial.println(F("BOOT: servo startup movement skipped"));
  }

#if USE_LCD
  Serial.println(F("BOOT: starting LCD"));
  lcd.init();
  lcd.backlight();
  writeLcdLine(0, "UNO Safety");
  writeLcdLine(1, "Starting...");
#endif

  Serial.println(F("Uno safety module ready."));

  xTaskCreate(TaskDoorControl, "Doors", 180, NULL, 3, NULL);
  xTaskCreate(TaskSafety, "Safety", 170, NULL, 2, NULL);
  xTaskCreate(TaskDisplay, "Display", 220, NULL, 1, NULL);
}

void loop() {
  // Scheduler is started automatically by Arduino_FreeRTOS.
}

void TaskDoorControl(void *pvParameters) {
  (void) pvParameters;

  TickType_t innerDeadline = 0;
  uint8_t lastButtonState = HIGH;

  for (;;) {
    const TickType_t now = xTaskGetTickCount();
    const uint8_t currentButton = digitalRead(PIN_BUTTON_INNER);

    if (currentButton == LOW && lastButtonState == HIGH) {
      if (gInnerDoorState == DOOR_CLOSED) {
        setInnerDoorOpen();
        gInnerDoorState = DOOR_OPENING;
        innerDeadline = now + pdMS_TO_TICKS(INNER_SERVO_MOVE_MS);
        gSecurityGraceCounter = SECURITY_GRACE_TICKS;
        Serial.println(F("Inner door: opening"));
      } else if (gInnerDoorState == DOOR_OPEN) {
        setInnerDoorClosed();
        gInnerDoorState = DOOR_CLOSING;
        innerDeadline = now + pdMS_TO_TICKS(INNER_SERVO_MOVE_MS);
        Serial.println(F("Inner door: closing"));
      }
    }
    lastButtonState = currentButton;

    switch (gInnerDoorState) {
      case DOOR_OPENING:
        if (now >= innerDeadline) {
          gInnerDoorState = DOOR_OPEN;
          Serial.println(F("Inner door: open"));
        }
        break;

      case DOOR_CLOSING:
        if (now >= innerDeadline) {
          gInnerDoorState = DOOR_CLOSED;
          Serial.println(F("Inner door: closed"));
        }
        break;

      default:
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(75));
  }
}

void TaskSafety(void *pvParameters) {
  (void) pvParameters;

  uint8_t smokeCounter = 0;
  uint8_t pirCounter = 0;
  uint8_t laserCounter = 0;
  uint8_t intrusionBlink = 0;
  uint8_t lastFireAlarm = 0;
  uint8_t lastIntrusionAlarm = 0;

  for (;;) {
    const uint16_t smokeValue = analogRead(PIN_SMOKE_AO);
    const uint16_t pirValue = analogRead(PIN_PIR_AO);
    const uint8_t lightRaw = digitalRead(PIN_LIGHT_DO);

    const bool smokeNow = (smokeValue >= SMOKE_THRESHOLD);
    const bool pirNow = (pirValue >= PIR_THRESHOLD);
    const bool laserBrokenNow = (lightRaw == LIGHT_SENSOR_BROKEN_LEVEL);

    if (smokeNow) {
      if (smokeCounter < SENSOR_CONFIRM_COUNT) {
        smokeCounter++;
      }
    } else if (smokeCounter > 0) {
      smokeCounter--;
    }

    if (pirNow) {
      if (pirCounter < SENSOR_CONFIRM_COUNT) {
        pirCounter++;
      }
    } else if (pirCounter > 0) {
      pirCounter--;
    }

    if (laserBrokenNow) {
      if (laserCounter < SENSOR_CONFIRM_COUNT) {
        laserCounter++;
      }
    } else if (laserCounter > 0) {
      laserCounter--;
    }

    gSmokeDetected = (smokeCounter >= SENSOR_CONFIRM_COUNT);
    gPirDetected = (pirCounter >= SENSOR_CONFIRM_COUNT);
    gLaserBroken = (laserCounter >= SENSOR_CONFIRM_COUNT);

    if (gSecurityGraceCounter > 0) {
      gSecurityGraceCounter--;
    }

    gFireAlarm = gSmokeDetected;
    gIntrusionAlarm = isSecurityArmed() && (gPirDetected || gLaserBroken);

    if (gFireAlarm) {
      setBuzzer(true);
    } else if (gIntrusionAlarm) {
      intrusionBlink ^= 0x01;
      setBuzzer(intrusionBlink != 0);
    } else {
      setBuzzer(false);
      intrusionBlink = 0;
    }

    if (gFireAlarm != lastFireAlarm) {
      Serial.print(F("Fire alarm: "));
      Serial.println(gFireAlarm ? F("ON") : F("OFF"));
      lastFireAlarm = gFireAlarm;
    }

    if (gIntrusionAlarm != lastIntrusionAlarm) {
      Serial.print(F("Intrusion alarm: "));
      Serial.println(gIntrusionAlarm ? F("ON") : F("OFF"));
      lastIntrusionAlarm = gIntrusionAlarm;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void TaskDisplay(void *pvParameters) {
  (void) pvParameters;

  char line0[17];
  char line1[17];
  char last0[17] = "";
  char last1[17] = "";
  uint8_t displayPage = 0;
  uint8_t pageCounter = 0;
  uint8_t serialCounter = 0;

  for (;;) {
    if (gFireAlarm) {
      strcpy(line0, "!!! FIRE !!!");
      strcpy(line1, "Smoke alarm ON");
    } else if (gIntrusionAlarm) {
      strcpy(line0, "SECURITY ALERT");
      snprintf(line1, sizeof(line1), "P:%u L:%u", gPirDetected, gLaserBroken);
    } else if (displayPage == 0) {
      snprintf(line0, sizeof(line0), "Inner:%c Arm:%u",
               doorStateSymbol(gInnerDoorState),
               isSecurityArmed() ? 1 : 0);
      snprintf(line1, sizeof(line1), "Grace:%2us",
               (uint8_t) (gSecurityGraceCounter / 10));
    } else {
      snprintf(line0, sizeof(line0), "P:%u Sm:%u L:%u",
               gPirDetected, gSmokeDetected, gLaserBroken);
      snprintf(line1, sizeof(line1), "Bz:%u Btn:%u",
               (gFireAlarm || gIntrusionAlarm) ? 1 : 0,
               digitalRead(PIN_BUTTON_INNER) == LOW ? 1 : 0);
    }

    if (strcmp(line0, last0) != 0) {
#if USE_LCD
      writeLcdLine(0, line0);
#endif
      strcpy(last0, line0);
    }
    if (strcmp(line1, last1) != 0) {
#if USE_LCD
      writeLcdLine(1, line1);
#endif
      strcpy(last1, line1);
    }

    pageCounter++;
    if (pageCounter >= 10) {
      pageCounter = 0;
      displayPage ^= 0x01;
    }

    serialCounter++;
    if (serialCounter >= 8) {
      serialCounter = 0;
      Serial.print(F("UNO | In:"));
      Serial.print(doorStateSymbol(gInnerDoorState));
      Serial.print(F(" Sm:"));
      Serial.print(gSmokeDetected);
      Serial.print(F(" Pir:"));
      Serial.print(gPirDetected);
      Serial.print(F(" Laser:"));
      Serial.print(gLaserBroken);
      Serial.print(F(" Fire:"));
      Serial.print(gFireAlarm);
      Serial.print(F(" Intr:"));
      Serial.print(gIntrusionAlarm);
      Serial.print(F(" Arm:"));
      Serial.println(isSecurityArmed() ? 1 : 0);
    }

    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void setInnerDoorOpen() {
  innerServo.write(INNER_SERVO_OPEN_ANGLE);
}

void setInnerDoorClosed() {
  innerServo.write(INNER_SERVO_CLOSED_ANGLE);
}

void setBuzzer(bool enabled) {
  const uint8_t level = enabled
      ? (BUZZER_ACTIVE_HIGH ? HIGH : LOW)
      : (BUZZER_ACTIVE_HIGH ? LOW : HIGH);
  digitalWrite(PIN_BUZZER, level);
}

bool isSecurityArmed() {
  return (gSecurityGraceCounter == 0 &&
          gInnerDoorState == DOOR_CLOSED);
}

char doorStateSymbol(uint8_t state) {
  switch (state) {
    case DOOR_OPEN:
      return 'O';
    case DOOR_OPENING:
      return 'o';
    case DOOR_CLOSING:
      return 'c';
    default:
      return 'C';
  }
}

#if USE_LCD
void writeLcdLine(uint8_t row, const char *text) {
  char buffer[17];
  uint8_t i = 0;

  while (i < 16 && text[i] != '\0') {
    buffer[i] = text[i];
    i++;
  }
  while (i < 16) {
    buffer[i++] = ' ';
  }
  buffer[16] = '\0';

  lcd.setCursor(0, row);
  lcd.print(buffer);
}
#endif
