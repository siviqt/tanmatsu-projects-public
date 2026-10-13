// SPDX-License-Identifier: MIT
#pragma once
#include "net/wire.h"
#include <string.h>
/* Software-enabled profiles; physical RF qualification is tracked separately.
 * Both use conservative SF7/BW125 airtime accounting (overcharges US BW500).
 * Check the supported board and radio firmware, independent of device MAC. */
typedef struct {
    const char *region;
    uint32_t frequency;
    uint16_t bandwidth;
    int8_t power;
    sb_net_profile_t transport;
} sb_radio_region_t;
#ifdef SB_NET_PROFILE_HEADER
#include SB_NET_PROFILE_HEADER
#ifndef SB_NET_BANDWIDTH
#define SB_NET_BANDWIDTH 125
#endif
static const sb_radio_region_t sb_radio_regions[]={
    {SB_NET_REGION,SB_NET_FREQUENCY,SB_NET_BANDWIDTH,SB_NET_POWER,SB_NET_PROFILE_INITIALIZER}
};
#else
#define SB_NET_RADIO_VERSION "v3.4.0"
#define SB_NET_RADIO_BOARD 7
#define SB_NET_RADIO_SHA {0xdb,0x2f,0x91,0x39,0xd5,0x1a,0x43,0xfe,0xbb,0x86,0x17,0x7e,0x4f,0x80,0x22,0x05,0xaa,0xdd,0xdb,0x48,0x9c,0xdd,0x8d,0x54,0xc7,0x09,0x0f,0x1f,0xc0,0xcb,0x04,0xc0}
/* 9% pacing leaves margin below the EU sub-band's 10% duty limit.
 * Worst-case spacing is 4384 ms; retry covers two packets plus 1 s.
 * validated means transport timing checked, not regulatory certification. */
#define SB_REGIONAL_TRANSPORT {.max_packet=253,.duty_permille=90,.retry_ms=10000, \
    .heartbeat_ms=30000,.silence_ms=90000,.grace_ms=30000,.validated=true}
static const sb_radio_region_t sb_radio_regions[]={
    {"EU868",869525000,125,0,SB_REGIONAL_TRANSPORT},
    {"US915",915000000,500,0,SB_REGIONAL_TRANSPORT},
};
#endif
static inline const sb_radio_region_t *sb_radio_region_find(const char *region) {
    if(!region) return NULL;
    for(size_t i=0;i<sizeof(sb_radio_regions)/sizeof(sb_radio_regions[0]);i++) {
        const sb_radio_region_t *p=&sb_radio_regions[i];
        if(!strcmp(region,p->region) && (p->bandwidth==125 || p->bandwidth==500) &&
           sb_net_profile_valid(&p->transport)) return p;
    }
    return NULL;
}
