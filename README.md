# SIT315 M1.T1 – Interrupt-Driven Room Security Monitor

A Sense-Think-Act system for the Arduino Uno, built and simulated in Tinkercad. It combines an external interrupt, pin change interrupts and a timer interrupt.

## What it does

| Input | Pin | How it is read | Effect |
|---|---|---|---|
| PIR motion sensor | D2 | External interrupt (`attachInterrupt`, RISING) | Raises the alarm, but only if the system is armed |
| Slide switch (arm/disarm) | D4 | Polled in `loop()` | Arms or disarms; disarming clears the alarm |
| Door pushbutton | D8 | Pin change interrupt (PCINT0) | Zone LED on while door or window is open |
| Window pushbutton | D9 | Pin change interrupt (PCINT1) | Shares one interrupt vector with the door |

| Output | Pin | Meaning |
|---|---|---|
| Red LED | D5 | Alarm: motion detected while armed |
| Yellow LED | D6 | Zone: door OR window open |
| Green LED | D7 | Heartbeat, toggled every second by Timer1 |

Timer1 also prints a status line to the Serial Monitor every 5 seconds.

## Design

- Every ISR only sets a flag, copies a value or increments a counter. No ISR uses `delay()` or Serial.
- All decisions, LED writes and Serial output happen in `loop()`, split into `handleArmSwitch()`, `handleMotion()`, `handleZones()`, `handleTimer()` and `updateOutputs()`.
- Variables shared between ISRs and `loop()` are declared `volatile`.
- D8 and D9 are both on PORTB, so they share `PCINT0_vect`. The ISR captures `PINB` and `loop()` compares it with the stored state to find which pin changed.
- Timer1 runs in CTC mode with a 1024 prescaler and `OCR1A = 15624`, giving a 1 Hz interrupt on a 16 MHz Uno.

## Wiring

- LEDs: D5, D6, D7 → 220 Ω resistor → LED anode; LED cathode → GND rail
- Pushbuttons: one leg → D8 / D9; diagonally opposite leg → GND rail (internal pull-ups are used)
- Slide switch: common terminal → D4; one outer terminal → GND rail
- PIR: Signal → D2, Power → 5V, Ground → GND rail
- Arduino GND → breadboard GND rail

See the circuit diagram in this repository.

## How to run

1. Open the circuit in Tinkercad (or build it from the wiring list above).
2. Click **Code**, choose **Text**, and paste in the contents of `TaskM1.cpp`.
3. Open the **Serial Monitor** and click **Start Simulation**.
4. Try the three test conditions:
   - Motion while disarmed: logged as ignored.
   - Arm the switch, then cause motion: alarm on, red LED lit. Disarm to clear.
   - Press the door and window buttons separately and together: each is logged, yellow LED on while either is held.

## Files

- `TaskM1.cpp` – source code
- Circuit diagram and Serial Monitor screenshots
- Reflection report (PDF)
