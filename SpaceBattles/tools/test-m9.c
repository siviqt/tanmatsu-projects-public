// SPDX-License-Identifier: MIT
#include "app/local.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t rng(void *p) { uint32_t *s=p; *s=*s*1664525u+1013904223u; return *s; }
static void press(sb_local_t *s,sb_action_t action) { sb_local_event(s,(sb_input_event_t){action,SB_INPUT_PRESS}); }
static void chooser(void) {
    sb_knowledge_t k; sb_knowledge_init(&k);
    uint8_t a[SB_SALVO],b[SB_SALVO];
    uint32_t seed=7,copy=7;
    assert(sb_ai_choose(&k,SB_ESCALATED,SB_AI_EASY,5,rng,&seed,a)==5);
    assert(sb_ai_choose(&k,SB_ESCALATED,SB_AI_EASY,5,rng,&copy,b)==5);
    assert(!memcmp(a,b,5));
    // Every seeded salvo is unique, legal, bounded, and does not mutate public knowledge.
    for (unsigned i=0;i<1000;++i) {
        for (unsigned c=0;c<100;++c) k.cells[c]=(sb_cell_state_t)(rng(&seed)%4);
        sb_knowledge_t before=k;
        for (unsigned d=0;d<2;++d) for(unsigned rules=1;rules<=2;++rules) {
            size_t n=sb_ai_choose(&k,(sb_ruleset_t)rules,(sb_ai_difficulty_t)d,i%8,rng,&seed,a);
            assert(n<=5 && n<=i%8 && (rules!=SB_CLASSIC || n<=1));
            for (size_t j=0;j<n;++j) { assert(k.cells[a[j]]==SB_UNKNOWN); for(size_t z=0;z<j;++z) assert(a[z]!=a[j]); }
            assert(!memcmp(&before,&k,sizeof(k)));
        }
    }
    sb_knowledge_init(&k); k.cells[44]=SB_HIT;
    for(unsigned i=0;i<100;++i) {
        assert(sb_ai_choose(&k,SB_CLASSIC,SB_AI_NORMAL,5,rng,&seed,a)==1);
        assert(a[0]==34 || a[0]==43 || a[0]==45 || a[0]==54);
    }
    k.cells[45]=SB_HIT;
    assert(sb_ai_choose(&k,SB_CLASSIC,SB_AI_NORMAL,1,rng,&seed,a)==1);
    assert(a[0]==43 || a[0]==46);
    // Sinking one size-three must not erase an adjacent hit on another ship.
    k.cells[46]=SB_HIT; uint8_t sunk[]={44,45,46};
    assert(sb_disclose(&k,3,sunk) && k.remaining[3]==1); k.cells[54]=SB_HIT;
    assert(sb_ai_choose(&k,SB_CLASSIC,SB_AI_NORMAL,1,rng,&seed,a)==1);
    assert(a[0]==53 || a[0]==55 || a[0]==64);
    for(unsigned c=0;c<100;++c) k.cells[c]=SB_MISS;
    k.cells[99]=SB_UNKNOWN;
    assert(sb_ai_choose(&k,SB_ESCALATED,SB_AI_NORMAL,5,rng,&seed,a)==1 && a[0]==99);
    k.cells[99]=SB_HIT;
    assert(!sb_ai_choose(&k,SB_ESCALATED,SB_AI_NORMAL,5,rng,&seed,a));
    assert(!sb_ai_choose(NULL,SB_CLASSIC,SB_AI_EASY,1,rng,&seed,a));
    assert(!sb_ai_choose(&k,SB_CLASSIC,SB_AI_EASY,1,NULL,&seed,a));
}
static void start(sb_local_t *s,sb_ruleset_t rules,sb_ai_difficulty_t difficulty,uint32_t *seed) {
    assert(sb_local_begin_single(s,rules,difficulty,false,rng,seed,100));
    sb_local_tick(s,90000); // Setup summary has no deployment deadline.
    assert(s->match.phase==SB_DEPLOYMENT && !s->match.ready[1]);
    press(s,SB_ACTION_CONFIRM);
    assert(s->screen==SB_LOCAL_DEPLOY && s->match.ready[1]);
    assert(sb_local_indicator(s)==-1);
    for(unsigned i=0;i<5;++i) {
        press(s,SB_ACTION_FIRE);
        if(i<4) press(s,SB_ACTION_DOWN);
    }
    assert(sb_fleet_valid(&s->match.fleets[0],true));
    press(s,SB_ACTION_CONFIRM);
    assert(s->screen==SB_LOCAL_TARGET || s->screen==SB_LOCAL_RESULT);
}
static void full_matches(void) {
    for(unsigned rules=1;rules<=2;++rules) for(unsigned d=0;d<2;++d) for(unsigned trial=1;trial<=100;++trial) {
        uint32_t seed=trial, human=trial*17;
        sb_local_t s; start(&s,(sb_ruleset_t)rules,(sb_ai_difficulty_t)d,&seed);
        uint64_t now=s.match.now_ms;
        unsigned steps=0;
        while(s.screen!=SB_LOCAL_FINISHED) {
            assert(++steps<401);
            sb_ui_tactical_t v; assert(sb_local_project(&s,&v));
            assert(v.play_type==SB_UI_SINGLE_PLAYER && !v.player_b);
            if(!v.own_board) for(unsigned c=0;c<100;++c) assert(!v.occupied[c]);
            if(s.screen==SB_LOCAL_RESULT) {
                sb_record_t record=s.match.record;
                press(&s,SB_ACTION_CONFIRM); assert(!memcmp(&record,&s.match.record,sizeof(record)));
                sb_local_presented(&s,now); now+=3000; assert(sb_local_tick(&s,now));
            } else {
                assert(s.screen==SB_LOCAL_TARGET);
                uint8_t targets[5];
                size_t n=sb_ai_choose(&s.match.knowledge[0],(sb_ruleset_t)rules,SB_AI_EASY,
                    sb_allowance(&s.match,0),rng,&human,targets);
                assert(n);
                if(rules==SB_CLASSIC) { assert(s.match.actor==0); s.cursor=targets[0]; press(&s,SB_ACTION_CONFIRM); }
                else {
                    // AI lock must not force completion or disclose any target to the human view.
                    assert(s.match.salvo_locked[1] && !s.match.salvo_locked[0]);
                    now+=20; sb_local_tick(&s,now); assert(s.screen==SB_LOCAL_TARGET);
                    for(size_t i=0;i<n;++i) { s.cursor=targets[i]; press(&s,SB_ACTION_FIRE); }
                    assert(s.match.selected[0]==n); press(&s,SB_ACTION_CONFIRM);
                }
                assert(s.screen==SB_LOCAL_RESULT);
            }
        }
        assert(s.match.outcome==SB_WIN_A || s.match.outcome==SB_WIN_B || s.match.outcome==SB_DRAW);
        assert(!s.match.clock_expired); // Complete by fleet destruction, not an accidental timeout.
        uint8_t starter=s.match.spec.starter;
        assert(sb_local_begin_single(&s,(sb_ruleset_t)rules,(sb_ai_difficulty_t)d,true,rng,&seed,now));
        if(rules==SB_CLASSIC) assert(s.match.spec.starter==1-starter);
    }
}
static void clocks_and_dialogs(void) {
    uint32_t seed=5; sb_local_t s;
    assert(sb_local_begin_single(&s,SB_ESCALATED,SB_AI_NORMAL,false,rng,&seed,100));
    press(&s,SB_ACTION_CONFIRM); press(&s,SB_ACTION_FIRE);
    sb_ship_t first=s.match.fleets[0].ships[0];
    sb_local_tick(&s,1000); press(&s,SB_ACTION_HELP);
    assert(s.match.paused); sb_local_tick(&s,71000);
    sb_ui_tactical_t v; assert(!sb_local_project(&s,&v));
    press(&s,SB_ACTION_CANCEL); assert(!s.match.paused && s.screen==SB_LOCAL_DEPLOY);
    sb_local_tick(&s,130099); assert(s.match.phase==SB_DEPLOYMENT);
    sb_local_tick(&s,130100); assert(s.screen==SB_LOCAL_TARGET);
    assert(!memcmp(&first,&s.match.fleets[0].ships[0],sizeof(first)));
    assert(s.match.combat_elapsed_ms==0 && s.match.selected[1]==5);
    assert(sb_local_project(&s,&v) && v.seconds_remaining==600);
    press(&s,SB_ACTION_FIRE); assert(s.match.selected[0]==1);
    press(&s,SB_ACTION_FIRE); assert(s.match.selected[0]==0);
    press(&s,SB_ACTION_CONFIRM);
    assert(s.screen==SB_LOCAL_RESULT && s.match.selected[0]==0);
    sb_local_presented(&s,130100); sb_local_tick(&s,133100);
    assert(s.match.combat_elapsed_ms==3000 && s.screen==SB_LOCAL_TARGET);
    press(&s,SB_ACTION_CANCEL); assert(s.match.paused);
    sb_local_tick(&s,233100); assert(s.match.combat_elapsed_ms==3000);
    press(&s,SB_ACTION_CANCEL);
    press(&s,SB_ACTION_FIRE);
    sb_local_tick(&s,830100); // Final partial salvo resolves once at 10 minutes.
    assert(s.match.clock_expired && s.screen==SB_LOCAL_RESULT && s.match.phase==SB_FINISHED);
    sb_record_t record=s.match.record;
    sb_local_presented(&s,830100); sb_local_tick(&s,833100);
    assert(s.screen==SB_LOCAL_FINISHED && !memcmp(&record,&s.match.record,sizeof(record)));
    // Expiry during presentation must not resolve again or begin a new round.
    start(&s,SB_ESCALATED,SB_AI_EASY,&seed);
    sb_local_tick(&s,s.match.now_ms+599000);
    press(&s,SB_ACTION_CONFIRM); press(&s,SB_ACTION_CONFIRM);
    uint64_t now=s.match.now_ms; record=s.match.record;
    sb_local_presented(&s,now); sb_local_tick(&s,now+3000);
    assert(s.screen==SB_LOCAL_FINISHED && s.match.clock_expired);
    assert(record.sequence==s.match.record.sequence && record.count==s.match.record.count);
    // A full salvo commits immediately, with no confirmation screen.
    start(&s,SB_ESCALATED,SB_AI_EASY,&seed);
    for(unsigned i=0;i<5;++i) { s.cursor=i; press(&s,SB_ACTION_FIRE); }
    press(&s,SB_ACTION_CONFIRM); assert(s.screen==SB_LOCAL_RESULT);
    // Classic Space is inert; Enter fires, and AI match menus offer abandonment.
    start(&s,SB_CLASSIC,SB_AI_EASY,&seed);
    if(s.screen==SB_LOCAL_RESULT) { sb_local_presented(&s,s.match.now_ms); sb_local_tick(&s,s.match.now_ms+3000); }
    sb_local_tick(&s,s.match.now_ms+9999999); assert(!s.match.clock_expired);
    sb_match_t before_space=s.match;
    press(&s,SB_ACTION_FIRE); assert(s.screen==SB_LOCAL_TARGET && !memcmp(&s.match,&before_space,sizeof(s.match)));
    press(&s,SB_ACTION_CONFIRM); assert(s.screen==SB_LOCAL_RESULT);
    while(s.screen==SB_LOCAL_RESULT) { sb_local_presented(&s,s.match.now_ms); sb_local_tick(&s,s.match.now_ms+3000); }
    assert(s.screen==SB_LOCAL_TARGET);
    press(&s,SB_ACTION_CANCEL); for(unsigned i=0;i<3;++i) press(&s,SB_ACTION_DOWN); press(&s,SB_ACTION_CONFIRM);
    assert(s.match.outcome==SB_ABANDONED && s.screen==SB_LOCAL_FINISHED);
    start(&s,SB_ESCALATED,SB_AI_EASY,&seed);
    press(&s,SB_ACTION_CANCEL); press(&s,SB_ACTION_UP); press(&s,SB_ACTION_CONFIRM);
    assert(s.match.outcome==SB_ABANDONED);
}
int main(void) {
    chooser(); full_matches(); clocks_and_dialogs();
    puts("M9: public-only Easy/Normal, 400 complete sessions, legality, touching ships, rematches, privacy, clocks, pause, timeout and abandonment passed");
}
