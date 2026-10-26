/* Teknogem jeneratör protokolü (TGP v1) testleri */
#include "check.h"
#include "hal_sim.h"
#include "proto.h"
#include <string.h>
#include <stdio.h>

static exposure_sm_t sm;
static sim_t s;
static tgp_ctx_t c;
static char r[TGP_REPLY_MAX];

static void setup(void)
{
    sm_config_t cfg = { .hs_dual_channel = true, .pedal_dual_channel = true, .line_readback = true };
    sm_init(&sm, &cfg);
    sim_init(&s);
    sim_run(&s, &sm, 10);
    tgp_init(&c, &sm, "0.7");
}

static const char *cmd(const char *line)
{
    tgp_handle_line(&c, line, r, sizeof r);
    return r;
}

static int starts(const char *a, const char *p) { return strncmp(a, p, strlen(p)) == 0; }

TEST(temel_komutlar)
{
    setup();
    CHECK(strcmp(cmd("PING\r\n"), "OK\r\n") == 0, "PING");
    CHECK(strcmp(cmd("VER"), "OK VER FW=0.7 PROTO=1\r\n") == 0, "VER");
    CHECK(starts(cmd("FOO"), "ERR 904"), "bilinmeyen komut");
}

TEST(ayar_gonderme)
{
    setup();
    CHECK(strcmp(cmd("SET KV=81 MA=25.5 MS=120 FOCUS=L MODE=RAD"),
                 "OK SET KV=81.0 MA=25.5 MS=120 FOCUS=L MODE=RAD\r\n") == 0, "SET kabul ve geri bildirim");
    CHECK(sm.params.kv == 81.0f && sm.params.time_ms == 120u, "durum makinesine uygulandi");
    CHECK(strcmp(cmd("SET KV=90"), "OK SET KV=90.0 MA=25.5 MS=120 FOCUS=L MODE=RAD\r\n") == 0,
          "kismi SET, diger degerler korunur");
    CHECK(starts(cmd("GET SET"), "OK SET KV=90.0"), "GET SET");
}

TEST(ayar_reddi)
{
    setup();
    CHECK(starts(cmd("SET KV=30"), "ERR 201"), "30 kV -> E201");
    CHECK(starts(cmd("SET KV=70 MA=20 FOCUS=S"), "ERR 203"), "kucuk fokus 20 mA -> E203");
    CHECK(starts(cmd("SET KV=70 MA=50 MS=100 FOCUS=L"), "ERR 206"), "3.5 kW -> E206");
    CHECK(starts(cmd("SET KV=abc"), "ERR 901"), "sayi degil");
    CHECK(starts(cmd("SET KV"), "ERR 901"), "deger yok");
    CHECK(starts(cmd("SET FOO=1"), "ERR 901"), "bilinmeyen anahtar");
    CHECK(sm.params.kv == 70.0f, "reddedilen ayar uygulanmadi");
    CHECK(starts(cmd("SET MODE=FLU KV=50 MA=10 FOCUS=S"), "ERR 906"), "floroskopi kapali");
    sm.cfg.fluoro_enabled = true;
    CHECK(starts(cmd("SET MODE=FLU KV=50 MA=10 MS=10000 FOCUS=S"), "OK SET"), "floroskopi acikken kabul");
}

TEST(mesgulken_ayar_degismez)
{
    setup();
    cmd("SET KV=70 MA=20 MS=100 FOCUS=L MODE=RAD");
    s.hs1 = true;
    sim_run(&s, &sm, 1000);
    CHECK(starts(cmd("SET KV=100"), "ERR 900"), "hazirlikta SET -> BUSY");
    CHECK(sm.params.kv == 70.0f, "deger degismedi");
}

TEST(isinlama_komutu_yok)
{
    setup();
    CHECK(starts(cmd("EXPOSE"), "ERR 905"), "EXPOSE reddedilir");
    CHECK(starts(cmd("PREP"), "ERR 905"), "PREP reddedilir");
    CHECK(starts(cmd("ACK"), "ERR 905"), "uzaktan hata onayi reddedilir");
    sim_run(&s, &sm, 100);
    CHECK(s.xray_on_ms == 0 && !s.last_out.preheat, "protokol ile isinlama/hazirlik baslamaz");
}

TEST(saglama_toplami)
{
    setup();
    char line[64];
    const char *body = "SET KV=60";
    snprintf(line, sizeof line, "%s*%02X", body, tgp_checksum(body, strlen(body)));
    CHECK(starts(cmd(line), "OK SET KV=60.0"), "dogru sagliama toplami kabul");
    CHECK(starts(cmd("SET KV=61*00"), "ERR 903"), "yanlis sagliama toplami reddedilir");
    CHECK(sm.params.kv == 60.0f, "bozuk satir uygulanmadi");
}

TEST(cekim_kaydi_ve_durum)
{
    setup();
    CHECK(starts(cmd("GET LAST"), "ERR 902"), "cekim yokken LAST");
    cmd("SET KV=70 MA=20 MS=100 FOCUS=L MODE=RAD");
    s.hs1 = true; sim_run(&s, &sm, 4100);
    CHECK(starts(cmd("GET STATUS"), "OK STATUS READY ERR=E000"), "durum HAZIR");
    s.hs2 = true; sim_run(&s, &sm, 300);
    const char *a = cmd("GET LAST");
    CHECK(starts(a, "OK EXPOSURE KV=70.0 MA=20.0 MS=100 FOCUS=L MODE=RAD AKV="), "kayit basligi");
    CHECK(strstr(a, " AMS=100 ") != NULL, "gerceklesen sure");
    CHECK(strstr(a, " END=E000") != NULL, "normal bitis kodu");
    CHECK(strstr(a, " MAS=2.0") != NULL || strstr(a, " MAS=1.9") != NULL, "olculen mAs ~2");
    char ev[TGP_REPLY_MAX];
    tgp_fmt_event_exposure(&sm.last_record, ev, sizeof ev);
    CHECK(starts(ev, "EVT EXPOSURE KV=70.0"), "olay satiri");
}

TEST(hata_kodu_durumda)
{
    setup();
    cmd("SET KV=70 MA=20 MS=100 FOCUS=L MODE=RAD");
    s.hs1 = true; sim_run(&s, &sm, 1000);
    s.hs2 = true; sim_run(&s, &sm, 10);        /* erken 2. kademe */
    CHECK(starts(cmd("GET STATUS"), "OK STATUS LOCKED ERR=E101"), "durumda E101");
}

TEST(baglanti_kopmasi)
{
    setup();
    cmd("DETREADY 1");
    CHECK(c.pc_detector_ready && c.link_up, "DR hazir bildirdi");
    tgp_tick(&c, 2000);
    CHECK(c.link_up, "2 s: baglanti var");
    tgp_tick(&c, 1500);
    CHECK(!c.link_up && !c.pc_detector_ready, "3 s heartbeat yok: baglanti koptu, hazir dustu");
    cmd("HB");
    CHECK(c.link_up && !c.pc_detector_ready, "geri geldi ama hazir yeniden bildirilmeli");
}

TEST(uzun_satir)
{
    setup();
    char big[300];
    memset(big, 'A', sizeof big - 1); big[sizeof big - 1] = 0;
    CHECK(starts(cmd(big), "ERR 901"), "asiri uzun satir reddedilir");
}

int main(void)
{
    printf("TGP v1 protokol testleri\n");
    RUN(temel_komutlar);
    RUN(ayar_gonderme);
    RUN(ayar_reddi);
    RUN(mesgulken_ayar_degismez);
    RUN(isinlama_komutu_yok);
    RUN(saglama_toplami);
    RUN(cekim_kaydi_ve_durum);
    RUN(hata_kodu_durumda);
    RUN(baglanti_kopmasi);
    RUN(uzun_satir);
    return REPORT();
}
