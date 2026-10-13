// SPDX-License-Identifier: MIT
#include "net/wire.h"
#include <string.h>
void sb_net_put32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(24-8*i)); }
void sb_net_put64(uint8_t *p,uint64_t n) { sb_net_put32(p,(uint32_t)(n>>32)); sb_net_put32(p+4,(uint32_t)n); }
uint32_t sb_net_get32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
uint64_t sb_net_get64(const uint8_t *p) { return (uint64_t)sb_net_get32(p)<<32|sb_net_get32(p+4); }
bool sb_net_spec_encode(const sb_spec_t *s,uint8_t o[24]) {
    if(!s || !o || s->play_type!=SB_VERSUS || !sb_spec_valid(s)) return false;
    memset(o,0,24); o[0]=SB_NET_SPEC_VERSION; o[1]=(uint8_t)s->rules.id; o[2]=s->rules.version; o[3]=10; o[4]=5;
    memcpy(o+5,s->rules.lengths,5); o[10]=s->starter;
    sb_net_put32(o+12,s->deployment_ms); sb_net_put32(o+16,s->selection_ms); sb_net_put32(o+20,s->match_ms);
    return true;
}
bool sb_net_spec_decode(const uint8_t in[24],sb_spec_t *s) {
    sb_spec_t candidate; uint8_t expected[24];
    if(!in || !s || !sb_spec_make(&candidate,(sb_ruleset_t)in[1],SB_VERSUS,in[10]) ||
       !sb_net_spec_encode(&candidate,expected) || memcmp(expected,in,24)) return false;
    *s=candidate; return true;
}
uint32_t sb_net_airtime_us(size_t n) {
    if(n>253) return 0;
    /* ceil((8*n - 4*7 + 28 + 16)/(4*7))*5 + 8 payload symbols. */
    uint32_t symbols=8+(uint32_t)((8*n+16+27)/28)*5;
    return (symbols*4+49)*256; /* 8 preamble + 4.25, 1024 us/symbol */
}
bool sb_net_profile_valid(const sb_net_profile_t *p) {
    if(!p || !p->validated || p->max_packet!=253 || !p->duty_permille || p->duty_permille>1000 ||
       !p->retry_ms || p->retry_ms>10000 || p->grace_ms!=30000 ||
       p->heartbeat_ms>60000 || p->silence_ms>180000 || p->heartbeat_ms<p->retry_ms ||
       p->silence_ms<p->heartbeat_ms+2*p->retry_ms) return false;
    uint64_t spacing=(sb_net_airtime_us(p->max_packet)+p->duty_permille-1)/p->duty_permille;
    return 2*spacing+1000<=p->retry_ms && 3ULL*p->retry_ms<=p->grace_ms;
}
bool sb_net_link_init(sb_net_link_t *l,unsigned role,const uint8_t session[16],const uint8_t spec[32],
                      const uint8_t material[72],const sb_net_profile_t *p,uint64_t now) {
    if(!l || role>1 || !session || !spec || !material || !sb_net_profile_valid(p)) return false;
    *l=(sb_net_link_t){.role=(uint8_t)role,.profile=*p,.last_rx=now,.now=now,.next_command=1};
    memcpy(l->session,session,16); memcpy(l->spec,spec,32);
    memcpy(l->keys,material,32); memcpy(l->prefixes,material+32,8); return true;
}
bool sb_net_send(sb_net_link_t *l,uint8_t type,uint32_t sequence,const void *data,size_t n) {
    if(!l || l->failed || type<SB_N_READY || type>SB_N_REMATCH || n>SB_NET_PAYLOAD ||
       (n && !data) || l->count==SB_NET_QUEUE || !l->next_command) return false;
    sb_net_message_t *m=&l->queue[(l->head+l->count)%SB_NET_QUEUE];
    *m=(sb_net_message_t){.type=type,.size=(uint8_t)n,.sequence=sequence,.command=l->next_command++};
    if(n) memcpy(m->data,data,n);
    ++l->count; return true;
}
/* header: SB, version, type, game[8], role, counter32, command32,
 * cumulative receipt32, game sequence32, payload length. */
static bool seal(sb_net_link_t *l,sb_net_packet_t *p,const sb_net_message_t *m) {
    if(l->tx_counter>=UINT32_MAX) { l->failed=true; return false; }
    memset(p,0,sizeof(*p)); p->size=SB_NET_HEADER+m->size+16;
    uint8_t *b=p->bytes; b[0]='S'; b[1]='B'; b[2]=SB_NET_VERSION; b[3]=m->type;
    memcpy(b+4,l->session,8); b[12]=l->role; sb_net_put32(b+13,(uint32_t)++l->tx_counter);
    sb_net_put32(b+17,m->command); sb_net_put32(b+21,l->received_command);
    sb_net_put32(b+25,m->sequence); b[29]=m->size;
    uint8_t nonce[12]={0}; memcpy(nonce,l->prefixes[l->role],4); memcpy(nonce+8,b+13,4);
    if(!sb_net_aead(true,l->keys[l->role],nonce,b,SB_NET_HEADER,m->data,m->size,b+SB_NET_HEADER,b+SB_NET_HEADER+m->size)) {
        l->failed=true; return false;
    }
    return true;
}
bool sb_net_receive(sb_net_link_t *l,const sb_net_packet_t *p,uint64_t now,sb_net_deliver_fn deliver,void *ctx) {
    if(!l || !p || l->failed || now<l->now || p->size<46 || p->size>253) return false;
    const uint8_t *b=p->bytes; uint32_t ctr=sb_net_get32(b+13),receipt=sb_net_get32(b+21);
    if(b[0]!='S' || b[1]!='B' || b[2]!=SB_NET_VERSION || b[3]<SB_N_ACK || b[3]>SB_N_REMATCH ||
       b[12]!=1-l->role || memcmp(b+4,l->session,8) || !ctr || b[29]>SB_NET_PAYLOAD || p->size!=(size_t)46+b[29]) return false;
    sb_net_message_t m={.type=b[3],.size=b[29],.command=sb_net_get32(b+17),.sequence=sb_net_get32(b+25)};
    if((m.type==SB_N_ACK && (m.size || m.command || m.sequence)) || (m.type!=SB_N_ACK && !m.command)) return false;
    uint8_t nonce[12]={0},tag[16],hash[32]; memcpy(nonce,l->prefixes[1-l->role],4); memcpy(nonce+8,b+13,4);
    memcpy(tag,b+30+m.size,16);
    if(!sb_net_aead(false,l->keys[1-l->role],nonce,b,30,b+30,m.size,m.data,tag) || !sb_net_hash(b,p->size,hash)) return false;
    if(m.type!=SB_N_ACK && m.command==l->received_command) {
        if(!sb_net_equal(hash,l->received_hash,32)) return false;
        l->ack_due=true; return true; /* Replay never renews liveness. */
    }
    if(ctr<=l->rx_high && (l->rx_high-ctr>=64 || (l->rx_window & (UINT64_C(1)<<(l->rx_high-ctr))))) return false;
    /* Receipt can release a command before a new application message is handled.
     * It is authenticated and must exactly identify our one outstanding frame. */
    bool submitted=l->pending_sent || (l->inflight && l->flight.bytes[3]!=SB_N_ACK && l->count &&
        sb_net_get32(l->flight.bytes+17)==l->queue[l->head].command);
    /* RX and completion use separate queues: a real receipt can reach the
     * owner between draining completions and draining RX. Receipt is sufficient
     * proof of transmission even if its TX_DONE is processed next iteration. */
    if(l->count && submitted && receipt==l->queue[l->head].command) {
        l->head=(l->head+1)%SB_NET_QUEUE; --l->count; l->pending.size=0; l->pending_sent=false;
        l->recovering=false;
    }
    if(m.type!=SB_N_ACK) {
        if(l->received_command==UINT32_MAX || m.command!=l->received_command+1 || !deliver || !deliver(ctx,&m,now)) return false;
        l->received_command=m.command; memcpy(l->received_hash,hash,32); l->ack_due=true;
    }
    if(ctr>l->rx_high) {
        uint64_t shift=ctr-l->rx_high; l->rx_window=shift>=64 ? 1 : (l->rx_window<<shift)|1; l->rx_high=ctr;
    } else l->rx_window|=UINT64_C(1)<<(l->rx_high-ctr);
    l->last_rx=now; l->now=now;
    if(!l->count) l->recovering=false;
    return true;
}
void sb_net_link_tick(sb_net_link_t *l,uint64_t now) {
    if(!l || l->failed || now<l->now) return;
    l->now=now;
    bool missing=l->pending_sent && now>=l->retry_at;
    if(!l->recovering && (missing || now-l->last_rx>=l->profile.silence_ms)) { l->recovering=true; l->recovery_at=now; }
    if(l->recovering && now-l->recovery_at>=l->profile.grace_ms) l->failed=true;
}
bool sb_net_poll(sb_net_link_t *l,uint64_t now,sb_net_packet_t *out) {
    if(!l || !out) return false;
    sb_net_link_tick(l,now);
    if(l->failed || l->inflight) return false;
    /* A new semantic response carries the receipt; do not send a redundant ACK. */
    if(l->count && !l->pending.size) {
        if(!seal(l,&l->pending,&l->queue[l->head])) return false;
        *out=l->pending;
    } else if(l->ack_due) {
        sb_net_message_t ack={.type=SB_N_ACK}; if(!seal(l,out,&ack)) return false;
    } else if(l->count && (!l->pending_sent || now>=l->retry_at)) *out=l->pending;
    else return false;
    l->flight=*out; l->inflight=true; return true;
}
void sb_net_tx_complete(sb_net_link_t *l,const sb_net_packet_t *p,uint64_t now,bool sent) {
    if(!l || !p || !l->inflight || p->size!=l->flight.size || memcmp(p->bytes,l->flight.bytes,p->size)) return;
    l->inflight=false;
    if(!sent) return; /* Queue rejection is not a transmission or a lost ACK. */
    if(sb_net_get32(p->bytes+21)==l->received_command) l->ack_due=false;
    if(l->count && l->pending.size==p->size && !memcmp(p->bytes,l->pending.bytes,p->size)) {
        l->pending_sent=true;
        /* Profile bounds cover maximum-frame pacing; jitter breaks symmetric retries. */
        uint32_t jitter=0; (void)sb_net_random(&jitter,sizeof(jitter));
        l->retry_at=now+l->profile.retry_ms+(l->role ? (sb_net_airtime_us(253)+999)/1000+150 : 0)+(jitter%101);
    }
}
