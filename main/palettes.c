//
// Cthugha ESP32-P4 Port — Palette system
// Procedurally generated palettes inspired by the original Cthugha v5.3 built-ins
//

#include "cthugha.h"

uint8_t LUTbuffer[LUTSIZE];
uint8_t LUTfiles[MAXLUTS][LUTSIZE];
int numluts = 0;
int curpal = 0;

static void set_entry(uint8_t *pal, int idx, uint8_t r, uint8_t g, uint8_t b)
{
    pal[idx * 3 + 0] = r;
    pal[idx * 3 + 1] = g;
    pal[idx * 3 + 2] = b;
}

static uint8_t lerp8(uint8_t a, uint8_t b, int pos, int total)
{
    return (uint8_t)(a + (int)(b - a) * pos / total);
}

static void gradient(uint8_t *pal, int start, int end,
                     uint8_t r0, uint8_t g0, uint8_t b0,
                     uint8_t r1, uint8_t g1, uint8_t b1)
{
    int span = end - start;
    if (span <= 0) return;
    for (int i = 0; i <= span; i++) {
        set_entry(pal, start + i,
                  lerp8(r0, r1, i, span),
                  lerp8(g0, g1, i, span),
                  lerp8(b0, b1, i, span));
    }
}

// Palette 0: Royal Purple (adapted from original)
static void gen_royal_purple(uint8_t *pal)
{
    set_entry(pal, 0, 0, 0, 0);
    gradient(pal, 1, 63, 60, 0, 80, 120, 0, 160);
    gradient(pal, 64, 127, 124, 4, 160, 252, 252, 252);
    gradient(pal, 128, 191, 252, 252, 252, 252, 252, 0);
    gradient(pal, 192, 255, 252, 248, 0, 120, 0, 0);
}

// Palette 1: Classic Fire
static void gen_fire(uint8_t *pal)
{
    set_entry(pal, 0, 0, 0, 0);
    gradient(pal, 1, 64, 32, 0, 0, 200, 0, 0);
    gradient(pal, 65, 128, 200, 0, 0, 255, 200, 0);
    gradient(pal, 129, 192, 255, 200, 0, 255, 255, 100);
    gradient(pal, 193, 255, 255, 255, 100, 255, 255, 255);
}

// Palette 2: Ocean Blue
static void gen_ocean(uint8_t *pal)
{
    set_entry(pal, 0, 0, 0, 0);
    gradient(pal, 1, 64, 0, 0, 40, 0, 0, 160);
    gradient(pal, 65, 128, 0, 0, 160, 0, 128, 255);
    gradient(pal, 129, 192, 0, 128, 255, 128, 255, 255);
    gradient(pal, 193, 255, 128, 255, 255, 255, 255, 255);
}

// Palette 3: Green Acid
static void gen_acid(uint8_t *pal)
{
    set_entry(pal, 0, 0, 0, 0);
    gradient(pal, 1, 64, 0, 20, 0, 0, 120, 0);
    gradient(pal, 65, 128, 0, 120, 0, 0, 255, 0);
    gradient(pal, 129, 192, 0, 255, 0, 200, 255, 100);
    gradient(pal, 193, 255, 200, 255, 100, 255, 255, 255);
}

// Palette 4: Sunset
static void gen_sunset(uint8_t *pal)
{
    set_entry(pal, 0, 0, 0, 0);
    gradient(pal, 1, 64, 40, 0, 60, 150, 0, 100);
    gradient(pal, 65, 128, 150, 0, 100, 255, 80, 0);
    gradient(pal, 129, 192, 255, 80, 0, 255, 220, 0);
    gradient(pal, 193, 255, 255, 220, 0, 255, 255, 200);
}

// Palette 5: Ice
static void gen_ice(uint8_t *pal)
{
    set_entry(pal, 0, 0, 0, 0);
    gradient(pal, 1, 85, 0, 0, 20, 100, 100, 200);
    gradient(pal, 86, 170, 100, 100, 200, 200, 220, 255);
    gradient(pal, 171, 255, 200, 220, 255, 255, 255, 255);
}

// Palette 6: Rainbow
static void gen_rainbow(uint8_t *pal)
{
    set_entry(pal, 0, 0, 0, 0);
    gradient(pal, 1, 42, 255, 0, 0, 255, 255, 0);
    gradient(pal, 43, 85, 255, 255, 0, 0, 255, 0);
    gradient(pal, 86, 128, 0, 255, 0, 0, 255, 255);
    gradient(pal, 129, 170, 0, 255, 255, 0, 0, 255);
    gradient(pal, 171, 213, 0, 0, 255, 255, 0, 255);
    gradient(pal, 214, 255, 255, 0, 255, 255, 255, 255);
}

// Palette 7: Hot Metal
static void gen_hot_metal(uint8_t *pal)
{
    set_entry(pal, 0, 0, 0, 0);
    gradient(pal, 1, 85, 0, 0, 0, 200, 0, 0);
    gradient(pal, 86, 170, 200, 0, 0, 255, 200, 0);
    gradient(pal, 171, 255, 255, 200, 0, 255, 255, 255);
}

void init_palettes(void)
{
    gen_royal_purple(LUTfiles[0]);
    gen_fire(LUTfiles[1]);
    gen_ocean(LUTfiles[2]);
    gen_acid(LUTfiles[3]);
    gen_sunset(LUTfiles[4]);
    gen_ice(LUTfiles[5]);
    gen_rainbow(LUTfiles[6]);
    gen_hot_metal(LUTfiles[7]);
    numluts = 8;
}

void fill_lut_buffer(int pal)
{
    if (pal >= 0) {
        curpal = pal % numluts;
        memcpy(LUTbuffer, LUTfiles[curpal], LUTSIZE);
    }
}
