// SPDX-License-Identifier: MIT
#pragma once
#include "net/wire.h"
typedef struct {
    unsigned role; bool derived, failed;
    char host_name[17];
    sb_spec_t spec; sb_net_key_t key;
    uint8_t nonce[16], resume_key[32], transcript[32], spec_hash[32], material[72];
    sb_net_packet_t advertisement, join, hello, reply;
    bool bound, reply_sent;
    uint64_t next_send, next_tx, began;
    sb_net_profile_t profile;
} sb_net_pairing_t;
bool sb_net_pair_begin(sb_net_pairing_t *,unsigned role,const sb_spec_t *host_spec,const sb_net_profile_t *,uint64_t now);
bool sb_net_pair_set_host_name(sb_net_pairing_t *,const char *name);
bool sb_net_pair_receive(sb_net_pairing_t *,const sb_net_packet_t *);
bool sb_net_pair_poll(sb_net_pairing_t *,uint64_t now,sb_net_packet_t *);
unsigned sb_net_pair_code(const sb_net_pairing_t *);
bool sb_net_pair_link(const sb_net_pairing_t *,sb_net_link_t *,uint64_t now);

/* Mutual authenticated rematch nonces replace a new ECDH/code comparison. */
bool sb_net_pair_rematch(sb_net_pairing_t *,const uint8_t nonces[2][16]);
