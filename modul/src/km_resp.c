#include "km_resp.h"
#include <string.h>

/* Tür tablosu – anestezi altında tipik değerler (yaklaşık):
 *   fare  : nefes 0,15–0,25 mL, 50–150 /dk (uyanık ≤ 300) → süre 0,2–2 s, düzlük ≈ 0,15–0,25 s
 *   sıçan : 1,5–2,5 mL, 40–90 /dk                      → 0,3–3 s
 *   kedi  : 30–60 mL, 10–30 /dk                         → 1–10 s
 *   köpek : 100–500 mL, 6–25 /dk                        → 1,2–12 s
 * En küçük genlikler başlangıç değeridir; ilk kartlarda gerçek yastık / T parçasıyla ayarlanır.
 * Akış tümlevi 0,1 Pa × örnek; τ türün birkaç nefesi kadar (taban kayması nefes sinyalini bozmaz). */
static const km_resp_par_t PAR[KM_SP_COUNT] = {
    [KM_SP_MOUSE] = {  200,  2000, 2, 2, 6, {  5,  20,  5 } },
    [KM_SP_RAT]   = {  300,  3000, 3, 3, 7, { 10,  50, 10 } },
    [KM_SP_CAT]   = { 1000, 10000, 5, 5, 8, { 20, 200, 20 } },
    [KM_SP_DOG]   = { 1200, 12000, 5, 5, 9, { 20, 400, 20 } },
    [KM_SP_OTHER] = {  240, 10000, 5, 5, 8, { 20, 500, 20 } },   /* rev 1 davranışı */
};

const km_resp_par_t *km_resp_par(uint8_t species) { return &PAR[species < KM_SP_COUNT ? species : KM_SP_OTHER]; }

void km_resp_init(km_resp_t *r, uint8_t rsrc, bool invert, uint8_t species)
{
    memset(r, 0, sizeof *r);
    r->par = km_resp_par(species);
    r->rsrc = rsrc; r->invert = invert;
    r->min_amp = (rsrc >= KM_RSRC_PILLOW && rsrc <= KM_RSRC_IMP ? r->par->min_amp[rsrc - 1u] : 20) * KM_RESP_Q;
    r->period_ms = 0;   /* ilk geçerli nefese kadar hız yok (önceden 3000: türe uymayan nefeste sahte 20/dk görünüyordu) */
}

bool km_resp_valid(const km_resp_t *r)
{
    if (r->rsrc == KM_RSRC_NONE || !r->have_cross) return false;
    if (r->env_max - r->env_min < r->min_amp) return false;
    if (r->period_ms < r->par->period_min_ms || r->period_ms > r->par->period_max_ms) return false;
    return (r->t_now - r->t_cross) <= 2u * (uint32_t)r->period_ms + 2000u;
}

uint16_t km_resp_rate_x10(const km_resp_t *r) { return km_resp_valid(r) ? (uint16_t)(600000u / r->period_ms) : 0u; }

bool km_resp_phase_ok(const km_resp_t *r, uint8_t rph) { return rph == KM_RPH_INS ? r->ins_ok : r->exp_ok; }

uint16_t km_resp_plateau_remain_ms(const km_resp_t *r)
{
    if (!r->plateau) return 0;
    uint16_t last = r->last_plateau_ms[r->plateau - 1u];
    if (!last) return 0;
    uint32_t budget = (uint32_t)last * 3u / 4u, used = r->t_now - r->plateau_t0;
    return used >= budget ? 0u : (uint16_t)(budget - used);
}

bool km_resp_sample(km_resp_t *r, uint32_t t, int32_t x, km_respevt_t *evt)
{
    r->t_now = t;
    if (r->rsrc == KM_RSRC_NONE) return false;
    if (r->rsrc == KM_RSRC_FLOW) { r->vol += x; r->vol -= r->vol / (1 << r->par->flow_k); x = r->vol; }
    if (r->invert) x = -x;
    x *= KM_RESP_Q;                      /* iç çözünürlük: giriş birimi × 16 */
    uint8_t man = r->par->ma_n;
    r->ma[r->mi] = x; r->mi = (uint8_t)((r->mi + 1u) % man); if (r->n < man) r->n++;
    int32_t sum = 0; for (uint8_t i = 0; i < r->n; i++) sum += r->ma[i];
    int32_t s = sum / (int32_t)r->n;
    if (r->n == 1u) { r->env_min = r->env_max = s; for (unsigned i = 0; i < 6; i++) r->s_hist[i] = s; }
    r->shi = (uint8_t)((r->shi + 1u) % 6u);
    r->s_hist[r->shi] = s; r->s = s;
    int32_t s_old = r->s_hist[(r->shi + 6u - r->par->slope_n) % 6u];   /* slope_n örnek önceki değer */

    /* zarf: nefes süresine bağlı yavaş çözülme (4 nefeste bir genlik kadar) */
    int32_t amp = r->env_max - r->env_min;
    int32_t per_samples = (int32_t)(r->period_ms ? r->period_ms : r->par->period_max_ms / 2u) / 20; if (per_samples < 12) per_samples = 12;
    int32_t decay = amp / (4 * per_samples); if (decay < 1 && amp > 0) decay = 1;
    if (s > r->env_max) r->env_max = s; else if (r->env_max - decay > s) r->env_max -= decay;
    if (s < r->env_min) r->env_min = s; else if (r->env_min + decay < s) r->env_min += decay;
    amp = r->env_max - r->env_min;

    int32_t lvl = amp > 0 ? (s - r->env_min) * 255 / amp : 0;
    if (lvl < 0) lvl = 0;
    if (lvl > 255) lvl = 255;
    r->level = (uint8_t)lvl;
    int32_t slope = s - s_old; if (slope < 0) slope = -slope;
    r->quiet = amp > 0 && slope * 8 < amp;

    /* nefes sayımı: %40 / %60 histerezisle yükselen geçiş */
    bool breath = false;
    if (!r->high && lvl > 153 && amp >= r->min_amp) {
        r->high = true;
        if (r->have_cross) {
            uint32_t p = t - r->t_cross;
            if (p >= r->par->period_min_ms && p <= r->par->period_max_ms) { r->period_ms = (uint16_t)p; breath = true; }
        }
        r->t_cross = t; r->have_cross = true;
    } else if (r->high && lvl < 102) {
        r->high = false;
    }

    bool valid = km_resp_valid(r);
    bool exp_ok = valid && lvl < 77 && r->quiet;
    bool ins_ok = valid && lvl > 178 && r->quiet;
    uint8_t now_p = exp_ok ? 1u : (ins_ok ? 2u : 0u);
    if (now_p != r->plateau) {
        if (r->plateau) {
            uint32_t len = t - r->plateau_t0;
            r->last_plateau_ms[r->plateau - 1u] = (uint16_t)(len > 65535u ? 65535u : len);
        }
        r->plateau = now_p; r->plateau_t0 = t;
    }
    r->exp_ok = exp_ok; r->ins_ok = ins_ok;

    if (breath && evt) { evt->t_ms = t; evt->period_ms = r->period_ms; evt->phase = KM_RPH_INS; evt->level = r->level; }
    return breath;
}
