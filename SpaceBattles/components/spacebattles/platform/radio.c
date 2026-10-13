// SPDX-License-Identifier: MIT
#include "platform/radio.h"
#include "platform/radio_profile.h"
#include <string.h>

const sb_net_profile_t *sb_radio_profile(const char *region) {
    const sb_radio_region_t *p=SB_NET_RADIO ? sb_radio_region_find(region) : NULL;
    return p ? &p->transport : NULL;
}
#if SB_NET_RADIO
#include "platform/tanmatsu.h"
#include "bsp/power.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "lora.h"
#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif
static lora_handle_t radio;
static const sb_radio_region_t *active_profile;
static QueueHandle_t tx_queue,rx_queue,identities,completions;
static EventGroupHandle_t events;
static bool initialized, opened;
/* Starts eligible immediately; preserve charged pacing across opens/rematches. */
static uint64_t next_tx;
static esp_err_t result;
static int64_t progress;
static sb_radio_stats_t stats;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
#define RADIO_READY BIT0
#define RADIO_STOP BIT1
#define RADIO_DONE BIT2
#define RADIO_FAILED BIT3
static void beat(void) { portENTER_CRITICAL(&lock); progress=esp_timer_get_time(); portEXIT_CRITICAL(&lock); }
typedef struct __attribute__((packed)) {
    uint32_t seq,type; char name[32],version[32],date[16],time[16],idf[32],sha[32]; uint8_t board; char country[2];
} radio_identity_t;
static void identity_rx(uint32_t id,const uint8_t *data,size_t n) {
    if(id==5 && n==sizeof(radio_identity_t) && identities) (void)xQueueSend(identities,data,0);
}
int hosted_reset_slave_callback(void) {
    esp_err_t e=bsp_power_set_radio_state(BSP_POWER_RADIO_STATE_OFF); if(e!=ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(500)); e=bsp_power_set_radio_state(BSP_POWER_RADIO_STATE_APPLICATION);
    vTaskDelay(pdMS_TO_TICKS(1200)); return e;
}
esp_err_t hosted_sdio_reset_slave_callback(void) { return hosted_reset_slave_callback(); }
#define TRY(call) do { esp_err_t e=(call); if(e!=ESP_OK) return e; } while(0)
static esp_err_t restore_rx(void) {
    lora_protocol_mode_t mode; TRY(lora_get_mode(&radio,&mode));
    if(mode==LORA_PROTOCOL_MODE_RX) return ESP_OK;
    if(mode!=LORA_PROTOCOL_MODE_STANDBY_RC && mode!=LORA_PROTOCOL_MODE_STANDBY_XOSC && mode!=LORA_PROTOCOL_MODE_FS) return ESP_ERR_INVALID_STATE;
    TRY(lora_set_mode(&radio,LORA_PROTOCOL_MODE_RX)); TRY(lora_get_mode(&radio,&mode));
    if(mode==LORA_PROTOCOL_MODE_RX && initialized) {
        portENTER_CRITICAL(&lock); ++stats.rx_recoveries; portEXIT_CRITICAL(&lock);
    }
    return mode==LORA_PROTOCOL_MODE_RX ? ESP_OK : ESP_ERR_INVALID_STATE;
}
static esp_err_t initialize(void) {
    if(!initialized) {
        esp_err_t e=esp_event_loop_create_default(); if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE) return e;
        TRY(lora_init_remote(&radio,8));
        TRY(esp_hosted_register_custom_callback(5,identity_rx)); TRY(esp_hosted_init()); TRY(esp_hosted_connect_to_slave());
        lora_protocol_status_params_t status={0}; TRY(lora_get_status(&radio,&status));
        TRY(lora_set_mode(&radio,LORA_PROTOCOL_MODE_STANDBY_RC));
        radio_identity_t req={.seq=esp_random(),.type=2},identity;
        TRY(esp_hosted_send_custom_data(5,(uint8_t *)&req,sizeof(req)));
        if(xQueueReceive(identities,&identity,pdMS_TO_TICKS(2500))!=pdTRUE) return ESP_ERR_TIMEOUT;
        const uint8_t sha[32]=SB_NET_RADIO_SHA;
        if(identity.seq!=req.seq || identity.type!=2 || status.chip_type!=LORA_PROTOCOL_CHIP_SX1262 || status.errors ||
           identity.board!=SB_NET_RADIO_BOARD || strncmp(identity.version,SB_NET_RADIO_VERSION,32) || memcmp(identity.sha,sha,32)) return ESP_ERR_NOT_SUPPORTED;
    }
    TRY(lora_set_mode(&radio,LORA_PROTOCOL_MODE_STANDBY_RC));
    lora_protocol_config_params_t config={.frequency=active_profile->frequency,.spreading_factor=7,.bandwidth=active_profile->bandwidth,.coding_rate=5,
        .sync_word=0x12,.preamble_length=8,.power=active_profile->power,.ramp_time=40,.crc_enabled=true,.rx_boost=true,.use_dcdc=true},readback;
    TRY(lora_set_config(&radio,&config)); TRY(lora_set_frequency_offset(&radio,0)); TRY(lora_get_config(&radio,&readback));
    if(memcmp(&config,&readback,sizeof(config))) return ESP_ERR_INVALID_RESPONSE;
    TRY(restore_rx()); initialized=true; return ESP_OK;
}
static void worker(void *unused) {
    (void)unused; result=initialize(); beat();
    if(result==ESP_OK) xEventGroupSetBits(events,RADIO_READY);
    sb_net_packet_t pending={0};
    uint64_t rx_check_at=0;
    while(result==ESP_OK && !(xEventGroupGetBits(events)&RADIO_STOP)) {
        beat(); uint64_t now=(uint64_t)esp_timer_get_time()/1000;
        if(!pending.size) (void)xQueueReceive(tx_queue,&pending,0);
        if(pending.size && now>=next_tx) {
            lora_protocol_lora_packet_t p={.length=(uint8_t)pending.size}; memcpy(p.data,pending.bytes,pending.size);
            next_tx=now+(sb_net_airtime_us(pending.size)+active_profile->transport.duty_permille-1)/active_profile->transport.duty_permille;
            /* Charge before the call; uncertain TX is never refunded. */
            result=lora_send_packet(&radio,&p);
            sb_radio_tx_event_t done={.packet=pending,.at=(uint64_t)esp_timer_get_time()/1000,.sent=result==ESP_OK};
            if(xQueueSend(completions,&done,0)!=pdTRUE && result==ESP_OK) result=ESP_ERR_NO_MEM;
            pending.size=0;
            if(result==ESP_OK) {
                portENTER_CRITICAL(&lock); ++stats.tx_packets; portEXIT_CRITICAL(&lock);
                result=restore_rx();
            }
        }
        lora_protocol_lora_packet_t incoming={0};
        if(result==ESP_OK && lora_receive_packet(&radio,&incoming,pdMS_TO_TICKS(20))==ESP_OK) {
            result=restore_rx();
            if(result==ESP_OK && incoming.length<=253) {
                portENTER_CRITICAL(&lock); ++stats.rx_packets; portEXIT_CRITICAL(&lock);
                sb_net_packet_t p={.size=incoming.length}; memcpy(p.bytes,incoming.data,p.size);
                (void)xQueueSend(rx_queue,&p,0); /* Overflow is packet loss, recovered by the protocol. */
            }
        }
        /* A remote RX timeout/error can leave standby without a queued packet.
         * Poll even when receive returns empty; never restart RX while already
         * receiving, or interrupt a transmission in an unexpected mode. */
        if(result==ESP_OK && now>=rx_check_at) {
            result=restore_rx();
            rx_check_at=(uint64_t)esp_timer_get_time()/1000+1000;
        }
        vTaskDelay(1);
    }
    if(initialized) { esp_err_t e=lora_set_mode(&radio,LORA_PROTOCOL_MODE_STANDBY_RC); if(result==ESP_OK) result=e; }
    if(result!=ESP_OK) {
#ifdef ESP_PLATFORM
        ESP_LOGE("Radio", "Worker stopped (%s): %s", initialized ? "active/close" : "initialization", esp_err_to_name(result));
#endif
        xEventGroupSetBits(events,RADIO_FAILED);
    }
    xEventGroupSetBits(events,RADIO_DONE); vTaskDelete(NULL);
}
esp_err_t sb_radio_open(const char *region) {
    if(!sb_radio_profile(region)) return ESP_ERR_NOT_SUPPORTED;
    if(opened) return ESP_ERR_INVALID_STATE;
    if(!events) {
        events=xEventGroupCreate(); tx_queue=xQueueCreate(2,sizeof(sb_net_packet_t)); rx_queue=xQueueCreate(8,sizeof(sb_net_packet_t));
        identities=xQueueCreate(2,sizeof(radio_identity_t)); completions=xQueueCreate(4,sizeof(sb_radio_tx_event_t));
    }
    if(!events || !tx_queue || !rx_queue || !identities || !completions) return ESP_ERR_NO_MEM;
    active_profile=sb_radio_region_find(region);
    portENTER_CRITICAL(&lock); stats=(sb_radio_stats_t){0}; portEXIT_CRITICAL(&lock);
    xEventGroupClearBits(events,RADIO_READY|RADIO_STOP|RADIO_DONE|RADIO_FAILED);
    xQueueReset(tx_queue); xQueueReset(rx_queue); xQueueReset(completions); beat();
    if(xTaskCreate(worker,"sb_radio",8192,NULL,4,NULL)!=pdPASS) return ESP_ERR_NO_MEM;
    opened=true; return ESP_OK;
}
sb_radio_stats_t sb_radio_stats(void) {
    portENTER_CRITICAL(&lock); sb_radio_stats_t snapshot=stats; portEXIT_CRITICAL(&lock); return snapshot;
}
bool sb_radio_host_ready(void) { return initialized; }
bool sb_radio_ready(void) { return opened && (xEventGroupGetBits(events)&RADIO_READY) && !(xEventGroupGetBits(events)&RADIO_DONE); }
esp_err_t sb_radio_status(void) {
    if(!opened) return ESP_ERR_INVALID_STATE;
    if(xEventGroupGetBits(events)&RADIO_FAILED) return result;
    int64_t last; portENTER_CRITICAL(&lock); last=progress; portEXIT_CRITICAL(&lock);
    if(esp_timer_get_time()-last>(sb_radio_ready() ? 15000000 : 30000000)) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}
bool sb_radio_send(const sb_net_packet_t *p) {
    return p && p->size<=253 && p->size && sb_radio_ready() && xQueueSend(tx_queue,p,0)==pdTRUE;
}
bool sb_radio_receive(sb_net_packet_t *p) { return p && opened && xQueueReceive(rx_queue,p,0)==pdTRUE; }
bool sb_radio_tx_complete(sb_radio_tx_event_t *e) { return e && opened && xQueueReceive(completions,e,0)==pdTRUE; }
esp_err_t sb_radio_close(void) {
    if(!opened) return ESP_OK;
    xEventGroupSetBits(events,RADIO_STOP);
    if(!(xEventGroupWaitBits(events,RADIO_DONE,pdFALSE,pdTRUE,pdMS_TO_TICKS(5000))&RADIO_DONE)) {
        /* Never delete a task blocked in SDIO or free live callback storage. */
        sb_platform_exit(); return ESP_ERR_TIMEOUT;
    }
    opened=false; return result;
}
#else
/* The pinned hosted component references this callback even in a no-radio image. */
int hosted_reset_slave_callback(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t hosted_sdio_reset_slave_callback(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sb_radio_open(const char *region) { (void)region; return ESP_ERR_NOT_SUPPORTED; }
sb_radio_stats_t sb_radio_stats(void) { return (sb_radio_stats_t){0}; }
bool sb_radio_host_ready(void) { return false; }
bool sb_radio_ready(void) { return false; }
esp_err_t sb_radio_status(void) { return ESP_ERR_NOT_SUPPORTED; }
bool sb_radio_send(const sb_net_packet_t *p) { (void)p; return false; }
bool sb_radio_receive(sb_net_packet_t *p) { (void)p; return false; }
bool sb_radio_tx_complete(sb_radio_tx_event_t *e) { (void)e; return false; }
esp_err_t sb_radio_close(void) { return ESP_OK; }
#endif
