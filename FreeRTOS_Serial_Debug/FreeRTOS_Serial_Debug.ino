#include <Arduino_FreeRTOS.h>

/*
  FreeRTOS serial debug sketch for Arduino Uno R3

  Purpose
  - Verify Arduino_FreeRTOS can boot
  - Verify Serial Monitor can receive data
  - Verify the board is not resetting or hanging

  Hardware
  - No sensor or actuator is required
  - Only the USB cable is needed
*/

const uint32_t SERIAL_BAUD = 9600;
const uint8_t PIN_STATUS_LED = LED_BUILTIN;

void TaskSerialHeartbeat(void *pvParameters);
void TaskLedBlink(void *pvParameters);

void setup() {
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  Serial.begin(SERIAL_BAUD);
  delay(1500);

  Serial.println(F("BOOT: FreeRTOS serial debug starting"));
  Serial.println(F("SETTING: baud = 9600"));
  Serial.println(F("EXPECT: heartbeat every 1 second"));

  xTaskCreate(TaskSerialHeartbeat, "Serial", 160, NULL, 2, NULL);
  xTaskCreate(TaskLedBlink, "LED", 128, NULL, 1, NULL);
}

void loop() {
  // Scheduler is started automatically by Arduino_FreeRTOS.
}

void TaskSerialHeartbeat(void *pvParameters) {
  (void) pvParameters;

  uint32_t counter = 0;

  for (;;) {
    Serial.print(F("HB "));
    Serial.print(counter++);
    Serial.println(F(" | FreeRTOS serial is alive"));
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void TaskLedBlink(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    digitalWrite(PIN_STATUS_LED, HIGH);
    vTaskDelay(pdMS_TO_TICKS(500));
    digitalWrite(PIN_STATUS_LED, LOW);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
