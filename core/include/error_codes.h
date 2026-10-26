/*
 * Hata kodları – tek kaynak
 *
 * Ekrandaki mesaj, DR yazılımına gönderilen kod ve kullanım kılavuzundaki
 * "Hata kodları" bölümü bu tablodan üretilir (tools/gen_error_table).
 * Bir kodu değiştirmek = ekran + kayıt + kılavuz birlikte değişir.
 *
 * Kod aralıkları:
 *   E1xx  Kullanım ve oda güvenliği (operatör giderebilir)
 *   E2xx  Tüp koruması (ayar veya bekleme gerekir)
 *   E3xx  Jeneratör ve ışınlama kalitesi
 *   E4xx  Donanım ve kablo arızası (servis gerekir)
 *   E5xx  Dedektör ve haberleşme
 *   W6xx  Uyarılar (ışınlamayı engellemez)
 */
#ifndef ERROR_CODES_H
#define ERROR_CODES_H

#include <stdbool.h>
#include <stdint.h>
#include "exposure_sm.h"
#include "tube.h"

typedef enum {
    SEV_INFO = 0,     /* bilgi: yeniden denenebilir */
    SEV_WARNING,      /* uyarı: ışınlama engellenmez */
    SEV_ERROR,        /* hata: ekrandan onay gerekir */
    SEV_SERVICE       /* servis: onay + yetkili servis kontrolü */
} fe_severity_t;

typedef struct {
    uint16_t code;            /* ör. 101 -> ekranda "E101" */
    char prefix;              /* 'E' veya 'W' */
    fe_severity_t severity;
    const char *title;        /* ekranda gösterilen kısa mesaj */
    const char *cause;        /* olası neden (kılavuz) */
    const char *action;       /* kullanıcının yapacağı (ekran + kılavuz) */
} fe_error_t;

/* Durum makinesinin bitiş/kilit nedeninden hata kaydı.
 * Normal bitişlerde (END_NONE, END_TIME_DONE, END_RELEASED) NULL döner.
 * END_TUBE_REJECT için tüp kontrol sonucu ayrıntılı kodu belirler. */
const fe_error_t *fe_error_for(end_reason_t reason, tube_result_t tube);

/* Uyarılar */
const fe_error_t *fe_warning_fluoro_cumulative(void);

/* Tüm tablo (kılavuz üretimi ve testler için) */
const fe_error_t *fe_error_table(uint32_t *count);

const char *fe_severity_text(fe_severity_t s);

#endif
