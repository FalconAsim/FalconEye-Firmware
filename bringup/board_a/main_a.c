/*
 * FalconEye Kart A (STM32G0B1RET6) – devreye alma firmware'i.
 * Ekran: BT817Q + SATOZ 7" (J47). Durum ekranda da gösterilir; konsol isteğe bağlı:
 * USART2 (PA2 TX / PA3 RX) → J42, 3,3 V TTL, 115200 8N1 (USB-TTL çevirici).
 * CAN1 → Kart B (J30), CAN2 → Kart M (J46 → P16). Kart A ışınlama zincirine bağlı değildir.
 */
#include "hw.h"
#include "bu_core.h"
#include "bu_boards.h"
#include "eve.h"
#include "thermal_mon.h"
#include <string.h>

#define CON USART2
#define FW_TEXT "FalconEye Kart A devreye alma v0.1 (10.11.2026)"

static bool g_hse;
static eve_info_t g_eve;
static hw_can_t g_cb, g_cm;
static bu_peer_t g_b, g_m;
static uint32_t g_pings;
static float g_v24, g_vdda, g_tmcu, g_t_brd, g_t_ext, g_pid;
static struct { int16_t dp10, abp100, t10; uint8_t ads, fl; bool ok; uint32_t ms; } g_md;

static void setup(void)
{
    hw_gpio_out('B', 1, false);   /* DSP_PD */
    hw_gpio_out('A', 4, true);    /* DSP_CS */
    hw_gpio_out('D', 2, false);   /* CANA_STB normal */
    hw_gpio_out('B', 10, false);  /* CANM_STB normal (R_MSTB 10 k yukarı → bekleme; sürülür) */
    hw_gpio_out('C', 6, false);   /* LED_A */
    g_hse = hw_clock_init();
    hw_gpio_af('A', 2, 1, false); hw_gpio_af('A', 3, 1, false);
    hw_uart_init(CON, 115200u); hw_printf_init(CON);
    hw_gpio_an('A', 0); hw_gpio_an('A', 1); hw_gpio_an('B', 2); hw_gpio_an('C', 4);
    hw_adc_init(ADC1);
    hw_gpio_af('D', 0, 3, false); hw_gpio_af('D', 1, 3, false);
    hw_can_init(&g_cb, FDCAN1, 0, hw_sysclk);
    hw_gpio_af('B', 12, 3, false); hw_gpio_af('B', 13, 3, false);
    hw_can_init(&g_cm, FDCAN2, 1, hw_sysclk);
    eve_hw_init();
}

static void measure(void)
{
    g_vdda = hw_vref(ADC1);
    float vr = bu_is_nan(g_vdda) ? 3.3f : g_vdda;
    g_v24 = bu_adc_to_v(hw_adc_read_avg(ADC1, 17, 16), vr, 4095, BU_A_R24_TOP, BU_A_R24_BOT);
    g_t_brd = bu_ntc_c((float)hw_adc_read_avg(ADC1, 1, 16) / 4095.0f, BU_A_NTC_RPU, 10000.0f, BU_A_RT1_B);
    g_t_ext = bu_ntc_c((float)hw_adc_read_avg(ADC1, 0, 16) / 4095.0f, BU_A_NTC_RPU, 10000.0f, BU_A_PROBE_B);
    g_pid = (float)hw_adc_read_avg(ADC1, 10, 16) / 4095.0f;
    g_tmcu = hw_mcu_temp(ADC1, vr);
}

static void line(const bu_limit_t *l, float v, int *f)
{
    char b[96]; bu_verdict_t r = bu_judge(l, v);
    if (r == BU_FAIL && f) (*f)++;
    bu_fmt_result(b, sizeof b, l, v, r); outln(b);
}

static void verdict(bool ok, const char *name, const char *d, int *f)
{
    out(ok ? "GECTI  " : "KALDI  "); out(name); for (size_t k = strlen(name); k < 24; k++) out(" "); outln(d);
    if (!ok && f) (*f)++;
}

static void cmd_rapor(void)
{
    int f = 0; char b[32];
    measure();
    outln("\n===== RAPOR: " FW_TEXT " =====");
    verdict(g_hse, "HSE 8 MHz (Y4)", g_hse ? "calisiyor" : "baslamadi - HSI16", &f);
    line(&BU_A_L24, g_v24, &f);
    line(&BU_A_LVDDA, g_vdda, &f);
    line(&BU_A_LTEMP, g_tmcu, &f);
    bu_panel_t p = bu_panel_id(g_pid);
    verdict(p == BU_PNL_SATOZ40, "Panel kimligi (PANEL_ID)", bu_panel_text(p), &f);
    bu_limit_t lb = BU_A_LNTC; lb.name = "NTC kart (RT1)"; line(&lb, g_t_brd, &f);
    if (bu_is_nan(g_t_ext)) outln("BILGI  NTC prob (J49)          takili degil / acik devre");
    else { bu_limit_t le = BU_A_LNTC; le.name = "NTC prob (J49)"; line(&le, g_t_ext, &f); }
    bu_fmt_hex(b, sizeof b, g_eve.reg_id, 2);
    verdict(g_eve.ok, "BT817 REG_ID (0x7C)", g_eve.ok ? (g_eve.ext_clk ? "0x7C, harici kristal" : "0x7C, IC OSILATOR (kristal?)") : b, &f);
    bu_fmt_hex(b, sizeof b, g_eve.chip_id, 8);
    verdict((g_eve.chip_id & 0xFFFFu) == 0x1708u, "BT817 yonga kimligi", b, &f);
    uint32_t now = hw_ms();
    verdict(bu_peer_alive(&g_b, now, 3000u), "CAN1: Kart B", bu_peer_alive(&g_b, now, 3000u) ? "0x7E2 aliniyor" : "yok (J30-J23 kablosu)", &f);
    out("       Kart B yanki istegi "); out_u(g_pings); outln(" (her biri yanitlandi)");
    bool m = bu_peer_alive(&g_m, now, 3000u);
    verdict(m, "CAN2: Kart M", m ? "0x7E3 aliniyor" : "yok (P16 / modul kablosu)", &f);
    if (m) {
        bu_fmt_hex(b, sizeof b, g_md.ads, 2);
        verdict(g_md.ads == BU_M_ADS1292R_ID, "Kart M ADS1292R kimligi", b, &f);
        verdict(g_md.fl & 1u, "Kart M ABP (0x28)", (g_md.fl & 1u) ? "yanit" : "yok", &f);
        verdict(g_md.fl & 2u, "Kart M SDP810 (0x25)", (g_md.fl & 2u) ? "yanit, CRC dogru" : "yok", &f);
        out("       dp "); out_f((float)g_md.dp10 / 10.0f, 1); out(" Pa, ABP "); out_f((float)g_md.abp100 / 100.0f, 2); out(" kPa, SDP "); out_f((float)g_md.t10 / 10.0f, 1); outln(" C");
    }
    outln("BILGI  Ekran testi             renk cubuklari / gri basamak / beyaz cerceve gozle kontrol");
    outln(f ? "===== SONUC: KALDI =====" : "===== SONUC: GECTI =====");
    out("("); out_u((uint32_t)f); outln(" kalem kaldi)\n");
}

/* Ekran: test deseni + canlı durum satırları */
static void screen(void)
{
    if (!g_eve.ok) return;
    char b[64], f[16];
    uint32_t now = hw_ms();
    eve_test_pattern("FalconEye Kart A - devreye alma");
    int y = 320;
    strcpy(b, "24V "); bu_fmt_f(f, sizeof f, g_v24, 2); strcat(b, f); strcat(b, " V   VDDA "); bu_fmt_f(f, sizeof f, g_vdda, 2); strcat(b, f); strcat(b, " V");
    eve_text(16, y, 28, b, bu_judge(&BU_A_L24, g_v24) == BU_PASS ? 0x80FF80 : 0xFF6060); y += 30;
    strcpy(b, "NTC kart "); bu_fmt_f(f, sizeof f, g_t_brd, 1); strcat(b, f); strcat(b, " C   prob "); bu_fmt_f(f, sizeof f, g_t_ext, 1); strcat(b, f); strcat(b, " C");
    eve_text(16, y, 28, b, 0xFFFFFF); y += 30;
    strcpy(b, "CAN B "); strcat(b, bu_peer_alive(&g_b, now, 3000u) ? "VAR" : "YOK"); strcat(b, "   CAN M "); strcat(b, bu_peer_alive(&g_m, now, 3000u) ? "VAR" : "YOK");
    eve_text(16, y, 28, b, 0xFFFFFF); y += 30;
    uint32_t xy = eve_rd32(EVE_REG_TOUCH_RAW_XY);
    strcpy(b, "Dokunma ham X "); bu_fmt_hex(f, sizeof f, xy >> 16, 4); strcat(b, f); strcat(b, " Y "); bu_fmt_hex(f, sizeof f, xy & 0xFFFFu, 4); strcat(b, f);
    eve_text(16, y, 28, b, (xy == 0xFFFFFFFFu) ? 0x909090 : 0xFFFF00);
    eve_dl_end();
}

static void help(void)
{
    outln("Komutlar: rapor | ekran (yeniden baslat + desen) | ses | dokunma (10 s ham XY) | sifirla");
}

static void handle(int argc, char **argv)
{
    (void)argc;
    if (bu_streq(argv[0], "?") || bu_streq(argv[0], "yardim")) help();
    else if (bu_streq(argv[0], "rapor")) cmd_rapor();
    else if (bu_streq(argv[0], "ekran")) { if (eve_start(&g_eve)) eve_timing(&EVE_SATOZ_800x480, 96); screen(); outln(g_eve.ok ? "ekran tamam" : "BT817 yanit vermiyor"); }
    else if (bu_streq(argv[0], "ses")) { if (g_eve.ok) eve_beep(); }
    else if (bu_streq(argv[0], "dokunma")) {
        uint32_t t = hw_ms();
        while ((uint32_t)(hw_ms() - t) < 10000u) {
            uint32_t xy = eve_rd32(EVE_REG_TOUCH_RAW_XY);
            out("X "); out_u(xy >> 16); out("  Y "); out_u(xy & 0xFFFFu); out("  RZ "); out_u(eve_rd32(EVE_REG_TOUCH_RZ) & 0xFFFFu); out("\n");
            hw_delay_ms(250);
        }
    }
    else if (bu_streq(argv[0], "sifirla")) NVIC_SystemReset();
    else outln("? yardim");
}

static void can_poll(uint32_t now)
{
    uint16_t id; uint8_t d[8]; bu_hb_t h;
    while (hw_can_recv(&g_cb, &id, d)) {
        if (id == BU_CAN_HB_B && bu_hb_unpack(d, &h)) bu_peer_seen(&g_b, &h, now);
        else if (id == BU_CAN_PING) { hw_can_send(&g_cb, BU_CAN_PONG, d); g_pings++; }
    }
    while (hw_can_recv(&g_cm, &id, d)) {
        if (id == BU_CAN_HB_M && bu_hb_unpack(d, &h)) bu_peer_seen(&g_m, &h, now);
        else if (id == BU_CAN_M_DATA) {
            g_md.dp10 = (int16_t)(d[0] | (d[1] << 8)); g_md.abp100 = (int16_t)(d[2] | (d[3] << 8)); g_md.t10 = (int16_t)(d[4] | (d[5] << 8));
            g_md.ads = d[6]; g_md.fl = d[7]; g_md.ok = true; g_md.ms = now;
        }
    }
}

static int16_t deci(float t) { return bu_is_nan(t) ? (int16_t)FE_TBOX_NONE : (int16_t)(t * 10.0f + (t >= 0 ? 0.5f : -0.5f)); }

static void can_tx(uint32_t now)
{
    static uint32_t t; static uint8_t seq;
    if ((uint32_t)(now - t) < 1000u) return;
    t = now; seq++;
    measure();
    bu_hb_t h = { 'A', seq, (uint16_t)(now / 1000u), 0, (int8_t)g_tmcu, (uint16_t)(g_v24 * 100.0f) };
    if (g_hse) h.status |= BU_ST_HSE;
    if (bu_judge(&BU_A_L24, g_v24) == BU_PASS && bu_judge(&BU_A_LVDDA, g_vdda) == BU_PASS) h.status |= BU_ST_RAILS;
    if (g_eve.ok) h.status |= BU_ST_PERIPH;
    if (bu_peer_alive(&g_b, now, 3000u)) h.status |= BU_ST_PEER1;
    if (bu_peer_alive(&g_m, now, 3000u)) h.status |= BU_ST_PEER2;
    if (!bu_is_nan(g_t_brd)) h.status |= BU_ST_SENS;
    uint8_t d[8]; bu_hb_pack(&h, d);
    hw_can_send(&g_cb, BU_CAN_HB_A, d);
    hw_can_send(&g_cm, BU_CAN_HB_A, d);
    /* Üretim biçiminde kutu sıcaklığı (CAN 0x330) – Kart B üretim firmware'i de okur */
    tbox_msg_t tb = { seq, deci(g_t_brd), deci(g_t_ext), 0 };
    tbox_pack(&tb, d);
    hw_can_send(&g_cb, FE_CAN_ID_TBOX, d);
}

int main(void)
{
    setup();
    outln("\n\n" FW_TEXT);
    if (eve_start(&g_eve)) eve_timing(&EVE_SATOZ_800x480, 96);
    hw_delay_ms(1500);
    cmd_rapor();
    if (g_eve.ok) eve_beep();
    bu_console_t con; memset(&con, 0, sizeof con);
    int argc; char *argv[BU_ARGV_MAX];
    out("> ");
    uint32_t ts = 0;
    for (;;) {
        uint32_t now = hw_ms();
        int c = hw_uart_getc(CON);
        if (c >= 0) {
            hw_uart_putc(CON, (char)c);
            if (bu_console_feed(&con, (char)c, &argc, argv)) { out("\n"); handle(argc, argv); out("> "); }
        }
        can_poll(now);
        can_tx(now);
        if ((uint32_t)(now - ts) >= 500u) { ts = now; screen(); }
        hw_gpio_write('C', 6, (now / 500u) & 1u);
    }
}
