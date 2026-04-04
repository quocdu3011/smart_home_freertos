#include <Arduino.h>
#line 1 "E:\\SmartHome_uno\\Servo_Timer_Form_Base_FreeRTOS\\Servo_Timer_Form_Base_FreeRTOS.ino"
#include <Arduino_FreeRTOS.h>
#include <Servo.h>

/*
  Servo control every 2 seconds
  Developed from the provided FreeRTOS template

  Pin map
  D5 -> Servo signal

  Notes
  - Servo should use an external 5V supply
  - All GNDs must be connected together
*/

// =========================
// KHAI BAO PROTOTYPE CHO CAC TASK
// =========================
void TaskServoControl(void *pvParameters);

// =========================
// CAU HINH CHAN
// =========================
const uint8_t PIN_SERVO = 5;

// =========================
// CAU HINH CHUNG
// =========================
const uint32_t SERIAL_BAUD = 9600;
const uint8_t SERVO_CLOSED_ANGLE = 0;
const uint8_t SERVO_OPEN_ANGLE = 90;
const uint16_t SERVO_INTERVAL_MS = 2000;

Servo gateServo;

// =========================
// HAM SETUP
// =========================
#line 39 "E:\\SmartHome_uno\\Servo_Timer_Form_Base_FreeRTOS\\Servo_Timer_Form_Base_FreeRTOS.ino"
void setup();
#line 63 "E:\\SmartHome_uno\\Servo_Timer_Form_Base_FreeRTOS\\Servo_Timer_Form_Base_FreeRTOS.ino"
void loop();
#line 39 "E:\\SmartHome_uno\\Servo_Timer_Form_Base_FreeRTOS\\Servo_Timer_Form_Base_FreeRTOS.ino"
void setup() {
  Serial.begin(SERIAL_BAUD);

  while (!Serial) {
    ; // Giu dung form code base. Tren Uno se bo qua nhanh.
  }

  gateServo.attach(PIN_SERVO);
  gateServo.write(SERVO_CLOSED_ANGLE);

  Serial.println(F("FreeRTOS servo timer ready."));
  Serial.println(F("Servo will change position every 2 seconds."));

  // Tạo Task: Dieu khien servo theo chu ky 2 giay
  xTaskCreate(
    TaskServoControl,
    "Servo",
    128,
    NULL,
    1,
    NULL
  );
}

void loop() {
  // De trong, FreeRTOS se chiem quyen dieu khien chip
}

// =========================
// DINH NGHIA CAC TASK
// =========================

void TaskServoControl(void *pvParameters) {
  (void) pvParameters;

  bool isOpen = false;

  for (;;) {
    if (isOpen) {
      gateServo.write(SERVO_CLOSED_ANGLE);
      Serial.println(F("Servo -> CLOSED"));
    } else {
      gateServo.write(SERVO_OPEN_ANGLE);
      Serial.println(F("Servo -> OPEN"));
    }

    isOpen = !isOpen;
    vTaskDelay(SERVO_INTERVAL_MS / portTICK_PERIOD_MS);
  }
}

