#include <Arduino.h>
#line 1 "E:\\SmartHome_uno\\FreeRTOS_Basic_Test\\FreeRTOS_Basic_Test.ino"
#include <Arduino_FreeRTOS.h>

/*
  Minimal FreeRTOS test for Arduino Uno R3

  Purpose:
  - Verify Arduino_FreeRTOS library is installed correctly
  - Verify a very small FreeRTOS sketch can boot and print to Serial
*/

const uint32_t SERIAL_BAUD = 115200;
const uint8_t PIN_STATUS_LED = LED_BUILTIN;

void TaskHeartbeat(void *pvParameters);

#line 16 "E:\\SmartHome_uno\\FreeRTOS_Basic_Test\\FreeRTOS_Basic_Test.ino"
void setup();
#line 27 "E:\\SmartHome_uno\\FreeRTOS_Basic_Test\\FreeRTOS_Basic_Test.ino"
void loop();
#line 16 "E:\\SmartHome_uno\\FreeRTOS_Basic_Test\\FreeRTOS_Basic_Test.ino"
void setup() {
  //pinMode(PIN_STATUS_LED, OUTPUT);

  Serial.begin(SERIAL_BAUD);
  delay(1200);

  Serial.println("FreeRTOS test booted.");

  xTaskCreate(TaskHeartbeat, "HB", 128, NULL, 1, NULL);
}

void loop() {
  // Not used.
}

void TaskHeartbeat(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    Serial.println("FreeRTOS heartbeat");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}


