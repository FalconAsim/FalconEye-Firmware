/*
 * Dirençle denetlenen el butonu hattı (2 telli kademe, NC kontak yok)
 *
 * Butonun İÇİNDE, kontağın hemen yanında:
 *     siyah ──┬── RS (1k) ──┬── kontak ──┐
 *             │             └── RP (3k3) ┤
 *     beyaz ──┴──────────────────────────┘
 * Kartta: VDDA ── RPU (2k2) ── hat ── ADC,  diğer tel GND.
 * Pull-up ADC referansıyla aynı beslemeden: ölçüm oransaldır, besleme
 * değişse de eşikler kaymaz.
 *
 *  Hat durumu        Direnç   Oran (V/VDDA)
 *  kısa devre        0        0.00   -> hata
 *  basılı            RS       0.31
 *  bırakılmış        RS+RP    0.66
 *  kopuk             ∞        1.00   -> hata
 *
 * Çıktı, durum makinesinin mevcut NO/NC girişlerine eşlenir. Kısa devre ve
 * kopukluk NO/NC'yi tutarsız yapar: ışınlama aynı adımda durur, 20 ms
 * sonra E402 "Buton/pedal kablo arızası" verilir.
 */
#ifndef HS_LINE_H
#define HS_LINE_H
#include <stdbool.h>
#include <stdint.h>

#define HS_LINE_RPU_OHM   2200.0f
#define HS_LINE_RS_OHM    1000.0f
#define HS_LINE_RP_OHM    3300.0f

typedef enum {
    HS_LINE_SHORT = 0,
    HS_LINE_PRESSED,
    HS_LINE_RELEASED,
    HS_LINE_OPEN,
    HS_LINE_INVALID      /* pencereler arası: bozuk direnç, nem, temassızlık */
} hs_line_state_t;

/* 12 bit ADC kodundan (0..4095) hat durumu */
hs_line_state_t hs_line_classify(uint16_t adc);

/* Durum makinesi girişine eşleme: *no = "basılı" kontağı, *nc = "bırakılmış" kontağı */
void hs_line_map(hs_line_state_t s, bool *no, bool *nc);

/* Verilen direnç değerleriyle beklenen oran (test ve servis ekranı için) */
float hs_line_ratio(float r_line_ohm, float rpu_ohm);

const char *hs_line_text(hs_line_state_t s);
#endif
