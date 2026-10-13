// SPDX-License-Identifier: MIT
#include "net/session.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "multiplayer-vectors.h"
static const sb_net_profile_t profile={.max_packet=253,.duty_permille=90,.retry_ms=10000,
    .heartbeat_ms=30000,.silence_ms=90000,.grace_ms=30000,.validated=true};
static sb_net_pairing_t pairs[2];
static sb_net_session_t peers[2];
static void pair(sb_ruleset_t rules) {
    sb_spec_t spec; assert(sb_spec_make(&spec,rules,SB_VERSUS,0));
    assert(!spec.deployment_ms && !spec.selection_ms && !spec.match_ms);
    assert(sb_net_pair_begin(&pairs[0],0,&spec,&profile,0));
    assert(sb_net_pair_set_host_name(&pairs[0],"Test host"));
    assert(sb_net_pair_begin(&pairs[1],1,NULL,&profile,0));
    assert(sb_net_pair_receive(&pairs[1],&pairs[0].advertisement));
    assert(sb_net_pair_receive(&pairs[0],&pairs[1].join));
    assert(sb_net_pair_receive(&pairs[1],&pairs[0].hello));
    assert(sb_net_pair_receive(&pairs[0],&pairs[1].reply));
    assert(pairs[0].derived && pairs[1].derived && sb_net_pair_code(&pairs[0])==sb_net_pair_code(&pairs[1]));
    assert(!memcmp(pairs[0].material,pairs[1].material,72));
    assert(!memcmp(pairs[0].resume_key,pairs[1].resume_key,32));
    for(unsigned p=0;p<2;p++) { assert(sb_net_session_init(&peers[p],&pairs[p],0)); assert(sb_net_confirm(&peers[p])); }
}
typedef struct { sb_net_packet_t packet; uint64_t end,next; bool queued,on_air,lost; } radio_t;
static radio_t radios[2];
static uint64_t now;
static unsigned transmissions,drops,duplicates,collisions;
static bool lossy;
static unsigned drop_leaves;
static void network(void) {
    for(unsigned p=0;p<2;p++) if(radios[p].on_air && now>=radios[p].end) {
        radio_t *r=&radios[p]; r->on_air=false; r->queued=false;
        sb_net_tx_complete(&peers[p].link,&r->packet,now,true);
        if(r->lost) ++drops;
        else {
            (void)sb_net_session_receive(&peers[1-p],&r->packet,now);
            if(lossy && transmissions%7==0) { (void)sb_net_session_receive(&peers[1-p],&r->packet,now); ++duplicates; }
        }
    }
    for(unsigned p=0;p<2;p++) {
        sb_net_tick(&peers[p],now);
        if(!radios[p].queued && sb_net_session_poll(&peers[p],now,&radios[p].packet)) radios[p].queued=true;
    }
    for(unsigned p=0;p<2;p++) {
        radio_t *r=&radios[p],*other=&radios[1-p];
        if(!r->queued || r->on_air || now<r->next) continue;
        r->on_air=true; ++transmissions;
        r->end=now+(sb_net_airtime_us(r->packet.size)+999)/1000;
        r->next=now+(sb_net_airtime_us(r->packet.size)+89)/90;
        r->lost=lossy && transmissions%13==0;
        if(drop_leaves && r->packet.bytes[3]==SB_N_LEAVE) { --drop_leaves; r->lost=true; }
        if(other->on_air) { r->lost=other->lost=true; ++collisions; }
    }
}
static bool drained(void) {
    for(unsigned p=0;p<2;p++) if(peers[p].link.count || peers[p].link.ack_due || peers[p].link.inflight || peers[p].state_dirty || radios[p].queued) return false;
    return true;
}
static void input(unsigned p) {
    sb_net_session_t *s=&peers[p];
    if(s->phase==SB_NET_DEPLOY) {
        for(unsigned i=0;i<5;i++) assert(sb_net_place(s,i,0,i,false));
        assert(sb_net_deploy(s));
    }
    if(sb_net_can_act(s)) {
        uint8_t cells[5]; unsigned n=0;
        for(unsigned c=0;c<100 && n<s->allowance[p];c++) if(s->knowledge[p].cells[c]==SB_UNKNOWN) cells[n++]=(uint8_t)c;
        if(s->spec.rules.id==SB_ESCALATED && s->sequence==1) n=0; /* Explicit empty salvos. */
        assert(sb_net_targets(s,cells,n)); assert(sb_net_lock(s));
    }
}
static void full(sb_ruleset_t rules,bool loss) {
    pair(rules); memset(radios,0,sizeof(radios)); now=0; lossy=loss; transmissions=drops=duplicates=collisions=0;
    for(;now<3000000;now+=10) {
        network();
        for(unsigned p=0;p<2;p++) {
            if(peers[p].phase==SB_NET_ABANDONED) fprintf(stderr,"abandoned role%u phase%u seq%u now%llu error%s\n",p,peers[p].phase,peers[p].sequence,(unsigned long long)now,peers[p].error);
            assert(peers[p].phase!=SB_NET_ABANDONED); input(p);
        }
        if(peers[0].phase==SB_NET_FINISHED && peers[1].phase==SB_NET_FINISHED && drained()) break;
    }
    assert(now<3000000); assert(peers[0].outcome==peers[1].outcome);
    assert(peers[0].end_reason==SB_END_COMBAT && peers[1].end_reason==SB_END_COMBAT);
    if(rules==SB_CLASSIC) {
        assert(sb_result_for(peers[0].outcome,peers[0].end_reason,0)==SB_RESULT_VICTORY);
        assert(sb_result_for(peers[1].outcome,peers[1].end_reason,1)==SB_RESULT_DEFEAT);
    }
    assert(peers[0].outcome==(rules==SB_CLASSIC ? SB_WIN_A : SB_DRAW));
    assert(!memcmp(peers[0].knowledge,peers[1].knowledge,sizeof(peers[0].knowledge)));
    assert(!memcmp(&peers[0].match.fleets[1],&peers[1].fleet,sizeof(sb_fleet_t)));
    sb_match_t zero={0}; assert(!memcmp(&peers[1].match,&zero,sizeof(zero))); /* No hidden host state. */
    printf("Authoritative %s %s: %u actions/rounds, %llu ms, %u frames/%u losses/%u duplicates/%u collisions\n",
        rules==SB_CLASSIC ? "Classic" : "Escalated",loss ? "lossy" : "clean",peers[0].sequence,(unsigned long long)now,transmissions,drops,duplicates,collisions);
    uint8_t old_material[72],old_id[16]; memcpy(old_material,pairs[0].material,72); memcpy(old_id,peers[0].link.session,16);
    assert(sb_net_rematch(&peers[0]) && sb_net_rematch(&peers[1]));
    uint64_t end=now+120000;
    for(;now<end;now+=10) { network(); if(peers[0].rematch_ready && peers[1].rematch_ready && drained()) break; }
    assert(now<end); assert(!memcmp(peers[0].rematch_nonce,peers[1].rematch_nonce,32));
    for(unsigned p=0;p<2;p++) assert(sb_net_pair_rematch(&pairs[p],(const uint8_t (*)[16])peers[p].rematch_nonce));
    assert(!memcmp(pairs[0].material,pairs[1].material,72)); assert(memcmp(old_material,pairs[0].material,72));
    assert(memcmp(old_id,pairs[0].transcript,16)); assert(pairs[0].spec.starter==(rules==SB_CLASSIC ? 1 : 0));
}
static unsigned deliveries;
static bool receive(void *ctx,const sb_net_message_t *m,uint64_t when) { (void)ctx;(void)m;(void)when;++deliveries;return true; }
static void transport(void) {
    pair(SB_CLASSIC); sb_net_link_t a,b; uint8_t material[72]; memcpy(material,pairs[0].material,72);
    assert(sb_net_link_init(&a,0,pairs[0].transcript,pairs[0].spec_hash,material,&profile,0));
    assert(sb_net_link_init(&b,1,pairs[0].transcript,pairs[0].spec_hash,material,&profile,0));
    uint8_t cell=3; assert(sb_net_send(&a,SB_N_ACT,1,&cell,1));
    sb_net_packet_t packet; assert(sb_net_poll(&a,0,&packet)); assert(packet.size==47);
    sb_net_link_tick(&a,20000); assert(!a.pending_sent && !a.recovering); /* Queued, not transmitted. */
    sb_net_tx_complete(&a,&packet,20000,false); assert(sb_net_poll(&a,20000,&packet));
    sb_net_tx_complete(&a,&packet,20000,true); deliveries=0;
    for(size_t i=0;i<packet.size;i++) { sb_net_packet_t bad=packet; bad.bytes[i]^=1; assert(!sb_net_receive(&b,&bad,20000,receive,NULL)); }
    for(size_t i=0;i<packet.size;i++) { sb_net_packet_t bad=packet;bad.size=i;assert(!sb_net_receive(&b,&bad,20000,receive,NULL)); }
    assert(!deliveries); assert(sb_net_receive(&b,&packet,20000,receive,NULL)); assert(deliveries==1);
    assert(sb_net_receive(&b,&packet,21000,receive,NULL) && deliveries==1 && b.last_rx==20000);
    sb_net_packet_t retry; assert(sb_net_poll(&a,31000,&retry)); assert(retry.size==packet.size && !memcmp(retry.bytes,packet.bytes,packet.size));
    sb_net_tx_complete(&a,&retry,31000,true);
    assert(sb_net_send(&b,SB_N_STATE,2,&cell,1)); sb_net_packet_t response; assert(sb_net_poll(&b,31000,&response));
    assert(response.bytes[3]==SB_N_STATE && sb_net_get32(response.bytes+21)==1); /* Piggyback, no standalone ACK. */
    sb_net_tx_complete(&b,&response,31000,true); assert(sb_net_receive(&a,&response,31000,receive,NULL)); assert(!a.count);
    sb_net_packet_t ack; assert(sb_net_poll(&a,31000,&ack)); sb_net_tx_complete(&a,&ack,31000,true);
    assert(sb_net_receive(&b,&ack,31000,receive,NULL)); assert(!b.count);
    /* The worker can publish TX_DONE between the application's two queue drains. */
    assert(sb_net_send(&a,SB_N_ACT,3,&cell,1));
    sb_net_packet_t racing; assert(sb_net_poll(&a,31000,&racing));
    assert(sb_net_receive(&b,&racing,31000,receive,NULL));
    assert(sb_net_poll(&b,31000,&ack)); sb_net_tx_complete(&b,&ack,31000,true);
    assert(sb_net_receive(&a,&ack,31000,receive,NULL) && !a.count);
    sb_net_tx_complete(&a,&racing,31000,true); assert(!a.inflight && !a.pending_sent);
    /* Future commands are rejected without consuming the authenticated counter. */
    sb_net_link_t future_a,future_b;
    assert(sb_net_link_init(&future_a,0,pairs[0].transcript,pairs[0].spec_hash,material,&profile,0));
    assert(sb_net_link_init(&future_b,1,pairs[0].transcript,pairs[0].spec_hash,material,&profile,0));
    future_a.next_command=2;
    assert(sb_net_send(&future_a,SB_N_ACT,1,&cell,1)); assert(sb_net_poll(&future_a,0,&retry));
    assert(!sb_net_receive(&future_b,&retry,0,receive,NULL) && !future_b.rx_high);
    future_a.queue[0].command=1; future_a.inflight=false; future_a.pending.size=0;
    assert(sb_net_poll(&future_a,0,&retry)); assert(sb_net_receive(&future_b,&retry,0,receive,NULL));
    future_a.queue[0].data[0]=4; future_a.inflight=false; future_a.pending.size=0;
    assert(sb_net_poll(&future_a,0,&retry)); assert(!sb_net_receive(&future_b,&retry,0,receive,NULL));
    sb_net_link_t fresh; uint8_t other[16]={1}; assert(sb_net_link_init(&fresh,1,other,pairs[0].spec_hash,material,&profile,0));
    assert(!sb_net_receive(&fresh,&packet,31000,receive,NULL));
    a.tx_counter=UINT32_MAX; a.ack_due=true; assert(!sb_net_poll(&a,31000,&ack) && a.failed);
    puts("Transport: tampering, truncation, duplicate, exact retries, actual TX clocks, piggyback receipts, stale session and counter exhaustion passed");
}
static void failures(void) {
    /* Human code comparison can take longer than the silence timeout. */
    pair(SB_CLASSIC);
    for(unsigned p=0;p<2;p++) assert(sb_net_session_init(&peers[p],&pairs[p],0));
    memset(radios,0,sizeof(radios)); lossy=false; now=0;
    for(;now<180000;now+=10) network();
    assert(peers[0].phase==SB_NET_VERIFY && peers[1].phase==SB_NET_VERIFY);
    assert(sb_net_confirm(&peers[1]) && sb_net_confirm(&peers[0]));
    uint64_t verified_by=now+120000;
    for(;now<verified_by;now+=10) { network(); if(peers[0].phase==SB_NET_DEPLOY && peers[1].phase==SB_NET_DEPLOY && drained()) break; }
    assert(now<verified_by);
    pair(SB_ESCALATED); memset(radios,0,sizeof(radios)); lossy=false; now=0;
    for(;now<120000;now+=10) { network(); if(peers[0].phase==SB_NET_DEPLOY && peers[1].phase==SB_NET_DEPLOY && drained()) break; }
    assert(now<120000);
    uint64_t end=now+180000; for(;now<end;now+=10) network();
    assert(peers[0].phase==SB_NET_DEPLOY && peers[1].phase==SB_NET_DEPLOY); /* No deployment deadline. */
    input(0); input(1);
    end=now+120000; for(;now<end;now+=10) { network(); if(peers[0].sequence==1 && peers[1].sequence==1 && drained()) break; }
    assert(now<end);
    assert(sb_net_targets(&peers[1],NULL,0)); assert(sb_net_lock(&peers[1]));
    end=now+180000; for(;now<end;now+=10) network();
    assert(peers[0].sequence==1 && peers[1].sequence==1 && peers[1].phase==SB_NET_EXCHANGE);
    assert(sb_net_targets(&peers[0],NULL,0)); assert(sb_net_lock(&peers[0]));
    end=now+120000; for(;now<end;now+=10) { network(); if(peers[1].sequence==2 && drained()) break; }
    assert(now<end);
    sb_match_t before=peers[0].match;
    uint8_t bad[]={2,100,101}; assert(sb_net_send(&peers[1].link,SB_N_ACT,2,bad,sizeof(bad)));
    end=now+60000; for(;now<end;now+=10) network();
    assert(!memcmp(&before,&peers[0].match,sizeof(before)));
    assert(peers[1].phase==SB_NET_ABANDONED); /* Semantic rejection does not renew a fixed incident. */
    /* Surrender crossing an in-flight guest salvo must release the cancelled command. */
    pair(SB_ESCALATED); memset(radios,0,sizeof(radios)); now=0; lossy=false;
    for(;now<120000;now+=10) {
        network();
        for(unsigned p=0;p<2;p++) if(peers[p].phase==SB_NET_DEPLOY) {
            for(unsigned ship=0;ship<5;ship++) assert(sb_net_place(&peers[p],ship,0,ship,false));
            assert(sb_net_deploy(&peers[p]));
        }
        if(peers[1].sequence==1 && drained()) break;
    }
    assert(now<120000);
    assert(sb_net_targets(&peers[1],NULL,0) && sb_net_lock(&peers[1]));
    assert(sb_net_surrender(&peers[0]));
    end=now+120000; for(;now<end;now+=10) { network(); if(peers[1].phase==SB_NET_FINISHED && drained()) break; }
    assert(now<end && peers[0].outcome==SB_WIN_B && peers[1].outcome==SB_WIN_B);
    assert(peers[0].end_reason==SB_END_SURRENDER_A && peers[1].end_reason==SB_END_SURRENDER_A);
    assert(peers[1].record.end_reason==SB_END_SURRENDER_A);
    pair(SB_CLASSIC); sb_net_tick(&peers[0],90000); sb_net_tick(&peers[0],120000);
    assert(peers[0].phase==SB_NET_ABANDONED && peers[0].outcome==SB_ABANDONED);
    puts("Untimed deployment/thinking, early empty lock, malformed action, bounded failure without timeout victory passed");
}
static void leave_contract(void) {
    for(unsigned sender=0;sender<2;sender++) for(unsigned stage=0;stage<3;stage++) {
        pair(SB_CLASSIC); memset(radios,0,sizeof(radios)); now=0; lossy=false;
        if(!stage) {
            for(unsigned p=0;p<2;p++) assert(sb_net_session_init(&peers[p],&pairs[p],0));
        } else {
            for(;now<120000;now+=10) { network(); if(peers[0].phase==SB_NET_DEPLOY && peers[1].phase==SB_NET_DEPLOY && drained()) break; }
            assert(now<120000);
            if(stage==2) {
                assert(sb_net_surrender(&peers[0]));
                uint64_t end=now+120000;
                for(;now<end;now+=10) { network(); if(peers[0].phase==SB_NET_FINISHED && peers[1].phase==SB_NET_FINISHED && drained()) break; }
                assert(now<end);
            }
        }
        unsigned receiver=1-sender;
        sb_outcome_t previous=peers[receiver].outcome;
        assert(sb_net_send(&peers[sender].link,SB_N_LEAVE,peers[sender].sequence,NULL,0));
        uint64_t end=now+120000;
        for(;now<end;now+=10) { network(); if(peers[receiver].peer_left) break; }
        assert(now<end && !sb_net_rematch(&peers[receiver]));
        sb_outcome_t expected=stage==0 ? SB_ABANDONED : stage==2 ? previous : receiver ? SB_WIN_B : SB_WIN_A;
        assert(peers[receiver].outcome==expected);
        if(stage==1) assert(!strcmp(peers[receiver].error,"Opponent has abandoned"));
        const char *reason=peers[receiver].error;
        sb_net_tick(&peers[receiver],now+1000000);
        assert(peers[receiver].outcome==expected && peers[receiver].error==reason);
    }
    puts("LEAVE: both roles, pairing cancellation, immediate forfeit, completed-result retention and persistent reason passed");
}
static void reliable_exit(void) {
    for(unsigned sender=0;sender<2;sender++) for(unsigned stage=0;stage<3;stage++) {
        pair(SB_CLASSIC); memset(radios,0,sizeof(radios)); now=0; lossy=false;
        if(stage==0) {
            for(unsigned p=0;p<2;p++) assert(sb_net_session_init(&peers[p],&pairs[p],0));
        } else {
            for(;now<120000;now+=10) { network(); if(drained() && peers[0].started && peers[1].started) break; }
            assert(now<120000);
        }
        // Saturate the ordered queue; LEAVE must wait rather than skip command IDs.
        uint8_t status[20]={0};
        assert(sb_net_send(&peers[sender].link,SB_N_STATUS,peers[sender].sequence,status,sizeof(status)));
        assert(sb_net_send(&peers[sender].link,SB_N_STATUS,peers[sender].sequence,status,sizeof(status)));
        if(stage==2) {
            assert(sb_net_surrender(&peers[0])); // Final snapshot has not been sent yet.
            if(sender==1) {
                uint64_t end=now+120000;
                for(;now<end;now+=10) { network(); if(peers[1].phase==SB_NET_FINISHED && drained()) break; }
                assert(now<end);
            }
        }
        assert(sb_net_leave(&peers[sender]));
        assert(!sb_net_can_act(&peers[sender]) && !sb_net_rematch(&peers[sender]));
        drop_leaves=1; uint64_t end=now+profile.grace_ms;
        for(;now<end;now+=10) {
            network();
            if(peers[1-sender].peer_left && peers[sender].leave_command &&
               !peers[sender].link.count && !peers[sender].link.inflight) break;
        }
        assert(now<end && !drop_leaves);
        assert(peers[1-sender].peer_left && !peers[sender].link.count);
        if(stage==2) assert(peers[1-sender].outcome==SB_WIN_B && peers[1-sender].end_reason==SB_END_SURRENDER_A);
    }
    // A departure can cross a completed rematch key handoff. Authenticate it
    // with the previous link, acknowledge there, and close the new session.
    for(unsigned sender=0;sender<2;sender++) {
        pair(SB_CLASSIC); memset(radios,0,sizeof(radios)); now=0; lossy=false;
        for(;now<120000;now+=10) { network(); if(drained() && peers[0].started && peers[1].started) break; }
        assert(now<120000);
        unsigned receiver=1-sender; sb_net_link_t previous=peers[receiver].link;
        uint8_t nonces[2][16]={{1},{2}};
        assert(sb_net_pair_rematch(&pairs[receiver],(const uint8_t (*)[16])nonces));
        assert(sb_net_session_init(&peers[receiver],&pairs[receiver],now));
        assert(sb_net_leave(&peers[sender]));
        sb_net_packet_t packet,ack; assert(sb_net_session_poll(&peers[sender],now,&packet));
        assert(packet.bytes[3]==SB_N_LEAVE);
        sb_net_tx_complete(&peers[sender].link,&packet,now,true);
        sb_net_packet_t forged=packet; forged.bytes[forged.size-1]^=1;
        assert(!sb_net_session_receive_previous(&peers[receiver],&previous,&forged,now));
        assert(!peers[receiver].peer_left);
        assert(!sb_net_session_receive(&peers[receiver],&packet,now));
        assert(sb_net_session_receive_previous(&peers[receiver],&previous,&packet,now));
        assert(peers[receiver].peer_left && peers[receiver].phase==SB_NET_ABANDONED);
        assert(sb_net_poll(&previous,now,&ack));
        assert(sb_net_session_receive(&peers[sender],&ack,now));
        assert(!peers[sender].link.count);
    }
    puts("Reliable exit: both roles, full queue, lost LEAVE, final snapshot ordering, verification ACK and authenticated rematch handoff passed");
}
static void classic_selection(void) {
    for(unsigned role=0;role<2;role++) {
        sb_net_session_t s={.role=role,.phase=SB_NET_SELECT,.actor=role};
        assert(sb_spec_make(&s.spec,SB_CLASSIC,SB_VERSUS,role));
        s.allowance[role]=1;
        uint8_t c=44;
        assert(sb_net_targets(&s,&c,1) && s.target_count[role]==1);
        assert(sb_net_targets(&s,NULL,0) && !s.target_count[role]);
        assert(!sb_net_lock(&s) && !s.committed[role] && !s.link.count);
    }
}
static void crypto_tests(void) {
    uint8_t digest[32],derived[72],encoded[24]; sb_spec_t spec;
    assert(sb_net_hash(vector_transcript,sizeof(vector_transcript),digest)); assert(!memcmp(digest,vector_transcript_hash,32));
    assert(sb_net_kdf(vector_shared,digest,derived)); assert(!memcmp(derived,vector_material,72));
    assert(sb_net_spec_decode(vector_spec,&spec) && sb_net_spec_encode(&spec,encoded)); assert(!memcmp(encoded,vector_spec,24));
    uint8_t old_spec[24]; memcpy(old_spec,vector_spec,24); old_spec[0]=3;
    assert(!sb_net_spec_decode(old_spec,&spec));
    for(unsigned i=0;i<24;i++) { uint8_t bad[24]; memcpy(bad,vector_spec,24); bad[i]^=0x80; assert(!sb_net_spec_decode(bad,&spec)); }
    sb_net_key_t a,b; uint8_t ab[32],ba[32];
    assert(sb_net_keygen(&a) && sb_net_keygen(&b)); assert(sb_net_shared(&a,b.public_key,ab) && sb_net_shared(&b,a.public_key,ba));
    assert(!memcmp(ab,ba,32)); assert(!sb_net_shared(&a,(uint8_t[65]){0},ab));
    sb_net_wipe(&a,sizeof(a)); sb_net_wipe(&b,sizeof(b));
    uint8_t key[16]={0},nonce[12]={0},plain[16]={0},cipher[16],tag[16],out[16];
    const uint8_t expected[]={0x03,0x88,0xda,0xce,0x60,0xb6,0xa3,0x92,0xf3,0x28,0xc2,0xb9,0x71,0xb2,0xfe,0x78};
    const uint8_t expected_tag[]={0xab,0x6e,0x47,0xd4,0x2c,0xec,0x13,0xbd,0xf5,0x3a,0x67,0xb2,0x12,0x57,0xbd,0xdf};
    assert(sb_net_aead(true,key,nonce,NULL,0,plain,16,cipher,tag));
    assert(!memcmp(cipher,expected,16) && !memcmp(tag,expected_tag,16));
    assert(sb_net_aead(false,key,nonce,NULL,0,cipher,16,out,tag)); assert(!memcmp(out,plain,16));
    tag[0]^=1; assert(!sb_net_aead(false,key,nonce,NULL,0,cipher,16,out,tag));
    sb_net_profile_t bench=profile; bench.duty_permille=9; assert(!sb_net_profile_valid(&bench));
    bench=profile; bench.validated=false; assert(!sb_net_profile_valid(&bench));
    assert(sb_net_airtime_us(253)==394496);
}
int main(void) { crypto_tests(); leave_contract(); reliable_exit(); classic_selection(); transport(); full(SB_CLASSIC,false); full(SB_ESCALATED,false); full(SB_CLASSIC,true); full(SB_ESCALATED,true); failures(); return 0; }
