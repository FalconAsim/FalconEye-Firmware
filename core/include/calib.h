/*
 * Kalibrasyon: ayarlanan kV/mA -> jeneratör referans gerilimi -> DAC kodu,
 * ve ADC kodu -> feedback gerilimi -> gerçek kV/mA.
 *
 * Kalibrasyon yokken nominal ölçek kullanılır (6 V = 110 kV, 6 V = 50 mA).
 * kVp metre ve mAs metre ölçümleri girildikçe çok noktalı tablolar devreye
 * girer; noktalar arasında doğrusal ara değer bulunur.
 *
 * Donanım sabitleri (blok şema):
 *   DAC: STM32 DAC 12 bit, VREF+ = 3.0 V (REF3030), OPA2192 kazancı x2
 *        -> en fazla 6.0 V (donanımsal tavan).
 *   ADC: 12k/10k bölücü (6.6 V -> 3.0 V), 12 bit, VREF+ = 3.0 V.
 */
#ifndef CALIB_H
#define CALIB_H

#include <stdbool.h>
#include <stdint.h>

#define CAL_MAX_POINTS   16
#define CAL_MA_KV_ROWS    4    /* mA tablosu: en fazla 4 kV satırı */

#define CAL_DAC_VREF      3.0f
#define CAL_DAC_GAIN      2.0f
#define CAL_DAC_MAX    4095u
#define CAL_ADC_VREF      3.0f
#define CAL_ADC_DIV   (10.0f / 22.0f)   /* 12k üst, 10k alt */
#define CAL_ADC_MAX    4095u
#define CAL_REF_MAX_V     6.0f          /* jeneratör referans tam skalası */

/* Tek boyutlu tablo: x artan sırada */
typedef struct {
    uint8_t n;
    float x[CAL_MAX_POINTS];
    float y[CAL_MAX_POINTS];
} cal_curve_t;

/* mA referans tablosu: her kV satırı için mA -> referans gerilimi */
typedef struct {
    uint8_t rows;
    float kv[CAL_MA_KV_ROWS];
    cal_curve_t ma_to_v[CAL_MA_KV_ROWS];
} cal_ma_table_t;

typedef struct {
    cal_curve_t kv_to_v;     /* ayar kV -> EP Cont (V) */
    cal_ma_table_t ma;       /* ayar mA (+kV) -> IP Cont (V) */
    cal_curve_t kvfb_v_to_kv;/* EP FB (V) -> gerçek kV (kVp metre) */
    cal_curve_t mafb_v_to_ma;/* IP FB (V) -> gerçek mA (mAs metre) */
} calib_t;

/* Nominal (kalibrasyonsuz) tabloları yükler */
void calib_default(calib_t *c);

/* Tablo geçerli mi: nokta sayısı, x kesin artan, y aralıkta */
bool calib_curve_valid(const cal_curve_t *k, float y_min, float y_max);
bool calib_valid(const calib_t *c);

/* Doğrusal ara değer; aralık dışında uçtaki iki noktadan doğrusal uzatma */
float calib_eval(const cal_curve_t *k, float x);

/* Ayar -> referans gerilimi (0..6 V'a kırpılır) */
float calib_kv_ref_volts(const calib_t *c, float kv);
float calib_ma_ref_volts(const calib_t *c, float kv, float ma);

/* Referans gerilimi -> DAC kodu */
uint16_t calib_volts_to_dac(float volts);

/* ADC kodu -> feedback gerilimi -> gerçek değer */
float calib_adc_to_volts(uint16_t code);
float calib_kv_from_fb(const calib_t *c, float fb_volts);
float calib_ma_from_fb(const calib_t *c, float fb_volts);

/* Kalibrasyon noktası ekler/günceller (x'e göre sıralı tutar) */
bool calib_curve_set_point(cal_curve_t *k, float x, float y);

#endif
