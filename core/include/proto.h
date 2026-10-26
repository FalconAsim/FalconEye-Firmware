/*
 * Teknogem jeneratör protokolü (TGP) v1
 *
 * DR yazılımı / konsol <-> kumanda paneli. Ethernet (TCP) ve RS-232 üzerinde
 * aynı satır tabanlı ASCII komut seti. Ayrıntı: docs/generator_protocol_EN.md
 *
 * Güvenlik kuralları (protokol tasarımıyla garanti edilir):
 *  - Protokolde ışınlama başlatan komut YOKTUR. Işınlama yalnızca el butonu
 *    veya ayak pedalıyla, donanım güvenlik zincirinden geçerek başlar.
 *  - Hata onayı (ACK) yalnızca panelden yapılır; uzaktan reddedilir.
 *  - Hazırlık veya ışınlama sırasında ayar değiştirilemez.
 */
#ifndef PROTO_H
#define PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "exposure_sm.h"

#define TGP_VERSION        1
#define TGP_LINE_MAX     128
#define TGP_REPLY_MAX    256
#define TGP_HEARTBEAT_TIMEOUT_MS 3000u

/* Protokol hata kodları (cihaz hata kodlarından ayrı aralık: 9xx) */
#define TGP_ERR_BUSY        900   /* hazırlık/ışınlama sürüyor */
#define TGP_ERR_SYNTAX      901
#define TGP_ERR_RANGE       902   /* değer aralık dışı / eksik parametre */
#define TGP_ERR_CHECKSUM    903
#define TGP_ERR_UNKNOWN     904
#define TGP_ERR_LOCAL_ONLY  905   /* yalnızca panelden yapılabilir */
#define TGP_ERR_DISABLED    906   /* özellik bu cihazda kapalı (ör. floroskopi) */

typedef struct {
    exposure_sm_t *sm;
    bool pc_detector_ready;     /* DETREADY komutuyla PC'nin bildirdiği hazır durumu */
    uint32_t since_rx_ms;       /* son geçerli satırdan beri geçen süre */
    bool link_up;
    const char *fw_version;
} tgp_ctx_t;

void tgp_init(tgp_ctx_t *c, exposure_sm_t *sm, const char *fw_version);

/* Bir satırı işler, cevabı reply'a yazar (\r\n dahil). Cevap uzunluğunu döner. */
size_t tgp_handle_line(tgp_ctx_t *c, const char *line, char *reply, size_t reply_max);

/* Zaman ilerletme: bağlantı kopmasını izler */
void tgp_tick(tgp_ctx_t *c, uint32_t dt_ms);

/* Olay satırları (panel kendiliğinden gönderir) */
size_t tgp_fmt_event_exposure(const exposure_record_t *r, char *out, size_t max);
size_t tgp_fmt_event_state(const exposure_sm_t *sm, char *out, size_t max);

/* NMEA tarzı sağlama toplamı: '*' öncesi karakterlerin XOR'u */
uint8_t tgp_checksum(const char *s, size_t len);

#endif
