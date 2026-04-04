#include <Arduino_FreeRTOS.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

/*
  SmartHome model using Arduino Uno R3 + FreeRTOS

  Required libraries:
  - Arduino_FreeRTOS_Library
  - MFRC522
  - LiquidCrystal_I2C
  - Servo

  Pin plan used in this sketch:
  D2  -> Flame sensor DO
  D3  -> Drying rack motor PWM
  D4  -> Drying rack motor IN1
  D5  -> Drying rack motor IN2
  D6  -> Gate servo signal (split this signal to both servos)
  D7  -> 74HC595 LATCH (STCP)
  D8  -> RC522 RST
  D9  -> 74HC595 CLOCK (SHCP)
  D10 -> RC522 SDA/SS
  D11 -> RC522 MOSI
  D12 -> RC522 MISO
  D13 -> RC522 SCK
  A0  -> Smoke sensor AO
  A1  -> LDR/light sensor AO
  A2  -> Rain sensor AO
  A3  -> 74HC595 DATA (DS)
  A4  -> LCD1602 I2C SDA
  A5  -> LCD1602 I2C SCL

  Important assumptions:
  - Both gate servos move together, so they share the same signal pin D6.
  - RC522 is powered from 3.3V.
  - Servos and DC motor use an external supply, but all GNDs must be common.
  - 74HC595 only drives low-power outputs such as LEDs or a transistor for a buzzer.
*/

// --------------------------
// Pin definitions
// --------------------------
const uint8_t PIN_FLAME_DO = 2;
const uint8_t PIN_DRYING_PWM = 3;
const uint8_t PIN_DRYING_IN1 = 4;
const uint8_t PIN_DRYING_IN2 = 5;
const uint8_t PIN_GATE_SERVO = 6;
const uint8_t PIN_SR_LATCH = 7;
const uint8_t PIN_RFID_RST = 8;
const uint8_t PIN_SR_CLOCK = 9;
const uint8_t PIN_RFID_SS = 10;
const uint8_t PIN_SMOKE_AO = A0;
const uint8_t PIN_LDR_AO = A1;
const uint8_t PIN_RAIN_AO = A2;
const uint8_t PIN_SR_DATA = A3;

// --------------------------
// Configuration
// --------------------------
const uint8_t LCD_I2C_ADDRESS = 0x27;  // Change to 0x3F if your LCD uses 0x3F

const uint8_t FLAME_ACTIVE_LEVEL = LOW;   // Most flame modules pull LOW on detection
const bool RAIN_WET_WHEN_LOW = true;      // Common rain modules read lower when wet
const bool LASER_BROKEN_WHEN_LOW = true;  // LDR value usually drops when beam is cut

const uint16_t SMOKE_THRESHOLD = 450;     // Tune this after reading your sensor
const uint16_t RAIN_THRESHOLD = 500;      // Tune this after reading your sensor
const uint16_t LDR_BREAK_MARGIN = 120;    // Beam broken if reading differs this much

const uint8_t GATE_CLOSED_ANGLE = 0;
const uint8_t GATE_OPEN_ANGLE = 95;
const uint16_t GATE_SERVO_MOVE_MS = 900;
const uint16_t GATE_OPEN_HOLD_MS = 5000;

const uint8_t DRYING_MOTOR_SPEED = 200;   // 0..255
const uint16_t RACK_RETRACT_MS = 6000;
const uint16_t RACK_EXTEND_MS = 6000;
const uint16_t RACK_DRY_DELAY_MS = 10000; // Wait before extending again

const uint8_t SENSOR_CONFIRM_COUNT = 3;
const uint8_t RFID_FEEDBACK_TICKS = 30;   // 30 x 75ms ~= 2.25s

// Replace these with your actual card UID values from Serial Monitor.
struct KnownCard {
  uint8_t size;
  uint8_t uid[7];
};

const KnownCard AUTHORIZED_CARDS[] = {
  {4, {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00}},
  {4, {0x12, 0x34, 0x56, 0x78, 0x00, 0x00, 0x00}}
};
const uint8_t AUTHORIZED_CARD_COUNT =
    sizeof(AUTHORIZED_CARDS) / sizeof(AUTHORIZED_CARDS[0]);

// 74HC595 output mapping
const uint8_t SR_FIRE_LED = 1 << 0;
const uint8_t SR_SECURITY_LED = 1 << 1;
const uint8_t SR_RAIN_LED = 1 << 2;
const uint8_t SR_GATE_LED = 1 << 3;
const uint8_t SR_ACCESS_LED = 1 << 4;
const uint8_t SR_BUZZER = 1 << 5;
const uint8_t SR_HEARTBEAT = 1 << 6;

enum GateState : uint8_t {
  GATE_CLOSED = 0,
  GATE_OPENING,
  GATE_OPEN,
  GATE_CLOSING
};

enum RackState : uint8_t {
  RACK_RETRACTED = 0,
  RACK_RETRACTING,
  RACK_EXTENDED,
  RACK_EXTENDING
};

enum RfidFeedback : uint8_t {
  RFID_IDLE = 0,
  RFID_GRANTED,
  RFID_DENIED
};

enum MotorDirection : uint8_t {
  MOTOR_STOP = 0,
  MOTOR_EXTEND,
  MOTOR_RETRACT
};

MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Servo gateServo;
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 16, 2);

volatile uint8_t gGateState = GATE_CLOSED;
volatile uint8_t gRackState = RACK_RETRACTED;
volatile uint8_t gFireAlarm = 0;
volatile uint8_t gIntrusionAlarm = 0;
volatile uint8_t gFlameDetected = 0;
volatile uint8_t gSmokeDetected = 0;
volatile uint8_t gRainDetected = 0;
volatile uint8_t gRfidFeedback = RFID_IDLE;
volatile uint8_t gRfidFeedbackCounter = 0;

uint16_t gLaserReference = 0;

void TaskAccessControl(void *pvParameters);
void TaskSafety(void *pvParameters);
void TaskDryingRack(void *pvParameters);
void TaskUiAndAlarm(void *pvParameters);

bool isAuthorizedUid(const MFRC522::Uid &uid);
void printUidToSerial(const MFRC522::Uid &uid);
void writeShiftRegister(uint8_t value);
uint16_t calibrateLaserReference();
bool isRainDetected(uint16_t value);
bool isLaserBroken(uint16_t value);
void setDryingMotor(MotorDirection direction);
void writeLcdLine(uint8_t row, const char *text);
const char *gateLabel(uint8_t state);
const char *rackLabel(uint8_t state);

void setup() {
  Serial.begin(115200);

  pinMode(PIN_FLAME_DO, INPUT);
  pinMode(PIN_DRYING_PWM, OUTPUT);
  pinMode(PIN_DRYING_IN1, OUTPUT);
  pinMode(PIN_DRYING_IN2, OUTPUT);
  pinMode(PIN_SR_DATA, OUTPUT);
  pinMode(PIN_SR_CLOCK, OUTPUT);
  pinMode(PIN_SR_LATCH, OUTPUT);

  setDryingMotor(MOTOR_STOP);
  writeShiftRegister(0x00);

  lcd.init();
  lcd.backlight();
  writeLcdLine(0, "SmartHome RTOS");
  writeLcdLine(1, "Booting...");

  gateServo.attach(PIN_GATE_SERVO);
  gateServo.write(GATE_CLOSED_ANGLE);
  delay(GATE_SERVO_MOVE_MS);
  gateServo.detach();

  SPI.begin();
  rfid.PCD_Init();
  delay(100);

  writeLcdLine(0, "Align laser...");
  writeLcdLine(1, "Calibrating");
  gLaserReference = calibrateLaserReference();

  Serial.println(F("System ready."));
  Serial.print(F("Laser reference = "));
  Serial.println(gLaserReference);
  Serial.println(F("Replace AUTHORIZED_CARDS with your real RFID UIDs."));

  xTaskCreate(TaskAccessControl, "Access", 192, NULL, 3, NULL);
  xTaskCreate(TaskSafety, "Safety", 160, NULL, 3, NULL);
  xTaskCreate(TaskDryingRack, "Drying", 160, NULL, 2, NULL);
  xTaskCreate(TaskUiAndAlarm, "UI", 192, NULL, 1, NULL);
}

void loop() {
  // Not used. Arduino_FreeRTOS starts the scheduler after setup().
}

void TaskAccessControl(void *pvParameters) {
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
        gRfidFeedback = RFID_GRANTED;
        gRfidFeedbackCounter = RFID_FEEDBACK_TICKS;
        Serial.println(F("Access granted"));

        if (gGateState == GATE_CLOSED || gGateState == GATE_CLOSING) {
          gateServo.attach(PIN_GATE_SERVO);
          gateServo.write(GATE_OPEN_ANGLE);
          gGateState = GATE_OPENING;
          gateDeadline = now + pdMS_TO_TICKS(GATE_SERVO_MOVE_MS);
        } else if (gGateState == GATE_OPEN) {
          gateDeadline = now + pdMS_TO_TICKS(GATE_OPEN_HOLD_MS);
        }
      } else {
        gRfidFeedback = RFID_DENIED;
        gRfidFeedbackCounter = RFID_FEEDBACK_TICKS;
        Serial.println(F("Access denied"));
      }

      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }

    switch (gGateState) {
      case GATE_OPENING:
        if (now >= gateDeadline) {
          gateServo.detach();
          gGateState = GATE_OPEN;
          gateDeadline = now + pdMS_TO_TICKS(GATE_OPEN_HOLD_MS);
        }
        break;

      case GATE_OPEN:
        if (now >= gateDeadline) {
          gateServo.attach(PIN_GATE_SERVO);
          gateServo.write(GATE_CLOSED_ANGLE);
          gGateState = GATE_CLOSING;
          gateDeadline = now + pdMS_TO_TICKS(GATE_SERVO_MOVE_MS);
        }
        break;

      case GATE_CLOSING:
        if (now >= gateDeadline) {
          gateServo.detach();
          gGateState = GATE_CLOSED;
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

  uint8_t fireCounter = 0;
  uint8_t intrusionCounter = 0;

  for (;;) {
    const bool flameNow = (digitalRead(PIN_FLAME_DO) == FLAME_ACTIVE_LEVEL);
    const uint16_t smokeValue = analogRead(PIN_SMOKE_AO);
    const uint16_t ldrValue = analogRead(PIN_LDR_AO);

    const bool smokeNow = (smokeValue >= SMOKE_THRESHOLD);
    const bool fireNow = flameNow || smokeNow;

    if (fireNow) {
      if (fireCounter < SENSOR_CONFIRM_COUNT) {
        fireCounter++;
      }
    } else if (fireCounter > 0) {
      fireCounter--;
    }

    gFlameDetected = flameNow;
    gSmokeDetected = smokeNow;
    gFireAlarm = (fireCounter >= SENSOR_CONFIRM_COUNT);

    const bool securityEnabled = (gGateState == GATE_CLOSED);
    const bool intrusionNow = securityEnabled && isLaserBroken(ldrValue);

    if (intrusionNow) {
      if (intrusionCounter < SENSOR_CONFIRM_COUNT) {
        intrusionCounter++;
      }
    } else if (intrusionCounter > 0) {
      intrusionCounter--;
    }

    gIntrusionAlarm = (intrusionCounter >= SENSOR_CONFIRM_COUNT);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void TaskDryingRack(void *pvParameters) {
  (void) pvParameters;

  TickType_t motionDeadline = 0;
  TickType_t lastWetTick = xTaskGetTickCount();

  gRackState = RACK_RETRACTED;

  for (;;) {
    const TickType_t now = xTaskGetTickCount();
    const uint16_t rainValue = analogRead(PIN_RAIN_AO);
    const bool rainNow = isRainDetected(rainValue);

    gRainDetected = rainNow;
    if (rainNow) {
      lastWetTick = now;
    }

    switch (gRackState) {
      case RACK_EXTENDED:
        if (rainNow) {
          setDryingMotor(MOTOR_RETRACT);
          gRackState = RACK_RETRACTING;
          motionDeadline = now + pdMS_TO_TICKS(RACK_RETRACT_MS);
        }
        break;

      case RACK_RETRACTED:
        if (!rainNow && (now - lastWetTick) >= pdMS_TO_TICKS(RACK_DRY_DELAY_MS)) {
          setDryingMotor(MOTOR_EXTEND);
          gRackState = RACK_EXTENDING;
          motionDeadline = now + pdMS_TO_TICKS(RACK_EXTEND_MS);
        }
        break;

      case RACK_EXTENDING:
        if (rainNow) {
          setDryingMotor(MOTOR_RETRACT);
          gRackState = RACK_RETRACTING;
          motionDeadline = now + pdMS_TO_TICKS(RACK_RETRACT_MS);
        } else if (now >= motionDeadline) {
          setDryingMotor(MOTOR_STOP);
          gRackState = RACK_EXTENDED;
        }
        break;

      case RACK_RETRACTING:
        if (now >= motionDeadline) {
          setDryingMotor(MOTOR_STOP);
          gRackState = RACK_RETRACTED;
        }
        break;

      default:
        setDryingMotor(MOTOR_STOP);
        gRackState = RACK_RETRACTED;
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void TaskUiAndAlarm(void *pvParameters) {
  (void) pvParameters;

  char line0[17];
  char line1[17];
  char last0[17] = "";
  char last1[17] = "";

  for (;;) {
    const TickType_t now = xTaskGetTickCount();
    uint8_t srValue = 0;

    if (gFireAlarm) {
      srValue |= SR_FIRE_LED;
    }
    if (gIntrusionAlarm) {
      srValue |= SR_SECURITY_LED;
    }
    if (gRainDetected) {
      srValue |= SR_RAIN_LED;
    }
    if (gGateState != GATE_CLOSED) {
      srValue |= SR_GATE_LED;
    }
    if (gRfidFeedback == RFID_GRANTED && (gRfidFeedbackCounter & 0x01)) {
      srValue |= SR_ACCESS_LED;
    }
    if ((gFireAlarm || gIntrusionAlarm) &&
        (((now / pdMS_TO_TICKS(200)) & 0x01) != 0)) {
      srValue |= SR_BUZZER;
    }
    if (((now / pdMS_TO_TICKS(500)) & 0x01) != 0) {
      srValue |= SR_HEARTBEAT;
    }

    writeShiftRegister(srValue);

    if (gFireAlarm) {
      strcpy(line0, "*** FIRE ***");
      strcpy(line1, gSmokeDetected ? "SMK:ON FLM:? " : "FLM:ON SMK:?");
      if (gSmokeDetected && gFlameDetected) {
        strcpy(line1, "FLM:ON SMK:ON");
      } else if (gFlameDetected) {
        strcpy(line1, "FLM:ON SMK:OFF");
      } else {
        strcpy(line1, "FLM:OFF SMK:ON");
      }
    } else if (gIntrusionAlarm) {
      strcpy(line0, "INTRUSION !!!");
      strcpy(line1, "LASER BEAM CUT");
    } else if (gRfidFeedback == RFID_GRANTED) {
      strcpy(line0, "RFID GRANTED");
      strcpy(line1, "G:");
      strcat(line1, gateLabel(gGateState));
      strcat(line1, " R:");
      strcat(line1, rackLabel(gRackState));
    } else if (gRfidFeedback == RFID_DENIED) {
      strcpy(line0, "RFID DENIED");
      strcpy(line1, "AUTHORIZED ONLY");
    } else {
      strcpy(line0, "G:");
      strcat(line0, gateLabel(gGateState));
      strcat(line0, " F:");
      strcat(line0, gFireAlarm ? "ALM" : "OK");

      strcpy(line1, "S:");
      strcat(line1, gIntrusionAlarm ? "ALM" : "OK");
      strcat(line1, " R:");
      strcat(line1, rackLabel(gRackState));
    }

    if (strcmp(line0, last0) != 0) {
      writeLcdLine(0, line0);
      strcpy(last0, line0);
    }
    if (strcmp(line1, last1) != 0) {
      writeLcdLine(1, line1);
      strcpy(last1, line1);
    }

    vTaskDelay(pdMS_TO_TICKS(150));
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

void writeShiftRegister(uint8_t value) {
  digitalWrite(PIN_SR_LATCH, LOW);
  shiftOut(PIN_SR_DATA, PIN_SR_CLOCK, MSBFIRST, value);
  digitalWrite(PIN_SR_LATCH, HIGH);
}

uint16_t calibrateLaserReference() {
  uint32_t total = 0;
  const uint8_t samples = 40;

  for (uint8_t i = 0; i < samples; i++) {
    total += analogRead(PIN_LDR_AO);
    delay(50);
  }

  return total / samples;
}

bool isRainDetected(uint16_t value) {
  if (RAIN_WET_WHEN_LOW) {
    return value <= RAIN_THRESHOLD;
  }
  return value >= RAIN_THRESHOLD;
}

bool isLaserBroken(uint16_t value) {
  if (LASER_BROKEN_WHEN_LOW) {
    return (value + LDR_BREAK_MARGIN) < gLaserReference;
  }
  return value > (gLaserReference + LDR_BREAK_MARGIN);
}

void setDryingMotor(MotorDirection direction) {
  switch (direction) {
    case MOTOR_EXTEND:
      digitalWrite(PIN_DRYING_IN1, HIGH);
      digitalWrite(PIN_DRYING_IN2, LOW);
      analogWrite(PIN_DRYING_PWM, DRYING_MOTOR_SPEED);
      break;

    case MOTOR_RETRACT:
      digitalWrite(PIN_DRYING_IN1, LOW);
      digitalWrite(PIN_DRYING_IN2, HIGH);
      analogWrite(PIN_DRYING_PWM, DRYING_MOTOR_SPEED);
      break;

    default:
      digitalWrite(PIN_DRYING_IN1, LOW);
      digitalWrite(PIN_DRYING_IN2, LOW);
      analogWrite(PIN_DRYING_PWM, 0);
      break;
  }
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

const char *gateLabel(uint8_t state) {
  switch (state) {
    case GATE_OPENING:
      return "OPN";
    case GATE_OPEN:
      return "OPEN";
    case GATE_CLOSING:
      return "CLS";
    default:
      return "SHUT";
  }
}

const char *rackLabel(uint8_t state) {
  switch (state) {
    case RACK_RETRACTING:
      return "IN";
    case RACK_EXTENDED:
      return "OUT";
    case RACK_EXTENDING:
      return "OUT+";
    default:
      return "IN";
  }
}
