# Serial Basic Test

Sketch: `Serial_Basic_Test.ino`

## How to use

1. Upload this sketch to the Arduino Uno
2. Open Serial Monitor
3. Set baud rate to `9600`
4. The built-in LED on pin 13 should blink once per second
5. Serial Monitor should repeatedly print:
   - `UNO serial test booted.`
   - `Heartbeat: board is running.`

## If nothing appears

- Check `Tools -> Board -> Arduino Uno`
- Check `Tools -> Port`
- Try another USB cable
- Try another USB port
- Disconnect high-current loads such as servos while testing
