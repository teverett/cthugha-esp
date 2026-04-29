//
// Cthugha ESP32-P4 Port — Translation/remap effects
// Spatial remapping of the framebuffer using precomputed lookup tables
// Original: Zaph, Digital Aasvogel Group, Torps Productions 1993-1995
//

#include "cthugha.h"
#include <math.h>

#define MAXTRANS 8

int nrtrans = 0;
int translate_idx = 0;

static uint16_t *trans_maps[MAXTRANS];
static int current_loaded = -1;

// Built-in procedural translation maps
static void gen_swirl(uint16_t *map, float strength)
{
    int cx = BUFF_WIDTH / 2;
    int cy = BUFF_HEIGHT / 2;
    for (int y = 0; y < (int)BUFF_HEIGHT; y++) {
        for (int x = 0; x < (int)BUFF_WIDTH; x++) {
            float dx = (float)(x - cx);
            float dy = (float)(y - cy);
            float dist = sqrtf(dx * dx + dy * dy);
            float angle = strength / (dist + 1.0f);
            float cs = cosf(angle);
            float sn = sinf(angle);
            int sx = (int)(cs * dx - sn * dy + cx);
            int sy = (int)(sn * dx + cs * dy + cy);
            sx = ct_clamp(sx, 0, BUFF_WIDTH - 1);
            sy = ct_clamp(sy, 0, BUFF_HEIGHT - 1);
            map[y * BUFF_WIDTH + x] = (uint16_t)(sy * BUFF_WIDTH + sx);
        }
    }
}

static void gen_tunnel(uint16_t *map)
{
    int cx = BUFF_WIDTH / 2;
    int cy = BUFF_HEIGHT / 2;
    for (int y = 0; y < (int)BUFF_HEIGHT; y++) {
        for (int x = 0; x < (int)BUFF_WIDTH; x++) {
            float dx = (float)(x - cx);
            float dy = (float)(y - cy);
            float dist = sqrtf(dx * dx + dy * dy);
            float scale = (dist > 1.0f) ? (1.0f - 2.0f / dist) : 0.0f;
            int sx = (int)(dx * scale + cx);
            int sy = (int)(dy * scale + cy);
            sx = ct_clamp(sx, 0, BUFF_WIDTH - 1);
            sy = ct_clamp(sy, 0, BUFF_HEIGHT - 1);
            map[y * BUFF_WIDTH + x] = (uint16_t)(sy * BUFF_WIDTH + sx);
        }
    }
}

static void gen_fisheye(uint16_t *map)
{
    int cx = BUFF_WIDTH / 2;
    int cy = BUFF_HEIGHT / 2;
    float max_r = sqrtf((float)(cx * cx + cy * cy));
    for (int y = 0; y < (int)BUFF_HEIGHT; y++) {
        for (int x = 0; x < (int)BUFF_WIDTH; x++) {
            float dx = (float)(x - cx);
            float dy = (float)(y - cy);
            float dist = sqrtf(dx * dx + dy * dy);
            float r = dist / max_r;
            float nr = r * r;
            int sx = (int)(dx * nr / (r + 0.001f) + cx);
            int sy = (int)(dy * nr / (r + 0.001f) + cy);
            sx = ct_clamp(sx, 0, BUFF_WIDTH - 1);
            sy = ct_clamp(sy, 0, BUFF_HEIGHT - 1);
            map[y * BUFF_WIDTH + x] = (uint16_t)(sy * BUFF_WIDTH + sx);
        }
    }
}

static void gen_ripple(uint16_t *map)
{
    int cx = BUFF_WIDTH / 2;
    int cy = BUFF_HEIGHT / 2;
    for (int y = 0; y < (int)BUFF_HEIGHT; y++) {
        for (int x = 0; x < (int)BUFF_WIDTH; x++) {
            float dx = (float)(x - cx);
            float dy = (float)(y - cy);
            float dist = sqrtf(dx * dx + dy * dy);
            float wave = sinf(dist * 0.15f) * 4.0f;
            float angle = atan2f(dy, dx);
            int sx = (int)(x + wave * cosf(angle));
            int sy = (int)(y + wave * sinf(angle));
            sx = ct_clamp(sx, 0, BUFF_WIDTH - 1);
            sy = ct_clamp(sy, 0, BUFF_HEIGHT - 1);
            map[y * BUFF_WIDTH + x] = (uint16_t)(sy * BUFF_WIDTH + sx);
        }
    }
}

void init_translate(void)
{
    nrtrans = 0;
    for (int i = 0; i < MAXTRANS; i++)
        trans_maps[i] = NULL;

    // Allocate and generate built-in maps
    for (int i = 0; i < 4; i++) {
        trans_maps[i] = (uint16_t *)malloc(BUFF_SIZE * sizeof(uint16_t));
        if (!trans_maps[i]) break;
        nrtrans++;
    }

    if (nrtrans >= 1) gen_swirl(trans_maps[0], 3.0f);
    if (nrtrans >= 2) gen_tunnel(trans_maps[1]);
    if (nrtrans >= 3) gen_fisheye(trans_maps[2]);
    if (nrtrans >= 4) gen_ripple(trans_maps[3]);

    translate_idx = 0;
}

void translate_screen(void)
{
    if (nrtrans <= 0 || translate_idx <= 0 || translate_idx > nrtrans)
        return;

    uint16_t *map = trans_maps[translate_idx - 1];
    if (!map) return;

    buff[0] = 0;

    for (unsigned int i = 0; i < BUFF_SIZE; i++)
        shadow[i] = buff[map[i]];

    flip_screens();
}
