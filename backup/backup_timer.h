/*
 * Yedek zamanlayıcı (STM32G031, ayrı MCU, kendi saati)
 *
 * Görev: ana MCU ne yaparsa yapsın, ışınlamanın ayarlanan süreyi ve mutlak
 * üst sınırı aşmasını donanımsal olarak engellemek.
 *
 * Çıkış ENABLE, X-RAY ON yolundaki AND kapısının bir girişidir. Varsayılan
 * kapalıdır; yalnızca ana MCU geçerli bir ARM mesajı gönderdiğinde açılır.
 *
 *  - ARM <süre_ms> <mod> <sıra> : ana MCU HAZIR durumunda gönderir (en az
 *    500 ms'de bir yeniler). ARM 1 s içinde yenilenmezse geçersiz olur.
 *  - X-RAY ON hattı (geri okuma) aktif olunca sayım başlar. Sınır:
 *    süre × 1.1 + 10 ms, ve mod için mutlak üst sınır (radyografi 10 s,
 *    floroskopi 20 s; DF-161SBR).
 *  - Sınır aşılırsa ENABLE düşer ve TRIP kilitlenir; yalnızca hat pasifken
 *    gelen CLEAR ile kalkar.
 *  - Işınlama bitince (hat pasif) ENABLE düşer: her çekim yeni ARM ister.
 *  - Silahlanmadan hat aktif görülürse (AND kapısı arızası) TRIP.
 */
#ifndef BACKUP_TIMER_H
#define BACKUP_TIMER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BT_RAD_ABS_MAX_MS     10000u
#define BT_FLUORO_ABS_MAX_MS  20000u
#define BT_MARGIN_FRAC          0.10f
#define BT_MARGIN_MS             10u
#define BT_ARM_VALID_MS        1000u

typedef enum { BT_MODE_RAD = 0, BT_MODE_FLUORO = 1 } bt_mode_t;

typedef enum {
    BT_IDLE = 0,     /* silahsız, ENABLE kapalı */
    BT_ARMED,        /* ENABLE açık, ışınlama bekleniyor */
    BT_TIMING,       /* ışınlama sürüyor, sayılıyor */
    BT_TRIPPED       /* sınır aşıldı veya arıza: ENABLE kapalı, kilitli */
} bt_state_t;

typedef enum {
    BT_TRIP_NONE = 0,
    BT_TRIP_TIME,          /* süre sınırı aşıldı */
    BT_TRIP_UNARMED_XRAY,  /* silahsızken X-RAY ON hattı aktif */
} bt_trip_t;

typedef struct {
    bt_state_t state;
    bt_trip_t trip;
    uint32_t limit_ms;
    uint32_t count_ms;
    uint32_t arm_age_ms;
    uint16_t last_seq;
    bool enable;            /* ENABLE çıkışı */
} backup_timer_t;

void bt_init(backup_timer_t *b);

/* Mesajlar (UART çerçevesi çözüldükten sonra) */
bool bt_arm(backup_timer_t *b, uint32_t set_ms, bt_mode_t mode, uint16_t seq);
bool bt_clear(backup_timer_t *b, bool xray_line_active);

/* Her 1 ms: X-RAY ON hattının gerçek durumu */
void bt_tick(backup_timer_t *b, bool xray_line_active, uint32_t dt_ms);

/* UART çerçevesi: [0xA5][tip][süre_ms 4B][mod 1B][sıra 2B][CRC8] */
#define BT_FRAME_LEN 10
#define BT_MSG_ARM   0x01
#define BT_MSG_CLEAR 0x02
size_t bt_encode(uint8_t type, uint32_t set_ms, uint8_t mode, uint16_t seq, uint8_t out[BT_FRAME_LEN]);
bool bt_decode_and_apply(backup_timer_t *b, const uint8_t f[BT_FRAME_LEN], bool xray_line_active);
uint8_t bt_crc8(const uint8_t *d, size_t n);

#endif
