// SPDX-License-Identifier: MIT
#pragma once
#include "net/wire.h"
#include "esp_err.h"
const sb_net_profile_t *sb_radio_profile(const char *region);
/* Nonblocking owner-task API; worker owns all blocking driver calls. */
esp_err_t sb_radio_open(const char *region);
typedef struct { uint32_t tx_packets, rx_packets, rx_recoveries; } sb_radio_stats_t;
sb_radio_stats_t sb_radio_stats(void);
bool sb_radio_ready(void);
bool sb_radio_host_ready(void);
esp_err_t sb_radio_status(void);
bool sb_radio_send(const sb_net_packet_t *);
bool sb_radio_receive(sb_net_packet_t *);
typedef struct { sb_net_packet_t packet; uint64_t at; bool sent; } sb_radio_tx_event_t;
bool sb_radio_tx_complete(sb_radio_tx_event_t *);
esp_err_t sb_radio_close(void);
