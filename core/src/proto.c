/*
 * TGP v1 (arayüz tanımı rev. 0.4) – satır tabanlı komut ayrıştırıcı
 *
 * Kayan nokta printf kullanılmaz (STM32 newlib-nano için); sayılar sabit
 * noktalı olarak elle biçimlendirilir.
 */
#include "proto.h"
#include "error_codes.h"
#include <string.h>

/* ---------- yazma yardımcıları ---------- */

typedef struct { char *p; size_t left; } wbuf_t;

static void w_str(wbuf_t *w, const char *s)
{
    while (*s && w->left > 1) { *w->p++ = *s++; w->left--; }
    *w->p = '\0';
}

static void w_uint(wbuf_t *w, uint32_t v)
{
    char t[11]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 10);
    while (n-- > 0 && w->left > 1) { *w->p++ = t[n]; w->left--; }
    *w->p = '\0';
}

static void w_uint_pad(wbuf_t *w, uint32_t v, int width)
{
    uint32_t lim = 1;
    for (int i = 1; i < width; i++) lim *= 10u;
    while (lim > 1u && v < lim) { w_str(w, "0"); lim /= 10u; }
    w_uint(w, v);
}

/* dec ondalık basamakla (0..2) pozitif sayı */
static void w_fix(wbuf_t *w, float v, int dec)
{
    if (!(v >= 0.0f)) v = 0.0f;
    if (v > 999999.0f) v = 999999.0f;
    const uint32_t mul = dec == 3 ? 1000u : dec == 2 ? 100u : dec == 1 ? 10u : 1u;
    const uint32_t x = (uint32_t)(v * (float)mul + 0.5f);
    w_uint(w, x / mul);
    if (dec > 0) {
        w_str(w, ".");
        w_uint_pad(w, x % mul, dec);
    }
}

static void w_code(wbuf_t *w, const fe_error_t *e)
{
    if (!e) { w_str(w, "E000"); return; }
    const char t[2] = { e->prefix, '\0' };
    w_str(w, t);
    w_uint_pad(w, e->code, 3);
}

uint16_t tgp_crc16(const char *s, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)(uint8_t)s[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* Çıkış: birden fazla çerçeveli satır biriktirilir */
typedef struct { char *start; size_t max; size_t len; } out_t;

static void out_line(tgp_ctx_t *c, out_t *o, const char *body)
{
    char line[TGP_LINE_MAX + 8];
    wbuf_t w = { line, sizeof line };
    w_uint_pad(&w, c->tx_seq, 4);
    c->tx_seq = (uint16_t)((c->tx_seq + 1u) % 10000u);
    w_str(&w, " ");
    w_str(&w, body);
    const size_t n = (size_t)(w.p - line);
    const uint16_t crc = tgp_crc16(line, n);
    static const char hx[] = "0123456789ABCDEF";
    const char tail[8] = { '*', hx[(crc >> 12) & 15u], hx[(crc >> 8) & 15u],
                           hx[(crc >> 4) & 15u], hx[crc & 15u], '\r', '\n', '\0' };
    w_str(&w, tail);
    const size_t total = (size_t)(w.p - line);
    if (o->len + total < o->max) {
        memcpy(o->start + o->len, line, total);
        o->len += total;
        o->start[o->len] = '\0';
    }
}

/* ---------- okuma yardımcıları ---------- */

static int hexv(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool tok_eq(const char *t, size_t n, const char *lit)
{
    return strlen(lit) == n && strncmp(t, lit, n) == 0;
}

/* "70", "70.5" -> değer; max_dec: izin verilen ondalık basamak sayısı */
static bool parse_num(const char *s, size_t n, int max_dec, float *out)
{
    if (n == 0 || n > 9) return false;
    float v = 0.0f, scale = 0.0f;
    int dec = -1;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '.') {
            if (dec >= 0 || i == 0 || i == n - 1) return false;
            dec = 0; scale = 1.0f; continue;
        }
        if (s[i] < '0' || s[i] > '9') return false;
        if (dec >= 0) {
            if (++dec > max_dec) return false;
            scale *= 0.1f; v += (float)(s[i] - '0') * scale;
        } else {
            v = v * 10.0f + (float)(s[i] - '0');
        }
    }
    *out = v;
    return true;
}

static bool parse_seq(const char *s, int *seq)
{
    int v = 0;
    for (int i = 0; i < 4; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    if (s[4] != ' ') return false;
    *seq = v;
    return true;
}

/* ---------- metin parçaları ---------- */

static const char *state_name(sm_state_t s)
{
    switch (s) {
    case ST_IDLE:     return "IDLE";
    case ST_PREP:     return "PREHEAT";
    case ST_READY:    return "READY";
    case ST_EXPOSING: return "XRAY";
    case ST_LOCKED:   return "LOCKED";
    case ST_FAULT:    return "ERROR";
    }
    return "?";
}

static void w_values(wbuf_t *w, const exposure_params_t *p, bool with_mas)
{
    w_str(w, "KV="); w_fix(w, p->kv, 1);
    w_str(w, " MA="); w_fix(w, p->ma, 0);
    w_str(w, " MS="); w_uint(w, p->time_ms);
    if (with_mas) { w_str(w, " MAS="); w_fix(w, p->ma * (float)p->time_ms / 1000.0f, 2); }
    w_str(w, p->focus == FOCUS_SMALL ? " FO=S" : " FO=L");
    w_str(w, p->mode == MODE_FLUORO ? " MODE=FLUORO" : " MODE=RAD");
}

static const fe_error_t *current_error(const exposure_sm_t *sm)
{
    return (sm->state == ST_FAULT || sm->state == ST_LOCKED)
               ? fe_error_for(sm->last_reason, sm->last_tube_result) : NULL;
}

/* ---------- API ---------- */

void tgp_init(tgp_ctx_t *c, exposure_sm_t *sm, const char *fw_version)
{
    memset(c, 0, sizeof(*c));
    c->sm = sm;
    c->fw_version = fw_version;
    c->hw_rev = "A";
    c->serial = "-";
    c->ctrl = TGP_CTRL_PC;
    c->link_sel = TGP_LINK_TGP;
    c->last_rx_seq = -1;
    c->tx_seq = 100u;
}

bool tgp_writes_allowed(const tgp_ctx_t *c)
{
    return c->ctrl == TGP_CTRL_PC && c->link_sel == TGP_LINK_TGP;
}

void tgp_link_lost(tgp_ctx_t *c)
{
    c->link_up = false;
    c->pc_detector_ready = false;   /* bağlantı yoksa "hazır" bilgisi geçersiz */
    c->last_rx_seq = -1;
}

void tgp_tick(tgp_ctx_t *c, uint32_t dt_ms)
{
    if (c->since_rx_ms < 0xFFFF0000u) c->since_rx_ms += dt_ms;
    const uint32_t limit = c->sm->state == ST_EXPOSING ? TGP_TIMEOUT_XRAY_MS : TGP_TIMEOUT_MS;
    if (c->link_up && c->since_rx_ms > limit) tgp_link_lost(c);
}

static void line_ack(tgp_ctx_t *c, out_t *o, int seq)
{
    char b[16];
    wbuf_t w = { b, sizeof b };
    w_str(&w, "ACK ");
    w_uint_pad(&w, (uint32_t)seq, 4);
    out_line(c, o, b);
}

static void line_nak(tgp_ctx_t *c, out_t *o, int seq, char prefix, uint32_t code)
{
    char b[24];
    wbuf_t w = { b, sizeof b };
    w_str(&w, "NAK ");
    w_uint_pad(&w, (uint32_t)seq, 4);
    const char t[3] = { ' ', prefix, '\0' };
    w_str(&w, t);
    w_uint_pad(&w, code, 3);
    out_line(c, o, b);
}

static void line_actual(tgp_ctx_t *c, out_t *o)
{
    char b[TGP_LINE_MAX];
    wbuf_t w = { b, sizeof b };
    w_str(&w, "ACTUAL ");
    w_values(&w, &c->sm->params, true);
    out_line(c, o, b);
}

static void line_version(tgp_ctx_t *c, out_t *o)
{
    char b[TGP_LINE_MAX];
    wbuf_t w = { b, sizeof b };
    w_str(&w, "VERSION FW="); w_str(&w, c->fw_version ? c->fw_version : "?");
    w_str(&w, " HW="); w_str(&w, c->hw_rev);
    w_str(&w, " TGP="); w_uint(&w, TGP_VERSION);
    w_str(&w, " IF="); w_str(&w, TGP_IF_REV);
    w_str(&w, " TUBE="); w_str(&w, tube_insert_name(&c->sm->tube));
    w_str(&w, c->ctrl == TGP_CTRL_PC ? " CTRL=PC" : " CTRL=MANUAL");
    w_str(&w, c->link_sel == TGP_LINK_TGP ? " LINK=TGP" : " LINK=CURRENT");
    w_str(&w, " SN="); w_str(&w, c->serial);
    const bool fl = sm_fluoro_available(c->sm), dap = c->sm->cfg.dap_meter_enabled;
    w_str(&w, fl && dap ? " OPT=FLUORO+DAP" : fl ? " OPT=FLUORO" : dap ? " OPT=DAP" : " OPT=NONE");
    out_line(c, o, b);
}

static void line_status(tgp_ctx_t *c, out_t *o)
{
    const exposure_sm_t *sm = c->sm;
    char b[TGP_LINE_MAX];
    wbuf_t w = { b, sizeof b };
    w_str(&w, "STATUS ST="); w_str(&w, state_name(sm->state));
    w_str(&w, c->ctrl == TGP_CTRL_PC ? " CTRL=PC" : " CTRL=MANUAL");
    w_str(&w, c->link_sel == TGP_LINK_TGP ? " LINK=TGP" : " LINK=CURRENT");
    w_str(&w, sm->params.mode == MODE_FLUORO ? " MODE=FLUORO" : " MODE=RAD");
    w_str(&w, " ERR="); w_code(&w, current_error(sm));
    w_str(&w, " HEAT="); w_uint(&w, (uint32_t)(tube_heat_fraction(&sm->tube) * 100.0f + 0.5f));
    w_str(&w, " COOL="); w_uint(&w, (sm->tube.cooldown_ms + 999u) / 1000u);
    w_str(&w, c->pc_detector_ready ? " DET=1" : " DET=0");
    out_line(c, o, b);
}

static void line_state(tgp_ctx_t *c, out_t *o)
{
    const exposure_sm_t *sm = c->sm;
    char b[TGP_LINE_MAX];
    wbuf_t w = { b, sizeof b };
    w_str(&w, "STATE ST="); w_str(&w, state_name(sm->state));
    w_str(&w, " ERR="); w_code(&w, current_error(sm));
    w_str(&w, " HEAT="); w_uint(&w, (uint32_t)(tube_heat_fraction(&sm->tube) * 100.0f + 0.5f));
    w_str(&w, " COOL="); w_uint(&w, (sm->tube.cooldown_ms + 999u) / 1000u);
    out_line(c, o, b);
}

static void line_done(tgp_ctx_t *c, out_t *o, const exposure_record_t *r, uint32_t xn)
{
    char b[TGP_LINE_MAX + 64];
    wbuf_t w = { b, sizeof b };
    w_str(&w, "STATE ST=DONE XN="); w_uint(&w, xn);
    w_str(&w, " KV="); w_fix(&w, r->set.kv, 1);
    w_str(&w, " MA="); w_fix(&w, r->set.ma, 0);
    w_str(&w, " MS="); w_uint(&w, r->set.time_ms);
    w_str(&w, " AKV="); w_fix(&w, r->kv_avg, 1);
    w_str(&w, " AMA="); w_fix(&w, r->ma_avg, 1);
    w_str(&w, " AMS="); w_uint(&w, r->actual_ms);
    w_str(&w, " IRR="); w_uint(&w, r->irr_ms);
    w_str(&w, " MAS="); w_fix(&w, r->mas_measured, 2);
    w_str(&w, r->kv_avg > 0.0f ? " MEAS=1" : " MEAS=0");
    w_str(&w, r->cut ? " CUT=1" : " CUT=0");
    w_str(&w, " END="); w_code(&w, fe_error_for(r->reason, TUBE_OK));
    out_line(c, o, b);
}

static void line_dose(tgp_ctx_t *c, out_t *o, const exposure_record_t *r, uint32_t xn)
{
    if (!r->dap_valid || !c->sm->cfg.dap_meter_enabled) return;
    char b[TGP_LINE_MAX];
    wbuf_t w = { b, sizeof b };
    w_str(&w, "DOSE XN="); w_uint(&w, xn);
    w_str(&w, " DAP="); w_fix(&w, r->dap_dgycm2, 3);
    w_str(&w, " SRC=METER");
    out_line(c, o, b);
}

static void lines_limits(tgp_ctx_t *c, out_t *o)
{
    struct { focus_t f; exp_mode_t m; bool on; } rows[3] = {
        { FOCUS_LARGE, MODE_RADIOGRAPHY, true },
        { FOCUS_SMALL, MODE_RADIOGRAPHY, tube_has_small_focus(&c->sm->tube) },
        { FOCUS_SMALL, MODE_FLUORO, sm_fluoro_available(c->sm) },
    };
    for (int i = 0; i < 3; i++) {
        if (!rows[i].on) continue;
        tube_limits_t l;
        tube_limits(&c->sm->tube, rows[i].f, rows[i].m, &l);
        char b[TGP_LINE_MAX];
        wbuf_t w = { b, sizeof b };
        w_str(&w, "LIMITS TUBE="); w_str(&w, tube_insert_name(&c->sm->tube));
        w_str(&w, rows[i].m == MODE_FLUORO ? " MODE=FLUORO" : " MODE=RAD");
        w_str(&w, rows[i].f == FOCUS_SMALL ? " FO=S" : " FO=L");
        w_str(&w, " KV="); w_fix(&w, l.kv_min, 1); w_str(&w, "-"); w_fix(&w, l.kv_max, 1);
        w_str(&w, " MA=1-"); w_fix(&w, l.ma_max, 0);
        w_str(&w, " MS=1-"); w_uint(&w, l.time_max_ms);
        w_str(&w, " P="); w_fix(&w, l.power_max_w, 0);
        out_line(c, o, b);
    }
}

/* ---------- komutlar ---------- */

typedef struct {
    exposure_params_t p;
    bool have_ma, have_ms, have_mas;
    float mas;
} set_req_t;

/* "KV=70.0 MA=20 ..." ayrıştırır. Hata kodu döner (0 = tamam). */
static uint32_t parse_set(const char *s, bool mode_only, set_req_t *q)
{
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        const char *k = s;
        while (*s && *s != '=' && *s != ' ') s++;
        if (*s != '=') return TGP_ERR_SYNTAX;
        const size_t kn = (size_t)(s - k);
        const char *v = ++s;
        while (*s && *s != ' ') s++;
        const size_t vn = (size_t)(s - v);
        float num;
        if (mode_only) {
            if (!tok_eq(k, kn, "M")) return TGP_ERR_SYNTAX;
            if (tok_eq(v, vn, "RAD")) q->p.mode = MODE_RADIOGRAPHY;
            else if (tok_eq(v, vn, "FLUORO")) q->p.mode = MODE_FLUORO;
            else return TGP_ERR_SYNTAX;
        } else if (tok_eq(k, kn, "KV")) {
            if (!parse_num(v, vn, 1, &num)) return TGP_ERR_SYNTAX;
            q->p.kv = num;
        } else if (tok_eq(k, kn, "MA")) {
            if (!parse_num(v, vn, 0, &num)) return TGP_ERR_SYNTAX;
            q->p.ma = num; q->have_ma = true;
        } else if (tok_eq(k, kn, "MS")) {
            if (!parse_num(v, vn, 0, &num) || num > 600000.0f) return TGP_ERR_SYNTAX;
            q->p.time_ms = (uint32_t)(num + 0.5f); q->have_ms = true;
        } else if (tok_eq(k, kn, "MAS")) {
            if (!parse_num(v, vn, 2, &num)) return TGP_ERR_SYNTAX;
            q->mas = num; q->have_mas = true;
        } else if (tok_eq(k, kn, "FO")) {
            if (tok_eq(v, vn, "L")) q->p.focus = FOCUS_LARGE;
            else if (tok_eq(v, vn, "S")) q->p.focus = FOCUS_SMALL;
            else return TGP_ERR_SYNTAX;
        } else {
            return TGP_ERR_SYNTAX;
        }
    }
    if (q->have_ms && q->have_mas) return TGP_ERR_SYNTAX;   /* ikisi birlikte olmaz */
    if (q->have_mas) {
        if (!q->have_ma || q->p.ma <= 0.0f) return TGP_ERR_SYNTAX;   /* mA'yı yazılım belirler */
        q->p.time_ms = (uint32_t)(q->mas * 1000.0f / q->p.ma + 0.5f);
    }
    return 0;
}

static void cmd_set(tgp_ctx_t *c, out_t *o, int seq, const char *args, bool mode_only)
{
    if (!tgp_writes_allowed(c)) { line_nak(c, o, seq, 'E', TGP_ERR_READONLY); return; }
    if (c->sm->state == ST_EXPOSING) { line_nak(c, o, seq, 'E', TGP_ERR_BUSY); return; }

    set_req_t q;
    memset(&q, 0, sizeof q);
    q.p = c->sm->params;
    const uint32_t perr = parse_set(args, mode_only, &q);
    if (perr) { line_nak(c, o, seq, 'E', perr); return; }
    if (mode_only) {
        /* Panelin tek tuşu ile aynı: her mod kendi son değerleriyle gelir */
        if (q.p.mode == MODE_FLUORO && !sm_fluoro_available(c->sm)) {
            line_nak(c, o, seq, 'E', TGP_ERR_DISABLED); return;
        }
        if ((c->sm->state == ST_PREP || c->sm->state == ST_READY) && q.p.mode != c->sm->params.mode) {
            line_nak(c, o, seq, 'E', 213); return;
        }
        switch (sm_select_mode(c->sm, q.p.mode)) {
        case SM_SET_OK:              break;
        case SM_SET_BUSY:            line_nak(c, o, seq, 'E', TGP_ERR_BUSY); return;
        case SM_SET_FOCUS_LOCKED:    line_nak(c, o, seq, 'E', 213); return;
        case SM_SET_FLUORO_DISABLED: line_nak(c, o, seq, 'E', TGP_ERR_DISABLED); return;
        }
        line_ack(c, o, seq);
        line_actual(c, o);
        return;
    }

    if (q.p.focus == FOCUS_SMALL && !tube_has_small_focus(&c->sm->tube)) {
        line_nak(c, o, seq, 'E', 214); return;   /* E214: bu tüpte küçük fokus yok */
    }
    if (q.p.mode == MODE_FLUORO && !sm_fluoro_available(c->sm)) {
        line_nak(c, o, seq, 'E', TGP_ERR_DISABLED); return;
    }
    if ((c->sm->state == ST_PREP || c->sm->state == ST_READY) &&
        (q.p.focus != c->sm->params.focus || q.p.mode != c->sm->params.mode)) {
        line_nak(c, o, seq, 'E', 213); return;   /* hazırlıkta fokus / mod değişmez */
    }
    /* Değer sınırları (anlık ısı / soğuma hariç; onlar çekim anında denetlenir) */
    tube_state_t cold = c->sm->tube;
    cold.heat_j = 0.0f;
    cold.cooldown_ms = 0u;
    const tube_result_t tr = tube_check(&cold, &q.p);
    if (tr != TUBE_OK) {
        const fe_error_t *e = fe_error_for(END_TUBE_REJECT, tr);
        line_nak(c, o, seq, e ? e->prefix : 'E', e ? e->code : 212u);
        return;
    }
    switch (sm_set_params_ex(c->sm, &q.p)) {
    case SM_SET_OK:              break;
    case SM_SET_BUSY:            line_nak(c, o, seq, 'E', TGP_ERR_BUSY); return;
    case SM_SET_FOCUS_LOCKED:    line_nak(c, o, seq, 'E', 213); return;
    case SM_SET_FLUORO_DISABLED: line_nak(c, o, seq, 'E', TGP_ERR_DISABLED); return;
    }
    line_ack(c, o, seq);
    line_actual(c, o);
}

static void cmd_detready(tgp_ctx_t *c, out_t *o, int seq, const char *args)
{
    const size_t an = strlen(args);
    if (tok_eq(args, an, "R=1")) c->pc_detector_ready = true;
    else if (tok_eq(args, an, "R=0")) c->pc_detector_ready = false;
    else { line_nak(c, o, seq, 'E', TGP_ERR_SYNTAX); return; }
    line_ack(c, o, seq);
}

size_t tgp_handle_line(tgp_ctx_t *c, const char *line, char *reply, size_t max)
{
    out_t o = { reply, max, 0 };
    if (max) reply[0] = '\0';

    char buf[TGP_LINE_MAX + 1];
    size_t n = strnlen(line, TGP_LINE_MAX + 1);
    int seq = -1;
    if (n > TGP_LINE_MAX) {
        c->bad_lines++;
        if (parse_seq(line, &seq)) line_nak(c, &o, seq, 'E', TGP_ERR_SYNTAX);
        return o.len;
    }
    memcpy(buf, line, n);
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n')) buf[--n] = '\0';

    const bool seq_ok = n >= 5 && parse_seq(buf, &seq);

    /* Sağlama: ...*HHHH zorunlu */
    char *star = strrchr(buf, '*');
    bool crc_ok = false;
    if (star && star + 5 == buf + n) {
        uint16_t got = 0;
        crc_ok = true;
        for (int i = 1; i <= 4; i++) {
            const int h = hexv(star[i]);
            if (h < 0) { crc_ok = false; break; }
            got = (uint16_t)((got << 4) | (uint16_t)h);
        }
        crc_ok = crc_ok && got == tgp_crc16(buf, (size_t)(star - buf));
    }
    if (!crc_ok || !seq_ok) {
        c->bad_lines++;
        if (seq_ok) line_nak(c, &o, seq, 'E', TGP_ERR_CHECKSUM);
        return o.len;
    }
    *star = '\0';
    n = (size_t)(star - buf);

    c->since_rx_ms = 0;
    c->link_up = true;

    /* Aynı SEQ ikinci kez: komut yeniden uygulanmaz, önceki cevap tekrarlanır */
    if (seq == c->last_rx_seq && c->last_reply_len > 0 && c->last_reply_len < max) {
        memcpy(reply, c->last_reply, c->last_reply_len + 1);
        return c->last_reply_len;
    }

    const char *cmd = buf + 5;
    const char *args = strchr(cmd, ' ');
    const size_t cn = args ? (size_t)(args - cmd) : strlen(cmd);
    args = args ? args + 1 : cmd + cn;

    if (tok_eq(cmd, cn, "PING")) {
        char b[16];
        wbuf_t w = { b, sizeof b };
        w_str(&w, "PONG ");
        w_uint_pad(&w, (uint32_t)seq, 4);
        out_line(c, &o, b);
    } else if (tok_eq(cmd, cn, "HELLO")) {
        line_ack(c, &o, seq);
        line_version(c, &o);
    } else if (tok_eq(cmd, cn, "VERSION?")) {
        line_version(c, &o);
    } else if (tok_eq(cmd, cn, "STATUS?")) {
        line_status(c, &o);
        line_actual(c, &o);
    } else if (tok_eq(cmd, cn, "LIMITS?")) {
        lines_limits(c, &o);
    } else if (tok_eq(cmd, cn, "LAST?")) {
        if (!c->sm->last_record.valid) line_nak(c, &o, seq, 'E', TGP_ERR_NODATA);
        else {
            line_done(c, &o, &c->sm->last_record, c->xn);
            line_dose(c, &o, &c->sm->last_record, c->xn);
        }
    } else if (tok_eq(cmd, cn, "SET")) {
        cmd_set(c, &o, seq, args, false);
    } else if (tok_eq(cmd, cn, "MODE")) {
        cmd_set(c, &o, seq, args, true);
    } else if (tok_eq(cmd, cn, "DETREADY")) {
        cmd_detready(c, &o, seq, args);
    } else if (tok_eq(cmd, cn, "EXPOSE") || tok_eq(cmd, cn, "XRAY") || tok_eq(cmd, cn, "PREP") ||
               tok_eq(cmd, cn, "ACK-FAULT") || tok_eq(cmd, cn, "RESET")) {
        /* Bilerek desteklenmez: ışınlama yalnız el butonu/pedal, onay yalnız panel */
        line_nak(c, &o, seq, 'E', TGP_ERR_LOCAL_ONLY);
    } else {
        line_nak(c, &o, seq, 'E', TGP_ERR_UNKNOWN);
    }

    c->last_rx_seq = seq;
    if (o.len < sizeof c->last_reply) {
        memcpy(c->last_reply, reply, o.len + 1);
        c->last_reply_len = o.len;
    } else {
        c->last_reply_len = 0;
    }
    return o.len;
}

/* ---------- olay satırları ---------- */

size_t tgp_fmt_actual(tgp_ctx_t *c, char *out, size_t max)
{
    out_t o = { out, max, 0 };
    line_actual(c, &o);
    return o.len;
}

size_t tgp_fmt_state(tgp_ctx_t *c, char *out, size_t max)
{
    out_t o = { out, max, 0 };
    line_state(c, &o);
    return o.len;
}

size_t tgp_fmt_done(tgp_ctx_t *c, const exposure_record_t *r, char *out, size_t max)
{
    out_t o = { out, max, 0 };
    c->xn++;
    line_done(c, &o, r, c->xn);
    return o.len;
}

size_t tgp_fmt_dose(tgp_ctx_t *c, const exposure_record_t *r, char *out, size_t max)
{
    out_t o = { out, max, 0 };
    if (max) out[0] = '\0';
    line_dose(c, &o, r, c->xn);
    return o.len;
}

size_t tgp_fmt_error(tgp_ctx_t *c, char prefix, uint16_t code, bool cleared, char *out, size_t max)
{
    out_t o = { out, max, 0 };
    char b[32];
    wbuf_t w = { b, sizeof b };
    const char t[2] = { prefix, '\0' };
    w_str(&w, "ERROR E="); w_str(&w, t); w_uint_pad(&w, code, 3);
    w_str(&w, cleared ? " CLR=1" : " CLR=0");
    out_line(c, &o, b);
    return o.len;
}

size_t tgp_fmt_limits(tgp_ctx_t *c, char *out, size_t max)
{
    out_t o = { out, max, 0 };
    lines_limits(c, &o);
    return o.len;
}
