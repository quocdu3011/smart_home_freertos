# SmartHome Uno Safety

Sketch: `SmartHome_UNO_Safety.ino`

## Features

- Inner door button toggles the inner door servo
- Laser and light sensor DO work as a beam-break intrusion detector
- PIR analog output and smoke sensor monitor motion and smoke
- Local buzzer handles fire and intrusion alarm
- LCD1602 I2C support is optional
- This module works independently from the ESP32 access module

## Pin map

- `D2`: inner door button
- `D3`: inner door servo
- `D4`: buzzer
- `D7`: light sensor DO
- `D8`: laser enable
- `A0`: smoke sensor AO
- `A1`: PIR AO
- `A4`: LCD SDA
- `A5`: LCD SCL

## Libraries

- `Arduino_FreeRTOS_Library`
- `Servo`
- `LiquidCrystal_I2C` when `USE_LCD` is `1`

## Things to edit before upload

1. Tune:
   - `SMOKE_THRESHOLD`
   - `PIR_THRESHOLD`
   - `LIGHT_SENSOR_BROKEN_LEVEL`
2. Tune the inner door servo:
   - `INNER_SERVO_CLOSED_ANGLE`
   - `INNER_SERVO_OPEN_ANGLE`
   - `INNER_SERVO_MOVE_MS`
3. Change `LCD_I2C_ADDRESS` if your LCD uses `0x3F`
4. Change `USE_LCD` to `1` if you want the LCD enabled
5. Change `MOVE_SERVO_ON_BOOT` if you want the door forced closed at startup

## Important notes

- This sketch is local only and does not use Wi-Fi, Bluetooth, Internet, or Blynk
- If the laser is powered directly from `5V`, set `USE_LASER_CONTROL_PIN` to `false`
- The inner door servo should use an external `5V` supply
- All grounds must be connected together
