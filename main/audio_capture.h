#pragma once

extern int mic_amplify; // current AGC gain (read-only; computed each frame by peak envelope follower)

void audio_capture_init(void);
int audio_capture_read(void);
