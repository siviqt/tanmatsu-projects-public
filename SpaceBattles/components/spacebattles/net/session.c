// SPDX-License-Identifier: MIT
#include "net/session.h"
#include <string.h>
#define STATE_SIZE 104
_Static_assert(STATE_SIZE<=SB_NET_PAYLOAD,"State must fit one radio frame");
static bool send(sb_net_session_t *s,uint8_t type,const void *p,size_t n) {
    return sb_net_send(&s->link,type,s->sequence,p,n);
}
void sb_net_abandon(sb_net_session_t *s,const char *reason) {
    if(!s || s->phase==SB_NET_FINISHED) return;
    s->phase=SB_NET_ABANDONED; s->outcome=SB_ABANDONED; s->end_reason=SB_END_ABORTED; s->error=reason;
    s->record.outcome=s->outcome; s->record.end_reason=s->end_reason;
}
void sb_net_close(sb_net_session_t *s) { if(s) sb_net_wipe(s,sizeof(*s)); }
static void fleet_encode(const sb_fleet_t *f,uint8_t b[10]) {
    for(unsigned i=0;i<5;i++) {
        b[2*i]=f->ships[i].placed ? f->ships[i].cells[0] : 255;
        b[2*i+1]=f->ships[i].placed && f->ships[i].cells[1]-f->ships[i].cells[0]==10;
    }
}
static bool fleet_decode(sb_fleet_t *f,const uint8_t b[10],bool complete) {
    *f=(sb_fleet_t){0};
    for(unsigned i=0;i<5;i++) {
        if(b[2*i+1]>1) return false;
        if(b[2*i]==255 && !complete && !b[2*i+1]) continue;
        if(b[2*i]>=100 || !sb_place(f,i,b[2*i]%10,b[2*i]/10,b[2*i+1])) return false;
    }
    return sb_fleet_valid(f,complete);
}
static void host_project(sb_net_session_t *s) {
    sb_match_t *m=&s->match;
    s->fleet=m->fleets[0]; memcpy(s->knowledge,m->knowledge,sizeof(s->knowledge));
    s->sequence=m->sequence; s->actor=m->actor; s->outcome=m->outcome; s->end_reason=m->end_reason;
    for(unsigned p=0;p<2;p++) { s->deployed[p]=m->ready[p]; s->committed[p]=m->salvo_locked[p]; s->allowance[p]=m->phase==SB_TARGETING ? m->allowance[p] : 0;
        unsigned unknown=0; for(unsigned c=0;c<100;c++) unknown+=m->knowledge[p].cells[c]==SB_UNKNOWN;
        if(s->allowance[p]>unknown) s->allowance[p]=(uint8_t)unknown; }
    s->phase=m->phase==SB_FINISHED ? SB_NET_FINISHED : m->phase==SB_DEPLOYMENT ?
        m->ready[0] ? SB_NET_WAIT_DEPLOY : SB_NET_DEPLOY : m->salvo_locked[0] ? SB_NET_EXCHANGE : SB_NET_SELECT;
}
static bool begin(sb_net_session_t *s) {
    if(s->started || !s->confirmed[0] || !s->confirmed[1]) return true;
    s->started=true; s->phase=SB_NET_DEPLOY;
    return s->role || sb_match_init(&s->match,&s->spec,s->now);
}
bool sb_net_session_init(sb_net_session_t *s,const sb_net_pairing_t *p,uint64_t now) {
    if(!s || !p || !p->derived || p->failed) return false;
    *s=(sb_net_session_t){.spec=p->spec,.phase=SB_NET_VERIFY,.role=p->role,.now=now,.last_ping=now};
    if(!sb_net_pair_link(p,&s->link,now)) return false;
    memcpy(s->transcript,p->transcript,32); sb_knowledge_init(&s->knowledge[0]); sb_knowledge_init(&s->knowledge[1]);
    return true;
}
bool sb_net_confirm(sb_net_session_t *s) {
    if(!s || s->phase!=SB_NET_VERIFY || s->confirmed[s->role]) return false;
    /* Host initiates key confirmation; the guest response piggybacks receipt.
     * A guest can confirm in the UI first without colliding on the radio. */
    if(!s->role || s->confirmed[0]) {
        if(!send(s,SB_N_READY,s->transcript,32)) return false;
        s->ready_sent=true;
    }
    s->confirmed[s->role]=true; return begin(s);
}
bool sb_net_place(sb_net_session_t *s,unsigned ship,unsigned x,unsigned y,bool vertical) {
    if(!s || s->phase!=SB_NET_DEPLOY || s->leaving || s->peer_left || s->surrender_sent || s->local_deployed) return false;
    if(s->role) return sb_place(&s->fleet,ship,x,y,vertical);
    if(!sb_match_place(&s->match,0,ship,x,y,vertical)) return false;
    host_project(s); return true;
}
bool sb_net_deploy(sb_net_session_t *s) {
    if(!s || s->phase!=SB_NET_DEPLOY || s->leaving || s->peer_left || s->surrender_sent || s->local_deployed || !sb_fleet_valid(&s->fleet,true)) return false;
    if(s->role) {
        uint8_t b[10]; fleet_encode(&s->fleet,b);
        if(!send(s,SB_N_DEPLOY,b,10)) return false;
        s->fleet.locked=true; s->deployed[1]=true; s->phase=SB_NET_WAIT_DEPLOY;
    } else {
        if(!sb_match_ready(&s->match,0)) return false;
        s->state_dirty=true; host_project(s);
    }
    s->local_deployed=true; return true;
}
bool sb_net_can_act(const sb_net_session_t *s) {
    return s && s->phase==SB_NET_SELECT && !s->leaving && !s->peer_left && !s->committed[s->role] && !s->surrender_sent && !s->link.failed &&
        !s->link.recovering && !s->link.count && !s->state_dirty &&
        (s->spec.rules.id==SB_ESCALATED || s->actor==s->role);
}
bool sb_net_targets(sb_net_session_t *s,const uint8_t *cells,size_t n) {
    if(!sb_net_can_act(s) || n>5 || n>s->allowance[s->role] || (n && !cells) || (s->spec.rules.id==SB_CLASSIC && n>1)) return false;
    for(size_t i=0;i<n;i++) if(cells[i]>=100 || (i && cells[i]<=cells[i-1]) || s->knowledge[s->role].cells[cells[i]]!=SB_UNKNOWN) return false;
    if(n) memcpy(s->targets[s->role],cells,n);
    s->target_count[s->role]=(uint8_t)n; return true;
}
static bool host_action(sb_net_session_t *s,unsigned p,const uint8_t *cells,size_t n) {
    sb_match_t *m=&s->match;
    if(m->phase!=SB_TARGETING || m->salvo_locked[p]) return false;
    for(size_t i=1;i<n;i++) if(cells[i]<=cells[i-1]) return false;
    bool ok=m->spec.rules.id==SB_CLASSIC ? n==1 && sb_classic_fire(m,p,m->sequence,cells[0]) :
        sb_select(m,p,m->sequence,cells,n) && sb_escalated_lock(m,p,m->sequence,true);
    if(!ok) return false;
    if(m->phase==SB_LOCKED) {
        if(!sb_match_resolve(m,m->sequence,&s->record)) return false;
        if(m->phase!=SB_FINISHED && !sb_match_advance(m,m->sequence)) return false;
        memset(s->target_count,0,sizeof(s->target_count)); s->state_dirty=true;
    }
    host_project(s); return true;
}
bool sb_net_lock(sb_net_session_t *s) {
    if(!sb_net_can_act(s)) return false;
    unsigned p=s->role; size_t n=s->target_count[p];
    if(s->spec.rules.id==SB_CLASSIC && n!=1) return false;
    if(!p) return host_action(s,0,s->targets[0],n);
    uint8_t b[6]; b[0]=(uint8_t)n; memcpy(b+1,s->targets[1],n);
    if(!send(s,SB_N_ACT,b,n+1)) return false;
    s->committed[1]=true; s->phase=SB_NET_EXCHANGE;
    s->response_due=s->spec.rules.id==SB_CLASSIC ? s->now+6ULL*s->link.profile.retry_ms+s->link.profile.grace_ms : 0;
    return true;
}
/* Full guest projection is intentionally bounded to a single frame. No host
 * layout is serialized, including in recovery. A snapshot replaces a fragile
 * delta/reorder protocol; state revision and digest make replay inspectable. */
static bool state_encode(sb_net_session_t *s,uint8_t b[STATE_SIZE]) {
    sb_match_t *m=&s->match; memset(b,0,STATE_SIZE);
    sb_net_put32(b,s->revision+1); sb_net_put32(b+4,m->sequence);
    b[8]=m->phase==SB_FINISHED ? 2 : m->phase==SB_DEPLOYMENT ? 0 : 1;
    b[9]=(uint8_t)m->outcome; b[10]=m->actor;
    for(unsigned p=0;p<2;p++) {
        b[11]|=(uint8_t)m->ready[p]<<p; b[12]|=(uint8_t)m->salvo_locked[p]<<p;
        /* allowance is the round-start entitlement even after a local lock. */
        b[13+p]=m->phase==SB_TARGETING ? m->allowance[p] : 0;
        for(unsigned c=0;c<100;c++) b[25+25*p+c/4]|=(uint8_t)m->knowledge[p].cells[c]<<(2*(c%4));
        memcpy(b+75+6*p,m->knowledge[p].remaining,6);
    }
    fleet_encode(&m->fleets[1],b+15);
    b[87]=(uint8_t)m->end_reason;
    uint8_t hash[32]; if(!sb_net_hash(b,88,hash)) return false;
    memcpy(b+88,hash,16); return true;
}
static bool state_apply(sb_net_session_t *s,const uint8_t *b,size_t n) {
    if(n!=STATE_SIZE || b[8]>2 || b[9]>SB_DRAW || b[10]>1 || b[11]>3 || b[12]>3 || b[13]>5 || b[14]>5) return false;
    if(!sb_result_valid((sb_outcome_t)b[9],(sb_end_reason_t)b[87])) return false;
    if(s->phase==SB_NET_FINISHED && (b[8]!=2 || b[9]!=s->outcome || b[87]!=s->end_reason)) return false;
    if(s->spec.rules.id==SB_CLASSIC && (b[13]>1 || b[14]>1)) return false;
    uint32_t revision=sb_net_get32(b),seq=sb_net_get32(b+4); uint8_t hash[32];
    if(!revision || revision<=s->revision || seq<s->sequence || !sb_net_hash(b,88,hash) || !sb_net_equal(hash,b+88,16)) return false;
    if((b[8]==0 && (seq || b[9] || b[12])) || (b[8]==1 && (!seq || b[9] || b[11]!=3)) || (b[8]==2 && !b[9])) return false;
    sb_fleet_t fleet; if(!fleet_decode(&fleet,b+15,(b[11]&2)!=0)) return false;
    sb_knowledge_t knowledge[2];
    for(unsigned p=0;p<2;p++) {
        for(unsigned c=0;c<100;c++) knowledge[p].cells[c]=(b[25+25*p+c/4]>>(2*(c%4)))&3;
        memcpy(knowledge[p].remaining,b+75+6*p,6);
        if(knowledge[p].remaining[0] || knowledge[p].remaining[1] || knowledge[p].remaining[2]>1 ||
           knowledge[p].remaining[3]>2 || knowledge[p].remaining[4]>1 || knowledge[p].remaining[5]>1) return false;
    }
    sb_record_t record={.sequence=s->sequence,.outcome=(sb_outcome_t)b[9],.end_reason=(sb_end_reason_t)b[87]};
    for(unsigned p=0;p<2;p++) for(unsigned c=0;c<100;c++) {
        if(s->knowledge[p].cells[c]==SB_UNKNOWN && knowledge[p].cells[c]!=SB_UNKNOWN && record.count<SB_EVENTS)
            record.events[record.count++]=(sb_event_t){.type=SB_ATTACK_EVENT,.attacker=(uint8_t)p,.cell=(uint8_t)c,.hit=knowledge[p].cells[c]!=SB_MISS};
    }
    if(record.count || b[8]==2 || (seq>s->sequence && s->sequence)) s->record=record;
    if(seq!=s->sequence || b[8]==2) { s->target_count[1]=0; s->committed[1]=false; s->response_due=0; }
    s->revision=revision; s->sequence=seq; s->actor=b[10]; s->outcome=(sb_outcome_t)b[9]; s->end_reason=(sb_end_reason_t)b[87]; memcpy(s->digest,b+88,16);
    memcpy(s->knowledge,knowledge,sizeof(knowledge));
    for(unsigned p=0;p<2;p++) {
        s->deployed[p]=(b[11]&(1u<<p))!=0; s->allowance[p]=b[13+p];
        unsigned unknown=0; for(unsigned c=0;c<100;c++) unknown+=knowledge[p].cells[c]==SB_UNKNOWN;
        if(s->allowance[p]>unknown) s->allowance[p]=(uint8_t)unknown;
    }
    /* An older waiting snapshot must not unlock the guest's submitted fleet or salvo. */
    s->deployed[1]|=s->local_deployed;
    if(b[11]&2) {
        for(unsigned c=0;c<100;c++) fleet.hit[c]=knowledge[0].cells[c]!=SB_UNKNOWN;
        fleet.locked=true; s->fleet=fleet;
    }
    s->committed[0]=(b[12]&1)!=0; s->committed[1]|=(b[12]&2)!=0;
    s->phase=b[8]==2 ? SB_NET_FINISHED : b[8]==0 ? s->local_deployed ? SB_NET_WAIT_DEPLOY : SB_NET_DEPLOY :
        s->committed[1] ? SB_NET_EXCHANGE : SB_NET_SELECT;
    return true;
}
bool sb_net_can_surrender(const sb_net_session_t *s) {
    return s && !s->leaving && !s->peer_left && s->started && s->outcome==SB_ONGOING && !s->committed[s->role] &&
        s->phase>=SB_NET_DEPLOY && s->phase<SB_NET_FINISHED && !s->surrender_sent &&
        !s->link.failed && !s->link.recovering;
}
bool sb_net_surrender(sb_net_session_t *s) {
    if(!sb_net_can_surrender(s)) return false;
    if(s->role) { s->surrender_sent=send(s,SB_N_SURRENDER,NULL,0); return s->surrender_sent; }
    if(!sb_match_surrender(&s->match,0)) return false;
    s->record=s->match.record;
    s->state_dirty=true; host_project(s); return true;
}
bool sb_net_rematch(sb_net_session_t *s) {
    if(!s || s->leaving || s->peer_left || s->link.failed || s->link.recovering || s->phase!=SB_NET_FINISHED || s->rematch[s->role] || s->state_dirty || s->link.count) return false;
    if(!sb_net_random(s->rematch_nonce[s->role],16) || !send(s,SB_N_REMATCH,s->rematch_nonce[s->role],16)) return false;
    s->rematch[s->role]=true; s->rematch_ready=s->rematch[1-s->role]; return true;
}
static void peer_departed(sb_net_session_t *s) {
    unsigned peer=1-s->role;
    // Once the authenticated peer leaves, pending gameplay/rematch commands
    // are obsolete. Retain any in-flight buffer until worker completion; the
    // next poll sends the departure receipt rather than a queued command.
    s->link.count=0; s->link.pending.size=0; s->link.pending_sent=false;
    s->peer_left=true;
    s->response_due=0;
    s->rematch_ready=false;
    if(s->phase==SB_NET_ABANDONED) return;
    if(!s->started) { sb_net_abandon(s,"Opponent cancelled pairing"); return; }
    if(s->phase!=SB_NET_FINISHED) {
        // An authenticated deliberate departure forfeits, even after a salvo lock.
        if(!s->role) (void)sb_match_leave(&s->match,peer);
        s->outcome=s->role ? SB_WIN_B : SB_WIN_A;
        s->end_reason=peer ? SB_END_LEFT_B : SB_END_LEFT_A;
        s->record.outcome=s->outcome; s->record.end_reason=s->end_reason;
        s->phase=SB_NET_FINISHED;
        s->error="Opponent has abandoned";
    } else if(!s->error) s->error="Opponent left the match";
}
bool sb_net_leave(sb_net_session_t *s) {
    if(!s || s->peer_left || s->link.failed || s->phase==SB_NET_ABANDONED) return false;
    s->leaving=true; s->rematch_ready=false; s->response_due=0;
    return true;
}
static bool previous_message(void *ctx,const sb_net_message_t *m,uint64_t now) {
    (void)now;
    if(m->type!=SB_N_LEAVE || m->size) return false;
    peer_departed(ctx); return true;
}
bool sb_net_session_receive_previous(sb_net_session_t *s,sb_net_link_t *previous,const sb_net_packet_t *p,uint64_t now) {
    bool received=s && previous && sb_net_receive(previous,p,now,previous_message,s);
    if(received && s->peer_left) {
        previous->count=0; previous->pending.size=0; previous->pending_sent=false;
    }
    return received;
}
static bool handle(void *ctx,const sb_net_message_t *m,uint64_t now) {
    sb_net_session_t *s=ctx; unsigned peer=1-s->role; s->now=now;
    if(s->phase==SB_NET_ABANDONED) return false;
    if(m->type==SB_N_READY) {
        if(s->phase!=SB_NET_VERIFY || m->sequence || m->size!=32 || !sb_net_equal(m->data,s->transcript,32)) return false;
        s->confirmed[peer]=true;
        if(s->leaving) return true;
        if(s->role && s->confirmed[1] && !s->ready_sent) {
            if(!send(s,SB_N_READY,s->transcript,32)) return false;
            s->ready_sent=true;
        }
        return begin(s);
    }
    if(m->type==SB_N_LEAVE && !m->size) {
        peer_departed(s); return true;
    }
    if(s->peer_left) return false; // A departed peer cannot reopen or change a result.
    if(m->type==SB_N_STATUS && m->size==20) {
        if(!s->leaving && s->started && !s->role && (sb_net_get32(m->data)!=s->revision || !sb_net_equal(m->data+4,s->digest,16))) s->state_dirty=true;
        return true;
    }
    if(!s->started) return false;
    if(m->type==SB_N_REMATCH && s->phase==SB_NET_FINISHED && m->size==16) {
        memcpy(s->rematch_nonce[peer],m->data,16); s->rematch[peer]=true; s->rematch_ready=!s->leaving && s->rematch[s->role]; return true;
    }
    if(s->role) return m->type==SB_N_STATE && state_apply(s,m->data,m->size);
    /* A host surrender can cross a valid guest command on the wire. Consume
     * that cancelled command so its receipt cannot block a terminal rematch. */
    if(s->phase==SB_NET_FINISHED && m->sequence<=s->sequence) {
        if(m->type==SB_N_SURRENDER && !m->size) return true;
        if(m->type==SB_N_DEPLOY && m->size==10) { sb_fleet_t fleet; return fleet_decode(&fleet,m->data,true); }
        if(m->type==SB_N_ACT && m->size>=1 && m->data[0]<=5 && m->size==m->data[0]+1) {
            for(unsigned i=0;i<m->data[0];i++) if(m->data[1+i]>=100 || (i && m->data[1+i]<=m->data[i])) return false;
            return true;
        }
    }
    if(m->type==SB_N_SURRENDER && !m->size && s->outcome==SB_ONGOING && !s->match.salvo_locked[1]) {
        if(!sb_match_surrender(&s->match,1)) return false;
        s->record=s->match.record;
        s->state_dirty=true; host_project(s); return true;
    }
    if(m->sequence!=s->sequence) return false;
    if(m->type==SB_N_DEPLOY && m->size==10 && s->match.phase==SB_DEPLOYMENT && !s->match.ready[1]) {
        sb_fleet_t fleet; if(!fleet_decode(&fleet,m->data,true)) return false;
        s->match.fleets[1]=fleet;
        if(!sb_match_ready(&s->match,1)) return false;
        s->state_dirty=true; host_project(s); return true;
    }
    if(m->type==SB_N_ACT && m->size>=1 && m->data[0]<=5 && m->size==m->data[0]+1)
        return host_action(s,1,m->data+1,m->data[0]);
    return false;
}
void sb_net_tick(sb_net_session_t *s,uint64_t now) {
    if(!s || now<s->now || (s->phase==SB_NET_ABANDONED && !s->leaving)) return;
    s->now=now;
    if(s->peer_left) return; // Keep the terminal result/reason; polling can still ACK LEAVE.
    sb_net_link_tick(&s->link,now);
    if(s->leaving) {
        // A host may leave immediately after winning, before its final snapshot
        // has been queued. Deliver that result ahead of LEAVE so it is retained.
        if(!s->leave_command && !s->role && s->state_dirty && (s->match.ready[1] || s->match.phase==SB_FINISHED)) {
            if(s->link.count) return;
            uint8_t b[STATE_SIZE];
            if(s->revision==UINT32_MAX || !state_encode(s,b) || !send(s,SB_N_STATE,b,sizeof(b))) return;
            ++s->revision; memcpy(s->digest,b+88,16); s->state_dirty=false;
        }
        if(!s->leave_command && !s->link.failed && s->link.count<SB_NET_QUEUE) {
            uint32_t command=s->link.next_command;
            if(send(s,SB_N_LEAVE,NULL,0)) s->leave_command=command;
        }
        return;
    }
    if(s->link.failed) { sb_net_abandon(s,"Link ended - no winner"); if(s->phase==SB_NET_FINISHED) s->error="Link ended - result retained"; return; }
    if(s->response_due && now>=s->response_due) { sb_net_abandon(s,"Host response expired"); return; }
    if(!s->role && s->state_dirty && !s->link.count && (s->match.ready[1] || s->match.phase==SB_FINISHED)) {
        uint8_t b[STATE_SIZE];
        if(s->revision==UINT32_MAX || !state_encode(s,b) || !send(s,SB_N_STATE,b,sizeof(b))) { sb_net_abandon(s,"State unavailable"); return; }
        ++s->revision; memcpy(s->digest,b+88,16); s->state_dirty=false;
    }
    if((!s->role || s->link.recovering) && !s->link.count && !s->rematch_ready && now-s->last_ping>=s->link.profile.heartbeat_ms && now-s->link.last_rx>=s->link.profile.heartbeat_ms) {
        uint8_t b[20]; sb_net_put32(b,s->revision); memcpy(b+4,s->digest,16);
        if(send(s,SB_N_STATUS,b,20)) s->last_ping=now;
    }
}
bool sb_net_session_receive(sb_net_session_t *s,const sb_net_packet_t *p,uint64_t now) {
    if(!s || now<s->now) return false;
    sb_net_tick(s,now); return sb_net_receive(&s->link,p,now,handle,s);
}
bool sb_net_session_poll(sb_net_session_t *s,uint64_t now,sb_net_packet_t *p) {
    if(!s) return false;
    sb_net_tick(s,now); return (s->phase!=SB_NET_ABANDONED || s->peer_left || s->leaving) && sb_net_poll(&s->link,now,p);
}
