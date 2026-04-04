#include <Arduino.h>
#line 1 "E:\\SmartHome_uno\\SmartHome_Form_Base_FreeRTOS\\SmartHome_Form_Base_FreeRTOS.ino"
#include <Arduino_FreeRTOS.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

/*
  Smart Home - Full Feature FreeRTOS Sketch
  Phat trien tu form mau FreeRTOS ma ban cung cap.

  ========================
  So do chan de xuat
  ========================
  D2  -> Nut bam cua trong (INPUT_PULLUP)
  D3  -> Servo cua trong
  D4  -> Coi bao dong
  D5  -> Servo cua ngoai 1
  D6  -> Servo cua ngoai 2
  D7  -> Cam bien anh sang DO
  D8  -> Chan bat/tat laser (tuy chon)
  D9  -> RC522 RST
  D10 -> RC522 SDA/SS
  D11 -> RC522 MOSI
  D12 -> RC522 MISO
  D13 -> RC522 SCK
  A0  -> Cam bien khoi AO
  A1  -> Cam bien hong ngoai thu dong AO
  A2  -> DHT11/DHT22 DATA
  A4  -> LCD I2C SDA
  A5  -> LCD I2C SCL
*/

// =========================
// KHAI BAO PROTOTYPE CHO CAC TASK
// =========================
void TaskAccessControl(void *pvParameters);
void TaskSensorsAndAlarm(void *pvParameters);
void TaskDisplayAndClimate(void *pvParameters);

// =========================
// KHAI BAO PROTOTYPE CHO CAC HAM HO TRO
// =========================
bool isAuthorizedUid(const MFRC522::Uid &uid);
void printUidToSerial(const MFRC522::Uid &uid);
void setOuterDoorOpen();
void setOuterDoorClosed();
void setInnerDoorOpen();
void setInnerDoorClosed();
void setBuzzer(bool enabled);
bool isSecurityArmed();
void writeLcdLine(uint8_t row, const char *text);
char doorStateSymbol(uint8_t state);

// =========================
// CAU HINH CHAN
// =========================
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

// =========================
// CAU HINH TINH NANG
// =========================
const uint32_t SERIAL_BAUD = 115200;
const bool USE_LCD = true;
const bool USE_LASER_CONTROL_PIN = true;
const bool MOVE_SERVOS_ON_BOOT = false;
const bool BUZZER_ACTIVE_HIGH = true;
const uint8_t LCD_I2C_ADDRESS = 0x27;  // Doi thanh 0x3F neu LCD cua ban dung dia chi nay

#define DHT_TYPE DHT11                  // Doi thanh DHT22 neu ban dung DHT22/AM2302

// Cam bien anh sang DO: thay doi HIGH/LOW neu logic cua module nguoc lai
const uint8_t LIGHT_SENSOR_BROKEN_LEVEL = HIGH;

// Nguong cam bien analog, can tune lai theo module thuc te
const uint16_t SMOKE_THRESHOLD = 450;
const uint16_t PIR_THRESHOLD = 400;
const uint8_t SENSOR_CONFIRM_COUNT = 3;

// Goc servo cua ngoai
const uint8_t OUTER_SERVO_1_CLOSED_ANGLE = 5;
const uint8_t OUTER_SERVO_1_OPEN_ANGLE = 95;
const uint8_t OUTER_SERVO_2_CLOSED_ANGLE = 170;
const uint8_t OUTER_SERVO_2_OPEN_ANGLE = 80;
const uint16_t OUTER_SERVO_MOVE_MS = 900;
const uint16_t OUTER_DOOR_HOLD_MS = 5000;

// Goc servo cua trong
const uint8_t INNER_SERVO_CLOSED_ANGLE = 0;
const uint8_t INNER_SERVO_OPEN_ANGLE = 90;
const uint16_t INNER_SERVO_MOVE_MS = 700;

// Bao mat
const uint8_t SECURITY_GRACE_TICKS = 80;  // 80 x 100ms = 8 giay
const uint8_t RFID_FEEDBACK_TICKS = 40;   // 40 x 75ms = 3 giay

// =========================
// THE RFID DUOC PHEP
// =========================
struct KnownCard {
  uint8_t size;
  uint8_t uid[7];
};

const KnownCard AUTHORIZED_CARDS[] = {
  {4, {0xFB, 0x5E, 0xF5, 0x04, 0x00, 0x00, 0x00}}
};

const uint8_t AUTHORIZED_CARD_COUNT =
    sizeof(AUTHORIZED_CARDS) / sizeof(AUTHORIZED_CARDS[0]);

// =========================
// TRANG THAI HE THONG
// =========================
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
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 16, 2);
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

// =========================
// HAM SETUP
// =========================
#line 166 "E:\\SmartHome_uno\\SmartHome_Form_Base_FreeRTOS\\SmartHome_Form_Base_FreeRTOS.ino"
void setup();
#line 249 "E:\\SmartHome_uno\\SmartHome_Form_Base_FreeRTOS\\SmartHome_Form_Base_FreeRTOS.ino"
void loop();
#line 166 "E:\\SmartHome_uno\\SmartHome_Form_Base_FreeRTOS\\SmartHome_Form_Base_FreeRTOS.ino"
void setup() {
  Serial.begin(SERIAL_BAUD);

  while (!Serial) {
    ; // Giup dung voi form mau. Tren Uno se bo qua rat nhanh.
  }

  delay(1000);
  Serial.println(F("BOOT: SmartHome FreeRTOS starting..."));

  pinMode(PIN_BUTTON_INNER, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LIGHT_DO, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  if (USE_LASER_CONTROL_PIN) {
    pinMode(PIN_LASER_ENABLE, OUTPUT);
    digitalWrite(PIN_LASER_ENABLE, HIGH);
  }

  setBuzzer(false);

  outerServo1.attach(PIN_SERVO_OUTER_1);
  outerServo2.attach(PIN_SERVO_OUTER_2);
  innerServo.attach(PIN_SERVO_INNER);

  if (MOVE_SERVOS_ON_BOOT) {
    Serial.println(F("BOOT: Moving servos to initial position..."));
    setOuterDoorClosed();
    setInnerDoorClosed();
    delay(1200);
  } else {
    Serial.println(F("BOOT: Skip servo startup movement"));
  }

  Serial.println(F("BOOT: Init SPI + RFID"));
  SPI.begin();
  rfid.PCD_Init();

  if (USE_LCD) {
    Serial.println(F("BOOT: Init LCD"));
    lcd.init();
    lcd.backlight();
    writeLcdLine(0, "SmartHome RTOS");
    writeLcdLine(1, "Starting...");
  }

  Serial.println(F("BOOT: Init DHT"));
  dht.begin();

  // Tạo Task 1: Dieu khien RFID + cua ngoai + cua trong
  xTaskCreate(
    TaskAccessControl,
    "Access",
    210,
    NULL,
    3,
    NULL
  );

  // Tạo Task 2: Doc cam bien + bao dong
  xTaskCreate(
    TaskSensorsAndAlarm,
    "Sensors",
    180,
    NULL,
    2,
    NULL
  );

  // Tạo Task 3: Cap nhat LCD + DHT + Serial debug + LED heartbeat
  xTaskCreate(
    TaskDisplayAndClimate,
    "Display",
    220,
    NULL,
    1,
    NULL
  );

  Serial.println(F("BOOT: Tasks created"));
}

void loop() {
  // De trong, FreeRTOS se chiem quyen dieu khien chip
}

// =========================
// DINH NGHIA CAC TASK
// =========================

void TaskAccessControl(void *pvParameters) {
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

    // Xu ly RFID cho cua ngoai
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      Serial.print(F("Card UID: "));
      printUidToSerial(rfid.uid);

      if (isAuthorizedUid(rfid.uid)) {
        Serial.println(F("RFID: Access granted"));
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
        Serial.println(F("RFID: Access denied"));
        gRfidFeedback = RFID_DENIED;
        gRfidFeedbackCounter = RFID_FEEDBACK_TICKS;
      }

      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }

    // State machine cua ngoai
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

    // Xu ly nut bam cua trong
    if (currentButton == LOW && lastButtonState == HIGH) {
      if (gInnerDoorState == DOOR_CLOSED) {
        Serial.println(F("Inner door: Opening"));
        setInnerDoorOpen();
        gInnerDoorState = DOOR_OPENING;
        innerDeadline = now + pdMS_TO_TICKS(INNER_SERVO_MOVE_MS);
        gSecurityGraceCounter = SECURITY_GRACE_TICKS;
      } else if (gInnerDoorState == DOOR_OPEN) {
        Serial.println(F("Inner door: Closing"));
        setInnerDoorClosed();
        gInnerDoorState = DOOR_CLOSING;
        innerDeadline = now + pdMS_TO_TICKS(INNER_SERVO_MOVE_MS);
      }
    }
    lastButtonState = currentButton;

    // State machine cua trong
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

    vTaskDelay(75 / portTICK_PERIOD_MS);
  }
}

void TaskSensorsAndAlarm(void *pvParameters) {
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

    vTaskDelay(100 / portTICK_PERIOD_MS);
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
  uint8_t ledState = LOW;

  for (;;) {
    // Heartbeat LED
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState);

    // Doc DHT moi 2 giay
    if (dhtCounter == 0) {
      const float humidity = dht.readHumidity();
      const float temperature = dht.readTemperature();

      if (!isnan(humidity) && !isnan(temperature)) {
        const int16_t roundedTemp =
            (int16_t)(temperature + (temperature >= 0.0f ? 0.5f : -0.5f));
        const int16_t roundedHumidity = (int16_t)(humidity + 0.5f);

        gTemperatureC = (int8_t)roundedTemp;
        gHumidityPct = (uint8_t)roundedHumidity;
        gDhtValid = 1;
      } else {
        gDhtValid = 0;
      }

      dhtCounter = 8;  // 8 x 250ms = 2 giay
    } else {
      dhtCounter--;
    }

    // Noi dung hien thi
    if (gFireAlarm) {
      strcpy(line0, "!!! FIRE !!!");
      strcpy(line1, "Smoke alarm ON");
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

    // Debug Serial moi chu ky
    Serial.print(F("DEBUG | Out:"));
    Serial.print(doorStateSymbol(gOuterDoorState));
    Serial.print(F(" In:"));
    Serial.print(doorStateSymbol(gInnerDoorState));
    Serial.print(F(" Smoke:"));
    Serial.print(gSmokeDetected);
    Serial.print(F(" PIR:"));
    Serial.print(gPirDetected);
    Serial.print(F(" Laser:"));
    Serial.print(gLaserBroken);
    Serial.print(F(" Fire:"));
    Serial.print(gFireAlarm);
    Serial.print(F(" Sec:"));
    Serial.println(gIntrusionAlarm);

    if (USE_LCD) {
      if (strcmp(line0, last0) != 0) {
        writeLcdLine(0, line0);
        strcpy(last0, line0);
      }
      if (strcmp(line1, last1) != 0) {
        writeLcdLine(1, line1);
        strcpy(last1, line1);
      }
    }

    vTaskDelay(250 / portTICK_PERIOD_MS);
  }
}

// =========================
// CAC HAM HO TRO
// =========================

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

