// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    SB_ACTION_NONE, SB_ACTION_UP, SB_ACTION_DOWN, SB_ACTION_LEFT, SB_ACTION_RIGHT,
    SB_ACTION_CONFIRM, SB_ACTION_FIRE, SB_ACTION_ROTATE, SB_ACTION_CANCEL,
    SB_ACTION_NEXT_PANEL, SB_ACTION_HELP, SB_ACTION_OWN_FLEET,
    SB_ACTION_ENEMY_VIEW, SB_ACTION_PAUSE, SB_ACTION_RADIO_STATUS,
    SB_ACTION_CONTEXT, SB_ACTION_BACKSPACE, SB_ACTION_DELETE, SB_ACTION_HOME, SB_ACTION_END,
    SB_ACTION_TEXT_BASE=256,
} sb_action_t;
typedef enum { SB_INPUT_PRESS, SB_INPUT_RELEASE, SB_INPUT_REPEAT } sb_input_phase_t;
typedef struct { sb_action_t action; sb_input_phase_t phase; } sb_input_event_t;

#define SB_INPUT_REPEAT_DELAY_MS 400
#define SB_INPUT_REPEAT_INTERVAL_MS 80

// Single application-task owner. No BSP types escape this interface.
esp_err_t sb_input_init(void);
// Discard queued input and suppress currently held keys until released.
// Call on every screen transition, including after the startup splash.
void sb_input_reset(void);
// Nonblocking; call regularly (the application uses a 10 ms idle yield).
// Only arrows repeat. Space aliases emit one aggregate press/release pair.
// Other release events are per physical binding.
// Text actions encode an ASCII byte as SB_ACTION_TEXT_BASE + byte.
// Text mode isolates characters/edit keys from gameplay actions.
void sb_input_set_text_mode(bool enabled);
bool sb_input_poll(sb_input_event_t *event);
const char *sb_input_action_name(sb_action_t action);
