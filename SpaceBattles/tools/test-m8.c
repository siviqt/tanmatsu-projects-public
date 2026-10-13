// SPDX-License-Identifier: MIT
#include "app/local.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t rng(void *ctx) { (void)ctx; return 1; }
static void event(sb_local_t *s, sb_action_t a, sb_input_phase_t p) { sb_local_event(s, (sb_input_event_t){a,p}); }
static void press(sb_local_t *s, sb_action_t a) { event(s,a,SB_INPUT_PRESS); }
static void hidden(sb_local_t *s) {
    sb_ui_tactical_t v, zero={0}; memset(&v,0xff,sizeof(v));
    assert(!sb_local_project(s,&v)); assert(!memcmp(&v,&zero,sizeof(v)));
}
static void unlock(sb_local_t *s) {
    assert(s->screen==SB_LOCAL_COVER); hidden(s);
    event(s,SB_ACTION_FIRE,SB_INPUT_RELEASE);
    event(s,SB_ACTION_FIRE,SB_INPUT_REPEAT);
    press(s,SB_ACTION_CONFIRM); assert(s->screen==SB_LOCAL_COVER); hidden(s);
    sb_match_t before=s->match;
    press(s,SB_ACTION_FIRE);
    assert(s->screen==SB_LOCAL_DEPLOY || s->screen==SB_LOCAL_TARGET);
    assert(s->reset_input && !memcmp(&before,&s->match,sizeof(before)));
    event(s,SB_ACTION_FIRE,SB_INPUT_REPEAT); event(s,SB_ACTION_FIRE,SB_INPUT_RELEASE);
    assert(!memcmp(&before,&s->match,sizeof(before)));
    assert(s->match.sequence<2 || s->match.phase==SB_TARGETING);
}
static void result_delay(sb_local_t *s) {
    assert(s->screen==SB_LOCAL_RESULT);
    sb_match_t before=s->match; sb_ui_tactical_t view;
    assert(sb_local_project(s,&view) && view.effects && view.targets[s->cursor]);
    for(unsigned a=SB_ACTION_UP;a<=SB_ACTION_SURRENDER;a++) if(a!=SB_ACTION_CONFIRM && a!=SB_ACTION_COMMIT) press(s,a);
    assert(s->screen==SB_LOCAL_RESULT && !memcmp(&before,&s->match,sizeof(before)));
    sb_local_presented(s,100000);
    assert(!sb_local_tick(s,1000000000) && s->screen==SB_LOCAL_RESULT);
    press(s,SB_ACTION_COMMIT);
    assert(s->reset_input && s->screen==(before.phase==SB_FINISHED ? SB_LOCAL_FINISHED : SB_LOCAL_COVER));
}
static void deploy(sb_local_t *s) {
    unlock(s);
    press(s,SB_ACTION_CONFIRM); assert(s->screen==SB_LOCAL_DEPLOY);
    sb_ui_tactical_t view;
    sb_fleet_t before=s->match.fleets[s->player];
    assert(sb_local_project(s,&view) && view.placement_valid);
    for(unsigned c=0;c<100;++c) assert(view.placement[c]==(c<5));
    press(s,SB_ACTION_RIGHT); press(s,SB_ACTION_ROTATE);
    assert(sb_local_project(s,&view) && view.placement[1] && view.placement[41] && !view.placement[0]);
    assert(!memcmp(&before,&s->match.fleets[s->player],sizeof(before)));
    for(unsigned n=0;n<12;++n) press(s,SB_ACTION_DOWN);
    assert(s->cursor==51); // Full length remains on the board.
    press(s,SB_ACTION_ROTATE); assert(s->cursor==51);
    s->cursor=0;

    for(unsigned i=0;i<SB_SHIPS;++i) {
        s->cursor=i*10; press(s,i%2 ? SB_ACTION_FIRE : SB_ACTION_SELECT);
        if(i==0) {
            assert(sb_local_project(s,&view) && !view.placement_valid);
            before=s->match.fleets[s->player];
            press(s,SB_ACTION_FIRE); // Overlap remains a preview, never commits.
            assert(!memcmp(&before,&s->match.fleets[s->player],sizeof(before)));
        }
    }
    assert(sb_fleet_valid(&s->match.fleets[s->player],true));
    assert(s->ship==0 && s->cursor==0 && !s->vertical);
    for(unsigned i=0;i<SB_SHIPS;++i) {
        assert(s->ship==i && s->cursor==i*10 && !s->vertical);
        assert(sb_local_project(s,&view) && view.deployment && view.ship_selected==i);
        for(unsigned j=0;j<SB_SHIPS;++j) assert(view.ships_placed[j]);
        before=s->match.fleets[s->player];
        press(s,SB_ACTION_RIGHT); press(s,SB_ACTION_ROTATE);
        assert(!memcmp(&before,&s->match.fleets[s->player],sizeof(before)));
        press(s,SB_ACTION_NEXT_PANEL);
    }
    // Committing a move replaces the old cells; cycling restores its new anchor/orientation.
    sb_local_t edit=*s;
    edit.cursor=55; edit.vertical=true; press(&edit,SB_ACTION_FIRE);
    assert(edit.match.fleets[edit.player].ships[0].cells[0]==55);
    for(unsigned i=0;i<4;++i) press(&edit,SB_ACTION_NEXT_PANEL);
    assert(edit.ship==0 && edit.cursor==55 && edit.vertical);

    press(s,SB_ACTION_CONFIRM); assert(s->screen==SB_LOCAL_COVER);
}
static void reveal_result(sb_local_t *s) {
    assert(s->screen==SB_LOCAL_COVER && s->review_result); hidden(s);
    // Neither a stale release, a view shortcut nor Enter reveals the result.
    event(s,SB_ACTION_FIRE,SB_INPUT_RELEASE); press(s,SB_ACTION_SURRENDER); press(s,SB_ACTION_CONFIRM);
    assert(s->screen==SB_LOCAL_COVER); hidden(s);
    assert(sb_local_open_settings(s)); sb_local_close_settings(s);
    event(s,SB_ACTION_FIRE,SB_INPUT_RELEASE); assert(s->screen==SB_LOCAL_COVER);
    press(s,SB_ACTION_HELP); press(s,SB_ACTION_CANCEL); assert(s->screen==SB_LOCAL_COVER);
    press(s,SB_ACTION_FIRE);
    assert(s->screen==SB_LOCAL_RESULT && !s->review_result);
}
static void escalated_shared(void) {
    sb_local_t s; assert(sb_local_begin_shared(&s,SB_ESCALATED,false,rng,NULL));
    assert(!s.match.spec.deployment_ms && !s.match.spec.selection_ms && !s.match.spec.match_ms);
    press(&s,SB_ACTION_CONFIRM); deploy(&s); deploy(&s);
    assert(s.player==0 && s.match.sequence==1);
    uint64_t clock=100000;
    unsigned rounds=0;
    while(s.match.phase!=SB_FINISHED) {
        assert(++rounds<30);
        uint32_t seq=s.match.sequence;
        for(unsigned p=0;p<2;++p) {
            assert(s.screen==SB_LOCAL_COVER && s.player==p); hidden(&s); unlock(&s);
            assert(s.screen==SB_LOCAL_TARGET && s.view_mode==0);
            sb_match_t before=s.match;
            sb_local_tick(&s,clock+1000000); assert(!memcmp(&before,&s.match,sizeof(before)));
            sb_ui_tactical_t v; assert(sb_local_project(&s,&v));
            assert(v.player_b==(p==1) && !strcmp(v.owner_name,p ? "Player B" : "Player A"));
            for(unsigned c=0;c<100;++c) assert(!v.targets[c]);
            unsigned count=0, allowance=sb_allowance(&s.match,p);
            // A partial first salvo also commits directly.
            if(rounds==1) allowance=1;
            for(unsigned ship=0;ship<SB_SHIPS && count<allowance;++ship)
                for(unsigned j=0;j<s.match.spec.rules.lengths[ship] && count<allowance;++j) {
                    unsigned c=s.match.fleets[1-p].ships[ship].cells[j];
                    if(s.match.knowledge[p].cells[c]!=SB_UNKNOWN) continue;
                    s.cursor=c; press(&s,SB_ACTION_FIRE); ++count;
                }
            assert(s.match.selected[p]==count);
            before=s.match;
            press(&s,SB_ACTION_DEPLOY_ROTATE);
            assert(!memcmp(&before,&s.match,sizeof(before)));
            assert(s.match.selected[p]==count);
            press(&s,SB_ACTION_CONFIRM);
            if(p==0) {
                assert(s.screen==SB_LOCAL_COVER && s.match.salvo_locked[0] && !s.match.salvo_locked[1]);
                assert(s.match.sequence==seq && s.match.phase==SB_TARGETING); hidden(&s);
            } else assert(s.screen==SB_LOCAL_RESULT && s.result_seen==2);
        }
        sb_record_t record=s.match.record;
        sb_local_presented(&s,clock); sb_local_tick(&s,clock+3000); press(&s,SB_ACTION_COMMIT); clock+=3000;
        assert(s.player==0 && s.match.sequence==seq); reveal_result(&s);
        assert(s.result_seen==3 && !memcmp(&record,&s.match.record,sizeof(record)));
        sb_ui_tactical_t v; assert(sb_local_project(&s,&v));
        assert(v.effects && v.dual && v.result && !v.player_b);
        assert(sb_local_indicator(&s)==0);
        sb_local_presented(&s,clock); sb_local_tick(&s,clock+3000); press(&s,SB_ACTION_COMMIT); clock+=3000;
        if(s.match.phase!=SB_FINISHED) assert(s.screen==SB_LOCAL_COVER && s.player==0 && s.match.sequence==seq+1);
    }
    assert(s.screen==SB_LOCAL_FINISHED && s.match.outcome==SB_DRAW && sb_local_indicator(&s)==-1);
    assert(sb_local_begin_shared(&s,SB_ESCALATED,true,rng,NULL));
    assert(s.match.spec.rules.id==SB_ESCALATED);
    press(&s,SB_ACTION_CONFIRM); press(&s,SB_ACTION_CANCEL);
    for(unsigned i=0;i<3;++i) press(&s,SB_ACTION_DOWN);
    press(&s,SB_ACTION_CONFIRM); assert(s.screen==SB_LOCAL_SURRENDER); press(&s,SB_ACTION_CONFIRM); assert(s.match.outcome==SB_WIN_B);
    puts("Shared Escalated: untimed private salvos, partial submission, two private result views, mutual draw, rematch and surrender passed");
}
int main(void) {
    escalated_shared();
    sb_local_t s={0}; assert(sb_local_begin(&s,false,rng,NULL));
    assert(s.match.spec.rules.id==SB_CLASSIC && !s.match.spec.deployment_ms);
    assert(s.match.spec.starter==1 && sb_local_indicator(&s)==-1);
    press(&s,SB_ACTION_CONFIRM); assert(sb_local_indicator(&s)==0);
    press(&s,SB_ACTION_HELP); hidden(&s);
    press(&s,SB_ACTION_SELECT); hidden(&s);
    press(&s,SB_ACTION_CANCEL); assert(s.screen==SB_LOCAL_COVER && s.presses==0);
    press(&s,SB_ACTION_DEPLOY_ROTATE); hidden(&s);
    deploy(&s); assert(s.player==1 && s.match.ready[0] && !s.match.ready[1]);
    deploy(&s); assert(s.player==1 && s.match.phase==SB_TARGETING);
    unlock(&s);
    sb_ui_tactical_t v;
    assert(sb_local_project(&s,&v));
    for(unsigned c=0;c<100;++c) assert(!v.occupied[c]);
    press(&s,SB_ACTION_DEPLOY_ROTATE); assert(sb_local_project(&s,&v));
    unsigned occupied=0; for(unsigned c=0;c<100;++c) occupied+=v.fleet_occupied[c]; assert(occupied==17 && s.view_mode==0);
    press(&s,SB_ACTION_FIRE); assert(s.match.phase==SB_TARGETING);
    press(&s,SB_ACTION_SELECT);
    press(&s,SB_ACTION_FIRE); assert(s.screen==SB_LOCAL_TARGET && s.match.sequence==1);
    event(&s,SB_ACTION_CONFIRM,SB_INPUT_REPEAT); assert(s.match.sequence==1);
    // Complete a whole match through the public controller; both fleets identical,
    // starter B wins after 33 alternating shots, without a final handover.
    unsigned next[2]={0,0};
    while(s.match.phase!=SB_FINISHED) {
        assert(s.screen==SB_LOCAL_TARGET);
        unsigned p=s.player;
        unsigned n=next[p]++, cell=0, count=0;
        for(unsigned ship=0;ship<SB_SHIPS;++ship) {
            unsigned length=s.match.spec.rules.lengths[ship];
            if(n<count+length) {cell=ship*10+n-count;break;} count+=length;
        }
        s.cursor=cell; s.classic_selected=false; press(&s,SB_ACTION_SELECT);
        unsigned seq=s.match.sequence;
        press(&s,SB_ACTION_CONFIRM);
        result_delay(&s);
        if(s.match.phase==SB_FINISHED) break;
        assert(s.screen==SB_LOCAL_COVER && s.player==1-p && s.match.sequence==seq+1);
        press(&s,SB_ACTION_CONFIRM); assert(s.match.sequence==seq+1 && s.presses==0);
        unlock(&s);
    }
    assert(s.match.outcome==SB_WIN_B && s.match.sequence==33 && sb_local_indicator(&s)==-1);
    assert(s.screen==SB_LOCAL_FINISHED); hidden(&s);
    assert(sb_local_begin(&s,true,rng,NULL)); assert(s.match.spec.starter==0);
    press(&s,SB_ACTION_CONFIRM); deploy(&s); deploy(&s); unlock(&s);
    press(&s,SB_ACTION_SELECT); press(&s,SB_ACTION_CONFIRM); result_delay(&s); assert(s.screen==SB_LOCAL_COVER);
    unlock(&s); s.cursor=99; press(&s,SB_ACTION_SELECT); press(&s,SB_ACTION_CONFIRM); result_delay(&s);
    unlock(&s); s.cursor=0;
    press(&s,SB_ACTION_CONFIRM); assert(s.screen==SB_LOCAL_TARGET && s.match.sequence==3);
    press(&s,SB_ACTION_CANCEL); hidden(&s); press(&s,SB_ACTION_UP); press(&s,SB_ACTION_CONFIRM);
    assert(s.match.outcome==SB_ABANDONED && sb_local_indicator(&s)==-1);
    puts("M8: full Shared Device match, privacy, handovers, explicit results, ship editing, rematch and abandonment passed");
}
