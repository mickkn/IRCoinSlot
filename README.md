# IRCoinSlot - Optical Simple Coin Slot for DIY Arcade Machines
IR Coin Slot is a simple and effective coin detection system designed for DIY arcade machines. 
It utilizes an IR reflective sensor to detect coins as they pass through the slot, providing 
reliable and accurate coin detection for various coin sizes and materials. The design is 
compact and easy to integrate into arcade machines, making it an ideal choice for hobbyists 
and DIY enthusiasts looking to add coin-operated functionality to their projects.

## Features
- Detects coins using an IR reflective sensor.
- Each player's IR detection sends at most one credit every 2 seconds. Sustained detection repeats credits until the sensor clears, allowing sensitivity adjustment without debug output. The first detection after startup is accepted immediately; detections during cooldown do not extend it. Coin-insert buttons retain their separate 50 ms release debounce.
- Simple design for easy integration into DIY arcade machines.
- Compatible with u32u4-based microcontrollers.
- Provides reliable coin detection for various coin sizes.
- Adjustable sensitivity for the sensor to accommodate different coin materials.
- Compact and durable design suitable for arcade environments.
- Easy to install and maintain.
- USB interface for easy connectivity to microcontrollers and arcade machine systems.
- Keyboard emulation support for seamless integration with arcade machine software.
- Coin-insert buttons for both players send the same credit key and LED animation as the IR sensors, with one credit per debounced press.

## Specifications
- Sensor Type: ITR20001/T IR Reflective Sensor
- Coin Detection Range: 10mm to 20mm
- Power Supply: 5V USB
- Output: USB HID (Keyboard Emulation)
- Dimensions: 50mm x 100mm x 4mm

## Concept image

![Concept Image](img/image.png)

## Installation
1. Make 1 x 28mm holes in your arcade machine's coin slot area to mount the coin slot securely.
2. Use the measurement guide provided to ensure proper alignment of the second hole for the coin slot.
3. Install the coin guides on the inside of the arcade machine in the 2 x 28mm holes.
4. Align the coin slot front plate with the holes and secure it in place using screws or adhesive.
5. Mount the IR sensor in the designated square hole in the coin guides, ensuring it is properly positioned to detect coins passing through the slot.
6. Connect the sensor to the microcontroller in the proper IO ports as per the wiring diagram provided in the documentation. Wire a 10 kohm pull-up resistor from the phototransistor's collector to 5V, tie its emitter to GND, and take the analog signal from the collector. The input idles HIGH and is pulled down when a coin reflects IR light back into the sensor.
7. Connect each 10 kohm sensitivity potentiometer's outer terminals to 5V and GND, and its wiper to the player's sensitivity input (A0 for Player 1, A3 for Player 2).
8. Flash the microcontroller with the appropriate firmware to enable coin detection functionality.
9. Test the coin slot by inserting coins and verifying that the sensor detects them correctly and sends the appropriate signals to the microcontroller.
10. With no coin present, turn the sensitivity potentiometer fully toward its 5V end (maximum sensitivity). Credits will register every 2 seconds. Slowly turn it back toward GND until credits stop registering on the arcade, then slightly farther to avoid ambient-light noise. Wait longer than 2 seconds at each setting to confirm credits have stopped. Insert coins to confirm they still register, spacing them more than 2 seconds apart. No Serial Monitor or debug messages are needed. The GND end stop disables IR detection. Which physical direction increases sensitivity depends on how the pot's outer terminals are wired. Adjustment adds real credits; clear those in the arcade software afterward if needed. The start LED keeps fading after a credit until Start is pressed, so its fading is not an indication of current sensor detection.

## Debug Serial output

Each accepted IR coin detection logs the exact ADC sample that triggered it, for example `1234 ms P1: IR coin detected; coin_adc=420`.

With `DEBUG` set to `true`, open the Serial Monitor at 115200 baud to see timestamped coin detections, coin-button presses, start-button presses, keys sent, and LED fading start/stop events. Every two seconds, an `AIN` readout shows each enabled player's raw coin sensor value, raw sensitivity potentiometer value, calculated threshold, release threshold, and detection status. The firmware never waits on Serial: debug text is queued in a 512-byte buffer and sent only as fast as USB accepts it. Text queued before a Serial Monitor connects is kept until one does. If the buffer fills, newer text is dropped and counted; once the queued text has been sent, a `[N debug bytes dropped]` line marks where text was lost. Set `DEBUG` to `false` to disable Serial output.

The sensitivity potentiometer adjusts the absolute detection threshold, not an offset from a calibrated idle reading. Its raw `sensitivity_pot_adc` value (0-1023) maps to `detection_threshold_adc` (0-1024), covering the entire sensor range. At pot ADC 0, IR detection is off, including any previously latched detection. At pot ADC 1023, detection stays `yes` even if the sensor reads 1023. Any sustained detection sends a credit every 2 seconds, including at maximum sensitivity, with a coin held in front of the sensor, or when ambient light exceeds the selected sensitivity. Brief coin passages normally produce one credit. A detection that begins during cooldown can register once cooldown expires if it is still active; one that clears before then is ignored. The optional `detected` readout is diagnostic only; sensitivity can be adjusted using the arcade's credit counter with `DEBUG` disabled.

The phototransistor is wired with a pull-up on its collector and the signal taken from that collector, so `coin_adc` idles HIGH and is pulled down as reflected IR light increases. Detection occurs when `coin_adc` drops **below** the threshold. Higher thresholds make detection more sensitive; lower thresholds require a stronger sensor signal. To reject sensor noise, detection uses up to 30 ADC units of hysteresis (`k_coin_hysteresis`). After detection, the reading must reach `release_threshold_adc` to clear detection and stop repeated credits. For thresholds below 1023, the release threshold is threshold + 30 capped at 1023, so it remains reachable. At the maximum-sensitivity end stop it is deliberately 1024, keeping detection active until the knob is turned back. `below_threshold_adc` shows the signed difference between the detection threshold and the coin reading; positive means the reading is below the detection threshold. Hysteresis can keep `detected=yes` even with a small negative difference. Adjustments take effect on every sensor poll; the updated values print every two seconds.

## Pinouts

Set `P1_ENABLE` or `P2_ENABLE` in the firmware to `true` to enable Player 1 or Player 2. When a player is disabled, its pins are not configured, its inputs are not polled, and no keyboard events, LED updates or debug readouts are generated for that player. The other player is unaffected.

Connect each optional coin-insert button between its input pin and GND: `P1_COIN_BUT` on pin 7 and `P2_COIN_BUT` on pin 15. Internal pull-ups are enabled. A press sends Player 1's credit key (`5`) or Player 2's credit key (`6`) and starts that player's repeating LED fade. Release the button for at least 50 ms before pressing again.

All buttons are interrupt-driven: `P1_BUT` (pin 2, INT1), `P1_COIN_BUT` (pin 7, INT6), `P2_BUT` (pin 14, PCINT3) and `P2_COIN_BUT` (pin 15, PCINT1). The interrupt records a press as soon as it happens, even while the firmware is busy, and the main loop sends the key right away. Any button pin you change must also be interrupt-capable on the ATmega32u4: pins 0, 1, 2, 3 or 7, or pin-change pins 8–11 or 14–17.

Each enabled player's LED uses PWM (`P1_LED` pin 3, `P2_LED` pin 5) and is off at startup. An accepted IR coin or coin-button press starts continuous fading from minimum brightness up to full brightness and back, with a 2-second cycle (`k_dim_duration_ms`). The fade stays between PWM 1 (`k_led_min`) and 255, never reaching zero, so PWM remains active at the bottom of the cycle. A start-button press sends one start key (`1` or `2`) immediately, stops that player's fading, and turns the LED off. Holding Start does not repeat the key; release it for at least 50 ms before pressing again. A new credit starts the fading again.

### Pin headers out from CoinSlot Controller Box

```text
                         Player 1

   Start LED  o--- D3             D2 ---o  Start Button
         GND  o--- GND           GND ---o  GND
          5V  o--- VCC            5V ---o  5V
      Unused  o--- GND           GND ---o  GND
 Coin Button  o--- D7             A1 ---o  IR Sensor

                         Player 2

   Start LED  o--- D5            D14 ---o  Start Button
         GND  o--- GND           GND ---o  GND
          5V  o--- VCC            5V ---o  5V
      Unused  o--- GND           GND ---o  GND
 Coin Button  o--- D15            A2 ---o  IR Sensor
```

### Wiring Diagram

Arduino Pro Micro / ATmega32u4, **5V / 16 MHz**. Pin labels below match the firmware, not the physical order of the board's headers. Connect all grounds together.

```text
                         Pro Micro (5V)
                    +----------------------+
 P1 start LED <-----| D3 (PWM)    D5 (PWM) |-----> P2 start LED
 P1 start switch ---| D2               D14 |--- P2 start switch
 P1 coin switch ----| D7               D15 |---- P2 coin switch
 P1 sensor OUT -----| A1                A2 |----- P2 sensor OUT
 P1 pot wiper ------| A0                A3 |------ P2 pot wiper
                    | VCC (5V)         GND |
                    +----------------------+

 Start and coin switches (each):
   Input pin --------o    o-------- GND
                     switch
   Internal pull-up enabled; pressing connects the input to GND.

 Start button LED (each, bare LED):
   PWM pin ----[330 ohm]---- LED anode (+)
                             LED cathode (-) ---- GND

 Sensitivity potentiometer (each, 10 kohm linear):
   5V --------[ 10 kohm pot ]-------- GND
                      |
                    wiper
                      |
              A0 (P1) or A3 (P2)
```

### ITR20001/T sensor wiring (one circuit per player)

The sensor contains a separate IR LED and phototransistor. Wire by terminal function below; verify the physical lead orientation against the [Everlight datasheet](https://everlightamericas.com/datasheets/ITR20001_T.pdf) before connecting it.

```text
 IR emitter:

   5V ----[220 ohm]---- Anode (A)
                        IR LED
   GND ---------------- Cathode (K)

 Reflected-light detector:

   5V ----[10 kohm]----+---- OUT to A1 (P1) or A2 (P2)
                       |
                       C
                 Phototransistor
                       E
                       |
                      GND
```

| Component                             | Value per player | Purpose                                                                            |
|---------------------------------------|------------------|------------------------------------------------------------------------------------|
| IR LED series resistor                | 220 ohm, 1/4 W   | Limits emitter current to roughly 17 mA at 5V with a typical 1.2V forward drop.    |
| Phototransistor pull-up               | 10 kohm, 1/4 W   | Pulls OUT toward 5V when little IR light is reflected.                             |
| Sensitivity potentiometer             | 10 kohm linear   | Sets the firmware threshold through A0 or A3; not in series with the sensor.       |
| Bare start-button LED series resistor | 330 ohm, 1/4 W   | Limits LED current; choose for the actual LED's forward voltage and rated current. |

Use a separate resistor for each LED and each sensor output. The **220 ohm resistor belongs in series with the IR LED**, not in place of the phototransistor's **10 kohm pull-up**. When a coin reflects IR light, the phototransistor pulls OUT lower; the firmware detects this falling analog signal.

The start-button LED circuit assumes a bare low-current LED suitable for direct GPIO drive. A button with a built-in resistor may not need another series resistor; a higher-current or 12V lamp needs a suitable driver rather than a direct GPIO connection.