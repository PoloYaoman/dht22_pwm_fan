# DHT22 4-pin Fan Controller for Raspberry Pi Pico

This project demonstrates how to use a DHT22 temperature and humidity sensor to control a fan via PWM on a Raspberry Pi Pico. The fan speed is adjusted based on the temperature readings from the DHT22 sensor.

The threshold on activating the fan can be adjusted in `TEMP_THRESHOLD`. Max speed can be adjusted with `MAX_FAN_SPEED` in percents of max speed (typically 1900 rpm).


## Wiring

- **DHT22 Sensor**
  - VCC: Connect to 3.3V on the Pico
  - GND: Connect to GND on the Pico
  - DATA: Connect to a GPIO pin (e.g., GP2) on the Pico (use a 10kΩ pull-up resistor between DATA and VCC)

- **PWM Fan**
  - VCC: Connect to external 5V/12V deending on the type (do NOT use Pico 3.3V for the fan)
  - GND: Connect to GND (shared with Pico)
  - PWM: Connect to a PWM-capable GPIO pin on the Pico (e.g., GP15)
  - TACH: (optional) connect to any GPIO with 3.3V pull-up (typically 1kOhm)


## Compile

1. Make sure you have the [Pico SDK](https://github.com/raspberrypi/pico-sdk) installed and set up.
2. Clone this repository and initialize submodules if needed.
3. Create a `build` directory:
   ```bash
   mkdir build
   cd build
   ```
4. Run CMake to configure the project:
   ```bash
   cmake ..
   ```
5. Compile the project:
   ```bash
   ninja
   ```
   Or use the provided VS Code task: **Compile Project**


## Run / Flash

- To flash the compiled firmware to your Pico, use the **Run Project** or **Flash** tasks in VS Code, or run:
  ```bash
  # Using picotool (replace temp_sens.uf2 with your output file if different)
  picotool load temp_sens.uf2 -fx
  ```
  Or, hold the BOOTSEL button on your Pico, connect it to your computer, and drag-and-drop the `temp_sens.uf2` file onto the RPI-RP2 drive.


## File Structure

- `temp_sens.c` - Main application source
- `dht/` - DHT22 driver and PIO program by Valentin Milea <valentin.milea@gmail.com>
- `build/` - Build output directory


## Requirements
- Raspberry Pi Pico
- DHT22 sensor
- PWM-capable 4-pin fan
- CMake, Ninja, Pico SDK
