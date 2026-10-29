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
    SVC_ERR_SAVE            /* kalıcı belleğe yazılamadı, değişiklik geri alındı */
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

const char *svc_result_text(svc_result_t r);

#endif
