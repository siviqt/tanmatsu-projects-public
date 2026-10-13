// SPDX-License-Identifier: MIT
#include "net/pairing.h"
#include <string.h>
bool sb_net_pair_begin(sb_net_pairing_t *p,unsigned role,const sb_spec_t *spec,const sb_net_profile_t *profile,uint64_t now) {
    if(!p || role>1 || !sb_net_profile_valid(profile)) return false;
    *p=(sb_net_pairing_t){.role=role,.profile=*profile,.began=now};
    if(!sb_net_keygen(&p->key) || !sb_net_random(p->nonce,16)) { p->failed=true; return false; }
    if(role==0) {
        if(!spec || !sb_net_spec_encode(spec,p->hello.bytes+4)) { p->failed=true; return false; }
        p->spec=*spec; memcpy(p->hello.bytes,"SBH\2",4); memcpy(p->hello.bytes+28,p->key.public_key,65);
        memcpy(p->hello.bytes+93,p->nonce,16); p->hello.size=109;
        p->advertisement.size=77; memcpy(p->advertisement.bytes,"SBA\2",4); memcpy(p->advertisement.bytes+4,p->hello.bytes+4,24);
        if(!sb_net_hash(p->hello.bytes,109,p->advertisement.bytes+28)) { p->failed=true; return false; }
    }
    if(role==0) return sb_net_pair_set_host_name(p,"Host");
    return true;
}
bool sb_net_pair_set_host_name(sb_net_pairing_t *p,const char *name) {
    if(!p || !name || p->role || p->bound || p->next_send || p->derived) return false;
    size_t n=strlen(name); if(!n || n>16) return false;
    for(size_t i=0;i<n;i++) if(!((name[i]>='A' && name[i]<='Z') || (name[i]>='a' && name[i]<='z') ||
        (name[i]>='0' && name[i]<='9') || name[i]==' ' || name[i]=='_' || name[i]=='-')) return false;
    memset(p->host_name,0,17); memcpy(p->host_name,name,n);
    p->advertisement.bytes[60]=(uint8_t)n; memset(p->advertisement.bytes+61,0,16); memcpy(p->advertisement.bytes+61,name,n); return true;
}
static bool same(const sb_net_packet_t *a,const sb_net_packet_t *b) { return a->size==b->size && !memcmp(a->bytes,b->bytes,a->size); }
static bool derive(sb_net_pairing_t *p,const uint8_t peer[65]) {
    uint8_t transcript[371], shared[32];
    memcpy(transcript,p->advertisement.bytes,77); memcpy(transcript+77,p->join.bytes,68);
    memcpy(transcript+145,p->hello.bytes,109); memcpy(transcript+254,p->reply.bytes,117);
    bool ok=sb_net_hash(transcript,sizeof(transcript),p->transcript) && sb_net_shared(&p->key,peer,shared) &&
        sb_net_kdf(shared,p->transcript,p->material) && sb_net_hash(p->hello.bytes+4,24,p->spec_hash);
    uint8_t domain[41], root_hash[32], root_material[72];
    memcpy(domain,p->transcript,32); memcpy(domain+32,"resume/v2",9);
    ok=ok && sb_net_hash(domain,sizeof(domain),root_hash) && sb_net_kdf(shared,root_hash,root_material);
    if(ok) memcpy(p->resume_key,root_material,32);
    sb_net_wipe(root_material,sizeof(root_material));
    sb_net_wipe(shared,sizeof(shared)); sb_net_wipe(&p->key,sizeof(p->key));
    p->derived=ok; p->failed=!ok;
    if(ok) p->next_send=p->next_tx;
    return ok;
}
bool sb_net_pair_receive(sb_net_pairing_t *p,const sb_net_packet_t *packet) {
    if(!p || !packet || p->failed || packet->size>253) return false;
    if(p->derived) return same(packet,p->role ? &p->hello : &p->reply);
    const uint8_t *b=packet->bytes; uint8_t hash[32];
    if(p->role==1) {
        if(!p->bound) {
            sb_spec_t s;
            if(packet->size!=77 || memcmp(b,"SBA\2",4) || !sb_net_spec_decode(b+4,&s)) return false;
            unsigned length=b[60]; if(!length || length>16) return false;
            for(unsigned i=0;i<16;i++) {
                uint8_t c=b[61+i];
                if(i>=length ? c!=0 : !((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c==' ' || c=='_' || c=='-')) return false;
            }
            memcpy(p->host_name,b+61,16); p->host_name[16]=0;
            p->advertisement=*packet; p->spec=s;
            p->reply.size=117; memcpy(p->reply.bytes,"SBJ\2",4);
            if(!sb_net_hash(b+4,24,p->reply.bytes+4)) return false;
            memcpy(p->reply.bytes+36,p->key.public_key,65); memcpy(p->reply.bytes+101,p->nonce,16);
            p->join.size=68; memcpy(p->join.bytes,"SBC\2",4);
            if(!sb_net_hash(b,77,p->join.bytes+4) || !sb_net_hash(p->reply.bytes,117,p->join.bytes+36)) return false;
            p->bound=true; p->next_send=p->next_tx; return true;
        }
        if(same(packet,&p->advertisement)) return true;
        if(packet->size!=109 || memcmp(b,"SBH\2",4) || memcmp(b+4,p->advertisement.bytes+4,24) ||
           !sb_net_hash(b,109,hash) || !sb_net_equal(hash,p->advertisement.bytes+28,32)) return false;
        p->hello=*packet; return derive(p,b+28);
    }
    if(!p->bound) {
        if(packet->size!=68 || memcmp(b,"SBC\2",4) || !sb_net_hash(p->advertisement.bytes,77,hash) || !sb_net_equal(hash,b+4,32)) return false;
        p->join=*packet; p->bound=true; p->next_send=p->next_tx; return true;
    }
    if(same(packet,&p->join)) return true;
    if(packet->size!=117 || memcmp(b,"SBJ\2",4) || !sb_net_hash(b,117,hash) || !sb_net_equal(hash,p->join.bytes+36,32) ||
       !sb_net_hash(p->hello.bytes+4,24,hash) || !sb_net_equal(hash,b+4,32)) return false;
    p->reply=*packet; return derive(p,b+36);
}
bool sb_net_pair_poll(sb_net_pairing_t *p,uint64_t now,sb_net_packet_t *out) {
    if(!p || !out || p->failed) return false;
    /* The guest reveal proves delivery of the host reveal. No repeat needed. */
    if(!p->role && p->derived) return false;
    if(now>=p->began && now-p->began>=300000) { p->failed=true; return false; }
    if(now<p->next_send) return false;
    const sb_net_packet_t *packet=p->role ? (p->derived ? &p->reply : &p->join) : (p->bound ? &p->hello : &p->advertisement);
    if(!packet->size) return false;
    *out=*packet;
    if(p->role && p->derived) p->reply_sent=true;
    uint64_t spacing=(sb_net_airtime_us(out->size)+p->profile.duty_permille-1)/p->profile.duty_permille;
    p->next_tx=now+spacing;
    p->next_send=now+(spacing>p->profile.retry_ms ? spacing : p->profile.retry_ms); return true;
}
unsigned sb_net_pair_code(const sb_net_pairing_t *p) { return p && p->derived ? sb_net_get32(p->material+40)%1000000 : 0; }
bool sb_net_pair_link(const sb_net_pairing_t *p,sb_net_link_t *l,uint64_t now) {
    return p && p->derived && !p->failed && sb_net_link_init(l,p->role,p->transcript,p->spec_hash,p->material,&p->profile,now);
}

bool sb_net_pair_rematch(sb_net_pairing_t *p,const uint8_t nonces[2][16]) {
    if(!p || !p->derived || p->failed || !nonces) return false;
    if(p->spec.rules.id==SB_CLASSIC) p->spec.starter=1-p->spec.starter;
    uint8_t b[98]; memcpy(b,"rematch/v2",10); memcpy(b+10,p->transcript,32);
    memcpy(b+42,nonces,32);
    if(!sb_net_spec_encode(&p->spec,b+74) || !sb_net_hash(b,sizeof(b),p->transcript) ||
       !sb_net_kdf(p->resume_key,p->transcript,p->material) || !sb_net_hash(b+74,24,p->spec_hash)) return false;
    p->reply_sent=true; return true;
}
