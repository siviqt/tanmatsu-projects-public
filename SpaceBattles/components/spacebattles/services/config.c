// SPDX-License-Identifier: MIT
#include "services/config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct {
    const char *section, *key;
    size_t offset, size;
    enum { NUMBER, BOOLEAN, CHOICE, CALLSIGN } type;
    const char *choices;
} field_t;
#define FIELD(s, k, t, c) {s, #k, offsetof(sb_settings_t, k), sizeof(((sb_settings_t *)0)->k), t, c}
static const field_t fields[] = {
    FIELD("display", brightness, NUMBER, NULL),
    FIELD("display", keyboard_enabled, BOOLEAN, NULL),
    FIELD("display", keyboard_brightness, NUMBER, NULL),
    FIELD("display", leds_enabled, BOOLEAN, NULL),
    FIELD("display", led_brightness, NUMBER, NULL),
    FIELD("display", font_size, CHOICE, "small|medium|large"),
    FIELD("display", animation_quality, CHOICE, "full|reduced|off"),
    FIELD("display", reduced_animations, BOOLEAN, NULL),
    FIELD("performance", mode, CHOICE, "auto|60|30"),
    FIELD("performance", power_saving, BOOLEAN, NULL),
    FIELD("audio", sound_enabled, BOOLEAN, NULL),
    FIELD("audio", master_volume, NUMBER, NULL),
    FIELD("audio", effects_volume, NUMBER, NULL),
    FIELD("audio", music_volume, NUMBER, NULL),
    FIELD("gameplay", preferred_ruleset, CHOICE, "classic|escalated"),
    FIELD("gameplay", ai_difficulty, CHOICE, "easy|normal|hard"),
    FIELD("gameplay", targeting_confirmation, BOOLEAN, NULL),
    FIELD("player", callsign, CALLSIGN, NULL),
    // NONE fails closed; old files lack manual_region and require confirmation.
    FIELD("radio", region, CHOICE, "NONE|EU868|US915"),
    FIELD("radio", manual_region, CHOICE, "NONE|EU868|US915"),
    FIELD("radio", channel, CHOICE, "default"),
};

void sb_config_defaults(sb_config_t *c) {
    memset(c, 0, sizeof(*c));
    c->settings = (sb_settings_t){
        .keyboard_enabled=true, .keyboard_brightness=50, .leds_enabled=true, .led_brightness=100,
        .brightness=80, .master_volume=80, .effects_volume=80, .music_volume=60,
        .sound_enabled=true, .preferred_ruleset="escalated",
        .targeting_confirmation=true, .font_size="medium", .animation_quality="full",
        .mode="auto", .ai_difficulty="normal", .callsign="COMMANDER",
        .region="NONE", .manual_region="NONE", .channel="default",
    };
}

static bool choice(const char *list, const char *value) {
    size_t n = strlen(value);
    while (*list) {
        size_t len = strcspn(list, "|");
        if (len == n && !strncmp(list, value, n)) return true;
        list += len;
        if (*list) ++list;
    }
    return false;
}

bool sb_config_set(sb_config_t *c, const char *section, const char *key, const char *value) {
    for (size_t i=0; i<sizeof(fields)/sizeof(fields[0]); ++i) {
        const field_t *f = &fields[i];
        if (strcmp(section, f->section) || strcmp(key, f->key)) continue;
        void *dest = (char *)&c->settings + f->offset;
        if (f->type == NUMBER) {
            if (!*value) return false;
            for (const char *p=value; *p; ++p) if (*p<'0' || *p>'9') return false;
            errno=0;
            unsigned long n = strtoul(value, NULL, 10);
            if (errno || n>100) return false;
            if(f->offset==offsetof(sb_settings_t,brightness) && n<10) n=10;
            if (*(unsigned *)dest != n) { *(unsigned *)dest = (unsigned)n; c->dirty=true; }
        } else if (f->type == BOOLEAN) {
            if (strcmp(value, "true") && strcmp(value, "false")) return false;
            bool b = !strcmp(value, "true");
            if (*(bool *)dest != b) { *(bool *)dest=b; c->dirty=true; }
        } else {
            size_t len = strlen(value);
            if (!len || len>=f->size) return false;
            if (f->type == CHOICE && !choice(f->choices, value)) return false;
            if (f->type == CALLSIGN) {
                if (value[0]==' ' || value[len-1]==' ') return false;
                for (const char *p=value; *p; ++p)
                    if (!((*p>='A' && *p<='Z') || (*p>='a' && *p<='z') ||
                          (*p>='0' && *p<='9') || *p==' ' || *p=='_' || *p=='-')) return false;
            }
            if (strcmp(dest, value)) {
                if (f->offset==offsetof(sb_settings_t,region)) strcpy(c->settings.manual_region,"NONE");
                memcpy(dest, value, len+1); c->dirty=true;
            }
        }
        return true;
    }
    return false;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) ++s;
    char *end = s+strlen(s);
    while (end>s && isspace((unsigned char)end[-1])) --end;
    *end=0;
    return s;
}

sb_config_result_t sb_config_parse(sb_config_t *c, const char *text) {
    sb_config_defaults(c);
    if (strlen(text)>SB_CONFIG_TEXT_MAX) return SB_CONFIG_INVALID;
    char line[256], section[64]="";
    bool version=false, invalid=false, future=false, radio_invalid=false;
    bool region_seen=false, manual_seen=false;
    char manual_region[8]="NONE";
    while (*text) {
        size_t len = strcspn(text, "\n");
        if (len>=sizeof(line)) { invalid=true; text+=len; if (*text) ++text; continue; }
        memcpy(line, text, len); line[len]=0;
        text+=len; if (*text) ++text;
        char *s=trim(line);
        if (!*s || *s==';' || *s=='#') continue;
        if (*s=='[') {
            char *end=strchr(s, ']');
            section[0]=0;
            if (!end || *trim(end+1)) continue;
            *end=0; s=trim(s+1);
            if (strlen(s)<sizeof(section)) strcpy(section, s);
            continue;
        }
        char *eq=strchr(s, '=');
        if (!eq) continue;
        *eq=0;
        char *key=trim(s), *value=trim(eq+1);
        // Inline comments require separating whitespace.
        for (char *p=value; *p; ++p) {
            if ((*p==';' || *p=='#') && (p==value || isspace((unsigned char)p[-1]))) { *p=0; break; }
        }
        value=trim(value);
        if (!strcmp(section,"meta") && !strcmp(key,"version")) {
            bool digits=*value != 0;
            for (char *p=value; *p; ++p) if (*p<'0' || *p>'9') digits=false;
            errno=0; unsigned long v=strtoul(value, NULL, 10);
            if (digits && (errno==ERANGE || v>SB_CONFIG_VERSION)) future=true;
            if (!digits || v!=SB_CONFIG_VERSION || version) invalid=true;
            version=true;
        } else {
            if(!strcmp(section,"radio") && !strcmp(key,"manual_region")) {
                if(manual_seen || !choice("NONE|EU868|US915",value)) radio_invalid=true;
                else snprintf(manual_region,sizeof(manual_region),"%s",value);
                manual_seen=true;
            } else {
                bool accepted=sb_config_set(c, section, key, value);
                if(!strcmp(section,"radio") && !strcmp(key,"region")) {
                    if(region_seen || !accepted) radio_invalid=true;
                    region_seen=true;
                }
            }
        }
    }
    if(radio_invalid) {
        strcpy(c->settings.region,"NONE");
        strcpy(c->settings.manual_region,"NONE");
    } else strcpy(c->settings.manual_region,manual_region);
    c->dirty=false;
    if (future || invalid || !version) {
        sb_config_defaults(c);
        return future ? SB_CONFIG_FUTURE : SB_CONFIG_INVALID;
    }
    return SB_CONFIG_VALID;
}

void sb_config_reset(sb_config_t *c) { sb_config_defaults(c); c->dirty=true; }

size_t sb_config_serialize(const sb_config_t *c, char *text, size_t capacity) {
    int n=snprintf(text, capacity, "[meta]\nversion=%u\n", SB_CONFIG_VERSION);
    if (n<0 || (size_t)n>=capacity) return 0;
    size_t used=(size_t)n;
    const char *section="";
    for (size_t i=0; i<sizeof(fields)/sizeof(fields[0]); ++i) {
        const field_t *f=&fields[i];
        if (strcmp(section, f->section)) {
            section=f->section;
            n=snprintf(text+used, capacity-used, "\n[%s]\n", section);
            if (n<0 || (size_t)n>=capacity-used) return 0;
            used+=(size_t)n;
        }
        const void *src=(const char *)&c->settings+f->offset;
        char number[16]; const char *value=src;
        if (f->type==NUMBER) { snprintf(number, sizeof(number), "%u", *(const unsigned *)src); value=number; }
        if (f->type==BOOLEAN) value=*(const bool *)src ? "true" : "false";
        n=snprintf(text+used, capacity-used, "%s=%s\n", f->key, value);
        if (n<0 || (size_t)n>=capacity-used) return 0;
        used+=(size_t)n;
    }
    return used;
}

bool sb_config_radio_confirmed(const sb_config_t *c) {
    return c && (!strcmp(c->settings.region,"EU868") || !strcmp(c->settings.region,"US915")) &&
        !strcmp(c->settings.region,c->settings.manual_region);
}
