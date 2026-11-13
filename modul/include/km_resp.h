/*
 * Kart M – solunum fazı izleyici (50 örnek/s, tam sayı).
 * Kaynaklar: yastık / kemer basıncı (ABP), T parçası akışı (SDP810 – sızıntılı tümlevle hacme çevrilir),
 * EKG elektrotlarından empedans (ADS1292R). Hepsi "soluk alınca artan" tek bir sinyale indirgenir.
 * Çıktı: soluk verme sonu (EXP) ve soluk alma sonu (INS) düzlükleri, nefes hızı.
 *
 * Giriş birimleri (km_app_resp_sample): yastık / kemer Pa · akış 0,1 Pa (SDP810 ham / ölçek × 10) · empedans mΩ.
 * Tür tablosu (rev 2, 13.11.2026): fare nefesi 0,4 s, köpeğinki 10 s'ye kadar – süzgeç boyu, eğim penceresi,
 * akış tümlevinin sızıntı zaman sabiti, geçerli nefes aralığı ve en küçük genlik türe göre seçilir.
 */
#ifndef KM_RESP_H
#define KM_RESP_H
#include "km_proto.h"

#define KM_RESP_FS_HZ 50u
#define KM_RESP_Q     16      /* iç ölçek: s, env_min, env_max giriş biriminin 16 katıdır */

typedef struct {
    uint16_t period_min_ms, period_max_ms;   /* geçerli nefes süresi */
    uint8_t  ma_n;                           /* kayan ortalama (örnek, 1–5) */
    uint8_t  slope_n;                        /* düzlük için eğim penceresi (örnek, 1–5) */
    uint8_t  flow_k;                         /* akış tümlevi sızıntısı: vol −= vol / 2^k  (τ ≈ 2^k × 20 ms) */
    int16_t  min_amp[3];                     /* en küçük tepe–tepe genlik: yastık Pa, akış tümlevi, empedans mΩ */
} km_resp_par_t;

/* geçersiz tür → KM_SP_OTHER satırı (önceki tek ayarla aynı) */
const km_resp_par_t *km_resp_par(uint8_t species);

typedef struct {
    const km_resp_par_t *par;
    uint8_t rsrc; bool invert;
    int32_t min_amp;                 /* geçerli sayılacak en küçük tepe–tepe genlik (kaynak birimi) */
    int32_t vol;                     /* akış tümlevi */
    int32_t ma[5]; uint8_t mi; uint8_t n;
    int32_t s, s_hist[6]; uint8_t shi;
    int32_t env_min, env_max;
    bool high; uint32_t t_cross; bool have_cross;
    uint16_t period_ms;
    uint8_t level;                   /* 0–255: zarf içindeki konum */
    bool quiet;
    bool exp_ok, ins_ok;
    uint32_t plateau_t0; uint16_t last_plateau_ms[2]; uint8_t plateau;   /* 0 yok, 1 EXP, 2 INS */
    uint32_t t_now;
} km_resp_t;

void km_resp_init(km_resp_t *r, uint8_t rsrc, bool invert, uint8_t species);
/* Her 20 ms'de bir örnek. Yeni nefes başladıysa true ve *evt dolu. */
bool km_resp_sample(km_resp_t *r, uint32_t t_ms, int32_t x, km_respevt_t *evt);
bool km_resp_valid(const km_resp_t *r);
uint16_t km_resp_rate_x10(const km_resp_t *r);       /* nefes / dk × 10; 0 = bilinmiyor */
bool km_resp_phase_ok(const km_resp_t *r, uint8_t rph);
/* Bulunulan düzlüğün tahmini kalan süresi (ms): önceki düzlüğün 3/4'ü − geçen süre; bilinmiyorsa 0 */
uint16_t km_resp_plateau_remain_ms(const km_resp_t *r);

#endif
