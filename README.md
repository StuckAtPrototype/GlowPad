# GlowPad

A multi-mode interactive pad built on the ESP32-S3 with 5 capacitive touch keys, 5 WS2812B LEDs, and a piezo buzzer. Supports four modes: a Pomodoro timer, a pentatonic piano, a Simon-style memory game, and a reaction speed game.

## Hardware

| Peripheral | Details |
|---|---|
| MCU | ESP32-S3-WROOM-1 |
| Touch inputs | 5 capacitive pads (CAP1-CAP5) on GPIO 4, 5, 3, 2, 1 |
| LEDs | 5x IN-PI15TAT5R5G5B (WS2812B) chain on GPIO 21 (via BSS138 level shifter) |
| Buzzer | Piezo on GPIO 48 (via transistor driver) |

## Modes

Switch modes by long-pressing keys 1-4 (the first four pads).

### 1. Pomodoro Timer (key 1 long press)

A focus timer with five preset durations mapped to the five keys:

| Key | Duration |
|---|---|
| 1 | 5 min |
| 2 | 10 min |
| 3 | 25 min |
| 4 | 45 min |
| 5 | 60 min |

LEDs show a green progress bar while running. After the timer completes, a 60-second grace period starts (pulsing green). If not dismissed, it enters a 60-second alert with pulsing red LEDs and an audible jingle.

**Idle LEDs:** cyan at 30%.

### 2. Piano (key 2 long press)

Each key plays a note from the C-major pentatonic scale:

| Key | Note | Frequency |
|---|---|---|
| 1 | C5 | 523 Hz |
| 2 | D5 | 587 Hz |
| 3 | E5 | 659 Hz |
| 4 | G5 | 784 Hz |
| 5 | A5 | 880 Hz |

The corresponding LED flashes its colour at full brightness while the note plays, then fades back to a dim rainbow idle pattern.

**Idle LEDs:** rainbow (red, yellow, green, cyan, blue) at 20%.

### 3. Memory Game (key 3 long press)

A Simon-style memory game. The system plays an ever-growing random sequence of lights and tones. The player must repeat the sequence by pressing the matching keys in order.

- Each round adds one element to the sequence
- Correct presses flash the key's colour and play its tone
- Wrong press or 3-second timeout ends the game
- Score (rounds completed) is displayed on the LEDs at game over

**Idle LEDs:** yellow at 30%.

### 4. Reaction Game (key 4 long press)

A speed-and-accuracy reaction test over 5 rounds.

1. A random LED lights up after a random delay (0.5-1.5 seconds)
2. Press the matching key as fast as possible
3. Pressing before the LED appears counts as a false start (no penalty, round restarts)
4. After 5 rounds, the score is displayed as 0-5 green LEDs (one per "good" round)

A "good" round requires pressing the correct key in under 450ms. Per-round times and the total time are printed to the serial console.

**Idle LEDs:** purple at 30%.

## Building

Requires [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/).

```bash
cd GlowPad/firmware
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

## Project Structure

```
GlowPad/firmware/main/
├── main.c              # Entry point, initialises peripherals and main loop
├── mode_manager.c/h    # Mode switching and dispatch
├── button.c/h          # Capacitive touch driver (ESP32-S3 touch sensor v2)
├── led.c/h             # High-level LED control (progress bar, pulsing, per-LED colour)
├── ws2812_control.c/h  # Low-level WS2812 RMT driver
├── piezo.c/h           # Piezo buzzer driver (LEDC PWM)
├── timer.c/h           # Pomodoro timer state machine
├── piano.c/h           # Piano mode
├── memory_game.c/h     # Simon-style memory game
├── reaction_game.c/h   # Reaction speed game
├── led_color_lib.c/h   # Colour generation utilities
└── serial_protocol.c/h # Serial protocol (placeholder)
```
