#pragma once

#include <stdbool.h>

typedef enum {
    TOUCH_NONE = 0,
    TOUCH_TAP,
    TOUCH_SWIPE_LEFT,
    TOUCH_SWIPE_RIGHT,
    TOUCH_SWIPE_UP,
    TOUCH_SWIPE_DOWN,
    TOUCH_LONG_PRESS,
    TOUCH_DOUBLE_TAP,
} touch_gesture_t;

void touch_input_init(void);
touch_gesture_t touch_input_poll(void);
