// SPDX-License-Identifier: MIT
#include "input/input.h"
#include "bsp/input.h"
#include "esp_timer.h"
#include <stddef.h>
#include <string.h>

typedef struct { int key; sb_action_t action; bool scancode; } binding_t;
#define NAV(key, action) {BSP_INPUT_NAVIGATION_KEY_##key, SB_ACTION_##action, false}
static const binding_t bindings[] = {
    NAV(UP, UP), NAV(DOWN, DOWN), NAV(LEFT, LEFT), NAV(RIGHT, RIGHT),
    NAV(RETURN, CONFIRM), NAV(SPACE_L, FIRE), NAV(SPACE_M, FIRE), NAV(SPACE_R, FIRE),
    {BSP_INPUT_SCANCODE_R, SB_ACTION_ROTATE, true},
    NAV(ESC, CANCEL), NAV(BACKSPACE, CANCEL), NAV(TAB, NEXT_PANEL),
    NAV(F1, HELP), NAV(F2, OWN_FLEET), NAV(F3, ENEMY_VIEW), NAV(F4, PAUSE),
    NAV(F5, RADIO_STATUS), NAV(F6, CONTEXT), NAV(HOME, HOME), NAV(END, END),
};
#define BINDING_COUNT (sizeof(bindings) / sizeof(bindings[0]))
static struct { bool held, blocked; int64_t next_repeat; } keys[BINDING_COUNT];
static QueueHandle_t events;
static size_t repeat_cursor;
static bool text_mode;
static sb_action_t action_for(size_t i) {
    if(text_mode && !bindings[i].scancode && bindings[i].key==BSP_INPUT_NAVIGATION_KEY_BACKSPACE) return SB_ACTION_BACKSPACE;
    return bindings[i].action;
}
void sb_input_set_text_mode(bool enabled) {
    if(text_mode!=enabled) { text_mode=enabled; sb_input_reset(); }
}

static esp_err_t read_key(size_t i, bool *down) {
    return bindings[i].scancode ? bsp_input_read_scancode(bindings[i].key, down)
                               : bsp_input_read_navigation_key(bindings[i].key, down);
}

void sb_input_reset(void) {
    if (events) xQueueReset(events);
    memset(keys, 0, sizeof(keys));
    repeat_cursor = 0;
    for (size_t i = 0; i < BINDING_COUNT; ++i) {
        bool down = false;
        // If the query fails, require a release before trusting this binding.
        keys[i].blocked = read_key(i, &down) != ESP_OK || down;
    }
}

esp_err_t sb_input_init(void) {
    events = NULL;
    esp_err_t err = bsp_input_get_queue(&events);
    if (err != ESP_OK) { events = NULL; return err; }
    if (!events) return ESP_ERR_INVALID_STATE;
    sb_input_reset();
    return ESP_OK;
}

static bool transition(size_t i, bool down, sb_input_event_t *out) {
    if (keys[i].blocked) {
        if (!down) keys[i].blocked = false;
        return false;
    }
    // A held Space binding at a screen boundary blocks the whole logical
    // Space key, including another physical alias, until all are released.
    if (bindings[i].action == SB_ACTION_FIRE) {
        for (size_t k = 0; k < BINDING_COUNT; ++k) {
            if (bindings[k].action == SB_ACTION_FIRE && keys[k].blocked) {
                keys[i].blocked = down;
                return false;
            }
        }
    }
    if (keys[i].held == down) return false;
    bool fire_was_down = false;
    if (bindings[i].action == SB_ACTION_FIRE)
        for (size_t k = 0; k < BINDING_COUNT; ++k)
            if (bindings[k].action == SB_ACTION_FIRE) fire_was_down |= keys[k].held;
    keys[i].held = down;
    if (bindings[i].action == SB_ACTION_FIRE) {
        bool fire_is_down = false;
        for (size_t k = 0; k < BINDING_COUNT; ++k)
            if (bindings[k].action == SB_ACTION_FIRE) fire_is_down |= keys[k].held;
        if (fire_was_down == fire_is_down) return false;
    }
    keys[i].next_repeat = esp_timer_get_time() + SB_INPUT_REPEAT_DELAY_MS * 1000LL;
    *out = (sb_input_event_t){action_for(i), down ? SB_INPUT_PRESS : SB_INPUT_RELEASE};
    return true;
}

bool sb_input_poll(sb_input_event_t *out) {
    if (!events || !out) return false;
    *out = (sb_input_event_t){0};
    bsp_input_event_t raw;
    // Bound work under a noisy producer. Preserve queued short taps; state
    // reconciliation must only happen after the queue has been consumed.
    for (unsigned n = 0; n < 64; ++n) {
        if (xQueueReceive(events, &raw, 0) != pdTRUE) goto reconcile;
        if(text_mode && raw.type==INPUT_EVENT_TYPE_KEYBOARD) {
            unsigned char c=(unsigned char)raw.args_keyboard.ascii;
            if((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c==' ' || c=='_' || c=='-') {
                *out=(sb_input_event_t){(sb_action_t)(SB_ACTION_TEXT_BASE+c),SB_INPUT_PRESS}; return true;
            }
            continue;
        }
        if(text_mode && raw.type==INPUT_EVENT_TYPE_SCANCODE &&
            raw.args_scancode.scancode==BSP_INPUT_SCANCODE_ESCAPED_GREY_DEL) {
            *out=(sb_input_event_t){SB_ACTION_DELETE,SB_INPUT_PRESS}; return true;
        }
        for (size_t i = 0; i < BINDING_COUNT; ++i) {
            if(text_mode && (bindings[i].scancode || bindings[i].action==SB_ACTION_FIRE ||
                bindings[i].action==SB_ACTION_HELP || bindings[i].action==SB_ACTION_PAUSE ||
                bindings[i].action==SB_ACTION_CONTEXT || bindings[i].action==SB_ACTION_RADIO_STATUS ||
                bindings[i].action==SB_ACTION_OWN_FLEET || bindings[i].action==SB_ACTION_ENEMY_VIEW)) continue;
            bool down;
            if (bindings[i].scancode) {
                if (raw.type != INPUT_EVENT_TYPE_SCANCODE) continue;
                unsigned code = raw.args_scancode.scancode;
                if ((code & ~BSP_INPUT_SCANCODE_RELEASE_MODIFIER) != (unsigned)bindings[i].key) continue;
                down = !(code & BSP_INPUT_SCANCODE_RELEASE_MODIFIER);
            } else {
                if (raw.type != INPUT_EVENT_TYPE_NAVIGATION ||
                    (int)raw.args_navigation.key != bindings[i].key) continue;
                down = raw.args_navigation.state;
            }
            if (transition(i, down, out)) return true;
            break;
        }
    }
    return false;

reconcile:
    // Recover lost releases, but never synthesize a destructive press from a
    // snapshot. A missed press is suppressed until the next physical press.
    for (size_t i = 0; i < BINDING_COUNT; ++i) {
        bool down = false;
        if (read_key(i, &down) != ESP_OK) {
            bool held = keys[i].held;
            keys[i].held = false;
            keys[i].blocked = true;
            bool another_fire = false;
            if (bindings[i].action == SB_ACTION_FIRE)
                for (size_t k = 0; k < BINDING_COUNT; ++k)
                    if (bindings[k].action == SB_ACTION_FIRE) another_fire |= keys[k].held;
            if (held && !another_fire) { *out = (sb_input_event_t){action_for(i), SB_INPUT_RELEASE}; return true; }
        } else if (!down) {
            if (transition(i, false, out)) return true;
        } else if (!keys[i].held) {
            keys[i].blocked = true;
        }
    }
    int64_t now = esp_timer_get_time();
    // Fair service for simultaneous arrows; no catch-up bursts after a stall.
    for (size_t n = 0; n < (text_mode ? 5u : 4u); ++n) {
        size_t slot=(repeat_cursor+n)%(text_mode ? 5u : 4u);
        size_t i=slot==4 ? 10 : slot;
        if (keys[i].held && now >= keys[i].next_repeat) {
            keys[i].next_repeat = now + SB_INPUT_REPEAT_INTERVAL_MS * 1000LL;
            repeat_cursor = (slot + 1) % (text_mode ? 5u : 4u);
            *out = (sb_input_event_t){action_for(i), SB_INPUT_REPEAT};
            return true;
        }
    }
    return false;
}

const char *sb_input_action_name(sb_action_t action) {
    static const char *const names[] = {"NONE", "UP", "DOWN", "LEFT", "RIGHT", "CONFIRM",
        "FIRE / PLACE", "ROTATE", "CANCEL", "NEXT PANEL", "HELP", "OWN FLEET",
        "ENEMY VIEW", "PAUSE", "RADIO STATUS", "CONTEXT"};
    return (unsigned)action < sizeof(names)/sizeof(names[0]) ? names[action] : "UNKNOWN";
}
