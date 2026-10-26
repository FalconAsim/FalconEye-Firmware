/*
 * TGP v1 - satır tabanlı komut ayrıştırıcı
 *
 * Kayan nokta printf kullanılmaz (STM32 newlib-nano için); sayılar sabit
 * noktalı olarak elle biçimlendirilir.
 */
#include "proto.h"
#include "error_codes.h"
#include <string.h>

/* ---------- küçük yardımcılar ---------- */

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

/* dec ondalık basamakla (0..2) pozitif sayı */
static void w_fix(wbuf_t *w, float v, int dec)
{
    if (!(v >= 0.0f)) v = 0.0f;
    const uint32_t mul = dec == 2 ? 100u : dec == 1 ? 10u : 1u;
    const uint32_t x = (uint32_t)(v * (float)mul + 0.5f);
    w_uint(w, x / mul);
    if (dec > 0) {
        w_str(w, ".");
        const uint32_t frac = x % mul;
        if (dec == 2 && frac < 10u) w_str(w, "0");
        w_uint(w, frac);
    }
}

static void w_code(wbuf_t *w, const fe_error_t *e)
{
    if (!e) { w_str(w, "E000"); return; }
    char t[2] = { e->prefix, '\0' };
    w_str(w, t);
    if (e->code < 100u) w_str(w, "0");
    if (e->code < 10u) w_str(w, "0");
    w_uint(w, e->code);
}

static size_t w_done(wbuf_t *w, char *start)
{
    w_str(w, "\r\n");
    return (size_t)(w->p - start);
}

uint8_t tgp_checksum(const char *s, size_t len)
{
    uint8_t x = 0;
    for (size_t i = 0; i < len; i++) x ^= (uint8_t)s[i];
    return x;
}

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* "70", "70.5" -> değer; geçersizse false */
static bool parse_num(const char *s, size_t n, float *out)
{
    if (n == 0 || n > 8) return false;
    float v = 0.0f, scale = 0.0f;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '.') { if (scale != 0.0f) return false; scale = 1.0f; continue; }
        if (s[i] < '0' || s[i] > '9') return false;
        if (scale != 0.0f) { scale *= 0.1f; v += (float)(s[i] - '0') * scale; }
        else v = v * 10.0f + (float)(s[i] - '0');
    }
    *out = v;
    return true;
}

static bool tok_eq(const char *t, size_t n, const char *lit)
{
    return strlen(lit) == n && strncmp(t, lit, n) == 0;
}

/* ---------- API ---------- */

void tgp_init(tgp_ctx_t *c, exposure_sm_t *sm, const char *fw_version)
{
    memset(c, 0, sizeof(*c));
    c->sm = sm;
    c->fw_version = fw_version;
}

void tgp_tick(tgp_ctx_t *c, uint32_t dt_ms)
{
    if (c->since_rx_ms < 0xFFFF0000u) c->since_rx_ms += dt_ms;
    if (c->since_rx_ms > TGP_HEARTBEAT_TIMEOUT_MS) {
        c->link_up = false;
        c->pc_detector_ready = false;   /* bağlantı yoksa "hazır" bilgisi geçersiz */
    }
}

static size_t err_reply(char *reply, size_t max, uint32_t code, const char *text)
{
    wbuf_t w = { reply, max };
    w_str(&w, "ERR ");
    w_uint(&w, code);
    w_str(&w, " ");
    w_str(&w, text);
    return w_done(&w, reply);
}

static size_t reply_tube_err(char *reply, size_t max, tube_result_t r)
{
    const fe_error_t *e = fe_error_for(END_TUBE_REJECT, r);
    wbuf_t w = { reply, max };
    w_str(&w, "ERR ");
    w_uint(&w, e ? e->code : TGP_ERR_RANGE);
    w_str(&w, " ");
    w_str(&w, e ? e->title : "range");
    return w_done(&w, reply);
}

static const char *state_name(sm_state_t s)
{
    switch (s) {
    case ST_IDLE:     return "IDLE";
    case ST_PREP:     return "PREP";
    case ST_READY:    return "READY";
    case ST_EXPOSING: return "EXPOSING";
    case ST_LOCKED:   return "LOCKED";
    case ST_FAULT:    return "FAULT";
    }
    return "?";
}

static void w_params(wbuf_t *w, const exposure_params_t *p)
{
    w_str(w, " KV="); w_fix(w, p->kv, 1);
    w_str(w, " MA="); w_fix(w, p->ma, 1);
    w_str(w, " MS="); w_uint(w, p->time_ms);
    w_str(w, p->focus == FOCUS_SMALL ? " FOCUS=S" : " FOCUS=L");
    w_str(w, p->mode == MODE_FLUORO ? " MODE=FLU" : " MODE=RAD");
}

static size_t cmd_set(tgp_ctx_t *c, const char *args, char *reply, size_t max)
{
    exposure_params_t p = c->sm->params;
    bool have_kv = false, have_ma = false, have_ms = false;
    const char *s = args;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        const char *k = s;
        while (*s && *s != '=' && *s != ' ') s++;
        if (*s != '=') return err_reply(reply, max, TGP_ERR_SYNTAX, "syntax");
        const size_t kn = (size_t)(s - k);
        const char *v = ++s;
        while (*s && *s != ' ') s++;
        const size_t vn = (size_t)(s - v);
        float num;
        if (tok_eq(k, kn, "KV")) {
            if (!parse_num(v, vn, &num)) return err_reply(reply, max, TGP_ERR_SYNTAX, "KV");
            p.kv = num; have_kv = true;
        } else if (tok_eq(k, kn, "MA")) {
            if (!parse_num(v, vn, &num)) return err_reply(reply, max, TGP_ERR_SYNTAX, "MA");
            p.ma = num; have_ma = true;
        } else if (tok_eq(k, kn, "MS")) {
            if (!parse_num(v, vn, &num) || num > 600000.0f)
                return err_reply(reply, max, TGP_ERR_SYNTAX, "MS");
            p.time_ms = (uint32_t)(num + 0.5f); have_ms = true;
        } else if (tok_eq(k, kn, "FOCUS")) {
            if (tok_eq(v, vn, "L")) p.focus = FOCUS_LARGE;
            else if (tok_eq(v, vn, "S")) p.focus = FOCUS_SMALL;
            else return err_reply(reply, max, TGP_ERR_SYNTAX, "FOCUS");
        } else if (tok_eq(k, kn, "MODE")) {
            if (tok_eq(v, vn, "RAD")) p.mode = MODE_RADIOGRAPHY;
            else if (tok_eq(v, vn, "FLU")) p.mode = MODE_FLUORO;
            else return err_reply(reply, max, TGP_ERR_SYNTAX, "MODE");
        } else {
            return err_reply(reply, max, TGP_ERR_SYNTAX, "key");
        }
    }
    (void)have_kv; (void)have_ma; (void)have_ms;   /* eksik anahtarlar mevcut değerini korur */

    if (p.mode == MODE_FLUORO && !c->sm->cfg.fluoro_enabled) {
        return err_reply(reply, max, TGP_ERR_DISABLED, "fluoro disabled");
    }
    /* Parametre sınırları (soğuma/ısı durumu hariç; onlar çekim anında kontrol edilir) */
    tube_state_t cold = c->sm->tube;    /* güç sınırı dahil cihaz ayarları */
    cold.heat_j = 0.0f;
    cold.cooldown_ms = 0u;
    const tube_result_t tr = tube_check(&cold, &p);
    if (tr != TUBE_OK) return reply_tube_err(reply, max, tr);

    if (!sm_set_params(c->sm, &p)) return err_reply(reply, max, TGP_ERR_BUSY, "busy");

    wbuf_t w = { reply, max };
    w_str(&w, "OK SET");
    w_params(&w, &c->sm->params);
    return w_done(&w, reply);
}

static void w_state_body(wbuf_t *w, const exposure_sm_t *sm)
{
    w_str(w, " ");
    w_str(w, state_name(sm->state));
    w_str(w, " ERR=");
    w_code(w, (sm->state == ST_FAULT || sm->state == ST_LOCKED)
                  ? fe_error_for(sm->last_reason, sm->last_tube_result) : NULL);
    w_str(w, " HEAT=");
    w_uint(w, (uint32_t)(tube_heat_fraction(&sm->tube) * 100.0f + 0.5f));
    w_str(w, " COOL=");
    w_uint(w, (sm->tube.cooldown_ms + 999u) / 1000u);
}

size_t tgp_fmt_event_state(const exposure_sm_t *sm, char *out, size_t max)
{
    wbuf_t w = { out, max };
    w_str(&w, "EVT STATE");
    w_state_body(&w, sm);
    return w_done(&w, out);
}

static void w_exposure_body(wbuf_t *w, const exposure_record_t *r)
{
    w_params(w, &r->set);
    w_str(w, " AKV="); w_fix(w, r->kv_avg, 1);
    w_str(w, " AMA="); w_fix(w, r->ma_avg, 1);
    w_str(w, " AMS="); w_uint(w, r->actual_ms);
    w_str(w, " IRR="); w_uint(w, r->irr_ms);
    w_str(w, " MAS="); w_fix(w, r->mas_measured, 2);
    w_str(w, " END=");
    w_code(w, fe_error_for(r->reason, TUBE_OK));
}

size_t tgp_fmt_event_exposure(const exposure_record_t *r, char *out, size_t max)
{
    wbuf_t w = { out, max };
    w_str(&w, "EVT EXPOSURE");
    w_exposure_body(&w, r);
    return w_done(&w, out);
}

size_t tgp_handle_line(tgp_ctx_t *c, const char *line, char *reply, size_t max)
{
    char buf[TGP_LINE_MAX];
    size_t n = strnlen(line, TGP_LINE_MAX);
    if (n >= TGP_LINE_MAX) return err_reply(reply, max, TGP_ERR_SYNTAX, "too long");
    memcpy(buf, line, n);
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ')) buf[--n] = '\0';

    /* İsteğe bağlı sağlama toplamı: ...*HH */
    char *star = strrchr(buf, '*');
    if (star) {
        if (star + 3 != buf + n) return err_reply(reply, max, TGP_ERR_CHECKSUM, "checksum");
        const int h = hexv(star[1]), l = hexv(star[2]);
        if (h < 0 || l < 0 || (uint8_t)(h * 16 + l) != tgp_checksum(buf, (size_t)(star - buf)))
            return err_reply(reply, max, TGP_ERR_CHECKSUM, "checksum");
        *star = '\0';
        n = (size_t)(star - buf);
    }

    c->since_rx_ms = 0;
    c->link_up = true;

    char *args = strchr(buf, ' ');
    size_t cn = args ? (size_t)(args - buf) : n;
    if (args) args++; else args = buf + n;

    wbuf_t w = { reply, max };
    if (tok_eq(buf, cn, "PING") || tok_eq(buf, cn, "HB")) {
        w_str(&w, "OK");
        return w_done(&w, reply);
    }
    if (tok_eq(buf, cn, "VER")) {
        w_str(&w, "OK VER FW=");
        w_str(&w, c->fw_version ? c->fw_version : "?");
        w_str(&w, " PROTO=");
        w_uint(&w, TGP_VERSION);
        return w_done(&w, reply);
    }
    if (tok_eq(buf, cn, "SET")) return cmd_set(c, args, reply, max);
    if (tok_eq(buf, cn, "GET")) {
        const size_t an = strlen(args);
        if (tok_eq(args, an, "SET")) {
            w_str(&w, "OK SET");
            w_params(&w, &c->sm->params);
            return w_done(&w, reply);
        }
        if (tok_eq(args, an, "STATUS")) {
            w_str(&w, "OK STATUS");
            w_state_body(&w, c->sm);
            w_str(&w, c->link_up ? " LINK=1" : " LINK=0");
            return w_done(&w, reply);
        }
        if (tok_eq(args, an, "LAST")) {
            if (!c->sm->last_record.valid) return err_reply(reply, max, TGP_ERR_RANGE, "no exposure");
            w_str(&w, "OK EXPOSURE");
            w_exposure_body(&w, &c->sm->last_record);
            return w_done(&w, reply);
        }
        return err_reply(reply, max, TGP_ERR_SYNTAX, "GET what");
    }
    if (tok_eq(buf, cn, "DETREADY")) {
        const size_t an = strlen(args);
        if (tok_eq(args, an, "1")) c->pc_detector_ready = true;
        else if (tok_eq(args, an, "0")) c->pc_detector_ready = false;
        else return err_reply(reply, max, TGP_ERR_SYNTAX, "DETREADY 0|1");
        w_str(&w, "OK DETREADY ");
        w_str(&w, c->pc_detector_ready ? "1" : "0");
        return w_done(&w, reply);
    }
    if (tok_eq(buf, cn, "ACK") || tok_eq(buf, cn, "RESET")) {
        return err_reply(reply, max, TGP_ERR_LOCAL_ONLY, "local only");
    }
    if (tok_eq(buf, cn, "EXPOSE") || tok_eq(buf, cn, "XRAY") || tok_eq(buf, cn, "PREP")) {
        /* Bilerek desteklenmez: ışınlama yalnızca el butonu/pedal ile */
        return err_reply(reply, max, TGP_ERR_LOCAL_ONLY, "exposure only by hand switch");
    }
    return err_reply(reply, max, TGP_ERR_UNKNOWN, "unknown");
}
