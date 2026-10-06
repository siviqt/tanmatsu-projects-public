// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include "services/storage.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fault, rename_count;
static FILE *writing;
FILE *__real_fopen(const char *, const char *);
size_t __real_fwrite(const void *, size_t, size_t, FILE *);
int __real_fflush(FILE *);
int __real_fsync(int);
int __real_fclose(FILE *);
int __real_rename(const char *, const char *);
int __real_unlink(const char *);
FILE *__wrap_fopen(const char *p, const char *m) {
    if (fault==9) { errno=EIO; return NULL; }
    if (fault==1 && strchr(m,'w')) { errno=EROFS; return NULL; }
    FILE *f=__real_fopen(p,m);
    if (strchr(m,'w')) writing=f;
    return f;
}
size_t __wrap_fwrite(const void *p,size_t s,size_t n,FILE *f) {
    if (fault==2) { errno=ENOSPC; return __real_fwrite(p,s,n/2,f); }
    return __real_fwrite(p,s,n,f);
}
int __wrap_fflush(FILE *f) { int r=__real_fflush(f); if(fault==3) {errno=EIO; return -1;} return r; }
int __wrap_fsync(int fd) { if(fault==4) {errno=EIO; return -1;} return __real_fsync(fd); }
int __wrap_fclose(FILE *f) {
    // Only fail the write close (scan uses read streams).
    bool write_close=f==writing;
    if (write_close) writing=NULL;
    int r=__real_fclose(f);
    if(fault==5 && write_close) {errno=EIO; return -1;} return r;
}
int __wrap_rename(const char *a,const char *b) {
    ++rename_count;
    if ((fault==6 && rename_count==1) || (fault==7 && rename_count==2)) {errno=EIO; return -1;}
    return __real_rename(a,b);
}
int __wrap_unlink(const char *p) { if(fault==8) {errno=EIO; return -1;} return __real_unlink(p); }

static void put(const char *name,const char *text) {
    char path[256]; snprintf(path,sizeof(path),"sd/SpaceBattles/%s",name);
    FILE *f=fopen(path,"w"); assert(f); assert(fputs(text,f)>=0); assert(fclose(f)==0);
}
static void clean(void) {
    fault=0; rename_count=0;
    unlink("sd/SpaceBattles/config.ini"); unlink("sd/SpaceBattles/config.bak"); unlink("sd/SpaceBattles/config.tmp");
    rmdir("sd/SpaceBattles");
}
static void check_value(unsigned expected) {
    sb_storage_t s; sb_config_t c; sb_storage_load(&s,&c,true);
    assert(c.settings.brightness==expected);
}
static void check_m10_config(void) {
    sb_config_t c, d;
    char text[SB_CONFIG_TEXT_MAX+1];
    // Existing v1 files gain defaults without being dirtied or rewritten.
    assert(sb_config_parse(&c,"[meta]\nversion=1\n[audio]\nmaster_volume=31\neffects_volume=0\nmusic_volume=73\n")==SB_CONFIG_VALID);
    assert(!c.dirty && c.settings.sound_enabled);
    assert(!strcmp(c.settings.preferred_ruleset,"escalated"));
    assert(sb_config_set(&c,"audio","sound_enabled","true") && !c.dirty);
    assert(sb_config_set(&c,"gameplay","preferred_ruleset","escalated") && !c.dirty);
    assert(!sb_config_set(&c,"audio","sound_enabled","1") && !c.dirty);
    assert(!sb_config_set(&c,"gameplay","preferred_ruleset","Classic") && !c.dirty);
    assert(sb_config_set(&c,"audio","sound_enabled","false") && c.dirty);
    assert(sb_config_set(&c,"gameplay","preferred_ruleset","classic"));
    assert(sb_config_serialize(&c,text,sizeof(text)));
    assert(sb_config_parse(&d,text)==SB_CONFIG_VALID && !d.dirty);
    assert(!d.settings.sound_enabled && !strcmp(d.settings.preferred_ruleset,"classic"));
    assert(sb_config_set(&d,"audio","sound_enabled","true") && d.dirty);
    assert(d.settings.master_volume==31 && d.settings.effects_volume==0 && d.settings.music_volume==73);
    assert(sb_config_set(&d,"gameplay","preferred_ruleset","escalated"));
    assert(sb_config_serialize(&d,text,sizeof(text)));
    assert(sb_config_parse(&c,text)==SB_CONFIG_VALID && !c.dirty);
    assert(c.settings.sound_enabled && !strcmp(c.settings.preferred_ruleset,"escalated"));
    assert(sb_config_parse(&c,"[meta]\nversion=1\n[audio]\nsound_enabled=no\n[gameplay]\npreferred_ruleset=invalid\n")==SB_CONFIG_VALID);
    assert(c.settings.sound_enabled && !strcmp(c.settings.preferred_ruleset,"escalated") && !c.dirty);
    assert(sb_config_parse(&c,"[meta]\nversion=2\n[audio]\nsound_enabled=false\n")==SB_CONFIG_FUTURE);
    assert(c.settings.sound_enabled && !c.dirty);
    sb_config_reset(&d);
    assert(d.dirty && d.settings.sound_enabled && !strcmp(d.settings.preferred_ruleset,"escalated"));
    puts("M10 optional settings, compatibility, dirty state and mute volume preservation passed");
}

int main(void) {
    check_m10_config();
    sb_config_t c,d; sb_storage_t s; char text[SB_CONFIG_TEXT_MAX+1];
    sb_config_defaults(&c); assert(c.settings.brightness==80 && !c.dirty);
    assert(c.settings.keyboard_enabled && c.settings.leds_enabled);
    assert(sb_config_set(&c,"display","brightness","0") && c.settings.brightness==10);
    assert(sb_config_parse(&c,"[meta]\nversion=1\n[display]\nbrightness=3\nkeyboard_enabled=false\nkeyboard_brightness=35\nleds_enabled=false\nled_brightness=25\n")==SB_CONFIG_VALID);
    assert(c.settings.brightness==10 && !c.settings.keyboard_enabled && c.settings.keyboard_brightness==35 && !c.settings.leds_enabled && c.settings.led_brightness==25);
    sb_config_defaults(&c);
    assert(sb_config_serialize(&c,text,sizeof(text))>0);
    assert(sb_config_parse(&d,text)==SB_CONFIG_VALID);
    char again[SB_CONFIG_TEXT_MAX+1]; assert(sb_config_serialize(&d,again,sizeof(again))); assert(!strcmp(text,again));
    assert(sb_config_serialize(&d,again,8)==0);
    assert(sb_config_parse(&c,"# test\r\n [meta]\r\nversion=1\r\n[display]\nbrightness=45 ; hi\nfont_size=large\n[other]\nfoo=bar\n[audio]\nmusic_volume=999\n[gameplay]\ntargeting_confirmation=false\n")==SB_CONFIG_VALID);
    assert(c.settings.brightness==45 && !strcmp(c.settings.font_size,"large"));
    assert(c.settings.music_volume==60 && !c.settings.targeting_confirmation && !c.dirty);
    assert(!sb_config_set(&c,"display","brightness","-1"));
    assert(!sb_config_set(&c,"display","brightness","100x"));
    assert(!sb_config_set(&c,"display","brightness","99999999999999999999999"));
    assert(!sb_config_set(&c,"player","callsign","bad\nkey"));
    assert(!sb_config_set(&c,"player","callsign","12345678901234567"));
    assert(!sb_config_set(&c,"radio","region","unknown"));
    assert(!sb_config_set(&c,"player","callsign"," "));
    assert(!sb_config_set(&c,"player","callsign"," Commander"));
    assert(sb_config_set(&c,"display","brightness","45") && !c.dirty);
    assert(sb_config_set(&c,"display","brightness","46") && c.dirty);
    assert(sb_config_parse(&c,"[meta]\nversion=2\nversion=1\n")==SB_CONFIG_FUTURE);
    assert(sb_config_parse(&c,"[meta]\nversion=0\n")==SB_CONFIG_INVALID);
    assert(sb_config_parse(&c,"[display]\nbrightness=3\n")==SB_CONFIG_INVALID);
    assert(sb_config_parse(&c,"[meta]\nversion=1\n[display]\nbrightness=bad\n")==SB_CONFIG_VALID && c.settings.brightness==80);
    sb_config_reset(&c); assert(c.dirty && c.settings.brightness==80);
    assert(mkdir("sd",0775)==0);
    sb_storage_load(&s,&c,false); assert(s.status==SB_STORAGE_UNAVAILABLE);
    assert(sb_config_set(&c,"audio","music_volume","5")); assert(!sb_storage_save(&s,&c) && c.dirty);
    assert(access("sd/SpaceBattles",F_OK)!=0);
    sb_storage_load(&s,&c,true); assert(s.status==SB_STORAGE_READY && !c.dirty);
    struct stat before,after; assert(stat("sd/SpaceBattles/config.ini",&before)==0);
    assert(sb_storage_save(&s,&c)); assert(stat("sd/SpaceBattles/config.ini",&after)==0); assert(before.st_ino==after.st_ino);
    assert(sb_config_set(&c,"display","brightness","42")); assert(sb_storage_save(&s,&c) && !c.dirty); check_value(42);
    assert(access("sd/SpaceBattles/config.bak",F_OK)==0);
    for(int f=1;f<=8;++f) {
        clean(); sb_storage_load(&s,&c,true);
        assert(sb_config_set(&c,"display","brightness","42"));
        fault=f; rename_count=0;
        assert(!sb_storage_save(&s,&c) && c.dirty);
        fault=0;
        check_value(80); // Never lose the last committed configuration.
        assert(sb_storage_save(&s,&c) && !c.dirty); check_value(42);
    }
    // Every interrupted rename boundary: committed ini wins, backup wins over
    // pending tmp, and a complete tmp is recovered if no committed file exists.
    clean(); sb_storage_load(&s,&c,true);
    put("config.tmp","[meta]\nversion=1\n[display]\nbrightness=42\n"); check_value(80);
    assert(rename("sd/SpaceBattles/config.ini","sd/SpaceBattles/config.bak")==0); check_value(80);
    unlink("sd/SpaceBattles/config.ini"); check_value(42);
    assert(access("sd/SpaceBattles/config.ini",F_OK)==0);
    // Partial or future files remain untouched; defaults/valid backups stay usable.
    for(int i=0;i<3;++i) {
        clean(); sb_storage_load(&s,&c,true);
        const char *names[]={"config.ini","config.bak","config.tmp"};
        put(names[i],"[meta]\nversion=99\n");
        sb_storage_load(&s,&c,true); assert(s.status==SB_STORAGE_PROTECTED);
        sb_config_reset(&c); assert(!sb_storage_save(&s,&c) && c.dirty);
        char path[256], content[64]={0}; snprintf(path,sizeof(path),"sd/SpaceBattles/%s",names[i]);
        FILE *future=fopen(path,"r"); assert(future); assert(fread(content,1,sizeof(content)-1,future)>0); fclose(future);
        assert(!strcmp(content,"[meta]\nversion=99\n"));
    }
    clean(); sb_storage_load(&s,&c,true); put("config.tmp","[meta]\nver");
    sb_storage_load(&s,&c,true); assert(s.status==SB_STORAGE_PROTECTED && c.settings.brightness==80);
    clean(); fault=1; sb_storage_load(&s,&c,true); assert(s.status==SB_STORAGE_ERROR && c.dirty); fault=0;
    assert(sb_storage_save(&s,&c));
    clean(); assert(mkdir("sd/SpaceBattles",0775)==0); put("config.ini", "garbage");
    sb_storage_load(&s,&c,true); assert(s.status==SB_STORAGE_PROTECTED && c.settings.brightness==80);
    clean(); sb_storage_load(&s,&c,true); fault=9;
    sb_storage_load(&s,&c,true); assert(s.status==SB_STORAGE_ERROR && c.settings.brightness==80);
    sb_config_reset(&c); assert(!sb_storage_save(&s,&c) && c.dirty); fault=0; check_value(80);
    // Bounded input and embedded NULs are protected, including hidden versions.
    clean(); sb_storage_load(&s,&c,true);
    char huge[SB_CONFIG_TEXT_MAX+2]; memset(huge,'x',sizeof(huge)-1); huge[sizeof(huge)-1]=0;
    put("config.ini",huge); sb_storage_load(&s,&c,true); assert(s.status==SB_STORAGE_PROTECTED);
    clean(); sb_storage_load(&s,&c,true);
    FILE *nul=fopen("sd/SpaceBattles/config.ini","wb"); assert(nul);
    const char hidden[]="[meta]\nversion=1\n\0version=99\n";
    assert(fwrite(hidden,1,sizeof(hidden)-1,nul)==sizeof(hidden)-1); fclose(nul);
    sb_storage_load(&s,&c,true); assert(s.status==SB_STORAGE_PROTECTED);
    clean(); FILE *f=fopen("sd/SpaceBattles","w"); assert(f); fclose(f);
    sb_storage_load(&s,&c,true); assert(s.status==SB_STORAGE_ERROR); unlink("sd/SpaceBattles");
    puts("M2 parser, persistence, recovery and 9 I/O fault cases passed");
    return 0;
}
