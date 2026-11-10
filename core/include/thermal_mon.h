/*
 * Kumanda kutusu iç sıcaklığı ve fan izleme (Kart B).
 *
 * Girdiler: kutu iç sıcaklığı (°C; STM32G474 iç sensörü + yakma testinde ölçülen ofset,
 * ya da ileride ayrı NTC) ve fan takometre frekansı (Hz, J9 FAN_TACH).
 * Çıktılar: fan komutu (FAN_EN), W606 / W607 uyarıları, en yüksek sıcaklık kaydı.
 *
 * Bu modül çekimi ENGELLEMEZ; yalnız uyarı üretir. Sıcaklıkta çekim kilidi açık karardır
 * (Sicaklik_Guvenilirlik_Dogrulama rev 0.1, karar K-T3).
 *
 * Fan varsayılanı "cihaz açıkken sürekli çalışır": kontrol hatası fanı durduramaz,
 * seçilen fan (12 dB(A) sınıfı) zaten sessiz. İstenirse termostatik moda geçilir.
 */
#ifndef THERMAL_MON_H
#define THERMAL_MON_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool fan_always_on;       /* true: fan sürekli (varsayılan) */
    float fan_on_c, fan_off_c;/* termostatik mod histerezisi */
    float warn_c, clear_c;    /* W606 eşiği ve bırakma eşiği */
    float tach_min_hz;        /* bunun altı = fan durmuş / yavaş */
    uint32_t fan_start_ms;    /* fan açıldıktan sonra takometre denetimi için bekleme */
    uint32_t fan_fail_ms;     /* takometre bu süre düşük kalırsa W607 */
} thermal_cfg_t;

typedef struct {
    thermal_cfg_t cfg;
    bool fan_cmd;             /* FAN_EN çıkışı */
    bool warn_temp;           /* W606 etkin */
    bool warn_fan;            /* W607 etkin (kalıcı; onayla temizlenir) */
    bool sensor_bad;          /* sıcaklık okuması geçersiz */
    uint32_t fan_on_ms;       /* fan açık süresi (başlangıç beklemesi için) */
    uint32_t low_tach_ms;
    float t_c, t_max_c;       /* son ve en yüksek sıcaklık (yakma testi kaydı) */
} thermal_mon_t;

void thermal_default_cfg(thermal_cfg_t *c);
void thermal_init(thermal_mon_t *t, const thermal_cfg_t *c);

/* Her dt_ms'de bir çağrılır (ör. 100 ms). t_c geçersizse (NaN, < -40, > 125) sensör arızası. */
void thermal_step(thermal_mon_t *t, float t_c, float tach_hz, uint32_t dt_ms);

/* W607 operatör onayı; fan hâlâ durmuşsa süre dolunca yeniden kurulur */
void thermal_ack_fan(thermal_mon_t *t);

#endif
