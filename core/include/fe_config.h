/*
 * FalconEye kumanda paneli - sistem sabitleri
 *
 * Kaynaklar:
 *  - Referans ölçeği: EP-REF / IP-REF ölçümü (Asım, 29.10.2026; DF-161SBR küçük
 *    fokus), calib_default. Feedback ölçeği: nominal (6 V = 110 kV, 6 V = 50 mA)
 *  - Flaman ısınma süresi: kullanıcı (4 s)
 *  - Tüp sınırları: Canon PE-DF-161SBR (2018-11), bkz. tube.c
 */
#ifndef FE_CONFIG_H
#define FE_CONFIG_H

/* --- Analog ölçekler (J2 arayüzü) --- */
#define FE_KV_FULL_SCALE        110.0f   /* kV, 6 V'a karşılık */
#define FE_MA_FULL_SCALE         50.0f   /* mA, 6 V'a karşılık */
#define FE_REF_FULL_SCALE_V       6.0f   /* V */

/* --- Jeneratör --- */
#define FE_GEN_POWER_MAX_W     3200.0f   /* Mevcut jeneratör; 5 kW modelde değişir */
/* Eski panel 100 kV üstünde çalışmayacak şekilde sınırlıydı; bu invertör +
 * monoblokla 100 kV üstü hiç kullanılmadı (Asım, 29.10.2026). 100 kV üstü
 * yalnız kVp metre doğrulaması ve üretici onayıyla açılır. */
#define FE_GEN_KV_MAX           100.0f

/* --- Zamanlamalar (ms) --- */
#define FE_TICK_MS                    1u
#define FE_FILAMENT_WARMUP_MS      4000u   /* kullanıcı: 4 s */
#define FE_PREP_HOLD_MAX_MS       30000u   /* 1. kademe en uzun bekleme; flaman ömrü */
#define FE_FILAMENT_OK_TIMEOUT_MS  3000u   /* ısınma sonrası "Filaman tamam" bekleme payı */
#define FE_DETECTOR_READY_TIMEOUT_MS 10000u
#define FE_DETECTOR_ENABLE_TIMEOUT_MS  1000u   /* senkron: ışınlama izni bekleme */
#define FE_KV_RISE_MS                20u   /* tolerans kontrolü bu süreden sonra başlar */
#define FE_TOL_VIOLATION_MS          50u   /* tolerans dışında kalma süresi -> hata */
#define FE_INPUT_DEBOUNCE_MS         15u
#define FE_CHANNEL_MISMATCH_MS       20u   /* NO/NC veya A/B tutarsızlığı -> hata */
#define FE_ESTOP_MISMATCH_MS        100u   /* iki acil stop kontağı aynı anda açılmalı */
#define FE_LINE_MISMATCH_MS           5u   /* J2 hat geri okuma uyuşmazlığı -> hata */
#define FE_FOCUS_SETTLE_MS           50u   /* fokus rölesi oturma süresi (filaman sürülmeden önce) */
#define FE_FOCUS_MISMATCH_MS        100u   /* röle geri bildirimi komutla uyuşmuyor -> E406 */
#define FE_REF_MISMATCH_MS           20u   /* DAC geri okuması uyuşmuyor -> E407 */
#define FE_REF_TOL_KV               1.0f   /* kV referansı geri okuma toleransı (en az) */
#define FE_REF_TOL_MA               0.5f   /* mA referansı geri okuma toleransı (en az) */
#define FE_REF_TOL_FRAC            0.03f   /* veya ayarın %3'ü (büyük olanı) */
#define FE_BUTTON_STUCK_MS        10000u   /* çekimden sonra 10 s basılı kalan buton -> takılı */
#define FE_BUTTON_HELD_MAX_MS     60000u   /* diğer kilitlerde 60 s */

/* --- Ekspozisyon toleransları --- */
#define FE_KV_LOW_FRAC            0.85f   /* kullanıcı: kV feedback < %85 -> hata */
#define FE_KV_HIGH_FRAC           1.10f   /* kV feedback > %110 -> hata */
#define FE_IRR_THRESHOLD_FRAC     0.75f   /* ışınlama süresi: kV >= %75 olduğu süre (standarda göre doğrulanacak) */
#define FE_TIME_TOL_FRAC          0.10f   /* süre toleransı: ±(%10 + 1 ms) (standarda göre doğrulanacak) */
#define FE_TIME_TOL_MS               1u
#define FE_MA_TOL_FRAC            0.20f

/* --- Floroskopi --- */
#define FE_FLUORO_KV_MIN           40.0f
#define FE_FLUORO_KV_MAX           60.0f
#define FE_FLUORO_TIME_MAX_MS     20000u
#define FE_FLUORO_CUMULATIVE_ALERT_MS (5u * 60u * 1000u)  /* IEC 60601-2-54: 5 dk uyarı */

/* --- Güvenlik payı (yük eğrileri grafikten okunduğu için) --- */
#define FE_RATING_DERATE          0.90f
#define FE_HEAT_DERATE            0.90f

#endif
