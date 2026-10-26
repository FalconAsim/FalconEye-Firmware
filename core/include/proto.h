/*
 * Teknogem jeneratör protokolü (TGP) v1 – arayüz tanımı rev. 0.4
 *
 * DR yazılımı (FalconXray) <-> kumanda paneli. TCP (varsayılan 5025) üzerinde
 * satır tabanlı ASCII:
 *
 *     <SEQ> <KOMUT>[ <ANAHTAR>=<DEĞER> ...]*<CRC16>\r\n
 *
 *   SEQ   : 4 haneli onlu sayı 0000-9999; her taraf kendi sayacını tutar.
 *   CRC16 : CRC-16/CCITT-FALSE, SEQ'in ilk karakterinden '*' öncesine kadar,
 *           4 haneli büyük harf hex.
 *
 * Ayrıntı: FalconEye_Arayuz_Tanimi.md rev. 0.4 ve docs/generator_protocol_EN.md
 *
 * Güvenlik kuralları (protokol tasarımıyla garanti edilir):
 *  - Işınlama, hazırlık veya hata onayı komutu YOKTUR (NAK E905).
 *  - Değer yazma yalnız PC modunda ve TGP seçiliyken (aksi halde NAK E907).
 *  - Işınlama sırasında değer değişmez (NAK E900).
 */
#ifndef PROTO_H
#define PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "exposure_sm.h"

#define TGP_VERSION        1
#define TGP_IF_REV         "0.4"
#define TGP_LINE_MAX     128          /* \r\n dahil */
#define TGP_REPLY_MAX    512          /* bir cevapta birden fazla satır olabilir */
#define TGP_TIMEOUT_MS           3000u   /* boşta / hazırlıkta canlılık eşiği */
#define TGP_TIMEOUT_XRAY_MS       750u   /* ışınlama sırasında canlılık eşiği */

/* Protokol cevap kodları (E9xx, yalnız NAK'ta; ekranda gösterilmez) */
#define TGP_ERR_BUSY        900   /* ışınlama sürüyor */
#define TGP_ERR_SYNTAX      901   /* söz dizimi / alan hatası */
#define TGP_ERR_NODATA      902   /* veri yok (ör. henüz çekim yok) */
#define TGP_ERR_CHECKSUM    903
#define TGP_ERR_UNKNOWN     904
#define TGP_ERR_LOCAL_ONLY  905   /* yalnız panelden yapılabilir */
#define TGP_ERR_DISABLED    906   /* özellik bu cihazda kapalı */
#define TGP_ERR_READONLY    907   /* Manuel mod veya etkin bağlantı TGP değil */

typedef enum { TGP_CTRL_PC = 0, TGP_CTRL_MANUAL } tgp_ctrl_t;
typedef enum { TGP_LINK_TGP = 0, TGP_LINK_CURRENT } tgp_link_sel_t;

typedef struct {
    exposure_sm_t *sm;
    const char *fw_version;
    const char *hw_rev;
    const char *serial;

    tgp_ctrl_t ctrl;            /* panel menüsünden: PC / Manuel */
    tgp_link_sel_t link_sel;    /* servis menüsünden: PC bağlantısı TGP / Current */

    bool pc_detector_ready;     /* DETREADY R=1 */
    bool link_up;
    uint32_t since_rx_ms;

    uint16_t tx_seq;            /* kartın gönderdiği satırların sayacı */
    int last_rx_seq;            /* yinelenen istek algılama (-1: yok) */
    char last_reply[TGP_REPLY_MAX];
    size_t last_reply_len;

    uint32_t xn;                /* çekim sayacı (DONE olaylarında) */
    uint32_t bad_lines;         /* servis sayacı: CRC / biçim hatalı satırlar */
} tgp_ctx_t;

void tgp_init(tgp_ctx_t *c, exposure_sm_t *sm, const char *fw_version);

/* Bir satırı işler, cevabı (bir veya birden fazla satır, \r\n dahil) reply'a
 * yazar. Cevap uzunluğunu döner; 0 = cevap yok (okunamayan satır atıldı). */
size_t tgp_handle_line(tgp_ctx_t *c, const char *line, char *reply, size_t reply_max);

/* Zaman ilerletme: canlılık eşiği ışınlamada 750 ms, diğer durumlarda 3 s */
void tgp_tick(tgp_ctx_t *c, uint32_t dt_ms);

/* Ethernet link kopması veya TCP kapanması: anında bağlantı yok */
void tgp_link_lost(tgp_ctx_t *c);

/* Yazılım değer yazabilir mi (PC modu + TGP seçili) */
bool tgp_writes_allowed(const tgp_ctx_t *c);

/* Olay satırları (panel kendiliğinden gönderir, onaylanmaz) */
size_t tgp_fmt_actual(tgp_ctx_t *c, char *out, size_t max);
size_t tgp_fmt_state(tgp_ctx_t *c, char *out, size_t max);
size_t tgp_fmt_done(tgp_ctx_t *c, const exposure_record_t *r, char *out, size_t max);
size_t tgp_fmt_error(tgp_ctx_t *c, char prefix, uint16_t code, bool cleared, char *out, size_t max);
size_t tgp_fmt_limits(tgp_ctx_t *c, char *out, size_t max);

/* CRC-16/CCITT-FALSE */
uint16_t tgp_crc16(const char *s, size_t len);

#endif
