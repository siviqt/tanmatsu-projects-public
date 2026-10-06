// SPDX-License-Identifier: MIT
#pragma once
#include "game.h"
void sb_begin_combat(sb_match_t *match);
void sb_snapshot(sb_match_t *match);
void sb_check_result(sb_match_t *match);
void sb_attack(sb_match_t *match, unsigned player, uint8_t cell);
bool sb_command(const sb_match_t *match, unsigned player, uint32_t sequence);
