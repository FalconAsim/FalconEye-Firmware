/*
 * Kart M uygulama katmanı: örnekleri alır, R / solunum / pencere mantığını çalıştırır, CAN mesajlarını üretir.
 * Donanımdan bağımsızdır: sürücüler (ADS1292R, ABP, SDP810, FDCAN) örnekleri ve çerçeveleri bu arayüzle verir.
 */
#ifndef KM_APP_H
#define KM_APP_H
#include "km_proto.h"
#include "km_ecg.h"
#include "km_resp.h"
#include "km_gate.h"

typedef bool (*km_tx_fn)(void *ctx, uint32_t id, const uint8_t *data, uint8_t len);

typedef struct {
    km_ecg_t ecg; km_resp_t resp; km_gate_t gate;
    km_tx_fn tx; void *ctx; km_version_t ver;
    uint8_t win_seq; bool win_sent_open; uint32_t win_tx_ms; bool win_tx_once;
    uint32_t status_ms, hb_ms; bool hb_seen; bool wave_on;
    int32_t dec_acc; uint8_t dec_n; km_wave_t wecg; uint8_t wecg_n; km_wave_t wresp; uint8_t wresp_n;
    uint8_t rsrc; uint8_t err_hw; uint32_t t_now; uint32_t boot_ms; bool started;
    bool reset_req, boot_req;      /* HAL bu bayrakları görünce sıfırlar / önyükleyiciye geçer */
} km_app_t;

void km_app_init(km_app_t *a, km_tx_fn tx, void *ctx, const km_version_t *ver);
/* 1 kHz: EKG örneği (µV) ve elektrot kopukluk maskesi. Pencere mantığı ve pencere mesajı burada işler. */
void km_app_ecg_sample(km_app_t *a, uint32_t t_ms, int32_t x_uv, uint8_t lead_mask);
/* 50 Hz: etkin solunum kaynağından örnek (yastık: 10 Pa birimi; akış: Pa; empedans: mΩ) */
void km_app_resp_sample(km_app_t *a, uint32_t t_ms, int32_t x);
void km_app_set_rsrc(km_app_t *a, uint8_t rsrc);      /* sürücü: takılı / seçili solunum kaynağı */
void km_app_set_hw_err(km_app_t *a, uint8_t err_bits); /* sürücü: KM_ERR_ADS / ABP / SDP */
void km_app_rx(km_app_t *a, uint32_t t_ms, uint32_t id, const uint8_t *data, uint8_t len);
void km_app_get_status(const km_app_t *a, km_status_t *s);

#endif
