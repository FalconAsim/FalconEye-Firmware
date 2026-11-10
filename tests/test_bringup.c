/* Devreye alma firmware'i – donanımdan bağımsız çekirdeğin testleri */
#include "check.h"
#include "bu_core.h"
#include "bu_boards.h"
#include <math.h>
#include <string.h>

TEST(guvenlik_tablosu)
{
    CHECK(bu_guard_table_ok(BU_B_OUT, BU_B_NOUT), "Kart B cikis tablosu gecerli");
    /* X-RAY ON isteği tabloda yok, ucu da hiçbir çıkışa atanmamış */
    for (int i = 0; i < BU_B_NOUT; i++) {
        CHECK(!(BU_B_OUT_PIN[i].port == BU_B_XRAY_REQ.port && BU_B_OUT_PIN[i].pin == BU_B_XRAY_REQ.pin), "PE2 XRAY_REQ hicbir cikista yok");
        CHECK(!bu_streq(BU_B_OUT[i].name, "XRAY_REQ"), "XRAY_REQ adi yok");
        for (int j = i + 1; j < BU_B_NOUT; j++)
            CHECK(!(BU_B_OUT_PIN[i].port == BU_B_OUT_PIN[j].port && BU_B_OUT_PIN[i].pin == BU_B_OUT_PIN[j].pin), "cikis uclari tekil");
        for (int k = 0; k < BU_B_NIN; k++)
            CHECK(!(BU_B_OUT_PIN[i].port == BU_B_IN[k].pin.port && BU_B_OUT_PIN[i].pin == BU_B_IN[k].pin.pin), "cikis ucu giris ucu degil");
    }
    /* J2 ve J4 hatlarının hepsi korumalı grupta */
    const char *j2[] = { "SOFAJ", "ONISITMA", "INTLK", "ALMRST", "RL5", "FOKUS", "KAPSARJ" };
    for (size_t k = 0; k < sizeof j2 / sizeof j2[0]; k++) {
        int found = 0;
        for (int i = 0; i < BU_B_NOUT; i++)
            if (bu_streq(BU_B_OUT[i].name, j2[k])) { found = 1; CHECK(BU_B_OUT[i].grp == BU_GRP_J2J4 && BU_B_OUT[i].max_on_ms <= BU_J2_MAX_ON_MS, j2[k]); }
        CHECK(found, "J2/J4 cikisi tabloda");
    }
    /* Yasak ad içeren tablo reddedilir, bekçi hiç çıkış açmaz */
    bu_out_def_t bad[2] = { { "LED", "", BU_GRP_GENEL, 0 }, { "XRAY_REQ", "", BU_GRP_GENEL, 100 } };
    CHECK(!bu_guard_table_ok(bad, 2), "XRAY_REQ iceren tablo reddedilir");
    bu_guard_t g; bu_guard_init(&g, bad, 2);
    CHECK(g.n == 0, "hatali tabloda cikis sayisi 0");
    bu_out_def_t bad2[1] = { { "SOFAJ", "", BU_GRP_J2J4, 0 } };
    CHECK(!bu_guard_table_ok(bad2, 1), "J2 grubunda sinirsiz sure reddedilir");
}

TEST(bekci)
{
    bu_guard_t g; bu_guard_init(&g, BU_B_OUT, BU_B_NOUT);
    CHECK(g.n == BU_B_NOUT, "bekci tum cikislari aldi");
    CHECK(bu_guard_set_name(&g, "xray_req", true, 0) == BU_ERR_FORBIDDEN, "XRAY_REQ yasak");
    CHECK(bu_guard_set_name(&g, "yok", true, 0) == BU_ERR_NAME, "bilinmeyen ad");
    /* onaysız J2 hattı açılmaz */
    CHECK(bu_guard_set_name(&g, "sofaj", true, 100) == BU_ERR_ACK, "onaysiz SOFAJ reddedilir");
    int s = bu_guard_find(&g, "SOFAJ");
    CHECK(!g.on[s], "SOFAJ kapali");
    /* genel çıkış onaysız açılır, süresi dolunca kapanır */
    CHECK(bu_guard_set_name(&g, "lambaisin", true, 100) == BU_OK, "RL8 acilir");
    int l = bu_guard_find(&g, "LAMBAISIN");
    CHECK(bu_guard_tick(&g, 5099) == 0 && g.on[l], "5 s dolmadan acik");
    CHECK(bu_guard_tick(&g, 5100) == 1 && !g.on[l], "5 s sonra kapandi");
    /* onaydan sonra J2 hattı en çok 2 s */
    bu_guard_ack(&g, 1000);
    CHECK(bu_guard_set(&g, s, true, 1000) == BU_OK && g.on[s], "onayla SOFAJ acilir");
    bu_guard_tick(&g, 2999); CHECK(g.on[s], "2 s icinde acik");
    bu_guard_tick(&g, 3000); CHECK(!g.on[s], "2 s sonra kapandi");
    /* onay 10 dk sonra düşer; açık J2 hattı da kapanır */
    bu_guard_set(&g, s, true, 1000 + BU_ACK_VALID_MS - 500);
    bu_guard_tick(&g, 1000 + BU_ACK_VALID_MS + 1);
    CHECK(!g.on[s], "onay suresi dolunca kapandi");
    CHECK(bu_guard_set(&g, s, true, 1000 + BU_ACK_VALID_MS + 2) == BU_ERR_ACK, "onay dustu");
    /* kapatma her zaman serbest; fan sınırsız */
    int f = bu_guard_find(&g, "FAN");
    bu_guard_set(&g, f, true, 0); bu_guard_tick(&g, 3600000u); CHECK(g.on[f], "fan sinirsiz acik");
    CHECK(bu_guard_set(&g, f, false, 1) == BU_OK && !g.on[f], "kapatma serbest");
    bu_guard_set(&g, f, true, 0); bu_guard_all_off(&g); CHECK(!g.on[f], "hepsini kapat");
    /* sayaç taşması (49 gün) */
    bu_guard_set(&g, l, true, 0xFFFFFF00u); bu_guard_tick(&g, 0x00000100u); CHECK(g.on[l], "tasmada sure dogru (512 ms)");
}

TEST(donusumler)
{
    /* 24 V → 100k/10k → 2,18 V; 12 bit, 3,0 V */
    uint16_t c = (uint16_t)(24.0f * 10.0f / 110.0f / 3.0f * 4095.0f + 0.5f);
    CHECK(fabsf(bu_adc_to_v(c, 3.0f, 4095, BU_B_R24_TOP, BU_B_R24_BOT) - 24.0f) < 0.02f, "24 V olcumu");
    CHECK(fabsf(bu_vref_from_vrefint(1655, 1655, 3.0f) - 3.0f) < 1e-4f, "VREF = kalibrasyon");
    CHECK(fabsf(bu_vref_from_vrefint(1655, 1505, 3.0f) - 3.299f) < 0.002f, "VDDA 3,3 V");
    CHECK(bu_is_nan(bu_vref_from_vrefint(1655, 0, 3.0f)), "sifir bolme");
    /* iç sensör: kalibrasyon noktaları */
    CHECK(fabsf(bu_mcu_temp_c(1000, 3.0f, 1000, 1350, 30.0f, 130.0f, 3.0f) - 30.0f) < 0.01f, "TS_CAL1 = 30 C");
    CHECK(fabsf(bu_mcu_temp_c(1350, 3.0f, 1000, 1350, 30.0f, 130.0f, 3.0f) - 130.0f) < 0.01f, "TS_CAL2 = 130 C");
    /* 3,3 V'ta ölçülen kod 3,0 V'a ölçeklenir */
    CHECK(fabsf(bu_mcu_temp_c((uint16_t)(1000 * 3.0f / 3.3f + 0.5f), 3.3f, 1000, 1350, 30.0f, 130.0f, 3.0f) - 30.0f) < 0.5f, "VDDA olcekleme");
    /* NTC: 25 °C'de R = R25 → oran 0,5 */
    CHECK(fabsf(bu_ntc_c(0.5f, 10000.0f, 10000.0f, 3380.0f) - 25.0f) < 0.01f, "NTC 25 C");
    /* 50 °C: R = 10k × exp(3380 × (1/323,15 − 1/298,15)) ≈ 4,16 k (şema notu: ≈ 0,97 V @ 3,3 V) */
    float r50 = 10000.0f * expf(3380.0f * (1.0f / 323.15f - 1.0f / 298.15f));
    float ratio = r50 / (10000.0f + r50);
    CHECK(fabsf(ratio * 3.3f - 0.97f) < 0.01f, "50 C oran sema notuyla ayni");
    CHECK(fabsf(bu_ntc_c(ratio, 10000.0f, 10000.0f, 3380.0f) - 50.0f) < 0.01f, "NTC 50 C");
    CHECK(bu_is_nan(bu_ntc_c(0.99f, 1e4f, 1e4f, 3380.0f)), "acik devre NaN");
    CHECK(bu_is_nan(bu_ntc_c(0.01f, 1e4f, 1e4f, 3380.0f)), "kisa devre NaN");
}

TEST(dac_geri_okuma)
{
    /* Şema: 3 V DAC → J2-5 ≈ 5,2 V (TL431 kıskacı 5,36 V'un altında) */
    CHECK(fabsf(bu_b_ep_j2(3.0f) - 5.199f) < 0.01f, "EP tam skala");
    CHECK(bu_b_ep_j2(3.0f) < 2.495f * (1.0f + 115.0f / 100.0f), "EP kiskac altinda");
    CHECK(fabsf(bu_b_ip_j2(1.2f) - 3.944f) < 0.01f, "IP 1,2 V");
    CHECK(bu_b_ip_j2(1.2f) < 2.495f * (1.0f + 107.0f / 100.0f), "IP test noktasi kiskac altinda");
    CHECK(bu_b_ep_rb(3.0f) < 3.0f && bu_b_ip_rb(1.2f) < 3.0f, "geri okuma ADC araliginda");
    CHECK(bu_b_rb_ok(1.000f, 1.045f) && !bu_b_rb_ok(1.000f, 1.06f), "tolerans %3 + 20 mV");
}

TEST(panel_kimligi)
{
    CHECK(bu_panel_id(2.2f / 12.2f) == BU_PNL_SATOZ40, "2k2 SATOZ");
    CHECK(bu_panel_id(0.5f) == BU_PNL_AT070_50, "10k AT070");
    CHECK(bu_panel_id(33.0f / 43.0f) == BU_PNL_LVDS, "33k LVDS");
    CHECK(bu_panel_id(1.0f) == BU_PNL_NONE, "takili degil");
    CHECK(bu_panel_id(0.33f) == BU_PNL_UNKNOWN, "tanimsiz");
}

TEST(can_kalp_atisi)
{
    bu_hb_t h = { 'A', 7, 3600, BU_ST_HSE | BU_ST_PERIPH, -5, 2412 }, r;
    uint8_t b[8];
    bu_hb_pack(&h, b);
    CHECK(bu_hb_unpack(b, &r), "cozuldu");
    CHECK(r.board == 'A' && r.seq == 7 && r.uptime_s == 3600 && r.status == (BU_ST_HSE | BU_ST_PERIPH) && r.temp_c == -5 && r.aux == 2412, "alanlar ayni");
    b[0] = 'X'; CHECK(!bu_hb_unpack(b, &r), "gecersiz kart harfi");
    bu_peer_t p; memset(&p, 0, sizeof p);
    CHECK(!bu_peer_alive(&p, 0, 3000), "hic gorulmedi");
    bu_peer_seen(&p, &h, 1000);
    CHECK(bu_peer_alive(&p, 4000, 3000) && !bu_peer_alive(&p, 4001, 3000), "3 s zaman asimi");
    CHECK(BU_CAN_HB_A == 0x7E1u && BU_CAN_PING < 0x800u, "kimlikler 11 bit");
}

TEST(konsol)
{
    bu_console_t c; memset(&c, 0, sizeof c);
    int argc = 0; char *argv[BU_ARGV_MAX];
    const char *in = "cikis  LambaIsin 1\r\n";
    bool got = false;
    for (const char *p = in; *p; p++) if (bu_console_feed(&c, *p, &argc, argv)) got = true;
    CHECK(got && argc == 3, "uc sozcuk");
    CHECK(bu_streq(argv[0], "CIKIS") && bu_streq(argv[1], "lambaisin") && strcmp(argv[2], "1") == 0, "sozcukler");
    const char *in2 = "dac 3x\b00 1200\n";
    for (const char *p = in2; *p; p++) bu_console_feed(&c, *p, &argc, argv);
    long v;
    CHECK(argc == 3 && bu_parse_int(argv[1], &v) && v == 300, "geri silme");
    CHECK(bu_parse_int("0x7E1", &v) && v == 0x7E1, "on altilik");
    CHECK(!bu_parse_int("12a", &v) && !bu_parse_int("", &v), "hatali sayi");
    /* uzun satır taşmaz */
    for (int i = 0; i < 200; i++) bu_console_feed(&c, 'a', &argc, argv);
    CHECK(bu_console_feed(&c, '\n', &argc, argv) && strlen(argv[0]) == BU_LINE_MAX - 1, "uzun satir kesildi");
}

TEST(bicim)
{
    char b[96];
    bu_fmt_f(b, sizeof b, -12.345f, 2); CHECK(strcmp(b, "-12.35") == 0, "ondalik yuvarlama");
    bu_fmt_f(b, sizeof b, 0.05f, 1); CHECK(strcmp(b, "0.1") == 0, "0.05 -> 0.1");
    bu_fmt_f(b, sizeof b, NAN, 2); CHECK(strcmp(b, "---") == 0, "NaN");
    bu_fmt_hex(b, sizeof b, 0xEF4016u, 6); CHECK(strcmp(b, "EF4016") == 0, "hex");
    int n = bu_fmt_result(b, sizeof b, &BU_B_L24, 24.12f, bu_judge(&BU_B_L24, 24.12f));
    CHECK(n > 40 && strncmp(b, "GECTI", 5) == 0 && strstr(b, "24.12 V") && strstr(b, "[22.80 .. 25.20]"), "sonuc satiri");
    CHECK(bu_judge(&BU_B_L24, NAN) == BU_FAIL, "NaN kaldi");
    char s[8]; bu_fmt_result(s, sizeof s, &BU_B_L24, 1.0f, BU_FAIL); CHECK(strlen(s) == 7, "kucuk tampon tasmaz");
}

TEST(sensirion_crc)
{
    const uint8_t d[2] = { 0xBE, 0xEF };
    CHECK(bu_sensirion_crc(d, 2) == 0x92, "Sensirion veri sayfasi ornegi 0xBEEF -> 0x92");
}

int main(void)
{
    RUN(guvenlik_tablosu);
    RUN(bekci);
    RUN(donusumler);
    RUN(dac_geri_okuma);
    RUN(panel_kimligi);
    RUN(can_kalp_atisi);
    RUN(konsol);
    RUN(bicim);
    RUN(sensirion_crc);
    return REPORT();
}
