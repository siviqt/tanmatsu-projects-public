// SPDX-License-Identifier: MIT
#pragma once
#include "services/storage.h"
#include "game/game.h"
#include "esp_err.h"
esp_err_t sb_versus_run(sb_config_t *config,bool host,sb_ruleset_t rules,sb_storage_t *storage);
