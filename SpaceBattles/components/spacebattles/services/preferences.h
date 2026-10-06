// SPDX-License-Identifier: MIT
#pragma once
#include "services/config.h"
// Effective diagnostic gain, 0..100. A future mixer must apply only the
// channel volume: the BSP already applies master volume to its output.
unsigned sb_audio_gain(const sb_settings_t *settings, bool music);
unsigned sb_frame_interval_ms(const sb_settings_t *settings);
// 0=off, 1=reduced, 2=full. Consumed by future animation playback.
unsigned sb_animation_level(const sb_settings_t *settings);
