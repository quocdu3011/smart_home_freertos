# FreeRTOS Serial Debug

Sketch: `FreeRTOS_Serial_Debug.ino`

## Purpose

- Verify `Arduino_FreeRTOS` boots correctly on `Arduino Uno R3`
- Verify `Serial Monitor` is working
- Verify the board is not resetting or hanging

## How to test

1. Upload this sketch to `Arduino Uno R3`
2. Open `Serial Monitor`
3. Set baud rate to `9600`
4. Expect these lines first:
   - `BOOT: FreeRTOS serial debug starting`
   - `SETTING: baud = 9600`
   - `EXPECT: heartbeat every 1 second`
5. After that, expect repeated lines like:
   - `HB 0 | FreeRTOS serial is alive`
   - `HB 1 | FreeRTOS serial is alive`

## If nothing appears

- Check that the selected board is `Arduino Uno`
- Check that the selected COM port is correct
- Try another USB cable
- Close other apps that may be using the same COM port
- Press the reset button once after opening `Serial Monitor`
- If this sketch also prints nothing, the problem is very likely not in your application code
