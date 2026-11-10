/*
 * PC simülatörü: jeneratör, flaman ve operatör davranışını taklit eder.
 * Kart gelmeden durum makinesini ve tüp korumasını test etmek için.
 */
#ifndef HAL_SIM_H
#define HAL_SIM_H

#include "exposure_sm.h"

typedef struct {
    /* Operatör ve ortam (testler değiştirir) */
    bool hs1, hs2, pedal;
    bool door_ok, thermal_ok, estop_ok;
    bool box_hot;           /* thermal_mon kilidi (kutu ≥ 50 °C) */
    bool gen_alarm, gen_over;
    bool backup_ok;
    bool detector_ready;
    bool detector_enable, detector_error;
    bool pc_link_ok;        /* PC modu: yazılım bağlantısı canlı (varsayılan true) */
    /* Tetikleme modülü modeli: her gate_period_ms'de bir, gate_dly_ms'de açılıp gate_len_ms süren pencere.
     * gate_period_ms = 0: pencere hiç gelmez. gate_first_x_ms: pencere açıldıktan X-RAY ON'a kadar geçen süre (ölçüm). */
    bool gate_module_ok;    /* varsayılan true */
    uint32_t gate_period_ms, gate_dly_ms, gate_len_ms;
    bool xray_outside_gate; /* kontrol: X-RAY ON, pencere kapalıyken başladı */
    uint32_t gate_x_lag_ms; /* son çekimde pencere açılışından X-RAY ON'a gecikme */
    bool gate_prev_x;

    /* Fokus rölesi ve referans geri okuması */
    bool focus_fb;               /* röle geri bildirimi (çekili = küçük) */
    uint32_t focus_delay_ms;     /* röle oturma süresi */
    uint32_t focus_move_ms;
    bool focus_stuck;            /* hata enjeksiyonu: röle konum değiştirmiyor */
    float dac_kv_error, dac_ma_error;   /* hata enjeksiyonu: DAC çıkışında sapma */
    bool hot_switch;             /* kontrol: röle filaman sürülürken hareket etti */

    /* Kablo ve buton arızası enjeksiyonu */
    bool hs1_no_short;      /* 1. kademe NO hattı kısa devre (basılı görünür) */
    bool hs2_no_short;      /* 2. kademe NO hattı kısa devre */
    bool hs2_nc_open;       /* 2. kademe NC hattı kopuk */
    bool pedal_b_short;     /* pedal B kanalı kısa devre */
    bool estop_ch2_short;   /* acil stop 2. kanal kısa devre (basılınca açılmıyor) */
    bool xray_line_short;   /* J2 X-RAY ON hattı GND'ye kısa devre */
    bool xray_line_open;    /* J2 X-RAY ON hattı kopuk */
    bool hv_from_short;     /* kısa devre sonucu jeneratör ışınladı (Inter-Lock yoksa) */

    /* Dirençle denetlenen el butonu (2 telli kademe). use_hs_adc=true ise
     * hs1/hs2 yerine bu ADC kodları okunur (bkz. hs_line.h). */
    bool use_hs_adc;
    uint16_t hs1_adc, hs2_adc;

    /* Jeneratör modeli */
    uint32_t filament_heat_ms;     /* PreHeat sürekli aktif olduğu süre */
    uint32_t filament_ready_after_ms;
    bool filament_broken;          /* hata enjeksiyonu */
    float kv, ma;                  /* çıkıştaki gerçek değerler */
    float kv_gain_error;           /* ör. 0.2 => %20 düşük kV (hata enjeksiyonu) */
    float rise_tau_ms;

    /* Sıcaklık sürüklenmesi modeli (1.0 / 0 = sürüklenme yok). Ayrıntı: tests/sim_sicaklik.c
     *  vref_gain   : REF3030 sürüklenmesi. DAC ve ADC aynı referansı kullanır (oransal):
     *                jeneratöre giden referans × g, ADC okuması / g.
     *  kv_div_gain : Kart B kV geri besleme bölücüsü + op-amp kazanç sürüklenmesi (yalnız ölçüm)
     *  ma_div_gain : Kart B mA geri besleme bölücüsü + op-amp kazanç sürüklenmesi (yalnız ölçüm)
     *  kv_fb_offset, ma_fb_offset : op-amp ofset sürüklenmesi (kV, mA biriminde)
     *  ma_gain_error : jeneratör mA çıkışının referanstan sapması (kv_gain_error'ün mA karşılığı) */
    float vref_gain, kv_div_gain, ma_div_gain;
    float kv_fb_offset, ma_fb_offset;
    float ma_gain_error;
    /* Gerçek (sürüklenmesiz) çıkış kaydı: kV ≥ referansın %75'i olduğu süre boyunca toplam */
    float kv_true_acc, ma_true_acc;
    uint32_t hv_true_ms;

    /* Kayıt */
    uint32_t now_ms;
    uint32_t xray_on_ms;           /* toplam X-RAY ON süresi */
    bool xray_without_rl5;         /* güvenlik ihlali bayrağı */
    sm_outputs_t last_out;
} sim_t;

void sim_init(sim_t *s);

/* Durum makinesini ms adımlarla çalıştırır */
void sim_run(sim_t *s, exposure_sm_t *sm, uint32_t ms);

#endif
