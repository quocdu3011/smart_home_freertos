#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>

/*
  RFID gate control for Arduino Uno R3

  Wiring:
  RC522 RST  -> D8
  RC522 SDA  -> D10
  RC522 MOSI -> D11
  RC522 MISO -> D12
  RC522 SCK  -> D13

  Servo 1 signal -> D5
  Servo 2 signal -> D6

  Important:
  - RC522 must use 3.3V
  - Servos should use an external 5V supply
  - All GNDs must be connected together
*/

const uint8_t PIN_RFID_RST = 9;
const uint8_t PIN_RFID_SS = 10;
const uint8_t PIN_SERVO_1 = 5;
const uint8_t PIN_SERVO_2 = 6;

const uint8_t SERVO_1_CLOSED_ANGLE = 0;
const uint8_t SERVO_1_OPEN_ANGLE = 90;
const uint8_t SERVO_2_CLOSED_ANGLE = 180;
const uint8_t SERVO_2_OPEN_ANGLE = 90;

const uint16_t SERVO_MOVE_DELAY_MS = 1000;
const uint16_t GATE_OPEN_HOLD_MS = 5000;
const uint16_t RFID_READ_COOLDOWN_MS = 1200;

struct KnownCard {
  uint8_t size;
  uint8_t uid[7];
};

// Replace these sample values with your real card UID.
const KnownCard AUTHORIZED_CARDS[] = {
  {4, {0xFB, 0x5E, 0xF5, 0x04, 0x00, 0x00, 0x00}}
};

const uint8_t AUTHORIZED_CARD_COUNT =
    sizeof(AUTHORIZED_CARDS) / sizeof(AUTHORIZED_CARDS[0]);

MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Servo gateServo1;
Servo gateServo2;

unsigned long lastReadTime = 0;

bool isAuthorizedUid(const MFRC522::Uid &uid);
void printUid(const MFRC522::Uid &uid);
void openGate();
void closeGate();
void moveGate(uint8_t servo1Angle, uint8_t servo2Angle);

void setup() {
  Serial.begin(115200);

  gateServo1.attach(PIN_SERVO_1);
  gateServo2.attach(PIN_SERVO_2);
  closeGate();

  SPI.begin();
  rfid.PCD_Init();
  delay(100);

  Serial.println(F("RFID gate system ready."));
  Serial.println(F("Scan card to read UID."));
}

void loop() {
  if (millis() - lastReadTime < RFID_READ_COOLDOWN_MS) {
    delay(50);
    return;
  }

  if (!rfid.PICC_IsNewCardPresent()) {
    delay(50);
    return;
  }

  if (!rfid.PICC_ReadCardSerial()) {
    delay(50);
    return;
  }

  Serial.print(F("Card UID: "));
  printUid(rfid.uid);

  if (isAuthorizedUid(rfid.uid)) {
    Serial.println(F("Access granted. Opening gate..."));
    openGate();
    delay(GATE_OPEN_HOLD_MS);
    Serial.println(F("Closing gate..."));
    closeGate();
  } else {
    Serial.println(F("Access denied."));
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  lastReadTime = millis();
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

void printUid(const MFRC522::Uid &uid) {
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

void openGate() {
  moveGate(SERVO_1_OPEN_ANGLE, SERVO_2_OPEN_ANGLE);
}

void closeGate() {
  moveGate(SERVO_1_CLOSED_ANGLE, SERVO_2_CLOSED_ANGLE);
}

void moveGate(uint8_t servo1Angle, uint8_t servo2Angle) {
  gateServo1.write(servo1Angle);
  gateServo2.write(servo2Angle);
  delay(SERVO_MOVE_DELAY_MS);
}

