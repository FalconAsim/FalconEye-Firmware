/*
 * Kalibrasyon: ayarlanan kV/mA -> jeneratör referans gerilimi -> DAC kodu,
 * ve ADC kodu -> gerilim -> gerçek kV/mA.
 *
 * Tablolar nokta tablosudur (formül değil). Başlangıç değerleri eski panelden
 * ölçülen EP-REF / IP-REF (29.10.2026); kesin değerler servis menüsünde
 * kVp metre (kV) ve mA metre (mA) ile nokta nokta girilir (Asım, 30.10.2026).
 * mA tablosu FOKUS BAŞINA ayrıdır ve her fokusta en çok 4 kV satırı vardır
 * (aynı mA için kV arttıkça referans düşer: uzay yükü).
 *
 * Donanım (Kart B şema rev 0.3, sayfa 6):
 *   DAC: STM32 DAC1 12 bit, VREF+ = 3,0 V (REF3030).
 *   EP-REF: sınır bölücüsü 10 k / 64,9 k (0,8665) × OPA2192 kazanç 2
 *           -> efektif 1,733; tam skala 5,20 V (donanım tavanı ≈ 103 kV).
 *   IP-REF: kazanç 3,32 × sınır bölücüsü; bölücü fokus rölesinin GERÇEK
 *           konumuna göre donanımla seçilir:
 *           küçük fokus 10 k / (1 M ‖ 10,2 k) -> 0,5025 -> efektif 1,668, tam skala 5,0 V
 *           büyük fokus 10 k / 1 M            -> 0,9901 -> efektif 3,287, tam skala 9,86 V
 *           (ilk çalıştırmada sınır dirençleri ölçüme göre seçilirse bu sabitler de güncellenir)
 *   Geri okuma: EP 120 k / 100 k (÷2,2), IP 240 k / 100 k (÷3,4), MON 12 k / 10 k (÷2,2).
 */
#ifndef CALIB_H
#define CALIB_H

#include <stdbool.h>
#include <stdint.h>

#define CAL_MAX_POINTS   16
#define CAL_MA_KV_ROWS    4    /* mA tablosu: fokus başına en çok 4 kV satırı */
#define CAL_FOCUS_LARGE   0    /* focus_t ile aynı sıra */
#define CAL_FOCUS_SMALL   1
#define CAL_FOCI          2

#define CAL_DAC_VREF      3.0f
#define CAL_DAC_MAX    4095u
#define CAL_ADC_VREF      3.0f
#define CAL_ADC_MAX    4095u

/* Kanal başına efektif DAC kazancı ve tam skala (V) */
#define CAL_EP_GAIN       (2.0f * 64.9f / 74.9f)
#define CAL_IP_GAIN_SMALL (3.32f * 0.5025f)
#define CAL_IP_GAIN_LARGE (3.32f * 0.9901f)
#define CAL_EP_MAX_V      (CAL_DAC_VREF * CAL_EP_GAIN)
#define CAL_IP_MAX_V_SMALL (CAL_DAC_VREF * CAL_IP_GAIN_SMALL)
#define CAL_IP_MAX_V_LARGE (CAL_DAC_VREF * CAL_IP_GAIN_LARGE)
#define CAL_REF_TABLE_MAX_V 10.0f   /* tablo geçerlilik sınırı (her iki kanal) */

/* ADC bölücüleri (alt / toplam) */
#define CAL_ADC_DIV_MON   (10.0f / 22.0f)
#define CAL_ADC_DIV_EP_RB (100.0f / 220.0f)
#define CAL_ADC_DIV_IP_RB (100.0f / 340.0f)
#define CAL_ADC_DIV       CAL_ADC_DIV_MON   /* geriye uyum */

/* Kalibrasyon adımında tek seferde izin verilen en büyük düzeltme */
#define CAL_STEP_MAX_FRAC 0.10f

typedef enum { CAL_CH_EP = 0, CAL_CH_IP } cal_channel_t;

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

/* Hangi tabloların ışınlamalı ölçümle doğrulandığı (servis bilgisi, W602) */
#define CAL_DONE_KV        0x01u
#define CAL_DONE_MA_LARGE  0x02u
#define CAL_DONE_MA_SMALL  0x04u

typedef struct {
    cal_curve_t kv_to_v;            /* ayar kV -> EP-REF (V) */
    cal_ma_table_t ma[CAL_FOCI];    /* fokus başına: ayar mA (+kV) -> IP-REF (V) */
    cal_curve_t kvfb_v_to_kv;       /* EP-MON (V) -> gerçek kV */
    cal_curve_t mafb_v_to_ma;       /* IP-MON (V) -> gerçek mA */
    uint8_t done;                   /* CAL_DONE_* bitleri */
} calib_t;

/* Nominal (kalibrasyonsuz) tabloları yükler */
void calib_default(calib_t *c);
void calib_default_ma(cal_ma_table_t *t, int focus);

/* Tablo geçerli mi: nokta sayısı, x kesin artan, y aralıkta */
bool calib_curve_valid(const cal_curve_t *k, float y_min, float y_max);
bool calib_valid(const calib_t *c);

/* Doğrusal ara değer; aralık dışında uçtaki iki noktadan doğrusal uzatma */
float calib_eval(const cal_curve_t *k, float x);

/* Ayar -> referans gerilimi (kanal / fokus tam skalasına kırpılır) */
float calib_kv_ref_volts(const calib_t *c, float kv);
float calib_ma_ref_volts(const calib_t *c, int focus, float kv, float ma);

/* Referans gerilimi -> DAC kodu (kanal ve fokus kazancıyla) */
uint16_t calib_dac_code(cal_channel_t ch, int focus, float volts);
float calib_max_volts(cal_channel_t ch, int focus);
uint16_t calib_volts_to_dac(float volts);   /* EP kanalı (geriye uyum) */

/* ADC kodu -> gerilim (bölücüyle) */
float calib_adc_to_volts_div(uint16_t code, float div);
float calib_adc_to_volts(uint16_t code);    /* MON bölücüsü */
float calib_kv_from_fb(const calib_t *c, float fb_volts);
float calib_ma_from_fb(const calib_t *c, float fb_volts);

/* Kalibrasyon noktası ekler/günceller (x'e göre sıralı tutar) */
bool calib_curve_set_point(cal_curve_t *k, float x, float y);

/* --- Işınlamalı nokta kalibrasyonu (servis menüsü) ---
 * Teknisyen ayar değeriyle çekim yapar, ölçüm cihazının okuduğunu girer.
 * Firmware o noktanın referans gerilimini tablonun yerel eğimiyle düzeltir:
 *   V_yeni = V_kullanılan + (ayar − ölçülen) × dV/dx
 * Tek adımda en çok %10 düzeltme; tablo monoton kalmalı; tam skala aşılamaz.
 * Ölçülen ayara yeterince yakınsa (tolerans) nokta yalnız doğrulanır.
 * Dönüş: CAL_OK / CAL_IN_TOL / hata. */
typedef enum {
    CAL_OK = 0,          /* nokta düzeltildi: aynı ayarla tekrar çekim yapın */
    CAL_IN_TOL,          /* ölçülen tolerans içinde: nokta doğrulandı */
    CAL_ERR_PARAM,       /* geçersiz ayar / ölçüm */
    CAL_ERR_STEP,        /* fark çok büyük (> %30): bağlantı / ölçüm hatası şüphesi */
    CAL_ERR_LIMIT,       /* düzeltme donanım tam skalasını aşar */
    CAL_ERR_MONOTONIC,   /* tablo monotonluğunu bozar */
    CAL_ERR_FULL         /* tabloda yer yok */
} cal_result_t;

cal_result_t calib_correct_kv(calib_t *c, float set_kv, float measured_kv, float tol_kv, float *v_new);
cal_result_t calib_correct_ma(calib_t *c, int focus, float set_kv, float set_ma, float measured_ma,
                              float tol_ma, float *v_new);
const char *calib_result_text(cal_result_t r);

#endif
