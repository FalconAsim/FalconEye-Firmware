#include "km_ecg.h"
#include <string.h>

#define MIN_THR_UV 20

/* Başlangıç değerleri: literatürdeki tipik anestezi altı aralıklar. İlk kartta simülatör ve
 * gerçek kayıtlarla doğrulanacak (FalconEye_Arayuz_Tanimi §15.6). */
static const km_species_par_t PAR[KM_SP_COUNT] = {
    /* hr_min hr_max qrs refr diff ma */
    { 250, 800, 20,  50, 2,  1 },   /* MOUSE – QRS ≈ 10 ms: süzgeç yok */
    { 180, 600, 25,  70, 3,  2 },   /* RAT   */
    {  80, 300, 50, 140, 5, 20 },   /* CAT   – 20 ms: 50 Hz sıfırlanır */
    {  40, 250, 70, 170, 8, 20 },   /* DOG   */
    {  30, 800, 60,  70, 4,  4 },   /* OTHER */
};

const km_species_par_t *km_species_par(uint8_t species) { return species < KM_SP_COUNT ? &PAR[species] : 0; }

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

static void relearn(km_ecg_t *e, uint32_t t)
{
    e->learned = false; e->learn_blk = 0; e->learn_t0 = t; e->learn_max = 0;
    e->tracking = false; e->above = 0; e->have_r = false; e->n_rr = 0; e->rr_i = 0; e->noisy = false; e->reject = 0; e->reject_t0 = t;
}

void km_ecg_init(km_ecg_t *e, uint8_t species)
{
    memset(e, 0, sizeof *e);
    e->par = km_species_par(species);
    if (!e->par) e->par = &PAR[KM_SP_OTHER];
}

void km_ecg_set_leadoff(km_ecg_t *e, uint8_t lead_mask)
{
    if (lead_mask && !e->lead_mask) { e->tracking = false; e->have_r = false; e->n_rr = 0; }
    if (!lead_mask && e->lead_mask) relearn(e, e->t_now);      /* elektrot geri takıldı: eşiği yeniden öğren */
    e->lead_mask = lead_mask;
}

static uint16_t max_rr_ms(const km_ecg_t *e) { return (uint16_t)(60000u / e->par->hr_min); }
static uint16_t min_rr_ms(const km_ecg_t *e) { return (uint16_t)(60000u / e->par->hr_max); }

bool km_ecg_sample(km_ecg_t *e, uint32_t t, int32_t x, km_revt_t *evt)
{
    e->t_now = t;
    if (!e->primed) {
        for (unsigned i = 0; i < KM_ECG_MA_MAX; i++) e->ma[i] = x;
        e->ma_sum = x * (int32_t)e->par->ma_ms; e->ma_i = 0;
        e->base_q6 = x * 64;
        for (unsigned i = 0; i < KM_ECG_HIST; i++) e->hist[i] = 0;
        e->primed = true;
        relearn(e, t);
    }
    /* ön süzgeç: ma_ms örneklik kayan ortalama; grup gecikmesi (ma_ms − 1) / 2 ms */
    e->ma_sum += x - e->ma[e->ma_i]; e->ma[e->ma_i] = x; e->ma_i = (uint8_t)((e->ma_i + 1u) % e->par->ma_ms);
    x = e->ma_sum / (int32_t)e->par->ma_ms;
    uint32_t tf = t - (uint32_t)(e->par->ma_ms - 1u) / 2u;      /* süzgeçlenmiş örneğin gerçek zamanı */
    e->base_q6 += x - e->base_q6 / 64;
    int32_t hp = x - e->base_q6 / 64;
    e->hi = (uint8_t)((e->hi + 1u) % KM_ECG_HIST);
    e->hist[e->hi] = hp;
    int32_t d = hp - e->hist[(e->hi + KM_ECG_HIST - e->par->diff_ms) % KM_ECG_HIST];
    int32_t ad = iabs(d);

    if (e->lead_mask) return false;

    /* --- öğrenme: en uzun R-R kadar 3 blok; blok tepe eğimlerinin ortancası --- */
    if (!e->learned) {
        if (ad > e->learn_max) e->learn_max = ad;
        if (t - e->learn_t0 >= max_rr_ms(e)) {
            e->learn_pk[e->learn_blk++] = e->learn_max; e->learn_max = 0; e->learn_t0 = t;
            if (e->learn_blk >= 3) {
                int32_t a = e->learn_pk[0], b = e->learn_pk[1], c = e->learn_pk[2];
                int32_t med = a > b ? (b > c ? b : (a > c ? c : a)) : (a > c ? a : (b > c ? c : b));
                e->spk = med; e->thr = med * 3 / 8; if (e->thr < MIN_THR_UV) e->thr = MIN_THR_UV;
                e->learned = true; e->t_last_r = t; e->have_r = false;
            }
        }
        return false;
    }

    /* --- uzun süre R yok: eşiği gevşet, sonra yeniden öğren --- */
    uint32_t since = t - e->t_last_r;
    if (!e->tracking && since > 4u * max_rr_ms(e)) { relearn(e, t); return false; }
    if (!e->tracking && since > 2u * max_rr_ms(e) && e->thr > MIN_THR_UV && (since % max_rr_ms(e)) == 0u) {
        e->spk /= 2; e->thr = e->spk * 3 / 8; if (e->thr < MIN_THR_UV) e->thr = MIN_THR_UV;
    }

    /* --- gürültü sayacı penceresi (1 s) --- */
    if (t - e->reject_t0 >= 1000u) { e->noisy = e->reject > 5u; e->reject = 0; e->reject_t0 = t; }

    bool refractory = e->have_r && since < e->par->refr_ms;
    if (!e->tracking) {
        if (ad >= e->thr) { if (e->above < 255u) e->above++; } else e->above = 0;
        if (e->above >= 2u) {
            if (refractory) {
                if (since > e->par->qrs_ms && e->above == 2u) e->reject++;   /* QRS kuyruğu dışında eşik aşımı = gürültü */
            } else {
                e->tracking = true; e->t_start = t; e->t_max = tf; e->max_hp = hp; e->max_hp_abs = iabs(hp); e->max_ad = ad;
            }
        }
        return false;
    }

    /* --- QRS içinde tepe izleme --- */
    if (ad > e->max_ad) e->max_ad = ad;
    if (iabs(hp) > e->max_hp_abs) { e->max_hp_abs = iabs(hp); e->max_hp = hp; e->t_max = tf; }
    bool fell = iabs(hp) * 2 < e->max_hp_abs && tf != e->t_max;
    if (!fell && t - e->t_start < e->par->qrs_ms) return false;

    e->tracking = false; e->above = 0;
    uint32_t rr = e->have_r ? e->t_max - e->t_last_r : 0u;
    e->spk = (e->spk * 7 + e->max_ad) / 8;
    e->thr = e->spk * 3 / 8; if (e->thr < MIN_THR_UV) e->thr = MIN_THR_UV;
    if (e->have_r && rr >= min_rr_ms(e) && rr <= max_rr_ms(e)) {
        e->rr[e->rr_i] = (uint16_t)rr; e->rr_i = (uint8_t)((e->rr_i + 1u) % 5u); if (e->n_rr < 5u) e->n_rr++;
    } else if (e->have_r) {
        e->n_rr = 0; e->rr_i = 0;           /* aralık dışı R-R: ritim bilgisi sıfırlanır */
    }
    e->t_last_r = e->t_max; e->have_r = true;
    if (evt) {
        evt->t_ms = e->t_max; evt->rr_ms = (uint16_t)(rr > 65535u ? 65535u : rr);
        int32_t a = e->max_hp; if (a > 32767) a = 32767; if (a < -32768) a = -32768;
        evt->amp_uv = (int16_t)a;
    }
    return true;
}

uint16_t km_ecg_rr_median_ms(const km_ecg_t *e)
{
    if (e->n_rr < 3u) return 0;
    uint16_t v[5]; uint8_t n = e->n_rr;
    for (uint8_t i = 0; i < n; i++) v[i] = e->rr[i];
    for (uint8_t i = 1; i < n; i++) { uint16_t k = v[i]; int j = (int)i - 1; while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; } v[j + 1] = k; }
    return v[n / 2u];
}

uint16_t km_ecg_hr_bpm(const km_ecg_t *e)
{
    uint16_t rr = km_ecg_rr_median_ms(e);
    if (!rr || e->lead_mask) return 0;
    if (e->t_now - e->t_last_r > 2u * max_rr_ms(e)) return 0;
    return (uint16_t)((60000u + rr / 2u) / rr);
}

km_ecgq_t km_ecg_quality(const km_ecg_t *e)
{
    if (e->lead_mask) return KM_ECGQ_LEADOFF;
    if (!e->learned || km_ecg_hr_bpm(e) == 0u) return KM_ECGQ_NOSIG;
    if (e->noisy) return KM_ECGQ_NOISE;
    return KM_ECGQ_OK;
}
