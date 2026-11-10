/* Devreye alma – kart tabloları (açıklama: bu_boards.h) */
#include "bu_boards.h"
#include <math.h>

/* ---------------- Kart B ---------------- */
const bu_out_def_t BU_B_OUT[BU_B_NOUT] = {
    /* jeneratör (J2) ve invertör röleleri (J4): yalnız j2onay sonrası, en çok 2 s */
    { "SOFAJ",     "K1 -> J2-1 sofaj (1. kademe)",               BU_GRP_J2J4, 2000 },
    { "ONISITMA",  "K2 -> J2-2 on isitma",                       BU_GRP_J2J4, 2000 },
    { "INTLK",     "K4 -> J2-11 interlock",                      BU_GRP_J2J4, 2000 },
    { "ALMRST",    "K8 -> J2-13 alarm sifirlama (K8 takilmaz)",  BU_GRP_J2J4, 2000 },
    { "RL5",       "RL5 seri guvenlik rolesi (bobin acil stop K1 uzerinden)", BU_GRP_J2J4, 2000 },
    { "FOKUS",     "U48 -> J4-3/4 fokus roleleri (kucuk fokus)", BU_GRP_J2J4, 2000 },
    { "KAPSARJ",   "U48 -> J4-5 kondansator sarj rolesi",        BU_GRP_J2J4, 2000 },
    /* genel */
    { "SYNC1",     "K5 -> J19-7/8 senkron cikis 1",              BU_GRP_GENEL, 5000 },
    { "SYNC2",     "K6 -> J19-9/10 senkron cikis 2",             BU_GRP_GENEL, 5000 },
    { "SYNC3",     "K7 -> J19-11/12 senkron cikis 3",            BU_GRP_GENEL, 5000 },
    { "RL1",       "kV kademe rolesi RL1 (J6-1/2)",              BU_GRP_GENEL, 5000 },
    { "RL2",       "kV kademe rolesi RL2 (J6-3/4)",              BU_GRP_GENEL, 5000 },
    { "RL3",       "kV kademe rolesi RL3 (J6-5/6)",              BU_GRP_GENEL, 5000 },
    { "BUZZER",    "BZ1 kart buzzer'i",                          BU_GRP_GENEL, 1000 },
    { "LAMBAISIN", "RL8 -> J21-3/4 isinlama lambasi",            BU_GRP_GENEL, 5000 },
    { "LAMBAHAZIR","RL9 -> J21-5/6 hazir lambasi",               BU_GRP_GENEL, 5000 },
    { "ALARM",     "RL10 -> J21-7/8 harici alarm",               BU_GRP_GENEL, 5000 },
    { "FAN",       "J9 fan (dusuk taraf)",                       BU_GRP_GENEL, 0 },
    { "KOLLAMBA",  "U35 -> J7-1 kolimator LED 12 V",             BU_GRP_GENEL, 30000 },
    { "KOLLAZER",  "U34 -> J7-3 kolimator lazeri 5 V",           BU_GRP_GENEL, 30000 },
    { "LED",       "D_FW durum LED'i",                           BU_GRP_GENEL, 0 },
    { "RS485DE",   "U17 surucu etkin (J14)",                     BU_GRP_GENEL, 5000 },
};
const bu_pin_t BU_B_OUT_PIN[BU_B_NOUT] = {
    {'E', 0}, {'E', 1}, {'E', 3}, {'C', 11}, {'E', 7}, {'E', 8}, {'F', 10},
    {'E', 4}, {'E', 5}, {'E', 6}, {'E', 9}, {'E', 10}, {'E', 11}, {'E', 12},
    {'E', 13}, {'E', 14}, {'E', 15}, {'D', 10}, {'D', 11}, {'D', 12}, {'B', 3},
    {'D', 3},
};
const bu_pin_t BU_B_XRAY_REQ = {'E', 2};

const bu_in_def_t BU_B_IN[BU_B_NIN] = {
    { "ESTOP2",   {'A', 6},  "acil stop kanal 2 (opto; kapali devre = 0)" },
    { "PEDAL_A",  {'A', 7},  "pedal kanal A (basili = 0)" },
    { "PEDAL_B",  {'A', 10}, "pedal kanal B (basili = 0)" },
    { "TERMIK",   {'B', 2},  "tup termigi J8 (kapali = 0)" },
    { "SYNC_G1",  {'B', 6},  "J19 senkron giris 1" },
    { "SYNC_G2",  {'B', 9},  "J19 senkron giris 2" },
    { "SYNC_G3",  {'B', 10}, "J19 senkron giris 3" },
    { "KAPI",     {'B', 11}, "kapi J21-1/2 (gerilim var = 0)" },
    { "OVER_OK",  {'A', 8},  "J2-10 asiri yuk (JP2 kutbu)" },
    { "ALARM",    {'C', 9},  "J2-9 jenerator alarmi" },
    { "XRAY_FB",  {'D', 14}, "J2-8 hat geri okuma" },
    { "SOFAJ_FB", {'D', 15}, "J2-1 hat geri okuma" },
    { "ONIS_FB",  {'C', 8},  "J2-2 hat geri okuma" },
    { "FOKUS_FA", {'B', 4},  "J4-7 fokus A geri bildirim (cekili = 0)" },
    { "FOKUS_FB", {'B', 5},  "J4-8 fokus B geri bildirim (cekili = 0)" },
    { "BT_TRIP",  {'D', 7},  "yedek zamanlayici TRIP" },
    { "YEDEK",    {'C', 13}, "J2-14 yedek giris" },
    { "FABRIKA",  {'A', 9},  "SW2 FABRIKA (basili = 0)" },
};

const bu_limit_t BU_B_L24   = { "+24V (MON_24V)",  22.80f, 25.20f, "V" };
const bu_limit_t BU_B_L12   = { "+12V (MON_12V)",  11.40f, 12.60f, "V" };
const bu_limit_t BU_B_L5    = { "+5V (MON_5V)",     4.80f,  5.20f, "V" };
const bu_limit_t BU_B_LVREF = { "VREF+ (REF3030)",  2.97f,  3.03f, "V" };
const bu_limit_t BU_B_LTEMP = { "MCU sicakligi",    5.0f,  60.0f, "C" };
const bu_limit_t BU_B_LFAN  = { "Fan takometre",   20.0f, 400.0f, "Hz" };

float bu_b_ep_j2(float d) { return d * (64.9f / 74.9f) * 2.0f; }
float bu_b_ip_j2(float d) { return d * (1000.0f / 1010.0f) * (1.0f + 23.2f / 10.0f); }
float bu_b_ep_rb(float d) { return bu_b_ep_j2(d) * 100.0f / 220.0f; }
float bu_b_ip_rb(float d) { return bu_b_ip_j2(d) * 100.0f / 340.0f; }
bool  bu_b_rb_ok(float e, float m) { return fabsf(m - e) <= 0.03f * e + 0.020f; }

/* ---------------- Kart A ---------------- */
const bu_limit_t BU_A_L24   = { "+24V_A (MONA_24V)", 22.50f, 25.20f, "V" };   /* F3 + kablo düşümü */
const bu_limit_t BU_A_LVDDA = { "VDDA (+3V3A_A)",    3.20f,  3.40f, "V" };
const bu_limit_t BU_A_LTEMP = { "MCU sicakligi",     5.0f,  60.0f, "C" };
const bu_limit_t BU_A_LNTC  = { "NTC",               5.0f,  60.0f, "C" };

bu_panel_t bu_panel_id(float r)
{
    static const float nom[] = { 2.2f / 12.2f, 10.0f / 20.0f, 33.0f / 43.0f };
    if (r >= 0.95f) return BU_PNL_NONE;
    for (int i = 0; i < 3; i++) if (fabsf(r - nom[i]) <= 0.05f) return (bu_panel_t)i;
    return BU_PNL_UNKNOWN;
}

const char *bu_panel_text(bu_panel_t p)
{
    switch (p) {
    case BU_PNL_SATOZ40: return "SATOZ 40p RGB (R_PID2 2k2)";
    case BU_PNL_AT070_50: return "AT070 50p RGB (R_PID2 10k)";
    case BU_PNL_LVDS: return "LVDS (R_PID2 33k)";
    case BU_PNL_NONE: return "R_PID2 takili degil (servis secimi)";
    default: return "TANIMSIZ (R_PID2 kontrol)";
    }
}

/* ---------------- Kart M ---------------- */
const bu_limit_t BU_M_LVDDA = { "VDDA (+3V3_ISO)", 3.20f, 3.40f, "V" };
const bu_limit_t BU_M_LTEMP = { "MCU sicakligi",   5.0f, 60.0f, "C" };

uint8_t bu_sensirion_crc(const uint8_t *d, int n)
{
    uint8_t c = 0xFF;
    for (int i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (uint8_t)((c & 0x80u) ? (uint8_t)((c << 1) ^ 0x31u) : (uint8_t)(c << 1));
    }
    return c;
}
