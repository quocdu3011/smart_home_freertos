#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <DHT.h>

/*
  SmartHome ESP32 Access Module

  Features
  - RC522 RFID access control
  - 2 outer gate servos
  - DHT11 temperature and humidity monitor
  - Local buzzer alert for denied cards or DHT fault

  This module is fully independent from the Arduino Uno safety module.
*/

// --------------------------
// Pin map
// --------------------------
const uint8_t PIN_RFID_SS = 5;
const uint8_t PIN_RFID_RST = 27;
const uint8_t PIN_RFID_SCK = 18;
const uint8_t PIN_RFID_MISO = 19;
const uint8_t PIN_RFID_MOSI = 23;
const uint8_t PIN_SERVO_OUTER_1 = 25;
const uint8_t PIN_SERVO_OUTER_2 = 26;
const uint8_t PIN_DHT = 16;
const uint8_t PIN_BUZZER = 17;
const uint8_t PIN_STATUS_LED = 2;

// --------------------------
// Configuration
// --------------------------
const uint32_t SERIAL_BAUD = 115200;
const bool MOVE_SERVOS_ON_BOOT = false;
const bool BUZZER_ACTIVE_HIGH = true;
const bool STATUS_LED_ACTIVE_HIGH = true;

#define DHT_TYPE DHT11

const uint8_t OUTER_SERVO_1_CLOSED_ANGLE = 5;
const uint8_t OUTER_SERVO_1_OPEN_ANGLE = 95;
const uint8_t OUTER_SERVO_2_CLOSED_ANGLE = 170;
const uint8_t OUTER_SERVO_2_OPEN_ANGLE = 80;
const uint16_t OUTER_SERVO_MOVE_MS = 900;
const uint16_t OUTER_DOOR_HOLD_MS = 5000;

const uint16_t ACCESS_TASK_INTERVAL_MS = 75;
const uint16_t CLIMATE_TASK_INTERVAL_MS = 250;
const uint16_t ALERT_TASK_INTERVAL_MS = 80;

const uint8_t RFID_FEEDBACK_TICKS = 40;
const uint8_t DHT_SAMPLE_TICKS = 8;       // 8 x 250ms = 2s
const uint8_t DHT_FAULT_CONFIRM_COUNT = 3;
const uint8_t DENIED_ALERT_TICKS = 16;    // 16 x 80ms = 1.28s

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
DHT dht(PIN_DHT, DHT_TYPE);

volatile uint8_t gOuterDoorState = DOOR_CLOSED;
volatile uint8_t gRfidFeedback = RFID_IDLE;
volatile uint8_t gRfidFeedbackCounter = 0;
volatile uint8_t gDeniedAlertCounter = 0;

volatile uint8_t gDhtValid = 0;
volatile int8_t gTemperatureC = 0;
volatile uint8_t gHumidityPct = 0;
volatile uint8_t gDhtFailureCounter = 0;
volatile uint8_t gDhtFaultActive = 0;

void TaskAccess(void *pvParameters);
void TaskClimate(void *pvParameters);
void TaskAlert(void *pvParameters);

bool isAuthorizedUid(const MFRC522::Uid &uid);
void printUidToSerial(const MFRC522::Uid &uid);
void setOuterDoorOpen();
void setOuterDoorClosed();
void setBuzzer(bool enabled);
void setStatusLed(bool enabled);
char doorStateSymbol(uint8_t state);

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1200);

  Serial.println(F("BOOT: ESP32 access module starting"));

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  setBuzzer(false);
  setStatusLed(false);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  outerServo1.setPeriodHertz(50);
  outerServo2.setPeriodHertz(50);
  outerServo1.attach(PIN_SERVO_OUTER_1, 500, 2400);
  outerServo2.attach(PIN_SERVO_OUTER_2, 500, 2400);

  if (MOVE_SERVOS_ON_BOOT) {
    Serial.println(F("BOOT: moving outer gate to closed position"));
    setOuterDoorClosed();
    delay(OUTER_SERVO_MOVE_MS);
  } else {
    Serial.println(F("BOOT: servo startup movement skipped"));
  }

  Serial.println(F("BOOT: starting SPI/RFID"));
  SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SS);
  rfid.PCD_Init();

  Serial.println(F("BOOT: starting DHT"));
  dht.begin();

  Serial.println(F("ESP32 access module ready."));
  Serial.println(F("Scan RFID card to print UID."));

  xTaskCreate(TaskAccess, "Access", 4096, NULL, 3, NULL);
  xTaskCreate(TaskClimate, "Climate", 3072, NULL, 1, NULL);
  xTaskCreate(TaskAlert, "Alert", 2048, NULL, 2, NULL);
}

void loop() {
  delay(1000);
}

void TaskAccess(void *pvParameters) {
  (void) pvParameters;

  TickType_t outerDeadline = 0;

  for (;;) {
    const TickType_t now = xTaskGetTickCount();

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
        gDeniedAlertCounter = DENIED_ALERT_TICKS;
      }

      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }

    switch (gOuterDoorState) {
      case DOOR_OPENING:
        if (now >= outerDeadline) {
          gOuterDoorState = DOOR_OPEN;
          outerDeadline = now + pdMS_TO_TICKS(OUTER_DOOR_HOLD_MS);
          Serial.println(F("Gate is open."));
        }
        break;

      case DOOR_OPEN:
        if (now >= outerDeadline) {
          setOuterDoorClosed();
          gOuterDoorState = DOOR_CLOSING;
          outerDeadline = now + pdMS_TO_TICKS(OUTER_SERVO_MOVE_MS);
          Serial.println(F("Closing gate..."));
        }
        break;

      case DOOR_CLOSING:
        if (now >= outerDeadline) {
          gOuterDoorState = DOOR_CLOSED;
          Serial.println(F("Gate is closed."));
        }
        break;

      default:
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(ACCESS_TASK_INTERVAL_MS));
  }
}

void TaskClimate(void *pvParameters) {
  (void) pvParameters;

  uint8_t dhtCounter = 0;
  bool ledState = false;

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
        gDhtFailureCounter = 0;
        gDhtFaultActive = 0;

        Serial.print(F("DHT OK | T:"));
        Serial.print((int) gTemperatureC);
        Serial.print(F("C H:"));
        Serial.print(gHumidityPct);
        Serial.println(F("%"));
      } else {
        if (gDhtFailureCounter < 255) {
          gDhtFailureCounter++;
        }

        gDhtValid = 0;
        gDhtFaultActive = (gDhtFailureCounter >= DHT_FAULT_CONFIRM_COUNT);
        Serial.println(F("DHT read failed"));
      }

      dhtCounter = DHT_SAMPLE_TICKS;
    } else {
      dhtCounter--;
    }

    ledState = !ledState;
    setStatusLed(ledState);

    vTaskDelay(pdMS_TO_TICKS(CLIMATE_TASK_INTERVAL_MS));
  }
}

void TaskAlert(void *pvParameters) {
  (void) pvParameters;

  uint16_t alertTick = 0;

  for (;;) {
    bool buzzerOn = false;

    if (gDeniedAlertCounter > 0) {
      const uint8_t phase = DENIED_ALERT_TICKS - gDeniedAlertCounter;
      buzzerOn = ((phase % 4) < 2);
      gDeniedAlertCounter--;
    } else if (gDhtFaultActive) {
      buzzerOn = ((alertTick % 25) < 2);
    }

    setBuzzer(buzzerOn);
    alertTick++;
    vTaskDelay(pdMS_TO_TICKS(ALERT_TASK_INTERVAL_MS));
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

void setBuzzer(bool enabled) {
  const uint8_t level = enabled
      ? (BUZZER_ACTIVE_HIGH ? HIGH : LOW)
      : (BUZZER_ACTIVE_HIGH ? LOW : HIGH);
  digitalWrite(PIN_BUZZER, level);
}

void setStatusLed(bool enabled) {
  const uint8_t level = enabled
      ? (STATUS_LED_ACTIVE_HIGH ? HIGH : LOW)
      : (STATUS_LED_ACTIVE_HIGH ? LOW : HIGH);
  digitalWrite(PIN_STATUS_LED, level);
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
