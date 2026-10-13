// SPDX-License-Identifier: MIT
#pragma once
#include "services/config.h"
typedef enum {
    SB_STORAGE_UNAVAILABLE, SB_STORAGE_READY, SB_STORAGE_ERROR, SB_STORAGE_PROTECTED
} sb_storage_status_t;
typedef struct {
    sb_storage_status_t status;
    bool mounted, protected_files;
    // Last failed operation, reported once per load/save attempt. No file contents.
    const char *last_operation, *last_path;
    int last_errno;
} sb_storage_t;
// Called only after the platform mount has completed. No mounting or formatting.
void sb_storage_load(sb_storage_t *storage, sb_config_t *config, bool mounted);
// Single application-task owner. Save only when dirty; failure retains dirty.
bool sb_storage_save(sb_storage_t *storage, sb_config_t *config);
const char *sb_storage_status_text(const sb_storage_t *storage);
