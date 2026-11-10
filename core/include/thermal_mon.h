/*
 * Kumanda kutusu iç sıcaklığı, çekim kilidi ve fan izleme (Kart B).
 *
 * Sıcaklık kaynakları (Asım 10.11.2026):
 *  1. Kart A NTC'leri (RT1 kart üstü, J49 kablolu prob) → CAN 0x330 (FE_CAN_ID_TBOX), 1 s'de bir.
 *     İkisi de geçerliyse yüksek olanı alınır (panel bölgesi en sıcak hava).
 *  2. Yedek: STM32G474 iç sensörü + FCT'de ölçülen ofset. Kart A verisi yoksa / bayatsa kullanılır → W608.
 *  İkisi de geçersizse çekim kilitlenir (hataya güvenli yön) → W608 + E221.
 *
 * Çekim kilidi: kutu içi ≥ 50 °C → yeni hazırlık yok (E221); 47 °C'de bırakır.
 * Devam eden hazırlık / ışınlama kesilmez (exposure_sm yalnız 1. kademe başlangıcında bakar).
 * W606 ön uyarı 47 °C (45 °C'de bırakır). W607 fan arızası.
 *
 * Fan varsayılanı "cihaz açıkken sürekli çalışır"; istenirse termostatik.
 */
#ifndef THERMAL_MON_H
#define THERMAL_MON_H

#include <stdbool.h>
#include <stdint.h>

#define FE_CAN_ID_TBOX         0x330u   /* Kart A → Kart B, FDCAN1 */
#define FE_TBOX_NONE           0x7FFF   /* sensör yok / arızalı */

typedef struct {
    bool fan_always_on;
    float fan_on_c, fan_off_c;   /* termostatik mod */
    float warn_c, warn_clear_c;  /* W606 */
    float lock_c, unlock_c;      /* E221 çekim kilidi */
    float tach_min_hz;
    uint32_t fan_start_ms, fan_fail_ms;
} thermal_cfg_t;

typedef enum { TSRC_NONE = 0, TSRC_NTC, TSRC_MCU } thermal_src_t;

typedef struct {
    thermal_cfg_t cfg;
    bool fan_cmd;          /* FAN_EN */
    bool lock;             /* E221: yeni çekim yok (exposure_sm box_hot girişi) */
    bool warn_temp;        /* W606 */
    bool warn_fan;         /* W607 (onayla temizlenir) */
    bool warn_sensor;      /* W608: Kart A NTC verisi yok, yedek sensör kullanılıyor ya da hiç sensör yok */
    thermal_src_t src;
    uint32_t fan_on_ms, low_tach_ms;
    float t_c, t_max_c;
} thermal_mon_t;

/* CAN 0x330 yükü (8 bayt, küçük uçlu): seq u8 · t_brd i16 (0,1 °C) · t_ext i16 (0,1 °C) · flags u8 · 2 bayt boş.
 * Sensör yok / kısa / açık: FE_TBOX_NONE. */
typedef struct { uint8_t seq; int16_t t_brd_d, t_ext_d; uint8_t flags; } tbox_msg_t;
void tbox_pack(const tbox_msg_t *m, uint8_t out[8]);
void tbox_unpack(const uint8_t in[8], tbox_msg_t *m);
/* Mesajdan kullanılacak NTC sıcaklığı (°C); ikisi de yoksa NaN */
float tbox_ntc_c(const tbox_msg_t *m);

void thermal_default_cfg(thermal_cfg_t *c);
void thermal_init(thermal_mon_t *t, const thermal_cfg_t *c);

/* Her dt_ms'de bir. t_ntc_c: Kart A'dan (mesaj > FE_TBOX_STALE_MS eskiyse HAL NaN verir).
 * t_mcu_c: iç sensör (ofset düzeltilmiş). Geçersiz değer: NaN ya da −40…125 dışı. */
void thermal_step(thermal_mon_t *t, float t_ntc_c, float t_mcu_c, float tach_hz, uint32_t dt_ms);
void thermal_ack_fan(thermal_mon_t *t);

#endif
