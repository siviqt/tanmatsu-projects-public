// SPDX-License-Identifier: MIT
#pragma once
#include "game/game.h"
#include "net/crypto.h"
#define SB_NET_PACKET 253
#define SB_NET_HEADER 30
#define SB_NET_PAYLOAD (SB_NET_PACKET-SB_NET_HEADER-16)
#define SB_NET_SPEC 24
#define SB_NET_SPEC_VERSION 4 /* Explicit authoritative terminal reasons. */
#define SB_NET_QUEUE 2
#define SB_NET_VERSION 2
/* Protocol v2 is deliberately incompatible with the private-peer protocol. */
typedef enum {
    SB_N_ACK=1, SB_N_READY, SB_N_DEPLOY, SB_N_ACT, SB_N_STATE,
    SB_N_STATUS, SB_N_SURRENDER, SB_N_LEAVE, SB_N_REMATCH
} sb_net_type_t;
typedef struct { uint8_t bytes[SB_NET_PACKET]; size_t size; } sb_net_packet_t;
typedef struct { uint8_t type, size; uint32_t sequence, command; uint8_t data[SB_NET_PAYLOAD]; } sb_net_message_t;
void sb_net_put32(uint8_t *p,uint32_t n);
void sb_net_put64(uint8_t *p,uint64_t n);
uint32_t sb_net_get32(const uint8_t *p);
uint64_t sb_net_get64(const uint8_t *p);
bool sb_net_spec_encode(const sb_spec_t *s,uint8_t out[SB_NET_SPEC]);
bool sb_net_spec_decode(const uint8_t in[SB_NET_SPEC],sb_spec_t *s);
/* The driver additionally checks device identity and selects regional RF settings.
 * validated is a software timing gate, not a claim of physical RF certification. */
typedef struct {
    uint16_t max_packet, duty_permille;
    uint32_t retry_ms, heartbeat_ms, silence_ms, grace_ms;
    bool validated;
} sb_net_profile_t;
bool sb_net_profile_valid(const sb_net_profile_t *p);
uint32_t sb_net_airtime_us(size_t bytes); /* SF7/BW125/4:5, explicit header, CRC */
typedef bool (*sb_net_deliver_fn)(void *,const sb_net_message_t *,uint64_t);
typedef struct {
    uint8_t role, session[16], spec[32], keys[2][16], prefixes[2][4];
    uint64_t tx_counter, rx_high, rx_window, last_rx, retry_at, recovery_at, now;
    uint32_t next_command, received_command;
    uint8_t received_hash[32];
    sb_net_message_t queue[SB_NET_QUEUE]; unsigned head,count;
    sb_net_packet_t pending, flight;
    bool pending_sent, ack_due, failed, recovering, inflight;
    sb_net_profile_t profile;
} sb_net_link_t;
bool sb_net_link_init(sb_net_link_t *,unsigned role,const uint8_t session[16],const uint8_t spec[32],
                      const uint8_t material[72],const sb_net_profile_t *,uint64_t now);
bool sb_net_send(sb_net_link_t *,uint8_t type,uint32_t sequence,const void *data,size_t n);
bool sb_net_receive(sb_net_link_t *,const sb_net_packet_t *,uint64_t now,sb_net_deliver_fn,void *);
/* poll reserves one frame. Always report either enqueue rejection or actual
 * worker completion. Only the worker owns airtime pacing. */
bool sb_net_poll(sb_net_link_t *,uint64_t now,sb_net_packet_t *);
void sb_net_tx_complete(sb_net_link_t *,const sb_net_packet_t *,uint64_t now,bool sent);
void sb_net_link_tick(sb_net_link_t *,uint64_t now);
