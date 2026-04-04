# SmartHome Complete FreeRTOS

Sketch: `SmartHome_Complete_FreeRTOS.ino`

## Features

- RFID RC522 opens and closes the outer gate with 2 servos
- A push button toggles the inner door servo
- Laser plus light sensor DO acts as a beam-break anti-theft sensor
- PIR analog output and smoke sensor monitor motion and smoke
- Buzzer sounds for fire or intrusion alarm
- LCD1602 I2C shows status
- DHT sensor shows temperature and humidity
- FreeRTOS runs the system in 3 tasks to fit Arduino Uno better

## Pin map

- `D2`: inner door button
- `D3`: inner door servo
- `D4`: buzzer
- `D5`: outer servo 1
- `D6`: outer servo 2
- `D7`: light sensor DO
- `D8`: laser enable (optional)
- `D9`: RC522 RST
- `D10`: RC522 SDA/SS
- `D11`: RC522 MOSI
- `D12`: RC522 MISO
- `D13`: RC522 SCK
- `A0`: smoke sensor AO
- `A1`: PIR AO
- `A2`: DHT data
- `A4`: LCD SDA
- `A5`: LCD SCL

## Libraries

- `Arduino_FreeRTOS_Library`
- `MFRC522`
- `LiquidCrystal_I2C`
- `DHT sensor library`
- `Servo`

## Things to edit before upload

1. `AUTHORIZED_CARDS[]` da duoc cai san voi UID the: `FB 5E F5 04`
2. Tune:
   - `SMOKE_THRESHOLD`
   - `PIR_THRESHOLD`
   - `LIGHT_SENSOR_BROKEN_LEVEL`
3. Tune servo angles:
   - `OUTER_SERVO_1_CLOSED_ANGLE`
   - `OUTER_SERVO_1_OPEN_ANGLE`
   - `OUTER_SERVO_2_CLOSED_ANGLE`
   - `OUTER_SERVO_2_OPEN_ANGLE`
   - `INNER_SERVO_CLOSED_ANGLE`
   - `INNER_SERVO_OPEN_ANGLE`
4. Change `LCD_I2C_ADDRESS` if your LCD uses `0x3F`
5. Change `DHT_TYPE` to `DHT22` if needed

## Important notes

- RC522 must use `3.3V`
- All servos should use external power
- All grounds must be connected together
- If the laser is powered directly from `5V`, set `USE_LASER_CONTROL_PIN` to `false`

