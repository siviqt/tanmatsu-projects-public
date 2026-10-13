// SPDX-License-Identifier: MIT
#include "game/ai.h"
#include <string.h>

const char *sb_ai_name(sb_ai_difficulty_t d) {
    return d==SB_AI_HARD ? "Hard" : d==SB_AI_NORMAL ? "Normal" : "Easy";
}
sb_ai_difficulty_t sb_ai_parse(const char *s) {
    return s && !strcmp(s,"hard") ? SB_AI_HARD : s && !strcmp(s,"easy") ? SB_AI_EASY : SB_AI_NORMAL;
}

/* Marginal placement density, not a claim of exact fleet probabilities.
 * Ships may touch. Never exclude neighbors of hits/sunk ships. Recompute
 * after each salvo choice, discounting placements already covered by it. */
static void hard_scores(const sb_knowledge_t *k, const bool used[SB_CELLS], unsigned scores[SB_CELLS]) {
    unsigned hunt[SB_CELLS]={0}, target[SB_CELLS]={0};
    bool targeting=false;
    for(unsigned n=2;n<=SB_SEGMENTS;n++) if(k->remaining[n])
        for(unsigned dir=0;dir<2;dir++) for(unsigned c=0;c<SB_CELLS;c++) {
            if((dir ? c/SB_BOARD_SIDE : c%SB_BOARD_SIDE)+n>SB_BOARD_SIDE) continue;
            unsigned cells[SB_SEGMENTS],hits=0,covered=0; bool legal=true;
            for(unsigned i=0;i<n;i++) {
                unsigned cell=c+i*(dir ? SB_BOARD_SIDE : 1); cells[i]=cell;
                if(k->cells[cell]==SB_MISS || k->cells[cell]==SB_SUNK) legal=false;
                hits+=k->cells[cell]==SB_HIT; covered+=used[cell];
            }
            if(!legal) continue;
            unsigned weight=60*k->remaining[n]/(1+covered);
            for(unsigned i=0;i<n;i++) if(k->cells[cells[i]]==SB_UNKNOWN && !used[cells[i]]) {
                hunt[cells[i]]+=weight;
                if(hits) { target[cells[i]]+=weight*hits*hits; targeting=true; }
            }
        }
    for(unsigned c=0;c<SB_CELLS;c++) scores[c]=targeting ? target[c] : hunt[c];
}

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
        (difficulty < SB_AI_EASY || difficulty > SB_AI_HARD)) return 0;
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
        unsigned total=0, best=0, scores[SB_CELLS]={0};
        if(difficulty==SB_AI_HARD) hard_scores(k,used,scores);
        for (unsigned c=0; c<SB_CELLS; ++c) {
            if (used[c] || k->cells[c]!=SB_UNKNOWN) continue;
            unsigned score=difficulty==SB_AI_HARD ? scores[c] : difficulty==SB_AI_NORMAL ? priority(k,c,smallest) : 0;
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
