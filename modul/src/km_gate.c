#include "km_gate.h"
#include "km_ecg.h"
#include <string.h>

void km_gate_init(km_gate_t *g) { memset(g, 0, sizeof *g); g->cfg.src = KM_SRC_OFF; g->cfg_crc = km_cfg_crc(&g->cfg); }

static void close_win(km_gate_t *g) { g->open = false; g->scheduled = false; }

km_cfg_result_t km_gate_set_cfg(km_gate_t *g, const km_cfg_t *c, uint8_t rsrc, km_cfg_ack_t *ack)
{
    km_cfg_result_t res = KM_CFG_OK;
    const km_species_par_t *sp = km_species_par(c->species);
    uint16_t min_dly = 0;
    if (c->src > KM_SRC_BOTH) res = KM_CFG_BAD_SRC;
    else if (!sp) res = KM_CFG_BAD_SPECIES;
    else if (c->rph > KM_RPH_INS) res = KM_CFG_BAD_RPH;
    else {
        /* EKG'de pencere, R kararı verildikten sonra açılabilir: en küçük DLY = en uzun karar gecikmesi */
        if (c->src == KM_SRC_ECG || c->src == KM_SRC_BOTH) min_dly = sp->qrs_ms;
        if (c->src != KM_SRC_OFF) {
            if (c->dly_ms < min_dly) res = KM_CFG_DLY_SMALL;
            else if (c->dly_ms > KM_DLY_MAX_MS) res = KM_CFG_DLY_LARGE;
            else if (c->win_ms < KM_WIN_MIN_MS || c->win_ms > KM_WIN_MAX_MS) res = KM_CFG_WIN_RANGE;
            else if ((c->src == KM_SRC_RESP || c->src == KM_SRC_BOTH) && rsrc == KM_RSRC_NONE) res = KM_CFG_NO_RSRC;
        }
    }
    if (res == KM_CFG_OK) {
        close_win(g);                    /* ayar değişince açık pencere kapanır */
        g->cfg = *c; g->have_cfg = true; g->cfg_crc = km_cfg_crc(c);
        g->resp_prev = false; g->resp_armed = false;
    }
    if (ack) { ack->result = (uint8_t)res; ack->rsv = 0; ack->cfg_crc = g->cfg_crc; ack->min_dly_ms = min_dly; ack->max_win_ms = KM_WIN_MAX_MS; }
    return res;
}

void km_gate_on_r(km_gate_t *g, uint32_t now, const km_revt_t *evt, bool ecg_ok)
{
    if (g->cfg.src != KM_SRC_ECG && g->cfg.src != KM_SRC_BOTH) return;
    close_win(g);                        /* yeni atım: önceki pencere biter */
    if (!ecg_ok) return;
    uint32_t open_at = evt->t_ms + g->cfg.dly_ms;
    if ((int32_t)(open_at - now) < 0) { if (g->late < 0xFFFFu) g->late++; return; }   /* geç kalındı: bu atım atlanır */
    g->open_at = open_at; g->close_at = open_at + g->cfg.win_ms; g->scheduled = true;
}

void km_gate_tick(km_gate_t *g, uint32_t now, bool ecg_ok, bool resp_ok, bool phase_ok, uint16_t plateau_remain)
{
    (void)plateau_remain;
    switch (g->cfg.src) {
    case KM_SRC_ECG:
    case KM_SRC_BOTH: {
        bool both = g->cfg.src == KM_SRC_BOTH;
        if (!ecg_ok || (both && !resp_ok)) { close_win(g); break; }
        if (g->scheduled && !g->open && (int32_t)(now - g->open_at) >= 0) {
            if (!both || phase_ok) { g->open = true; if (g->windows < 0xFFFFu) g->windows++; }
            else g->scheduled = false;                 /* solunum düzlüğünde değil: bu atım atlanır */
        }
        if (g->open && ((int32_t)(now - g->close_at) >= 0 || (both && !phase_ok))) close_win(g);
        break;
    }
    case KM_SRC_RESP:
        if (!resp_ok) { close_win(g); g->resp_prev = false; g->resp_armed = false; break; }
        if (phase_ok && !g->resp_prev) { g->resp_t0 = now; g->resp_armed = true; }
        if (!phase_ok) { close_win(g); g->resp_armed = false; }
        if (g->resp_armed && !g->open && (int32_t)(now - (g->resp_t0 + g->cfg.dly_ms)) >= 0) {
            g->open = true; g->resp_armed = false; g->open_at = now; g->close_at = now + g->cfg.win_ms;
            if (g->windows < 0xFFFFu) g->windows++;
        }
        if (g->open && (int32_t)(now - g->close_at) >= 0) close_win(g);
        g->resp_prev = phase_ok;
        break;
    default:
        close_win(g);
        break;
    }
}

bool km_gate_is_open(const km_gate_t *g) { return g->open; }

uint16_t km_gate_remain_ms(const km_gate_t *g, uint32_t now, uint16_t plateau_remain)
{
    if (!g->open) return 0;
    int32_t rem = (int32_t)(g->close_at - now);
    if (rem < 0) rem = 0;
    /* solunumlu kaynaklarda düzlüğün tahmini kalanı da sınırlar (tutucu taraf) */
    if ((g->cfg.src == KM_SRC_RESP || g->cfg.src == KM_SRC_BOTH) && (int32_t)plateau_remain < rem) rem = plateau_remain;
    return (uint16_t)rem;
}
