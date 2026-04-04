#include <Arduino.h>
#line 1 "E:\\SmartHome_uno\\SmartHome_Complete_FreeRTOS\\SmartHome_Complete_FreeRTOS.ino"
#include <Arduino_FreeRTOS.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>
#include <DHT.h>

#define USE_LCD 0

#if USE_LCD
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#endif

/*
  Smart home model for Arduino Uno R3 using FreeRTOS

  Pin map
  D2  -> Button for inner door (INPUT_PULLUP)
  D3  -> Inner door servo
  D4  -> Alarm buzzer
  D5  -> Outer door servo 1
  D6  -> Outer door servo 2
  D7  -> Light sensor DO (laser beam detector)
  D8  -> Laser enable pin (optional, can power laser from 5V directly instead)
  D9  -> RC522 RST
  D10 -> RC522 SDA/SS
  D11 -> RC522 MOSI
  D12 -> RC522 MISO
  D13 -> RC522 SCK
  A0  -> Smoke sensor AO
  A1  -> PIR analog output AO
  A2  -> DHT data pin
  A4  -> LCD1602 I2C SDA
  A5  -> LCD1602 I2C SCL

  Required libraries
  - Arduino_FreeRTOS_Library
  - MFRC522
  - LiquidCrystal_I2C
  - DHT sensor library
  - Servo

  Hardware notes
  - RC522 must use 3.3V.
  - All servos should use an external 5V supply.
  - All grounds must be connected together.
  - If your laser module draws too much current, power it through a transistor
    or from 5V directly and set USE_LASER_CONTROL_PIN to false.
*/

// --------------------------
// Pin definitions
// --------------------------
const uint8_t PIN_BUTTON_INNER = 2;
const uint8_t PIN_SERVO_INNER = 3;
const uint8_t PIN_BUZZER = 4;
const uint8_t PIN_SERVO_OUTER_1 = 5;
const uint8_t PIN_SERVO_OUTER_2 = 6;
const uint8_t PIN_LIGHT_DO = 7;
const uint8_t PIN_LASER_ENABLE = 8;
const uint8_t PIN_RFID_RST = 9;
const uint8_t PIN_RFID_SS = 10;
const uint8_t PIN_SMOKE_AO = A0;
const uint8_t PIN_PIR_AO = A1;
const uint8_t PIN_DHT = A2;

// --------------------------
// Configuration
// --------------------------
#if USE_LCD
const uint8_t LCD_I2C_ADDRESS = 0x27;  // Change to 0x3F if needed
#endif
#define DHT_TYPE DHT11                  // Change to DHT22 if you use DHT22/AM2302

const bool USE_LASER_CONTROL_PIN = true;
const bool MOVE_SERVOS_ON_BOOT = false;
const uint8_t LIGHT_SENSOR_BROKEN_LEVEL = HIGH;
const bool BUZZER_ACTIVE_HIGH = true;

const uint16_t SMOKE_THRESHOLD = 450;   // Tune this value for your smoke sensor
const uint16_t PIR_THRESHOLD = 400;     // Tune this value for your PIR analog output
const uint8_t SENSOR_CONFIRM_COUNT = 3;

const uint8_t OUTER_SERVO_1_CLOSED_ANGLE = 5;
const uint8_t OUTER_SERVO_1_OPEN_ANGLE = 95;
const uint8_t OUTER_SERVO_2_CLOSED_ANGLE = 170;
const uint8_t OUTER_SERVO_2_OPEN_ANGLE = 80;
const uint16_t OUTER_SERVO_MOVE_MS = 900;
const uint16_t OUTER_DOOR_HOLD_MS = 5000;

const uint8_t INNER_SERVO_CLOSED_ANGLE = 0;
const uint8_t INNER_SERVO_OPEN_ANGLE = 90;
const uint16_t INNER_SERVO_MOVE_MS = 700;

const uint8_t SECURITY_GRACE_TICKS = 80;   // 80 x 100ms = 8s
const uint8_t RFID_FEEDBACK_TICKS = 40;    // 40 x 75ms = 3s

// Add more cards here if you want to authorize multiple RFID tags.
struct KnownCard {
  uint8_t size;
  uint8_t uid[7];
};

const KnownCard AUTHORIZED_CARDS[] = {
  {4, {0xFB, 0x5E, 0xF5, 0x04, 0x00, 0x00, 0x00}}
};

const uint8_t AUTHORIZED_CARD_COUNT =
    sizeof(AUTHORIZED_CARDS) / sizeof(AUTHORIZED_CARDS[0]);

enum DoorState : uint8_t {
  DOOR_CLOSED = 0,
  DOOR_OPENING,
  DOOR_OPEN,
  DOOR_CLOSING
};

enum RfidFeedback : uint8_t {
  RFID_IDLE = 0,
  RFID_GRANTED,
  RFID_DENIED
};

MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Servo outerServo1;
Servo outerServo2;
Servo innerServo;
#if USE_LCD
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 16, 2);
#endif
DHT dht(PIN_DHT, DHT_TYPE);

volatile uint8_t gOuterDoorState = DOOR_CLOSED;
volatile uint8_t gInnerDoorState = DOOR_CLOSED;
volatile uint8_t gRfidFeedback = RFID_IDLE;
volatile uint8_t gRfidFeedbackCounter = 0;
volatile uint8_t gSecurityGraceCounter = 0;

volatile uint8_t gLaserBroken = 0;
volatile uint8_t gSmokeDetected = 0;
volatile uint8_t gPirDetected = 0;
volatile uint8_t gIntrusionAlarm = 0;
volatile uint8_t gFireAlarm = 0;

volatile uint8_t gDhtValid = 0;
volatile int8_t gTemperatureC = 0;
volatile uint8_t gHumidityPct = 0;

void TaskDoorControl(void *pvParameters);
void TaskSafety(void *pvParameters);
void TaskDisplayAndClimate(void *pvParameters);

bool isAuthorizedUid(const MFRC522::Uid &uid);
void printUidToSerial(const MFRC522::Uid &uid);
void setOuterDoorOpen();
void setOuterDoorClosed();
void setInnerDoorOpen();
void setInnerDoorClosed();
void setBuzzer(bool enabled);
bool isSecurityArmed();
#if USE_LCD
void writeLcdLine(uint8_t row, const char *text);
#endif
char doorStateSymbol(uint8_t state);

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println(F("BOOT: setup started"));

  pinMode(PIN_BUTTON_INNER, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LIGHT_DO, INPUT);

  if (USE_LASER_CONTROL_PIN) {
    pinMode(PIN_LASER_ENABLE, OUTPUT);
    digitalWrite(PIN_LASER_ENABLE, HIGH);
  }

  setBuzzer(false);

  outerServo1.attach(PIN_SERVO_OUTER_1);
  outerServo2.attach(PIN_SERVO_OUTER_2);
  innerServo.attach(PIN_SERVO_INNER);

  if (MOVE_SERVOS_ON_BOOT) {
    Serial.println(F("BOOT: moving servos to initial position"));
    setOuterDoorClosed();
    setInnerDoorClosed();
    delay(1200);
  } else {
    Serial.println(F("BOOT: servo startup movement skipped"));
  }

  Serial.println(F("BOOT: starting SPI/RFID"));
  SPI.begin();
  rfid.PCD_Init();

#if USE_LCD
  Serial.println(F("BOOT: starting LCD"));
  lcd.init();
  lcd.backlight();
  writeLcdLine(0, "SmartHome RTOS");
  writeLcdLine(1, "Starting...");
#endif

  Serial.println(F("BOOT: starting DHT"));
  dht.begin();

  Serial.println(F("Smart home system ready."));
  Serial.println(F("Scan RFID card to print UID."));
  Serial.println(F("Authorized RFID list loaded."));

  xTaskCreate(TaskDoorControl, "Doors", 210, NULL, 3, NULL);
  xTaskCreate(TaskSafety, "Safety", 170, NULL, 2, NULL);
  xTaskCreate(TaskDisplayAndClimate, "Display", 210, NULL, 1, NULL);
}

void loop() {
  // Scheduler is started automatically by Arduino_FreeRTOS.
}

void TaskDoorControl(void *pvParameters) {
  (void) pvParameters;

  TickType_t outerDeadline = 0;
  TickType_t innerDeadline = 0;
  uint8_t lastButtonState = HIGH;

  for (;;) {
    const TickType_t now = xTaskGetTickCount();
    const uint8_t currentButton = digitalRead(PIN_BUTTON_INNER);

    if (gRfidFeedbackCounter > 0) {
      gRfidFeedbackCounter--;
      if (gRfidFeedbackCounter == 0) {
        gRfidFeedback = RFID_IDLE;
      }
    }

    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      Serial.print(F("Card UID: "));
      printUidToSerial(rfid.uid);

      if (isAuthorizedUid(rfid.uid)) {
        Serial.println(F("Access granted"));
        gRfidFeedback = RFID_GRANTED;
        gRfidFeedbackCounter = RFID_FEEDBACK_TICKS;
        gSecurityGraceCounter = SECURITY_GRACE_TICKS;

        if (gOuterDoorState == DOOR_CLOSED || gOuterDoorState == DOOR_CLOSING) {
          setOuterDoorOpen();
          gOuterDoorState = DOOR_OPENING;
          outerDeadline = now + pdMS_TO_TICKS(OUTER_SERVO_MOVE_MS);
        } else if (gOuterDoorState == DOOR_OPEN) {
          outerDeadline = now + pdMS_TO_TICKS(OUTER_DOOR_HOLD_MS);
        }
      } else {
        Serial.println(F("Access denied"));
        gRfidFeedback = RFID_DENIED;
        gRfidFeedbackCounter = RFID_FEEDBACK_TICKS;
      }

      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }

    switch (gOuterDoorState) {
      case DOOR_OPENING:
        if (now >= outerDeadline) {
          gOuterDoorState = DOOR_OPEN;
          outerDeadline = now + pdMS_TO_TICKS(OUTER_DOOR_HOLD_MS);
        }
        break;

      case DOOR_OPEN:
        if (now >= outerDeadline) {
          setOuterDoorClosed();
          gOuterDoorState = DOOR_CLOSING;
          outerDeadline = now + pdMS_TO_TICKS(OUTER_SERVO_MOVE_MS);
        }
        break;

      case DOOR_CLOSING:
        if (now >= outerDeadline) {
          gOuterDoorState = DOOR_CLOSED;
        }
        break;

      default:
        break;
    }

    if (currentButton == LOW && lastButtonState == HIGH) {
      if (gInnerDoorState == DOOR_CLOSED) {
        setInnerDoorOpen();
        gInnerDoorState = DOOR_OPENING;
        innerDeadline = now + pdMS_TO_TICKS(INNER_SERVO_MOVE_MS);
        gSecurityGraceCounter = SECURITY_GRACE_TICKS;
      } else if (gInnerDoorState == DOOR_OPEN) {
        setInnerDoorClosed();
        gInnerDoorState = DOOR_CLOSING;
        innerDeadline = now + pdMS_TO_TICKS(INNER_SERVO_MOVE_MS);
      }
    }
    lastButtonState = currentButton;

    switch (gInnerDoorState) {
      case DOOR_OPENING:
        if (now >= innerDeadline) {
          gInnerDoorState = DOOR_OPEN;
        }
        break;

      case DOOR_CLOSING:
        if (now >= innerDeadline) {
          gInnerDoorState = DOOR_CLOSED;
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

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void TaskDisplayAndClimate(void *pvParameters) {
  (void) pvParameters;

  char line0[17];
  char line1[17];
  char last0[17] = "";
  char last1[17] = "";
  uint8_t displayPage = 0;
  uint8_t pageCounter = 0;
  uint8_t dhtCounter = 0;

  for (;;) {
    if (dhtCounter == 0) {
      const float humidity = dht.readHumidity();
      const float temperature = dht.readTemperature();

      if (!isnan(humidity) && !isnan(temperature)) {
        const int16_t roundedTemp =
            (int16_t) (temperature + (temperature >= 0.0f ? 0.5f : -0.5f));
        const int16_t roundedHumidity = (int16_t) (humidity + 0.5f);

        gTemperatureC = (int8_t) roundedTemp;
        gHumidityPct = (uint8_t) roundedHumidity;
        gDhtValid = 1;
      } else {
        gDhtValid = 0;
      }

      dhtCounter = 8;  // 8 x 250ms = 2 seconds
    } else {
      dhtCounter--;
    }

    if (gFireAlarm) {
      strcpy(line0, "!!! FIRE !!!");
      snprintf(line1, sizeof(line1), "Smoke alarm ON");
    } else if (gIntrusionAlarm) {
      strcpy(line0, "SECURITY ALERT");
      snprintf(line1, sizeof(line1), "P:%u L:%u", gPirDetected, gLaserBroken);
    } else if (gRfidFeedback == RFID_GRANTED) {
      strcpy(line0, "RFID GRANTED");
      snprintf(line1, sizeof(line1), "Outer:%c", doorStateSymbol(gOuterDoorState));
    } else if (gRfidFeedback == RFID_DENIED) {
      strcpy(line0, "RFID DENIED");
      strcpy(line1, "Unknown card");
    } else {
      if (displayPage == 0) {
        if (gDhtValid) {
          snprintf(line0, sizeof(line0), "T:%2dC H:%2u%%", gTemperatureC, gHumidityPct);
        } else {
          strcpy(line0, "T:--C H:--%");
        }
        snprintf(line1, sizeof(line1), "Out:%c In:%c",
                 doorStateSymbol(gOuterDoorState),
                 doorStateSymbol(gInnerDoorState));
      } else {
        snprintf(line0, sizeof(line0), "P:%u Sm:%u L:%u",
                 gPirDetected, gSmokeDetected, gLaserBroken);
        snprintf(line1, sizeof(line1), "Arm:%u Bz:%u",
                 isSecurityArmed(),
                 (gFireAlarm || gIntrusionAlarm) ? 1 : 0);
      }

      pageCounter++;
      if (pageCounter >= 10) {
        pageCounter = 0;
        displayPage ^= 0x01;
      }
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

    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

bool isAuthorizedUid(const MFRC522::Uid &uid) {
  for (uint8_t i = 0; i < AUTHORIZED_CARD_COUNT; i++) {
    if (AUTHORIZED_CARDS[i].size != uid.size) {
      continue;
    }

    bool matched = true;
    for (uint8_t j = 0; j < uid.size; j++) {
      if (AUTHORIZED_CARDS[i].uid[j] != uid.uidByte[j]) {
        matched = false;
        break;
      }
    }

    if (matched) {
      return true;
    }
  }

  return false;
}

void printUidToSerial(const MFRC522::Uid &uid) {
  for (uint8_t i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) {
      Serial.print('0');
    }
    Serial.print(uid.uidByte[i], HEX);
    if (i + 1 < uid.size) {
      Serial.print(' ');
    }
  }
  Serial.println();
}

void setOuterDoorOpen() {
  outerServo1.write(OUTER_SERVO_1_OPEN_ANGLE);
  outerServo2.write(OUTER_SERVO_2_OPEN_ANGLE);
}

void setOuterDoorClosed() {
  outerServo1.write(OUTER_SERVO_1_CLOSED_ANGLE);
  outerServo2.write(OUTER_SERVO_2_CLOSED_ANGLE);
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
          gOuterDoorState == DOOR_CLOSED &&
          gInnerDoorState == DOOR_CLOSED);
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





