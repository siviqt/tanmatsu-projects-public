// SPDX-License-Identifier: MIT
#pragma once
#include "net/pairing.h"
typedef enum {
    SB_NET_VERIFY, SB_NET_DEPLOY, SB_NET_WAIT_DEPLOY, SB_NET_SELECT,
    SB_NET_EXCHANGE, SB_NET_FINISHED, SB_NET_ABANDONED
} sb_net_phase_t;
/* Application-task owned. Only role 0 mutates match. Role 1 receives a
 * projection: its own fleet plus public knowledge, never the host fleet. */
typedef struct {
    sb_net_link_t link;
    sb_match_t match;
    sb_spec_t spec; sb_net_phase_t phase;
    sb_fleet_t fleet; sb_knowledge_t knowledge[2]; sb_record_t record;
    uint8_t transcript[32], digest[16], rematch_nonce[2][16];
    unsigned role, actor; uint32_t sequence, revision;
    bool confirmed[2], deployed[2], committed[2], rematch[2];
    uint8_t targets[2][5], target_count[2], allowance[2];
    uint64_t now, last_ping, response_due;
    bool surrender_sent, peer_left, leaving;
    uint32_t leave_command; // Ordered reliable departure, queued once capacity is available.
    bool ready_sent, started, state_dirty, rematch_ready, local_deployed;
    sb_outcome_t outcome;
    sb_end_reason_t end_reason;
    const char *error;
} sb_net_session_t;
bool sb_net_session_init(sb_net_session_t *,const sb_net_pairing_t *,uint64_t now);
bool sb_net_confirm(sb_net_session_t *);
bool sb_net_place(sb_net_session_t *,unsigned ship,unsigned x,unsigned y,bool vertical);
bool sb_net_deploy(sb_net_session_t *);
bool sb_net_targets(sb_net_session_t *,const uint8_t *,size_t count);
bool sb_net_lock(sb_net_session_t *);
bool sb_net_can_act(const sb_net_session_t *);
bool sb_net_can_surrender(const sb_net_session_t *);
bool sb_net_surrender(sb_net_session_t *);
bool sb_net_rematch(sb_net_session_t *);
/* Stop new gameplay/health commands and drain an ordered LEAVE, including after
 * the match. Caller continues RX/TX until its receipt or a bounded deadline. */
bool sb_net_leave(sb_net_session_t *);
/* During rematch grace, only an authenticated old-session LEAVE may close the
 * new session. Old receipts/retries still use their original link and keys. */
bool sb_net_session_receive_previous(sb_net_session_t *,sb_net_link_t *,const sb_net_packet_t *,uint64_t now);
void sb_net_abandon(sb_net_session_t *,const char *reason);
void sb_net_close(sb_net_session_t *);
void sb_net_tick(sb_net_session_t *,uint64_t now);
bool sb_net_session_receive(sb_net_session_t *,const sb_net_packet_t *,uint64_t now);
bool sb_net_session_poll(sb_net_session_t *,uint64_t now,sb_net_packet_t *);
