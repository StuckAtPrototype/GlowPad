# GlowPad

A multi-mode interactive pad built on the ESP32-S3 with 5 capacitive touch keys, 5 WS2812B LEDs, and a piezo buzzer. Supports five modes: a Pomodoro timer, a pentatonic piano, a Simon-style memory game, a reaction speed game, and a USB media-control (keyboard) mode for music players.

## Hardware

| Peripheral | Details |
|---|---|
| MCU | ESP32-S3-WROOM-1 |
| Touch inputs | 5 capacitive pads (CAP1-CAP5) on GPIO 4, 5, 3, 2, 1 |
| LEDs | 5x IN-PI15TAT5R5G5B (WS2812B) chain on GPIO 21 (via BSS138 level shifter) |
| Buzzer | Piezo (SMT1240SHT) on GPIO 48 (via transistor driver) |
| USB | Native USB-C (D+/D− on GPIO 20/19) for HID + programming |
| Power | AP2205 3.3V LDO |

### PCB / Design Files

The board is designed in [KiCad](https://www.kicad.org/) (v8+). Design sources and
manufacturing outputs live under `hardware/GlowPad/`:

| File / Folder | Contents |
|---|---|
| `GlowPad.kicad_pro` | KiCad project file |
| `GlowPad.kicad_sch` | Schematic |
| `GlowPad.kicad_pcb` | PCB layout |
| `GlowPad_BOM.csv` | Bill of materials |
| `gerber/` | Gerber + drill files for fabrication |

To open the design, launch KiCad and open `hardware/GlowPad/GlowPad.kicad_pro`.

#### Key components (see `GlowPad_BOM.csv` for the full BOM)

| Ref | Part | Function |
|---|---|---|
| U1 | ESP32-S3-WROOM-1 | MCU |
| U6 | AP2205-33 | 3.3V LDO regulator |
| Q1 | BSS138 | LED data level shifter |
| D5 | TPD1E10B06 | USB ESD protection |
| J1 | USB4105-GF-A | USB-C connector |
| LED1-5 | IN-PI15TAT5R5G5B | Addressable RGB LEDs |
| TCH1-5 | Capacitive pads | Touch keys |
| LS1 | SMT1240SHT | Piezo buzzer |
| SW1 / SW2 | RKB2 tactile | Reset / Boot (enable) |

## Modes

Switch modes by long-pressing keys 1–5 (pads 1–5).

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

### 5. Media Control (key 5 long press)

USB HID Consumer (media keys) mode. Connect GlowPad to the host via **native USB** (ESP32-S3 USB D+/D− on GPIO 20/19). The five pads act as media keys:

| Key | Action |
|---|---|
| 1 | Previous track |
| 2 | Play / Pause |
| 3 | Next track |
| 4 | Volume down |
| 5 | Volume up |

Works with most music players (Spotify, VLC, system media keys, etc.). Short press sends the key; the corresponding LED flashes for feedback.

**Idle LEDs:** orange/amber at 30%.

**Note:** Media mode requires the `esp_tinyusb` component (pulled in automatically) and HID enabled in menuconfig (`CONFIG_TINYUSB_HID_ENABLED`). The firmware uses a custom HID Consumer report descriptor.

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
GlowPad/
├── firmware/                # ESP-IDF application
│   └── main/
│       ├── main.c              # Entry point, initialises peripherals and main loop
│       ├── mode_manager.c/h    # Mode switching and dispatch
│       ├── button.c/h          # Capacitive touch driver (ESP32-S3 touch sensor v2)
│       ├── led.c/h             # High-level LED control (progress bar, pulsing, per-LED colour)
│       ├── ws2812_control.c/h  # Low-level WS2812 RMT driver
│       ├── piezo.c/h           # Piezo buzzer driver (LEDC PWM)
│       ├── timer.c/h           # Pomodoro timer state machine
│       ├── piano.c/h           # Piano mode
│       ├── memory_game.c/h     # Simon-style memory game
│       ├── reaction_game.c/h   # Reaction speed game
│       ├── media_control.c/h   # USB HID media keys mode
│       ├── led_color_lib.c/h   # Colour generation utilities
│       └── serial_protocol.c/h # Serial protocol (placeholder)
└── hardware/
    └── GlowPad/             # KiCad project (schematic, PCB, BOM, gerbers)
```

## License

See [LICENSE](LICENSE).
