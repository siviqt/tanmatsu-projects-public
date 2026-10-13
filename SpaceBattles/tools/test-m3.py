#!/usr/bin/env python3
"""Real input manager, pinned BSP header; mock queue, time and key snapshots."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="sb-m3-") as tmp:
    p = Path(tmp)
    (p / 'freertos').mkdir()
    (p / 'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_STATE 2\n')
    (p / 'esp_timer.h').write_text('#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
    (p / 'freertos/FreeRTOS.h').write_text('#pragma once\n#define pdTRUE 1\n')
    (p / 'freertos/queue.h').write_text('#pragma once\ntypedef void *QueueHandle_t;\nint xQueueReceive(QueueHandle_t,void *,unsigned);\nint xQueueReset(QueueHandle_t);\n')
    (p / 'test.c').write_text(r'''
#include <assert.h>
#include <stdio.h>
#include "input/input.c"
static bsp_input_event_t queue[1024];
static unsigned head, tail;
static bool physical[BINDING_COUNT], query_fail, null_queue;
static int init_error;
static int64_t now;
int64_t esp_timer_get_time(void) { return now; }
int xQueueReset(QueueHandle_t q) { assert(q); head=tail=0; return 1; }
int xQueueReceive(QueueHandle_t q, void *e, unsigned timeout) {
    assert(q && !timeout);
    if(head==tail) return 0;
    *(bsp_input_event_t *)e=queue[head++]; return 1;
}
esp_err_t bsp_input_get_queue(QueueHandle_t *q) { *q=null_queue ? NULL : (void*)1; return init_error; }
esp_err_t bsp_input_read_navigation_key(bsp_input_navigation_key_t k, bool *s) {
    for(size_t i=0;i<BINDING_COUNT;++i) if(!bindings[i].scancode && bindings[i].key==(int)k) {
        *s=physical[i]; return query_fail ? ESP_FAIL : ESP_OK;
    }
    assert(0); return ESP_FAIL;
}
esp_err_t bsp_input_read_scancode(bsp_input_scancode_t k, bool *s) {
    assert(k==BSP_INPUT_SCANCODE_R); *s=physical[8]; return query_fail ? ESP_FAIL : ESP_OK;
}
static void push(size_t i, bool down) {
    physical[i]=down;
    bsp_input_event_t e={0};
    if(bindings[i].scancode) {
        e.type=INPUT_EVENT_TYPE_SCANCODE;
        e.args_scancode.scancode=bindings[i].key | (down ? 0 : BSP_INPUT_SCANCODE_RELEASE_MODIFIER);
    } else {
        e.type=INPUT_EVENT_TYPE_NAVIGATION;
        e.args_navigation.key=bindings[i].key; e.args_navigation.state=down;
    }
    assert(tail<1024); queue[tail++]=e;
}
static void expect_at(sb_action_t action, sb_input_phase_t phase, unsigned line) {
    sb_input_event_t e;
    if(!sb_input_poll(&e)) { fprintf(stderr,"Missing action %u at test line %u\n",action,line); assert(0); }
    assert(e.action==action && e.phase==phase);
}
#define expect(a,p) expect_at(a,p,__LINE__)
static void empty(void) { sb_input_event_t e; assert(!sb_input_poll(&e)); }
static void fresh(void) { memset(physical,0,sizeof(physical)); query_fail=false; now=0; sb_input_reset(); }
int main(void) {
    sb_input_event_t e;
    assert(!sb_input_poll(&e));
    init_error=ESP_FAIL; assert(sb_input_init()==ESP_FAIL); assert(!sb_input_poll(&e));
    init_error=0; null_queue=true; assert(sb_input_init()==ESP_ERR_INVALID_STATE);
    null_queue=false; assert(sb_input_init()==ESP_OK); assert(!sb_input_poll(NULL));
    // Independently verify the specified navigation mapping.
    const struct { bsp_input_navigation_key_t key; sb_action_t action; } expected[] = {
        {BSP_INPUT_NAVIGATION_KEY_UP,SB_ACTION_UP}, {BSP_INPUT_NAVIGATION_KEY_DOWN,SB_ACTION_DOWN},
        {BSP_INPUT_NAVIGATION_KEY_LEFT,SB_ACTION_LEFT}, {BSP_INPUT_NAVIGATION_KEY_RIGHT,SB_ACTION_RIGHT},
        {BSP_INPUT_NAVIGATION_KEY_RETURN,SB_ACTION_CONFIRM}, {BSP_INPUT_NAVIGATION_KEY_SPACE_L,SB_ACTION_FIRE},
        {BSP_INPUT_NAVIGATION_KEY_SPACE_M,SB_ACTION_FIRE}, {BSP_INPUT_NAVIGATION_KEY_SPACE_R,SB_ACTION_FIRE},
        {BSP_INPUT_NAVIGATION_KEY_ESC,SB_ACTION_CANCEL}, {BSP_INPUT_NAVIGATION_KEY_BACKSPACE,SB_ACTION_CANCEL},
        {BSP_INPUT_NAVIGATION_KEY_TAB,SB_ACTION_NEXT_PANEL}, {BSP_INPUT_NAVIGATION_KEY_F1,SB_ACTION_HELP},
        {BSP_INPUT_NAVIGATION_KEY_F2,SB_ACTION_DEPLOY_ROTATE}, {BSP_INPUT_NAVIGATION_KEY_F3,SB_ACTION_SELECT},
        {BSP_INPUT_NAVIGATION_KEY_F4,SB_ACTION_COMMIT}, {BSP_INPUT_NAVIGATION_KEY_F5,SB_ACTION_RADIO_STATUS},
        {BSP_INPUT_NAVIGATION_KEY_F6,SB_ACTION_SURRENDER},
    };
    for(size_t i=0;i<sizeof(expected)/sizeof(expected[0]);++i) {
        fresh(); queue[tail++]=(bsp_input_event_t){.type=INPUT_EVENT_TYPE_NAVIGATION,
            .args_navigation={.key=expected[i].key,.state=true}};
        expect(expected[i].action,SB_INPUT_PRESS);
    }
    // All bindings: one press, suppressed duplicates, one release. No character actions.
    for(size_t i=0;i<BINDING_COUNT;++i) {
        fresh(); push(i,true); push(i,true);
        queue[tail++]=(bsp_input_event_t){.type=INPUT_EVENT_TYPE_KEYBOARD,.args_keyboard.ascii='r'};
        expect(bindings[i].action,SB_INPUT_PRESS); empty();
        if(i>=4) { now=10000000; empty(); }
        push(i,false); expect(bindings[i].action,SB_INPUT_RELEASE); empty();
    }
    fresh();
    queue[tail++]=(bsp_input_event_t){.type=INPUT_EVENT_TYPE_SCANCODE,.args_scancode.scancode=BSP_INPUT_SCANCODE_ENTER};
    push(4,true); expect(SB_ACTION_CONFIRM,SB_INPUT_PRESS); empty();
    // Precise delay/interval, long stall without catch-up, release stops repeat.
    fresh(); push(0,true); expect(SB_ACTION_UP,SB_INPUT_PRESS);
    now=399999; empty(); now=400000; expect(SB_ACTION_UP,SB_INPUT_REPEAT); empty();
    now=479999; empty(); now=480000; expect(SB_ACTION_UP,SB_INPUT_REPEAT);
    now=9000000; expect(SB_ACTION_UP,SB_INPUT_REPEAT); empty();
    push(0,false); expect(SB_ACTION_UP,SB_INPUT_RELEASE); now+=1000000; empty();
    // Space aliases are one logical key: overlap cannot fake a release.
    fresh(); push(5,true); expect(SB_ACTION_FIRE,SB_INPUT_PRESS);
    push(6,true); empty(); push(5,false); empty();
    push(7,true); empty(); push(6,false); empty();
    push(7,false); expect(SB_ACTION_FIRE,SB_INPUT_RELEASE); empty();
    fresh(); push(5,true); expect(SB_ACTION_FIRE,SB_INPUT_PRESS);
    sb_input_reset(); push(6,true); empty(); push(5,false); empty();
    push(7,true); empty(); push(6,false); empty(); push(7,false); empty();
    push(6,true); expect(SB_ACTION_FIRE,SB_INPUT_PRESS);
    push(6,false); expect(SB_ACTION_FIRE,SB_INPUT_RELEASE);
    // Short tap already released in BSP snapshot still preserves both queued edges.
    fresh(); push(8,true); push(8,false); expect(SB_ACTION_ROTATE,SB_INPUT_PRESS); expect(SB_ACTION_ROTATE,SB_INPUT_RELEASE); empty();
    // Simultaneous arrows are independently held and fairly repeated.
    fresh(); for(size_t i=0;i<4;++i) { push(i,true); expect(bindings[i].action,SB_INPUT_PRESS); }
    now=400000; for(size_t i=0;i<4;++i) expect(bindings[i].action,SB_INPUT_REPEAT); empty();
    physical[0]=false; expect(SB_ACTION_UP,SB_INPUT_RELEASE);
    now=480000; for(size_t i=1;i<4;++i) expect(bindings[i].action,SB_INPUT_REPEAT); empty();
    // Missed release recovers edge-only bindings too; aliases stay independent.
    fresh(); push(9,true); push(10,true); expect(SB_ACTION_CANCEL,SB_INPUT_PRESS); expect(SB_ACTION_CANCEL,SB_INPUT_PRESS);
    physical[9]=false; expect(SB_ACTION_CANCEL,SB_INPUT_RELEASE); empty();
    push(9,true); expect(SB_ACTION_CANCEL,SB_INPUT_PRESS); empty();
    // Reset discards queue, blocks held keys through duplicate events, then rearms.
    for(size_t i=0;i<BINDING_COUNT;++i) {
        fresh(); push(i,true); sb_input_reset(); now=1000000; empty();
        push(i,true); empty(); physical[i]=false; empty();
        push(i,true); expect(bindings[i].action,SB_INPUT_PRESS);
    }
    // Failed snapshot cancels held repeat and suppresses until a known release.
    fresh(); push(0,true); expect(SB_ACTION_UP,SB_INPUT_PRESS); query_fail=true;
    now=400000; expect(SB_ACTION_UP,SB_INPUT_RELEASE); empty(); query_fail=false; empty();
    push(0,false); empty(); push(0,true); expect(SB_ACTION_UP,SB_INPUT_PRESS);
    // Dropped press snapshots cannot invent confirmation actions.
    fresh(); physical[4]=true; empty(); push(4,true); empty(); push(4,false); empty();
    push(4,true); expect(SB_ACTION_CONFIRM,SB_INPUT_PRESS);
    // Noisy queue work is bounded; a later action remains available.
    fresh(); for(int i=0;i<130;++i) queue[tail++]=(bsp_input_event_t){.type=INPUT_EVENT_TYPE_KEYBOARD};
    push(8,true); empty(); assert(head==64); empty(); assert(head==128); expect(SB_ACTION_ROTATE,SB_INPUT_PRESS);
    assert(!strcmp(sb_input_action_name(SB_ACTION_COMMIT),"COMMIT"));
    assert(!strcmp(sb_input_action_name((sb_action_t)-1),"UNKNOWN"));
    fresh(); sb_input_set_text_mode(true);
    push(8,true); // R scancode must not rotate in text mode.
    queue[tail++]=(bsp_input_event_t){.type=INPUT_EVENT_TYPE_KEYBOARD,.args_keyboard.ascii='R'};
    expect(SB_ACTION_TEXT_BASE+'R',SB_INPUT_PRESS); empty();
    push(5,true); // Space navigation is ignored; text is delivered exactly once.
    queue[tail++]=(bsp_input_event_t){.type=INPUT_EVENT_TYPE_KEYBOARD,.args_keyboard.ascii=' '};
    expect(SB_ACTION_TEXT_BASE+' ',SB_INPUT_PRESS); empty();
    push(10,true);
    queue[tail++]=(bsp_input_event_t){.type=INPUT_EVENT_TYPE_KEYBOARD,.args_keyboard.ascii='\b'};
    expect(SB_ACTION_BACKSPACE,SB_INPUT_PRESS); empty();
    now=400000; expect(SB_ACTION_BACKSPACE,SB_INPUT_REPEAT);
    push(10,false); expect(SB_ACTION_BACKSPACE,SB_INPUT_RELEASE);
    queue[tail++]=(bsp_input_event_t){.type=INPUT_EVENT_TYPE_SCANCODE,.args_scancode.scancode=BSP_INPUT_SCANCODE_ESCAPED_GREY_DEL};
    expect(SB_ACTION_DELETE,SB_INPUT_PRESS);
    sb_input_set_text_mode(false); // Held R must not escape the editing context.
    push(8,true); empty(); push(8,false); empty(); push(8,true); expect(SB_ACTION_ROTATE,SB_INPUT_PRESS);
    fresh();
    puts("PASS: M10 text characters, Space/R isolation, Backspace repeat, Delete and held-key exit suppression");
    puts("PASS: M3 mappings, deduplication, edges, repeat timing/fairness, queue bounds, transitions and lost-release recovery");
}
''')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                    '-I', str(p), '-I', str(root/'components/spacebattles'),
                    '-I', str(root/'managed_components/badgeteam__badge-bsp'),
                    str(p/'test.c'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
