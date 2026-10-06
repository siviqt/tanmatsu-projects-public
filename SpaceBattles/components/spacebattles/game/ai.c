// SPDX-License-Identifier: MIT
#include "game/ai.h"

static unsigned priority(const sb_knowledge_t *k, unsigned cell, unsigned smallest) {
    unsigned x=cell%SB_BOARD_SIDE, y=cell/SB_BOARD_SIDE;
    unsigned score=0;
    const int dx[]={-1,1,0,0}, dy[]={0,0,-1,1};
    for (unsigned d=0; d<4; ++d) {
        int nx=(int)x+dx[d], ny=(int)y+dy[d];
        if (nx<0 || nx>=SB_BOARD_SIDE || ny<0 || ny>=SB_BOARD_SIDE) continue;
        if (k->cells[ny*SB_BOARD_SIDE+nx]!=SB_HIT) continue;
        unsigned candidate=2;
        nx+=dx[d]; ny+=dy[d];
        if (nx>=0 && nx<SB_BOARD_SIDE && ny>=0 && ny<SB_BOARD_SIDE &&
            k->cells[ny*SB_BOARD_SIDE+nx]==SB_HIT) candidate=3;
        if (candidate>score) score=candidate;
    }
    // Search stripes intersect every straight ship of the smallest remaining
    // size. They are a preference, never an exclusion (ships may touch).
    return score ? score : ((x+y)%smallest==0);
}
size_t sb_ai_choose(const sb_knowledge_t *k, sb_ruleset_t ruleset,
                    sb_ai_difficulty_t difficulty, unsigned allowance,
                    sb_random_fn random, void *context, uint8_t out[SB_SALVO]) {
    if (!k || !out || !random || !sb_rules(ruleset) ||
        (difficulty!=SB_AI_EASY && difficulty!=SB_AI_NORMAL)) return 0;
    unsigned smallest=0;
    for (unsigned n=2; n<=SB_SEGMENTS; ++n) if (k->remaining[n]) { smallest=n; break; }
    if (!smallest) return 0;
    for (unsigned c=0; c<SB_CELLS; ++c)
        if (k->cells[c]<SB_UNKNOWN || k->cells[c]>SB_SUNK) return 0;
    unsigned limit=ruleset==SB_CLASSIC ? 1 : SB_SALVO;
    if (allowance<limit) limit=allowance;
    bool used[SB_CELLS]={0};
    size_t count=0;
    while (count<limit) {
        uint8_t candidates[SB_CELLS];
        unsigned total=0, best=0;
        for (unsigned c=0; c<SB_CELLS; ++c) {
            if (used[c] || k->cells[c]!=SB_UNKNOWN) continue;
            unsigned score=difficulty==SB_AI_NORMAL ? priority(k,c,smallest) : 0;
            if (score>best) { best=score; total=0; }
            if (score==best) candidates[total++]=(uint8_t)c;
        }
        if (!total) break;
        uint8_t cell=candidates[random(context)%total];
        used[cell]=true;
        out[count++]=cell;
    }
    return count;
}
