# Cthugha ESP32-P4

An ESP32-P4 port of **Cthugha v5.3** — the classic 1993 real-time audio
visualizer ("An Oscilloscope on Acid") by Zaph / Digital Aasvogel Group /
Torps Productions.

Captures audio from an onboard MEMS microphone, processes it through the
original flame, wave, and translation effects, and renders to a 720x720
MIPI-DSI touchscreen at full framerate.

## Target Hardware

**Board:** ESP32-P4-WIFI6-Touch-LCD-4B (Waveshare)

| Component | Spec |
|-----------|------|
| MCU | ESP32-P4, dual-core RISC-V @ 400 MHz |
| Display | 4" 720x720 RGB LCD, MIPI-DSI interface |
| Touch | Capacitive multi-touch (GT911, I2C) |
| Microphone | Onboard SMD MEMS mic (I2S) |
| RAM | 32 MB PSRAM |
| Flash | 16 MB |

## Prerequisites

### ESP-IDF Toolchain

Install **ESP-IDF v5.3 or later** — this is the minimum version with
ESP32-P4 support.

Follow the official guide for your OS:
https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/get-started/

**Linux / macOS:**

```bash
mkdir -p ~/esp
cd ~/esp
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32p4
source export.sh
```

**Windows (ESP-IDF PowerShell):**

Use the ESP-IDF Tools Installer from
https://dl.espressif.com/dl/esp-idf/ — it sets up Python, CMake, Ninja,
and the RISC-V cross-compiler. After installation, open the
"ESP-IDF 5.4 PowerShell" shortcut.

### Verify Installation

```bash
idf.py --version          # should print 5.3.x or later
riscv32-esp-elf-gcc -v    # RISC-V cross-compiler for ESP32-P4
```

## Configure

### 1. Set the target chip

```bash
idf.py set-target esp32p4
```

This creates `sdkconfig` from `sdkconfig.defaults` with the correct CPU,
PSRAM, and peripheral settings.

### 2. Board-specific pin configuration

Open menuconfig:

```bash
idf.py menuconfig
```

Navigate to **Cthugha Configuration** and verify/adjust the GPIO
assignments for your specific board revision. The three sub-menus are:

**MIPI-DSI Display**

| Setting | Default | Description |
|---------|---------|-------------|
| LCD horizontal resolution | 720 | Panel width in pixels |
| LCD vertical resolution | 720 | Panel height in pixels |
| DPI pixel clock (MHz) | 36 | Pixel clock for DPI output |
| MIPI-DSI lane bitrate (Mbps) | 500 | Per-lane data rate |
| Number of MIPI-DSI data lanes | 2 | 1, 2, or 4 |
| HSYNC back/front porch | 20 | Horizontal blanking |
| HSYNC pulse width | 4 | |
| VSYNC back/front porch | 20 | Vertical blanking |
| VSYNC pulse width | 4 | |
| Backlight GPIO | 26 | LCD backlight enable pin |

**I2S Microphone**

| Setting | Default | Description |
|---------|---------|-------------|
| I2S peripheral number | 0 | I2S port (0 or 1) |
| I2S bit clock GPIO | 34 | BCK / SCK pin |
| I2S word select GPIO | 35 | WS / LRCK pin |
| I2S data in GPIO | 36 | SD / DOUT pin (mic output → ESP input) |
| Sample rate (Hz) | 44100 | Audio capture rate |

**I2C Touch**

| Setting | Default | Description |
|---------|---------|-------------|
| I2C peripheral number | 0 | I2C port (0 or 1) |
| I2C SDA GPIO | 7 | Data line |
| I2C SCL GPIO | 8 | Clock line |
| Touch reset GPIO | 5 | GT911 reset pin |
| Touch interrupt GPIO | 6 | GT911 interrupt pin |

> **Important:** The default GPIO numbers are starting estimates. Check
> the schematic for your board revision and correct them before building.

### 3. LCD panel controller initialization

The MIPI-DSI display requires an initialization command sequence specific
to the LCD controller IC on the panel (e.g. JD9365, ST7701S). This
sequence must be added to the `display_init()` function in
`main/display.c` at the marked `TODO`. Consult the panel datasheet or
the Waveshare BSP examples for the correct sequence.

## Build

```bash
idf.py build
```

The first build downloads the managed component `esp_lcd_touch_gt911`
automatically from the Espressif Component Registry.

Build output goes to `build/`. The firmware binary is
`build/cthugha_esp.bin`.

## Flash & Monitor

Connect the board via USB and run:

```bash
idf.py -p PORT flash monitor
```

Replace `PORT` with the serial port for your board:
- **Linux:** `/dev/ttyUSB0` or `/dev/ttyACM0`
- **macOS:** `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`
- **Windows:** `COM3` (check Device Manager)

To exit the serial monitor, press `Ctrl+]`.

### Flash only (no monitor)

```bash
idf.py -p PORT flash
```

### Monitor only (no flash)

```bash
idf.py -p PORT monitor
```

## Touch Controls

Touch gestures replace the original keyboard controls:

| Gesture | Action |
|---------|--------|
| Tap | Cycle wave renderer |
| Swipe right | Next flame effect |
| Swipe left | Next color palette |
| Swipe up | Next display mode (mirror/kaleidoscope) |
| Swipe down | Next translation effect (swirl/tunnel/etc.) |
| Long press | Toggle lock (freezes current effect combination) |

## How It Works

The rendering pipeline runs as a FreeRTOS task at ~60 fps:

```
┌─────────┐   ┌──────────┐   ┌──────┐   ┌───────────┐   ┌─────────┐   ┌─────────────┐
│  Flame  │──▶│  Audio   │──▶│ Wave │──▶│ Translate │──▶│ Display │──▶│ LCD Output  │
│ (scroll/│   │ (I2S mic │   │(draw │   │ (spatial  │   │ (mirror/│   │ (palette +  │
│  blur)  │   │  capture)│   │ audio│   │  remap)   │   │  rotate)│   │  3x scale)  │
└─────────┘   └──────────┘   │ data)│   └───────────┘   └─────────┘   └─────────────┘
                              └──────┘
240x240 @ 8-bit indexed ──────────────────────────────────────────────▶ 720x720 RGB565
```

The internal 240x240 framebuffer uses 8-bit indexed color with a 256-entry
palette. The LCD output stage looks up each pixel in the current palette
to get RGB888, converts to RGB565, and replicates each pixel in a 3x3
block to fill the 720x720 display.

## Effects

**15 flame effects** — directional blur/scroll variations that create the
trailing visual persistence (Up Slow, Up Subtle, Up Fast, Left/Right
variants, Water, Skyline, Weird, Fade, Zzz)

**24 wave renderers** — different ways to map audio waveform data onto the
buffer (dots, lines, spikes, Lissajous figures, lightning, fireflies,
fractal walkers)

**8 display modes** — buffer transformations applied before LCD output
(upward, downward, horizontal split, kaleidoscope, 90-degree rotated
mirrors)

**4 translation effects** — spatial remapping through precomputed lookup
tables (swirl, tunnel, fisheye, ripple)

**8 color palettes** — procedural gradients (Royal Purple, Fire, Ocean,
Acid, Sunset, Ice, Rainbow, Hot Metal)

## Project Structure

```
cthugha_esp/
├── CMakeLists.txt          # Top-level ESP-IDF project file
├── sdkconfig.defaults      # Default SDK configuration for ESP32-P4
├── main/
│   ├── CMakeLists.txt      # Component registration
│   ├── idf_component.yml   # Managed component dependencies
│   ├── Kconfig.projbuild   # Menuconfig options (GPIO pins, etc.)
│   ├── cthugha.h           # Core types, buffer constants, externs
│   ├── main.c              # Entry point, FreeRTOS render task, touch handling
│   ├── flames.c            # 15 flame effects (ported from x86 ASM)
│   ├── waves.c             # 24 wave renderers (ported from MODES.C + PETE.C)
│   ├── palettes.c/h        # 8 procedural color palettes
│   ├── translate.c         # 4 spatial remap effects
│   ├── display.c/h         # MIPI-DSI LCD driver, display modes, 3x scaling
│   ├── audio_capture.c/h   # I2S MEMS microphone capture
│   └── touch_input.c/h     # GT911 capacitive touch with gesture detection
```

## Credits

Original Cthugha v5.3 by Zaph / Digital Aasvogel Group / Torps
Productions, 1993-1995. Source released under a non-commercial open
source license — see the original `CTHUGHA.H` header for terms.
