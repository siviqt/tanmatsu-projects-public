// SPDX-License-Identifier: MIT
#include "services/storage.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "sdkconfig.h"
#if defined(CONFIG_FATFS_LFN_NONE) || CONFIG_FATFS_MAX_LFN < 12
#error "Persistent /sd/SpaceBattles saves require FatFs long filenames"
#endif
#endif

// Persistent namespace: independent of project name, release version and AppFS ID.
// Override only for host tests; production writes stay beneath /sd/SpaceBattles.
#ifndef SB_STORAGE_DIRECTORY
#define SB_STORAGE_DIRECTORY "/sd/SpaceBattles"
#endif
static const char *const paths[] = {
    SB_STORAGE_DIRECTORY "/config.ini", SB_STORAGE_DIRECTORY "/config.bak",
    SB_STORAGE_DIRECTORY "/config.tmp",
};
static bool failure(sb_storage_t *s, const char *operation, const char *path, int error) {
    s->last_operation=operation; s->last_path=path; s->last_errno=error ? error : EIO;
    return false;
}

static void report(const sb_storage_t *s) {
#ifdef ESP_PLATFORM
    if (s->last_operation)
        ESP_LOGW("Storage", "%s %s: errno=%d (%s)", s->last_operation,
                 s->last_path, s->last_errno, strerror(s->last_errno));
#else
    (void)s;
#endif
}

typedef enum { MISSING, VALID, INVALID, FUTURE, IO_ERROR } file_result_t;

static file_result_t read_config(const char *path, sb_config_t *c, const char *expected) {
    FILE *f=fopen(path, "rb");
    if (!f) return errno==ENOENT ? MISSING : IO_ERROR;
    char *text=malloc(SB_CONFIG_TEXT_MAX+2);
    if (!text) { fclose(f); errno=ENOMEM; return IO_ERROR; }
    size_t n=fread(text, 1, SB_CONFIG_TEXT_MAX+1, f);
    bool failed=ferror(f)!=0;
    int error=failed ? errno : 0;
    if (fclose(f)!=0) { if (!failed) error=errno; failed=true; }
    file_result_t result=IO_ERROR;
    if (!failed) {
        text[n]=0;
        if (n>SB_CONFIG_TEXT_MAX || memchr(text, 0, n) || (expected && strcmp(text,expected))) result=INVALID;
        else {
            sb_config_result_t parsed=sb_config_parse(c, text);
            result=parsed==SB_CONFIG_VALID ? VALID : parsed==SB_CONFIG_FUTURE ? FUTURE : INVALID;
        }
    }
    free(text);
    if (failed) errno=error ? error : EIO;
    return result;
}

static bool directory(sb_storage_t *s) {
    struct stat st;
    if (stat(SB_STORAGE_DIRECTORY, &st)==0)
        return S_ISDIR(st.st_mode) || failure(s,"directory",SB_STORAGE_DIRECTORY,ENOTDIR);
    if (errno!=ENOENT) return failure(s,"stat",SB_STORAGE_DIRECTORY,errno);
    if (mkdir(SB_STORAGE_DIRECTORY,0775)==0) return true;
    return failure(s,"mkdir",SB_STORAGE_DIRECTORY,errno);
}

static bool remove_if_present(const char *path) {
    return unlink(path)==0 || errno==ENOENT;
}

// Recheck all managed files before any mutation. Unknown versions, unreadable
// and unparseable files are preserved for manual repair, never truncated.
static bool scan(sb_storage_t *s, file_result_t result[3], sb_config_t candidates[3]) {
    bool ok=true;
    for (unsigned i=0; i<3; ++i) {
        result[i]=read_config(paths[i], &candidates[i], NULL);
        if (result[i]==FUTURE || result[i]==INVALID) {
            s->protected_files=true; s->status=SB_STORAGE_PROTECTED; ok=false;
            failure(s,result[i]==FUTURE ? "future version" : "invalid config",paths[i],EINVAL);
        } else if (result[i]==IO_ERROR) {
            if (!s->protected_files) s->status=SB_STORAGE_ERROR;
            failure(s,"read",paths[i],errno);
            ok=false;
        }
    }
    return ok;
}

static bool save(sb_storage_t *s, sb_config_t *c);

static void load(sb_storage_t *s, sb_config_t *c, bool mounted) {
    *s=(sb_storage_t){.mounted=mounted, .status=SB_STORAGE_UNAVAILABLE};
    sb_config_defaults(c);
    if (!mounted) return;
    s->status=SB_STORAGE_ERROR;
    if (!directory(s)) return;
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
        if (rename(paths[selected], paths[0])!=0) { failure(s,"recover",paths[selected],errno); return; }
    }
    if (selected>=0) { s->status=SB_STORAGE_READY; return; }
    c->dirty=true;
    (void)save(s, c);
}

static bool save(sb_storage_t *s, sb_config_t *c) {
    if (!c->dirty) return true;
    if (!s->mounted || s->protected_files) return false;
    s->status=SB_STORAGE_ERROR;
    if (!directory(s)) return false;
    file_result_t r[3]; sb_config_t candidates[3];
    if (!scan(s, r, candidates)) return false;
    // Never truncate the sole recoverable file, including a complete first-save
    // temporary file left by an interrupted/failed commit.
    if (r[0]==MISSING) {
        int source=r[1]==VALID ? 1 : r[2]==VALID ? 2 : -1;
        if (source>=0) {
            if (rename(paths[source],paths[0])!=0) return failure(s,"recover",paths[source],errno);
            r[0]=VALID; r[source]=MISSING;
        }
    }
    char *text=malloc(SB_CONFIG_TEXT_MAX+1);
    if (!text) return failure(s,"allocate",paths[2],ENOMEM);
    size_t n=sb_config_serialize(c, text, SB_CONFIG_TEXT_MAX+1);
    // Also reject callers bypassing set() and supplying invalid settings.
    sb_config_t validated;
    char *canonical=malloc(SB_CONFIG_TEXT_MAX+1);
    bool valid=canonical && n && sb_config_parse(&validated, text)==SB_CONFIG_VALID;
    if (valid) valid=sb_config_serialize(&validated, canonical, SB_CONFIG_TEXT_MAX+1)==n && !strcmp(text, canonical);
    int validation_error=canonical ? EINVAL : ENOMEM;
    free(canonical);
    if (!valid) { free(text); return failure(s,"serialize",paths[2],validation_error); }
    FILE *f=fopen(paths[2], "wb");
    if (!f) { int error=errno; free(text); return failure(s,"open write",paths[2],error); }
    bool ok=fwrite(text, 1, n, f)==n;
    if (!ok) failure(s,"write",paths[2],errno);
    if (fflush(f)!=0) { if (ok) failure(s,"flush",paths[2],errno); ok=false; }
    // IDF 6.0.2 VFS FatFs implements fsync via f_sync; failures are fatal.
    if (fsync(fileno(f))!=0) { if (ok) failure(s,"sync",paths[2],errno); ok=false; }
    if (fclose(f)!=0) { if (ok) failure(s,"close",paths[2],errno); ok=false; }
    if (!ok) {
        free(text);
        // Best effort only; if removal fails the next scan protects partial data.
        (void)remove_if_present(paths[2]);
        return false;
    }
    // The parser tolerates missing optional keys: parsing alone cannot detect a
    // truncated or altered yet syntactically valid replacement.
    file_result_t verified=read_config(paths[2], &validated, text);
    int verify_error=verified==IO_ERROR ? errno : EIO;
    free(text);
    if (verified!=VALID) return failure(s,"verify",paths[2],verify_error);
    if (r[0]==VALID) {
        // Preserve the current validated file until the new file is committed.
        if (!remove_if_present(paths[1])) return failure(s,"remove backup",paths[1],errno);
        if (rename(paths[0], paths[1])!=0) return failure(s,"backup",paths[0],errno);
    }
    if (rename(paths[2], paths[0])!=0) return failure(s,"commit",paths[2],errno);
    // Keep the backup for startup recovery. No power-loss atomicity is claimed.
    c->dirty=false;
    s->status=SB_STORAGE_READY;
    return true;
}

void sb_storage_load(sb_storage_t *s, sb_config_t *c, bool mounted) {
    load(s,c,mounted);
    report(s);
}

bool sb_storage_save(sb_storage_t *s, sb_config_t *c) {
    s->last_operation=NULL; s->last_path=NULL; s->last_errno=0;
    bool ok=save(s,c);
    report(s);
    return ok;
}

const char *sb_storage_status_text(const sb_storage_t *s) {
    switch (s->status) {
        case SB_STORAGE_READY: return "SD configuration loaded";
        case SB_STORAGE_PROTECTED: return "Config protected - session settings only";
        case SB_STORAGE_ERROR: return "SD config error - settings not saved";
        default: return "SD unavailable - settings not saved";
    }
}
