// SPDX-License-Identifier: MIT
#pragma once
#include "game/game.h"
typedef enum { SB_AI_EASY, SB_AI_NORMAL } sb_ai_difficulty_t;
/* Public knowledge only: no match/private fleet, renderer, or platform access.
 * allowance is the controller's round-start budget. Output is unique, unknown
 * cells; Classic caps it at one. Returns zero for invalid input/exhaustion. */
size_t sb_ai_choose(const sb_knowledge_t *knowledge, sb_ruleset_t ruleset,
                    sb_ai_difficulty_t difficulty, unsigned allowance,
                    sb_random_fn random, void *context, uint8_t targets[SB_SALVO]);
