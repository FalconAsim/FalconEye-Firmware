/*
 * Servis menüsü: cihaz opsiyonları ve kurulu insert (Asım, 26.10.2026)
 *
 *  - Floroskopi opsiyonu: sistemde hazır, servis menüsünden açılır/kapanır.
 *    Açıkken panelde tek tuşla RAD <-> FLUORO geçilir (sm_toggle_mode).
 *  - DAP metre: servis menüsünden açılır/kapanır. Kapalıyken (veteriner
 *    versiyonu) kart DAP göndermez, DAP'ı FalconXray hesaplar.
 *  - Kurulu insert: servis menüsünden seçilir.
 *  - Büyük fokus (çift fokuslu insertte): servis menüsünden açılır/kapanır.
 *    Varsayılan kapalı: cihaz DF-161SBR ile yalnız küçük fokusta çalışıyor
 *    (Asım, 29.10.2026).
 *
 * Kurallar (risk notu, arayüz tanımı §10):
 *  - Servis moduna yalnız panelden (Kart A ekranı) PIN ile girilir; DR
 *    yazılımından (TGP) girilemez. 3 yanlış PIN -> 60 s kilit.
 *  - Ayar değişikliği yalnız BEKLE / KİLİT / HATA durumunda; hazırlık, hazır
 *    ve ışınlamada reddedilir.
 *  - Her değişiklik kalıcı belleğe yazılır ve geri okunarak doğrulanır;
 *    yazma başarısızsa değişiklik geri alınır.
 *  - Insert değişince ısı modeli SIFIRLANMAZ (tube_select) ve açılış
 *    değerleri yeni insertin sınırlarına göre yeniden yüklenir.
 *  - Floroskopi kapatılırken cihaz floroskopi modundaysa radyografiye döner.
 *  - Servis modu 10 dk işlem yapılmazsa kendiliğinden kapanır.
 */
#ifndef SERVICE_H
#define SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "exposure_sm.h"
#include "nvstore.h"

#define SVC_PIN_TRIES        3u
#define SVC_LOCKOUT_MS       60000u
#define SVC_IDLE_TIMEOUT_MS  (10u * 60u * 1000u)

typedef enum {
    SVC_OK = 0,
    SVC_ERR_NOT_ACTIVE,     /* servis moduna girilmedi */
    SVC_ERR_BAD_PIN,
    SVC_ERR_LOCKED_OUT,     /* çok fazla yanlış PIN, bekleniyor */
    SVC_ERR_BUSY,           /* hazırlık / hazır / ışınlama sürüyor */
    SVC_ERR_PARAM,
    SVC_ERR_SAVE,           /* kalıcı belleğe yazılamadı, değişiklik geri alındı */
    SVC_ERR_NO_EXPOSURE     /* kalibrasyon: geçerli (tam süre) son çekim yok */
} svc_result_t;

typedef struct {
    exposure_sm_t *sm;
    nvstore_t *nv;
    nv_data_t *data;
    uint32_t pin;
    bool active;
    uint8_t bad_tries;
    uint32_t lockout_ms;
    uint32_t idle_ms;
} svc_t;

/* Açılışta: kalıcı ayarları durum makinesine uygular (insert, floroskopi,
 * DAP metre) ve açılış değerlerini yükler. Kayıtlı insert geçersizse
 * varsayılan insert kullanılır ve false döner. */
bool svc_apply_boot(exposure_sm_t *sm, const nv_settings_t *st);

void svc_init(svc_t *svc, exposure_sm_t *sm, nvstore_t *nv, nv_data_t *data, uint32_t pin);
svc_result_t svc_enter(svc_t *svc, uint32_t pin);
void svc_exit(svc_t *svc);
void svc_tick(svc_t *svc, uint32_t dt_ms);

svc_result_t svc_set_fluoro(svc_t *svc, bool on);
svc_result_t svc_set_dap_meter(svc_t *svc, bool on);
svc_result_t svc_set_insert(svc_t *svc, tube_insert_t id);
svc_result_t svc_set_large_focus(svc_t *svc, bool on);

/* --- Işınlamalı kalibrasyon (Asım, 30.10.2026: klasik yöntem, nokta nokta) ---
 * 1) Servis modunda kV (veya mA) ve fokus ayarlanır, normal çekim yapılır
 *    (el butonu, bütün güvenlik zinciri devrede).
 * 2) kVp metrenin (veya mA metrenin) okuduğu değer girilir.
 * 3) Firmware SON ÇEKİMİN ayarlarını kullanır (yanlış noktaya yazmayı önler):
 *    çekim tam süre bitmiş olmalı (END_TIME_DONE).
 * 4) Sonuç CAL_OK ise aynı değerle tekrar çekilir; CAL_IN_TOL olunca nokta
 *    doğrulanmış olur ve sıradaki noktaya geçilir. Her adım FRAM'e yazılır.
 * Toleranslar: kV en çok (0,5 kV, %1); mA en çok (0,1 mA, %2). */
#define SVC_CAL_KV_TOL_ABS   0.5f
#define SVC_CAL_KV_TOL_FRAC  0.01f
#define SVC_CAL_MA_TOL_ABS   0.1f
#define SVC_CAL_MA_TOL_FRAC  0.02f

svc_result_t svc_cal_kv(svc_t *svc, float measured_kv, cal_result_t *res, float *v_new);
svc_result_t svc_cal_ma(svc_t *svc, float measured_ma, cal_result_t *res, float *v_new);
/* Kalibrasyonu fabrika başlangıç tablolarına döndürür (kV, mA veya ikisi) */
svc_result_t svc_cal_reset(svc_t *svc, bool kv, bool ma);

const char *svc_result_text(svc_result_t r);

#endif
