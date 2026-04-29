# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

ESP32-P4 port of Cthugha v5.3, a real-time audio visualization program ("An Oscilloscope on Acid"). Originally a DOS program by Zaph / Digital Aasvogel Group / Torps Productions (1993-1995), ported to run on the ESP32-P4-WIFI6-Touch-LCD-4B development board with its 720x720 MIPI-DSI display and onboard MEMS microphone.

## Build

Requires ESP-IDF v5.3+ with ESP32-P4 target support.

```bash
idf.py set-target esp32p4
idf.py menuconfig    # Configure GPIO pins under "Cthugha Configuration"
idf.py build
idf.py flash monitor
```

GPIO pin assignments for the specific board must be configured via `idf.py menuconfig` → Cthugha Configuration. Defaults are estimates — check the board schematic.

## Architecture

### Rendering Pipeline

Internal framebuffer is 240x240 @ 8-bit indexed color (57,600 bytes), scaled 3x to 720x720 RGB565 for the LCD. Each frame:

1. **Flame** (flames.c) — scrolls/blurs the buffer via directional pixel averaging through `divsub[]` lookup. All 15 flame effects ported from original x86 inline assembly to portable C.
2. **Audio** (audio_capture.c) — reads I2S MEMS microphone, fills `stereo[240][2]` with 0-255 normalized samples. Mono mic is duplicated to both channels.
3. **Wave** (waves.c) — maps audio data onto the buffer as visual patterns (24 wave renderers ported from MODES.C and PETE.C).
4. **Translation** (translate.c) — optional spatial remapping via precomputed lookup tables (4 procedural effects: swirl, tunnel, fisheye, ripple).
5. **Display effect** (display.c) — buffer transforms (mirror, rotate, kaleidoscope — 8 modes from DISPLAY.C).
6. **LCD output** (display.c `display_render()`) — palette lookup + 3x nearest-neighbor scaling to 720x720 RGB565, sent to MIPI-DSI DPI panel.

### Key Differences from Original

- No DOS-specific code (no far pointers, no inline x86 asm, no EMS/XMS, no VGA ports)
- `divsub[]` lookup table replaces inline assembly flame math: `divsub[i] = max(0, i/4 - 1)`
- Audio source is I2S microphone instead of Sound Blaster/GUS/PAS DMA
- Touch gestures replace keyboard: tap=wave, swipe-right=flame, swipe-left=palette, swipe-up=display, swipe-down=translate, long-press=lock
- Palettes are procedurally generated RGB888 (not ported verbatim from 6-bit VGA data)
- Square 240x240 internal buffer (original was 320x204)

### Board-Specific Configuration

The MIPI-DSI display requires LCD controller IC initialization commands that depend on the specific panel IC (JD9365, ST7701S, etc.). These must be added to `display.c` `display_init()` after determining the panel IC from the board schematic. The `TODO` marker in that function indicates where to add them.
