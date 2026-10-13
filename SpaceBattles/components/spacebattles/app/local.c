// SPDX-License-Identifier: MIT
#include "app/local.h"
#include <stdio.h>
#include <string.h>

static void screen(sb_local_t *s, sb_local_screen_t next) {
    s->screen = next;
    s->reset_input = true;
    s->space_down = false;
}
static void cover(sb_local_t *s, unsigned player) {
    s->player = player;
    s->cursor = s->ship = s->presses = s->view_mode = 0;
    s->classic_selected=false;
    s->vertical = false;
    s->own = s->match.phase == SB_DEPLOYMENT;
    s->notice[0] = 0;
    screen(s, SB_LOCAL_COVER);
}
bool sb_local_begin_shared(sb_local_t *s, sb_ruleset_t rules, bool rematch,
                           sb_random_fn random, void *context) {
    if (!s || !random) return false;
    sb_series_t series = rematch ? s->series : (sb_series_t){0};
    uint8_t starter;
    sb_spec_t spec;
    if (!sb_series_starter(&series, rules, rematch, random, context, &starter) ||
        !sb_spec_make(&spec, rules, SB_SHARED_DEVICE, starter)) return false;
    *s = (sb_local_t){.series=series,.names={"Player A","Player B"}};
    if (!sb_match_init(&s->match, &spec, 0)) return false;
    screen(s, SB_LOCAL_START);
    return true;
}
bool sb_local_begin(sb_local_t *s, bool rematch, sb_random_fn random, void *context) {
    return sb_local_begin_shared(s,SB_CLASSIC,rematch,random,context);
}
static bool single(const sb_local_t *s) { return s->match.spec.play_type == SB_SINGLE_PLAYER; }
bool sb_local_begin_single(sb_local_t *s, sb_ruleset_t ruleset,
                           sb_ai_difficulty_t difficulty, bool rematch,
                           sb_random_fn random, void *context, uint64_t now_ms) {
    if (!s || !random || (difficulty < SB_AI_EASY || difficulty > SB_AI_HARD)) return false;
    sb_series_t series = rematch ? s->series : (sb_series_t){0};
    uint8_t starter;
    sb_spec_t spec;
    if (!sb_series_starter(&series, ruleset, rematch, random, context, &starter) ||
        !sb_spec_make(&spec, ruleset, SB_SINGLE_PLAYER, starter)) return false;
    *s = (sb_local_t){.series=series, .difficulty=difficulty, .random=random, .random_context=context,.names={"COMMANDER","AI"}};
    if (!sb_match_init(&s->match, &spec, now_ms)) return false;
    screen(s, SB_LOCAL_START);
    return true;
}
static void resolved(sb_local_t *s) {
    if (!sb_match_resolve(&s->match, s->match.sequence, &s->match.record)) return;
    s->result_seen=1u<<s->player;
    s->view_mode=0;
    s->result_started = false;
    screen(s, SB_LOCAL_RESULT);
}
// This adapter owns the private match; the chooser sees only public knowledge.
static void ai_target(sb_local_t *s) {
    sb_match_t *m=&s->match;
    if (m->phase != SB_TARGETING || m->paused || m->salvo_locked[1] ||
        (m->spec.rules.id == SB_CLASSIC && m->actor != 1)) return;
    uint8_t cells[SB_SALVO];
    size_t count=sb_ai_choose(&m->knowledge[1], m->spec.rules.id, s->difficulty,
        sb_allowance(m,1), s->random, s->random_context, cells);
    if (m->spec.rules.id == SB_CLASSIC) {
        if (count && sb_classic_fire(m,1,m->sequence,cells[0])) {
            s->own=true;
            s->cursor=cells[0];
            resolved(s);
        }
    } else if (sb_select(m,1,m->sequence,cells,count)) {
        sb_escalated_lock(m,1,m->sequence,true);
    }
}
static void single_target(sb_local_t *s) {
    s->player=0;
    s->classic_selected=false;
    s->own=false;
    s->cursor=0;
    s->notice[0]=0;
    screen(s,SB_LOCAL_TARGET);
    ai_target(s);
}
static void fire(sb_local_t *s) {
    if (!s->classic_selected || !sb_classic_fire(&s->match, s->player, s->match.sequence, s->classic_target)) return;
    s->cursor=s->classic_target;
    resolved(s);
}
void sb_local_presented(sb_local_t *s, uint64_t now_ms) {
    (void)now_ms;
    if(s && s->screen==SB_LOCAL_RESULT) s->result_started=true;
}
bool sb_local_tick(sb_local_t *s, uint64_t now_ms) {
    if(!s) return false;
    if(single(s)) sb_match_tick(&s->match,now_ms,s->random,s->random_context);
    if(s->match.phase==SB_LOCKED && s->screen!=SB_LOCAL_RESULT) { resolved(s); return true; }
    return false;
}
static void continue_result(sb_local_t *s) {
    if(!s->result_started) return;
    if(!single(s) && s->match.spec.rules.id==SB_ESCALATED && s->result_seen!=3) {
        cover(s,1-s->player); s->review_result=true;
    } else if(s->match.phase==SB_FINISHED) screen(s,SB_LOCAL_FINISHED);
    else if(sb_match_advance(&s->match,s->match.sequence)) {
        if(single(s)) single_target(s); else cover(s,s->match.actor);
    }
}
static void next_ship(sb_local_t *s) {
    s->ship = (s->ship + 1) % SB_SHIPS;
    const sb_ship_t *ship = &s->match.fleets[s->player].ships[s->ship];
    if (ship->placed) {
        s->cursor = ship->cells[0];
        s->vertical = ship->cells[1] - ship->cells[0] == SB_BOARD_SIDE;
    }
}
// Keep the complete candidate visible at the board edges, including on rotate.
static void clamp_ship(sb_local_t *s) {
    if (s->screen != SB_LOCAL_DEPLOY) return;
    unsigned last = SB_BOARD_SIDE - s->match.spec.rules.lengths[s->ship];
    unsigned x=s->cursor%10, y=s->cursor/10;
    if (s->vertical && y>last) y=last;
    if (!s->vertical && x>last) x=last;
    s->cursor=y*10+x;
}
bool sb_local_open_settings(sb_local_t *s) {
    if (!s || (s->screen!=SB_LOCAL_START && s->screen!=SB_LOCAL_COVER &&
        s->screen!=SB_LOCAL_DEPLOY && s->screen!=SB_LOCAL_TARGET &&
        s->screen!=SB_LOCAL_EXIT && s->screen!=SB_LOCAL_PAUSED)) return false;
    s->settings_return=s->screen;
    if(single(s)) sb_match_pause(&s->match,true,s->match.now_ms);
    s->presses=0;
    screen(s,SB_LOCAL_SETTINGS);
    return true;
}
void sb_local_close_settings(sb_local_t *s) {
    if(!s || s->screen!=SB_LOCAL_SETTINGS) return;
    if(single(s) && s->settings_return!=SB_LOCAL_EXIT && s->settings_return!=SB_LOCAL_PAUSED)
        sb_match_pause(&s->match,false,s->match.now_ms);
    s->presses=0;
    screen(s,s->settings_return);
}
void sb_local_event(sb_local_t *s, sb_input_event_t e) {
    if (!s || s->screen == SB_LOCAL_SETTINGS) return;
    if(s->screen==SB_LOCAL_RESULT) {
        if(e.phase==SB_INPUT_PRESS && (e.action==SB_ACTION_CONFIRM || e.action==SB_ACTION_COMMIT)) continue_result(s);
        return;
    }
    if(s->screen==SB_LOCAL_SURRENDER) {
        if(e.phase!=SB_INPUT_PRESS) return;
        if(e.action==SB_ACTION_CANCEL) screen(s,s->surrender_return);
        else if(e.action==SB_ACTION_CONFIRM && sb_match_surrender(&s->match,s->player)) screen(s,SB_LOCAL_FINISHED);
        return;
    }
    if(s->screen==SB_LOCAL_DEPLOY || s->screen==SB_LOCAL_TARGET) {
        sb_ui_tactical_t view;
        if(!sb_local_project(s,&view)) return;
        unsigned mask=sb_ui_tactical_keys(&view), bit=0;
        if(e.action==SB_ACTION_HELP) bit=1;
        if(e.action==SB_ACTION_DEPLOY_ROTATE) bit=2;
        if(e.action==SB_ACTION_SELECT) bit=4;
        if(e.action==SB_ACTION_COMMIT) bit=8;
        if(e.action==SB_ACTION_RADIO_STATUS) bit=16;
        if(e.action==SB_ACTION_SURRENDER) bit=32;
        if(bit && !(mask&bit)) return;
        if(e.action==SB_ACTION_SURRENDER && e.phase==SB_INPUT_PRESS) {
            s->surrender_return=s->screen; screen(s,SB_LOCAL_SURRENDER); return;
        }
        if(e.action==SB_ACTION_DEPLOY_ROTATE) e.action=SB_ACTION_ROTATE;
        if(e.action==SB_ACTION_SELECT) e.action=SB_ACTION_FIRE;
        if(e.action==SB_ACTION_COMMIT) e.action=SB_ACTION_CONFIRM;
    } else if(e.action==SB_ACTION_COMMIT || e.action==SB_ACTION_DEPLOY_ROTATE ||
              e.action==SB_ACTION_SELECT || e.action==SB_ACTION_SURRENDER || e.action==SB_ACTION_RADIO_STATUS) return;
    if (s->screen == SB_LOCAL_COVER && e.action == SB_ACTION_FIRE) {
        if(e.phase == SB_INPUT_PRESS) {
            // The screen boundary resets input and blocks held Space until release.
            // Consume this press here; it must never reach placement/targeting.
            if(s->review_result) {
                s->review_result=false; s->result_seen|=1u<<s->player;
                s->result_started=false;
                screen(s,SB_LOCAL_RESULT);
            } else screen(s,s->match.phase==SB_DEPLOYMENT ? SB_LOCAL_DEPLOY : SB_LOCAL_TARGET);
        }
        return;
    }
    if (e.phase == SB_INPUT_RELEASE) return;
    if (e.phase == SB_INPUT_REPEAT && !(e.action >= SB_ACTION_UP && e.action <= SB_ACTION_RIGHT)) return;
    if (s->screen == SB_LOCAL_HELP || s->screen == SB_LOCAL_EXIT || s->screen == SB_LOCAL_PAUSED) {
        if (e.phase != SB_INPUT_PRESS) return;
        if(s->screen==SB_LOCAL_EXIT && (e.action==SB_ACTION_UP || e.action==SB_ACTION_DOWN)) {
            unsigned count=5;
            s->menu_row=(s->menu_row+(e.action==SB_ACTION_UP ? count-1 : 1))%count;
        } else if(s->screen==SB_LOCAL_EXIT && e.action==SB_ACTION_CONFIRM && s->menu_row==1) {
            sb_local_open_settings(s);
        } else if(s->screen==SB_LOCAL_EXIT && (e.action==SB_ACTION_HELP || (e.action==SB_ACTION_CONFIRM && s->menu_row==2))) {
            s->help_return=SB_LOCAL_EXIT;
            screen(s,SB_LOCAL_HELP);
        } else if(s->screen==SB_LOCAL_EXIT && e.action==SB_ACTION_CONFIRM && s->menu_row>=3) {
            if(s->menu_row==3) {
                if(s->match.salvo_locked[s->player]) return;
                s->surrender_return=SB_LOCAL_EXIT; screen(s,SB_LOCAL_SURRENDER);
            } else { sb_match_abandon(&s->match); screen(s,SB_LOCAL_FINISHED); }
        } else if(e.action==SB_ACTION_CANCEL || (s->screen==SB_LOCAL_EXIT && e.action==SB_ACTION_CONFIRM) ||
                  (s->screen==SB_LOCAL_HELP && e.action==SB_ACTION_HELP) ||
                  (s->screen==SB_LOCAL_PAUSED && e.action==SB_ACTION_COMMIT)) {
            sb_local_screen_t next=s->screen==SB_LOCAL_HELP ? s->help_return : s->resume;
            if(single(s) && next!=SB_LOCAL_EXIT) sb_match_pause(&s->match,false,s->match.now_ms);
            s->presses=0;
            screen(s,next);
        }
        return;
    }
    if (e.phase == SB_INPUT_PRESS && e.action == SB_ACTION_HELP) {
        if (single(s)) sb_match_pause(&s->match,true,s->match.now_ms);
        s->help_return = s->screen;
        s->presses = 0;
        screen(s, SB_LOCAL_HELP);
        return;
    }
    if (e.phase == SB_INPUT_PRESS && e.action == SB_ACTION_CANCEL) {
        if (s->match.phase != SB_FINISHED) {
            if (single(s)) sb_match_pause(&s->match,true,s->match.now_ms);
            s->resume = s->screen;
            s->presses = 0;
            s->menu_row=0;
            screen(s, SB_LOCAL_EXIT);
        }
        return;
    }
    if (s->screen == SB_LOCAL_START && e.action == SB_ACTION_CONFIRM) {
        if (!single(s)) cover(s,0);
        else {
            // Deployment starts at Begin, excluding time spent on the setup summary.
            s->match.phase_started_ms=s->match.now_ms;
            sb_completion_t completion;
            if (!sb_complete(&s->match.fleets[1],SB_PLACEMENT_BUDGET,s->random,s->random_context,&completion)) return;
            sb_match_ready(&s->match,1);
            s->own=true;
            screen(s,SB_LOCAL_DEPLOY);
        }
        return;
    }
    if (s->screen == SB_LOCAL_START || s->screen == SB_LOCAL_COVER || s->screen == SB_LOCAL_FINISHED) return;
    if (e.action >= SB_ACTION_UP && e.action <= SB_ACTION_RIGHT) {
        if(s->screen==SB_LOCAL_TARGET && s->own) return;
        if (e.action == SB_ACTION_UP && s->cursor >= 10) s->cursor -= 10;
        if (e.action == SB_ACTION_DOWN && s->cursor < 90) s->cursor += 10;
        if (e.action == SB_ACTION_LEFT && s->cursor % 10) --s->cursor;
        if (e.action == SB_ACTION_RIGHT && s->cursor % 10 < 9) ++s->cursor;
        clamp_ship(s);
        s->notice[0] = 0;
        return;
    }
    if (e.phase != SB_INPUT_PRESS) return;
    if (s->screen == SB_LOCAL_DEPLOY) {
        if (e.action == SB_ACTION_ROTATE) s->vertical = !s->vertical;
        if (e.action == SB_ACTION_NEXT_PANEL) next_ship(s);
        if (e.action == SB_ACTION_FIRE) {
            bool ok = sb_match_place(&s->match, s->player, s->ship, s->cursor % 10, s->cursor / 10, s->vertical);
            snprintf(s->notice, sizeof(s->notice), "%s", ok ? "Ship placed" : "Invalid placement");
            if (ok) next_ship(s);
        }
        clamp_ship(s);
        if (e.action == SB_ACTION_ROTATE || e.action == SB_ACTION_NEXT_PANEL) s->notice[0] = 0;
        if (e.action == SB_ACTION_CONFIRM) {
            if (sb_match_ready(&s->match, s->player)) {
                if (single(s)) single_target(s);
                else cover(s, s->match.phase == SB_DEPLOYMENT ? 1 : s->match.actor);
            }
            else snprintf(s->notice, sizeof(s->notice), "Place all five ships first");
        }
    } else if (s->screen == SB_LOCAL_TARGET) {

        if (s->match.spec.rules.id == SB_ESCALATED) {
            if (!s->own && e.action == SB_ACTION_FIRE) {
                uint8_t cells[SB_SALVO];
                unsigned count=s->match.selected[s->player], i=0;
                memcpy(cells,s->match.selection[s->player],count);
                while (i<count && cells[i]!=s->cursor) ++i;
                if (i<count) { memmove(cells+i,cells+i+1,count-i-1); --count; }
                else if (count<sb_allowance(&s->match,s->player)) cells[count++]=(uint8_t)s->cursor;
                if (!sb_select(&s->match,s->player,s->match.sequence,cells,count))
                    snprintf(s->notice,sizeof(s->notice),"Already targeted");
            }
            if (!s->own && e.action == SB_ACTION_CONFIRM) {
                if (sb_escalated_lock(&s->match,s->player,s->match.sequence,true)) {
                    s->own=false;
                    if(s->match.phase==SB_LOCKED) resolved(s);
                    else if(!single(s)) cover(s,1-s->player);
                }
            }
            return;
        }
        if(e.action==SB_ACTION_FIRE && s->match.knowledge[s->player].cells[s->cursor]==SB_UNKNOWN) {
            if(s->classic_selected && s->classic_target==s->cursor) s->classic_selected=false;
            else { s->classic_selected=true; s->classic_target=s->cursor; }
        }
        if(e.action==SB_ACTION_CONFIRM) fire(s);
    }
}
bool sb_local_project(const sb_local_t *s, sb_ui_tactical_t *v) {
    if (!v) return false;
    *v = (sb_ui_tactical_t){0};
    if (!s || (s->screen != SB_LOCAL_DEPLOY && s->screen != SB_LOCAL_TARGET && s->screen != SB_LOCAL_RESULT)) return false;
    bool own = s->screen == SB_LOCAL_DEPLOY || s->own;
    *v = (sb_ui_tactical_t){.ruleset=s->match.spec.rules.id, .play_type=single(s) ? SB_UI_SINGLE_PLAYER : SB_UI_SHARED_DEVICE,
        .own_board=own, .active=!single(s) || s->match.spec.rules.id==SB_ESCALATED || s->match.actor==0, .player_b=s->player==1,
        .sequence=s->match.sequence, .cursor=s->cursor, .available=s->match.phase==SB_TARGETING ? sb_allowance(&s->match,s->player) : s->match.allowance[s->player],
        .selected=s->match.selected[s->player], .can_surrender=s->match.phase!=SB_FINISHED && !s->match.salvo_locked[s->player]};
    v->notice=s->notice;
    v->owner_turn=s->match.actor==s->player;
    v->dual=s->screen!=SB_LOCAL_DEPLOY;
    v->result=s->screen==SB_LOCAL_RESULT;
    v->view_mode=0;
    if(s->match.spec.rules.id==SB_CLASSIC && s->screen==SB_LOCAL_TARGET) {
        v->selected=s->classic_selected;
        if(s->classic_selected) v->targets[s->classic_target]=true;
    }
    v->owner_name=s->names[s->player]; v->target_name=s->names[1-s->player];
    for(unsigned i=0;i<SB_SHIPS;i++) if(s->match.fleets[s->player].ships[i].placed) {
        v->ships[i].length=s->match.spec.rules.lengths[i];
        memcpy(v->ships[i].cells,s->match.fleets[s->player].ships[i].cells,v->ships[i].length);
    }
    if(v->dual) {
        memcpy(v->fleet_cells,s->match.knowledge[1-s->player].cells,sizeof(v->fleet_cells));
        memcpy(v->target_cells,s->match.knowledge[s->player].cells,sizeof(v->target_cells));
        for(unsigned ship=0;ship<SB_SHIPS;++ship) if(s->match.fleets[s->player].ships[ship].placed)
            for(unsigned j=0;j<s->match.spec.rules.lengths[ship];++j) v->fleet_occupied[s->match.fleets[s->player].ships[ship].cells[j]]=true;
    }
    if (v->dual || !own) for (unsigned i=0; i<s->match.selected[s->player]; ++i)
        v->targets[s->match.selection[s->player][i]]=true;
    memcpy(v->cells, s->match.knowledge[own ? 1-s->player : s->player].cells, sizeof(v->cells));
    if (own) for (unsigned i=0; i<SB_SHIPS; ++i) {
        const sb_ship_t *ship = &s->match.fleets[s->player].ships[i];
        if (ship->placed) for (unsigned j=0; j<s->match.spec.rules.lengths[i]; ++j) v->occupied[ship->cells[j]]=true;
    }
    if (s->screen == SB_LOCAL_RESULT) {
        v->effects = &s->match.record;
        if (s->match.spec.rules.id == SB_CLASSIC) v->targets[s->cursor] = true;
    }
    if (s->screen == SB_LOCAL_DEPLOY) {
        v->deployment = true;
        v->ship_selected = s->ship;
        for (unsigned i=0; i<SB_SHIPS; ++i) v->ships_placed[i] = s->match.fleets[s->player].ships[i].placed;
        sb_fleet_t candidate = s->match.fleets[s->player];
        v->placement_valid = sb_place(&candidate, s->ship, s->cursor%10, s->cursor/10, s->vertical);
        for (unsigned j=0; j<s->match.spec.rules.lengths[s->ship]; ++j)
            v->placement[s->cursor+j*(s->vertical ? 10 : 1)] = true;
    }
    return true;
}
int sb_local_indicator(const sb_local_t *s) {
    return !s || single(s) || (s->match.phase == SB_FINISHED && s->screen==SB_LOCAL_FINISHED) || s->screen == SB_LOCAL_START ? -1 : (int)s->player;
}
