# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

ESP32-P4 port of Cthugha v5.3, a real-time audio visualization program ("An Oscilloscope on Acid"). Originally a DOS program by Zaph / Digital Aasvogel Group / Torps Productions (1993-1995), ported to run on the **ESP32-P4-WIFI6-Touch-LCD-4B** (Waveshare) development board.

The original DOS source lives at `../cthugha` for reference.

## Target Board

**ESP32-P4-WIFI6-Touch-LCD-4B** — dual-core RISC-V @ 360 MHz, 32 MB PSRAM, 32 MB flash.

| Function | GPIO | Notes |
|----------|------|-------|
| LCD Backlight | 26 | **Active LOW** (0 = on) |
| LCD Reset | 27 | ST7703 hardware reset |
| I2S MCLK | 13 | Master clock, 384× sample rate |
| I2S BCLK | 12 | Bit clock |
| I2S WS | 10 | Word select |
| I2S DIN | 11 | Mic data in |
| I2S DOUT | 9 | Speaker data out (unused) |
| PA Enable | 53 | Speaker amp enable (unused) |
| I2C SDA | 7 | GT911 touch + other I2C |
| I2C SCL | 8 | |
| Touch RST | 5 | GT911 reset |
| Touch INT | 6 | GT911 interrupt |

## Build

Requires **ESP-IDF v5.5.1+** targeting ESP32-P4.

```bash
idf.py set-target esp32p4
idf.py build
idf.py -p PORT flash monitor
```

GPIO pins are configurable via `idf.py menuconfig` → "Cthugha Configuration", but defaults match this board.

First build downloads managed components automatically: `waveshare/esp_lcd_st7703`, `espressif/esp_lcd_touch_gt911`.

## Architecture

### Rendering Pipeline

Internal framebuffer is 240×240 @ 8-bit indexed color (57,600 bytes), scaled 3× to 720×720 RGB565 for the LCD. Each frame in the render loop (`render_task` in main.c, pinned to core 0):

1. **Flame** (flames.c) — scrolls/blurs the buffer via directional pixel averaging through `divsub[]` lookup table. 15 effects ported from original x86 inline assembly.
2. **Audio** (audio_capture.c) — reads I2S MEMS microphone into `stereo[240][2]` as 0–255 normalized samples. Mono mic duplicated to both channels.
3. **Wave** (waves.c) — maps audio data onto the buffer as visual patterns. 24 renderers ported from MODES.C and PETE.C.
4. **Translation** (translate.c) — optional spatial remapping via precomputed uint16_t lookup tables. 4 procedural effects (swirl, tunnel, fisheye, ripple).
5. **Display effect** (display.c) — buffer transforms before output (mirror, rotate, kaleidoscope). 8 modes from DISPLAY.C.
6. **LCD output** (display.c `display_render()`) — palette lookup into `pal_lut[256]` (RGB565), 3× nearest-neighbor scaling, written directly into DPI panel framebuffer, then `draw_bitmap` to swap buffers on vsync.

### Display Driver (display.c)

Uses `waveshare/esp_lcd_st7703` managed component. Key initialization sequence:

1. LDO channel 3 @ 2500 mV powers MIPI DSI PHY
2. DSI bus via `ST7703_PANEL_BUS_DSI_2CH_CONFIG()` macro
3. DBI command IO via `ST7703_PANEL_IO_DBI_CONFIG()` macro
4. DPI config via `ST7703_720_720_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565)`
5. Panel created with `esp_lcd_new_panel_st7703()`, vendor config has `.use_mipi_interface = 1`
6. Vsync sync via `on_color_trans_done` ISR callback → semaphore
7. Framebuffers obtained via `esp_lcd_dpi_panel_get_frame_buffer()` (DMA-capable memory, **not** PSRAM — PSRAM buffers cause draw_bitmap to hang because DMA can't reach them)

The render loop writes directly into the back framebuffer, then calls `draw_bitmap` with that pointer — the DPI panel recognizes its own buffer and swaps without copying.

### Audio Capture (audio_capture.c)

I2S standard mode, mono, 16-bit, default 16 kHz sample rate. Uses `i2s_channel_init_std_mode()` (not the removed `_rx_mode` variant — that was dropped in ESP-IDF v5.5). MCLK multiply = 384×.

### Touch Input (touch_input.c)

GT911 capacitive touch over I2C master bus. Gesture detection: tap, double-tap, swipe (L/R/U/D), long press. Gestures mapped to mode changes in `handle_touch()` in main.c.

### Key Data Structures

- `buff` / `shadow` — two 240×240 uint8_t framebuffers, swapped via `flip_screens()`
- `divsub[1024]` — flame fade LUT: `divsub[i] = max(0, i/4 - 1)`. "slow" flames sum 4 neighbors (0–1020 range), "subtle" variants use wrapping uint8_t sums (0–255)
- `table[10][256]` — audio-to-color mapping curves (V-shape, ramp, inverse, threshold, etc.)
- `stereo[240][2]` — per-pixel audio amplitude, 0–255
- `LUTbuffer[768]` — current palette (256 × RGB888)
- `pal_lut[256]` — palette converted to RGB565 for LCD output
- `sine_table[240]` — precomputed sine values for wave effects
- `lcd_fb[2]` — DPI panel's own double-buffered 720×720 RGB565 framebuffers

### Effect Selection

All effects use the `function_opt` struct pattern: array of `{function_ptr, flag, name}` terminated by a NULL entry. Function pointer tables: `flamearray[]`, `wavearray[]`, `disparray[]`. Modes cycle via `change_flame()`, `change_wave()`, `change_display()`. Auto-randomization via timer in the render loop when not locked.

## Key Constants (cthugha.h)

- `BUFF_WIDTH` = 240, `BUFF_HEIGHT` = 240, `BUFF_SIZE` = 57600, `BUFF_BOTTOM` = 236
- `NUMTABLES` = 10, `LUTSIZE` = 768, `MAXLUTS` = 30

## Key Constants (display.h)

- `LCD_H_RES` = 720, `LCD_V_RES` = 720, `LCD_BPP` = 16, `SCALE_FACTOR` = 3

## sdkconfig.defaults

ESP32-P4 @ 360 MHz, SPIRAM @ 200 MHz, `IDF_EXPERIMENTAL_FEATURES` enabled (required for MIPI-DSI), task watchdog disabled, FreeRTOS @ 1000 Hz, perf-optimized compiler.

## Pitfalls / Lessons Learned

- **PSRAM buffers + DPI draw_bitmap**: Allocating the LCD render buffer from PSRAM (`MALLOC_CAP_SPIRAM`) causes `draw_bitmap` to never complete — the DMA transfer-done callback never fires and the render loop blocks forever on `xSemaphoreTake`. Always use the panel's own framebuffers via `esp_lcd_dpi_panel_get_frame_buffer()`.
- **I2S API v5.5**: `i2s_channel_init_std_rx_mode()` was removed in ESP-IDF v5.5 — use `i2s_channel_init_std_mode()`.
- **Backlight polarity**: LCD backlight is active LOW on this board (GPIO 26 = 0 to turn on).
- **Kconfig vs hardcoded**: LCD resolution (720×720) and MIPI-DSI timing are hardcoded via ST7703 macros, not Kconfig — the panel driver handles all timing. Only GPIO pins and I2S sample rate are in Kconfig.
- **sdkconfig v5.5.1 incompatibilities**: `SPIRAM_MODE_QUAD`, `SPIRAM_FETCH_INSTRUCTIONS`, `SPIRAM_RODATA` don't exist on this target/version. CPU max is 360 MHz (not 400).
