#!/usr/bin/env python3
"""Exercise the production radio worker with idle RX loss and a virtual clock."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='sb-radio-') as tmp:
    d=Path(tmp)
    for name in ['platform/tanmatsu.h','bsp/power.h','esp_event.h','esp_hosted.h','esp_mac.h','esp_random.h','esp_timer.h','esp_err.h','freertos/FreeRTOS.h','freertos/task.h','freertos/queue.h','freertos/event_groups.h','lora.h']:
        p=d/name; p.parent.mkdir(exist_ok=True); p.write_text('#include "mock.h"\n')
    (d/'mock.h').write_text(r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_INVALID_RESPONSE 4
#define ESP_ERR_NOT_SUPPORTED 5
#define BSP_POWER_RADIO_STATE_OFF 0
#define BSP_POWER_RADIO_STATE_APPLICATION 1
#define LORA_PROTOCOL_MODE_STANDBY_RC 1
#define LORA_PROTOCOL_MODE_STANDBY_XOSC 2
#define LORA_PROTOCOL_MODE_FS 3
#define LORA_PROTOCOL_MODE_RX 5
#define LORA_PROTOCOL_CHIP_SX1262 0
typedef int lora_protocol_mode_t;
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define BIT0 1u
#define BIT1 2u
#define BIT2 4u
#define BIT3 8u
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
typedef struct { unsigned count,head; size_t size; unsigned char data[8][512]; } queue_t;
typedef queue_t *QueueHandle_t;
typedef unsigned *EventGroupHandle_t;
typedef struct { int unused; } lora_handle_t;
typedef struct { uint32_t frequency; uint8_t spreading_factor; uint16_t bandwidth; uint8_t coding_rate,sync_word; uint16_t preamble_length; int8_t power; uint8_t ramp_time; bool crc_enabled,rx_boost,use_dcdc; } lora_protocol_config_params_t;
typedef struct { unsigned errors; int chip_type; } lora_protocol_status_params_t;
typedef struct { uint8_t length,data[256]; } lora_protocol_lora_packet_t;
int bsp_power_set_radio_state(int); void vTaskDelay(unsigned); void vTaskDelete(void *);
int64_t esp_timer_get_time(void); uint32_t esp_random(void);
int esp_efuse_mac_get_default(uint8_t*); int esp_event_loop_create_default(void);
QueueHandle_t xQueueCreate(int,size_t); int xQueueSend(QueueHandle_t,const void*,int); int xQueueReceive(QueueHandle_t,void*,int); void xQueueReset(QueueHandle_t);
EventGroupHandle_t xEventGroupCreate(void); unsigned xEventGroupGetBits(EventGroupHandle_t); void xEventGroupSetBits(EventGroupHandle_t,unsigned); void xEventGroupClearBits(EventGroupHandle_t,unsigned); unsigned xEventGroupWaitBits(EventGroupHandle_t,unsigned,int,int,unsigned);
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,void *);
int esp_hosted_init(void); int esp_hosted_connect_to_slave(void);
int esp_hosted_register_custom_callback(uint32_t,void(*)(uint32_t,const uint8_t*,size_t));
int esp_hosted_send_custom_data(uint32_t,const uint8_t*,size_t);
int lora_init_remote(lora_handle_t*,int); int lora_get_status(lora_handle_t*,lora_protocol_status_params_t*);
int lora_get_mode(lora_handle_t*,lora_protocol_mode_t*); int lora_set_mode(lora_handle_t*,int); int lora_set_config(lora_handle_t*,const lora_protocol_config_params_t*);
int lora_get_config(lora_handle_t*,lora_protocol_config_params_t*); int lora_set_frequency_offset(lora_handle_t*,float);
int lora_send_packet(lora_handle_t*,const lora_protocol_lora_packet_t*); int lora_receive_packet(lora_handle_t*,lora_protocol_lora_packet_t*,unsigned);
void sb_platform_exit(void);
''')
    (d/'test.c').write_text(r'''
#include "platform/radio.c"
#include <stdio.h>
static uint64_t clock_ms,stop_at=2500;
static int mode,scenario;
static bool interrupted,delivered;
static unsigned sends;
static uint64_t sent_at[4];
static lora_protocol_config_params_t cfg;
static void (*scheduled)(void *);
static void (*callback)(uint32_t,const uint8_t*,size_t);
int bsp_power_set_radio_state(int s) { (void)s; return 0; }
void vTaskDelay(unsigned ms) { clock_ms+=ms; if(clock_ms>=stop_at) xEventGroupSetBits(events,RADIO_STOP); }
void vTaskDelete(void *p) { (void)p; }
int64_t esp_timer_get_time(void) { return clock_ms*1000; }
uint32_t esp_random(void) { return 42; }
int esp_efuse_mac_get_default(uint8_t *p) { const uint8_t mac[2][6]=SB_NET_DEVICES; memcpy(p,mac[0],6); return 0; }
int esp_event_loop_create_default(void) { return 0; }
QueueHandle_t xQueueCreate(int n,size_t size) { assert(n<=8 && size<=512); queue_t *q=calloc(1,sizeof(*q)); assert(q); q->size=size; return q; }
int xQueueSend(QueueHandle_t q,const void *p,int t) { (void)t; if(q->count==8) return 0; memcpy(q->data[(q->head+q->count++)%8],p,q->size); return 1; }
int xQueueReceive(QueueHandle_t q,void *p,int t) { (void)t; if(!q->count) return 0; memcpy(p,q->data[q->head],q->size); q->head=(q->head+1)%8; --q->count; return 1; }
void xQueueReset(QueueHandle_t q) { q->count=q->head=0; }
EventGroupHandle_t xEventGroupCreate(void) { return calloc(1,sizeof(unsigned)); }
unsigned xEventGroupGetBits(EventGroupHandle_t e) { return *e; }
void xEventGroupSetBits(EventGroupHandle_t e,unsigned b) { *e|=b; }
void xEventGroupClearBits(EventGroupHandle_t e,unsigned b) { *e&=~b; }
unsigned xEventGroupWaitBits(EventGroupHandle_t e,unsigned b,int c,int a,unsigned t) { (void)b;(void)c;(void)a;(void)t; return *e; }
int xTaskCreate(void (*fn)(void *),const char *n,unsigned s,void *p,unsigned pri,void *h) { (void)n;(void)s;(void)p;(void)pri;(void)h; scheduled=fn; return pdPASS; }
int esp_hosted_init(void) { return 0; } int esp_hosted_connect_to_slave(void) { return 0; }
int esp_hosted_register_custom_callback(uint32_t id,void(*cb)(uint32_t,const uint8_t*,size_t)) { assert(id==5);callback=cb;return 0; }
int esp_hosted_send_custom_data(uint32_t id,const uint8_t *p,size_t n) { radio_identity_t identity; assert(n==sizeof(identity)); memcpy(&identity,p,n); identity.board=SB_NET_RADIO_BOARD; strcpy(identity.version,SB_NET_RADIO_VERSION); const uint8_t sha[32]=SB_NET_RADIO_SHA; memcpy(identity.sha,sha,32); callback(id,(uint8_t*)&identity,n); return 0; }
int lora_init_remote(lora_handle_t *h,int n) { (void)h;(void)n;return 0; }
int lora_get_status(lora_handle_t *h,lora_protocol_status_params_t *s) { (void)h; *s=(lora_protocol_status_params_t){.chip_type=LORA_PROTOCOL_CHIP_SX1262}; return 0; }
int lora_get_mode(lora_handle_t *h,lora_protocol_mode_t *m) { (void)h; *m=mode; return 0; }
int lora_set_mode(lora_handle_t *h,int m) { (void)h; mode=m; return 0; }
int lora_set_config(lora_handle_t *h,const lora_protocol_config_params_t *c) { (void)h;cfg=*c;return 0; }
int lora_get_config(lora_handle_t *h,lora_protocol_config_params_t *c) { (void)h;*c=cfg;if(scenario==3) c->frequency=433000000;return 0; }
int lora_set_frequency_offset(lora_handle_t *h,float f) { (void)h;(void)f;return 0; }
int lora_send_packet(lora_handle_t *h,const lora_protocol_lora_packet_t *p) { (void)h;assert(p->length==77);assert(sends<4);sent_at[sends++]=clock_ms; mode=LORA_PROTOCOL_MODE_STANDBY_RC;return 0; }
int lora_receive_packet(lora_handle_t *h,lora_protocol_lora_packet_t *p,unsigned ms) {
    (void)h;clock_ms+=ms;
    if(scenario<2 && !interrupted && clock_ms>=300) { interrupted=true;mode=scenario ? 99 : LORA_PROTOCOL_MODE_STANDBY_RC; }
    if(!delivered && mode==LORA_PROTOCOL_MODE_RX && clock_ms>=1000) { *p=(lora_protocol_lora_packet_t){.length=77}; memcpy(p->data,"SBA\1",4);delivered=true;return 0; }
    return ESP_FAIL;
}
void sb_platform_exit(void) { assert(0); }
int main(int argc,char **argv) {
    assert(argc==2);scenario=atoi(argv[1]);
    assert(sb_radio_open("unknown")==ESP_ERR_NOT_SUPPORTED);
    assert(sb_radio_open("NONE")==ESP_ERR_NOT_SUPPORTED);
    assert(sb_radio_open(NULL)==ESP_ERR_NOT_SUPPORTED);
    assert(!scheduled && !sends);
    if(scenario==2) stop_at=100;
    assert(sb_radio_open("EU868")==ESP_OK);
    sb_net_packet_t p={.size=77};
    if(scenario==2) assert(xQueueSend(tx_queue,&p,0));
    scheduled(NULL);
    if(scenario==0) { assert(delivered);assert(sb_radio_stats().rx_packets==1);assert(sb_radio_stats().rx_recoveries>=1);assert(sb_radio_receive(&p));assert(p.size==77 && !memcmp(p.bytes,"SBA\1",4)); }
    if(scenario==1) assert(sb_radio_status()==ESP_ERR_INVALID_STATE);
    if(scenario==2) {
        assert(sends==1 && sent_at[0]==0 && sb_radio_stats().tx_packets==1);
        sb_radio_tx_event_t done; assert(sb_radio_tx_complete(&done) && done.sent && done.packet.size==77 && done.at==sent_at[0]);
        assert(!sb_radio_tx_complete(&done)); /* No startup holdoff. */
        assert(sb_radio_close()==ESP_OK);
        assert(sb_radio_open("US915")==ESP_OK);stop_at=clock_ms+2500;
        assert(xQueueSend(tx_queue,&p,0));scheduled(NULL);
        assert(cfg.frequency==915000000 && cfg.bandwidth==500);
        assert(sends==2 && sent_at[1]-sent_at[0]>=(sb_net_airtime_us(77)+89)/90);
    }
    if(scenario==3) { assert(sb_radio_status()==ESP_ERR_INVALID_RESPONSE); assert(!sends && !sb_radio_ready()); }
    if(scenario!=1 && scenario!=3) assert(sb_radio_close()==ESP_OK);
    free(tx_queue);free(rx_queue);free(identities);free(completions);free(events);
    puts("Radio worker scenario passed");
}
''')
    exe=d/'test'
    subprocess.run(['cc','-std=c11','-DSB_NET_HOST','-DSB_NET_RADIO=1','-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=undefined','-ffunction-sections','-fdata-sections','-I'+tmp,'-I'+str(root/'components/spacebattles'),str(d/'test.c'),str(root/'components/spacebattles/net/wire.c'),'-Wl,--gc-sections','-o',str(exe)],check=True)
    for scenario in range(4): subprocess.run([str(exe),str(scenario)],check=True)
