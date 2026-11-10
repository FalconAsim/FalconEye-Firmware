/* FalconEye devreye alma – donanımdan bağımsız çekirdek (açıklama: bu_core.h) */
#include "bu_core.h"
#include <math.h>
#include <string.h>

/* ---------------- biçimleyici ---------------- */
static int put(char *buf, size_t n, size_t pos, char c)
{
    if (pos + 1 < n) { buf[pos] = c; buf[pos + 1] = '\0'; }
    return 1;
}

static int puts_(char *buf, size_t n, size_t pos, const char *s)
{
    int k = 0;
    while (*s) k += put(buf, n, pos + (size_t)k, *s++);
    return k;
}

int bu_fmt_f(char *buf, size_t n, float v, int decimals)
{
    size_t p = 0;
    if (n) buf[0] = '\0';
    if (bu_is_nan(v)) return puts_(buf, n, 0, "---");
    if (v < 0) { p += (size_t)put(buf, n, p, '-'); v = -v; }
    uint32_t scale = 1;
    for (int i = 0; i < decimals; i++) scale *= 10u;
    if (v > 4.0e9f / (float)scale) v = 4.0e9f / (float)scale;
    uint32_t x = (uint32_t)(v * (float)scale + 0.5f);
    uint32_t ip = x / scale, fp = x % scale;
    char tmp[12]; int k = 0;
    do { tmp[k++] = (char)('0' + ip % 10u); ip /= 10u; } while (ip && k < 11);
    while (k) p += (size_t)put(buf, n, p, tmp[--k]);
    if (decimals > 0) {
        p += (size_t)put(buf, n, p, '.');
        for (int i = decimals - 1; i >= 0; i--) {
            uint32_t d = fp;
            for (int j = 0; j < i; j++) d /= 10u;
            p += (size_t)put(buf, n, p, (char)('0' + d % 10u));
        }
    }
    return (int)p;
}

int bu_fmt_hex(char *buf, size_t n, uint32_t v, int digits)
{
    static const char hx[] = "0123456789ABCDEF";
    size_t p = 0;
    if (n) buf[0] = '\0';
    for (int i = digits - 1; i >= 0; i--) p += (size_t)put(buf, n, p, hx[(v >> (4 * i)) & 0xFu]);
    return (int)p;
}

bool bu_is_nan(float x) { return x != x; }

/* ---------------- sonuç ---------------- */
bu_verdict_t bu_judge(const bu_limit_t *l, float v)
{
    if (bu_is_nan(v)) return BU_FAIL;
    return (v >= l->lo && v <= l->hi) ? BU_PASS : BU_FAIL;
}

const char *bu_verdict_text(bu_verdict_t r)
{
    switch (r) {
    case BU_PASS: return "GECTI";
    case BU_FAIL: return "KALDI";
    case BU_INFO: return "BILGI";
    default:      return "ATLA ";
    }
}

static int pad(char *buf, size_t n, size_t pos, size_t col)
{
    int k = 0;
    while (pos + (size_t)k < col) k += put(buf, n, pos + (size_t)k, ' ');
    return k;
}

int bu_fmt_result(char *buf, size_t n, const bu_limit_t *l, float v, bu_verdict_t r)
{
    char f[16];
    size_t p = 0;
    if (n) buf[0] = '\0';
    p += (size_t)puts_(buf, n, p, bu_verdict_text(r));
    p += (size_t)puts_(buf, n, p, "  ");
    p += (size_t)puts_(buf, n, p, l->name);
    p += (size_t)pad(buf, n, p, 24);
    bu_fmt_f(f, sizeof f, v, 2);
    p += (size_t)pad(buf, n, p, 34 - strlen(f));
    p += (size_t)puts_(buf, n, p, f);
    p += (size_t)put(buf, n, p, ' ');
    p += (size_t)puts_(buf, n, p, l->unit);
    p += (size_t)pad(buf, n, p, 42);
    p += (size_t)puts_(buf, n, p, "[");
    bu_fmt_f(f, sizeof f, l->lo, 2); p += (size_t)puts_(buf, n, p, f);
    p += (size_t)puts_(buf, n, p, " .. ");
    bu_fmt_f(f, sizeof f, l->hi, 2); p += (size_t)puts_(buf, n, p, f);
    p += (size_t)puts_(buf, n, p, "]");
    return (int)p;
}

/* ---------------- dönüşümler ---------------- */
float bu_adc_to_v(uint16_t code, float vref, uint16_t full, float r_top, float r_bot)
{
    float v = (float)code * vref / (float)full;
    if (r_bot <= 0.0f) return v;
    return v * (r_top + r_bot) / r_bot;
}

float bu_vref_from_vrefint(uint16_t vrefint_cal, uint16_t vrefint_data, float cal_v)
{
    if (vrefint_data == 0u) return NAN;
    return cal_v * (float)vrefint_cal / (float)vrefint_data;
}

float bu_mcu_temp_c(uint16_t code, float vref, uint16_t ts_cal1, uint16_t ts_cal2,
                    float t1, float t2, float cal_v)
{
    if (ts_cal2 == ts_cal1) return NAN;
    float c = (float)code * vref / cal_v;          /* kalibrasyon gerilimine ölçekle */
    return t1 + (c - (float)ts_cal1) * (t2 - t1) / (float)(ts_cal2 - ts_cal1);
}

float bu_ntc_c(float ratio, float r_pu, float r25, float beta)
{
    if (bu_is_nan(ratio) || ratio >= 0.98f || ratio <= 0.02f) return NAN;
    float r = r_pu * ratio / (1.0f - ratio);
    float inv = 1.0f / 298.15f + logf(r / r25) / beta;
    return 1.0f / inv - 273.15f;
}

/* ---------------- CAN ---------------- */
void bu_hb_pack(const bu_hb_t *h, uint8_t o[8])
{
    o[0] = (uint8_t)h->board;
    o[1] = h->seq;
    o[2] = (uint8_t)(h->uptime_s & 0xFFu);
    o[3] = (uint8_t)(h->uptime_s >> 8);
    o[4] = h->status;
    o[5] = (uint8_t)h->temp_c;
    o[6] = (uint8_t)(h->aux & 0xFFu);
    o[7] = (uint8_t)(h->aux >> 8);
}

bool bu_hb_unpack(const uint8_t in[8], bu_hb_t *h)
{
    if (in[0] != 'A' && in[0] != 'B' && in[0] != 'M') return false;
    h->board = (char)in[0];
    h->seq = in[1];
    h->uptime_s = (uint16_t)(in[2] | (in[3] << 8));
    h->status = in[4];
    h->temp_c = (int8_t)in[5];
    h->aux = (uint16_t)(in[6] | (in[7] << 8));
    return true;
}

void bu_peer_seen(bu_peer_t *p, const bu_hb_t *h, uint32_t now_ms)
{
    p->last = *h; p->last_ms = now_ms; p->ever = true;
}

bool bu_peer_alive(const bu_peer_t *p, uint32_t now_ms, uint32_t timeout_ms)
{
    return p->ever && (uint32_t)(now_ms - p->last_ms) <= timeout_ms;
}

/* ---------------- çıkış bekçisi ---------------- */
bool bu_streq(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
    }
    return *a == *b;
}

static const char *const FORBIDDEN[] = { "XRAY_REQ", "XRAY", "XRAY_PERMIT", "XRAY_ON" };

bool bu_guard_table_ok(const bu_out_def_t *defs, int n)
{
    if (n > BU_MAX_OUT) return false;
    for (int i = 0; i < n; i++) {
        for (size_t k = 0; k < sizeof FORBIDDEN / sizeof FORBIDDEN[0]; k++)
            if (bu_streq(defs[i].name, FORBIDDEN[k])) return false;
        if (defs[i].grp == BU_GRP_J2J4 && (defs[i].max_on_ms == 0u || defs[i].max_on_ms > BU_J2_MAX_ON_MS)) return false;
    }
    return true;
}

void bu_guard_init(bu_guard_t *g, const bu_out_def_t *defs, int n)
{
    memset(g, 0, sizeof *g);
    if (!bu_guard_table_ok(defs, n)) { g->defs = defs; g->n = 0; return; }   /* hatalı tablo: hiçbir çıkış yok */
    g->defs = defs; g->n = n;
}

int bu_guard_find(const bu_guard_t *g, const char *name)
{
    for (int i = 0; i < g->n; i++) if (bu_streq(g->defs[i].name, name)) return i;
    return -1;
}

bool bu_guard_ack_valid(const bu_guard_t *g, uint32_t now_ms)
{
    return g->ack && (uint32_t)(now_ms - g->ack_ms) <= BU_ACK_VALID_MS;
}

void bu_guard_ack(bu_guard_t *g, uint32_t now_ms) { g->ack = true; g->ack_ms = now_ms; }

bu_err_t bu_guard_set(bu_guard_t *g, int idx, bool on, uint32_t now_ms)
{
    if (idx < 0 || idx >= g->n) return BU_ERR_NAME;
    if (!on) { g->on[idx] = false; return BU_OK; }   /* kapatmak her zaman serbest */
    if (g->defs[idx].grp == BU_GRP_J2J4 && !bu_guard_ack_valid(g, now_ms)) return BU_ERR_ACK;
    g->on[idx] = true; g->t_on[idx] = now_ms;
    return BU_OK;
}

bu_err_t bu_guard_set_name(bu_guard_t *g, const char *name, bool on, uint32_t now_ms)
{
    for (size_t k = 0; k < sizeof FORBIDDEN / sizeof FORBIDDEN[0]; k++)
        if (bu_streq(name, FORBIDDEN[k])) return BU_ERR_FORBIDDEN;
    return bu_guard_set(g, bu_guard_find(g, name), on, now_ms);
}

int bu_guard_tick(bu_guard_t *g, uint32_t now_ms)
{
    int k = 0;
    for (int i = 0; i < g->n; i++) {
        if (!g->on[i]) continue;
        uint32_t lim = g->defs[i].max_on_ms;
        bool expire = lim && (uint32_t)(now_ms - g->t_on[i]) >= lim;
        bool lost_ack = g->defs[i].grp == BU_GRP_J2J4 && !bu_guard_ack_valid(g, now_ms);
        if (expire || lost_ack) { g->on[i] = false; k++; }
    }
    return k;
}

void bu_guard_all_off(bu_guard_t *g) { for (int i = 0; i < g->n; i++) g->on[i] = false; }

/* ---------------- konsol ---------------- */
bool bu_console_feed(bu_console_t *c, char ch, int *argc, char *argv[BU_ARGV_MAX])
{
    if (ch == '\b' || ch == 0x7F) { if (c->len) c->len--; return false; }
    if (ch != '\r' && ch != '\n') {
        if (c->len < BU_LINE_MAX - 1) c->line[c->len++] = ch;
        return false;
    }
    if (c->len == 0) return false;   /* boş satır / \r\n'in ikinci yarısı */
    c->line[c->len] = '\0';
    c->len = 0;
    *argc = 0;
    char *p = c->line;
    while (*p && *argc < BU_ARGV_MAX) {
        while (*p == ' ' || *p == '\t') *p++ = '\0';
        if (!*p) break;
        argv[(*argc)++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    return *argc > 0;
}

bool bu_parse_int(const char *s, long *out)
{
    long v = 0; int neg = 0, base = 10, any = 0;
    if (*s == '-') { neg = 1; s++; }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    for (; *s; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (base == 16 && *s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (base == 16 && *s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else return false;
        v = v * base + d; any = 1;
    }
    if (!any) return false;
    *out = neg ? -v : v;
    return true;
}
