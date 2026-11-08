# Smart Pet Feeder

An Arduino Uno-based automatic pet feeder with scheduled dispensing, manual feeding, adjustable portion size, a real-time clock, keypad configuration, bowl/hopper status input, buzzer feedback, and a 20x4 I2C LCD interface.

## Project contents

- `sketch.ino` — Arduino firmware.
- `diagram.json` — Wokwi circuit definition.
- `libraries.txt` — required Arduino libraries for Wokwi.
- `wokwi-project.txt` — source project reference.

## Main features

- Scheduled food dispensing using an RTC.
- Manual dispensing using a pushbutton or keypad menu.
- Configure the clock, feed schedule, and portion size from a 4x4 keypad.
- Save schedule and portion settings in the Arduino Uno EEPROM.
- Display time, date, bowl status, menus, and dispensing progress on a 20x4 I2C LCD.
- Audible confirmation and hopper-empty warning.
- Stepper motor driven through an A4988 driver.

## Hardware and pin map

| Component | Arduino Uno connection |
|---|---|
| 20x4 I2C LCD SDA | A4 |
| 20x4 I2C LCD SCL | A5 |
| DS1307 RTC SDA | A4 (shared I2C bus) |
| DS1307 RTC SCL | A5 (shared I2C bus) |
| 4x4 keypad rows R1-R4 | D8, D9, D10, D11 |
| 4x4 keypad columns C1-C4 | A0, A1, A2, A3 |
| A4988 STEP | D12 |
| A4988 DIR | D13 |
| Manual feed button | D2 to GND (internal pull-up) |
| Buzzer | D3 and GND |
| Bowl/food-present switch | D4; switch closes to GND when food is present |
| A4988 ENABLE | GND (active-low enable) |
| A4988 RESET and SLEEP | Tied together |
| A4988 VDD | Uno 5V |
| A4988 logic GND | Uno GND |
| Stepper motor coils | A4988 1A/1B and 2A/2B outputs |

The I2C LCD and DS1307 RTC share the Uno's I2C bus. Their addresses are different (LCD commonly `0x27`; DS1307 `0x68`).

## RTC compatibility note

The supplied Wokwi diagram uses the **DS1307** RTC component. The firmware has been aligned to that component by using `RTC_DS1307` and its `isrunning()` check from RTClib. If you replace the module with a DS3231 in physical hardware, change the firmware RTC object back to `RTC_DS3231` and use the DS3231-specific power-loss check.

## Libraries

Install these libraries through Arduino IDE Library Manager (or use Wokwi's library manager):

- RTClib by Adafruit
- LiquidCrystal I2C
- Keypad

`Wire.h` and `EEPROM.h` are included with the Arduino AVR core.

## Wokwi simulation

1. Open [Wokwi](https://wokwi.com/).
2. Create an Arduino Uno project.
3. Replace the contents of `sketch.ino` and `diagram.json` with the files in this repository.
4. Add the libraries listed in `libraries.txt` if Wokwi does not resolve them automatically.
5. Start the simulation. Use the slide switch to simulate bowl/food-present status, the green pushbutton for manual feeding, and the keypad to open the settings menu (`A`).

Original Wokwi project reference: https://wokwi.com/projects/472697572380087297

## Important hardware safety notes

- **Do not power the stepper motor from the Arduino 5V pin.** For a real A4988 installation, supply VMOT from a suitable external motor supply within the A4988 carrier's specified range (commonly 8–35 V), connect the motor-supply ground to Arduino ground, and install the recommended bulk capacitor close to VMOT/GND. Set the A4988 current limit for the actual motor before use.
- The Wokwi A4988 model does not use VMOT for its simulation; therefore the VMOT pin is intentionally not wired to the Uno's 5V rail in this simulation diagram.
- Verify the bowl switch mechanics and motor direction before loading food. Keep fingers and loose objects away from the feeder mechanism.
- The code's portion setting is a number of step pulses, not a calibrated mass. Calibrate the mechanism with the actual auger/wheel, food type, and motor before unattended operation.
- The sketch uses blocking delays while dispensing and in menu input routines. Test the full operating sequence before deploying it unattended.

## Default settings

- First-run schedule: 08:00.
- First-run portion: 400 step pulses.
- The settings are stored in the Uno's EEPROM.

## License

MIT License. See `LICENSE`.
