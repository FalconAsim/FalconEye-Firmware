/*
 * Tüp koruması - Canon DF-161SBR
 *
 * Kaynak: Canon PE-DF-161SBR (2018-11), "Maximum Rating Charts",
 * "Absolute Maximum and Minimum Ratings", "Anode Heating/Cooling Curve".
 *
 * DİKKAT: Yük eğrisi değerleri datasheet grafiğinden gözle okunmuştur.
 * FE_RATING_DERATE ile %10 güvenlik payı uygulanır. Seri üretimden önce
 * eğriler sayısallaştırılıp bu tablo güncellenmelidir.
 */
#include "tube.h"
#include "fe_config.h"
#include <math.h>
#include <stddef.h>

/* --- Mutlak sınırlar (datasheet) --- */
#define TUBE_KV_MIN            40.0f
#define TUBE_KV_MAX           125.0f
#define TUBE_MA_MAX_LARGE      60.0f
#define TUBE_MA_MAX_SMALL      15.0f
#define TUBE_HEAT_CAPACITY_J 35500.0f
#define TUBE_DISSIPATION_W     600.0f   /* en yüksek anot ısı atma hızı */
#define TUBE_RAD_TIME_MAX_MS 10000u     /* en uzun radyografik ışınlama */
#define TUBE_FLUORO_POWER_W    600.0f   /* 600 W'ta 20 dk */
#define TUBE_DUTY_LARGE         40u     /* 1:40 */
#define TUBE_DUTY_SMALL         20u     /* 1:20 */

/* Isı modeli zaman sabiti: tam kapasitede atım 600 W olacak şekilde
 * tau = C / P = 35500 / 600 = ~59 s. Birinci dereceden model; soğuma
 * eğrisiyle ölçüm sonrası ince ayar yapılacak. */
#define TUBE_HEAT_TAU_S  (TUBE_HEAT_CAPACITY_J / TUBE_DISSIPATION_W)

/* Yük eğrisi tablosu: kV satırları, 0.1 s / 1 s / 10 s sütunları (mA) */
#define N_KV 9
#define N_T  3
static const float kv_axis[N_KV] = { 40, 50, 60, 70, 80, 90, 100, 110, 125 };
static const float t_axis_s[N_T] = { 0.1f, 1.0f, 10.0f };

static const float rating_large[N_KV][N_T] = {
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
};

static const float rating_small[N_KV][N_T] = {
    { 15.0f, 15.0f, 14.5f },  /*  40 kV */
    { 14.4f, 14.0f, 13.3f },  /*  50 kV */
    { 12.0f, 11.8f, 11.0f },  /*  60 kV */
    { 10.4f, 10.2f,  9.6f },  /*  70 kV */
    {  9.4f,  9.1f,  8.5f },  /*  80 kV */
    {  8.4f,  8.2f,  7.7f },  /*  90 kV */
    {  7.5f,  7.3f,  6.9f },  /* 100 kV */
    {  6.8f,  6.7f,  6.3f },  /* 110 kV */
    {  5.8f,  5.7f,  5.4f },  /* 125 kV */
};

void tube_init(tube_state_t *st)
{
    st->heat_j = 0.0f;
    st->cooldown_ms = 0u;
    st->gen_power_max_w = FE_GEN_POWER_MAX_W;
}

static float lerp(float a, float b, float f) { return a + (b - a) * f; }

float tube_rating_ma_max(focus_t focus, float kv, uint32_t time_ms)
{
    const float (*tbl)[N_T] = (focus == FOCUS_SMALL) ? rating_small : rating_large;

    if (kv < kv_axis[0] || kv > kv_axis[N_KV - 1]) {
        return 0.0f;
    }

    /* kV yönünde doğrusal */
    size_t i = 0;
    while (i < N_KV - 2 && kv > kv_axis[i + 1]) {
        i++;
    }
    float fk = (kv - kv_axis[i]) / (kv_axis[i + 1] - kv_axis[i]);

    /* zaman yönünde logaritmik; 0.1 s altı 0.1 s, 10 s üstü 10 s */
    float t = (float)time_ms / 1000.0f;
    if (t < t_axis_s[0]) t = t_axis_s[0];
    if (t > t_axis_s[N_T - 1]) t = t_axis_s[N_T - 1];
    size_t j = (t <= t_axis_s[1]) ? 0u : 1u;
    float ft = (log10f(t) - log10f(t_axis_s[j])) /
               (log10f(t_axis_s[j + 1]) - log10f(t_axis_s[j]));

    float a = lerp(tbl[i][j],     tbl[i][j + 1],     ft);
    float b = lerp(tbl[i + 1][j], tbl[i + 1][j + 1], ft);
    return lerp(a, b, fk) * FE_RATING_DERATE;
}

tube_result_t tube_check(const tube_state_t *st, const exposure_params_t *p)
{
    if (p == NULL || st == NULL || p->ma <= 0.0f || p->time_ms == 0u) {
        return TUBE_ERR_PARAM;
    }
    if (p->kv < TUBE_KV_MIN) return TUBE_ERR_KV_LOW;
    if (p->kv > TUBE_KV_MAX || p->kv > FE_GEN_KV_MAX) return TUBE_ERR_KV_HIGH;

    const float ma_abs = (p->focus == FOCUS_SMALL) ? TUBE_MA_MAX_SMALL : TUBE_MA_MAX_LARGE;
    if (p->ma > ma_abs) return TUBE_ERR_MA_FOCUS_MAX;

    const float power = p->kv * p->ma;   /* kV * mA = W */
    if (power > st->gen_power_max_w) return TUBE_ERR_GEN_POWER;

    if (p->mode == MODE_FLUORO) {
        if (p->focus != FOCUS_SMALL) return TUBE_ERR_FLUORO_FOCUS;
        if (p->kv < FE_FLUORO_KV_MIN || p->kv > FE_FLUORO_KV_MAX) return TUBE_ERR_FLUORO_KV;
        if (power > TUBE_FLUORO_POWER_W) return TUBE_ERR_FLUORO_POWER;
        if (p->time_ms > FE_FLUORO_TIME_MAX_MS) return TUBE_ERR_TIME_MAX;
    } else {
        if (p->time_ms > TUBE_RAD_TIME_MAX_MS) return TUBE_ERR_TIME_MAX;
        if (p->ma > tube_rating_ma_max(p->focus, p->kv, p->time_ms)) return TUBE_ERR_RATING;
    }

    if (st->cooldown_ms > 0u) return TUBE_ERR_COOLDOWN;

    /* Isı: ışınlama sırasındaki soğuma ihmal edilir (güvenli taraf) */
    const float added = power * ((float)p->time_ms / 1000.0f);
    if (st->heat_j + added > TUBE_HEAT_CAPACITY_J * FE_HEAT_DERATE) return TUBE_ERR_HEAT;

    return TUBE_OK;
}

void tube_tick(tube_state_t *st, float power_w, uint32_t dt_ms)
{
    const float dt = (float)dt_ms / 1000.0f;
    const float cool = st->heat_j / TUBE_HEAT_TAU_S;
    st->heat_j += (power_w - cool) * dt;
    if (st->heat_j < 0.0f) st->heat_j = 0.0f;

    st->cooldown_ms = (st->cooldown_ms > dt_ms) ? (st->cooldown_ms - dt_ms) : 0u;
}

void tube_exposure_done(tube_state_t *st, focus_t focus, uint32_t actual_ms)
{
    const uint32_t ratio = (focus == FOCUS_SMALL) ? TUBE_DUTY_SMALL : TUBE_DUTY_LARGE;
    const uint32_t wait = actual_ms * ratio;
    if (wait > st->cooldown_ms) st->cooldown_ms = wait;
}

float tube_heat_fraction(const tube_state_t *st)
{
    return st->heat_j / TUBE_HEAT_CAPACITY_J;
}

const char *tube_result_text(tube_result_t r)
{
    switch (r) {
    case TUBE_OK:               return "Uygun";
    case TUBE_ERR_KV_LOW:       return "kV tup alt sinirinin (40 kV) altinda";
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
    }
    return "?";
}

/* ---------- insert bilgisi ve sınırlar ---------- */

#define TUBE_POWER_LARGE_W   4100.0f   /* nominal radyografik anot gücü */
#define TUBE_POWER_SMALL_W    720.0f

const char *tube_insert_name(void) { return "DF-161SBR"; }

bool tube_has_small_focus(void) { return true; }

static float minf(float a, float b) { return a < b ? a : b; }

void tube_limits(const tube_state_t *st, focus_t focus, exp_mode_t mode, tube_limits_t *out)
{
    const float gen = st->gen_power_max_w > 0.0f ? st->gen_power_max_w : FE_GEN_POWER_MAX_W;
    if (mode == MODE_FLUORO) {
        out->kv_min = FE_FLUORO_KV_MIN;
        out->kv_max = FE_FLUORO_KV_MAX;
        out->ma_max = TUBE_MA_MAX_SMALL;
        out->time_max_ms = FE_FLUORO_TIME_MAX_MS;
        out->power_max_w = minf(TUBE_FLUORO_POWER_W, gen);
        return;
    }
    out->kv_min = TUBE_KV_MIN;
    out->kv_max = minf(TUBE_KV_MAX, FE_GEN_KV_MAX);
    out->ma_max = focus == FOCUS_SMALL ? TUBE_MA_MAX_SMALL : TUBE_MA_MAX_LARGE;
    out->time_max_ms = TUBE_RAD_TIME_MAX_MS;
    out->power_max_w = minf(focus == FOCUS_SMALL ? TUBE_POWER_SMALL_W : TUBE_POWER_LARGE_W, gen);
}
