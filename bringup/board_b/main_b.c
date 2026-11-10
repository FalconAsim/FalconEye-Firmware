/*
 * FalconEye Kart B (STM32G474VET6) – devreye alma firmware'i.
 * Konsol: USART2 (PD5/PD6) → MAX3232 → J20 → arka panel DB9 (P2) uç 2 RXD / 3 TXD / 5 GND, 115200 8N1.
 * Güvenlik kuralları: bringup/common/bu_core.h başı. XRAY_REQ (PE2) hiç sürülmez.
 */
#include "hw.h"
#include "bu_core.h"
#include "bu_boards.h"
#include "hs_line.h"
#include <string.h>

#define CON USART2
#define FW_TEXT "FalconEye Kart B devreye alma v0.1 (10.11.2026)"

static bu_guard_t g_guard;
static hw_can_t g_can;
static bool g_hse, g_w5500_ok;
static bu_peer_t g_peer_a, g_peer_m;
static uint32_t g_pong_ok, g_pong_bad;
static uint8_t g_ping[8];
static volatile uint32_t g_tach_cnt;
static float g_tach_hz;
static float g_v24;

/* ---------------- donanım kurulumu ---------------- */
static void wdi_hook(void)
{
    static uint16_t n;
    if (++n >= 100u) { n = 0; hw_port('D')->ODR ^= 1u << 13; }    /* U11 WDI, 100 ms */
}

void EXTI15_10_IRQHandler(void)
{
    if (EXTI->PR1 & (1u << 10)) { EXTI->PR1 = 1u << 10; g_tach_cnt++; }
}

static void safe_outputs(void)
{
    hw_gpio_out('E', 2, false);                       /* XRAY_REQ – sürekli düşük */
    for (int i = 0; i < BU_B_NOUT; i++) hw_gpio_out(BU_B_OUT_PIN[i].port, BU_B_OUT_PIN[i].pin, false);
    hw_gpio_out('D', 13, false);                      /* WDI */
    hw_gpio_out('D', 4, false);                       /* CAN_STB: normal */
    hw_gpio_out('C', 7, false);                       /* W5500_RST (aşağıda bırakılır) */
    hw_gpio_out('B', 12, true);                       /* W5500_CS */
    hw_gpio_out('F', 2, true);                        /* FLASH_CS */
    hw_gpio_out('F', 9, true);                        /* EE_WP: korumalı */
}

static void setup(void)
{
    safe_outputs();
    hw_tick_hook = wdi_hook;
    g_hse = hw_clock_init();
    hw_gpio_af('D', 5, 7, false); hw_gpio_af('D', 6, 7, false);
    hw_uart_init(CON, 115200u); hw_printf_init(CON);
    for (int i = 0; i < BU_B_NIN; i++) hw_gpio_in(BU_B_IN[i].pin.port, BU_B_IN[i].pin.pin, 0);
    hw_gpio_in('C', 10, 0);                           /* FAN_TACH (R82 10k yukarı) */
    static const struct { char p; int n; } an[] = { {'A', 0}, {'A', 1}, {'A', 2}, {'A', 3}, {'A', 4}, {'A', 5},
                                                    {'C', 0}, {'C', 1}, {'C', 2}, {'C', 3}, {'B', 0}, {'B', 1} };
    for (size_t i = 0; i < sizeof an / sizeof an[0]; i++) hw_gpio_an(an[i].p, an[i].n);
    hw_adc_init(ADC1); hw_adc_init(ADC3);
    /* takometre: EXTI10 düşen kenar */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    SYSCFG->EXTICR[2] = (SYSCFG->EXTICR[2] & ~(0xFu << 8)) | (2u << 8);
    EXTI->FTSR1 |= 1u << 10; EXTI->IMR1 |= 1u << 10;
    NVIC_EnableIRQ(EXTI15_10_IRQn);
    /* SPI2: W5500 + W25Q32 */
    hw_gpio_af('B', 13, 5, false); hw_gpio_af('B', 14, 5, false); hw_gpio_af('B', 15, 5, false);
    hw_spi_init(SPI2, 1);
    hw_delay_ms(2); hw_gpio_write('C', 7, true); hw_delay_ms(60);   /* W5500 reset bırak, PLL kilidi */
    /* I2C1: FRAM U16 0x50, EEPROM U47 0x51, MAC U46 0x53 */
    hw_gpio_af('A', 15, 4, true); hw_gpio_af('B', 7, 4, true);
    hw_i2c_init(I2C1);
    /* FDCAN1 → Kart A */
    hw_gpio_af('D', 0, 9, false); hw_gpio_af('D', 1, 9, false);
    hw_can_init(&g_can, FDCAN1, 0, hw_sysclk);
    bu_guard_init(&g_guard, BU_B_OUT, BU_B_NOUT);
}

static void apply_outputs(void)
{
    for (int i = 0; i < g_guard.n; i++) hw_gpio_write(BU_B_OUT_PIN[i].port, BU_B_OUT_PIN[i].pin, g_guard.on[i]);
    hw_gpio_write('E', 2, false);
}

/* ---------------- ölçümler ---------------- */
static float vref(void) { return hw_vref(ADC1); }
static float rail(ADC_TypeDef *a, int ch, float rt, float rb, float vr) { return bu_adc_to_v(hw_adc_read_avg(a, ch, 16), vr, 4095, rt, rb); }

static void line(const bu_limit_t *l, float v, int *fails)
{
    char b[96];
    bu_verdict_t r = bu_judge(l, v);
    if (r == BU_FAIL && fails) (*fails)++;
    bu_fmt_result(b, sizeof b, l, v, r);
    outln(b);
}

static void info(const char *name, const char *val)
{
    out("BILGI  "); out(name); for (size_t k = strlen(name); k < 24; k++) out(" "); outln(val);
}

static void verdict(bool ok, const char *name, const char *detail, int *fails)
{
    out(ok ? "GECTI  " : "KALDI  "); out(name); for (size_t k = strlen(name); k < 24; k++) out(" "); outln(detail);
    if (!ok && fails) (*fails)++;
}

static uint8_t w5500_read(uint16_t addr)
{
    hw_gpio_write('B', 12, false);
    hw_spi_xfer(SPI2, (uint8_t)(addr >> 8)); hw_spi_xfer(SPI2, (uint8_t)addr); hw_spi_xfer(SPI2, 0x00);
    uint8_t v = hw_spi_xfer(SPI2, 0);
    hw_gpio_write('B', 12, true);
    return v;
}

static uint32_t flash_jedec(void)
{
    hw_gpio_write('F', 2, false);
    hw_spi_xfer(SPI2, 0x9F);
    uint32_t v = (uint32_t)hw_spi_xfer(SPI2, 0) << 16; v |= (uint32_t)hw_spi_xfer(SPI2, 0) << 8; v |= hw_spi_xfer(SPI2, 0);
    hw_gpio_write('F', 2, true);
    return v;
}

static void cmd_raylar(int *fails)
{
    float vr = vref();
    line(&BU_B_LVREF, vr, fails);
    g_v24 = rail(ADC1, 9, BU_B_R24_TOP, BU_B_R24_BOT, vr);
    line(&BU_B_L24, g_v24, fails);
    line(&BU_B_L12, rail(ADC3, 12, BU_B_R12_TOP, BU_B_R12_BOT, vr), fails);
    line(&BU_B_L5, rail(ADC3, 1, BU_B_R5_TOP, BU_B_R5_BOT, vr), fails);
    line(&BU_B_LTEMP, hw_mcu_temp(ADC1, vr), fails);
    verdict(g_hse, "HSE 8 MHz (Y1)", g_hse ? "calisiyor" : "baslamadi - HSI16 ile devam", fails);
}

static void cmd_kimlik(int *fails)
{
    char b[40];
    uint8_t ver = w5500_read(0x0039);
    g_w5500_ok = ver == 0x04;
    b[0] = 0; out(""); bu_fmt_hex(b, sizeof b, ver, 2);
    verdict(g_w5500_ok, "W5500 VERSIONR", g_w5500_ok ? "0x04" : b, fails);
    info("Ethernet baglanti", (w5500_read(0x002E) & 1u) ? "VAR (P1 kablosu takili)" : "yok (kablo takili degilse normal)");
    uint32_t j = flash_jedec();
    bu_fmt_hex(b, sizeof b, j, 6);
    verdict(j == 0xEF4016u, "W25Q32 JEDEC (U51)", b, fails);
    verdict(hw_i2c_probe(I2C1, 0x50), "FRAM U16 (0x50)", "I2C yanit", fails);
    verdict(hw_i2c_probe(I2C1, 0x51), "EEPROM U47 (0x51)", "I2C yanit", fails);
    uint8_t reg = 0xFA, mac[6] = { 0 };
    bool ok = hw_i2c_write(I2C1, 0x53, &reg, 1, false) && hw_i2c_read(I2C1, 0x53, mac, 6);
    char m[24]; int p = 0;
    for (int k = 0; k < 6; k++) { bu_fmt_hex(m + p, sizeof m - (size_t)p, mac[k], 2); p += 2; if (k < 5) m[p++] = ':'; }
    m[p] = 0;
    verdict(ok && (mac[0] | mac[1] | mac[2]), "MAC U46 (0x53)", ok ? m : "yanit yok", fails);
}

static void measure_tach(void)
{
    g_tach_cnt = 0; hw_delay_ms(1000);
    g_tach_hz = (float)g_tach_cnt;
}

static void cmd_fan(int *fails)
{
    int f = bu_guard_find(&g_guard, "FAN");
    bu_guard_set(&g_guard, f, true, hw_ms()); apply_outputs();
    out("fan acik, 3 s bekleniyor...\n"); hw_delay_ms(3000);
    measure_tach();
    line(&BU_B_LFAN, g_tach_hz, fails);
    out("(PC fani tur basina 2 darbe: "); out_u((uint32_t)(g_tach_hz * 30.0f)); outln(" d/dk)");
}

static void cmd_can(int *fails)
{
    uint32_t now = hw_ms();
    bool a = bu_peer_alive(&g_peer_a, now, 3000u);
    verdict(a, "CAN: Kart A kalp atisi", a ? "0x7E1 aliniyor" : "yok (J23-J30 kablosu / Kart A firmware)", fails);
    if (a) {
        out("       Kart A 24 V = "); out_f((float)g_peer_a.last.aux / 100.0f, 2); out(" V, durum 0x"); out_hex(g_peer_a.last.status, 2);
        out(", sicaklik "); out_u((uint32_t)(g_peer_a.last.temp_c < 0 ? 0 : g_peer_a.last.temp_c)); outln(" C");
        bool m = (g_peer_a.last.status & BU_ST_PEER2) != 0;
        verdict(m, "CAN: Kart M (A uzerinden)", m ? "Kart A, M'yi goruyor" : "gorulmuyor (P16 kablosu / Kart M)", fails);
    }
    bool pp = g_pong_ok > 0 && g_pong_bad == 0;
    out(pp ? "GECTI  " : (g_pong_ok ? "KALDI  " : "BILGI  "));
    out("CAN yanki (0x7E0/0x7E8)  dogru "); out_u(g_pong_ok); out(", hatali "); out_u(g_pong_bad); out("\n");
    if (g_pong_bad && fails) (*fails)++;
    out("       PSR 0x"); out_hex(hw_can_psr(&g_can), 8); out("  ECR 0x"); out_hex(hw_can_ecr(&g_can), 8); out("  TX "); out_u(g_can.tx); out("\n");
}

static void cmd_girisler(void)
{
    for (int i = 0; i < BU_B_NIN; i++) {
        out(BU_B_IN[i].name); for (size_t k = strlen(BU_B_IN[i].name); k < 10; k++) out(" ");
        out(hw_gpio_read(BU_B_IN[i].pin.port, BU_B_IN[i].pin.pin) ? "1  " : "0  ");
        outln(BU_B_IN[i].desc);
    }
    float vr = vref();
    static const char *hn[2] = { "EL1 (HS1)", "EL2 (HS2)" };
    for (int k = 0; k < 2; k++) {
        uint16_t c = hw_adc_read_avg(ADC1, 6 + k, 8);
        out(hn[k]); out(" "); out_f((float)c * vr / 4095.0f, 2); out(" V  "); outln(hs_line_text(hs_line_classify(c)));
    }
    out("KOL_LAMBA akim (LAMP_IS) ADC "); out_f((float)hw_adc_read_avg(ADC1, 8, 8) * vr / 4095.0f, 3); outln(" V");
}

static void cmd_cikis_liste(void)
{
    for (int i = 0; i < g_guard.n; i++) {
        out(g_guard.on[i] ? "[ACIK] " : "[    ] "); out(BU_B_OUT[i].name); for (size_t k = strlen(BU_B_OUT[i].name); k < 11; k++) out(" ");
        out(BU_B_OUT[i].grp == BU_GRP_J2J4 ? "(j2onay) " : "         "); outln(BU_B_OUT[i].desc);
    }
}

static void say_err(bu_err_t e)
{
    if (e == BU_ERR_ACK) outln("RED: once J2 ve J4 fislerini sokun, sonra 'j2onay EVET'.");
    else if (e == BU_ERR_FORBIDDEN) outln("RED: X-RAY ON devreye alma firmware'inde yok (kabul testinde uretim firmware'iyle).");
    else if (e == BU_ERR_NAME) outln("RED: bilinmeyen cikis ('cikis liste').");
}

static void cmd_sira(void)
{
    outln("Cikislar sirayla 1 s acilir. Role tik sesi / LED / multimetre ile izleyin. Durdurmak icin bir tus.");
    bool ack = bu_guard_ack_valid(&g_guard, hw_ms());
    if (!ack) outln("(J2/J4 grubu atlanir: j2onay yok)");
    for (int i = 0; i < g_guard.n; i++) {
        if (BU_B_OUT[i].grp == BU_GRP_J2J4 && !ack) continue;
        if (bu_streq(BU_B_OUT[i].name, "FAN") || bu_streq(BU_B_OUT[i].name, "LED")) continue;
        out("  "); out(BU_B_OUT[i].name); out(" ... ");
        bu_guard_set(&g_guard, i, true, hw_ms()); apply_outputs();
        hw_delay_ms(BU_B_OUT[i].grp == BU_GRP_J2J4 ? 500u : 1000u);
        bu_guard_set(&g_guard, i, false, hw_ms()); apply_outputs();
        outln("kapandi");
        hw_delay_ms(300);
        if (hw_uart_getc(CON) >= 0) { outln("durduruldu"); break; }
    }
}

static void dac_set(int ch, float v)
{
    uint32_t code = (uint32_t)(v / 3.0f * 4095.0f + 0.5f);
    if (code > 4095u) code = 4095u;
    if (ch == 1) DAC1->DHR12R1 = code; else DAC1->DHR12R2 = code;
}

static void cmd_dac(long mv_ep, long mv_ip, int *fails)
{
    if (!bu_guard_ack_valid(&g_guard, hw_ms())) { say_err(BU_ERR_ACK); return; }
    if (mv_ep < 0 || mv_ep > 1500 || mv_ip < 0 || mv_ip > 1200) { outln("DAC 0..1500 mV (EP), 0..1200 mV (IP)"); return; }
    RCC->AHB2ENR |= RCC_AHB2ENR_DAC1EN; (void)RCC->AHB2ENR;
    DAC1->MCR = 0u;
    DAC1->CR |= DAC_CR_EN1 | DAC_CR_EN2;
    float de = (float)mv_ep / 1000.0f, di = (float)mv_ip / 1000.0f;
    dac_set(1, de); dac_set(2, di);
    hw_delay_ms(50);
    float vr = vref();
    float rbe = (float)hw_adc_read_avg(ADC1, 3, 16) * vr / 4095.0f, rbi = (float)hw_adc_read_avg(ADC1, 4, 16) * vr / 4095.0f;
    dac_set(1, 0.0f); dac_set(2, 0.0f);
    DAC1->CR &= ~(DAC_CR_EN1 | DAC_CR_EN2);          /* test bitti: DAC kapalı */
    bu_limit_t le = { "EP-REF geri okuma", bu_b_ep_rb(de) * 0.97f - 0.02f, bu_b_ep_rb(de) * 1.03f + 0.02f, "V" };
    bu_limit_t li = { "IP-REF geri okuma", bu_b_ip_rb(di) * 0.97f - 0.02f, bu_b_ip_rb(di) * 1.03f + 0.02f, "V" };
    line(&le, rbe, fails); line(&li, rbi, fails);
    out("       beklenen J2-5 "); out_f(bu_b_ep_j2(de), 2); out(" V, J2-6 "); out_f(bu_b_ip_j2(di), 2); outln(" V (TP4 / TP5 ile olcun)");
}

static void cmd_rapor(void)
{
    int f = 0;
    outln("\n===== RAPOR: " FW_TEXT " =====");
    cmd_raylar(&f);
    cmd_kimlik(&f);
    cmd_fan(&f);
    cmd_can(&f);
    float vr = vref();
    for (int k = 0; k < 2; k++) {
        hs_line_state_t s = hs_line_classify(hw_adc_read_avg(ADC1, 6 + k, 8));
        verdict(s == HS_LINE_RELEASED, k ? "El butonu 2. kademe" : "El butonu 1. kademe", hs_line_text(s), &f);
    }
    (void)vr;
    verdict(!hw_gpio_read('A', 6), "Acil stop kanal 2", hw_gpio_read('A', 6) ? "acik (P7 takili mi, buton cekili mi?)" : "kapali", &f);
    info("Kapi", hw_gpio_read('B', 11) ? "acik / gerilim yok" : "kapali");
    info("DAC testi", bu_guard_ack_valid(&g_guard, hw_ms()) ? "dac 300 1200 ile" : "j2onay sonrasi 'dac 300 1200'");
    outln(f ? "===== SONUC: KALDI =====" : "===== SONUC: GECTI =====");
    out("("); out_u((uint32_t)f); outln(" kalem kaldi)\n");
}

static void help(void)
{
    outln("Komutlar:");
    outln("  rapor                otomatik testler (raylar, kimlikler, fan, CAN, el butonu, acil stop)");
    outln("  raylar | kimlik | can | fan | girisler");
    outln("  izle                 girisleri 0,5 s'de bir yaz (bir tus: dur)");
    outln("  cikis liste          cikislar");
    outln("  cikis <ad> 1|0       tek cikis (sureli; J2/J4 grubu j2onay ister)");
    outln("  sira                 cikislari sirayla 1 s ac");
    outln("  j2onay EVET          J2 ve J4 fisleri SOKULU onayi (10 dk)");
    outln("  dac <mV_ep> <mV_ip>  DAC -> J2-5/J2-6 geri okuma (j2onay; ornek: dac 300 1200)");
    outln("  sifirla");
}

static void handle(int argc, char **argv)
{
    long a, b;
    if (bu_streq(argv[0], "?") || bu_streq(argv[0], "yardim")) help();
    else if (bu_streq(argv[0], "rapor")) cmd_rapor();
    else if (bu_streq(argv[0], "raylar")) cmd_raylar(0);
    else if (bu_streq(argv[0], "kimlik")) cmd_kimlik(0);
    else if (bu_streq(argv[0], "can")) cmd_can(0);
    else if (bu_streq(argv[0], "fan")) cmd_fan(0);
    else if (bu_streq(argv[0], "girisler")) cmd_girisler();
    else if (bu_streq(argv[0], "izle")) {
        while (hw_uart_getc(CON) < 0) { cmd_girisler(); outln("--"); hw_delay_ms(500); }
    }
    else if (bu_streq(argv[0], "cikis") && argc >= 2 && bu_streq(argv[1], "liste")) cmd_cikis_liste();
    else if (bu_streq(argv[0], "cikis") && argc == 3 && bu_parse_int(argv[2], &a)) {
        bu_err_t e = bu_guard_set_name(&g_guard, argv[1], a != 0, hw_ms());
        if (e) say_err(e); else { apply_outputs(); outln(a ? "acik (sure dolunca kendiliginden kapanir)" : "kapali"); }
    }
    else if (bu_streq(argv[0], "sira")) cmd_sira();
    else if (bu_streq(argv[0], "j2onay")) {
        if (argc == 2 && bu_streq(argv[1], "EVET")) { bu_guard_ack(&g_guard, hw_ms()); outln("J2/J4 grubu 10 dk acik. Cikislar en cok 2 s. XRAY_REQ yine yok."); }
        else outln("J2 (invertor CON2) ve J4 (fokus roleleri) fislerini sokun; sonra: j2onay EVET");
    }
    else if (bu_streq(argv[0], "dac") && argc == 3 && bu_parse_int(argv[1], &a) && bu_parse_int(argv[2], &b)) cmd_dac(a, b, 0);
    else if (bu_streq(argv[0], "sifirla")) NVIC_SystemReset();
    else outln("? yardim");
}

/* ---------------- CAN ---------------- */
static void can_poll(uint32_t now)
{
    uint16_t id; uint8_t d[8]; bu_hb_t h;
    while (hw_can_recv(&g_can, &id, d)) {
        if (id == BU_CAN_HB_A && bu_hb_unpack(d, &h)) bu_peer_seen(&g_peer_a, &h, now);
        else if (id == BU_CAN_HB_M && bu_hb_unpack(d, &h)) bu_peer_seen(&g_peer_m, &h, now);
        else if (id == BU_CAN_PONG) { if (memcmp(d, g_ping, 8) == 0) g_pong_ok++; else g_pong_bad++; }
    }
}

static void can_tx(uint32_t now)
{
    static uint32_t t; static uint8_t seq;
    if ((uint32_t)(now - t) < 1000u) return;
    t = now; seq++;
    bu_hb_t h = { 'B', seq, (uint16_t)(now / 1000u), 0, -128, (uint16_t)(g_v24 * 100.0f) };
    if (g_hse) h.status |= BU_ST_HSE;
    if (bu_judge(&BU_B_L24, g_v24) == BU_PASS) h.status |= BU_ST_RAILS;
    if (g_w5500_ok) h.status |= BU_ST_PERIPH;
    if (bu_peer_alive(&g_peer_a, now, 3000u)) h.status |= BU_ST_PEER1;
    uint8_t d[8]; bu_hb_pack(&h, d);
    hw_can_send(&g_can, BU_CAN_HB_B, d);
    for (int k = 0; k < 8; k++) g_ping[k] = (uint8_t)(seq * 37u + (uint32_t)k * 11u);
    hw_can_send(&g_can, BU_CAN_PING, g_ping);
}

int main(void)
{
    setup();
    outln("\n\n" FW_TEXT);
    outln("Uretim firmware'i DEGILDIR. X-RAY ON surulmez. '?' yardim.");
    if (g_guard.n == 0) outln("HATA: cikis tablosu gecersiz - cikislar kapali.");
    hw_delay_ms(1500);                                    /* Kart A kalp atışı için */
    cmd_rapor();
    bu_console_t con; memset(&con, 0, sizeof con);
    int argc; char *argv[BU_ARGV_MAX];
    out("> ");
    for (;;) {
        uint32_t now = hw_ms();
        int c = hw_uart_getc(CON);
        if (c >= 0) {
            hw_uart_putc(CON, (char)c);
            if (bu_console_feed(&con, (char)c, &argc, argv)) { out("\n"); handle(argc, argv); out("> "); }
        }
        can_poll(now);
        can_tx(now);
        if (bu_guard_tick(&g_guard, now)) outln("\n(sure doldu: cikis kapatildi)");
        apply_outputs();
        int led = bu_guard_find(&g_guard, "LED");                       /* yanıp sönme = firmware çalışıyor */
        if (led >= 0 && !g_guard.on[led]) hw_gpio_write('B', 3, (now / 500u) & 1u);
    }
}
