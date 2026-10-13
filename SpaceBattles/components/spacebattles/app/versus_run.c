// SPDX-License-Identifier: MIT
#include "app/versus_run.h"
#include "app/menu.h"
#include "net/session.h"
#include "platform/radio.h"
#include "platform/sdcard.h"
#include "platform/tanmatsu.h"
#include "render/renderer.h"
#include "input/input.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif
#include <string.h>
static const char *phase_name(sb_net_phase_t phase) {
    static const char *names[]={"Compare codes","Deployment","Peer deploying","Choose targets",
        "Waiting for host","Match finished","Match abandoned"};
    return (unsigned)phase<=SB_NET_ABANDONED ? names[phase] : "Protocol error";
}
static void project(const sb_net_session_t *s,sb_ui_tactical_t *v,unsigned cursor,unsigned ship,bool vertical,bool own) {
    *v=(sb_ui_tactical_t){.ruleset=s->spec.rules.id,.play_type=SB_UI_VERSUS,.player_b=s->role==1,
        .owner_turn=s->actor==s->role,.sequence=s->sequence,.cursor=cursor,.own_board=own,.active=sb_net_can_act(s),
        .owner_name="You",.target_name="Opponent",.selected=s->target_count[s->role],.available=s->allowance[s->role],
        .deployment=s->phase==SB_NET_DEPLOY,.ship_selected=ship,
        .dual=s->phase!=SB_NET_DEPLOY,.link_available=true,
        .deployment_locked=s->surrender_sent || s->link.recovering || s->link.failed,.can_surrender=sb_net_can_surrender(s)};
    for(unsigned i=0;i<100;i++) {
        v->fleet_cells[i]=s->knowledge[1-s->role].cells[i]; v->target_cells[i]=s->knowledge[s->role].cells[i];
    }
    for(unsigned i=0;i<5;i++) if(s->fleet.ships[i].placed) {
        v->ships_placed[i]=true;
        v->ships[i].length=s->spec.rules.lengths[i];
        memcpy(v->ships[i].cells,s->fleet.ships[i].cells,v->ships[i].length);
        for(unsigned j=0;j<s->spec.rules.lengths[i];j++) v->fleet_occupied[s->fleet.ships[i].cells[j]]=true;
    }
    for(unsigned i=0;i<s->target_count[s->role];i++) v->targets[s->targets[s->role][i]]=true;
    for(unsigned i=0;i<100;i++) { v->cells[i]=own ? v->fleet_cells[i] : v->target_cells[i]; v->occupied[i]=own && v->fleet_occupied[i]; }
    if(v->deployment) {
        sb_fleet_t f=s->fleet; v->placement_valid=sb_place(&f,ship,cursor%10,cursor/10,vertical);
        for(unsigned i=0;i<s->spec.rules.lengths[ship];i++) {
            unsigned x=cursor%10+(vertical ? 0 : i),y=cursor/10+(vertical ? i : 0);
            if(x<10 && y<10) v->placement[y*10+x]=true;
        }
    }
    if(s->record.count && !v->deployment) v->effects=&s->record;
    v->phase_label=phase_name(s->phase);
    v->detail_label=s->link.recovering ? "Recovering link" : s->surrender_sent ? "Surrender pending" : NULL;
    v->notice=s->error ? s->error : s->link.recovering ? "Recovering link" :
        s->surrender_sent ? "Surrender sent | Waiting for host" :
        s->phase==SB_NET_WAIT_DEPLOY ? "Fleet ready | Waiting for opponent" :
        s->committed[s->role] ? "Salvo locked | Waiting for opponent" :
        !v->active && !v->deployment ? "Waiting for opponent" : NULL;
    v->notice_tone=s->error || s->link.failed ? SB_TONE_ERROR : SB_TONE_PENDING;

}
// These labels describe the confirmed protocol state; both players still opt in.
static const char *rematch_status(const sb_net_session_t *s) {
    if(s->error) return s->error;
    if(s->peer_left || s->phase==SB_NET_ABANDONED) return "Opponent unavailable for a rematch.";
    if(s->rematch[0] && s->rematch[1]) return "Both players agreed - starting rematch.";
    if(s->rematch[s->role]) return "Rematch requested - waiting for opponent";
    if(s->rematch[1-s->role]) return "Opponent requested a rematch.";
    return "Both players must agree to a rematch.";
}
static esp_err_t unavailable(void) {
    esp_err_t e=sb_renderer_message("Versus (LoRa)\nConfirm your local region in Settings > Radio.\nRadio transmission remains disabled.\n\nEsc / Enter: return");
    sb_input_reset();
    while(e==ESP_OK) {
        sb_input_event_t input;
        if(sb_input_poll(&input) && input.phase==SB_INPUT_PRESS && (input.action==SB_ACTION_CANCEL || input.action==SB_ACTION_CONFIRM)) break;
        vTaskDelay(pdMS_TO_TICKS(10)+1);
    }
    sb_input_reset(); return e;
}
esp_err_t sb_versus_run(sb_config_t *config,bool host,sb_ruleset_t rules,sb_storage_t *storage) {
    if(!config) return ESP_ERR_INVALID_ARG;
    const sb_net_profile_t *profile=sb_radio_profile(config->settings.region);
    if(!sb_config_radio_confirmed(config) || !profile) return unavailable();
    /* Release SD slot 0 before using the radio. ESP-Hosted retains the shared
     * controller and slot 1 from startup through every SD remount. */
    if(storage) { storage->mounted=false; storage->status=SB_STORAGE_UNAVAILABLE; }
    esp_err_t err=sb_platform_sd_shutdown(); if(err!=ESP_OK) return err;
    err=sb_radio_open(config->settings.region); if(err!=ESP_OK) return err;
    static sb_net_pairing_t pairing; static sb_net_session_t session;
    static sb_net_link_t previous_link; uint64_t previous_link_until=0;
    static sb_ui_tactical_t view, drawn_view;
    static sb_record_t drawn_effects;
    static sb_menu_view_t menu, drawn_menu; static sb_menu_t settings;
    bool drawn_tactical=false, drawn_menu_valid=false;
    bool begun=false,secure=false,peer_secure=false,verified=false,exit=false,own=true,vertical=false,reviewing=false;
    enum { NONE, MATCH_MENU, HELP, LINK, SURRENDER, SETTINGS };
    unsigned overlay=NONE, overlay_return=NONE;
    unsigned cursor=0,ship=0,menu_row=0,starter=0;
    uint32_t reviewed_record=0;
    uint32_t random=0; if(!sb_net_random(&random,sizeof(random))) { (void)sb_radio_close(); return ESP_FAIL; }
    starter=rules==SB_CLASSIC ? random&1u : 0;
    uint64_t next_draw=0, disconnected_at=0;
    sb_net_phase_t previous=SB_NET_ABANDONED; unsigned previous_sequence=0;
    sb_input_reset();
    while(!exit && err==ESP_OK) {
        uint64_t now=(uint64_t)esp_timer_get_time()/1000;
        if(sb_radio_status()!=ESP_OK) { err=sb_radio_status(); break; }
        if(sb_radio_ready() && !begun) {
            sb_spec_t spec; sb_spec_make(&spec,rules,SB_VERSUS,(uint8_t)starter);
            if(!sb_net_pair_begin(&pairing,host ? 0 : 1,host ? &spec : NULL,profile,now)) { err=ESP_FAIL; break; }
            if(host && !sb_net_pair_set_host_name(&pairing,config->settings.callsign)) { err=ESP_FAIL; break; }
            begun=true;
        }
        sb_radio_tx_event_t done;
        while(sb_radio_tx_complete(&done)) {
            if(secure) sb_net_tx_complete(&session.link,&done.packet,done.at,done.sent);
            if(previous_link_until) sb_net_tx_complete(&previous_link,&done.packet,done.at,done.sent);
        }
        sb_net_packet_t packet;
        for(unsigned n=0;n<8 && sb_radio_receive(&packet);n++) {
            if(!begun) continue;
            if(secure && sb_net_session_receive(&session,&packet,now)) { peer_secure=true; next_draw=0; }
            else if(previous_link_until && now<previous_link_until && sb_net_session_receive_previous(&session,&previous_link,&packet,now)) { /* Old rematch receipts and authenticated departure during handoff. */ }
            else (void)sb_net_pair_receive(&pairing,&packet);
        }
        if(begun && pairing.derived && (host || pairing.reply_sent) && !secure) {
            if(!sb_net_session_init(&session,&pairing,now)) { err=ESP_FAIL; break; }
            secure=true; previous=SB_NET_ABANDONED; sb_input_reset();
        }
        if(begun && !peer_secure && sb_net_pair_poll(&pairing,now,&packet)) (void)sb_radio_send(&packet);
        if(secure) {
            sb_net_tick(&session,now);
            if(previous_link_until && now>=previous_link_until) { sb_net_wipe(&previous_link,sizeof(previous_link)); previous_link_until=0; }
            bool old_ack=previous_link_until && sb_net_poll(&previous_link,now,&packet);
            if(old_ack) { if(!sb_radio_send(&packet)) sb_net_tx_complete(&previous_link,&packet,now,false); }
            else if(sb_net_session_poll(&session,now,&packet) && !sb_radio_send(&packet)) sb_net_tx_complete(&session.link,&packet,now,false);
            if(previous!=session.phase || previous_sequence!=session.sequence) {
#ifdef ESP_PLATFORM
                ESP_LOGD("Versus","role=%u phase=%u->%u seq=%lu rev=%lu queued=%u stack_free=%u",
                    session.role,(unsigned)previous,(unsigned)session.phase,(unsigned long)session.sequence,
                    (unsigned long)session.revision,session.link.count,(unsigned)uxTaskGetStackHighWaterMark(NULL));
#endif
                sb_input_reset(); next_draw=0;
                if(session.phase==SB_NET_SELECT) own=false;

                if(session.phase>=SB_NET_FINISHED) {
                    overlay=NONE;
                }
                if(session.phase==SB_NET_DEPLOY) own=true;
                previous=session.phase; previous_sequence=session.sequence;
            }
            // Non-combat endings interrupt review immediately. In particular a
            // surrender snapshot may retain the preceding shot/round record;
            // it must neither keep an old review open nor start a new one.
            bool interrupted=session.peer_left || session.phase==SB_NET_ABANDONED ||
                session.end_reason==SB_END_SURRENDER_A || session.end_reason==SB_END_SURRENDER_B;
            if(interrupted) {
                if(reviewing || overlay) { next_draw=0; sb_input_reset(); }
                reviewing=false; overlay=NONE;
            } else if(session.record.sequence && session.record.sequence!=reviewed_record) {
                reviewed_record=session.record.sequence; reviewing=true;
                sb_input_reset(); next_draw=0;
            }
            if(!session.peer_left && !session.link.failed && session.rematch_ready && !session.link.count && !session.link.ack_due && !session.link.inflight && !session.state_dirty) {
                previous_link=session.link; previous_link_until=now+session.link.profile.grace_ms;
                if(!sb_net_pair_rematch(&pairing,(const uint8_t (*)[16])session.rematch_nonce) || !sb_net_session_init(&session,&pairing,now)) { err=ESP_FAIL; break; }
                verified=sb_net_confirm(&session); own=true; ship=cursor=0; vertical=false; overlay=NONE; reviewing=false; reviewed_record=0;
                previous=SB_NET_ABANDONED; previous_sequence=0; next_draw=0; sb_input_reset(); continue;
            }
        }
        if(secure && (session.peer_left || session.link.failed || session.phase==SB_NET_ABANDONED)) {
            if(!disconnected_at) {
                disconnected_at=now; reviewing=false; overlay=NONE;
                next_draw=0; sb_input_reset();
            }
            // Keep pumping authenticated receipts while the notice is visible.
            bool receipts_done=!session.link.ack_due && !session.link.inflight &&
                (!previous_link_until || (!previous_link.ack_due && !previous_link.inflight));
            if(now-disconnected_at>=3000 && (receipts_done || now-disconnected_at>=3000+profile->retry_ms)) break;
        }
        sb_input_event_t input;
        if(sb_input_poll(&input) && input.phase!=SB_INPUT_RELEASE && !disconnected_at) {
            bool press=input.phase==SB_INPUT_PRESS;
            if(!secure) { if(press && input.action==SB_ACTION_CANCEL) exit=true; }
            else if(session.phase==SB_NET_VERIFY) {
                if(press && input.action==SB_ACTION_CANCEL) exit=true;
                if(press && input.action==SB_ACTION_CONFIRM && !verified) verified=sb_net_confirm(&session);
            } else if(reviewing && !overlay) {
                if(press && (input.action==SB_ACTION_CONFIRM || input.action==SB_ACTION_COMMIT)) reviewing=false;
            } else if(session.phase>=SB_NET_FINISHED) {
                if(press && input.action==SB_ACTION_CANCEL) exit=true;
                if(press && input.action==SB_ACTION_CONFIRM && session.phase==SB_NET_FINISHED && !session.peer_left) (void)sb_net_rematch(&session);
            } else if(overlay==SETTINGS) {
                sb_menu_page_t before=settings.page;
                sb_menu_result_t result=sb_menu_event(&settings,config,input);
                if(settings.changed) {
                    sb_renderer_set_font_profile(config->settings.font_size);
                    drawn_tactical=drawn_menu_valid=false;
                    if(sb_platform_apply_settings(&config->settings)!=ESP_OK)
                        snprintf(settings.feedback,sizeof(settings.feedback),"Settings apply failed; retry edit.");
                }
                if((result==SB_MENU_SAVE && before==SB_MENU_SETTINGS) || result==SB_MENU_CLOSE) { overlay=MATCH_MENU; sb_input_reset(); }
            } else if(overlay==SURRENDER) {
                if(press && input.action==SB_ACTION_CANCEL) { overlay=overlay_return; sb_input_reset(); }
                else if(press && input.action==SB_ACTION_CONFIRM) {
                    if(sb_net_surrender(&session)) overlay=NONE;
                    sb_input_reset();
                }
            } else if(overlay==HELP || overlay==LINK) {
                if(press && (input.action==SB_ACTION_CANCEL ||
                   (overlay==HELP && input.action==SB_ACTION_HELP) ||
                   (overlay==LINK && input.action==SB_ACTION_RADIO_STATUS))) { overlay=overlay_return; sb_input_reset(); }
            } else if(overlay==MATCH_MENU) {
                overlay_return=MATCH_MENU;
                if(press && input.action==SB_ACTION_HELP) { overlay=HELP; sb_input_reset(); }
                if(press && input.action==SB_ACTION_CANCEL) { overlay=NONE; sb_input_reset(); }
                if(input.action==SB_ACTION_UP || input.action==SB_ACTION_DOWN) menu_row=(menu_row+(input.action==SB_ACTION_UP ? 4 : 1))%5;
                if(press && input.action==SB_ACTION_CONFIRM) {
                    if(menu_row==0) overlay=NONE;
                    if(menu_row==1) { sb_menu_init(&settings,config,true); sb_menu_settings(&settings); overlay=SETTINGS; }
                    if(menu_row==2) overlay=HELP;
                    if(menu_row==3 && sb_net_can_surrender(&session)) overlay=SURRENDER;
                    if(menu_row==4) exit=true;
                    sb_input_reset();
                }
            } else {
                overlay_return=NONE;
                project(&session,&view,cursor,ship,vertical,own);
                unsigned keys=sb_ui_tactical_keys(&view), bit=0;
                if(input.action==SB_ACTION_HELP) bit=1;
                if(input.action==SB_ACTION_DEPLOY_ROTATE) bit=2;
                if(input.action==SB_ACTION_SELECT) bit=4;
                if(input.action==SB_ACTION_COMMIT) bit=8;
                if(input.action==SB_ACTION_RADIO_STATUS) bit=16;
                if(input.action==SB_ACTION_SURRENDER) bit=32;
                if(!bit || (keys&bit)) {
                    if(press && input.action==SB_ACTION_CANCEL) { overlay=MATCH_MENU; menu_row=0; sb_input_reset(); }
                    else if(press && input.action==SB_ACTION_HELP) { overlay=HELP; sb_input_reset(); }
                    else if(press && input.action==SB_ACTION_RADIO_STATUS) { overlay=LINK; sb_input_reset(); }
                    else if(press && input.action==SB_ACTION_SURRENDER) { overlay=SURRENDER; sb_input_reset(); }
                    else if((session.phase==SB_NET_DEPLOY && !view.deployment_locked) || sb_net_can_act(&session)) {
                        if(input.action==SB_ACTION_UP && cursor>=10) cursor-=10;
                        if(input.action==SB_ACTION_DOWN && cursor<90) cursor+=10;
                        if(input.action==SB_ACTION_LEFT && cursor%10) --cursor;
                        if(input.action==SB_ACTION_RIGHT && cursor%10<9) ++cursor;
                        if(session.phase==SB_NET_DEPLOY) {
                            if(press && (input.action==SB_ACTION_ROTATE || input.action==SB_ACTION_DEPLOY_ROTATE)) vertical=!vertical;
                            if(press && input.action==SB_ACTION_NEXT_PANEL) ship=(ship+1)%5;
                            unsigned last=10-session.spec.rules.lengths[ship];
                            unsigned x=cursor%10,y=cursor/10;
                            if(vertical && y>last) y=last;
                            if(!vertical && x>last) x=last;
                            cursor=y*10+x;
                            if(press && (input.action==SB_ACTION_FIRE || input.action==SB_ACTION_SELECT) && sb_net_place(&session,ship,x,y,vertical)) ship=(ship+1)%5;
                            if(press && (input.action==SB_ACTION_CONFIRM || input.action==SB_ACTION_COMMIT)) (void)sb_net_deploy(&session);
                        } else if(press) {
                            if((input.action==SB_ACTION_CONFIRM || input.action==SB_ACTION_COMMIT) && (keys&8)) (void)sb_net_lock(&session);
                            if((input.action==SB_ACTION_FIRE || input.action==SB_ACTION_SELECT) && (keys&4)) {
                                if(session.spec.rules.id==SB_CLASSIC) {
                                    uint8_t c=(uint8_t)cursor;
                                    (void)sb_net_targets(&session,&c,view.targets[cursor] ? 0 : 1);
                                } else {
                                    bool selected[100]={0}; unsigned p=session.role;
                                    for(unsigned i=0;i<session.target_count[p];i++) selected[session.targets[p][i]]=true;
                                    selected[cursor]=!selected[cursor]; uint8_t cells[5]; unsigned n=0;
                                    for(unsigned i=0;i<100 && n<5;i++) if(selected[i]) cells[n++]=(uint8_t)i;
                                    (void)sb_net_targets(&session,cells,n);
                                }
                            }
                        }
                    }
                }
            }
            next_draw=0;
        }
        if(exit) break;
        if(now>=next_draw) {
            next_draw=now+200;
            bool tactical=secure && !disconnected_at && !overlay && (reviewing || (session.phase>=SB_NET_DEPLOY && session.phase<=SB_NET_EXCHANGE));
            if(tactical) {
                project(&session,&view,cursor,ship,vertical,own);
                if(reviewing) { view.result=true; view.phase_label=session.spec.rules.id==SB_CLASSIC ? "Shot resolved" : "Round resolved"; view.action_label="Review results"; }
                // No timers/animations change this view between events. Compare the
                // projected model, not transport counters or heartbeat traffic.
                // project() uses immutable literal labels; copy mutable effects too.
                bool same=drawn_tactical && !memcmp(&view,&drawn_view,sizeof(view)) &&
                    (!view.effects || !memcmp(view.effects,&drawn_effects,sizeof(drawn_effects)));
                if(!same) {
                    err=sb_renderer_tactical(&view);
                    drawn_view=view;
                    if(view.effects) drawn_effects=*view.effects;
                    drawn_tactical=true; drawn_menu_valid=false;
                }

            } else {
                menu=(sb_menu_view_t){.surface=SB_MENU_SURFACE_STATUS,.tone=SB_TONE_PENDING}; snprintf(menu.title,sizeof(menu.title),"Versus / %s",host ? "Host" : "Guest");
                snprintf(menu.footer,sizeof(menu.footer),"Esc: Cancel");
                if(disconnected_at) {
                    snprintf(menu.title,sizeof(menu.title),"%s",session.peer_left ? "Opponent disconnected" : "Connection lost");
                    menu.tone=session.peer_left ? SB_TONE_PENDING : SB_TONE_ERROR;
                    snprintf(menu.rows[menu.count++],96,"%s",session.peer_left ? "The other player left the session." :
                        session.error ? session.error : "The other device is no longer responding.");
                    if(session.outcome!=SB_ONGOING && session.outcome!=SB_ABANDONED)
                        snprintf(menu.rows[menu.count++],96,"%s",sb_result_message(sb_result_for(session.outcome,session.end_reason,session.role)));
                    snprintf(menu.footer,sizeof(menu.footer),"Returning to main menu...");
                } else if(!begun) { snprintf(menu.rows[0],96,"Checking radio identity"); menu.count=1; }
                else if(!secure) {
                    menu.tone=pairing.failed ? SB_TONE_ERROR : SB_TONE_PENDING;
                    sb_radio_stats_t stats=sb_radio_stats();
                    snprintf(menu.rows[0],96,"%s",pairing.failed ? "Pairing failed / cancel and retry" : pairing.bound ? "Peer found - exchanging keys" : host ? "Advertising - waiting for guest" : "Searching for host");
                    snprintf(menu.rows[1],96,"%s | Radio packets: TX %lu / RX %lu",config->settings.region,
                        (unsigned long)stats.tx_packets,(unsigned long)stats.rx_packets);
                    snprintf(menu.rows[2],96,"Receive recoveries: %lu",(unsigned long)stats.rx_recoveries); menu.count=3;
                }
                else if(session.phase==SB_NET_VERIFY) {
                    menu.surface=SB_MENU_SURFACE_VERIFY;
                    menu.tone=verified ? SB_TONE_SUCCESS : SB_TONE_PENDING;
                    snprintf(menu.headline,sizeof(menu.headline),"%06u",sb_net_pair_code(&pairing));
                    snprintf(menu.subtitle,96,"%s / starter: %s",session.spec.rules.id==SB_CLASSIC ? "Classic" : "Escalated",session.spec.starter ? "Guest" : "Host");
                    snprintf(menu.rows[0],96,"Verification code: %06u",sb_net_pair_code(&pairing));
                    snprintf(menu.rows[1],96,"Host: %s / compare codes and rules.",pairing.host_name);
                    snprintf(menu.rows[2],96,"%s",verified ? "Confirmed. Waiting for opponent." : "Enter ONLY if both codes and rules match.");
                    snprintf(menu.rows[3],96,"%s",session.spec.rules.id==SB_CLASSIC ? "Alternating turns / fleet 5,4,3,3,2" : "Simultaneous salvos / fleet 5,4,3,3,2"); menu.count=4;
                } else if(overlay==SETTINGS) {
                    sb_menu_view(&settings,config,"Settings saved after leaving match",&menu);
                } else if(overlay==MATCH_MENU) {
                    menu.surface=SB_MENU_SURFACE_MATCH; menu.tone=SB_TONE_NORMAL;
                    menu.selectable=true; menu.selected=menu_row; menu.count=5; menu.key_mask=1;
                    snprintf(menu.rows[0],96,"Resume"); snprintf(menu.rows[1],96,"Settings");
                    snprintf(menu.rows[2],96,"Help"); snprintf(menu.rows[3],96,"%s",sb_net_can_surrender(&session) ? "Surrender - opponent wins" : "Surrender unavailable after submission");
                    if(!sb_net_can_surrender(&session)) menu.disabled_rows=1u<<3;
                    snprintf(menu.rows[4],96,"Leave match - opponent wins");
                    snprintf(menu.subtitle,96,"Network stays active");
                    snprintf(menu.footer,96,"Up/Down: Choose / Enter: Select / Esc: Resume");
                } else if(overlay==SURRENDER) {
                    menu.surface=SB_MENU_SURFACE_DIALOG; menu.tone=SB_TONE_ERROR;
                    snprintf(menu.title,64,"Surrender?");
                    snprintf(menu.rows[0],96,"%s",sb_net_can_surrender(&session) ? "Your opponent wins." : "Surrender is currently unavailable."); menu.count=1;
                    snprintf(menu.footer,96,"Enter: Confirm surrender / Esc: Return");
                } else if(overlay==LINK) {
                    menu.tone=session.link.failed ? SB_TONE_ERROR : session.link.recovering ? SB_TONE_PENDING : SB_TONE_SUCCESS;
                    sb_radio_stats_t stats=sb_radio_stats();
                    snprintf(menu.title,64,"Link / %s",host ? "Host" : "Guest");
                    snprintf(menu.rows[0],96,"%s",session.link.failed ? "Link failed" : session.link.recovering ? "Recovering link" : "Encrypted link");
                    snprintf(menu.rows[1],96,"TX %lu / RX %lu",(unsigned long)stats.tx_packets,(unsigned long)stats.rx_packets);
                    snprintf(menu.rows[2],96,"Receive recoveries: %lu",(unsigned long)stats.rx_recoveries); menu.count=3;
                    snprintf(menu.footer,96,"Esc: Return"); menu.key_mask=menu.return_keys=16;
                } else if(overlay==HELP) {
                    menu.surface=SB_MENU_SURFACE_PLAIN; menu.tone=SB_TONE_NORMAL;
                    snprintf(menu.title,64,"Help / %s",session.spec.rules.id==SB_CLASSIC ? "Classic" : "Escalated");
                    snprintf(menu.rows[0],96,"Arrows: Move / F3 or Space: Place ships");
                    snprintf(menu.rows[1],96,"F2 / R: Rotate / Tab: Next ship");
                    snprintf(menu.rows[2],96,"F3 / Space: Select or deselect targets");
                    snprintf(menu.rows[3],96,"F4 / Enter: Ready, fire, submit or continue");
                    snprintf(menu.rows[4],96,"F5: Link / F6: Surrender / Esc: Match menu");
                    snprintf(menu.rows[5],96,"%s",session.spec.rules.id==SB_CLASSIC ? "Select one target, then confirm to fire." : "One shot per surviving ship; salvos resolve together.");
                    menu.count=6; menu.key_mask=menu.return_keys=1; snprintf(menu.footer,96,"Esc: Return");
                } else if(session.phase>=SB_NET_FINISHED) {
                    sb_ui_result_style(&menu,sb_result_for(session.outcome,session.end_reason,session.role));
                    menu.count=0;
                    if(session.end_reason!=SB_END_NONE && session.end_reason!=SB_END_COMBAT)
                        snprintf(menu.rows[menu.count++],96,"%s",sb_result_message(sb_result_for(session.outcome,session.end_reason,session.role)));
                    snprintf(menu.rows[menu.count++],96,"%s",rematch_status(&session));
                    snprintf(menu.footer,96,"%s",session.peer_left || session.phase==SB_NET_ABANDONED || session.rematch[session.role] ? "Esc: Main menu" : "Enter: Rematch / Esc: Main menu");
                } else { snprintf(menu.rows[0],96,"%s",phase_name(session.phase)); menu.count=1;
                    if(session.error) { snprintf(menu.rows[1],96,"%s",session.error); menu.count=2; }

                }
                if(!drawn_menu_valid || memcmp(&menu,&drawn_menu,sizeof(menu))!=0) {
                    err=sb_renderer_menu(&menu);
                    drawn_menu=menu; drawn_menu_valid=true; drawn_tactical=false;
                }
            }
        }
        sb_input_set_text_mode(overlay==SETTINGS && settings.page==SB_MENU_CALLSIGN);
        vTaskDelay(pdMS_TO_TICKS(10)+1);
    }
    sb_input_set_text_mode(false);
    /* Every secure exit sends a reliable departure, including verification,
     * results and rematch waiting. Drain existing commands without sequence
     * gaps, then wait for the LEAVE receipt (not merely a TX completion).
     * Radio pacing stays worker-owned; shutdown is bounded by the grace window. */
    if(err==ESP_OK && secure && sb_net_leave(&session)) {
        (void)sb_renderer_message("Leaving session...");
        uint64_t until=(uint64_t)esp_timer_get_time()/1000+profile->grace_ms;
        while((uint64_t)esp_timer_get_time()/1000<until && sb_radio_status()==ESP_OK) {
            uint64_t now=(uint64_t)esp_timer_get_time()/1000;
            sb_radio_tx_event_t done;
            while(sb_radio_tx_complete(&done))
                sb_net_tx_complete(&session.link,&done.packet,done.at,done.sent);
            sb_net_packet_t packet;
            for(unsigned n=0;n<8 && sb_radio_receive(&packet);n++) (void)sb_net_session_receive(&session,&packet,now);
            if(session.peer_left || (session.leave_command && !session.link.count && !session.link.inflight)) break;
            if(sb_net_session_poll(&session,now,&packet) && !sb_radio_send(&packet)) sb_net_tx_complete(&session.link,&packet,now,false);
            vTaskDelay(pdMS_TO_TICKS(10)+1);
        }
    }
    esp_err_t stopped=sb_radio_close(); if(err==ESP_OK) err=stopped;
    if(err==ESP_OK) {
        sb_sd_mount_result_t sd=sb_platform_sd_mount();
        if(storage) {
            storage->mounted=sd.error==ESP_OK && !sd.worker_pending;
            storage->status=!storage->mounted ? SB_STORAGE_UNAVAILABLE :
                storage->protected_files ? SB_STORAGE_PROTECTED : SB_STORAGE_READY;
        }
        if(sd.worker_pending) err=sd.error==ESP_OK ? ESP_FAIL : sd.error;
    }
    sb_net_close(&session); sb_net_wipe(&previous_link,sizeof(previous_link)); sb_net_wipe(&pairing,sizeof(pairing)); sb_input_reset(); return err;
}
