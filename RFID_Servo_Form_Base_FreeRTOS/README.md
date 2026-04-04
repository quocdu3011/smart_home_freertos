# RFID Servo Form Base FreeRTOS

Sketch: `RFID_Servo_Form_Base_FreeRTOS.ino`

## Chuc nang

- Dung `RC522` de doc the RFID
- Dung 2 servo de mo/dong cong
- Viet theo dung form FreeRTOS mau:
  - khai bao prototype task
  - tao task trong `setup()`
  - de trong `loop()`
  - dinh nghia task o phia duoi

## Pin map

- `D5`: Servo 1
- `D6`: Servo 2
- `D9`: RC522 RST
- `D10`: RC522 SDA/SS
- `D11`: RC522 MOSI
- `D12`: RC522 MISO
- `D13`: RC522 SCK

## UID hien tai

- `FB 5E F5 04`

## Can chinh neu servo quay nguoc

- `SERVO_1_CLOSED_ANGLE`
- `SERVO_1_OPEN_ANGLE`
- `SERVO_2_CLOSED_ANGLE`
- `SERVO_2_OPEN_ANGLE`

## Ghi chu

- `RC522` phai cap `3.3V`
- 2 servo nen dung nguon `5V` ngoai
- nho noi `GND` chung giua nguon ngoai va `Arduino Uno`
