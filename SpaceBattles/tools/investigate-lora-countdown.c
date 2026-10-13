// SPDX-License-Identifier: MIT
// Regression for the former countdown exit and all authoritative tactical views.
#include "app/versus_run.c"
#include <assert.h>
int main(void) {
    static sb_net_session_t s; static sb_ui_tactical_t v; pax_buf_t b;
    assert(pax_buf_init(&b,NULL,800,480,PAX_BUF_16_565RGB)); unsigned frames=0;
    for(unsigned rules=SB_CLASSIC;rules<=SB_ESCALATED;rules++) {
        assert(sb_spec_make(&s.spec,rules,SB_VERSUS,0));
        assert(!s.spec.deployment_ms && !s.spec.selection_ms);
        for(unsigned font=0;font<SB_FONT_PROFILE_COUNT;font++) {
            s.phase=SB_NET_DEPLOY; s.role=0; s.sequence=0;
            project(&s,&v,0,0,false,true);
            assert(sb_ui_tactical(&b,font,&v));
            for(unsigned role=0;role<2;role++) for(unsigned phase=SB_NET_DEPLOY;phase<=SB_NET_EXCHANGE;phase++)
                for(unsigned own=0;own<2;own++) for(unsigned recovery=0;recovery<2;recovery++) {
                    if(phase==SB_NET_DEPLOY && !own) continue;
                    s.role=role; s.phase=phase; s.sequence=phase>=SB_NET_SELECT ? 1 : 0;
                    s.allowance[role]=rules==SB_CLASSIC ? 1 : 5; s.link.recovering=recovery;
                    project(&s,&v,99,4,true,own);
                    if(!sb_ui_tactical(&b,font,&v)) fprintf(stderr,"Rejected rules%u font%u role%u phase%u own%u\n",rules,font,role,phase,own);
                    assert(sb_ui_tactical(&b,font,&v)); ++frames;
                }
        }
    }
    printf("Real-font tactical regression: %u untimed views passed.\n",frames);
    printf("Host sizeof: session=%zu pairing=%zu view=%zu link=%zu\n",sizeof(s),sizeof(sb_net_pairing_t),sizeof(v),sizeof(s.link));
    pax_buf_destroy(&b); return 0;
}
