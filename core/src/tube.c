/*
 * Tüp koruması – insert profilleri
 *
 * Kaynaklar (Tup_Insert_Verileri.md):
 *  - Canon PE-DF-161SBR (2018-11)   – çift fokus 1.6 / 0.5
 *  - Canon TE-D-067SB Rev.7 (2023)  – tek fokus 0.6
 *  - Canon PE-D-023/D-023SB (2018)  – tek fokus 0.2
 *  - CEI OX/105-4 Rev.H-0816        – tek fokus 2.6 (DC yük eğrisi)
 *
 * DİKKAT: Yük eğrisi değerleri veri sayfası grafiklerinden gözle okunmuştur.
 * FE_RATING_DERATE ile %10 güvenlik payı uygulanır. Seri üretimden önce
 * eğriler sayısallaştırılıp tablolar güncellenmelidir. Tablolar 0.1 / 1 /
 * 10 s sütunludur; grafiği 1 s'den başlayan insertlerde 0.1 s = 1 s değeri,
 * 10 s'den uzun radyografi bu profillerde izin verilmez (güvenli taraf).
 */
#include "tube.h"
#include "fe_config.h"
#include <math.h>
#include <stddef.h>

#define N_KV_MAX 9
#define N_T      3
static const float t_axis_s[N_T] = { 0.1f, 1.0f, 10.0f };

typedef struct {
    tube_insert_t id;
    const char *name;
    bool dual_focus;
    bool fluoro;                 /* floroskopi veri sayfasında tanımlı */
    float kv_min, kv_max;
    float ma_max_large, ma_max_small;
    float power_large_w, power_small_w;   /* nominal radyografik anot gücü */
    float heat_capacity_j;
    float dissipation_w;         /* en yüksek anot ısı atma hızı */
    uint32_t rad_time_max_ms;
    uint32_t duty_large, duty_small;      /* görev çevrimi 1:N */
    float fluoro_power_w;
    uint8_t n_kv;
    float kv_axis[N_KV_MAX];
    float rating_large[N_KV_MAX][N_T];
    float rating_small[N_KV_MAX][N_T];
} tube_profile_t;

static const tube_profile_t profiles[TUBE_INSERT_COUNT] = {
    [TUBE_INSERT_DF161SBR] = {
        .id = TUBE_INSERT_DF161SBR, .name = "DF-161SBR",
        .dual_focus = true, .fluoro = true,
        .kv_min = 40.0f, .kv_max = 125.0f,
        .ma_max_large = 60.0f, .ma_max_small = 15.0f,
        .power_large_w = 4100.0f, .power_small_w = 720.0f,
        .heat_capacity_j = 35500.0f, .dissipation_w = 600.0f,
        .rad_time_max_ms = 10000u, .duty_large = 40u, .duty_small = 20u,
        .fluoro_power_w = 600.0f,
        .n_kv = 9,
        .kv_axis = { 40, 50, 60, 70, 80, 90, 100, 110, 125 },
        .rating_large = {
            /* 0.1 s  1 s   10 s */
            { 60.0f, 60.0f, 55.0f },  /*  40 kV */
            { 60.0f, 60.0f, 50.0f },  /*  50 kV */
            { 60.0f, 60.0f, 45.0f },  /*  60 kV */
            { 58.5f, 57.0f, 40.0f },  /*  70 kV */
            { 51.5f, 50.0f, 35.0f },  /*  80 kV */
            { 45.5f, 44.5f, 31.5f },  /*  90 kV */
            { 41.0f, 40.0f, 28.5f },  /* 100 kV */
            { 37.0f, 36.0f, 25.5f },  /* 110 kV */
            { 33.0f, 32.0f, 22.5f },  /* 125 kV */
        },
        .rating_small = {
            { 15.0f, 15.0f, 14.5f },  /*  40 kV */
            { 14.4f, 14.0f, 13.3f },  /*  50 kV */
            { 12.0f, 11.8f, 11.0f },  /*  60 kV */
            { 10.4f, 10.2f,  9.6f },  /*  70 kV */
            {  9.4f,  9.1f,  8.5f },  /*  80 kV */
            {  8.4f,  8.2f,  7.7f },  /*  90 kV */
            {  7.5f,  7.3f,  6.9f },  /* 100 kV */
            {  6.8f,  6.7f,  6.3f },  /* 110 kV */
            {  5.8f,  5.7f,  5.4f },  /* 125 kV */
        },
    },
    [TUBE_INSERT_D067SB] = {
        /* Veri sayfası: en çok 22 mA, 3.5 A filaman, 1270 W, 35 kJ, 250 W,
         * görev çevrimi 1:60, en uzun çekim 20 s (burada 10 s ile sınırlı). */
        .id = TUBE_INSERT_D067SB, .name = "D-067SB",
        .dual_focus = false, .fluoro = false,
        .kv_min = 50.0f, .kv_max = 100.0f,
        .ma_max_large = 22.0f, .ma_max_small = 0.0f,
        .power_large_w = 1270.0f, .power_small_w = 0.0f,
        .heat_capacity_j = 35000.0f, .dissipation_w = 250.0f,
        .rad_time_max_ms = 10000u, .duty_large = 60u, .duty_small = 60u,
        .fluoro_power_w = 0.0f,
        .n_kv = 6,
        .kv_axis = { 50, 60, 70, 80, 90, 100 },
        .rating_large = {
            /* 0.1 s  1 s   10 s  (grafik 1-20 s) */
            { 20.0f, 20.0f, 20.0f },  /*  50 kV */
            { 21.0f, 21.0f, 16.5f },  /*  60 kV */
            { 18.0f, 18.0f, 14.0f },  /*  70 kV */
            { 15.8f, 15.8f, 12.4f },  /*  80 kV */
            { 14.0f, 14.0f, 11.1f },  /*  90 kV */
            { 12.6f, 12.6f,  9.9f },  /* 100 kV */
        },
    },
    [TUBE_INSERT_D023] = {
        /* Veri sayfası: en çok 4.3 mA, 3.9 A filaman, 387 W, 35 kJ, 250 W,
         * görev çevrimi 1:60, en uzun çekim 40 s (burada 10 s ile sınırlı). */
        .id = TUBE_INSERT_D023, .name = "D-023",
        .dual_focus = false, .fluoro = false,
        .kv_min = 50.0f, .kv_max = 100.0f,
        .ma_max_large = 4.3f, .ma_max_small = 0.0f,
        .power_large_w = 387.0f, .power_small_w = 0.0f,
        .heat_capacity_j = 35000.0f, .dissipation_w = 250.0f,
        .rad_time_max_ms = 10000u, .duty_large = 60u, .duty_small = 60u,
        .fluoro_power_w = 0.0f,
        .n_kv = 6,
        .kv_axis = { 50, 60, 70, 80, 90, 100 },
        .rating_large = {
            /* 0.1 s  1 s   10 s  (grafik 1-40 s, neredeyse yatay) */
            { 3.0f, 3.0f, 3.0f },     /*  50 kV */
            { 4.0f, 4.0f, 4.0f },     /*  60 kV */
            { 4.0f, 4.0f, 4.0f },     /*  70 kV */
            { 4.0f, 4.0f, 4.0f },     /*  80 kV */
            { 4.3f, 4.3f, 4.2f },     /*  90 kV */
            { 3.85f, 3.85f, 3.75f },  /* 100 kV */
        },
    },
    [TUBE_INSERT_OX105_4] = {
        /* Veri sayfası: 110 kV nominal, 2.6 mm, 30 kJ, 250 W, 5700 W (0.1 s DC);
         * yük eğrisi "RATING CHARTS DC" (sabit potansiyel). Görev çevrimi
         * veri sayfasında yok: 1:40 varsayıldı (ölçümle doğrulanacak).
         * En yüksek mA veri sayfasında yok: jeneratör sınırı (50 mA). */
        .id = TUBE_INSERT_OX105_4, .name = "OX/105-4",
        .dual_focus = false, .fluoro = false,
        .kv_min = 50.0f, .kv_max = 110.0f,
        .ma_max_large = 50.0f, .ma_max_small = 0.0f,
        .power_large_w = 5700.0f, .power_small_w = 0.0f,
        .heat_capacity_j = 30000.0f, .dissipation_w = 250.0f,
        .rad_time_max_ms = 10000u, .duty_large = 40u, .duty_small = 40u,
        .fluoro_power_w = 0.0f,
        .n_kv = 7,
        .kv_axis = { 50, 60, 70, 80, 90, 100, 110 },
        .rating_large = {
            /* 0.1 s   1 s    10 s  (DC) */
            { 114.0f, 106.0f, 77.0f },  /*  50 kV */
            {  95.0f,  88.0f, 64.0f },  /*  60 kV */
            {  81.0f,  76.0f, 56.0f },  /*  70 kV */
            {  71.0f,  67.0f, 48.0f },  /*  80 kV */
            {  63.0f,  59.0f, 44.0f },  /*  90 kV */
            {  57.0f,  53.0f, 39.0f },  /* 100 kV */
            {  52.0f,  48.0f, 35.0f },  /* 110 kV */
        },
    },
};

static const tube_profile_t *prof(const tube_state_t *st)
{
    return (st && st->insert < TUBE_INSERT_COUNT) ? &profiles[st->insert] : &profiles[TUBE_INSERT_DF161SBR];
}

static float minf(float a, float b) { return a < b ? a : b; }

void tube_init(tube_state_t *st)
{
    st->heat_j = 0.0f;
    st->cooldown_ms = 0u;
    st->gen_power_max_w = FE_GEN_POWER_MAX_W;
    st->insert = TUBE_INSERT_DF161SBR;
}

bool tube_select(tube_state_t *st, tube_insert_t id)
{
    if (id >= TUBE_INSERT_COUNT) return false;
    st->insert = id;
    st->heat_j = 0.0f;          /* yeni insert: ısı modeli baştan (servis işlemi) */
    st->cooldown_ms = 0u;
    return true;
}

const char *tube_insert_name(const tube_state_t *st) { return prof(st)->name; }

bool tube_has_small_focus(const tube_state_t *st) { return prof(st)->dual_focus; }

bool tube_has_fluoro(const tube_state_t *st) { return prof(st)->fluoro; }

static float lerp(float a, float b, float f) { return a + (b - a) * f; }

float tube_rating_ma_max_for(const tube_state_t *st, focus_t focus, float kv, uint32_t time_ms)
{
    const tube_profile_t *pr = prof(st);
    if (focus == FOCUS_SMALL && !pr->dual_focus) return 0.0f;
    const float (*tbl)[N_T] = (focus == FOCUS_SMALL) ? pr->rating_small : pr->rating_large;
    const size_t n = pr->n_kv;

    if (kv < pr->kv_axis[0] || kv > pr->kv_axis[n - 1]) {
        return 0.0f;
    }

    /* kV yönünde doğrusal */
    size_t i = 0;
    while (i < n - 2 && kv > pr->kv_axis[i + 1]) {
        i++;
    }
    const float fk = (kv - pr->kv_axis[i]) / (pr->kv_axis[i + 1] - pr->kv_axis[i]);

    /* zaman yönünde logaritmik; 0.1 s altı 0.1 s, 10 s üstü 10 s */
    float t = (float)time_ms / 1000.0f;
    if (t < t_axis_s[0]) t = t_axis_s[0];
    if (t > t_axis_s[N_T - 1]) t = t_axis_s[N_T - 1];
    const size_t j = (t <= t_axis_s[1]) ? 0u : 1u;
    const float ft = (log10f(t) - log10f(t_axis_s[j])) /
                     (log10f(t_axis_s[j + 1]) - log10f(t_axis_s[j]));

    const float a = lerp(tbl[i][j],     tbl[i][j + 1],     ft);
    const float b = lerp(tbl[i + 1][j], tbl[i + 1][j + 1], ft);
    return lerp(a, b, fk) * FE_RATING_DERATE;
}

float tube_rating_ma_max(focus_t focus, float kv, uint32_t time_ms)
{
    return tube_rating_ma_max_for(NULL, focus, kv, time_ms);   /* DF-161SBR */
}

tube_result_t tube_check(const tube_state_t *st, const exposure_params_t *p)
{
    if (p == NULL || st == NULL || p->ma <= 0.0f || p->time_ms == 0u) {
        return TUBE_ERR_PARAM;
    }
    const tube_profile_t *pr = prof(st);
    if (p->focus == FOCUS_SMALL && !pr->dual_focus) return TUBE_ERR_NO_SMALL_FOCUS;
    if (p->kv < pr->kv_min) return TUBE_ERR_KV_LOW;
    if (p->kv > pr->kv_max || p->kv > FE_GEN_KV_MAX) return TUBE_ERR_KV_HIGH;

    const float ma_abs = (p->focus == FOCUS_SMALL) ? pr->ma_max_small : pr->ma_max_large;
    if (p->ma > ma_abs) return TUBE_ERR_MA_FOCUS_MAX;

    const float power = p->kv * p->ma;   /* kV * mA = W */
    if (power > st->gen_power_max_w) return TUBE_ERR_GEN_POWER;

    if (p->mode == MODE_FLUORO) {
        if (!pr->fluoro || p->focus != FOCUS_SMALL) return TUBE_ERR_FLUORO_FOCUS;
        if (p->kv < FE_FLUORO_KV_MIN || p->kv > FE_FLUORO_KV_MAX) return TUBE_ERR_FLUORO_KV;
        if (power > pr->fluoro_power_w) return TUBE_ERR_FLUORO_POWER;
        if (p->time_ms > FE_FLUORO_TIME_MAX_MS) return TUBE_ERR_TIME_MAX;
    } else {
        if (p->time_ms > pr->rad_time_max_ms) return TUBE_ERR_TIME_MAX;
        if (p->ma > tube_rating_ma_max_for(st, p->focus, p->kv, p->time_ms)) return TUBE_ERR_RATING;
    }

    if (st->cooldown_ms > 0u) return TUBE_ERR_COOLDOWN;

    /* Isı: ışınlama sırasındaki soğuma ihmal edilir (güvenli taraf) */
    const float added = power * ((float)p->time_ms / 1000.0f);
    if (st->heat_j + added > pr->heat_capacity_j * FE_HEAT_DERATE) return TUBE_ERR_HEAT;

    return TUBE_OK;
}

void tube_tick(tube_state_t *st, float power_w, uint32_t dt_ms)
{
    const tube_profile_t *pr = prof(st);
    /* Isı modeli zaman sabiti: tam kapasitede atım en yüksek ısı atma hızına
     * eşit olacak şekilde tau = C / P (DF-161SBR: 35500 / 600 = ~59 s).
     * Birinci dereceden model; soğuma eğrisiyle ölçüm sonrası ince ayar. */
    const float tau_s = pr->heat_capacity_j / pr->dissipation_w;
    const float dt = (float)dt_ms / 1000.0f;
    const float cool = st->heat_j / tau_s;
    st->heat_j += (power_w - cool) * dt;
    if (st->heat_j < 0.0f) st->heat_j = 0.0f;

    st->cooldown_ms = (st->cooldown_ms > dt_ms) ? (st->cooldown_ms - dt_ms) : 0u;
}

void tube_exposure_done(tube_state_t *st, focus_t focus, uint32_t actual_ms)
{
    const tube_profile_t *pr = prof(st);
    const uint32_t ratio = (focus == FOCUS_SMALL) ? pr->duty_small : pr->duty_large;
    const uint32_t wait = actual_ms * ratio;
    if (wait > st->cooldown_ms) st->cooldown_ms = wait;
}

float tube_heat_fraction(const tube_state_t *st)
{
    return st->heat_j / prof(st)->heat_capacity_j;
}

const char *tube_result_text(tube_result_t r)
{
    switch (r) {
    case TUBE_OK:               return "Uygun";
    case TUBE_ERR_KV_LOW:       return "kV tup alt sinirinin altinda";
    case TUBE_ERR_KV_HIGH:      return "kV ust sinirin uzerinde";
    case TUBE_ERR_MA_FOCUS_MAX: return "mA bu fokus icin cok yuksek";
    case TUBE_ERR_RATING:       return "Tup yuk egrisi asiliyor";
    case TUBE_ERR_TIME_MAX:     return "Sure ust sinirin uzerinde";
    case TUBE_ERR_GEN_POWER:    return "Jenerator gucu asiliyor";
    case TUBE_ERR_HEAT:         return "Anot isisi yetersiz, bekleyin";
    case TUBE_ERR_COOLDOWN:     return "Sogutma beklemesi suruyor";
    case TUBE_ERR_FLUORO_FOCUS: return "Floroskopi yalnizca kucuk fokus";
    case TUBE_ERR_FLUORO_KV:    return "Floroskopi 40-60 kV";
    case TUBE_ERR_FLUORO_POWER: return "Floroskopi en fazla 600 W";
    case TUBE_ERR_PARAM:        return "Gecersiz parametre";
    case TUBE_ERR_NO_SMALL_FOCUS: return "Bu tupte kucuk fokus yok";
    }
    return "?";
}

/* ---------- sınır bildirimi ---------- */

void tube_limits(const tube_state_t *st, focus_t focus, exp_mode_t mode, tube_limits_t *out)
{
    const tube_profile_t *pr = prof(st);
    const float gen = st->gen_power_max_w > 0.0f ? st->gen_power_max_w : FE_GEN_POWER_MAX_W;
    if (mode == MODE_FLUORO) {
        out->kv_min = FE_FLUORO_KV_MIN;
        out->kv_max = FE_FLUORO_KV_MAX;
        out->ma_max = pr->ma_max_small;
        out->time_max_ms = FE_FLUORO_TIME_MAX_MS;
        out->power_max_w = minf(pr->fluoro_power_w, gen);
        return;
    }
    out->kv_min = pr->kv_min;
    out->kv_max = minf(pr->kv_max, FE_GEN_KV_MAX);
    out->ma_max = focus == FOCUS_SMALL ? pr->ma_max_small : pr->ma_max_large;
    out->time_max_ms = pr->rad_time_max_ms;
    out->power_max_w = minf(focus == FOCUS_SMALL ? pr->power_small_w : pr->power_large_w, gen);
}
