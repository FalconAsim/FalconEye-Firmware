/*
 * Kart M – tetik penceresi durum makinesi.
 * ECG : pencere R tepesi + DLY'de açılır, WIN kadar sürer; yeni R gelirse hemen kapanır.
 * RESP: seçili solunum düzlüğüne girildikten DLY sonra açılır; düzlük bitince ya da WIN dolunca kapanır.
 * BOTH: EKG penceresi, yalnız açılış anında solunum düzlüğündeyse açılır; düzlük biterse kapanır.
 * Pencere yalnız "şimdi çekilebilir" bilgisidir – ışınlamayı başlatmaz.
 */
#ifndef KM_GATE_H
#define KM_GATE_H
#include "km_proto.h"

typedef struct {
    km_cfg_t cfg; bool have_cfg; uint16_t cfg_crc;
    bool open; uint32_t open_at, close_at; bool scheduled;
    bool resp_prev; uint32_t resp_t0; bool resp_armed;
    uint16_t late;            /* DLY, karar gecikmesinden kısa kaldığı için atlanan atım sayısı */
    uint16_t windows;         /* açılan pencere sayısı */
} km_gate_t;

void km_gate_init(km_gate_t *g);
/* Ayarı denetler; uygunsa kabul eder. rsrc: etkin solunum kaynağı (yoksa RESP / BOTH reddedilir). */
km_cfg_result_t km_gate_set_cfg(km_gate_t *g, const km_cfg_t *c, uint8_t rsrc, km_cfg_ack_t *ack);
/* R olayı (ecg_ok: sinyal kalitesi uygun). now_ms: kararın verildiği an. */
void km_gate_on_r(km_gate_t *g, uint32_t now_ms, const km_revt_t *evt, bool ecg_ok);
/* Her 1 ms: resp_phase_ok = seçili düzlükte mi; plateau_remain_ms = düzlüğün tahmini kalanı (0 = bilinmiyor) */
void km_gate_tick(km_gate_t *g, uint32_t now_ms, bool ecg_ok, bool resp_ok, bool resp_phase_ok, uint16_t plateau_remain_ms);
bool km_gate_is_open(const km_gate_t *g);
uint16_t km_gate_remain_ms(const km_gate_t *g, uint32_t now_ms, uint16_t plateau_remain_ms);

#endif
