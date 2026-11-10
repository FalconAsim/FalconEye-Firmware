/*
 * Kart M – gerçek zamanlı R dalgası bulucu (tam sayı aritmetiği, Cortex-M0+ için FPU gerektirmez).
 * Giriş: 1000 örnek/s, µV. Küçük hayvanlarda QRS ≈ 10 ms olduğu için 250 örnek/s yetmez.
 * Yöntem: kayan ortalama (şebeke / gürültü) → yavaş taban çıkarma → k ms'lik fark (eğim) → uyarlanır eşik →
 * QRS içinde tepe izleme. Süzgecin grup gecikmesi R zamanından düşülür.
 * R zamanı tepe örneğinin zamanıdır; karar birkaç ms sonra verilir (gecikme her olayda bilinir).
 */
#ifndef KM_ECG_H
#define KM_ECG_H
#include "km_proto.h"

#define KM_ECG_FS_HZ 1000u
#define KM_ECG_HIST  16u
#define KM_ECG_MA_MAX 20u

typedef struct {
    uint16_t hr_min, hr_max;   /* atım / dk – tür için beklenen aralık */
    uint16_t qrs_ms;           /* en uzun QRS izleme süresi = en büyük karar gecikmesi */
    uint16_t refr_ms;          /* R sonrası yeni R kabul edilmeyen süre */
    uint8_t  diff_ms;          /* eğim penceresi k */
    uint8_t  ma_ms;            /* ön süzgeç: kayan ortalama (20 ms = 50 Hz şebekeyi sıfırlar) */
} km_species_par_t;

const km_species_par_t *km_species_par(uint8_t species);   /* geçersiz tür → NULL */

typedef struct {
    const km_species_par_t *par;
    int32_t base_q6;                 /* taban (µV × 64) */
    int32_t hist[KM_ECG_HIST];
    uint8_t hi;
    int32_t ma[KM_ECG_MA_MAX]; int32_t ma_sum; uint8_t ma_i;
    bool primed;
    /* öğrenme */
    uint8_t learn_blk; uint32_t learn_t0; int32_t learn_max; int32_t learn_pk[3];
    bool learned;
    /* eşik */
    int32_t spk, thr;
    uint8_t above;
    /* QRS izleme */
    bool tracking; uint32_t t_start, t_max; int32_t max_hp_abs, max_hp, max_ad;
    /* ritim */
    uint32_t t_last_r; bool have_r;
    uint16_t rr[5]; uint8_t n_rr, rr_i;
    /* kalite */
    uint8_t lead_mask;
    uint16_t reject; uint32_t reject_t0; bool noisy;
    uint32_t t_now;
} km_ecg_t;

void km_ecg_init(km_ecg_t *e, uint8_t species);
void km_ecg_set_leadoff(km_ecg_t *e, uint8_t lead_mask);
/* Her 1 ms'de bir örnek. R bulunduysa true ve *evt dolu (evt->t_ms = tepe zamanı). */
bool km_ecg_sample(km_ecg_t *e, uint32_t t_ms, int32_t x_uv, km_revt_t *evt);
uint16_t km_ecg_hr_bpm(const km_ecg_t *e);          /* 0 = bilinmiyor */
uint16_t km_ecg_rr_median_ms(const km_ecg_t *e);    /* 0 = bilinmiyor */
km_ecgq_t km_ecg_quality(const km_ecg_t *e);

#endif
