#pragma once

extern int mic_amplify; // software gain: 256 = 1x (full int16 range), 32 = 8x, 8 = 32x

void audio_capture_init(void);
int audio_capture_read(void);
