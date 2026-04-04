# SmartHome FreeRTOS for Arduino Uno R3

Sketch file: `SmartHome_FreeRTOS.ino`

## Features

- RFID RC522 opens the gate
- Two gate servos share one signal pin
- Fire alarm uses flame sensor + smoke sensor
- Anti-theft uses laser + light sensor
- Drying rack retracts when rain is detected
- LCD1602 I2C shows system state
- 74HC595 drives status LEDs / buzzer output
- FreeRTOS separates logic into 4 tasks

## Libraries to install in Arduino IDE

- `Arduino_FreeRTOS_Library`
- `MFRC522`
- `LiquidCrystal_I2C`
- `Servo`

## What you must edit before upload

1. Replace the sample RFID UIDs in `AUTHORIZED_CARDS[]`
2. Tune:
   - `SMOKE_THRESHOLD`
   - `RAIN_THRESHOLD`
   - `LDR_BREAK_MARGIN`
3. Tune gate servo angles:
   - `GATE_CLOSED_ANGLE`
   - `GATE_OPEN_ANGLE`
4. Tune drying rack motor times:
   - `RACK_RETRACT_MS`
   - `RACK_EXTEND_MS`

## Important wiring notes

- RC522 must use `3.3V`
- Servos and the DC motor should use an external power supply
- Connect all grounds together
- 74HC595 should not drive the motor or servos directly
- If your LCD does not respond, change `LCD_I2C_ADDRESS` from `0x27` to `0x3F`

## Suggested 74HC595 outputs

- `Q0`: Fire LED
- `Q1`: Security LED
- `Q2`: Rain LED
- `Q3`: Gate LED
- `Q4`: Access granted LED
- `Q5`: Buzzer through transistor
- `Q6`: Heartbeat LED
- `Q7`: Spare
