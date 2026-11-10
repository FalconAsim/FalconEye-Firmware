/*
 * Kart M (hayvan modülü) <-> Kart A CAN-FD protokolü – FalconEye_Arayuz_Tanimi.md §15 (rev 0.9)
 *
 * Bu dosya iki tarafta da (Kart M ve Kart A firmware'i) aynen kullanılır.
 * Hat: FDCAN, 11 bit kimlik, nominal 1 Mbit/s, veri 2 Mbit/s. Çok baytlı alanlar küçük uçlu (little-endian).
 * Küçük kimlik = yüksek öncelik: tetik penceresi her şeyden önce gider.
 *
 * GÜVENLİK: Kart M ışınlama BAŞLATAMAZ. Yalnız "pencere açık" bilgisini verir; çekim el butonunun
 * iki kademesine ve Kart B güvenlik zincirine bağlıdır. Pencere mesajı kaybolursa pencere KAPALI sayılır.
 */
#ifndef KM_PROTO_H
#define KM_PROTO_H
#include <stdint.h>
#include <stdbool.h>

/* ---- CAN kimlikleri ---- */
#define KM_ID_WIN        0x010u  /* M -> A  tetik penceresi (8 bayt) – A aynen Kart B hattına köprüler */
#define KM_ID_R_EVT      0x020u  /* M -> A  R dalgası olayı (8 bayt) */
#define KM_ID_RESP_EVT   0x030u  /* M -> A  nefes olayı (8 bayt) */
#define KM_ID_CFG        0x100u  /* A -> M  tetikleme ayarı (8 bayt) */
#define KM_ID_CFG_ACK    0x101u  /* M -> A  ayar cevabı (8 bayt) */
#define KM_ID_CMD        0x110u  /* A -> M  komut (8 bayt) */
#define KM_ID_CMD_ACK    0x111u  /* M -> A  komut cevabı (8 bayt) */
#define KM_ID_STATUS     0x180u  /* M -> A  durum, 10 Hz (16 bayt, FD) */
#define KM_ID_WAVE_ECG   0x200u  /* M -> A  EKG dalgası 250 örnek/s, 10 örnek (24 bayt, FD) */
#define KM_ID_WAVE_RESP  0x201u  /* M -> A  solunum dalgası 50 örnek/s, 10 örnek (24 bayt, FD) */
#define KM_ID_HB_A       0x300u  /* A -> M  canlılık, 10 Hz (8 bayt) */
#define KM_ID_VERSION    0x310u  /* M -> A  sürüm (16 bayt, FD) */
#define KM_ID_BOOT_BASE  0x700u  /* 0x700–0x70F önyükleyiciye ayrıldı */

/* ---- zamanlama ---- */
#define KM_WIN_TX_OPEN_MS     2u    /* pencere açıkken tekrar aralığı */
#define KM_WIN_TX_CLOSED_MS   20u   /* pencere kapalıyken tekrar aralığı */
#define KM_WIN_MAX_AGE_MS     10u   /* alıcı: son "açık" mesajı bundan eskiyse pencere KAPALI */
#define KM_STATUS_PERIOD_MS   100u
#define KM_STATUS_TIMEOUT_MS  300u  /* Kart A: bu süre durum gelmezse E408 */
#define KM_HB_PERIOD_MS       100u
#define KM_HB_TIMEOUT_MS      500u  /* Kart M: bu süre canlılık gelmezse dalga akışını kapatır */

/* ---- sınırlar ---- */
#define KM_DLY_MAX_MS   1000u
#define KM_WIN_MIN_MS   5u
#define KM_WIN_MAX_MS   1000u

typedef enum { KM_SRC_OFF = 0, KM_SRC_ECG = 1, KM_SRC_RESP = 2, KM_SRC_BOTH = 3 } km_src_t;
typedef enum { KM_RPH_EXP = 0, KM_RPH_INS = 1 } km_rph_t;
typedef enum { KM_SP_MOUSE = 0, KM_SP_RAT = 1, KM_SP_CAT = 2, KM_SP_DOG = 3, KM_SP_OTHER = 4, KM_SP_COUNT } km_species_t;
typedef enum { KM_RSRC_NONE = 0, KM_RSRC_PILLOW = 1, KM_RSRC_FLOW = 2, KM_RSRC_IMP = 3 } km_rsrc_t;
typedef enum { KM_ECGQ_OK = 0, KM_ECGQ_NOISE = 1, KM_ECGQ_LEADOFF = 2, KM_ECGQ_NOSIG = 3 } km_ecgq_t;

/* elektrot kopukluk maskesi */
#define KM_LEAD_RA 0x01u
#define KM_LEAD_LA 0x02u
#define KM_LEAD_LL 0x04u
#define KM_LEAD_RL 0x08u

/* KM_ID_WIN bayrakları */
#define KM_WINF_ECG_OK   0x01u
#define KM_WINF_RESP_OK  0x02u

/* ayar cevabı */
typedef enum {
    KM_CFG_OK = 0, KM_CFG_BAD_SRC = 1, KM_CFG_DLY_SMALL = 2, KM_CFG_DLY_LARGE = 3,
    KM_CFG_WIN_RANGE = 4, KM_CFG_BAD_SPECIES = 5, KM_CFG_BAD_RPH = 6, KM_CFG_NO_RSRC = 7
} km_cfg_result_t;

/* komutlar */
typedef enum {
    KM_CMD_RESP_ZERO = 1,   /* solunum taban değerini yeniden öğren */
    KM_CMD_RESP_SRC = 2,    /* arg: km_rsrc_t (0 = otomatik) */
    KM_CMD_SELFTEST = 3,
    KM_CMD_VERSION = 4,
    KM_CMD_RESET = 5,       /* magic gerekli */
    KM_CMD_BOOT = 6         /* önyükleyiciye geç – magic gerekli */
} km_cmd_t;
#define KM_CMD_MAGIC 0xB007C0DEu

/* modül hata bitleri (durum mesajı) */
#define KM_ERR_ADS      0x01u   /* EKG ön ucu cevap vermiyor */
#define KM_ERR_ABP      0x02u   /* yastık / kemer basınç sensörü */
#define KM_ERR_SDP      0x04u   /* T parçası fark basınç sensörü */
#define KM_ERR_RBASE    0x08u   /* solunum taban basıncı aralık dışı (W605) */
#define KM_ERR_CFG      0x10u   /* geçerli ayar yok */
#define KM_ERR_HB       0x20u   /* Kart A canlılığı yok */

typedef struct { uint8_t seq; bool open; uint16_t remain_ms; uint8_t src; uint8_t flags; uint16_t cfg_crc; } km_win_t;
typedef struct { uint32_t t_ms; uint16_t rr_ms; int16_t amp_uv; } km_revt_t;
typedef struct { uint32_t t_ms; uint16_t period_ms; uint8_t phase; uint8_t level; } km_respevt_t;
typedef struct { uint8_t src; uint8_t rph; uint8_t species; uint8_t flags; uint16_t dly_ms; uint16_t win_ms; } km_cfg_t;
#define KM_CFGF_RESP_INVERT 0x01u
typedef struct { uint8_t result; uint8_t rsv; uint16_t cfg_crc; uint16_t min_dly_ms; uint16_t max_win_ms; } km_cfg_ack_t;
typedef struct { uint8_t cmd; uint8_t arg; uint16_t rsv; uint32_t magic; } km_cmd_msg_t;
typedef struct { uint8_t cmd; uint8_t result; } km_cmd_ack_t;
typedef struct {
    uint8_t src;         /* etkin km_src_t */
    uint8_t ecg_q;       /* km_ecgq_t */
    uint8_t lead_mask;   /* KM_LEAD_* */
    uint8_t rsrc;        /* km_rsrc_t */
    uint16_t hr_bpm;     /* 0 = bilinmiyor */
    uint16_t rr_x10;     /* nefes / dk × 10; 0 = bilinmiyor */
    int16_t rbase;       /* yastık: 10 Pa birimi (kPa × 100); akış: Pa; empedans: mΩ */
    uint16_t cfg_crc;
    uint8_t err;         /* KM_ERR_* */
    uint8_t wave_on;
    uint16_t uptime_s;
} km_status_t;
typedef struct { uint32_t t_ms; int16_t s[10]; } km_wave_t;
typedef struct { uint8_t seq; uint8_t wave_on; uint32_t a_time_ms; } km_hb_t;
typedef struct { uint8_t proto; uint8_t fw_major; uint8_t fw_minor; uint8_t fw_patch; uint8_t hw_rev; uint32_t uid; } km_version_t;
#define KM_PROTO_VERSION 1u

/* paketleme: dönen değer bayt sayısı; açma: uzunluk yanlışsa false */
uint8_t km_pack_win(const km_win_t *m, uint8_t *d);
bool    km_unpack_win(const uint8_t *d, uint8_t len, km_win_t *m);
uint8_t km_pack_revt(const km_revt_t *m, uint8_t *d);
bool    km_unpack_revt(const uint8_t *d, uint8_t len, km_revt_t *m);
uint8_t km_pack_respevt(const km_respevt_t *m, uint8_t *d);
bool    km_unpack_respevt(const uint8_t *d, uint8_t len, km_respevt_t *m);
uint8_t km_pack_cfg(const km_cfg_t *m, uint8_t *d);
bool    km_unpack_cfg(const uint8_t *d, uint8_t len, km_cfg_t *m);
uint8_t km_pack_cfg_ack(const km_cfg_ack_t *m, uint8_t *d);
bool    km_unpack_cfg_ack(const uint8_t *d, uint8_t len, km_cfg_ack_t *m);
uint8_t km_pack_cmd(const km_cmd_msg_t *m, uint8_t *d);
bool    km_unpack_cmd(const uint8_t *d, uint8_t len, km_cmd_msg_t *m);
uint8_t km_pack_cmd_ack(const km_cmd_ack_t *m, uint8_t *d);
bool    km_unpack_cmd_ack(const uint8_t *d, uint8_t len, km_cmd_ack_t *m);
uint8_t km_pack_status(const km_status_t *m, uint8_t *d);
bool    km_unpack_status(const uint8_t *d, uint8_t len, km_status_t *m);
uint8_t km_pack_wave(const km_wave_t *m, uint8_t *d);
bool    km_unpack_wave(const uint8_t *d, uint8_t len, km_wave_t *m);
uint8_t km_pack_hb(const km_hb_t *m, uint8_t *d);
bool    km_unpack_hb(const uint8_t *d, uint8_t len, km_hb_t *m);
uint8_t km_pack_version(const km_version_t *m, uint8_t *d);
bool    km_unpack_version(const uint8_t *d, uint8_t len, km_version_t *m);

/* Ayarın 8 baytı üzerinden CRC-16/CCITT-FALSE: pencere mesajı hangi ayarla üretildiğini taşır */
uint16_t km_cfg_crc(const km_cfg_t *c);

/* ---- alıcı tarafı (Kart A ve Kart B): pencere geçerliliği ---- */
typedef struct { km_win_t last; uint32_t rx_ms; bool have; } km_win_rx_t;
void km_win_rx_init(km_win_rx_t *r);
void km_win_rx_update(km_win_rx_t *r, const km_win_t *m, uint32_t now_ms);
/* Pencere şimdi açık mı ve en az need_ms kadar açık kalacak mı?
 * Mesaj yaşı KM_WIN_MAX_AGE_MS'yi geçtiyse ya da ayar CRC'si beklenenle uyuşmuyorsa false (hataya güvenli). */
bool km_win_rx_usable(const km_win_rx_t *r, uint32_t now_ms, uint16_t need_ms, uint16_t expect_cfg_crc);

#endif
