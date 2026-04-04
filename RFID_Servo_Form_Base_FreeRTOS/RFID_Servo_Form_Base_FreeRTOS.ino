#include <Arduino_FreeRTOS.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>

/*
  RFID + 2 Servo gate control
  Developed from the provided FreeRTOS template

  Pin map
  D5  -> Outer gate servo 1
  D6  -> Outer gate servo 2
  D9  -> RC522 RST
  D10 -> RC522 SDA/SS
  D11 -> RC522 MOSI
  D12 -> RC522 MISO
  D13 -> RC522 SCK

  Important
  - RC522 must use 3.3V
  - Servos should use an external 5V supply
  - All GNDs must be connected together
*/

// =========================
// KHAI BAO PROTOTYPE CHO CAC TASK
// =========================
void TaskBlink(void *pvParameters);
void TaskRFIDGate(void *pvParameters);

// =========================
// KHAI BAO PROTOTYPE CHO CAC HAM HO TRO
// =========================
bool isAuthorizedUid(const MFRC522::Uid &uid);
void printUidToSerial(const MFRC522::Uid &uid);
void setGateOpen();
void setGateClosed();
char gateStateSymbol(uint8_t state);

// =========================
// CAU HINH CHAN
// =========================
const uint8_t PIN_SERVO_1 = 5;
const uint8_t PIN_SERVO_2 = 6;
const uint8_t PIN_RFID_RST = 9;
const uint8_t PIN_RFID_SS = 10;

// =========================
// CAU HINH CHUNG
// =========================
const uint32_t SERIAL_BAUD = 115200;
const bool MOVE_SERVOS_ON_BOOT = false;

const uint8_t SERVO_1_CLOSED_ANGLE = 0;
const uint8_t SERVO_1_OPEN_ANGLE = 90;
const uint8_t SERVO_2_CLOSED_ANGLE = 180;
const uint8_t SERVO_2_OPEN_ANGLE = 90;

const uint16_t SERVO_MOVE_MS = 900;
const uint16_t GATE_OPEN_HOLD_MS = 5000;
const uint16_t RFID_SCAN_INTERVAL_MS = 75;
const uint8_t RFID_FEEDBACK_TICKS = 40;  // 40 x 75ms = 3s

struct KnownCard {
  uint8_t size;
  uint8_t uid[7];
};

const KnownCard AUTHORIZED_CARDS[] = {
  {4, {0xFB, 0x5E, 0xF5, 0x04, 0x00, 0x00, 0x00}}
};

const uint8_t AUTHORIZED_CARD_COUNT =
    sizeof(AUTHORIZED_CARDS) / sizeof(AUTHORIZED_CARDS[0]);

enum GateState : uint8_t {
  GATE_CLOSED = 0,
  GATE_OPENING,
  GATE_OPEN,
  GATE_CLOSING
};

enum RfidFeedback : uint8_t {
  RFID_IDLE = 0,
  RFID_GRANTED,
  RFID_DENIED
};

MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Servo gateServo1;
Servo gateServo2;

volatile uint8_t gGateState = GATE_CLOSED;
volatile uint8_t gRfidFeedback = RFID_IDLE;
volatile uint8_t gRfidFeedbackCounter = 0;

// =========================
// HAM SETUP
// =========================
void setup() {
  Serial.begin(SERIAL_BAUD);

  while (!Serial) {
    ; // Giu dung form code base. Tren Uno se bo qua nhanh.
  }

  pinMode(LED_BUILTIN, OUTPUT);

  gateServo1.attach(PIN_SERVO_1);
  gateServo2.attach(PIN_SERVO_2);

  if (MOVE_SERVOS_ON_BOOT) {
    setGateClosed();
    delay(SERVO_MOVE_MS);
  }

  SPI.begin();
  rfid.PCD_Init();
  delay(100);

  Serial.println(F("RFID FreeRTOS gate ready."));
  Serial.println(F("Authorized UID: FB 5E F5 04"));
  Serial.println(F("Scan card to open gate."));

  // Tạo Task 1: Chớp tắt LED để báo hệ thống đang chạy
  xTaskCreate(
    TaskBlink,
    "Blink",
    128,
    NULL,
    1,
    NULL
  );

  // Tạo Task 2: Quét RFID và điều khiển 2 servo
  xTaskCreate(
    TaskRFIDGate,
    "RFIDGate",
    192,
    NULL,
    2,
    NULL
  );
}

void loop() {
  // De trong, FreeRTOS se chiem quyen dieu khien chip
}

// =========================
// DINH NGHIA CAC TASK
// =========================

void TaskBlink(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    digitalWrite(LED_BUILTIN, HIGH);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    digitalWrite(LED_BUILTIN, LOW);
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void TaskRFIDGate(void *pvParameters) {
  (void) pvParameters;

  TickType_t gateDeadline = 0;

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
        Serial.println(F("Access granted. Opening gate..."));
        gRfidFeedback = RFID_GRANTED;
        gRfidFeedbackCounter = RFID_FEEDBACK_TICKS;

        if (gGateState == GATE_CLOSED || gGateState == GATE_CLOSING) {
          setGateOpen();
          gGateState = GATE_OPENING;
          gateDeadline = now + pdMS_TO_TICKS(SERVO_MOVE_MS);
        } else if (gGateState == GATE_OPEN) {
          gateDeadline = now + pdMS_TO_TICKS(GATE_OPEN_HOLD_MS);
        }
      } else {
        Serial.println(F("Access denied."));
        gRfidFeedback = RFID_DENIED;
        gRfidFeedbackCounter = RFID_FEEDBACK_TICKS;
      }

      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }

    switch (gGateState) {
      case GATE_OPENING:
        if (now >= gateDeadline) {
          gGateState = GATE_OPEN;
          gateDeadline = now + pdMS_TO_TICKS(GATE_OPEN_HOLD_MS);
          Serial.println(F("Gate is open."));
        }
        break;

      case GATE_OPEN:
        if (now >= gateDeadline) {
          setGateClosed();
          gGateState = GATE_CLOSING;
          gateDeadline = now + pdMS_TO_TICKS(SERVO_MOVE_MS);
          Serial.println(F("Closing gate..."));
        }
        break;

      case GATE_CLOSING:
        if (now >= gateDeadline) {
          gGateState = GATE_CLOSED;
          Serial.println(F("Gate is closed."));
        }
        break;

      default:
        break;
    }

    vTaskDelay(RFID_SCAN_INTERVAL_MS / portTICK_PERIOD_MS);
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

void setGateOpen() {
  gateServo1.write(SERVO_1_OPEN_ANGLE);
  gateServo2.write(SERVO_2_OPEN_ANGLE);
}

void setGateClosed() {
  gateServo1.write(SERVO_1_CLOSED_ANGLE);
  gateServo2.write(SERVO_2_CLOSED_ANGLE);
}

char gateStateSymbol(uint8_t state) {
  switch (state) {
    case GATE_OPEN:
      return 'O';
    case GATE_OPENING:
      return 'o';
    case GATE_CLOSING:
      return 'c';
    default:
      return 'C';
  }
}
