# RFID Gate Only

Sketch: `RFID_Gate_Only.ino`

## Functions

- Read RFID card using RC522
- Print card UID to Serial Monitor
- Open gate if card is authorized
- Close gate automatically after a short delay

## Libraries

- `MFRC522`
- `Servo`

## Pins

- `D5`: Servo 1
- `D6`: Servo 2
- `D8`: RC522 RST
- `D10`: RC522 SDA/SS
- `D11`: RC522 MOSI
- `D12`: RC522 MISO
- `D13`: RC522 SCK

## Before upload

1. Replace UID values in `AUTHORIZED_CARDS[]`
2. Adjust servo angles:
   - `SERVO_1_CLOSED_ANGLE`
   - `SERVO_1_OPEN_ANGLE`
   - `SERVO_2_CLOSED_ANGLE`
   - `SERVO_2_OPEN_ANGLE`
3. Open Serial Monitor at `115200`
4. Scan a card and copy its UID into `AUTHORIZED_CARDS[]`

## Important notes

- RC522 uses `3.3V`, not `5V`
- Servos should use external power
- Connect all GND pins together
