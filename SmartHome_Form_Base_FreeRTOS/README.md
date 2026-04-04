# SmartHome Form Base FreeRTOS

Sketch: `SmartHome_Form_Base_FreeRTOS.ino`

## Muc tieu

- Viet lai theo dung form FreeRTOS mau:
  - khai bao prototype task
  - tao task trong `setup()`
  - de trong `loop()`
  - phan dinh nghia task o phia duoi

## Task trong sketch

- `TaskAccessControl`
  - doc RFID
  - dieu khien 2 servo cua ngoai
  - doc nut bam va servo cua trong

- `TaskSensorsAndAlarm`
  - doc cam bien khoi AO
  - doc cam bien hong ngoai thu dong AO
  - doc cam bien anh sang DO
  - dieu khien coi bao dong

- `TaskDisplayAndClimate`
  - doc DHT
  - cap nhat LCD
  - in debug ra Serial
  - nhap nhay LED built-in

## Thu vien can co

- `FreeRTOS` co file `Arduino_FreeRTOS.h`
- `MFRC522`
- `Servo`
- `LiquidCrystal_I2C`
- `DHT sensor library`

## Can chinh truoc khi nap

- `SMOKE_THRESHOLD`
- `PIR_THRESHOLD`
- `LIGHT_SENSOR_BROKEN_LEVEL`
- cac goc cua 3 servo
- `LCD_I2C_ADDRESS` neu LCD cua ban la `0x3F`
- `DHT_TYPE` neu ban dung `DHT22`

## UID hien tai

- `FB 5E F5 04`
