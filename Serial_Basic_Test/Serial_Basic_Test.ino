/*
  Minimal serial test for Arduino Uno R3

  Purpose:
  - Verify code upload works
  - Verify the selected COM port is correct
  - Verify Serial Monitor settings
  - Verify the board is not constantly resetting due to power issues
*/

const uint32_t SERIAL_BAUD = 9600;
const uint8_t PIN_STATUS_LED = LED_BUILTIN;

void setup() {
  pinMode(PIN_STATUS_LED, OUTPUT);

  Serial.begin(SERIAL_BAUD);
  delay(1500);

  Serial.println("UNO serial test booted.");
  Serial.println("If you can read this, upload and Serial Monitor are working.");
}

void loop() {
  digitalWrite(PIN_STATUS_LED, HIGH);
  Serial.println("Heartbeat: board is running.");
  delay(1000);

  digitalWrite(PIN_STATUS_LED, LOW);
  delay(1000);
}
