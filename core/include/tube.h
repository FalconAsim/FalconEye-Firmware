/*
 * Tüp koruması: yük eğrisi, anot ısı modeli, görev döngüsü.
 * Insert profilleri: Canon DF-161SBR (1.6 / 0.5), D-067SB (0.6), D-023 (0.2),
 * CEI OX/105-4 (2.6). Kurulu insert servis menüsünden seçilir (tube_select).
 */
#ifndef TUBE_H
#define TUBE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { FOCUS_LARGE = 0, FOCUS_SMALL = 1 } focus_t;

typedef enum {
    TUBE_INSERT_DF161SBR = 0,   /* varsayılan */
    TUBE_INSERT_D067SB,
    TUBE_INSERT_D023,
    TUBE_INSERT_OX105_4,
    TUBE_INSERT_COUNT
} tube_insert_t;
typedef enum { MODE_RADIOGRAPHY = 0, MODE_FLUORO = 1 } exp_mode_t;

typedef struct {
    float kv;          /* tüp gerilimi, kV */
    float ma;          /* tüp akımı, mA */
    uint32_t time_ms;  /* ışınlama süresi */
    focus_t focus;
    exp_mode_t mode;
} exposure_params_t;

/* Kontrol sonucu: OK değilse ekranda neden gösterilir */
typedef enum {
    TUBE_OK = 0,
    TUBE_ERR_KV_LOW,          /* tüp alt sınırı 40 kV */
    TUBE_ERR_KV_HIGH,         /* tüp veya jeneratör üst sınırı */
    TUBE_ERR_MA_FOCUS_MAX,    /* fokusun mutlak akım sınırı */
    TUBE_ERR_RATING,          /* yük eğrisi aşılıyor */
    TUBE_ERR_TIME_MAX,        /* radyografi 10 s / floroskopi 20 s */
    TUBE_ERR_GEN_POWER,       /* jeneratör gücü */
    TUBE_ERR_HEAT,            /* anot ısı kapasitesi */
    TUBE_ERR_COOLDOWN,        /* görev döngüsü beklemesi sürüyor */
    TUBE_ERR_FLUORO_FOCUS,    /* floroskopi yalnızca küçük fokus */
    TUBE_ERR_FLUORO_KV,       /* floroskopi 40-60 kV */
    TUBE_ERR_FLUORO_POWER,    /* floroskopi en fazla 600 W */
    TUBE_ERR_PARAM,           /* geçersiz parametre */
    TUBE_ERR_NO_SMALL_FOCUS   /* kurulu insert tek fokuslu */
} tube_result_t;

typedef struct {
    float heat_j;            /* anottaki ısı (J) */
    uint32_t cooldown_ms;    /* görev döngüsünden kalan bekleme */
    float gen_power_max_w;   /* jeneratör gücü sınırı (servis ayarı; varsayılan FE_GEN_POWER_MAX_W) */
    tube_insert_t insert;    /* kurulu insert (servis ayarı) */
} tube_state_t;

void tube_init(tube_state_t *st);

/* Kurulu insertin seçimi (servis menüsü). Isı modeli sıfırlanır. */
bool tube_select(tube_state_t *st, tube_insert_t id);

/* Verilen fokus ve kV için, verilen sürede izin verilen en yüksek mA
 * (yük eğrisi, güvenlik payı uygulanmış). st = NULL: DF-161SBR. */
float tube_rating_ma_max_for(const tube_state_t *st, focus_t focus, float kv, uint32_t time_ms);
float tube_rating_ma_max(focus_t focus, float kv, uint32_t time_ms);   /* DF-161SBR */

/* Işınlamadan önce tam kontrol. */
tube_result_t tube_check(const tube_state_t *st, const exposure_params_t *p);

/* Isı modeli: her tick'te çağrılır. power_w = o anki anot gücü (kV*mA). */
void tube_tick(tube_state_t *st, float power_w, uint32_t dt_ms);

/* Işınlama bittiğinde: görev döngüsü beklemesini başlatır. */
void tube_exposure_done(tube_state_t *st, focus_t focus, uint32_t actual_ms);

/* Ekranda gösterilecek doluluk (0..1) */
float tube_heat_fraction(const tube_state_t *st);

const char *tube_result_text(tube_result_t r);

/* Kurulu insert adı ve sınırları (TGP LIMITS cevabı için) */
typedef struct {
    float kv_min, kv_max;     /* jeneratör sınırı dahil */
    float ma_max;
    uint32_t time_max_ms;
    float power_max_w;        /* fokus / mod sınırı ile jeneratör gücünün küçüğü */
} tube_limits_t;

const char *tube_insert_name(const tube_state_t *st);
bool tube_has_small_focus(const tube_state_t *st);
bool tube_has_fluoro(const tube_state_t *st);
void tube_limits(const tube_state_t *st, focus_t focus, exp_mode_t mode, tube_limits_t *out);

#endif
