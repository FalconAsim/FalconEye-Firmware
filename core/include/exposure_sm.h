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
    END_LINK_DOWN,        /* PC modu: yazılım bağlantısı yok, hazırlık başlamaz / iptal (E701) */
    END_LINK_LOST,        /* PC modu: ışınlamada TGP bağlantısı koptu, ışınlama kesildi (E702) */
    END_LINK_LOST_SERIAL, /* PC modu: ışınlamada RS-232 (Current) bağlantısı koptu (E703) */
    END_FOCUS_RELAY,      /* fokus rölesi geri bildirimi komutla uyuşmuyor (E406) */
    END_REF_READBACK,     /* kV / mA referans (DAC) çıkışının geri okuması uyuşmuyor (E407) */
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
    bool pc_link_ok;      /* PC modu: seçili yazılım bağlantısı canlı (TGP veya Current) */
    bool focus_fb_small;  /* fokus rölesi geri bildirimi: çekili = küçük fokus */
    float kv_ref_fb;      /* kV referans çıkışının geri okuması (kV cinsinden) */
    float ma_ref_fb;      /* mA referans çıkışının geri okuması (mA cinsinden) */
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
    bool focus_small;     /* fokus rölesi: çekili = küçük fokus, bırakık = büyük (hataya güvenli) */
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
    bool fluoro_enabled;           /* floroskopi opsiyonu bu cihazda açık (servis menüsü) */
    bool dap_meter_enabled;        /* DAP metre takılı (servis menüsü); kapalıysa DAP'ı FalconXray hesaplar */
    float gen_power_max_w;         /* 0 = varsayılan (FE_GEN_POWER_MAX_W) */
    bool pc_link_required;         /* PC kontrol modu: bağlantı zorunlu, koparsa ışınlama kesilir */
    bool pc_link_serial;           /* PC bağlantısı RS-232 (Current); false = TGP */
    bool focus_relay_readback;     /* fokus rölesi durum geri bildirimi takılı */
    bool ref_readback;             /* kV / mA referans çıkışı geri okuması takılı */
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
    bool cut;              /* ışınlama bağlantı kopması nedeniyle kesildi */
    bool dap_valid;        /* DAP metreden okundu (sm_record_dap) */
    float dap_dgycm2;      /* doz-alan çarpımı, dGy·cm² (DICOM 0018,115E birimi) */
    bool valid;
} exposure_record_t;

typedef struct {
    sm_state_t state;
    sm_config_t cfg;
    exposure_params_t params;
    exposure_params_t saved_rad;     /* tek tuşla mod geçişinde her modun son değerleri */
    exposure_params_t saved_fluoro;
    bool have_saved_fluoro;
    tube_state_t tube;
    uint32_t t_state_ms;       /* mevcut durumdaki süre (hazırlıkta ısınma zamanlayıcısı) */
    uint32_t prep_total_ms;    /* 1. kademenin toplam basılı tutulma süresi (ısınma yeniden başlasa da) */
    uint32_t warmup_count;     /* servis sayacı: filaman ısınma sayısı (yeniden başlamalar dahil) */
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
    bool focus_cmd_small;      /* fokus rölesi komutu; yalnız filaman sürülmüyorken değişir */
    uint32_t focus_settle_ms;  /* son fokus değişiminden beri */
    uint32_t focus_bad_ms;
    float prev_kv_ref, prev_ma_ref;
    uint32_t ref_bad_ms;
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

/* Açılış değerleri: radyografi, çift fokuslu insertte küçük fokus.
 * HAL kurulu inserti (tube_select) seçtikten sonra yeniden çağırır. */
void sm_load_boot_defaults(exposure_sm_t *sm);

/* Ekrandan veya DR yazılımından gelen ayar (arayüz tanımı rev. 0.4 §4):
 *  - BEKLE / KİLİT / HATA: serbest
 *  - HAZIRLIK / HAZIR: kV, mA, süre kabul edilir; değer değiştiyse filaman
 *    ısınması BAŞTAN başlar (hazır lambası söner). Fokus veya mod değişmez.
 *  - IŞINLAMA: reddedilir */
typedef enum {
    SM_SET_OK = 0,
    SM_SET_BUSY,             /* ışınlama sürüyor */
    SM_SET_FOCUS_LOCKED,     /* hazırlıkta fokus / mod değiştirilemez (E213) */
    SM_SET_FLUORO_DISABLED   /* floroskopi opsiyonu kapalı */
} sm_set_result_t;

sm_set_result_t sm_set_params_ex(exposure_sm_t *sm, const exposure_params_t *p);
bool sm_set_params(exposure_sm_t *sm, const exposure_params_t *p);   /* = ..._ex == SM_SET_OK */

/* Floroskopi bu cihazda kullanılabilir mi: servis opsiyonu açık VE kurulu
 * insert floroskopiye uygun (çift fokuslu). */
bool sm_fluoro_available(const exposure_sm_t *sm);

/* Tek tuşla RAD <-> FLUORO geçişi (Asım, 26.10.2026). Her mod kendi son
 * değerlerini hatırlar; floroskopiye ilk geçişte küçük fokus, 50 kV, 2 mA,
 * 20 s. Kurallar sm_set_params_ex ile aynı: hazırlıkta mod değişmez (E213),
 * opsiyon kapalıysa SM_SET_FLUORO_DISABLED. */
void sm_mode_target(const exposure_sm_t *sm, exp_mode_t mode, exposure_params_t *out);
sm_set_result_t sm_select_mode(exposure_sm_t *sm, exp_mode_t mode);
sm_set_result_t sm_toggle_mode(exposure_sm_t *sm);

/* DAP metre okuması (HAL, ışınlama bittikten sonra metre entegrasyonunu
 * tamamlayınca). Yalnız dap_meter_enabled iken kayda yazılır. */
bool sm_record_dap(exposure_sm_t *sm, float dgycm2);

/* PC / Manuel kontrol modu (panelden, yalnız BEKLE veya KİLİT durumunda).
 * required = PC modu; serial = PC bağlantısı RS-232 (Current). */
bool sm_set_pc_link(exposure_sm_t *sm, bool required, bool serial);

/* Her 1 ms'de çağrılır */
void sm_step(exposure_sm_t *sm, const sm_inputs_t *raw, sm_outputs_t *out, uint32_t dt_ms);

/* Hata onayı (ekrandaki "Onayla" + jeneratöre ALM RST) */
bool sm_ack_fault(exposure_sm_t *sm);

/* Floroskopi 5 dk sayacını operatör onayıyla sıfırlar */
void sm_fluoro_reset_cumulative(exposure_sm_t *sm);

const char *sm_state_text(sm_state_t s);
const char *sm_reason_text(end_reason_t r);

#endif
