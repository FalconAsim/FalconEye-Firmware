#include "km_app.h"
#include <string.h>

#define RBASE_MIN 200     /* yastık / kemer taban basıncı 0,2 kPa (Pa) */
#define RBASE_MAX 9000    /* 9 kPa – sensör 0–10 kPa */

static void send(km_app_t *a, uint32_t id, const uint8_t *d, uint8_t n) { if (a->tx) (void)a->tx(a->ctx, id, d, n); }

void km_app_init(km_app_t *a, km_tx_fn tx, void *ctx, const km_version_t *ver)
{
    memset(a, 0, sizeof *a);
    a->tx = tx; a->ctx = ctx;
    if (ver) a->ver = *ver;
    a->ver.proto = KM_PROTO_VERSION;
    km_ecg_init(&a->ecg, KM_SP_OTHER);
    km_resp_init(&a->resp, KM_RSRC_NONE, false, KM_SP_OTHER);
    km_gate_init(&a->gate);
}

/* tür henüz gelmediyse (yapılandırma yok) KM_SP_OTHER – önceki tek ayar */
static void resp_reinit(km_app_t *a)
{
    km_resp_init(&a->resp, a->rsrc, (a->gate.cfg.flags & KM_CFGF_RESP_INVERT) != 0u,
                 a->gate.have_cfg ? a->gate.cfg.species : (uint8_t)KM_SP_OTHER);
}

void km_app_set_rsrc(km_app_t *a, uint8_t rsrc)
{
    if (rsrc == a->rsrc) return;
    a->rsrc = rsrc;
    resp_reinit(a);
    a->wresp_n = 0;
}

void km_app_set_hw_err(km_app_t *a, uint8_t e) { a->err_hw = (uint8_t)(e & (KM_ERR_ADS | KM_ERR_ABP | KM_ERR_SDP)); }

static bool ecg_ok(const km_app_t *a) { return !(a->err_hw & KM_ERR_ADS) && km_ecg_quality(&a->ecg) == KM_ECGQ_OK; }

static uint8_t err_bits(const km_app_t *a)
{
    uint8_t e = a->err_hw;
    if (!a->gate.have_cfg) e |= KM_ERR_CFG;
    if (!a->hb_seen || a->t_now - a->hb_ms > KM_HB_TIMEOUT_MS) e |= KM_ERR_HB;
    if (a->rsrc == KM_RSRC_PILLOW && a->resp.n && (a->resp.env_min < RBASE_MIN * KM_RESP_Q || a->resp.env_min > RBASE_MAX * KM_RESP_Q)) e |= KM_ERR_RBASE;
    return e;
}

void km_app_get_status(const km_app_t *a, km_status_t *s)
{
    memset(s, 0, sizeof *s);
    s->src = a->gate.cfg.src;
    s->ecg_q = (uint8_t)((a->err_hw & KM_ERR_ADS) ? KM_ECGQ_NOSIG : km_ecg_quality(&a->ecg));
    s->lead_mask = a->ecg.lead_mask;
    s->rsrc = a->rsrc;
    s->hr_bpm = km_ecg_hr_bpm(&a->ecg);
    s->rr_x10 = km_resp_rate_x10(&a->resp);
    /* kablodaki birim değişmedi (arayüz tanımı): yastık 10 Pa, akış Pa, empedans mΩ */
    int32_t b = a->resp.env_min / KM_RESP_Q;
    if (a->rsrc == KM_RSRC_PILLOW || a->rsrc == KM_RSRC_FLOW) b /= 10;
    if (b > 32767) b = 32767;
    if (b < -32768) b = -32768;
    s->rbase = (int16_t)b;
    s->cfg_crc = a->gate.cfg_crc;
    s->err = err_bits(a);
    s->wave_on = a->wave_on ? 1u : 0u;
    uint32_t up = (a->t_now - a->boot_ms) / 1000u;
    s->uptime_s = (uint16_t)(up > 65535u ? 65535u : up);
}

static void tx_win(km_app_t *a, uint32_t t, bool open)
{
    km_win_t w; uint8_t d[8];
    w.seq = ++a->win_seq; w.open = open;
    w.remain_ms = km_gate_remain_ms(&a->gate, t, km_resp_plateau_remain_ms(&a->resp));
    if (open && w.remain_ms == 0u) w.open = false;      /* kalan süre yoksa "açık" denmez */
    w.src = a->gate.cfg.src;
    w.flags = (uint8_t)((ecg_ok(a) ? KM_WINF_ECG_OK : 0u) | (km_resp_valid(&a->resp) ? KM_WINF_RESP_OK : 0u));
    w.cfg_crc = a->gate.cfg_crc;
    send(a, KM_ID_WIN, d, km_pack_win(&w, d));
    a->win_sent_open = w.open; a->win_tx_ms = t; a->win_tx_once = true;
}

void km_app_ecg_sample(km_app_t *a, uint32_t t, int32_t x, uint8_t lead_mask)
{
    if (!a->started) { a->started = true; a->boot_ms = t; a->status_ms = t; }
    a->t_now = t;
    km_ecg_set_leadoff(&a->ecg, lead_mask);
    km_revt_t ev;
    if (km_ecg_sample(&a->ecg, t, x, &ev)) {
        uint8_t d[8];
        km_gate_on_r(&a->gate, t, &ev, ecg_ok(a));
        send(a, KM_ID_R_EVT, d, km_pack_revt(&ev, d));
    }
    km_gate_tick(&a->gate, t, ecg_ok(a), km_resp_valid(&a->resp), km_resp_phase_ok(&a->resp, a->gate.cfg.rph),
                 km_resp_plateau_remain_ms(&a->resp));

    /* pencere mesajı: değişimde hemen, açıkken 2 ms'de, kapalıyken 20 ms'de bir */
    bool open = km_gate_is_open(&a->gate);
    uint32_t per = open ? KM_WIN_TX_OPEN_MS : KM_WIN_TX_CLOSED_MS;
    if (!a->win_tx_once || open != a->win_sent_open || t - a->win_tx_ms >= per) tx_win(a, t, open);

    /* dalga: 1000 → 250 örnek/s (4 örnek ortalaması), 10 örnek = 40 ms'de bir çerçeve */
    a->dec_acc += x;
    if (++a->dec_n >= 4u) {
        int32_t v = a->dec_acc / 4; a->dec_acc = 0; a->dec_n = 0;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        if (a->wecg_n == 0u) a->wecg.t_ms = t;
        a->wecg.s[a->wecg_n++] = (int16_t)v;
        if (a->wecg_n >= 10u) {
            uint8_t d[24];
            if (a->wave_on) send(a, KM_ID_WAVE_ECG, d, km_pack_wave(&a->wecg, d));
            a->wecg_n = 0;
        }
    }

    if (a->wave_on && a->t_now - a->hb_ms > KM_HB_TIMEOUT_MS) a->wave_on = false;   /* Kart A sustu: dalga kapanır */

    if (t - a->status_ms >= KM_STATUS_PERIOD_MS) {
        km_status_t s; uint8_t d[16];
        a->status_ms = t;
        km_app_get_status(a, &s);
        send(a, KM_ID_STATUS, d, km_pack_status(&s, d));
    }
}

void km_app_resp_sample(km_app_t *a, uint32_t t, int32_t x)
{
    km_respevt_t ev;
    if (km_resp_sample(&a->resp, t, x, &ev)) {
        uint8_t d[8];
        send(a, KM_ID_RESP_EVT, d, km_pack_respevt(&ev, d));
    }
    int32_t v = a->resp.s / KM_RESP_Q;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    if (a->wresp_n == 0u) a->wresp.t_ms = t;
    a->wresp.s[a->wresp_n++] = (int16_t)v;
    if (a->wresp_n >= 10u) {
        uint8_t d[24];
        if (a->wave_on && a->rsrc != KM_RSRC_NONE) send(a, KM_ID_WAVE_RESP, d, km_pack_wave(&a->wresp, d));
        a->wresp_n = 0;
    }
}

void km_app_rx(km_app_t *a, uint32_t t, uint32_t id, const uint8_t *data, uint8_t len)
{
    uint8_t d[16];
    a->t_now = t;
    switch (id) {
    case KM_ID_CFG: {
        km_cfg_t c; km_cfg_ack_t ack;
        if (!km_unpack_cfg(data, len, &c)) break;
        uint8_t old_sp = a->gate.cfg.species; uint8_t old_fl = a->gate.cfg.flags; bool had = a->gate.have_cfg;
        if (km_gate_set_cfg(&a->gate, &c, a->rsrc, &ack) == KM_CFG_OK) {
            if (!had || c.species != old_sp) km_ecg_init(&a->ecg, c.species);                 /* tür değişti: eşik yeniden öğrenilir */
            if (!had || c.species != old_sp || ((c.flags ^ old_fl) & KM_CFGF_RESP_INVERT)) resp_reinit(a);   /* tür de solunum ayarını belirler */
            tx_win(a, t, false);
        }
        send(a, KM_ID_CFG_ACK, d, km_pack_cfg_ack(&ack, d));
        break;
    }
    case KM_ID_HB_A: {
        km_hb_t hb;
        if (!km_unpack_hb(data, len, &hb)) break;
        a->hb_ms = t; a->hb_seen = true; a->wave_on = hb.wave_on != 0u;
        break;
    }
    case KM_ID_CMD: {
        km_cmd_msg_t m; km_cmd_ack_t ack;
        if (!km_unpack_cmd(data, len, &m)) break;
        ack.cmd = m.cmd; ack.result = 0;
        switch (m.cmd) {
        case KM_CMD_RESP_ZERO: resp_reinit(a); break;
        case KM_CMD_RESP_SRC: if (m.arg <= KM_RSRC_IMP) km_app_set_rsrc(a, m.arg); else ack.result = 1; break;
        case KM_CMD_SELFTEST: ack.result = a->err_hw ? 2u : 0u; break;
        case KM_CMD_VERSION: send(a, KM_ID_VERSION, d, km_pack_version(&a->ver, d)); break;
        case KM_CMD_RESET: if (m.magic == KM_CMD_MAGIC) a->reset_req = true; else ack.result = 1; break;
        case KM_CMD_BOOT:
            /* önyükleyiciye yalnız pencere kapalı ve tetikleme kapalıyken geçilir */
            if (m.magic == KM_CMD_MAGIC && a->gate.cfg.src == KM_SRC_OFF) a->boot_req = true; else ack.result = 1;
            break;
        default: ack.result = 1; break;
        }
        send(a, KM_ID_CMD_ACK, d, km_pack_cmd_ack(&ack, d));
        break;
    }
    default: break;
    }
}
