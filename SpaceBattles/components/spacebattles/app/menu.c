// SPDX-License-Identifier: MIT
#include "app/menu.h"
#include "services/preferences.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

typedef struct {
    const char *label, *section, *key, *choices;
    size_t offset;
    enum { NUMBER, BOOL, CHOICE } type;
} setting_t;
#define S(label,section,key,type,choices) {label,section,#key,choices,offsetof(sb_settings_t,key),type}
static const setting_t settings[][8] = {
    {S("Display brightness","display",brightness,NUMBER,NULL), S("Font size","display",font_size,CHOICE,"small|medium|large"),
     S("Animation quality","display",animation_quality,CHOICE,"full|reduced|off"), S("Reduced motion","display",reduced_animations,BOOL,NULL),
     S("Keyboard lighting","display",keyboard_enabled,BOOL,NULL), S("Keyboard brightness","display",keyboard_brightness,NUMBER,NULL),
     S("Player LEDs","display",leds_enabled,BOOL,NULL), S("Player LED brightness","display",led_brightness,NUMBER,NULL)},
    {S("Frame rate","performance",mode,CHOICE,"auto|60|30"), S("Power saving","performance",power_saving,BOOL,NULL)},
    {S("Sound","audio",sound_enabled,BOOL,NULL), S("Master volume","audio",master_volume,NUMBER,NULL),
     S("Effects volume","audio",effects_volume,NUMBER,NULL), S("Music volume","audio",music_volume,NUMBER,NULL)},
    {S("Preferred ruleset","gameplay",preferred_ruleset,CHOICE,"classic|escalated"),
     S("AI difficulty","gameplay",ai_difficulty,CHOICE,"easy|normal")},
};
static const char *categories[]={"Display","Performance","Audio","Gameplay","Player","Radio","System"};
static const unsigned counts[]={8,2,4,2,1,2,3};
static void page(sb_menu_t *m, sb_menu_page_t next) { m->page=next; m->row=0; }
static unsigned count(const sb_menu_t *m) {
    switch(m->page) {
        case SB_MENU_SETUP: return m->play_type==SB_SHARED_DEVICE ? 2 : 3;
        case SB_MENU_MAIN: return 7;
        case SB_MENU_SETTINGS: return 7;
        case SB_MENU_CATEGORY: return counts[m->category];
        case SB_MENU_RULES: case SB_MENU_DIFFICULTY: case SB_MENU_VERSUS: return 2;
        default: return 1;
    }
}
void sb_menu_init(sb_menu_t *m, const sb_config_t *c, bool in_match) {
    *m=(sb_menu_t){.page=SB_MENU_MAIN,.settings_return=SB_MENU_MAIN,.in_match=in_match,.rules=SB_ESCALATED,.difficulty=SB_AI_NORMAL};
    if(c) {
        if(!strcmp(c->settings.preferred_ruleset,"classic")) m->rules=SB_CLASSIC;
        if(!strcmp(c->settings.ai_difficulty,"easy")) m->difficulty=SB_AI_EASY;
    }
}
void sb_menu_settings(sb_menu_t *m) { m->settings_return=m->page; m->settings_row=m->row; page(m,SB_MENU_SETTINGS); }
void sb_menu_help(sb_menu_t *m, unsigned n) { m->help_return=m->page; m->help_row=m->row; m->help_page=n%5; page(m,SB_MENU_HELP); }
static void value(const sb_config_t *c, const setting_t *s, char *out, size_t size) {
    const void *p=(const char *)&c->settings+s->offset;
    if(s->type==NUMBER) snprintf(out,size,"%u%s",*(const unsigned *)p,!strcmp(s->section,"display") ? "%" : "");
    else if(s->type==BOOL) snprintf(out,size,"%s",*(const bool *)p ? "On" : "Off");
    else snprintf(out,size,"%s",(const char *)p);
}
static void edit(sb_menu_t *m, sb_config_t *c, int direction) {
    const setting_t *s=&settings[m->category][m->row];
    const void *p=(const char *)&c->settings+s->offset;
    char next[32];
    if(s->type==NUMBER) {
        int n=(int)*(const unsigned *)p+direction*5;
        // The screen must remain readable even at the minimum.
        int minimum=m->category==0 && m->row==0 ? 10 : 0;
        if(n<minimum) n=minimum;
        if(n>100) n=100;
        snprintf(next,sizeof(next),"%d",n);
    } else if(s->type==BOOL) snprintf(next,sizeof(next),"%s",*(const bool *)p ? "false" : "true");
    else {
        char choices[64]; snprintf(choices,sizeof(choices),"%s",s->choices);
        char *items[4]; unsigned n=0, selected=0;
        for(char *t=strtok(choices,"|");t && n<4;t=strtok(NULL,"|")) {
            items[n]=t; if(!strcmp(t,p)) selected=n; ++n;
        }
        // Legacy Hard has no selectable implementation; first edit chooses Normal.
        if(m->category==3 && m->row==1 && !strcmp(p,"hard")) selected=1;
        else selected=(selected+n+direction)%n;
        snprintf(next,sizeof(next),"%s",items[selected]);
    }
    sb_settings_t before=c->settings;
    if(sb_config_set(c,s->section,s->key,next) && memcmp(&before,&c->settings,sizeof(before))) {
        m->changed=true; m->feedback[0]=0;
    }
}
sb_menu_result_t sb_menu_event(sb_menu_t *m, sb_config_t *c, sb_input_event_t e) {
    m->changed=false;
    if(e.phase==SB_INPUT_RELEASE) return SB_MENU_STAY;
    bool arrow=e.action>=SB_ACTION_UP && e.action<=SB_ACTION_RIGHT;
    if(e.phase==SB_INPUT_REPEAT && !arrow && !(m->page==SB_MENU_CALLSIGN && e.action==SB_ACTION_BACKSPACE)) return SB_MENU_STAY;
    bool back=e.action==SB_ACTION_CANCEL;
    bool enter=e.action==SB_ACTION_CONFIRM;
    if(m->page==SB_MENU_SAVED) return back || enter ? SB_MENU_CLOSE : SB_MENU_STAY;
    if(m->page==SB_MENU_CALLSIGN) {
        if(back) { page(m,SB_MENU_CATEGORY); return SB_MENU_STAY; }
        if(enter) {
            if(sb_config_set(c,"player","callsign",m->callsign)) { m->changed=true; page(m,SB_MENU_CATEGORY); }
            else snprintf(m->feedback,sizeof(m->feedback),"Use 1-16 characters; no leading/trailing spaces.");
        } else {
            size_t n=strlen(m->callsign);
            bool insertion=e.action>=SB_ACTION_TEXT_BASE && e.action<SB_ACTION_TEXT_BASE+128;
            bool erase=e.action==SB_ACTION_BACKSPACE || e.action==SB_ACTION_DELETE;
            if((insertion || erase) && m->select_all) { m->callsign[0]=0; m->character=0; n=0; m->select_all=false; }
            if(insertion && n<16) {
                char ch=(char)(e.action-SB_ACTION_TEXT_BASE);
                if((ch>='A' && ch<='Z') || (ch>='a' && ch<='z') || (ch>='0' && ch<='9') || ch==' ' || ch=='_' || ch=='-') {
                    memmove(m->callsign+m->character+1,m->callsign+m->character,n-m->character+1);
                    m->callsign[m->character++]=ch;
                }
            } else if(e.action==SB_ACTION_BACKSPACE && m->character) {
                memmove(m->callsign+m->character-1,m->callsign+m->character,n-m->character+1); --m->character;
            } else if(e.action==SB_ACTION_DELETE && m->character<n) {
                memmove(m->callsign+m->character,m->callsign+m->character+1,n-m->character);
            } else if(e.action==SB_ACTION_HOME || e.action==SB_ACTION_LEFT) {
                m->character=e.action==SB_ACTION_HOME || m->select_all ? 0 : m->character ? m->character-1 : 0; m->select_all=false;
            } else if(e.action==SB_ACTION_END || e.action==SB_ACTION_RIGHT) {
                m->character=e.action==SB_ACTION_END || m->select_all ? n : m->character<n ? m->character+1 : n; m->select_all=false;
            }
        }
        return SB_MENU_STAY;
    }
    if(m->page==SB_MENU_HELP) {
        if(back || e.action==SB_ACTION_HELP) {
            if(m->in_match) return SB_MENU_CLOSE;
            page(m,m->help_return); m->row=m->help_row;
        } else if(arrow) m->help_page=(m->help_page+((e.action==SB_ACTION_LEFT || e.action==SB_ACTION_UP) ? 4 : 1))%5;
        return SB_MENU_STAY;
    }
    if(m->page==SB_MENU_RESET) {
        if(back) page(m,SB_MENU_CATEGORY);
        else if(enter) { sb_config_reset(c); m->changed=true; page(m,SB_MENU_CATEGORY); return SB_MENU_SAVE; }
        return SB_MENU_STAY;
    }
    if(m->page==SB_MENU_STORAGE || m->page==SB_MENU_DIAGNOSTICS) {
        if(back) page(m,SB_MENU_CATEGORY);
        else if(enter && m->page==SB_MENU_STORAGE) return SB_MENU_SAVE;
        return SB_MENU_STAY;
    }
    if(m->page==SB_MENU_LOGOFF) {
        if(back) page(m,SB_MENU_MAIN);
        else if(enter) return SB_MENU_EXIT;
        return SB_MENU_STAY;
    }
    if(back) {
        switch(m->page) {
            case SB_MENU_MAIN: page(m,SB_MENU_LOGOFF); break;
            case SB_MENU_SETTINGS: if(!m->in_match) { page(m,m->settings_return); m->row=m->settings_row; } return SB_MENU_SAVE;
            case SB_MENU_CATEGORY: page(m,SB_MENU_SETTINGS); m->row=m->category; break;
            case SB_MENU_RULES: page(m,m->host ? SB_MENU_VERSUS : SB_MENU_MAIN); break;
            case SB_MENU_DIFFICULTY: page(m,SB_MENU_RULES); m->row=m->rules==SB_ESCALATED; break;
            case SB_MENU_NETWORK: page(m,SB_MENU_VERSUS); break;
            default: page(m,SB_MENU_MAIN); break;
        }
        return SB_MENU_STAY;
    }
    if(e.action==SB_ACTION_HELP && (m->page==SB_MENU_MAIN || m->page==SB_MENU_SETUP)) { sb_menu_help(m,0); return SB_MENU_STAY; }
    if(e.action==SB_ACTION_PAUSE && (m->page==SB_MENU_MAIN || m->page==SB_MENU_SETUP)) { sb_menu_settings(m); return SB_MENU_STAY; }
    if(e.action==SB_ACTION_UP || e.action==SB_ACTION_DOWN) {
        m->row=(m->row+count(m)+(e.action==SB_ACTION_UP ? -1 : 1))%count(m);
        return SB_MENU_STAY;
    }
    if(m->page==SB_MENU_CATEGORY && m->category<4 && (enter || e.action==SB_ACTION_LEFT || e.action==SB_ACTION_RIGHT)) {
        if(enter && settings[m->category][m->row].type==NUMBER) return SB_MENU_STAY;
        edit(m,c,e.action==SB_ACTION_LEFT ? -1 : 1); return SB_MENU_STAY;
    }
    if(m->page==SB_MENU_SETUP && (enter || e.action==SB_ACTION_LEFT || e.action==SB_ACTION_RIGHT)) {
        if(m->row==0) m->rules=m->rules==SB_CLASSIC ? SB_ESCALATED : SB_CLASSIC;
        else if(m->row==1 && m->play_type!=SB_SHARED_DEVICE) m->difficulty=m->difficulty==SB_AI_EASY ? SB_AI_NORMAL : SB_AI_EASY;
        else if(enter) return SB_MENU_START;
        return SB_MENU_STAY;
    }
    if(!enter) return SB_MENU_STAY;
    switch(m->page) {
        case SB_MENU_MAIN:
            if(m->row==0) { m->host=false; m->play_type=SB_SINGLE_PLAYER; page(m,SB_MENU_SETUP); m->row=2; }
            else if(m->row==1) { m->host=false; m->play_type=SB_SHARED_DEVICE; page(m,SB_MENU_SETUP); m->row=1; }
            else if(m->row==2) page(m,SB_MENU_VERSUS);
            else if(m->row==3) sb_menu_settings(m);
            else if(m->row==4) sb_menu_help(m,0);
            else if(m->row==5) page(m,SB_MENU_ABOUT);
            else page(m,SB_MENU_LOGOFF);
            break;
        case SB_MENU_RULES:
            m->rules=m->row ? SB_ESCALATED : SB_CLASSIC;
            page(m,m->host ? SB_MENU_NETWORK : SB_MENU_DIFFICULTY);
            m->row=m->host ? 0 : m->difficulty==SB_AI_NORMAL;
            break;
        case SB_MENU_DIFFICULTY: m->difficulty=m->row ? SB_AI_NORMAL : SB_AI_EASY; return SB_MENU_START;
        case SB_MENU_VERSUS:
            m->host=m->row==0; m->play_type=SB_VERSUS;
            page(m,m->host ? SB_MENU_RULES : SB_MENU_NETWORK);
            if(m->host) m->row=m->rules==SB_ESCALATED;
            break;
        case SB_MENU_SETTINGS: m->category=m->row; page(m,SB_MENU_CATEGORY); break;
        case SB_MENU_CATEGORY:
            if(m->category==4) {
                snprintf(m->callsign,sizeof(m->callsign),"%s",c->settings.callsign);
                m->character=strlen(m->callsign); m->select_all=true; page(m,SB_MENU_CALLSIGN);
            } else if(m->category==6) page(m,m->row==0 ? SB_MENU_RESET : m->row==1 ? SB_MENU_STORAGE : SB_MENU_DIAGNOSTICS);
            break;
        default: break;
    }
    return SB_MENU_STAY;
}
static void row(sb_menu_view_t *v, const char *s) { if(v->count<SB_MENU_ROWS) snprintf(v->rows[v->count++],96,"%s",s); }
void sb_menu_view(const sb_menu_t *m, const sb_config_t *c, const char *storage, sb_menu_view_t *v) {
    *v=(sb_menu_view_t){.selected=m->row,.selectable=true};
    snprintf(v->footer,sizeof(v->footer),"Up/Down: Choose   Enter: Select   Esc: Back");
    snprintf(v->status,sizeof(v->status),"%s",m->feedback);
    if(m->page==SB_MENU_HELP) v->key_mask=1;
    if(m->page==SB_MENU_MAIN || m->page==SB_MENU_SETUP) v->key_mask=(1u<<0)|(1u<<3);
    switch(m->page) {
        case SB_MENU_SETUP:
            snprintf(v->title,sizeof(v->title),"%s",m->play_type==SB_SHARED_DEVICE ? "Shared Device" : "Single Player");
            snprintf(v->subtitle,sizeof(v->subtitle),"Five ships / 17 squares");
            snprintf(v->rows[v->count++],96,"Ruleset: %s",m->rules==SB_CLASSIC ? "Classic" : "Escalated");
            if(m->play_type!=SB_SHARED_DEVICE) snprintf(v->rows[v->count++],96,"Difficulty: %s",m->difficulty==SB_AI_EASY ? "Easy" : "Normal");
            row(v,"Start deployment");
            snprintf(v->status,sizeof(v->status),"%s",m->rules==SB_CLASSIC ? "One shot, alternating turns; untimed." : m->play_type==SB_SHARED_DEVICE ? "Private salvos resolve together; untimed." : "Simultaneous salvos; 60s deployment, 10min combat.");
            snprintf(v->footer,sizeof(v->footer),"Arrows: Choose / change   Enter: Select   Esc: Back"); break;
        case SB_MENU_MAIN:
            snprintf(v->title,sizeof(v->title),"SPACEBATTLES");
            snprintf(v->subtitle,sizeof(v->subtitle),"Welcome, %s",c->settings.callsign);
            row(v,"Single Player"); row(v,"Shared Device"); row(v,"Versus (LoRa)"); row(v,"Settings"); row(v,"Help"); row(v,"About"); row(v,"Log off"); break;
        case SB_MENU_RULES:
            snprintf(v->title,sizeof(v->title),"%s / Ruleset",m->host ? "Versus host" : "Single Player");
            snprintf(v->subtitle,sizeof(v->subtitle),"Five ships / 17 squares / 10 x 10 board");
            row(v,"Classic - one shot, alternating, untimed"); row(v,"Escalated - salvos, one per surviving ship"); break;
        case SB_MENU_DIFFICULTY:
            snprintf(v->title,sizeof(v->title),"%s / Difficulty",m->rules==SB_CLASSIC ? "Classic" : "Escalated");
            snprintf(v->subtitle,sizeof(v->subtitle),"Enter starts a new match; preferences are unchanged.");
            row(v,"Easy"); row(v,"Normal"); break;
        case SB_MENU_VERSUS:
            snprintf(v->title,sizeof(v->title),"Versus (LoRa)");
            snprintf(v->subtitle,sizeof(v->subtitle),"Network play is not available in this build."); row(v,"Host - choose ruleset"); row(v,"Join - availability"); break;
        case SB_MENU_NETWORK:
            snprintf(v->title,sizeof(v->title),"%s",m->host ? "Versus host" : "Join game"); v->selectable=false;
            if(m->host) row(v,m->rules==SB_CLASSIC ? "Classic / alternating turns / untimed" : "Escalated / simultaneous salvos");
            row(v,"Five ships / 17 squares"); row(v,"Secure multiplayer is not available yet."); row(v,"No advertising, scanning or radio transmission.");
            if(m->host && m->rules==SB_ESCALATED) row(v,"Planned: 60s deployment, 30s targeting.");
            snprintf(v->footer,sizeof(v->footer),"Esc: Return to Versus"); break;
        case SB_MENU_SETTINGS:
            snprintf(v->title,sizeof(v->title),"Settings");
            snprintf(v->subtitle,sizeof(v->subtitle),"%s",m->in_match ? "Match rules stay fixed; gameplay changes apply next match." : "Changes apply now; saved when leaving Settings.");
            for(unsigned i=0;i<7;++i) row(v,categories[i]);
            break;
        case SB_MENU_CATEGORY:
            snprintf(v->title,sizeof(v->title),"Settings / %s",categories[m->category]);
            snprintf(v->subtitle,sizeof(v->subtitle),"%s",m->category==0 ? "Display: 10-100%. Lighting applies immediately." : m->category==1 ? "Frame-rate target is a cap, not a guaranteed rate." : m->category==2 ? "Mute preserves volumes. Game sounds arrive later." : m->category==3 ? "Ruleset applies to new matches; difficulty is AI only." : "Up/Down selects; Enter opens an item.");
            if(m->category<4) {
                for(unsigned i=0;i<counts[m->category];++i) {
                    char val[32],line[96]; value(c,&settings[m->category][i],val,sizeof(val));
                    snprintf(line,sizeof(line),"%s: %s",settings[m->category][i].label,val); row(v,line);
                }
                snprintf(v->footer,sizeof(v->footer),"Up/Down: Choose   Left/Right: Change   Esc: Back");
            } else if(m->category==4) { char line[64]; snprintf(line,sizeof(line),"Callsign: %s",c->settings.callsign); row(v,line); }
            else if(m->category==5) { row(v,"Region: EU868 (configuration only)"); row(v,"Channel: default (configuration only)"); snprintf(v->subtitle,sizeof(v->subtitle),"Radio disabled; no validated production profiles yet."); }
            else { row(v,"Restore defaults..."); row(v,"Configuration status / Retry save"); row(v,"Diagnostics"); }
            break;
        case SB_MENU_SAVED:
            snprintf(v->title,sizeof(v->title),"Settings / Save status"); v->selectable=false;
            row(v,m->feedback); v->status[0]=0;
            snprintf(v->footer,sizeof(v->footer),"Enter or Esc: Return to match"); break;
        case SB_MENU_RESET:
            snprintf(v->title,sizeof(v->title),"Restore defaults?"); v->selectable=false;
            row(v,"Reset all settings, including callsign and audio."); row(v,"The current match keeps its rules and controls.");
            snprintf(v->footer,sizeof(v->footer),"Enter: Restore and save   Esc: Cancel"); break;
        case SB_MENU_CALLSIGN:
            snprintf(v->title,sizeof(v->title),"Commander callsign"); v->selectable=false;
            snprintf(v->subtitle,sizeof(v->subtitle),"Type letters and numbers; up to 16 characters.");
            v->edit_field=true; v->select_all=m->select_all; v->caret=m->character;
            snprintf(v->edit_text,sizeof(v->edit_text),"%s",m->callsign);
            row(v,"Letters, digits, spaces, underscore and hyphen.");
            snprintf(v->footer,sizeof(v->footer),"Enter: Save   Esc: Cancel   Backspace/Delete: Edit"); break;
        case SB_MENU_HELP: {
            static const char *titles[]={"Fleet and damage","Classic","Escalated","Shared Device","Controls"};
            static const char *lines[][7]={
                {"10 x 10 board. Five ships: 5, 4, 3, 3, 2.","Place straight ships; touching is allowed.","Ships cannot overlap or leave the board.","Each new hit removes one segment.","Sinking reveals that ship's coordinates.","Other unknown cells remain hidden.","Destroy the opposing fleet to win."},
                {"One shot per turn; players alternate.","Hits and sinkings do not grant extra shots.","Deployment and combat have no timer.","The first starter is randomly chosen.","Classic rematches alternate the starter.","Previously fired coordinates cannot be reused.","Enter fires immediately; no confirmation."},
                {"Each surviving ship grants one shot per round.","Both salvos resolve together; mutual loss draws.","AI/LoRa deployment: 60s, then auto-complete.","Single Player: 10-minute combat clock.","At expiry, compare surviving ships, then HP.","Enter fires the salvo; unused shots are lost.","Shared Device: private turns, no timers."},
                {"Classic or Escalated; Player A deploys, then B.","Pass the device only behind the opaque cover.","Press Space to begin immediately.","Help/Settings require a fresh Space press.","Both rulesets untimed. Results last 3 seconds.","A/B LEDs identify the receiving player.","Private salvos resolve together in Escalated."},
                {"Arrows: Move   Space: Place / select shot","R: Rotate ship   Tab: Next ship","Enter: Ready / Fire / Complete turn","F2: Fleet   F3: Targets   F6: Both boards","F1: Help   F4: Settings / pause","Esc: Match menu; resume or abandon","Shared Device also offers Surrender."}
            };
            snprintf(v->title,sizeof(v->title),"Help / %s",titles[m->help_page]);
            snprintf(v->subtitle,sizeof(v->subtitle),"Page %u / 5",m->help_page+1);
            v->selectable=false; for(unsigned i=0;i<7;++i) row(v,lines[m->help_page][i]);
            snprintf(v->footer,sizeof(v->footer),"Left/Right: Page   F1 or Esc: Return"); break;
        }
        case SB_MENU_ABOUT:
            snprintf(v->title,sizeof(v->title),"About SpaceBattles"); v->selectable=false;
            row(v,"Tactical fleet battles for Tanmatsu."); row(v,"Classic and Escalated / five ships / 17 squares"); row(v,"Single Player and Shared Device available."); row(v,"Secure Versus and game audio are forthcoming."); row(v,"Software license: MIT"); break;
        case SB_MENU_STORAGE:
            snprintf(v->title,sizeof(v->title),"Configuration status"); v->selectable=false;
            row(v,storage); row(v,c->dirty ? "Session changes have not been saved." : "No pending changes.");
            row(v,"Files: /sd/SpaceBattles/config.ini");
            snprintf(v->footer,sizeof(v->footer),"Enter: Retry save   Esc: Back"); break;
        case SB_MENU_DIAGNOSTICS: {
            snprintf(v->title,sizeof(v->title),"Diagnostics"); v->selectable=false;
            char line[96]; row(v,"Configuration schema: version 1");
            snprintf(line,sizeof(line),"Frame interval target: %u ms",sb_frame_interval_ms(&c->settings)); row(v,line);
            snprintf(line,sizeof(line),"Effects / music gain: %u%% / %u%%",sb_audio_gain(&c->settings,false),sb_audio_gain(&c->settings,true)); row(v,line);
            snprintf(line,sizeof(line),"Animation policy: %s",sb_animation_level(&c->settings)==0 ? "off" : sb_animation_level(&c->settings)==1 ? "reduced" : "full"); row(v,line);
            row(v,"Radio: disabled / local games only");
            snprintf(v->footer,sizeof(v->footer),"Esc: Back"); break;
        }
        case SB_MENU_LOGOFF:
            snprintf(v->title,sizeof(v->title),"Log off?"); v->selectable=false;
            row(v,"Save settings and return to the launcher."); row(v,"Active games are not saved."); snprintf(v->footer,sizeof(v->footer),"Enter: Log off   Esc: Cancel"); break;
    }
}
