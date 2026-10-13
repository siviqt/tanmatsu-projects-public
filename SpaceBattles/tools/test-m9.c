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
        for (unsigned d=0;d<3;++d) for(unsigned rules=1;rules<=2;++rules) {
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
/* Compare identical random fleets using only shot feedback in each AI view. */
static void search_benchmark(void) {
    unsigned shots[3]={0};
    for(unsigned trial=1;trial<=200;trial++) {
        uint32_t seed=trial*7919; sb_fleet_t fleet={0}; sb_completion_t completed;
        assert(sb_complete(&fleet,SB_PLACEMENT_BUDGET,rng,&seed,&completed));
        for(unsigned d=SB_AI_NORMAL;d<=SB_AI_HARD;d++) {
            sb_knowledge_t k; sb_knowledge_init(&k); unsigned sunk=0,turn=0;
            uint32_t decisions=trial; bool disclosed[SB_SHIPS]={0};
            while(sunk<SB_SHIPS) {
                uint8_t target[5]; assert(++turn<=100);
                assert(sb_ai_choose(&k,SB_CLASSIC,(sb_ai_difficulty_t)d,1,rng,&decisions,target)==1);
                unsigned cell=target[0]; k.cells[cell]=SB_MISS;
                for(unsigned ship=0;ship<SB_SHIPS;ship++) {
                    unsigned n=sb_rules(SB_CLASSIC)->lengths[ship];
                    for(unsigned j=0;j<n;j++) if(fleet.ships[ship].cells[j]==cell) k.cells[cell]=SB_HIT;
                    bool done=true;
                    for(unsigned j=0;j<n;j++) if(k.cells[fleet.ships[ship].cells[j]]!=SB_HIT) done=false;
                    if(done && !disclosed[ship]) {
                        assert(sb_disclose(&k,n,fleet.ships[ship].cells)); disclosed[ship]=true; ++sunk;
                    }
                }
            }
            shots[d]+=turn;
        }
    }
    printf("Identical 200 random fleets: Normal %.2f shots, Hard %.2f shots\n",shots[SB_AI_NORMAL]/200.0,shots[SB_AI_HARD]/200.0);
    assert(shots[SB_AI_HARD]<shots[SB_AI_NORMAL]);
}
static void hard_and_results(void) {
    sb_knowledge_t k; sb_knowledge_init(&k);
    for(unsigned c=0;c<100;c++) k.cells[c]=SB_MISS;
    memset(k.remaining,0,sizeof(k.remaining)); k.remaining[3]=1;
    k.cells[40]=SB_HIT; k.cells[41]=k.cells[42]=k.cells[39]=SB_UNKNOWN;
    uint32_t seed=4; uint8_t out[5];
    assert(sb_ai_choose(&k,SB_CLASSIC,SB_AI_HARD,1,rng,&seed,out)==1);
    assert(out[0]==41 || out[0]==42); // 39 is across a row boundary, no legal ship.
    assert(sb_ai_choose(&k,SB_ESCALATED,SB_AI_HARD,2,rng,&seed,out)==2);
    assert((out[0]==41 && out[1]==42) || (out[0]==42 && out[1]==41));
    assert(sb_ai_parse("hard")==SB_AI_HARD && !strcmp(sb_ai_name(SB_AI_HARD),"Hard"));
    for(unsigned p=0;p<2;p++) {
        sb_spec_t spec; sb_match_t m;
        assert(sb_spec_make(&spec,SB_CLASSIC,SB_SINGLE_PLAYER,0));
        assert(sb_match_init(&m,&spec,0) && sb_match_surrender(&m,p));
        assert(sb_result_for(m.outcome,m.end_reason,p)==SB_RESULT_SURRENDERED);
        assert(sb_result_for(m.outcome,m.end_reason,1-p)==SB_RESULT_OPPONENT_SURRENDERED);
        assert(m.record.outcome==m.outcome && m.record.end_reason==m.end_reason);
        assert(!sb_match_surrender(&m,1-p) && !sb_match_abandon(&m));
        assert(sb_match_init(&m,&spec,0) && sb_match_leave(&m,p));
        assert(sb_result_for(m.outcome,m.end_reason,p)==SB_RESULT_LEFT);
        assert(sb_result_for(m.outcome,m.end_reason,1-p)==SB_RESULT_OPPONENT_LEFT);
        assert(sb_match_init(&m,&spec,0) && sb_match_abandon(&m));
        assert(sb_result_for(m.outcome,m.end_reason,p)==SB_RESULT_ABORTED);
        assert(sb_result_for(p ? SB_WIN_B : SB_WIN_A,SB_END_COMBAT,p)==SB_RESULT_VICTORY);
        assert(sb_result_for(p ? SB_WIN_A : SB_WIN_B,SB_END_COMBAT,p)==SB_RESULT_DEFEAT);
    }
    assert(!sb_result_valid(SB_WIN_A,SB_END_SURRENDER_A));
    assert(!sb_result_valid(SB_ONGOING,SB_END_COMBAT));
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
    for(unsigned rules=1;rules<=2;++rules) for(unsigned d=0;d<3;++d) for(unsigned trial=1;trial<=100;++trial) {
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
                now+=30000000; assert(!sb_local_tick(&s,now));
                assert(!memcmp(&record,&s.match.record,sizeof(record)) && s.screen==SB_LOCAL_RESULT);
                sb_local_presented(&s,now); press(&s,SB_ACTION_COMMIT);
            } else {
                assert(s.screen==SB_LOCAL_TARGET);
                uint8_t targets[5];
                size_t n=sb_ai_choose(&s.match.knowledge[0],(sb_ruleset_t)rules,SB_AI_EASY,
                    sb_allowance(&s.match,0),rng,&human,targets);
                assert(n);
                if(rules==SB_CLASSIC) { assert(s.match.actor==0); s.cursor=targets[0]; press(&s,SB_ACTION_SELECT); press(&s,SB_ACTION_COMMIT); }
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
        assert(s.match.end_reason==SB_END_COMBAT);
        assert(sb_result_valid(s.match.outcome,s.match.end_reason));
        assert(!s.match.clock_expired); // Complete by fleet destruction, not an accidental timeout.
        uint8_t starter=s.match.spec.starter;
        assert(sb_local_begin_single(&s,(sb_ruleset_t)rules,(sb_ai_difficulty_t)d,true,rng,&seed,now));
        if(rules==SB_CLASSIC) assert(s.match.spec.starter==1-starter);
    }
}
static void clocks_and_dialogs(void) {
    uint32_t seed=5; sb_local_t s; sb_ui_tactical_t v;
    assert(sb_local_begin_single(&s,SB_ESCALATED,SB_AI_NORMAL,false,rng,&seed,100));
    assert(!s.match.spec.deployment_ms && !s.match.spec.match_ms);
    press(&s,SB_ACTION_CONFIRM); press(&s,SB_ACTION_FIRE);
    sb_fleet_t fleet=s.match.fleets[0];
    sb_local_tick(&s,1000000000); assert(s.screen==SB_LOCAL_DEPLOY && !memcmp(&fleet,&s.match.fleets[0],sizeof(fleet)));
    assert(sb_local_project(&s,&v)); assert(sb_ui_tactical_keys(&v)==(1|2|4|32));
    bool vertical=s.vertical; press(&s,SB_ACTION_DEPLOY_ROTATE); assert(s.vertical!=vertical);
    press(&s,SB_ACTION_COMMIT); assert(s.screen==SB_LOCAL_DEPLOY);
    sb_local_t space=s, f3=s; press(&space,SB_ACTION_FIRE); press(&f3,SB_ACTION_SELECT);
    assert(!memcmp(&space,&f3,sizeof(space)));
    press(&s,SB_ACTION_HELP); assert(!sb_local_project(&s,&v)); press(&s,SB_ACTION_CANCEL);
    assert(s.screen==SB_LOCAL_DEPLOY && !s.match.paused);
    press(&s,SB_ACTION_SURRENDER); assert(s.screen==SB_LOCAL_SURRENDER && !sb_local_project(&s,&v));
    press(&s,SB_ACTION_SURRENDER); press(&s,SB_ACTION_COMMIT); assert(s.match.outcome==SB_ONGOING);
    press(&s,SB_ACTION_CANCEL); assert(s.screen==SB_LOCAL_DEPLOY);
    press(&s,SB_ACTION_SURRENDER); press(&s,SB_ACTION_CONFIRM); assert(s.match.outcome==SB_WIN_B);
    start(&s,SB_CLASSIC,SB_AI_EASY,&seed);
    while(s.screen==SB_LOCAL_RESULT) { sb_local_presented(&s,s.match.now_ms); press(&s,SB_ACTION_COMMIT); }
    assert(sb_local_project(&s,&v) && !(sb_ui_tactical_keys(&v)&8));
    press(&s,SB_ACTION_COMMIT); assert(s.screen==SB_LOCAL_TARGET);
    s.cursor=44; press(&s,SB_ACTION_SELECT); assert(s.classic_selected && s.classic_target==44);
    press(&s,SB_ACTION_RIGHT); assert(s.classic_target==44 && s.cursor==45);
    assert(sb_local_project(&s,&v) && v.targets[44] && !v.targets[45] && (sb_ui_tactical_keys(&v)&8));
    press(&s,SB_ACTION_LEFT); press(&s,SB_ACTION_SELECT); assert(!s.classic_selected);
    press(&s,SB_ACTION_SELECT); press(&s,SB_ACTION_COMMIT); assert(s.screen==SB_LOCAL_RESULT);
    assert(s.match.knowledge[0].cells[44]!=SB_UNKNOWN && s.match.knowledge[0].cells[45]==SB_UNKNOWN);
    uint32_t sequence=s.match.sequence;
    sb_local_tick(&s,s.match.now_ms+1000000000); assert(s.screen==SB_LOCAL_RESULT && s.match.sequence==sequence);
    start(&s,SB_ESCALATED,SB_AI_EASY,&seed);
    s.cursor=99; press(&s,SB_ACTION_SELECT); assert(s.match.selected[0]==1);
    sb_local_tick(&s,s.match.now_ms+1000000000); assert(s.screen==SB_LOCAL_TARGET && !s.match.clock_expired);
    press(&s,SB_ACTION_SELECT); assert(!s.match.selected[0]);
    press(&s,SB_ACTION_COMMIT); assert(s.screen==SB_LOCAL_RESULT); // Explicit empty salvo remains legal.
    press(&s,SB_ACTION_COMMIT); assert(s.screen==SB_LOCAL_RESULT); // Must present before continuing.
    sb_local_presented(&s,s.match.now_ms); press(&s,SB_ACTION_COMMIT); assert(s.screen==SB_LOCAL_TARGET);
    press(&s,SB_ACTION_CANCEL); press(&s,SB_ACTION_UP); press(&s,SB_ACTION_CONFIRM); assert(s.match.outcome==SB_ABANDONED);
}
int main(void) {
    chooser(); search_benchmark(); hard_and_results(); full_matches(); clocks_and_dialogs();
    puts("M9: public-only Easy/Normal/Hard, 600 complete sessions, legality, touching ships, rematches, privacy, no deadlines, context keys, explicit results, surrender and abandonment passed");
}
