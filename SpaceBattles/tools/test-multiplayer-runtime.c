// SPDX-License-Identifier: MIT
#include <assert.h>
#include "services/storage.h"
#include "app/versus_run.c"
static const sb_net_profile_t synthetic={.max_packet=253,.duty_permille=1000,.retry_ms=2000,.heartbeat_ms=6000,
    .silence_ms=16000,.grace_ms=30000,.validated=true};
static sb_net_pairing_t remote_pair;
static sb_net_session_t remote;
static sb_net_packet_t inbox[64];
static unsigned in_head,in_count,remote_role,finished,pairings,frames,mounts,unmounts,exit_step;
static uint64_t clock_ms, idle_started, combat_idle_started;
static unsigned idle_frames, combat_idle_frames;
static bool idle_checked, combat_idle_checked, peer_rematch_seen;
static bool sd_missing;
static sb_storage_t *active_storage;
static bool supported,radio_open,tactical,remote_secure,remote_peer_secure,skip_input,closed;
static sb_ui_tactical_t last_view;
static sb_menu_view_t last_menu;
static sb_ruleset_t runtime_rules;
static sb_net_link_t remote_old;
static bool old_valid, surrender_case, opponent_surrender;
static unsigned surrender_step, leave_case;
static bool leave_requested, leave_sent, leave_delivered, saw_leave_win;
static uint64_t leave_requested_at;
static unsigned lost_rematch_ack, ui_step;
static uint32_t local_rematch_id, local_leave_id;
static unsigned leave_transmissions;
static bool power_off, leave_dropped, leave_ack_dropped, leave_ack_seen, saw_disconnect;
static uint64_t disconnect_notice_at, leaving_at;
static pax_buf_t framebuffer;
static void queue(const sb_net_packet_t *p) {
    if(leave_case==12 && local_leave_id && !leave_ack_dropped && p->size>=46 &&
       sb_net_get32(p->bytes+21)==local_leave_id) { leave_ack_dropped=true; return; }
    if(!lost_rematch_ack && local_rematch_id && p->size>=46 && p->bytes[3]==SB_N_ACK && sb_net_get32(p->bytes+21)==local_rematch_id) { ++lost_rematch_ack; return; }
    assert(in_count<64); inbox[(in_head+in_count++)%64]=*p; }
static void new_remote(unsigned starter) {
    sb_spec_t spec; assert(sb_spec_make(&spec,runtime_rules,SB_VERSUS,(uint8_t)starter));
    assert(sb_net_pair_begin(&remote_pair,remote_role,remote_role ? NULL : &spec,&synthetic,clock_ms));
    remote_secure=remote_peer_secure=false; ++pairings;
}
static void pump_remote(void) {
    if(!radio_open || power_off) return;
    sb_net_packet_t p;
    if(remote_pair.derived && (!remote_role || remote_pair.reply_sent) && !remote_secure) {
        assert(sb_net_session_init(&remote,&remote_pair,clock_ms)); assert(sb_net_confirm(&remote)); remote_secure=true;
    }
    if(!remote_peer_secure && sb_net_pair_poll(&remote_pair,clock_ms,&p)) queue(&p);
    if(!remote_secure) return;
    sb_net_tick(&remote,clock_ms);
    if(remote.peer_left) {
        if(sb_net_session_poll(&remote,clock_ms,&p)) { sb_net_tx_complete(&remote.link,&p,clock_ms,true); queue(&p); }
        return;
    }
    if(remote.phase==SB_NET_ABANDONED && exit_step && pairings==2) return;
    if(remote.phase==SB_NET_ABANDONED) { fprintf(stderr,"runtime peer abandoned: %s now%llu\n",remote.error,(unsigned long long)clock_ms); assert(0); }
    if(leave_requested && !leave_sent) {
        if(opponent_surrender) {
            if(sb_net_can_surrender(&remote)) leave_sent=sb_net_surrender(&remote);
        } else { assert(sb_net_leave(&remote)); leave_sent=true; }
    }
    if(!leave_requested && remote.phase==SB_NET_DEPLOY) {
        for(unsigned i=0;i<5;i++) assert(sb_net_place(&remote,i,0,i,false));
        assert(sb_net_deploy(&remote));
    }
    if(!leave_requested && leave_case!=3 && sb_net_can_act(&remote)) {
        uint8_t cells[5]; unsigned n=0;
        for(unsigned c=0;c<100 && n<remote.allowance[remote.role];c++) if(remote.knowledge[remote.role].cells[c]==SB_UNKNOWN) cells[n++]=(uint8_t)c;
        assert(sb_net_targets(&remote,cells,n)); assert(sb_net_lock(&remote));
    }
    if(leave_case!=8 && leave_case!=12 && !leave_requested && !remote.peer_left && remote.phase==SB_NET_FINISHED && !remote.rematch[remote.role] && !remote.state_dirty && !remote.link.count) { ++finished; assert(sb_net_rematch(&remote)); }
    if(old_valid && sb_net_poll(&remote_old,clock_ms,&p)) { sb_net_tx_complete(&remote_old,&p,clock_ms,true); queue(&p); }
    if(sb_net_session_poll(&remote,clock_ms,&p)) { sb_net_tx_complete(&remote.link,&p,clock_ms,true); queue(&p); }
    if(!remote.leaving && remote.rematch_ready && !remote.link.count && !remote.link.ack_due && !remote.state_dirty) {
        remote_old=remote.link; old_valid=true;
        assert(sb_net_pair_rematch(&remote_pair,(const uint8_t (*)[16])remote.rematch_nonce));
        assert(sb_net_session_init(&remote,&remote_pair,clock_ms)); assert(sb_net_confirm(&remote)); ++pairings;
    }
}
int64_t esp_timer_get_time(void) { return (int64_t)clock_ms*1000; }
void vTaskDelay(unsigned ms) { clock_ms+=ms;
    if(clock_ms>=3000000) fprintf(stderr,"Runtime timeout role=%u remote_phase=%d seq=%u remote_queue=%u local_seq=%u tactical=%d screen=%s / %s pairs=%u finished=%u\n",
        remote_role,remote.phase,remote.sequence,remote.link.count,last_view.sequence,tactical,last_menu.rows[0],last_menu.rows[1],pairings,finished);
    assert(clock_ms<3000000); pump_remote(); }
const sb_net_profile_t *sb_radio_profile(const char *region) { (void)region; return supported ? &synthetic : NULL; }
esp_err_t sb_radio_open(const char *region) { (void)region; assert(!radio_open && unmounts==1); radio_open=true; new_remote(0); return ESP_OK; }
sb_radio_stats_t sb_radio_stats(void) { return (sb_radio_stats_t){0}; }
bool sb_radio_ready(void) { return radio_open; }
esp_err_t sb_radio_status(void) { return ESP_OK; }
static sb_radio_tx_event_t completions[8]; static unsigned complete_count;
bool sb_radio_tx_complete(sb_radio_tx_event_t *e) {
    if(!complete_count) return false;
    *e=completions[0]; --complete_count; memmove(completions,completions+1,complete_count*sizeof(*completions)); return true;
}
bool sb_radio_send(const sb_net_packet_t *p) {
    assert(radio_open);
    if(p->size>=46 && p->bytes[2]==SB_NET_VERSION && p->bytes[3]==SB_N_REMATCH) local_rematch_id=sb_net_get32(p->bytes+17);
    assert(complete_count<8); completions[complete_count++]=(sb_radio_tx_event_t){.packet=*p,.at=clock_ms,.sent=true};
    if(p->size>=46 && p->bytes[3]==SB_N_LEAVE) {
        local_leave_id=sb_net_get32(p->bytes+17); ++leave_transmissions;
        if((leave_case==11 || leave_case==12) && !leave_dropped) { leave_dropped=true; return true; }
    }
    if(power_off) return true;
    if(!lost_rematch_ack && remote_secure && remote.rematch_ready && remote.link.count &&
       remote.link.queue[remote.link.head].type==SB_N_REMATCH && p->size>=46 && p->bytes[3]==SB_N_ACK &&
       sb_net_get32(p->bytes+21)==remote.link.queue[remote.link.head].command) { ++lost_rematch_ack; return true; }
    if(remote_secure && sb_net_session_receive(&remote,p,clock_ms)) remote_peer_secure=true;
    else if(old_valid && sb_net_receive(&remote_old,p,clock_ms,NULL,NULL)) {}
    else (void)sb_net_pair_receive(&remote_pair,p);
    if(leave_case==15 && remote_secure && p->size>=46 && p->bytes[3]==SB_N_LEAVE && remote.peer_left) power_off=true;
    if(leave_case==8 && local_rematch_id && !leave_requested) {
        leave_requested=true; leave_requested_at=clock_ms;
    }
    return true;
}
bool sb_radio_receive(sb_net_packet_t *p) {
    if(power_off) { in_count=0; return false; }
    if(!in_count) return false;
    *p=inbox[in_head];
    if(local_leave_id && p->size>=46 && sb_net_get32(p->bytes+21)==local_leave_id) leave_ack_seen=true;
    if(leave_case && p->size>=46 && p->bytes[3]==SB_N_LEAVE) leave_delivered=true;
    in_head=(in_head+1)%64; --in_count; return true;
}
esp_err_t sb_radio_close(void) { radio_open=false; closed=true; return ESP_OK; }
sb_sd_mount_result_t sb_platform_sd_mount(void) { ++mounts; assert(closed && !active_storage->mounted); return (sb_sd_mount_result_t){sd_missing ? ESP_FAIL : ESP_OK,false}; }
esp_err_t sb_platform_sd_shutdown(void) { ++unmounts; assert(!radio_open); return ESP_OK; }
void sb_input_set_text_mode(bool enabled) { (void)enabled; }
void sb_renderer_set_font_profile(const char *name) { (void)name; }
esp_err_t sb_platform_apply_settings(const sb_settings_t *settings) { (void)settings; return ESP_OK; }
void sb_input_reset(void) { skip_input=true; }
static sb_action_t toward(unsigned from,unsigned to) {
    if(from/10>to/10) return SB_ACTION_UP;
    if(from/10<to/10) return SB_ACTION_DOWN;
    if(from%10>to%10) return SB_ACTION_LEFT;
    if(from%10<to%10) return SB_ACTION_RIGHT;
    return SB_ACTION_NONE;
}
bool sb_input_poll(sb_input_event_t *e) {
    if(skip_input) { skip_input=false; return false; }
    sb_action_t action=SB_ACTION_NONE;
    // Let transport heartbeats continue while the deployed cursor stays still.
    // No repeated full-board draw/transfer is allowed for an unchanged view.
    if(!surrender_case && !leave_case && ui_step==18 && tactical && last_view.deployment && !idle_checked) {
        if(!idle_started) { idle_started=clock_ms; idle_frames=frames; }
        assert(frames==idle_frames);
        if(clock_ms-idle_started<1500) return false;
        idle_checked=true;
    }
    if(!surrender_case && !leave_case && ui_step==18 && tactical && !last_view.deployment &&
       !last_view.result && last_view.active && !combat_idle_checked) {
        if(!combat_idle_started) { combat_idle_started=clock_ms; combat_idle_frames=frames; }
        assert(frames==combat_idle_frames);
        if(clock_ms-combat_idle_started<1500) return false;
        combat_idle_checked=true;
    }
    if(!supported || !radio_open) action=SB_ACTION_CANCEL;
    else if(leave_requested) {
        assert(clock_ms-leave_requested_at<60000);
        if(opponent_surrender && saw_leave_win) action=SB_ACTION_CANCEL;
        // No Enter, End turn, or any other input until the win screen is rendered.
    }
    else if(ui_step<18 && ((tactical && last_view.deployment) || ui_step)) {
        static const sb_action_t actions[]={SB_ACTION_HELP,SB_ACTION_HELP,SB_ACTION_RADIO_STATUS,SB_ACTION_RADIO_STATUS,
            SB_ACTION_SURRENDER,SB_ACTION_SURRENDER,SB_ACTION_COMMIT,SB_ACTION_CANCEL,
            SB_ACTION_CANCEL,SB_ACTION_DOWN,SB_ACTION_CONFIRM,SB_ACTION_CANCEL,SB_ACTION_CANCEL,
            SB_ACTION_DEPLOY_ROTATE,SB_ACTION_DEPLOY_ROTATE,SB_ACTION_SELECT,SB_ACTION_COMMIT,SB_ACTION_HELP};
        if(ui_step==1) assert(strstr(last_menu.title,"Help"));
        if(ui_step==3) assert(strstr(last_menu.title,"Link"));
        if(ui_step>=5 && ui_step<=7) assert(!strcmp(last_menu.title,"Surrender?"));
        if(ui_step==11) assert(!strcmp(last_menu.title,"Settings"));
        if(ui_step>=13) assert(tactical && last_view.deployment);
        if(ui_step==17) { assert(last_view.ships_placed[0]); action=SB_ACTION_NONE; }
        else action=actions[ui_step];
        ++ui_step;
    }
    else if(surrender_case && ui_step==18) {
        if(tactical && !surrender_step) { assert(last_view.can_surrender); action=SB_ACTION_SURRENDER; ++surrender_step; }
        else if(!tactical && !strcmp(last_menu.title,"Surrender?")) { action=SB_ACTION_CONFIRM; ++surrender_step; }
        else if(!tactical && !strcmp(last_menu.rows[0],"You surrendered - you lose")) action=SB_ACTION_CANCEL;
    }
    else if(leave_case>=5 && leave_case<=6 && ui_step==18 && tactical && !last_view.deployment && !last_view.result)
        action=leave_case==5 ? SB_ACTION_HELP : SB_ACTION_RADIO_STATUS;
    else if(tactical && last_view.result) action=SB_ACTION_COMMIT;
    else if(tactical) {
        if(pairings>1 && last_view.deployment) {
            action=SB_ACTION_CANCEL; exit_step=1;
        } else if(last_view.deployment) {
            unsigned placed=0; for(unsigned i=0;i<5;i++) placed+=last_view.ships_placed[i];
            if(placed==5) action=SB_ACTION_CONFIRM;
            else { action=toward(last_view.cursor,last_view.ship_selected*10); if(action==SB_ACTION_NONE) action=SB_ACTION_SELECT; }
        } else if(last_view.active && remote_secure) {
            unsigned c=0; while(c<100 && (last_view.target_cells[c]!=SB_UNKNOWN || last_view.targets[c])) ++c;
            if(last_view.selected==last_view.available) action=SB_ACTION_CONFIRM;
            else if(c<100) { action=toward(last_view.cursor,c); if(action==SB_ACTION_NONE) action=SB_ACTION_SELECT; }
        }
    } else if(exit_step) {
        action=exit_step<5 ? SB_ACTION_DOWN : SB_ACTION_CONFIRM; ++exit_step;
    } else if(strstr(last_menu.rows[0],"Verification code")) action=leave_case==14 ? SB_ACTION_CANCEL : SB_ACTION_CONFIRM;
    else if((leave_case==11 || leave_case==15) && last_menu.surface==SB_MENU_SURFACE_RESULT) action=SB_ACTION_CANCEL;
    else if(leave_case==12 && last_menu.surface==SB_MENU_SURFACE_RESULT) action=local_rematch_id ? SB_ACTION_CANCEL : SB_ACTION_CONFIRM;
    else if(leave_case==8 && last_menu.surface==SB_MENU_SURFACE_RESULT) action=SB_ACTION_CONFIRM;
    else if(strstr(last_menu.footer,"Enter: Rematch") && peer_rematch_seen) action=SB_ACTION_CONFIRM;
    if(action==SB_ACTION_NONE) return false;
    *e=(sb_input_event_t){action,SB_INPUT_PRESS}; return true;
}
esp_err_t sb_renderer_message(const char *text) {
    assert(strstr(text,"disabled") || strstr(text,"Leaving"));
    if(strstr(text,"Leaving")) leaving_at=clock_ms;
    return ESP_OK;
}
esp_err_t sb_renderer_menu(const sb_menu_view_t *v) {
    if(opponent_surrender && leave_requested && !strcmp(v->rows[0],"Opponent surrendered - you win")) {
        assert(leave_sent); saw_leave_win=true;
    }
    if(!strcmp(v->title,"Opponent disconnected") || !strcmp(v->title,"Connection lost")) {
        assert(!strcmp(v->footer,"Returning to main menu..."));
        assert(!strstr(v->footer,"Rematch"));
        saw_disconnect=true; disconnect_notice_at=clock_ms;
        if(leave_case<=6) {
            assert(!strcmp(v->rows[1],"Opponent abandoned - you win")); saw_leave_win=true;
        }
        if(power_off && leave_case==9) assert(!strcmp(v->rows[0],"Link ended - no winner"));
    }
    if(!leave_requested &&
       ((leave_case>=5 && leave_case<=6 && ui_step==18 && !last_view.deployment &&
         strstr(v->title,leave_case==5 ? "Help" : "Link")) ||
        ((leave_case==7 || leave_case==10) && v->surface==SB_MENU_SURFACE_RESULT) ||
        (leave_case==13 && v->surface==SB_MENU_SURFACE_VERIFY))) {
        leave_requested=true; leave_requested_at=clock_ms;
        if(leave_case==10) power_off=true;
    }
    for(unsigned i=0;i<v->count;i++) if(strstr(v->rows[i],"Opponent requested a rematch")) {
        assert(finished==1); peer_rematch_seen=true;
    }
    last_menu=*v; tactical=false; ++frames;
    for(unsigned profile=0;profile<3;profile++) if(!sb_ui_menu(&framebuffer,profile,v)) { fprintf(stderr,"Menu layout failed: %s / %s / %s\n",v->title,v->rows[0],v->rows[1]); assert(0); }
    return ESP_OK; }
esp_err_t sb_renderer_tactical(const sb_ui_tactical_t *v) {
    assert(!leave_delivered);
    if(leave_case==9 && ui_step==18 && !leave_requested && !v->deployment && !v->result && v->active) {
        leave_requested=true; leave_requested_at=clock_ms; power_off=true;
    }
    if(ui_step==18 && !leave_requested &&
       ((leave_case==1 && v->deployment) || (leave_case==2 && !v->deployment && !v->result && v->active) ||
        (leave_case==3 && v->notice && strstr(v->notice,"Salvo locked")) || (leave_case==4 && v->result))) {
        leave_requested=true; leave_requested_at=clock_ms;
    }
    last_view=*v; tactical=true; ++frames;
    if(!v->own_board) for(unsigned i=0;i<100;i++) assert(!v->occupied[i]);
    if(!sb_ui_tactical(&framebuffer,SB_FONT_LARGE,v)) { fprintf(stderr,"Tactical layout failed: %s / %s / %s\n",v->phase_label,v->action_label,v->detail_label); assert(0); }
    return ESP_OK;
}
static void run(bool host,sb_ruleset_t rules,bool surrender,unsigned leave) {
    local_leave_id=leave_transmissions=0;
    power_off=leave_dropped=leave_ack_dropped=leave_ack_seen=saw_disconnect=false;
    disconnect_notice_at=leaving_at=0;
    leave_case=leave; leave_requested=leave_sent=leave_delivered=saw_leave_win=false;
    surrender_case=surrender; surrender_step=0;
    runtime_rules=rules; ui_step=0; old_valid=false; complete_count=0; lost_rematch_ack=0; local_rematch_id=0;
    idle_started=combat_idle_started=0; idle_frames=combat_idle_frames=0;
    idle_checked=combat_idle_checked=peer_rematch_seen=false;
    clock_ms=0; in_head=in_count=finished=pairings=frames=mounts=unmounts=exit_step=0;
    tactical=remote_secure=remote_peer_secure=closed=radio_open=false; remote_role=host ? 1 : 0;
    memset(&last_menu,0,sizeof(last_menu));
    sb_config_t config; sb_config_defaults(&config); supported=true;
    assert(sb_config_set(&config,"radio","region","EU868"));
    assert(sb_config_set(&config,"radio","manual_region","EU868"));
    sb_storage_t storage={.mounted=sd_missing, .status=SB_STORAGE_UNAVAILABLE};
    active_storage=&storage;
    assert(sb_versus_run(&config,host,rules,&storage)==ESP_OK);
    assert(storage.mounted==!sd_missing); // Successful SD remount must reach the save service.
    if(leave==15) {
        assert(remote.peer_left && !leave_ack_seen && leave_transmissions>1 &&
            clock_ms-leaving_at>=synthetic.grace_ms && clock_ms-leaving_at<synthetic.grace_ms+100);
        assert(mounts==1 && unmounts==1 && closed);
        puts("Exit notification: missing receipt stops at the bounded grace deadline");
        return;
    }
    if(leave==11 || leave==12 || leave==14) {
        assert(remote.peer_left && leave_ack_seen && mounts==1 && unmounts==1 && closed);
        if(leave!=14) assert(leave_dropped && leave_transmissions>=2 && remote.phase==SB_NET_FINISHED);
        if(leave==12) assert(leave_ack_dropped && leave_transmissions>=3);
        printf("Exit notification: role %u case %u acknowledged after %u transmissions\n",host,leave,leave_transmissions);
        return;
    }
    if(leave>=7) {
        assert(leave_requested && saw_disconnect && clock_ms-disconnect_notice_at>=3000 &&
               mounts==1 && unmounts==1 && closed);
        if(leave!=9 && leave!=10) assert(leave_delivered);
        printf("Automatic lobby return: role %u rules %u case %u, no dismissal input\n",host,rules,leave);
        return;
    }
    if(leave) {
        if(!opponent_surrender) assert(saw_disconnect && clock_ms-disconnect_notice_at>=3000);
        assert(leave_requested && leave_sent && (opponent_surrender || leave_delivered) && saw_leave_win && mounts==1 && unmounts==1 && closed);
        printf("Versus runtime %s rules%u: immediate %s win, context %u, without input\n",host ? "host" : "guest",rules,opponent_surrender ? "surrender" : "abandonment",leave);
        return;
    }
    if(surrender) {
        assert(surrender_step==2 && remote.outcome==(host ? SB_WIN_B : SB_WIN_A));
        assert(remote.end_reason==(host ? SB_END_SURRENDER_A : SB_END_SURRENDER_B));
        assert(sb_result_for(remote.outcome,remote.end_reason,remote.role)==SB_RESULT_OPPONENT_SURRENDERED);
        assert(mounts==1 && unmounts==1 && closed);
        printf("Versus runtime %s: confirmed surrender awards opponent win and restores SD\n",host ? "host" : "guest");
        return;
    }
    assert(finished==1 && pairings==2 && mounts==1 && unmounts==1 && closed && frames>100);
    assert(lost_rematch_ack==1 && ui_step==18 && idle_checked && combat_idle_checked && peer_rematch_seen);
    puts("UI: idle deployment and combat boards each avoid redraws for 1500 ms; peer rematch request appears without input");
    printf("Versus runtime %s: verified pairing, help/link/settings, surrender cancellation, hidden keys, deployment, full match, fresh rematch, private views, abandon and SD handoff passed\n",host ? "host" : "guest");
}
int main(void) {
    setvbuf(stdout,NULL,_IOLBF,0);
    for(unsigned role=0;role<2;role++) {
        sb_net_session_t s={.role=role,.phase=SB_NET_FINISHED};
        assert(strstr(rematch_status(&s),"Both players must agree"));
        s.rematch[1-role]=true; assert(strstr(rematch_status(&s),"Opponent requested"));
        s.rematch[role]=true; assert(strstr(rematch_status(&s),"Both players agreed"));
        s.rematch[1-role]=false; assert(strstr(rematch_status(&s),"waiting for opponent"));
        s.peer_left=true; assert(strstr(rematch_status(&s),"unavailable"));
        s.error="Link lost"; assert(!strcmp(rematch_status(&s),"Link lost"));
    }
    sb_config_t config; sb_config_defaults(&config); supported=false;
    assert(sb_versus_run(&config,true,SB_CLASSIC,NULL)==ESP_OK && !radio_open && !unmounts);
    supported=true; // Supported RF profile must still require explicit confirmation.
    assert(sb_versus_run(&config,true,SB_CLASSIC,NULL)==ESP_OK && !radio_open && !unmounts);
    assert(sb_config_set(&config,"radio","region","EU868"));
    assert(sb_versus_run(&config,true,SB_CLASSIC,NULL)==ESP_OK && !radio_open && !unmounts);
    assert(sb_config_set(&config,"radio","manual_region","US915"));
    assert(sb_versus_run(&config,true,SB_CLASSIC,NULL)==ESP_OK && !radio_open && !unmounts);
    assert(pax_buf_init(&framebuffer,NULL,800,480,PAX_BUF_16_565RGB));
    for(unsigned rules=SB_CLASSIC;rules<=SB_ESCALATED;rules++) for(unsigned host=0;host<2;host++) {
        run(host,rules,false,0); run(host,rules,true,0);
        for(unsigned leave=1;leave<=15;leave++) {
            if(leave==3 && rules==SB_CLASSIC) continue; // Classic has no simultaneous salvo lock.
            run(host,rules,false,leave);
        }
    }
    opponent_surrender=true;
    for(unsigned host=0;host<2;host++) {
        run(host,SB_CLASSIC,false,1); // Deployment.
        run(host,SB_ESCALATED,false,3); // Submitted salvo, waiting for opponent.
        run(host,SB_CLASSIC,false,4); // Existing shot review must be dismissed.
        run(host,SB_CLASSIC,false,5); // Help overlay.
        run(host,SB_CLASSIC,false,6); // Link overlay.
    }
    opponent_surrender=false;
    sd_missing=true; run(true,SB_CLASSIC,true,0);
    pax_buf_destroy(&framebuffer); return 0;
}
