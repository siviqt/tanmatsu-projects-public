// SPDX-License-Identifier: MIT
#include "services/storage.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Override only for host tests; production writes stay beneath /sd/SpaceBattles.
#ifndef SB_STORAGE_DIRECTORY
#define SB_STORAGE_DIRECTORY "/sd/SpaceBattles"
#endif
static const char *const paths[] = {
    SB_STORAGE_DIRECTORY "/config.ini", SB_STORAGE_DIRECTORY "/config.bak",
    SB_STORAGE_DIRECTORY "/config.tmp",
};
typedef enum { MISSING, VALID, INVALID, FUTURE, IO_ERROR } file_result_t;

static file_result_t read_config(const char *path, sb_config_t *c) {
    FILE *f=fopen(path, "rb");
    if (!f) return errno==ENOENT ? MISSING : IO_ERROR;
    char *text=malloc(SB_CONFIG_TEXT_MAX+2);
    if (!text) { fclose(f); return IO_ERROR; }
    size_t n=fread(text, 1, SB_CONFIG_TEXT_MAX+1, f);
    bool failed=ferror(f)!=0;
    if (fclose(f)!=0) failed=true;
    file_result_t result=IO_ERROR;
    if (!failed) {
        text[n]=0;
        if (n>SB_CONFIG_TEXT_MAX || memchr(text, 0, n)) result=INVALID;
        else {
            sb_config_result_t parsed=sb_config_parse(c, text);
            result=parsed==SB_CONFIG_VALID ? VALID : parsed==SB_CONFIG_FUTURE ? FUTURE : INVALID;
        }
    }
    free(text);
    return result;
}

static bool directory(void) {
    struct stat st;
    if (stat(SB_STORAGE_DIRECTORY, &st)==0) return S_ISDIR(st.st_mode);
    return errno==ENOENT && mkdir(SB_STORAGE_DIRECTORY, 0775)==0;
}

static bool remove_if_present(const char *path) {
    return unlink(path)==0 || errno==ENOENT;
}

// Recheck all managed files before any mutation. Unknown versions, unreadable
// and unparseable files are preserved for manual repair, never truncated.
static bool scan(sb_storage_t *s, file_result_t result[3], sb_config_t candidates[3]) {
    bool ok=true;
    for (unsigned i=0; i<3; ++i) {
        result[i]=read_config(paths[i], &candidates[i]);
        if (result[i]==FUTURE || result[i]==INVALID) {
            s->protected_files=true; s->status=SB_STORAGE_PROTECTED; ok=false;
        } else if (result[i]==IO_ERROR) {
            if (!s->protected_files) s->status=SB_STORAGE_ERROR;
            ok=false;
        }
    }
    return ok;
}

void sb_storage_load(sb_storage_t *s, sb_config_t *c, bool mounted) {
    *s=(sb_storage_t){.mounted=mounted, .status=SB_STORAGE_UNAVAILABLE};
    sb_config_defaults(c);
    if (!mounted) return;
    s->status=SB_STORAGE_ERROR;
    if (!directory()) return;
    file_result_t r[3]; sb_config_t candidates[3];
    bool safe=scan(s, r, candidates);
    // Prefer the committed file, then last committed backup, then a complete
    // temporary file. A damaged/future file anywhere freezes persistence.
    int selected=-1;
    for (unsigned i=0; i<3; ++i) if (r[i]==VALID) { selected=(int)i; break; }
    if (selected>=0) *c=candidates[selected];
    if (!safe) return;
    if (selected>0) {
        // Destination is absent here. FatFs does not replace existing targets.
        if (rename(paths[selected], paths[0])!=0) return;
    }
    if (selected>=0) { s->status=SB_STORAGE_READY; return; }
    c->dirty=true;
    (void)sb_storage_save(s, c);
}

bool sb_storage_save(sb_storage_t *s, sb_config_t *c) {
    if (!c->dirty) return true;
    if (!s->mounted || s->protected_files) return false;
    s->status=SB_STORAGE_ERROR;
    file_result_t r[3]; sb_config_t candidates[3];
    if (!scan(s, r, candidates)) return false;
    // Restore the last committed backup before starting another transaction.
    if (r[0]==MISSING && r[1]==VALID) {
        if (rename(paths[1], paths[0])!=0) return false;
        r[0]=VALID; r[1]=MISSING;
    }
    char *text=malloc(SB_CONFIG_TEXT_MAX+1);
    if (!text) return false;
    size_t n=sb_config_serialize(c, text, SB_CONFIG_TEXT_MAX+1);
    // Also reject callers bypassing set() and supplying invalid settings.
    sb_config_t validated;
    char *canonical=malloc(SB_CONFIG_TEXT_MAX+1);
    bool valid=canonical && n && sb_config_parse(&validated, text)==SB_CONFIG_VALID;
    if (valid) valid=sb_config_serialize(&validated, canonical, SB_CONFIG_TEXT_MAX+1)==n && !strcmp(text, canonical);
    free(canonical);
    if (!valid) { free(text); return false; }
    FILE *f=fopen(paths[2], "wb");
    if (!f) { free(text); return false; }
    bool ok=fwrite(text, 1, n, f)==n;
    free(text);
    if (fflush(f)!=0) ok=false;
    // IDF 6.0.2 VFS FatFs implements fsync via f_sync; failures are fatal.
    if (fsync(fileno(f))!=0) ok=false;
    if (fclose(f)!=0) ok=false;
    if (!ok) {
        // Best effort only; if removal fails the next scan protects partial data.
        (void)remove_if_present(paths[2]);
        return false;
    }
    if (read_config(paths[2], &validated)!=VALID) return false;
    if (r[0]==VALID) {
        // Preserve the current validated file until the new file is committed.
        if (!remove_if_present(paths[1])) return false;
        if (rename(paths[0], paths[1])!=0) return false;
    }
    if (rename(paths[2], paths[0])!=0) return false;
    // Keep the backup for startup recovery. No power-loss atomicity is claimed.
    c->dirty=false;
    s->status=SB_STORAGE_READY;
    return true;
}

const char *sb_storage_status_text(const sb_storage_t *s) {
    switch (s->status) {
        case SB_STORAGE_READY: return "SD configuration loaded";
        case SB_STORAGE_PROTECTED: return "Config protected - session settings only";
        case SB_STORAGE_ERROR: return "SD config error - settings not saved";
        default: return "SD unavailable - settings not saved";
    }
}
