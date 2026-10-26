#include "hs_line.h"

/* Pencere sınırları (oran). Nominal: basılı 0.3125, bırakılmış 0.6615.
 * %5'lik dirençlerin en kötü durumu ve kablo direnci (birkaç ohm) dahil
 * pencerelerin içinde kalır (bkz. test_hs_line). */
#define R_SHORT_MAX     0.12f
#define R_PRESSED_MIN   0.20f
#define R_PRESSED_MAX   0.45f
#define R_RELEASED_MIN  0.53f
#define R_RELEASED_MAX  0.80f
#define R_OPEN_MIN      0.90f

hs_line_state_t hs_line_classify(uint16_t adc)
{
    if (adc > 4095u) adc = 4095u;
    const float r = (float)adc / 4095.0f;
    if (r <= R_SHORT_MAX) return HS_LINE_SHORT;
    if (r >= R_PRESSED_MIN && r <= R_PRESSED_MAX) return HS_LINE_PRESSED;
    if (r >= R_RELEASED_MIN && r <= R_RELEASED_MAX) return HS_LINE_RELEASED;
    if (r >= R_OPEN_MIN) return HS_LINE_OPEN;
    return HS_LINE_INVALID;
}

void hs_line_map(hs_line_state_t s, bool *no, bool *nc)
{
    switch (s) {
    case HS_LINE_PRESSED:  *no = true;  *nc = false; break;
    case HS_LINE_RELEASED: *no = false; *nc = true;  break;
    case HS_LINE_SHORT:    *no = true;  *nc = true;  break;  /* tutarsız -> etkin "bırakıldı" + hata */
    case HS_LINE_OPEN:
    case HS_LINE_INVALID:
    default:               *no = false; *nc = false; break;  /* tutarsız -> etkin "bırakıldı" + hata */
    }
}

float hs_line_ratio(float r_line_ohm, float rpu_ohm)
{
    return r_line_ohm / (r_line_ohm + rpu_ohm);
}

const char *hs_line_text(hs_line_state_t s)
{
    switch (s) {
    case HS_LINE_SHORT:    return "Kisa devre";
    case HS_LINE_PRESSED:  return "Basili";
    case HS_LINE_RELEASED: return "Birakilmis";
    case HS_LINE_OPEN:     return "Kopuk";
    default:               return "Gecersiz";
    }
}
