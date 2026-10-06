// SPDX-License-Identifier: MIT
#include "render/ui.h"
#include <stdio.h>
#include <string.h>
#include "render/key_icons.h"
#define BG 0xff080e20
#define FG 0xffdce8f8
#define ACCENT 0xff65dafa
// All screens share a 24 px safe margin. Measure using the exact PAX font used
// to render; oversized content is rejected rather than silently clipped/scaled.
static bool text(pax_buf_t *buf, sb_font_profile_t p, sb_font_role_t role,
                 float x, float y, pax_col_t color, const char *label) {
    sb_font_t f = sb_font_get(p,role);
    pax_vec2f size = pax_text_size(f.font,f.size,label);
    if (x < 24 || y < 0 || x+size.x > pax_buf_get_width(buf)-24 ||
        y+size.y > pax_buf_get_height(buf)-12) return false;
    pax_draw_text(buf,color,f.font,f.size,x,y,label);
    return true;
}
bool sb_ui_keybar(pax_buf_t *buf, sb_font_profile_t p, unsigned enabled) {
    (void)p;
    static const char *labels[]={"Help","Fleet","Targets","Settings","Link","Both"};
    bool ok=true;
    for(unsigned i=0;i<6;++i) {
        float x=24+126*i;
        pax_draw_rect(buf,0xffe9f0f2,x,442,26,26);
        pax_buf_t icon;
        if(!pax_buf_init(&icon,(void *)key_pixels[i],32,32,PAX_BUF_8_PAL)) return false;
        pax_buf_set_palette_rom(&icon,key_palette[i],88);
        pax_draw_image_sized(buf,&icon,x,442,26,26);
        pax_join(); pax_buf_destroy(&icon);
        char key[4]; snprintf(key,sizeof(key),"F%u",i+1);
        pax_col_t color=enabled&(1u<<i) ? FG : 0xff718293;
        ok &= text(buf,SB_FONT_SMALL,SB_FONT_CAPTION,x+32,436,color,key);
        ok &= text(buf,SB_FONT_SMALL,SB_FONT_BODY,x+32,452,color,labels[i]);
    }
    return ok;
}
static bool footer(pax_buf_t *buf, sb_font_profile_t p) {
    return text(buf,p,SB_FONT_CAPTION,24,438,FG,"Preview | F4: Size   F6: Page   Esc: Log off");
}
bool sb_ui_hud(pax_buf_t *buf, sb_font_profile_t p, const sb_ui_hud_t *hud) {
    if (!hud || (unsigned)hud->play_type > SB_UI_SHARED_DEVICE ||
        hud->shots_selected > hud->shots_available || hud->shots_available > 25 ||
        hud->target_column < 'A' || hud->target_column > 'J' ||
        hud->target_row < 1 || hud->target_row > 10 || hud->seconds_remaining > 5999) return false;
    pax_background(buf,BG);
    const char *types[] = {"Single Player", "Versus", "Shared Device"};
    char line[96];
    snprintf(line,sizeof(line),"%s / %s",types[hud->play_type],hud->escalated ? "Escalated" : "Classic");
    bool ok = text(buf,p,SB_FONT_HEADING,24,16,ACCENT,line);
    snprintf(line,sizeof(line),"%s %02lu",hud->escalated ? "ROUND" : "TURN",(unsigned long)hud->turn_or_round);
    ok &= text(buf,p,SB_FONT_COORDINATE,24,80,FG,line);
    snprintf(line,sizeof(line),"TARGET  %c%u",hud->target_column,hud->target_row);
    ok &= text(buf,p,SB_FONT_COORDINATE,24,128,ACCENT,line);
    snprintf(line,sizeof(line),"SHOTS   %u / %u",hud->shots_selected,hud->shots_available);
    ok &= text(buf,p,SB_FONT_TELEMETRY,24,176,FG,line);
    if (!hud->escalated || hud->play_type==SB_UI_SHARED_DEVICE) snprintf(line,sizeof(line),"UNTIMED");
    else snprintf(line,sizeof(line),"TIME    %02u:%02u",hud->seconds_remaining/60,hud->seconds_remaining%60);
    ok &= text(buf,p,SB_FONT_TIMER,24,218,ACCENT,line);
    pax_draw_rect(buf,0xff23364a,24,286,pax_buf_get_width(buf)-48,58);
    ok &= text(buf,p,SB_FONT_BUTTON,40,296,FG,hud->escalated ? "Enter: Complete Turn" : "Enter: Fire");
    ok &= text(buf,p,SB_FONT_BODY,24,362,FG,hud->escalated ? "Lock selected targets for simultaneous resolution." : "One shot per turn. Hits do not grant another shot.");
    return ok;
}
bool sb_ui_handover(pax_buf_t *buf, sb_font_profile_t p,
                    bool player_b, bool deployment, bool first_press_received) {
    (void)first_press_received; // Compatibility with preview callers; no intermediate state.
    // Always clear the entire retained frame with an opaque color. No old board
    // content is blended through this cover, including outside the text area.
    pax_background(buf,BG);
    char line[96];
    snprintf(line,sizeof(line),"PLAYER %c - %s",player_b ? 'B' : 'A',deployment ? "DEPLOY YOUR FLEET" : "YOUR TURN");
    bool ok=text(buf,p,SB_FONT_HEADING,24,92,ACCENT,line);
    snprintf(line,sizeof(line),"Pass the device to Player %c.",player_b ? 'B' : 'A');
    ok &= text(buf,p,SB_FONT_BODY,24,176,FG,line);
    ok &= text(buf,p,SB_FONT_BUTTON,24,244,ACCENT,"Press Space to begin.");
    ok &= text(buf,p,SB_FONT_CAPTION,24,398,FG,"Esc: Match menu");
    ok &= sb_ui_keybar(buf,p,9);
    return ok;
}
bool sb_ui_preview(pax_buf_t *buf, sb_font_profile_t p, unsigned page) {
    page %= SB_UI_PREVIEW_PAGES;
    bool ok;
    if (page == 0) {
        pax_background(buf,BG);
        char title[80];
        snprintf(title,sizeof(title),"Typography / %s",sb_font_profile_name(p));
        ok=text(buf,p,SB_FONT_HEADING,24,16,ACCENT,title);
        ok &= text(buf,p,SB_FONT_BODY,24,88,FG,"Oxanium: menus, help and ship names");
        ok &= text(buf,p,SB_FONT_BUTTON,24,136,ACCENT,"Deploy fleet / Complete Turn");
        ok &= text(buf,p,SB_FONT_COORDINATE,24,188,FG,"TARGET J10   ROUND 99");
        ok &= text(buf,p,SB_FONT_TIMER,24,240,ACCENT,"TIME 00:23");
        ok &= text(buf,p,SB_FONT_TELEMETRY,24,294,FG,"RSSI -87 dBm   SNR +12 dB");
        ok &= text(buf,p,SB_FONT_CAPTION,24,352,FG,"M5 preview: sample data; gameplay follows later.");
    } else if (page < 4) {
        sb_ui_hud_t hud={.play_type=page==3 ? SB_UI_SHARED_DEVICE : SB_UI_SINGLE_PLAYER,
            .escalated=page==2,.turn_or_round=99,.shots_selected=page==2 ? 25 : 1,
            .shots_available=page==2 ? 25 : 1,.seconds_remaining=23,.target_column='J',.target_row=10};
        ok=sb_ui_hud(buf,p,&hud);
    } else {
        ok=sb_ui_handover(buf,p,page!=4,page==4,page==6);
    }
    return footer(buf,p) && ok;
}

static bool tactical_valid(const sb_ui_tactical_t *v) {
    if (!v || (v->ruleset != SB_CLASSIC && v->ruleset != SB_ESCALATED) ||
        (unsigned)v->play_type > SB_UI_SHARED_DEVICE ||
        (v->deployment && (!v->own_board || v->ship_selected >= SB_SHIPS)) ||
        v->cursor >= SB_CELLS || v->seconds_remaining > 5999 ||
        v->selected > v->available || v->available > (v->ruleset == SB_CLASSIC ? 1 : SB_SALVO)) return false;
    for (unsigned i=0; i<SB_CELLS; ++i)
        if ((unsigned)v->cells[i] > SB_SUNK || (!v->own_board && (v->occupied[i] || v->placement[i]))) return false;
    if (v->effects) {
        if (v->effects->count > SB_EVENTS) return false;
        for (unsigned i=0; i<v->effects->count; ++i) {
            const sb_event_t *e=&v->effects->events[i];
            if (e->attacker > 1 || e->cell >= SB_CELLS ||
                (e->type != SB_ATTACK_EVENT && e->type != SB_SUNK_EVENT)) return false;
            if (e->type == SB_SUNK_EVENT) {
                if (e->length < 2 || e->length > SB_SEGMENTS) return false;
                for (unsigned j=0; j<e->length; ++j) if (e->cells[j] >= SB_CELLS) return false;
            }
        }
    }
    return true;
}
static void outline(pax_buf_t *buf, pax_col_t color, float x, float y, float size) {
    pax_draw_rect(buf,color,x,y,size,2); pax_draw_rect(buf,color,x,y+size-2,size,2);
    pax_draw_rect(buf,color,x,y,2,size); pax_draw_rect(buf,color,x+size-2,y,2,size);
}
static void marker(pax_buf_t *buf, unsigned cell, bool hit) {
    float x=56+(cell%10)*32, y=96+(cell/10)*32;
    if (hit) {
        pax_draw_line(buf,0xffff8b70,x+9,y+9,x+23,y+23);
        pax_draw_line(buf,0xffff8b70,x+23,y+9,x+9,y+23);
    } else pax_draw_rect(buf,FG,x+14,y+14,4,4);
}
static bool combat(pax_buf_t *buf, sb_font_profile_t p, const sb_ui_tactical_t *v);
static bool board_label(pax_buf_t *,sb_font_profile_t,float,float,float,const char *,const char *);
bool sb_ui_tactical(pax_buf_t *buf, sb_font_profile_t p, const sb_ui_tactical_t *v) {
    // Fail closed: even an invalid view replaces any preceding private frame.
    pax_background(buf,BG);
    if (!tactical_valid(v) || pax_buf_get_width(buf)<800 || pax_buf_get_height(buf)<480) return false;
    if(v->dual && !v->deployment) return combat(buf,p,v);
    const char *types[]={"Single Player","Versus","Shared Device"};
    char line[96];
    snprintf(line,sizeof(line),"%s / %s",types[v->play_type],v->ruleset==SB_CLASSIC ? "Classic" : "Escalated");
    bool ok=text(buf,p,SB_FONT_HEADING,24,12,ACCENT,line);
    // Fixed cell geometry keeps the full board visible at every font profile;
    // native caption strikes fit the axes without shrinking the chosen fonts.
    for (unsigned i=0; i<10; ++i) {
        snprintf(line,sizeof(line),"%c",'A'+i);
        ok &= text(buf,p,SB_FONT_CAPTION,64+i*32,68,FG,line);
        snprintf(line,sizeof(line),"%u",i+1);
        ok &= text(buf,p,SB_FONT_CAPTION,24,102+i*32,FG,line);
    }
    for (unsigned i=0; i<SB_CELLS; ++i) {
        float x=56+(i%10)*32, y=96+(i/10)*32;
        pax_col_t color=v->occupied[i] ? 0xff397788 : 0xff152b40;
        if (v->cells[i]==SB_SUNK) color=0xff854737;
        pax_draw_rect(buf,color,x+1,y+1,30,30);
        if (v->occupied[i]) outline(buf,0xff7cc3ce,x+5,y+5,22);
        if (v->cells[i]!=SB_UNKNOWN) marker(buf,i,v->cells[i]!=SB_MISS);
        if (v->placement[i]) {
            pax_col_t ghost = v->placement_valid ? 0xff65dafa : 0xffff6b60;
            pax_draw_rect(buf,ghost,x+8,y+8,16,16);
            outline(buf,ghost,x+3,y+3,26);
        }
        if (v->targets[i]) outline(buf,0xffffd166,x+4,y+4,24);
    }
    if (v->effects) for (unsigned i=0; i<v->effects->count; ++i) {
        const sb_event_t *e=&v->effects->events[i];
        bool outgoing=e->attacker==(unsigned)v->player_b;
        if (outgoing==v->own_board) continue;
        if (e->type==SB_ATTACK_EVENT) marker(buf,e->cell,e->hit);
        else for (unsigned j=0; j<e->length; ++j)
            outline(buf,0xffff8b70,59+(e->cells[j]%10)*32,99+(e->cells[j]/10)*32,26);
    }
    outline(buf,ACCENT,56+(v->cursor%10)*32,96+(v->cursor/10)*32,32);
    if (v->deployment) {
        static const char *names[]={"Capital", "Large", "Medium 1", "Medium 2", "Small"};
        static const unsigned lengths[]={5,4,3,3,2};
        snprintf(line,sizeof(line),"%.16s",v->owner_name && *v->owner_name ? v->owner_name : v->player_b ? "Player B" : "Player A");
        ok &= board_label(buf,p,412,68,364,line,"FLEET");
        snprintf(line,sizeof(line),"ANCHOR %c%u",'A'+v->cursor%10,1+v->cursor/10);
        ok &= text(buf,p,SB_FONT_COORDINATE,412,108,FG,line);
        for (unsigned ship=0; ship<SB_SHIPS; ++ship) {
            float y=146+32*ship;
            if (ship==v->ship_selected) pax_draw_rect(buf,0xff23364a,408,y-2,368,31);
            snprintf(line,sizeof(line),"%s %u  %s",names[ship],lengths[ship],v->ships_placed[ship] ? "Placed" : "Unplaced");
            ok &= text(buf,p,SB_FONT_CAPTION,416,y,ship==v->ship_selected ? ACCENT : FG,line);
        }
        pax_draw_rect(buf,0xff23364a,412,312,364,32);
        ok &= text(buf,p,SB_FONT_BUTTON,424,312,ACCENT,"Space: Place / Move");
        ok &= text(buf,p,SB_FONT_CAPTION,412,350,FG,v->detail_label ? v->detail_label : "Place all five ships");
        ok &= text(buf,p,SB_FONT_CAPTION,412,378,FG,"R: Rotate  Tab: Ship");
        ok &= text(buf,p,SB_FONT_CAPTION,412,406,FG,"Enter: Ready  Esc: Menu");
        return sb_ui_keybar(buf,p,9) && ok;
    }
    ok &= text(buf,p,SB_FONT_BODY,412,68,ACCENT,v->own_board ? "PLAYER FLEET" : "OPPONENT TARGETS");
    snprintf(line,sizeof(line),"%s %lu",v->ruleset==SB_CLASSIC ? "TURN" : "ROUND",(unsigned long)v->sequence);
    ok &= text(buf,p,SB_FONT_TELEMETRY,412,110,FG,v->sequence_label ? v->sequence_label : line);
    snprintf(line,sizeof(line),"TARGET %c%u",'A'+v->cursor%10,1+v->cursor/10);
    ok &= text(buf,p,SB_FONT_COORDINATE,412,148,ACCENT,line);
    if (v->ruleset==SB_CLASSIC)
        snprintf(line,sizeof(line),"ACTIVE: PLAYER %c",(v->player_b != !v->active) ? 'B' : 'A');
    else snprintf(line,sizeof(line),"PLAYER %c",v->player_b ? 'B' : 'A');
    ok &= text(buf,p,SB_FONT_BODY,412,192,FG,line);
    if (v->ruleset==SB_CLASSIC) {
        ok &= text(buf,p,SB_FONT_TELEMETRY,412,232,FG,"UNTIMED");
        ok &= text(buf,p,SB_FONT_BODY,412,272,FG,v->phase_label ? v->phase_label : v->active ? "YOUR TURN" : "OPPONENT'S TURN");
    } else {
        snprintf(line,sizeof(line),"TIME %02u:%02u",v->seconds_remaining/60,v->seconds_remaining%60);
        ok &= text(buf,p,SB_FONT_TIMER,412,232,ACCENT,line);
        snprintf(line,sizeof(line),"SHOTS %u / %u",v->selected,v->available);
        ok &= text(buf,p,SB_FONT_TELEMETRY,412,272,FG,line);
    }
    pax_draw_rect(buf,0xff23364a,412,318,364,50);
    ok &= text(buf,p,SB_FONT_BUTTON,424,326,v->active ? ACCENT : FG,
        v->action_label ? v->action_label : !v->active ? "WAITING" : v->ruleset==SB_CLASSIC ? "FIRE" : "COMPLETE TURN");
    ok &= text(buf,p,SB_FONT_CAPTION,412,386,FG,v->detail_label ? v->detail_label : "Fleet: 5 / 4 / 3 / 3 / 2");
    ok &= text(buf,p,SB_FONT_CAPTION,24,438,FG,v->footer_label ? v->footer_label : "Preview | Arrows: Move  F2/F3: View  F4: Size  F6: Page");
    return ok;
}
bool sb_ui_tactical_preview(pax_buf_t *buf, sb_font_profile_t p,
                            unsigned page, unsigned cursor, bool enemy) {
    if (page>=SB_TACTICAL_PREVIEW_PAGES) { pax_background(buf,BG); return false; }
    // Cover pages accept no board model; shortcuts cannot uncover a fleet.
    if (page>=4) return sb_ui_handover(buf,p,page!=4,page==4,page==6) && footer(buf,p);
    sb_ui_tactical_t v={.ruleset=page==2 ? SB_ESCALATED : SB_CLASSIC,
        .play_type=page==3 ? SB_UI_SHARED_DEVICE : SB_UI_SINGLE_PLAYER,
        .own_board=!enemy,.active=page!=1,.player_b=page==3,
        .sequence=6,.seconds_remaining=23,.selected=page==2 ? 3 : 1,
        .available=page==2 ? 5 : 1,.cursor=cursor};
    static const unsigned starts[]={10,27,40,65,88}, lengths[]={5,4,3,3,2};
    if (!enemy) for (unsigned s=0; s<SB_SHIPS; ++s) for (unsigned j=0; j<lengths[s]; ++j)
        v.occupied[starts[s]+j*(s==1 ? 10 : 1)]=true;
    v.cells[10]=SB_HIT; v.cells[22]=SB_MISS;
    v.cells[88]=v.cells[89]=SB_SUNK;
    if (page==2 && enemy) v.targets[6]=v.targets[49]=v.targets[73]=true;
    return sb_ui_tactical(buf,p,&v);
}

bool sb_ui_menu(pax_buf_t *buf, sb_font_profile_t p, const sb_menu_view_t *v) {
    // Opaque on every draw, including dialogs above private gameplay.
    pax_background(buf,BG);
    if (!v || v->count>SB_MENU_ROWS || (v->selectable && v->selected>=v->count)) return false;
    bool ok=text(buf,p,SB_FONT_HEADING,24,16,ACCENT,v->title);
    ok &= text(buf,p,SB_FONT_CAPTION,24,66,FG,v->subtitle);
    if(v->edit_field) {
        pax_draw_rect(buf,0xff23364a,24,150,752,66);
        sb_font_t f=sb_font_get(p,SB_FONT_BODY);
        char prefix[17]; unsigned caret=v->caret>strlen(v->edit_text) ? strlen(v->edit_text) : v->caret;
        memcpy(prefix,v->edit_text,caret); prefix[caret]=0;
        float width=pax_text_size(f.font,f.size,v->edit_text).x;
        if(v->select_all) pax_draw_rect(buf,0xff397788,36,162,width+8,38);
        ok &= text(buf,p,SB_FONT_BODY,40,166,FG,v->edit_text);
        if(!v->select_all) pax_draw_rect(buf,ACCENT,40+pax_text_size(f.font,f.size,prefix).x,162,2,38);
    }
    unsigned first=v->selectable && v->selected>=7 ? v->selected-6 : 0;
    for(unsigned i=first;i<v->count && i<first+7;++i) {
        float y=(v->edit_field ? 240 : 96)+(i-first)*36;
        bool selected=v->selectable && v->selected==i;
        if(selected) pax_draw_rect(buf,0xff23364a,24,y-3,752,37);
        ok &= text(buf,p,SB_FONT_BODY,32,y,selected ? ACCENT : FG,v->rows[i]);
    }
    if(v->count>7 && !v->status[0]) {
        char range[48]; snprintf(range,sizeof(range),"%u-%u of %u | Up/Down: Scroll",first+1,first+7,v->count);
        ok &= text(buf,p,SB_FONT_CAPTION,24,354,FG,range);
    } else ok &= text(buf,p,SB_FONT_CAPTION,24,354,FG,v->status);
    ok &= text(buf,p,SB_FONT_CAPTION,24,398,ACCENT,v->footer);
    return sb_ui_keybar(buf,p,v->key_mask) && ok;
}

static bool board_label(pax_buf_t *buf,sb_font_profile_t p,float x,float y,float max,
                        const char *name,const char *role) {
    char label[64],short_name[20];
    snprintf(short_name,sizeof(short_name),"%.16s",name);
    sb_font_t f=sb_font_get(p,SB_FONT_CAPTION);
    snprintf(label,sizeof(label),"%s - %s",short_name,role);
    size_t n=strlen(short_name);
    while(n && pax_text_size(f.font,f.size,label).x>max) {
        short_name[--n]=0; snprintf(label,sizeof(label),"%s... - %s",short_name,role);
    }
    return text(buf,p,SB_FONT_CAPTION,x,y,ACCENT,label);
}
static bool combat(pax_buf_t *buf,sb_font_profile_t p,const sb_ui_tactical_t *v) {
    if(v->view_mode>2) return false;
    for(unsigned i=0;i<100;++i)
        if(v->fleet_cells[i]>SB_SUNK || v->target_cells[i]>SB_SUNK) return false;
    const char *owner=v->owner_name && *v->owner_name ? v->owner_name : v->player_b ? "Player B" : "Player A";
    const char *target=v->target_name && *v->target_name ? v->target_name : v->player_b ? "Player A" : "Player B";
    char line[96];
    snprintf(line,sizeof(line),"%s / %s",v->play_type==SB_UI_SINGLE_PLAYER ? "Single Player" : v->play_type==SB_UI_VERSUS ? "Versus" : "Shared Device",v->ruleset==SB_CLASSIC ? "Classic" : "Escalated");
    bool ok=text(buf,p,SB_FONT_HEADING,24,8,ACCENT,line);
    if(v->result) snprintf(line,sizeof(line),"%s | %s",v->phase_label ? v->phase_label : "Resolved",v->action_label ? v->action_label : "");
    else if(v->ruleset==SB_CLASSIC) snprintf(line,sizeof(line),"%.16s's turn | Turn %lu | Untimed",v->active ? owner : target,(unsigned long)v->sequence);
    else if(v->play_type==SB_UI_SHARED_DEVICE) snprintf(line,sizeof(line),"%.16s | R%lu | Untimed | %u/%u shots",owner,(unsigned long)v->sequence,v->selected,v->available);
    else snprintf(line,sizeof(line),"%.16s | R%lu | %02u:%02u | %u/%u shots",owner,(unsigned long)v->sequence,v->seconds_remaining/60,v->seconds_remaining%60,v->selected,v->available);
    sb_font_t status_font=sb_font_get(p,SB_FONT_CAPTION);
    sb_font_profile_t status_profile=pax_text_size(status_font.font,status_font.size,line).x>752 ? SB_FONT_SMALL : p;
    ok &= text(buf,status_profile,SB_FONT_CAPTION,24,52,FG,line);
    for(unsigned side=0;side<2;++side) {
        bool own=side==0;
        if((v->view_mode==1 && !own) || (v->view_mode==2 && own)) continue;
        float x=v->view_mode ? 56 : own ? 56 : 456;
        float y=v->view_mode ? 100 : 126;
        float cell=v->view_mode ? 30 : 27;
        ok &= board_label(buf,p,v->view_mode ? 412 : own ? 24 : 424,82,v->view_mode ? 364 : 352,own ? owner : target,own ? "FLEET" : "TARGETS");
        for(unsigned i=0;i<10;++i) {
            snprintf(line,sizeof(line),"%c",'A'+i);
            // Coordinates use native small caption strikes at fixed cell geometry.
            ok &= text(buf,SB_FONT_SMALL,SB_FONT_CAPTION,x+i*cell+8,y-18,FG,line);
            snprintf(line,sizeof(line),"%u",i+1);
            ok &= text(buf,SB_FONT_SMALL,SB_FONT_CAPTION,x-28,y+i*cell+5,FG,line);
        }
        for(unsigned i=0;i<100;++i) {
            float bx=x+(i%10)*cell,by=y+(i/10)*cell;
            sb_cell_state_t state=own ? v->fleet_cells[i] : v->target_cells[i];
            pax_col_t color=own && v->fleet_occupied[i] ? 0xff397788 : 0xff152b40;
            if(state==SB_SUNK) color=0xff854737;
            pax_draw_rect(buf,color,bx+1,by+1,cell-2,cell-2);
            if(state==SB_MISS) pax_draw_circle(buf,FG,bx+cell/2,by+cell/2,2);
            if(state==SB_HIT || state==SB_SUNK) {
                pax_draw_line(buf,0xffff8b70,bx+7,by+7,bx+cell-7,by+cell-7);
                pax_draw_line(buf,0xffff8b70,bx+cell-7,by+7,bx+7,by+cell-7);
            }
            if(!own && v->targets[i]) outline(buf,0xffffd166,bx+3,by+3,cell-6);
        }
        if(v->effects) for(unsigned i=0;i<v->effects->count;++i) {
            const sb_event_t *e=&v->effects->events[i];
            bool incoming=e->attacker!=(unsigned)v->player_b;
            if(incoming!=own) continue;
            if(e->type==SB_ATTACK_EVENT) outline(buf,0xff65dafa,x+e->cell%10*cell+2,y+e->cell/10*cell+2,cell-4);
            else for(unsigned j=0;j<e->length;++j) outline(buf,0xffff8b70,x+e->cells[j]%10*cell+2,y+e->cells[j]/10*cell+2,cell-4);
        }
        if(!own && !v->result) outline(buf,ACCENT,x+v->cursor%10*cell,y+v->cursor/10*cell,cell);
    }
    snprintf(line,sizeof(line),"%s",v->result ? "Results - controls locked" : v->notice && *v->notice ? v->notice : v->view_mode==1 ? "Fleet view | F3: Targets  F6: Both" : v->ruleset==SB_ESCALATED ? "Arrows: Aim  Space: Select  Enter: Complete turn" : "Arrows: Aim  Enter: Fire  Esc: Match menu");
    ok &= text(buf,p,SB_FONT_CAPTION,24,404,FG,line);
    return sb_ui_keybar(buf,p,v->result ? 0 : v->play_type==SB_UI_VERSUS ? 63 : 47) && ok;
}
