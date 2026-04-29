#pragma once

#include <stdint.h>
#include "cthugha.h"

#define LCD_H_RES CONFIG_CTHUGHA_LCD_H_RES
#define LCD_V_RES CONFIG_CTHUGHA_LCD_V_RES
#define SCALE_FACTOR (LCD_H_RES / BUFF_WIDTH)

void display_init(void);
void display_render(void);
