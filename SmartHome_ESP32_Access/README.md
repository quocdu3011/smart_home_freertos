# SmartHome ESP32 Access

Sketch: `SmartHome_ESP32_Access.ino`

## Features

- RFID RC522 opens and closes the outer gate with 2 servos
- DHT11 runs on ESP32 only
- Local buzzer alerts on denied RFID cards
- Local buzzer also warns if DHT11 keeps failing
- FreeRTOS tasks are used from the ESP32 Arduino core
- This module works independently from the Arduino Uno safety module

## Pin map

- `GPIO5`: RC522 SDA/SS
- `GPIO27`: RC522 RST
- `GPIO18`: RC522 SCK
- `GPIO19`: RC522 MISO
- `GPIO23`: RC522 MOSI
- `GPIO25`: outer servo 1
- `GPIO26`: outer servo 2
- `GPIO16`: DHT11 data
- `GPIO17`: buzzer
- `GPIO2`: status LED

## Libraries

- `MFRC522`
- `ESP32Servo`
- `DHT sensor library`

## Things to edit before upload

1. Replace `AUTHORIZED_CARDS[]` with your real RFID UIDs
2. Tune:
   - `OUTER_SERVO_1_CLOSED_ANGLE`
   - `OUTER_SERVO_1_OPEN_ANGLE`
   - `OUTER_SERVO_2_CLOSED_ANGLE`
   - `OUTER_SERVO_2_OPEN_ANGLE`
   - `OUTER_SERVO_MOVE_MS`
   - `OUTER_DOOR_HOLD_MS`
3. Change `DHT_TYPE` if you use `DHT22`
4. Change `MOVE_SERVOS_ON_BOOT` if you want the gate forced closed at startup

## Important notes

- `RC522` and `DHT11` must use `3.3V`
- Both servos should use an external `5V` supply
- All grounds must be connected together
- This sketch does not use Wi-Fi, Bluetooth, Internet, or Blynk
