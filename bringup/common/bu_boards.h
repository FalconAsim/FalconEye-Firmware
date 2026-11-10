/*
 * Devreye alma – kart tabloları (pin, sınır, beklenen değer). Donanımdan bağımsız; PC'de test edilir.
 * Pin kaynağı: Kart B pins_b.py (şema rev 0.5i), Kart A karta.PINS_A (rev 0.9a), Kart M kartm_net (rev 0.2).
 */
#ifndef BU_BOARDS_H
#define BU_BOARDS_H
#include "bu_core.h"

typedef struct { char port; uint8_t pin; } bu_pin_t;

typedef struct {
    const char *name;
    bu_pin_t pin;
    const char *desc;
} bu_in_def_t;

/* ---------------- Kart B (STM32G474VET6) ---------------- */
#define BU_B_NOUT 22
extern const bu_out_def_t BU_B_OUT[BU_B_NOUT];
extern const bu_pin_t     BU_B_OUT_PIN[BU_B_NOUT];
#define BU_B_NIN 18
extern const bu_in_def_t  BU_B_IN[BU_B_NIN];
/* Devreye alma firmware'inin hiç sürmediği, açılışta düşük tutulan uç */
extern const bu_pin_t     BU_B_XRAY_REQ;          /* PE2 */

/* Raylar (ADC, VREF+ = REF3030 3,0 V) */
extern const bu_limit_t BU_B_L24, BU_B_L12, BU_B_L5, BU_B_LVREF, BU_B_LTEMP, BU_B_LFAN;
#define BU_B_R24_TOP 100000.0f
#define BU_B_R24_BOT  10000.0f
#define BU_B_R12_TOP  47000.0f
#define BU_B_R12_BOT  10000.0f
#define BU_B_R5_TOP   10000.0f
#define BU_B_R5_BOT   10000.0f

/* DAC → J2-5 / J2-6 → geri okuma (Kart B şema sayfa 7, R20–R38).
 * EP: DAC × 64,9/(10+64,9) × (1 + 10/10)            ; RB = J2-5 × 100/(120+100)
 * IP: DAC × 1M/(10k+1M) × (1 + 23,2/10)  (büyük fokus) ; RB = J2-6 × 100/(240+100)
 * J2 sökülü (yük yalnız R_AO 1 MΩ). Fokus "küçük" (Q1) devreye alma firmware'inde kapalı. */
float bu_b_ep_j2(float dac_v);
float bu_b_ip_j2(float dac_v);
float bu_b_ep_rb(float dac_v);
float bu_b_ip_rb(float dac_v);
bool  bu_b_rb_ok(float expect_v, float meas_v);   /* ±%3 + 20 mV */

/* ---------------- Kart A (STM32G0B1RET6) ---------------- */
extern const bu_limit_t BU_A_L24, BU_A_LVDDA, BU_A_LTEMP, BU_A_LNTC;
#define BU_A_R24_TOP 100000.0f
#define BU_A_R24_BOT  10000.0f
#define BU_A_NTC_RPU  10000.0f          /* R_NTC1 / R_NTC2 %0,1 */
#define BU_A_RT1_B    3380.0f           /* NCP18XH103F03RB B25/50 */
#define BU_A_PROBE_B  3435.0f           /* Semitec 103AT-2 */

typedef enum { BU_PNL_SATOZ40 = 0, BU_PNL_AT070_50, BU_PNL_LVDS, BU_PNL_NONE, BU_PNL_UNKNOWN } bu_panel_t;
/* PANEL_ID oranı (Vadc / VDDA): R_PID1 10k üstte, R_PID2 altta: 2k2 SATOZ · 10k AT070 · 33k LVDS · takılmaz */
bu_panel_t bu_panel_id(float ratio);
const char *bu_panel_text(bu_panel_t p);

/* ---------------- Kart M (STM32G0B1CBU6) ---------------- */
extern const bu_limit_t BU_M_LVDDA, BU_M_LTEMP;
#define BU_M_ADS1292R_ID  0x73u
#define BU_M_ABP_ADDR     0x28u          /* ABPDANN010KG2A3: I2C 0x28, 3,3 V */
#define BU_M_SDP_ADDR     0x25u          /* SDP810-500Pa */
/* Sensirion CRC-8 (0x31, başlangıç 0xFF) – SDP8xx veri sözcükleri */
uint8_t bu_sensirion_crc(const uint8_t *d, int n);

#endif
