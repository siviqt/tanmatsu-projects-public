// SPDX-License-Identifier: MIT
#include "app/local_run.h"
#include "app/local.h"
#include "app/menu.h"
#include "services/preferences.h"
#include "platform/tanmatsu.h"
#include "render/renderer.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static uint32_t random_word(void *context) { (void)context; return esp_random(); }
static esp_err_t draw(const sb_local_t *s) {
    char phase[64], detail[64];
    bool single=s->match.spec.play_type==SB_SINGLE_PLAYER;
    bool escalated=s->match.spec.rules.id==SB_ESCALATED;
    const char *rules=escalated ? "Escalated" : "Classic";
    const char *difficulty=s->difficulty==SB_AI_NORMAL ? "Normal" : "Easy";
    static sb_ui_tactical_t v;
    if (sb_local_project(s, &v)) {
        v.footer_label = "Arrows: Move  F1: Help  Esc: Abandon";
        if (s->screen == SB_LOCAL_DEPLOY) {
            snprintf(phase, sizeof(phase), "Ship %u: size %u %s", s->ship+1,
                s->match.spec.rules.lengths[s->ship], s->vertical ? "V" : "H");
            unsigned placed = 0;
            for (unsigned i=0; i<SB_SHIPS; ++i) placed += s->match.fleets[s->player].ships[i].placed;
            if (single && escalated) snprintf(detail,sizeof(detail),"Placed %u/5 | %us left",placed,v.seconds_remaining);
            else if(single) snprintf(detail,sizeof(detail),"%u/5 | %s starts",placed,s->match.spec.starter ? "AI" : "You");
            else snprintf(detail, sizeof(detail), "Placed %u/5", placed);
            if(single && !escalated) snprintf(phase,sizeof(phase),"%s starts combat",s->match.spec.starter ? "AI" : "You");
            v.sequence_label = "DEPLOYMENT";
            v.phase_label = phase;
            v.action_label = "Space: Place";
            v.detail_label = s->notice[0] ? s->notice : v.placement_valid ? detail : "Overlap - move ship";
            v.footer_label = "R: Rotate  Tab: Ship  Enter: Ready  F4: Settings";
        } else if (s->screen == SB_LOCAL_RESULT) {
            v.phase_label = escalated ? "ROUND RESOLVED" : s->match.record.count > 1 ? "SHIP DESTROYED" :
                s->match.record.events[0].hit ? "HIT" : "MISS";
            snprintf(phase, sizeof(phase), "%s in %u...",
                !single && escalated && s->result_seen!=3 ? "Pass device" : s->match.phase == SB_FINISHED ? "Match result" : escalated ? "Next round" : single ? "Next turn" : "Next player", s->result_seconds);
            v.action_label = phase;
            if (escalated) {
                unsigned outgoing=0,incoming=0;
                for (unsigned i=0;i<s->match.record.count;++i) {
                    const sb_event_t *e=&s->match.record.events[i];
                    if (e->type==SB_ATTACK_EVENT && e->hit) {
                        if (e->attacker==s->player) ++outgoing; else ++incoming;
                    }
                }
                snprintf(detail,sizeof(detail),"Hits: Out %u | In %u",outgoing,incoming);
                v.detail_label=detail;
            } else v.detail_label = single && s->match.actor==1 ? "AI shot resolved" : "Shot resolved";
            v.footer_label = "Result display - controls locked";
        } else {
            v.action_label = escalated ? "Enter: Complete turn" : s->own ? "F3: Targets" : "Enter: Fire";
            snprintf(detail, sizeof(detail), "Fleet %u/5 | HP %u/17", sb_alive(&s->match.fleets[s->player]), sb_intact(&s->match.fleets[s->player]));
            v.detail_label = s->notice[0] ? s->notice : detail;
            v.footer_label = escalated ? "Space: Select  Enter: Complete  F4: Settings" : "Enter: Fire  F2/F3: View  F1: Help  F4: Settings";
        }
        return sb_renderer_tactical(&v);
    }
    static sb_menu_view_t vmenu;
    vmenu=(sb_menu_view_t){0};
    snprintf(vmenu.title,sizeof(vmenu.title),"%s / %s",single ? "Single Player" : "Shared Device",rules);
    snprintf(vmenu.subtitle,sizeof(vmenu.subtitle),"%s",single ? difficulty : "Player A / Player B");
    snprintf(vmenu.footer,sizeof(vmenu.footer),"Enter: Confirm   Esc: Return");
    switch (s->screen) {
        case SB_LOCAL_START:
            snprintf(vmenu.rows[0],96,"Five ships: 5, 4, 3, 3, 2 / 17 squares");
            if(escalated) snprintf(vmenu.rows[1],96,"%s",single ? "60s deployment / 10-minute combat / salvos" : "Private salvos / resolve together / untimed");
            else if(single) snprintf(vmenu.rows[1],96,"%s starts / one shot per turn / untimed",s->match.spec.starter ? "AI" : "You");
            else snprintf(vmenu.rows[1],96,"Player %c starts combat / deployment: A then B",'A'+s->match.spec.starter);
            snprintf(vmenu.rows[2],96,"%s",single ? "Enter: Begin deployment" : "Enter: Begin private deployment");
            snprintf(vmenu.rows[3],96,"F1: Help   F4: Settings   Esc: Match menu");
            vmenu.count=4; break;
        case SB_LOCAL_COVER:
            if(s->review_result) {
                snprintf(vmenu.title,sizeof(vmenu.title),"Player %c / Round results",'A'+s->player);
                snprintf(vmenu.rows[0],96,"Pass the device to Player %c.",'A'+s->player);
                snprintf(vmenu.rows[1],96,"Your fleet and outgoing shots remain private.");
                snprintf(vmenu.footer,sizeof(vmenu.footer),"%s","Press Space to view results");
                vmenu.count=2; vmenu.key_mask=9; break;
            }
            return sb_renderer_handover_match(s->player==1,s->match.phase==SB_DEPLOYMENT,s->presses!=0,s->match.spec.starter);
        case SB_LOCAL_EXIT:
            snprintf(vmenu.subtitle,sizeof(vmenu.subtitle),"%s",single ? "Paused / Match menu" : "Match menu");
            vmenu.selectable=true; vmenu.selected=s->menu_row; vmenu.key_mask=9;
            snprintf(vmenu.rows[0],96,"Resume"); snprintf(vmenu.rows[1],96,"Settings");
            snprintf(vmenu.rows[2],96,"Help");
            vmenu.count=3;
            if(!single) snprintf(vmenu.rows[vmenu.count++],96,"Surrender - opponent wins");
            snprintf(vmenu.rows[vmenu.count++],96,"Abandon - no winner");
            snprintf(vmenu.footer,sizeof(vmenu.footer),"Up/Down: Choose   Enter: Select   Esc: Resume"); break;
        case SB_LOCAL_PAUSED:
            snprintf(vmenu.rows[0],96,"Paused"); vmenu.count=1; break;
        default:
            snprintf(vmenu.rows[0],96,"%s",s->match.outcome==SB_WIN_A ? (single ? "You win" : "Player A wins") :
                s->match.outcome==SB_WIN_B ? (single ? "AI wins" : "Player B wins") : s->match.outcome==SB_DRAW ? "Draw" : "Match abandoned - no winner");
            snprintf(vmenu.rows[1],96,"%s",s->match.clock_expired ? "Time expired: surviving ships, then intact HP." : "Match ended");
            // Aggregate public result only; never expose fleet coordinates.
            snprintf(vmenu.rows[2],96,"%s: %u ships / %u HP",single ? "You" : "Player A",sb_alive(&s->match.fleets[0]),sb_intact(&s->match.fleets[0]));
            snprintf(vmenu.rows[3],96,"%s: %u ships / %u HP",single ? "AI" : "Player B",sb_alive(&s->match.fleets[1]),sb_intact(&s->match.fleets[1]));
            snprintf(vmenu.footer,sizeof(vmenu.footer),"Enter: Rematch   Esc: Main menu"); vmenu.count=4; break;
    }
    return sb_renderer_menu(&vmenu);
}
static bool save_settings(sb_menu_t *menu, sb_config_t *config, sb_storage_t *storage) {
    bool dirty=config->dirty;
    bool saved=storage && sb_storage_save(storage,config);
    snprintf(menu->feedback,sizeof(menu->feedback),"%s",!saved && dirty ? "Save failed - session changes kept. Retry on exit." :
        "");
    return !dirty || saved;
}
esp_err_t sb_local_run(sb_config_t *config, sb_storage_t *storage) {
    if(!config) return ESP_ERR_INVALID_ARG;
    static sb_local_t session;
    static sb_menu_t menus;
    static sb_menu_view_t view;
    sb_menu_init(&menus,config,false);
    bool menu=true, overlay=false, dirty=true, boundary=true;
    int indicator=-2;
    int64_t next_frame_us=0;
    esp_err_t err=ESP_OK;
    sb_renderer_set_font_profile(config->settings.font_size);
    if(sb_platform_apply_settings(&config->settings)!=ESP_OK)
        snprintf(menus.feedback,sizeof(menus.feedback),"Settings could not be applied. Try again.");
    sb_input_reset();
    while(err==ESP_OK) {
        if(!menu && sb_local_tick(&session,esp_timer_get_time()/1000)) dirty=true;
        int wanted=menu ? -1 : sb_local_indicator(&session);
        if(wanted!=indicator) {
            err=sb_platform_turn_indicator(wanted);
            if(err!=ESP_OK) break;
            indicator=wanted;
        }
        if(dirty) {
            int64_t remaining=next_frame_us-esp_timer_get_time();
            if(remaining>0) { vTaskDelay(pdMS_TO_TICKS((remaining+999)/1000)+1); continue; }
            next_frame_us=esp_timer_get_time()+sb_frame_interval_ms(&config->settings)*1000;
            if(menu || overlay) {
                sb_menu_view(&menus,config,storage ? sb_storage_status_text(storage) : "SD unavailable - settings not saved",&view);
                err=sb_renderer_menu(&view);
            } else err=draw(&session);
            if(err!=ESP_OK) break;
            if(!menu) sb_local_presented(&session,esp_timer_get_time()/1000);
            sb_input_set_text_mode((menu || overlay) && menus.page==SB_MENU_CALLSIGN);
            if(boundary || (!menu && session.reset_input)) {
                sb_input_reset(); session.reset_input=false; boundary=false;
            }
            dirty=false;
        }
        sb_input_event_t event;
        if(!sb_input_poll(&event)) { vTaskDelay(pdMS_TO_TICKS(sb_frame_interval_ms(&config->settings))+1); continue; }
        // Account for display/storage latency before resuming a paused match.
        // A deadline transition consumes the outgoing screen's queued input.
        if(!menu) {
            sb_local_screen_t before=session.screen;
            if(sb_local_tick(&session,esp_timer_get_time()/1000)) dirty=true;
            if(before!=session.screen) { boundary=true; continue; }
        }
        if(!menu && session.screen==SB_LOCAL_RESULT) continue;
        if(event.phase==SB_INPUT_RELEASE && (menu || overlay || session.screen!=SB_LOCAL_COVER || event.action!=SB_ACTION_FIRE)) continue;
        if(menu || overlay) {
            sb_menu_page_t before=menus.page;
            sb_menu_result_t result=sb_menu_event(&menus,config,event);
            boundary=before!=menus.page;
            if(menus.changed) {
                sb_renderer_set_font_profile(config->settings.font_size);
                if(sb_platform_apply_settings(&config->settings)!=ESP_OK)
                    snprintf(menus.feedback,sizeof(menus.feedback),"Settings apply failed; values retained. Retry edit.");
            }
            if(result==SB_MENU_EXIT) break;
            if(result==SB_MENU_SAVE) {
                bool saved=save_settings(&menus,config,storage);
                if(overlay && before==SB_MENU_SETTINGS) {
                    if(saved) {
                        // Storage time belongs to the paused settings screen.
                        sb_local_tick(&session,esp_timer_get_time()/1000);
                        sb_local_close_settings(&session); overlay=false;
                    }
                    else menus.page=SB_MENU_SAVED;
                    boundary=true;
                }
            } else if(result==SB_MENU_CLOSE) {
                if(session.screen==SB_LOCAL_SETTINGS) sb_local_close_settings(&session);
                else sb_local_event(&session,(sb_input_event_t){SB_ACTION_CANCEL,SB_INPUT_PRESS});
                overlay=false; boundary=true;
            } else if(result==SB_MENU_START) {
                bool started=menus.play_type==SB_SHARED_DEVICE ? sb_local_begin_shared(&session,menus.rules,false,random_word,NULL) :
                    sb_local_begin_single(&session,menus.rules,menus.difficulty,false,random_word,NULL,esp_timer_get_time()/1000);
                if(!started) { err=ESP_FAIL; break; }
                if(session.match.spec.play_type==SB_SINGLE_PLAYER) snprintf(session.names[0],sizeof(session.names[0]),"%s",config->settings.callsign);
                sb_local_event(&session,(sb_input_event_t){SB_ACTION_CONFIRM,SB_INPUT_PRESS});
                menu=false; boundary=true;
            }
        } else if(session.screen==SB_LOCAL_FINISHED && event.phase==SB_INPUT_PRESS) {
            if(event.action==SB_ACTION_CANCEL) { menu=true; sb_menu_init(&menus,config,false); boundary=true; }
            else if(event.action==SB_ACTION_CONFIRM) {
                bool started=session.match.spec.play_type==SB_SINGLE_PLAYER ?
                    sb_local_begin_single(&session,session.match.spec.rules.id,session.difficulty,true,random_word,NULL,esp_timer_get_time()/1000) :
                    sb_local_begin_shared(&session,session.match.spec.rules.id,true,random_word,NULL);
                if(!started) { err=ESP_FAIL; break; }
                if(session.match.spec.play_type==SB_SINGLE_PLAYER) snprintf(session.names[0],sizeof(session.names[0]),"%s",config->settings.callsign);
                sb_local_event(&session,(sb_input_event_t){SB_ACTION_CONFIRM,SB_INPUT_PRESS});
            }
        } else if(event.phase==SB_INPUT_PRESS && event.action==SB_ACTION_PAUSE && sb_local_open_settings(&session)) {
            sb_menu_init(&menus,config,true); sb_menu_settings(&menus); overlay=true; boundary=true;
        } else {
            sb_local_event(&session,event);
            if(session.screen==SB_LOCAL_SETTINGS) {
                sb_menu_init(&menus,config,true); sb_menu_settings(&menus); overlay=true; boundary=true;
            } else if(session.screen==SB_LOCAL_HELP) {
                sb_menu_init(&menus,config,true);
                sb_menu_help(&menus,session.match.spec.play_type==SB_SHARED_DEVICE ? 3 : session.match.spec.rules.id==SB_ESCALATED ? 2 : 1);
                overlay=true; boundary=true;
            }
        }
        if(!menu && session.screen==SB_LOCAL_FINISHED && session.match.outcome==SB_ABANDONED) {
            menu=true; overlay=false; sb_menu_init(&menus,config,false); boundary=true;
        }
        // Settings initialize only future setup; current match snapshots stay fixed.
        if(menu && menus.page==SB_MENU_MAIN) {
            menus.rules=!strcmp(config->settings.preferred_ruleset,"classic") ? SB_CLASSIC : SB_ESCALATED;
            menus.difficulty=!strcmp(config->settings.ai_difficulty,"easy") ? SB_AI_EASY : SB_AI_NORMAL;
        }
        dirty=true;
    }
    sb_match_abandon(&session.match);
    esp_err_t led_err=sb_platform_turn_indicator(-1);
    if(err==ESP_OK) err=led_err;
    sb_input_set_text_mode(false);
    sb_input_reset();
    if(err!=ESP_OK) ESP_LOGE("M10","Local session stopped: %s",esp_err_to_name(err));
    return err;
}
