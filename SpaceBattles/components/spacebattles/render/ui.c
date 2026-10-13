// SPDX-License-Identifier: MIT
#include "render/ui.h"
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "render/theme.h"
#include "render/title.h"
#define BG SB_BG
#define FG SB_FG
#define ACCENT SB_CYAN
#define INCOMING SB_INCOMING
#define OUTGOING SB_OUTGOING
static void bevel(pax_buf_t *b,float x,float y,float w,float h,pax_col_t fill,pax_col_t edge) {
    const float c=7;
    pax_draw_rect(b,fill,x+c,y,w-2*c,h);
    pax_draw_rect(b,fill,x,y+c,w,h-2*c);
    pax_draw_tri(b,fill,x,y+c,x+c,y,x+c,y+c);
    pax_draw_tri(b,fill,x+w-c,y,x+w,y+c,x+w-c,y+c);
    pax_draw_tri(b,fill,x,y+h-c,x+c,y+h-c,x+c,y+h);
    pax_draw_tri(b,fill,x+w-c,y+h-c,x+w,y+h-c,x+w-c,y+h);
    pax_draw_line(b,edge,x+c,y,x+w-c-1,y);
    pax_draw_line(b,edge,x+c,y+h-1,x+w-c-1,y+h-1);
    pax_draw_line(b,edge,x,y+c,x,y+h-c-1);
    pax_draw_line(b,edge,x+w-1,y+c,x+w-1,y+h-c-1);
    pax_draw_line(b,edge,x,y+c,x+c,y);
    pax_draw_line(b,edge,x+w-c-1,y,x+w-1,y+c);
    pax_draw_line(b,edge,x,y+h-c-1,x+c,y+h-1);
    pax_draw_line(b,edge,x+w-c-1,y+h-1,x+w-1,y+h-c-1);
}
static void frame(pax_buf_t *b,float x,float y,float w,float h,pax_col_t edge) {
    bevel(b,x,y,w,h,SB_PANEL,edge);
}
static void menu_button(pax_buf_t *b,float x,float y,float w,bool selected,bool disabled) {
    pax_col_t edge=disabled ? SB_MUTED : selected ? ACCENT : SB_CONTROL_EDGE;
    bevel(b,x,y,w,34,selected && !disabled ? ACCENT : SB_CONTROL,edge);
    // The arrow keeps keyboard focus visible without color alone.
    if(selected) {
        pax_col_t arrow=disabled ? SB_MUTED : BG;
        pax_draw_line(b,arrow,x+w-22,y+10,x+w-16,y+16);
        pax_draw_line(b,arrow,x+w-16,y+16,x+w-22,y+22);
    }
}
static pax_col_t tone_color(sb_ui_tone_t tone) {
    return tone==SB_TONE_ERROR ? SB_ERROR : tone==SB_TONE_PENDING ? SB_AMBER :
        tone==SB_TONE_SUCCESS ? SB_OUTGOING : SB_CYAN;
}
static void header(pax_buf_t *b) {
    pax_draw_rect(b,SB_FRAME,24,57,752,1);
}
static void control_rail(pax_buf_t *b) {
    pax_draw_rect(b,SB_STRIP,0,432,800,48);
    pax_draw_rect(b,SB_FRAME,24,432,752,1);
}
static void state_mark(pax_buf_t *b,float x,float y,sb_ui_tone_t tone) {
    pax_col_t c=tone_color(tone);
    if(tone==SB_TONE_PENDING) {
        pax_draw_tri(b,c,x,y+12,x+12,y+12,x+6,y);
        pax_draw_rect(b,BG,x+5,y+4,2,4);
        pax_draw_rect(b,BG,x+5,y+10,2,1);
    } else if(tone==SB_TONE_SUCCESS) {
        pax_draw_line(b,c,x,y+7,x+4,y+11);
        pax_draw_line(b,c,x+4,y+11,x+12,y+1);
    } else if(tone==SB_TONE_ERROR) {
        pax_draw_line(b,c,x,y,x+12,y+12);
        pax_draw_line(b,c,x+12,y,x,y+12);
    } else {
        pax_draw_rect(b,c,x,y,3,12);
        pax_draw_rect(b,c,x+5,y+4,3,8);
        pax_draw_rect(b,c,x+10,y+8,3,4);
    }
}
static const pax_buf_t *menu_art, *menu_cache;
void sb_ui_set_menu_art(const pax_buf_t *art) { menu_art=art; menu_cache=NULL; }
void sb_ui_set_menu_cache(const pax_buf_t *cache) { menu_cache=cache; }
void sb_ui_prepare_menu_cache(pax_buf_t *buf) {
    // Start opaque so private boards can never leak through a dialog.
    pax_background(buf,BG);
    if (!menu_art) return;
    float w=pax_buf_get_width(buf), h=pax_buf_get_height(buf);
    // Text-free native artwork; compose only once, never on each keypress.
    pax_draw_image_sized(buf,menu_art,0,0,w,h);

}
static bool brand(pax_buf_t *buf,float x,float y,float w,float h) {
    pax_buf_t title;
    if(!pax_buf_init(&title,(void *)sb_title_pixels,600,60,PAX_BUF_4_PAL)) return false;
    pax_buf_set_palette_rom(&title,sb_title_palette,16);
    pax_draw_image_sized(buf,&title,x,y,w,h);
    pax_join(); pax_buf_destroy(&title);
    return true;
}
static void menu_background(pax_buf_t *buf) {
    // Cache has the exact native storage layout, including display rotation and
    // endianness. Join before direct writes so queued PAX draws cannot race it.
    if(menu_cache && menu_cache!=buf && menu_cache->buf!=buf->buf &&
       menu_cache->type==buf->type && menu_cache->width==buf->width &&
       menu_cache->height==buf->height && menu_cache->orientation==buf->orientation &&
       menu_cache->reverse_endianness==buf->reverse_endianness) {
        pax_join();
        memcpy(pax_buf_get_pixels_rw(buf),pax_buf_get_pixels(menu_cache),pax_buf_get_size(buf));
        pax_mark_dirty0(buf);
    } else sb_ui_prepare_menu_cache(buf);
}
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
static bool keybar(pax_buf_t *buf, sb_font_profile_t p, unsigned enabled, const char *confirm, const char *select, unsigned returning) {
    (void)p;
    if(!enabled) return true;
    control_rail(buf);
    static const char *labels[]={"Help","Rotate","Select","Confirm","Link","Surrender"};
    bool ok=true;
    for(unsigned i=0;i<6;++i) {
        if(!(enabled&(1u<<i))) continue;
        float x=24+126*i;
        bevel(buf,x,436,32,32,SB_CONTROL,ACCENT);
        char key[4]; snprintf(key,sizeof(key),"F%u",i+1);
        ok &= text(buf,SB_FONT_SMALL,SB_FONT_BUTTON,x+6,443,ACCENT,key);
        ok &= text(buf,SB_FONT_SMALL,SB_FONT_BODY,x+38,443,FG,
                   returning&(1u<<i) ? "Return" : i==3 ? confirm : i==2 ? select : labels[i]);
    }
    return ok;
}
bool sb_ui_keybar(pax_buf_t *buf, sb_font_profile_t p, unsigned enabled) {
    return keybar(buf,p,enabled,"Confirm","Select",0);
}
static bool tactical_keybar(pax_buf_t *buf,sb_font_profile_t p,const sb_ui_tactical_t *v) {
    return keybar(buf,p,sb_ui_tactical_keys(v),v->result ? "Continue" : v->deployment ? "Ready" :
        v->ruleset==SB_CLASSIC ? "Fire" : "Submit", v->deployment ? "Place" : v->targets[v->cursor] ? "Deselect" : "Select",0);
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
    menu_background(buf);
    pax_draw_rect(buf,SB_STRIP,24,8,752,52);
    header(buf);
    frame(buf,24,88,752,288,SB_FRAME);
    bool ok=text(buf,p,SB_FONT_HEADING,24,12,FG,"Private handover");
    char line[96];
    snprintf(line,sizeof(line),"%s",deployment ? "DEPLOY YOUR FLEET" : "YOUR TURN");
    ok &= text(buf,p,SB_FONT_HEADING,40,108,ACCENT,line);
    snprintf(line,sizeof(line),"Pass the device to Player %c.",player_b ? 'B' : 'A');
    ok &= text(buf,p,SB_FONT_BODY,40,184,FG,line);
    ok &= text(buf,p,SB_FONT_BUTTON,40,252,ACCENT,"Press Space to begin.");
    ok &= text(buf,p,SB_FONT_CAPTION,24,398,FG,"Esc: Match menu");
    ok &= sb_ui_keybar(buf,p,1);
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
        ok &= text(buf,p,SB_FONT_TIMER,24,240,ACCENT,"SHOTS 3 / 5");
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
    for(unsigned i=0;i<SB_SHIPS;i++) {
        const sb_ui_ship_t *ship=&v->ships[i];
        if(ship->length && (ship->length<2 || ship->length>SB_SEGMENTS)) return false;
        unsigned step=ship->length>1 && ship->cells[1]==ship->cells[0]+10 ? 10 : 1;
        for(unsigned j=0;j<ship->length;j++) {
            if(ship->cells[j]>=SB_CELLS) return false;
            if(ship->cells[j]!=ship->cells[0]+step*j) return false;
            if(step==1 && ship->cells[j]/10!=ship->cells[0]/10) return false;
        }
    }
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
// Validation/legacy projections without descriptors retain an occupied tile.
static void hull_cell(pax_buf_t *b,float x,float y,float size) {
    pax_draw_rect(b,SB_HULL,x+4,y+4,size-8,size-8);
}
// Wireframe hull shared by board and roster. Geometry is owner-only; no enemy
// descriptors enter this renderer. Local coordinates make both orientations exact.
static void ship_line(pax_buf_t *b,pax_col_t c,float x,float y,float l,float h,bool vertical,
                      float ax,float ay,float bx,float by) {
    if(vertical) pax_draw_line(b,c,x+ay*h,y+ax*l,x+by*h,y+bx*l);
    else pax_draw_line(b,c,x+ax*l,y+ay*h,x+bx*l,y+by*h);
}
static void warship(pax_buf_t *b,float x,float y,float l,float h,bool vertical,pax_col_t ink) {
    static const float hull[][2]={{0,.38f},{.08f,.38f},{.12f,.18f},{.24f,.18f},
        {.27f,0},{.42f,0},{.46f,.18f},{.66f,.18f},{.70f,.28f},{.88f,.32f},
        {1,.50f},{.88f,.68f},{.70f,.72f},{.66f,.82f},{.46f,.82f},{.42f,1},
        {.27f,1},{.24f,.82f},{.12f,.82f},{.08f,.62f},{0,.62f}};
    for(unsigned i=0;i<sizeof(hull)/sizeof(hull[0]);i++) {
        unsigned j=(i+1)%(sizeof(hull)/sizeof(hull[0]));
        ship_line(b,ink,x,y,l,h,vertical,hull[i][0],hull[i][1],hull[j][0],hull[j][1]);
    }
    ship_line(b,ink,x,y,l,h,vertical,.05f,.5f,.94f,.5f);
    ship_line(b,ink,x,y,l,h,vertical,.15f,.30f,.80f,.38f);
    ship_line(b,ink,x,y,l,h,vertical,.15f,.70f,.80f,.62f);
    for(unsigned i=0;i<6;i++) {
        float a=.12f+i*.11f;
        ship_line(b,ink,x,y,l,h,vertical,a,.25f,a,.75f);
        ship_line(b,ink,x,y,l,h,vertical,a,.35f,a+.045f,.35f);
        ship_line(b,ink,x,y,l,h,vertical,a+.045f,.35f,a+.045f,.65f);
        ship_line(b,ink,x,y,l,h,vertical,a+.045f,.65f,a,.65f);
    }
}
// Both board sizes use the deployment silhouette; opponent geometry is never drawn.
static void fleet_hulls(pax_buf_t *buf,const sb_ui_tactical_t *v,float x,float y,float size) {
    for(unsigned i=0;i<SB_SHIPS;i++) {
        const sb_ui_ship_t *ship=&v->ships[i];
        if(!ship->length) continue;
        unsigned cell=ship->cells[0];
        bool vertical=ship->cells[1]==cell+10;
        bool sunk=!v->deployment;
        for(unsigned j=0;j<ship->length;j++) sunk &= v->fleet_cells[ship->cells[j]]==SB_SUNK;
        pax_col_t ink=sunk ? SB_MUTED : v->deployment && i==v->ship_selected ? ACCENT : SB_HULL_EDGE;
        warship(buf,x+cell%10*size+(vertical ? 5 : 3),
                y+cell/10*size+(vertical ? 3 : 5),ship->length*size-6,size-10,vertical,ink);
    }
}
static void cursor_brackets(pax_buf_t *b,float x,float y,float s) {
    for(unsigned i=0;i<2;i++) for(unsigned j=0;j<2;j++) {
        float bx=x+i*(s-8),by=y+j*(s-2);
        pax_draw_rect(b,ACCENT,bx,by,8,2);
        pax_draw_rect(b,ACCENT,x+i*(s-2),y+j*(s-8),2,8);
    }
}
static void cross(pax_buf_t *b,pax_col_t color,float x,float y,float size) {
    for(int d=-1;d<=1;d++) {
        pax_draw_line(b,color,x+7,y+7+d,x+size-7,y+size-7+d);
        pax_draw_line(b,color,x+size-7,y+7+d,x+7,y+size-7+d);
    }
}
static void marker(pax_buf_t *buf, unsigned cell, bool hit) {
    float x=56+(cell%10)*32, y=96+(cell/10)*32;
    if (hit) {
        pax_draw_line(buf,0xffff8b70,x+9,y+9,x+23,y+23);
        pax_draw_line(buf,0xffff8b70,x+23,y+9,x+9,y+23);
    } else pax_draw_rect(buf,FG,x+14,y+14,4,4);
}
static bool paragraph(pax_buf_t *,sb_font_profile_t,sb_font_role_t,float,float,float,float,pax_col_t,const char *);
static bool combat(pax_buf_t *buf, sb_font_profile_t p, const sb_ui_tactical_t *v);
static bool board_label(pax_buf_t *,sb_font_profile_t,float,float,float,const char *,const char *);
bool sb_ui_tactical(pax_buf_t *buf, sb_font_profile_t p, const sb_ui_tactical_t *v) {
    // Fail closed: even an invalid view replaces any preceding private frame.
    if (!tactical_valid(v) || pax_buf_get_width(buf)<800 || pax_buf_get_height(buf)<480) {
        pax_background(buf,BG);
        return false;
    }
    // Valid views replace the complete frame through menu_background(). Avoid
    // clearing all 384,000 pixels once more before copying that opaque backdrop.
    if(!v->deployment) return combat(buf,p,v);
    menu_background(buf);
    pax_draw_rect(buf,SB_STRIP,24,8,752,52);
    header(buf);
    frame(buf,400,76,376,276,ACCENT);
    frame(buf,400,360,376,48,ACCENT);
    frame(buf,54,94,324,324,ACCENT);
    pax_draw_rect(buf,SB_STRIP,24,64,354,30);
    pax_draw_rect(buf,SB_STRIP,24,94,30,324);
    const char *types[]={"Single Player","Versus","Shared Device"};
    char line[96];
    snprintf(line,sizeof(line),"%s / %s",types[v->play_type],v->ruleset==SB_CLASSIC ? "Classic" : "Escalated");
    bool ok=text(buf,p,SB_FONT_HEADING,24,12,FG,line);
    // Fixed cell geometry keeps the full board visible at every font profile;
    // native caption strikes fit the axes without shrinking the chosen fonts.
    for (unsigned i=0; i<10; ++i) {
        snprintf(line,sizeof(line),"%c",'A'+i);
        ok &= text(buf,p,SB_FONT_CAPTION,64+i*32,68,FG,line);
        snprintf(line,sizeof(line),"%u",i+1);
        ok &= text(buf,p,SB_FONT_CAPTION,24,102+i*32,FG,line);
    }
    bool geometry=false;
    for(unsigned i=0;i<SB_SHIPS;i++) geometry |= v->ships[i].length!=0;
    for (unsigned i=0; i<SB_CELLS; ++i) {
        float x=56+(i%10)*32, y=96+(i/10)*32;
        pax_col_t color=SB_GRID;
        if (v->cells[i]==SB_SUNK) color=0xff854737;
        pax_draw_rect(buf,color,x+1,y+1,30,30);
        if (v->occupied[i] && !geometry) hull_cell(buf,x,y,32);
        if (v->cells[i]!=SB_UNKNOWN) marker(buf,i,v->cells[i]!=SB_MISS);
    }
    fleet_hulls(buf,v,56,96,32);
    for(unsigned i=0;i<SB_CELLS;i++) {
        float x=56+(i%10)*32, y=96+(i/10)*32;
        if (v->placement[i]) {
            pax_col_t ghost = v->placement_valid ? 0xff65dafa : 0xffff6b60;
            outline(buf,ghost,x+3,y+3,26);
            if(!v->placement_valid) cross(buf,ghost,x,y,32);
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
    cursor_brackets(buf,56+(v->cursor%10)*32,96+(v->cursor/10)*32,32);
    if (v->deployment) {
        static const char *names[]={"Capital", "Large", "Medium 1", "Medium 2", "Small"};
        static const unsigned lengths[]={5,4,3,3,2};
        snprintf(line,sizeof(line),"%.16s",v->owner_name && *v->owner_name ? v->owner_name : v->player_b ? "Player B" : "Player A");
        ok &= board_label(buf,p,412,80,352,line,"FLEET");
        for (unsigned ship=0; ship<SB_SHIPS; ++ship) {
            float y=112+46*ship;
            bool selected=ship==v->ship_selected;
            bevel(buf,408,y,360,42,SB_CONTROL,selected ? ACCENT : SB_CONTROL_EDGE);
            if(selected) {
                pax_draw_rect(buf,ACCENT,406,y+7,2,28);
                pax_draw_rect(buf,ACCENT,410,y,352,2);
            }
            warship(buf,418,y+10,44+lengths[ship]*8,22,false,
                    selected ? ACCENT : SB_HULL_EDGE);
            pax_draw_rect(buf,SB_CONTROL_EDGE,510,y+6,1,30);
            snprintf(line,sizeof(line),"%s %u",names[ship],lengths[ship]);
            // Native caption strike leaves room for an explicit placement label.
            ok &= text(buf,p,SB_FONT_CAPTION,520,y+1,FG,line);
            ok &= text(buf,SB_FONT_SMALL,SB_FONT_CAPTION,520,y+26,
                       v->ships_placed[ship] ? ACCENT : SB_MUTED,
                       v->ships_placed[ship] ? "Placed" : "Not placed");
        }
        unsigned placed=0;
        for(unsigned ship=0;ship<SB_SHIPS;ship++) placed+=v->ships_placed[ship];
        snprintf(line,sizeof(line),"Placed %u/5",placed);
        ok &= paragraph(buf,p,SB_FONT_CAPTION,412,364,352,408,
                        v->deployment_locked ? SB_AMBER : !v->placement_valid ? SB_ERROR : ACCENT,
                        v->deployment_locked ? "Deployment locked" :
                        !v->placement_valid ? "Invalid position" : v->detail_label ? v->detail_label : line);
        ok &= text(buf,SB_FONT_SMALL,SB_FONT_CAPTION,24,418,FG,v->deployment_locked ?
            "Esc: Match menu" : "Arrows: Move   Tab: Next ship   Esc: Match menu");
        return tactical_keybar(buf,p,v) && ok;
    }
    return false;
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
    v.dual=true;
    memcpy(v.fleet_occupied,v.occupied,sizeof(v.fleet_occupied));
    memcpy(v.fleet_cells,v.cells,sizeof(v.fleet_cells));
    memcpy(v.target_cells,v.cells,sizeof(v.target_cells));
    return sb_ui_tactical(buf,p,&v);
}

// Wrap within the caller's region at the chosen font size. Never shrink text.
static bool paragraph(pax_buf_t *b,sb_font_profile_t p,sb_font_role_t role,
                      float x,float y,float width,float bottom,pax_col_t color,const char *label) {
    sb_font_t f=sb_font_get(p,role);
    char remaining[192]; snprintf(remaining,sizeof(remaining),"%s",label);
    char *at=remaining;
    while(*at) {
        size_t n=strlen(at), cut=n;
        while(cut && pax_text_size(f.font,f.size,at).x>width) {
            // Find a word boundary, preserving the remainder before measuring.
            cut--;
            while(cut && at[cut]!=' ') --cut;
            if(!cut) return false;
            char saved=at[cut]; at[cut]=0;
            bool fits=pax_text_size(f.font,f.size,at).x<=width;
            at[cut]=saved;
            if(fits) break;
        }
        char saved=at[cut]; at[cut]=0;
        if(y+f.size+2>bottom || !text(b,p,role,x,y,color,at)) return false;
        at[cut]=saved;
        if(cut==n) break;
        at+=cut+1; y+=f.size+6;
    }
    return true;
}

static bool menu_draw(pax_buf_t *buf, sb_font_profile_t p, const sb_menu_view_t *v, int previous) {
    // Opaque on every draw, including dialogs above private gameplay.
    if(previous<0) { if(v) menu_background(buf); else pax_background(buf,BG); }
    if (!v || v->count>SB_MENU_ROWS || (v->selectable && v->selected>=v->count)) return false;
    bool command=v->surface==SB_MENU_SURFACE_COMMAND;
    bool match=v->surface==SB_MENU_SURFACE_MATCH;
    bool setup=v->surface==SB_MENU_SURFACE_SETUP;
    bool rules=v->surface==SB_MENU_SURFACE_RULES;
    bool entry=command || setup || rules;
    bool ok=true;
    if(previous<0 && entry) {
        ok &= brand(buf,100,32,600,60);
        if(command) {
            sb_font_t f=sb_font_get(p,SB_FONT_CAPTION);
            float width=pax_text_size(f.font,f.size,v->subtitle).x;
            ok &= text(buf,p,SB_FONT_CAPTION,(800-width)/2,96,FG,v->subtitle);
        }
    } else if(previous<0 && !match) {
        // Dark translucent backing preserves contrast over the public artwork.
        pax_draw_rect(buf,SB_STRIP,24,8,752,80);
        header(buf);
        ok &= text(buf,p,SB_FONT_HEADING,24,12,FG,v->title);
        ok &= text(buf,p,SB_FONT_CAPTION,24,64,SB_MUTED,v->subtitle);
    } else if(previous<0) {
        ok &= brand(buf,32,28,240,24);
    }
    bool dialog=v->surface==SB_MENU_SURFACE_DIALOG || v->surface==SB_MENU_SURFACE_STATUS ||
        v->surface==SB_MENU_SURFACE_VERIFY || v->surface==SB_MENU_SURFACE_RESULT || match;
    float x=24,w=752,top=88;
    unsigned first=v->selectable && v->selected>=7 ? v->selected-6 : 0;
    unsigned visible=v->count-first; if(visible>7) visible=7;
    if(command) w=280;
    else if(setup || rules) {
        w=340;
        sb_font_t f=sb_font_get(p,SB_FONT_BODY),h=sb_font_get(p,SB_FONT_HEADING);
        float title_width=pax_text_size(h.font,h.size,v->title).x+32;
        if(title_width>w) w=title_width;
        for(unsigned i=0;i<v->count;i++) {
            float needed=pax_text_size(f.font,f.size,v->rows[i]).x+64;
            if(needed>w) w=needed;
        }
        if(w>752) w=752;
    }
    else if(dialog) {
        w=640;
        sb_font_t f=sb_font_get(p,SB_FONT_BODY);
        for(unsigned i=0;i<v->count;i++) {
            float needed=pax_text_size(f.font,f.size,v->rows[i]).x+112;
            if(needed>w) w=needed;
        }
        if(w>752) w=752;
        x=(800-w)/2;
        top=110;
    }
    float height=dialog ? 232 : setup || rules ? 270 : 258;
    if(setup || rules) top=122;
    if(match) { top=76; height=316; }
    if(v->edit_field) { x=24; w=752; top=120; height=210; }
    if(previous<0 && !command) frame(buf,x,top,w,height,entry || match ? ACCENT : dialog ? tone_color(v->tone) : SB_FRAME);
    if(previous<0 && dialog && !v->selectable) state_mark(buf,x+w-24,top+6,v->tone);
    float row_y=dialog ? top+22 : 96;
    if(command) row_y=130;
    if(setup || rules) {
        if(previous<0) {
            ok &= text(buf,p,SB_FONT_HEADING,x+16,132,FG,v->title);
            ok &= paragraph(buf,p,SB_FONT_CAPTION,x+16,176,w-32,232,SB_MUTED,v->subtitle);
        }
        row_y=rules ? 246 : 234;
        if(!rules && strlen(v->subtitle)<30) row_y=216;
    }
    if(match) {
        if(previous<0) {
        sb_font_t h=sb_font_get(p,SB_FONT_HEADING),c=sb_font_get(p,SB_FONT_CAPTION);
        ok &= text(buf,p,SB_FONT_HEADING,(800-pax_text_size(h.font,h.size,"MATCH MENU").x)/2,92,FG,"MATCH MENU");
        ok &= text(buf,p,SB_FONT_CAPTION,(800-pax_text_size(c.font,c.size,v->title).x)/2,136,FG,v->title);
        ok &= text(buf,p,SB_FONT_CAPTION,(800-pax_text_size(c.font,c.size,v->subtitle).x)/2,166,SB_AMBER,v->subtitle);
        }
        row_y=206;
    }
    if(v->headline[0] && v->surface!=SB_MENU_SURFACE_VERIFY) {
        ok &= text(buf,p,SB_FONT_HEADING,x+16,row_y,tone_color(v->tone),v->headline);
        row_y+=48;
    }
    if(v->edit_field) {
        frame(buf,32,150,736,66,ACCENT);
        sb_font_t f=sb_font_get(p,SB_FONT_BODY);
        char prefix[17]; unsigned caret=v->caret>strlen(v->edit_text) ? strlen(v->edit_text) : v->caret;
        memcpy(prefix,v->edit_text,caret); prefix[caret]=0;
        float width=pax_text_size(f.font,f.size,v->edit_text).x;
        if(v->select_all) pax_draw_rect(buf,SB_HULL,36,162,width+8,38);
        ok &= text(buf,p,SB_FONT_BODY,40,166,FG,v->edit_text);
        if(!v->select_all) pax_draw_rect(buf,ACCENT,40+pax_text_size(f.font,f.size,prefix).x,162,2,38);
        row_y=240;
    }
    unsigned displayed=0;
    for(unsigned i=first;i<v->count && i<first+7;++i) {
        if(!v->selectable && v->headline[0] && !strcmp(v->rows[i],v->headline)) continue;
        float y=row_y+(displayed++)*(command || setup || rules ? 38 : SB_ROW_PITCH);
        if(previous>=0 && i!=(unsigned)previous && i!=v->selected) continue;
        bool selected=v->selectable && v->selected==i;
        bool disabled=v->disabled_rows&(1u<<i);
        pax_col_t color=disabled ? SB_MUTED : selected ? BG : FG;
        float inset=match ? 28 : setup || rules ? 12 : 4;
        if(v->selectable) menu_button(buf,x+inset,y-2,w-2*inset,selected,disabled);
        float tx=x+(v->selectable ? inset+20 : 16);
        if(match) {
            sb_font_t f=sb_font_get(p,SB_FONT_BODY);
            tx=(800-pax_text_size(f.font,f.size,v->rows[i]).x)/2;
        }
        if(v->surface==SB_MENU_SURFACE_VERIFY && i==0) {
            if(v->headline[0]) {
                ok &= text(buf,p,SB_FONT_CAPTION,tx,y,SB_MUTED,"Verification code");
                ok &= text(buf,p,SB_FONT_TIMER,x+w-160,y,ACCENT,v->headline);
            } else ok &= text(buf,p,SB_FONT_COORDINATE,tx,y,ACCENT,v->rows[i]);
        } else if(!setup && (v->value_rows&(1u<<i))) {
            char label[96]; snprintf(label,sizeof(label),"%s",v->rows[i]);
            char *value=strchr(label,':');
            if(value) {
                *value++=0; while(*value==' ') ++value;
                sb_font_t f=sb_font_get(p,SB_FONT_BODY);
                float vw=pax_text_size(f.font,f.size,value).x;
                float vx=x+w-36-vw;
                if(tx+pax_text_size(f.font,f.size,label).x+20>vx) return false;
                ok &= text(buf,p,SB_FONT_BODY,tx,y,color,label);
                ok &= text(buf,p,SB_FONT_BODY,vx,y,selected && !disabled ? BG : ACCENT,value);
            } else ok &= text(buf,p,SB_FONT_BODY,tx,y,color,v->rows[i]);
        } else ok &= text(buf,p,SB_FONT_BODY,tx,y,
                          v->surface==SB_MENU_SURFACE_STATUS && i==0 ? tone_color(v->tone) : color,v->rows[i]);
    }
    if(previous>=0) return ok;
    if(v->count>7) {
        pax_draw_rect(buf,SB_FRAME,770,98,2,232);
        pax_draw_rect(buf,ACCENT,770,98+first*29,2,203);
    }
    if(v->count>7 && !v->status[0]) {
        char range[48]; snprintf(range,sizeof(range),"%u-%u of %u",first+1,first+visible,v->count);
        ok &= text(buf,p,SB_FONT_CAPTION,24,354,SB_MUTED,range);
    } else if(setup || rules) {
        if(v->status[0]) {
            bevel(buf,x+8,334,w-16,54,SB_CONTROL,SB_CONTROL_EDGE);
            ok &= paragraph(buf,p,SB_FONT_CAPTION,x+16,336,w-32,392,SB_MUTED,v->status);
        }
    } else if(v->status[0]) {
        pax_draw_rect(buf,SB_STRIP,24,350,752,44);
        ok &= paragraph(buf,p,SB_FONT_CAPTION,24,350,752,398,
                        v->tone==SB_TONE_NORMAL ? SB_MUTED : tone_color(v->tone),v->status);
    }
    if(v->footer[0]) {
        pax_draw_rect(buf,SB_STRIP,24,396,752,34);
        ok &= text(buf,p,SB_FONT_CAPTION,24,398,ACCENT,v->footer);
    }
    return keybar(buf,p,v->key_mask,"Confirm","Select",v->return_keys) && ok;
}

bool sb_ui_menu(pax_buf_t *buf, sb_font_profile_t p, const sb_menu_view_t *v) {
    return menu_draw(buf,p,v,-1);
}

bool sb_ui_menu_selection(pax_buf_t *buf, sb_font_profile_t p,
                          const sb_menu_view_t *before, const sb_menu_view_t *v) {
    // Only opaque selectable rows can be repainted in place. Everything else
    // (including viewport changes, edits and overlays) takes the full path.
    if(!before || !v || !v->selectable || !v->count || v->count>7 ||
       before->selected>=v->count || v->selected>=v->count ||
       v->edit_field || v->headline[0] || v->status[0]) return false;
    if(v->surface!=SB_MENU_SURFACE_COMMAND && v->surface!=SB_MENU_SURFACE_PLAIN &&
       v->surface!=SB_MENU_SURFACE_SETUP && v->surface!=SB_MENU_SURFACE_RULES &&
       v->surface!=SB_MENU_SURFACE_MATCH) return false;
    const size_t selected=offsetof(sb_menu_view_t,selected);
    const size_t after=selected+sizeof(v->selected);
    if(memcmp(before,v,selected) ||
       memcmp((const char *)before+after,(const char *)v+after,sizeof(*v)-after)) return false;
    return menu_draw(buf,p,v,(int)before->selected);
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
    menu_background(buf);
    pax_draw_rect(buf,SB_STRIP,24,8,752,96);
    pax_draw_rect(buf,SB_STRIP,24,400,752,32);
    pax_draw_rect(buf,SB_FRAME,24,46,752,1);
    for(unsigned i=0;i<100;++i)
        if(v->fleet_cells[i]>SB_SUNK || v->target_cells[i]>SB_SUNK) return false;
    const char *owner=v->owner_name && *v->owner_name ? v->owner_name : v->player_b ? "Player B" : "Player A";
    const char *target=v->target_name && *v->target_name ? v->target_name : v->player_b ? "Player A" : "Player B";
    char line[96];
    snprintf(line,sizeof(line),"%s / %s",v->play_type==SB_UI_SINGLE_PLAYER ? "Single Player" : v->play_type==SB_UI_VERSUS ? "Versus" : "Shared Device",v->ruleset==SB_CLASSIC ? "Classic" : "Escalated");
    bool ok=text(buf,p,SB_FONT_HEADING,24,8,FG,line);
    if(v->result) snprintf(line,sizeof(line),"%s | %s",v->phase_label ? v->phase_label : "Resolved",v->action_label ? v->action_label : "");
    else if(v->ruleset==SB_CLASSIC) snprintf(line,sizeof(line),"%s | Turn %lu",(v->owner_turn || v->active) ? "Your turn" : "Opponent's turn",(unsigned long)v->sequence);
    else snprintf(line,sizeof(line),"Round %lu | %u/%u shots",(unsigned long)v->sequence,v->selected,v->available);
    sb_font_t status_font=sb_font_get(p,SB_FONT_CAPTION);
    sb_font_profile_t status_profile=pax_text_size(status_font.font,status_font.size,line).x>752 ? SB_FONT_SMALL : p;
    ok &= text(buf,status_profile,SB_FONT_CAPTION,24,52,FG,line);
    for(unsigned side=0;side<2;++side) {
        bool own=side==0;
        float x=own ? 56 : 456;
        float y=126, cell=27;
        // Coordinates keep their contrast even over the brightest artwork.
        pax_draw_rect(buf,SB_STRIP,x-32,y-20,304,18);
        pax_draw_rect(buf,SB_STRIP,x-32,y-2,30,274);
        frame(buf,x-2,y-2,274,274,SB_FRAME);
        ok &= board_label(buf,p,own ? 24 : 424,82,352,own ? owner : target,own ? "FLEET / X taken" : "TARGETS / + dealt");
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
            pax_col_t color=SB_GRID;
            if(state==SB_SUNK) color=own ? 0xff633a35 : 0xff34513b;
            pax_draw_rect(buf,color,bx+1,by+1,cell-2,cell-2);
        }
        bool geometry=false;
        for(unsigned i=0;i<SB_SHIPS;i++) geometry |= v->ships[i].length!=0;
        if(own && geometry) fleet_hulls(buf,v,x,y,cell);
        for(unsigned i=0;i<100;++i) {
            float bx=x+(i%10)*cell,by=y+(i/10)*cell;
            sb_cell_state_t state=own ? v->fleet_cells[i] : v->target_cells[i];
            if(own && !geometry && v->fleet_occupied[i] && state!=SB_SUNK) hull_cell(buf,bx,by,cell);
            if(state==SB_MISS) {
                if(own) pax_outline_circle(buf,FG,bx+cell/2,by+cell/2,3);
                else pax_draw_circle(buf,FG,bx+cell/2,by+cell/2,2);
            }
            if(state==SB_HIT || state==SB_SUNK) {
                if(own) {
                    cross(buf,INCOMING,bx,by,cell);
                } else {
                    // A green plus differs from damage crosses even without color.
                    pax_draw_rect(buf,OUTGOING,bx+7,by+12,13,3);
                    pax_draw_rect(buf,OUTGOING,bx+12,by+7,3,13);
                }
            }
            if(!own && v->targets[i]) outline(buf,0xffffd166,bx+3,by+3,cell-6);
        }
        if(v->effects) for(unsigned i=0;i<v->effects->count;++i) {
            const sb_event_t *e=&v->effects->events[i];
            bool incoming=e->attacker!=(unsigned)v->player_b;
            if(incoming!=own) continue;
            if(e->type==SB_ATTACK_EVENT) outline(buf,e->hit ? (own ? INCOMING : OUTGOING) : FG,x+e->cell%10*cell+2,y+e->cell/10*cell+2,cell-4);
            else for(unsigned j=0;j<e->length;++j) outline(buf,own ? INCOMING : OUTGOING,x+e->cells[j]%10*cell+2,y+e->cells[j]/10*cell+2,cell-4);
        }
        if(!own && !v->result && v->active) cursor_brackets(buf,x+v->cursor%10*cell,y+v->cursor/10*cell,cell);
    }
    if(v->result) {
        if(v->effects) {
            unsigned taken=0,dealt=0;
            for(unsigned i=0;i<v->effects->count;i++) {
                const sb_event_t *e=&v->effects->events[i];
                if(e->type==SB_ATTACK_EVENT && e->hit) {
                    if(e->attacker==(unsigned)v->player_b) dealt++; else taken++;
                }
            }
            snprintf(line,sizeof(line),"Hits taken %u / dealt %u",taken,dealt);
            ok &= text(buf,p,SB_FONT_CAPTION,24,402,SB_MUTED,line);
        }
    } else {
        const char *notice=v->notice && *v->notice ? v->notice : !v->active ? "Waiting for opponent" : "";
        if(*notice) {
            ok &= paragraph(buf,SB_FONT_SMALL,SB_FONT_CAPTION,24,408,580,432,
                            v->notice_tone==SB_TONE_ERROR ? SB_ERROR : SB_AMBER,notice);
            ok &= text(buf,SB_FONT_SMALL,SB_FONT_CAPTION,630,408,FG,"Esc: Match menu");
        } else {
            ok &= text(buf,p,SB_FONT_CAPTION,24,402,FG,"Arrows: Aim   Esc: Match menu");
        }
    }
    if(v->active && !v->result) {
        snprintf(line,sizeof(line),"%c%u",'A'+v->cursor%10,1+v->cursor/10);
        pax_draw_rect(buf,SB_STRIP,732,126,42,28);
        ok &= text(buf,SB_FONT_SMALL,SB_FONT_COORDINATE,734,132,ACCENT,line);
    }
    return tactical_keybar(buf,p,v) && ok;
}
