/*
 * Işınlama durum makinesi
 *
 * Saf mantık: her tick'te girişleri alır, çıkışları üretir. Donanıma
 * doğrudan dokunmaz; HAL katmanı çıkışları pinlere ve DAC'a yazar.
 * Böylece aynı kod PC'de simülatörle test edilir.
 *
 * NOT: Bu yazılım güvenliğin tek bariyeri değildir. Donanım güvenlik
 * zinciri (yedek zamanlayıcı, watchdog, interlock, aşım komparatörü)
 * yazılımdan bağımsız olarak ışınlamayı keser.
 */
#ifndef EXPOSURE_SM_H
#define EXPOSURE_SM_H

#include <stdbool.h>
#include <stdint.h>
#include "tube.h"

typedef enum {
    ST_IDLE = 0,    /* bekleme */
    ST_PREP,        /* 1. kademe: flaman ısınıyor */
    ST_READY,       /* hazır lambası yanıyor */
    ST_EXPOSING,    /* ışınlama (RL5 oturma süresi dahil) */
    ST_LOCKED,      /* buton tamamen bırakılana kadar kilit */
    ST_FAULT        /* onay gerektiren hata */
} sm_state_t;

typedef enum {
    END_NONE = 0,
    END_TIME_DONE,        /* normal bitiş */
    END_RELEASED,         /* operatör butonu / pedalı bıraktı */
    END_INTERLOCK,        /* kapı açık */
    END_TUBE_THERMAL,     /* tüp termiği açık (aşırı sıcak) */
    END_ESTOP,            /* acil stop */
    END_FILAMENT_LOST,    /* "Filaman tamam" düştü */
    END_GEN_ALARM,        /* jeneratör Alarm veya EP/IP Over */
    END_KV_LOW,           /* ölçülen kV ayarın %85'inin altında */
    END_KV_HIGH,          /* ölçülen kV ayarın %110'unun üzerinde */
    END_MA_TOL,           /* mA tolerans dışı */
    END_BACKUP_TIMER,     /* yedek zamanlayıcı izni düştü */
    END_EARLY_STAGE2,     /* hazır olmadan 2. kademe */
    END_PREP_TIMEOUT,     /* 1. kademe çok uzun tutuldu */
    END_FILAMENT_TIMEOUT, /* ısınma sonrası "Filaman tamam" gelmedi */
    END_DETECTOR_TIMEOUT, /* dedektör hazır gelmedi */
    END_TUBE_REJECT,      /* tüp koruması izin vermedi */
    END_BUTTON_STUCK,     /* buton veya pedal takılı kaldı */
    END_INPUT_CHANNEL,    /* buton/pedal kanalları tutarsız: kısa devre veya kopukluk */
    END_ESTOP_CHANNEL,    /* acil stop kanalları tutarsız */
    END_LINE_FAULT,       /* J2 X-RAY ON / PreHeat hattı komutla uyuşmuyor (kısa devre, yapışık PhotoMOS, kopukluk) */
    END_EARLY_RELEASE,    /* radyografide buton süre dolmadan bırakıldı */
    END_TIME_DEV,         /* ölçülen ışınlama süresi ayarlanan süreden sapıyor */
    END_DETECTOR_WINDOW,  /* senkron dedektör ışınlama penceresini süre dolmadan kapattı */
    END_DETECTOR_ERROR,   /* dedektör hata sinyali */
    END__COUNT
} end_reason_t;

/* Debounce edilmiş girişler */
typedef struct {
    bool hs_stage1;       /* el butonu 1. kademe, NO kontak */
    bool hs_stage1_nc;    /* el butonu 1. kademe, NC kontak (basılı değilken kapalı) */
    bool hs_stage2;       /* el butonu 2. kademe, NO kontak */
    bool hs_stage2_nc;    /* el butonu 2. kademe, NC kontak */
    bool pedal;           /* floroskopi ayak pedalı, kanal A */
    bool pedal_b;         /* floroskopi ayak pedalı, kanal B */
    bool filament_ok;     /* akım rölesi: "Sistem hazır" */
    bool door_ok;         /* kapı kapalı (J21) */
    bool thermal_ok;      /* tüp termiği kapalı */
    bool estop_ok;        /* acil stop kanal 1 (NC zincir kapalı = basılı değil) */
    bool estop_ok_b;      /* acil stop kanal 2 */
    bool gen_alarm;       /* jeneratör Alarm */
    bool gen_over;        /* EP/IP Over */
    bool backup_ok;       /* yedek zamanlayıcı izni */
    bool detector_ready;  /* DR yazılımından veya senkron girişten */
    bool detector_enable; /* senkron: dedektör ışınlamaya izin veriyor (pencere açık) */
    bool detector_error;  /* senkron: dedektör hata sinyali */
    bool xray_line_fb;    /* geri okuma: J2 X-RAY ON hattı GND'ye çekilmiş (aktif) */
    bool preheat_line_fb; /* geri okuma: J2 PreHeat hattı aktif */
    float kv_fb;          /* ölçülen kV */
    float ma_fb;          /* ölçülen mA */
} sm_inputs_t;

typedef struct {
    bool preheat;         /* J2 PreHeat */
    bool xray_on;         /* J2 X-RAY ON isteği (donanım zinciri ayrıca AND'ler) */
    bool rl5_series;      /* RL5 seri güvenlik kontağı */
    bool gen_interlock;   /* J2 Inter-Lock: yalnızca hazırlık ve ışınlamada kapalı (2. kanal) */
    bool det_prep;        /* senkron: dedektöre hazırlık isteği */
    bool det_exp_req;     /* senkron: dedektöre ışınlama isteği */
    bool focus_small;     /* J2 FCS SML */
    bool lamp_prep;       /* J21 hazır/hazırlık lambası */
    bool lamp_exposure;   /* J21 ışınlama lambası */
    bool buzzer;          /* panel + J21 harici buzzer */
    bool ready_indicator; /* ekrandaki "HAZIR" */
    float kv_ref;         /* kV cinsinden; HAL volta çevirir */
    float ma_ref;         /* mA cinsinden */
} sm_outputs_t;

typedef struct {
    bool detector_ready_required;  /* dedektör profili: AED/kapalı -> false */
    bool hs_dual_channel;          /* el butonu NO+NC kontaklı (önerilen) */
    bool pedal_dual_channel;       /* pedal iki kanallı */
    bool line_readback;            /* J2 hat geri okuması takılı */
    bool wait_detector_enable;     /* senkron profil: ışınlamadan önce dedektör izni beklenir */
    bool fluoro_enabled;           /* floroskopi opsiyonu bu cihazda açık */
    float gen_power_max_w;         /* 0 = varsayılan (FE_GEN_POWER_MAX_W) */
} sm_config_t;

/* Son ışınlamanın kaydı (DR yazılımına ve kayıt defterine gider) */
typedef struct {
    exposure_params_t set;
    uint32_t actual_ms;
    float mas_measured;
    uint32_t irr_ms;       /* kV geri beslemesinden ölçülen ışınlama süresi (kV >= %75) */
    float kv_avg;
    float ma_avg;
    end_reason_t reason;
    bool valid;
} exposure_record_t;

typedef struct {
    sm_state_t state;
    sm_config_t cfg;
    exposure_params_t params;
    tube_state_t tube;
    uint32_t t_state_ms;       /* mevcut durumdaki süre */
    uint32_t t_expose_ms;      /* X-RAY ON'dan beri süre */
    uint32_t tol_kv_ms, tol_ma_ms;
    uint32_t rl5_hold_ms;      /* bitişte RL5'in geç açılması */
    uint32_t chan_bad_ms;      /* kanal tutarsızlığı süresi */
    uint32_t estop_bad_ms;     /* acil stop kanal tutarsızlığı süresi */
    uint32_t stuck_ms;         /* kilitte butonun basılı kalma süresi */
    uint32_t line_bad_ms;
    uint32_t det_wait_ms;      /* dedektör izni bekleme süresi */
    bool irr_active;           /* ışınlama süresi ölçümü sürüyor */
    uint32_t irr_ms, irr_after_ms;
    float irr_thr_kv;      /* hat geri okuma uyuşmazlığı süresi */
    bool prev_xray_cmd, prev_preheat_cmd;
    bool booted;               /* ilk adım yapıldı mı (açılışta takılı buton kontrolü) */
    float kv_sum, ma_sum;
    uint32_t n_samples;
    float mas_acc;
    uint32_t fluoro_cumulative_ms;
    bool fluoro_alert;
    end_reason_t last_reason;
    tube_result_t last_tube_result;
    exposure_record_t last_record;
} exposure_sm_t;

#define SM_RL5_SETTLE_MS   15u   /* RL5 kapandıktan sonra X-RAY ON */
#define SM_RL5_RELEASE_MS  20u   /* X-RAY ON kesildikten sonra RL5 açılır */

void sm_init(exposure_sm_t *sm, const sm_config_t *cfg);

/* Ekrandan veya DR yazılımından gelen ayar. Işınlama sırasında reddedilir. */
bool sm_set_params(exposure_sm_t *sm, const exposure_params_t *p);

/* Her 1 ms'de çağrılır */
void sm_step(exposure_sm_t *sm, const sm_inputs_t *raw, sm_outputs_t *out, uint32_t dt_ms);

/* Hata onayı (ekrandaki "Onayla" + jeneratöre ALM RST) */
bool sm_ack_fault(exposure_sm_t *sm);

/* Floroskopi 5 dk sayacını operatör onayıyla sıfırlar */
void sm_fluoro_reset_cumulative(exposure_sm_t *sm);

const char *sm_state_text(sm_state_t s);
const char *sm_reason_text(end_reason_t r);

#endif
