/* Teknogem jeneratör protokolü (TGP v1, arayüz tanımı rev. 0.5) testleri */
#include "check.h"
#include "hal_sim.h"
#include "proto.h"
#include <string.h>
#include <stdio.h>

static exposure_sm_t sm;
static sim_t s;
static tgp_ctx_t c;
static char r[TGP_REPLY_MAX];
static int rx_seq = 0;

static void setup(void)
{
    sm_config_t cfg = { .hs_dual_channel = true, .pedal_dual_channel = true, .line_readback = true };
    sm_init(&sm, &cfg);
    sim_init(&s);
    sim_run(&s, &sm, 10);
    tgp_init(&c, &sm, "0.9.0");
    rx_seq = 0;
}

/* İstek satırını SEQ ve CRC ile çerçeveler ve gönderir */
static const char *send_seq(int seq, const char *body)
{
    char line[TGP_LINE_MAX + 16];
    int n = snprintf(line, sizeof line, "%04d %s", seq, body);
    snprintf(line + n, sizeof line - (size_t)n, "*%04X\r\n", tgp_crc16(line, (size_t)n));
    tgp_handle_line(&c, line, r, sizeof r);
    return r;
}

static const char *cmd(const char *body) { return send_seq(++rx_seq, body); }

/* Cevaptaki her satırın biçimini ve CRC'sini doğrular; satır sayısını döner */
static int valid_lines(const char *txt)
{
    int count = 0;
    while (*txt) {
        const char *eol = strstr(txt, "\r\n");
        if (!eol) return -1;
        const char *star = NULL;
        for (const char *p = txt; p < eol; p++) if (*p == '*') star = p;
        if (!star || eol - star != 5) return -1;
        for (int i = 0; i < 4; i++) if (txt[i] < '0' || txt[i] > '9') return -1;
        if (txt[4] != ' ') return -1;
        unsigned got = 0;
        if (sscanf(star + 1, "%4X", &got) != 1) return -1;
        if (got != tgp_crc16(txt, (size_t)(star - txt))) return -1;
        count++;
        txt = eol + 2;
    }
    return count;
}

/* n'inci satırın SEQ'ten sonraki gövdesi bu önekle mi başlıyor */
static int line_has(const char *txt, int idx, const char *prefix)
{
    for (int i = 0; i < idx; i++) {
        const char *eol = strstr(txt, "\r\n");
        if (!eol) return 0;
        txt = eol + 2;
    }
    if (strlen(txt) < 5) return 0;
    return strncmp(txt + 5, prefix, strlen(prefix)) == 0;
}

TEST(acilis_degerleri)
{
    setup();
    const char *a = cmd("STATUS?");
    CHECK(line_has(a, 1, "ACTUAL KV=60.0 MA=10 MS=100 MAS=1.00 FO=S MODE=RAD"),
          "acilis: radyografi, kucuk fokus, kucuk fokus sinirlari icinde");
}

TEST(crc_test_vektorleri)
{
    CHECK(tgp_crc16("123456789", 9) == 0x29B1, "CRC-16/CCITT-FALSE kontrol degeri");
    CHECK(tgp_crc16("0001 HELLO", 10) == 0x7242, "0001 HELLO*7242");
    const char *v = "0012 SET KV=70.0 MA=20 MS=100 FO=L";
    CHECK(tgp_crc16(v, strlen(v)) == 0xCE47, "SET ornegi *CE47");
    const char *v2 = "0104 NAK 0013 E203";
    CHECK(tgp_crc16(v2, strlen(v2)) == 0xA7BC, "NAK ornegi *A7BC");
    const char *v3 = "0101 VERSION FW=0.9.0 HW=A TGP=1 IF=0.5 TUBE=DF-161SBR CTRL=PC LINK=TGP SN=FE0001 OPT=FLUORO+DAP";
    CHECK(tgp_crc16(v3, strlen(v3)) == 0x311C, "VERSION ornegi *311C");
    const char *v4 = "0107 DOSE XN=152 DAP=1.234 SRC=METER";
    CHECK(tgp_crc16(v4, strlen(v4)) == 0x7681, "DOSE ornegi *7681");
}

TEST(temel_komutlar)
{
    setup();
    const char *a = cmd("PING");
    CHECK(valid_lines(a) == 1 && line_has(a, 0, "PONG 0001"), "PING -> PONG <seq>");
    a = cmd("HELLO");
    CHECK(valid_lines(a) == 2 && line_has(a, 0, "ACK 0002"), "HELLO -> ACK");
    CHECK(line_has(a, 1, "VERSION FW=0.9.0 HW=A TGP=1 IF=0.5 TUBE=DF-161SBR CTRL=PC LINK=TGP"),
          "HELLO -> VERSION satiri");
    a = cmd("FOO");
    CHECK(valid_lines(a) == 1 && line_has(a, 0, "NAK 0003 E904"), "bilinmeyen komut E904");
}

TEST(kartin_satir_sayaci)
{
    setup();
    c.tx_seq = 102;
    const char *a = send_seq(12, "SET KV=70.0 MA=20 MS=100 FO=L");
    CHECK(strncmp(a, "0102 ACK 0012*BE6D\r\n", 20) == 0, "arayuz tanimi Ek A: ACK vektoru");
    CHECK(strstr(a, "0103 ACTUAL KV=70.0 MA=20 MS=100 MAS=2.00 FO=L MODE=RAD*") != NULL,
          "ACTUAL satiri, kartin sayaci artiyor");
    c.tx_seq = 9999;
    a = cmd("PING");
    CHECK(strncmp(a, "9999 ", 5) == 0, "9999");
    a = cmd("PING");
    CHECK(strncmp(a, "0000 ", 5) == 0, "9999'dan sonra 0000");
}

TEST(ayar_gonderme)
{
    setup();
    const char *a = cmd("SET KV=81.0 MA=25 MS=120 FO=L");
    CHECK(valid_lines(a) == 2 && line_has(a, 0, "ACK 0001"), "SET kabul");
    CHECK(line_has(a, 1, "ACTUAL KV=81.0 MA=25 MS=120 MAS=3.00 FO=L MODE=RAD"), "ACTUAL geri bildirim");
    CHECK(sm.params.kv == 81.0f && sm.params.time_ms == 120u, "durum makinesine uygulandi");
    a = cmd("SET KV=90");
    CHECK(line_has(a, 1, "ACTUAL KV=90.0 MA=25 MS=120"), "kismi SET, diger degerler korunur");
    a = cmd("STATUS?");
    CHECK(valid_lines(a) == 2 && line_has(a, 0, "STATUS ST=IDLE CTRL=PC LINK=TGP MODE=RAD ERR=E000"),
          "STATUS satiri");
    CHECK(line_has(a, 1, "ACTUAL KV=90.0"), "STATUS ile ACTUAL");
}

TEST(mas_teknigi)
{
    setup();
    const char *a = cmd("SET KV=70.0 MA=20 MAS=2.50 FO=L");
    CHECK(line_has(a, 1, "ACTUAL KV=70.0 MA=20 MS=125 MAS=2.50"), "MAS + MA -> sure hesaplanir");
    CHECK(line_has(cmd("SET MAS=2.0"), 0, "NAK 0002 E901"), "MAS var MA yok -> E901 (mA'yi yazilim belirler)");
    CHECK(line_has(cmd("SET MA=20 MS=100 MAS=2.0"), 0, "NAK 0003 E901"), "MS ve MAS birlikte -> E901");
    CHECK(sm.params.time_ms == 125u, "reddedilenler uygulanmadi");
}

TEST(ayar_reddi)
{
    setup();
    CHECK(line_has(cmd("SET KV=30.0"), 0, "NAK 0001 E201"), "30 kV -> E201");
    CHECK(line_has(cmd("SET KV=70.0 MA=20 FO=S"), 0, "NAK 0002 E203"), "kucuk fokus 20 mA -> E203");
    CHECK(line_has(cmd("SET KV=70.0 MA=50 MS=100 FO=L"), 0, "NAK 0003 E206"), "3.5 kW -> E206");
    CHECK(line_has(cmd("SET KV=abc"), 0, "NAK 0004 E901"), "sayi degil");
    CHECK(line_has(cmd("SET KV"), 0, "NAK 0005 E901"), "deger yok");
    CHECK(line_has(cmd("SET FOO=1"), 0, "NAK 0006 E901"), "bilinmeyen anahtar");
    CHECK(line_has(cmd("SET MA=25.5"), 0, "NAK 0007 E901"), "mA ondalikli olamaz");
    CHECK(line_has(cmd("SET KV=70.55"), 0, "NAK 0008 E901"), "kV en cok bir ondalik");
    CHECK(sm.params.kv == 60.0f, "reddedilen ayar uygulanmadi (acilis degeri 60 kV)");
    CHECK(line_has(cmd("MODE M=FLUORO"), 0, "NAK 0009 E906"), "floroskopi kapali");
    sm.cfg.fluoro_enabled = true;
    CHECK(line_has(cmd("SET KV=50.0 MA=10 MS=10000 FO=S"), 0, "ACK"), "floroskopi degerleri");
    const char *a = cmd("MODE M=FLUORO");
    CHECK(line_has(a, 0, "ACK") && line_has(a, 1, "ACTUAL KV=50.0 MA=2 MS=20000 MAS=40.00 FO=S MODE=FLUORO"),
          "floroskopi acikken MODE kabul; ilk geciste floroskopi baslangic degerleri");
    CHECK(line_has(cmd("SET KV=55.0 MA=3"), 0, "ACK"), "floroskopide deger degisimi");
    a = cmd("MODE M=RAD");
    CHECK(line_has(a, 1, "ACTUAL KV=50.0 MA=10 MS=10000 MAS=100.00 FO=S MODE=RAD"), "RAD'a donus: radyografi degerleri geri geldi");
    a = cmd("MODE M=FLUORO");
    CHECK(line_has(a, 1, "ACTUAL KV=55.0 MA=3 MS=20000 MAS=60.00 FO=S MODE=FLUORO"), "floroskopi son degerleri hatirlandi");
}

TEST(opsiyonlar_ve_doz)
{
    setup();
    CHECK(strstr(cmd("VERSION?"), " OPT=NONE*") != NULL, "opsiyon yok: OPT=NONE");
    sm.cfg.fluoro_enabled = true;
    CHECK(strstr(cmd("VERSION?"), " OPT=FLUORO*") != NULL, "OPT=FLUORO");
    sm.cfg.dap_meter_enabled = true;
    CHECK(strstr(cmd("VERSION?"), " OPT=FLUORO+DAP*") != NULL, "OPT=FLUORO+DAP");
    tube_select(&sm.tube, TUBE_INSERT_D067SB);
    CHECK(strstr(cmd("VERSION?"), " OPT=DAP*") != NULL, "tek fokuslu insertte floroskopi kullanilamaz");
    CHECK(line_has(cmd("MODE M=FLUORO"), 0, "NAK 0005 E906"), "tek fokuslu insertte MODE FLUORO -> E906");
    tube_select(&sm.tube, TUBE_INSERT_DF161SBR);

    /* DAP: DONE'dan sonra DOSE satırı, yalnız metre açık ve okuma geldiyse */
    char out[TGP_REPLY_MAX];
    sm.last_record.valid = true;
    sm.last_record.dap_valid = false;
    CHECK(tgp_fmt_dose(&c, &sm.last_record, out, sizeof out) == 0, "okuma yok: DOSE yok");
    CHECK(sm_record_dap(&sm, 12.345f), "DAP kaydedildi");
    c.xn = 7;
    CHECK(tgp_fmt_dose(&c, &sm.last_record, out, sizeof out) > 0 && valid_lines(out) == 1 &&
          line_has(out, 0, "DOSE XN=7 DAP=12.345 SRC=METER"), "DOSE satiri");
    const char *a = cmd("LAST?");
    CHECK(valid_lines(a) == 2 && line_has(a, 0, "STATE ST=DONE") && line_has(a, 1, "DOSE XN=7 DAP=12.345"), "LAST? DONE + DOSE");
    sm.cfg.dap_meter_enabled = false;
    CHECK(!sm_record_dap(&sm, 1.0f), "metre kapali: DAP kayda yazilmaz");
    CHECK(tgp_fmt_dose(&c, &sm.last_record, out, sizeof out) == 0, "metre kapali (veteriner): DOSE gonderilmez");
    CHECK(valid_lines(cmd("LAST?")) == 1, "LAST? yalniz DONE");
}

TEST(pc_manuel_modu)
{
    setup();
    c.ctrl = TGP_CTRL_MANUAL;
    CHECK(line_has(cmd("SET KV=65.0"), 0, "NAK 0001 E907"), "Manuel modda SET -> E907");
    CHECK(line_has(cmd("MODE M=RAD"), 0, "NAK 0002 E907"), "Manuel modda MODE -> E907");
    CHECK(sm.params.kv == 60.0f, "Manuel modda deger degismedi");
    const char *a = cmd("STATUS?");
    CHECK(line_has(a, 0, "STATUS ST=IDLE CTRL=MANUAL"), "Manuel modda okuma calisir");
    CHECK(line_has(cmd("VERSION?"), 0, "VERSION") && strstr(r, "CTRL=MANUAL") != NULL, "VERSION'da CTRL");
    c.ctrl = TGP_CTRL_PC;
    c.link_sel = TGP_LINK_CURRENT;
    CHECK(line_has(cmd("SET KV=60.0"), 0, "NAK 0005 E907"), "PC baglantisi Current iken TGP salt okunur");
    c.link_sel = TGP_LINK_TGP;
    CHECK(line_has(cmd("SET KV=60.0"), 0, "ACK 0006"), "PC + TGP: yazma serbest");
}

TEST(isinlama_sirasinda_ayar_degismez)
{
    setup();
    cmd("SET KV=70.0 MA=20 MS=1000 FO=L");
    s.hs1 = true; sim_run(&s, &sm, 4100);
    s.hs2 = true; sim_run(&s, &sm, 100);
    CHECK(sm.state == ST_EXPOSING, "isinlama suruyor");
    CHECK(line_has(cmd("SET KV=100.0"), 0, "NAK 0002 E900"), "isinlamada SET -> E900");
    CHECK(sm.params.kv == 70.0f, "deger degismedi");
}

TEST(hazirlikta_set)
{
    setup();
    cmd("SET KV=70.0 MA=20 MS=100 FO=L");
    s.hs1 = true; sim_run(&s, &sm, 4100);
    CHECK(sm.state == ST_READY, "HAZIR");
    const char *a = cmd("SET KV=75.0");
    CHECK(line_has(a, 0, "ACK 0002") && line_has(a, 1, "ACTUAL KV=75.0"), "hazirlikta kV kabul");
    CHECK(line_has(cmd("STATUS?"), 0, "STATUS ST=PREHEAT"), "isinma bastan: PREHEAT");
    CHECK(line_has(cmd("SET FO=S MA=10"), 0, "NAK 0004 E213"), "hazirlikta fokus -> E213");
    CHECK(sm.params.focus == FOCUS_LARGE && sm.params.ma == 20.0f, "fokus ve mA degismedi");
}

TEST(kesilen_cekim_kaydi)
{
    setup();
    sm_set_pc_link(&sm, true, false);
    cmd("SET KV=60.0 MA=10 MS=2000 FO=L");
    s.hs1 = true; sim_run(&s, &sm, 4100);
    s.hs2 = true; sim_run(&s, &sm, 300);
    s.pc_link_ok = false; sim_run(&s, &sm, 2);
    char ev[TGP_REPLY_MAX];
    tgp_fmt_done(&c, &sm.last_record, ev, sizeof ev);
    CHECK(strstr(ev, " CUT=1 END=E702*") != NULL, "DONE kaydinda CUT=1, E702");
}

TEST(isinlama_komutu_yok)
{
    setup();
    CHECK(line_has(cmd("EXPOSE"), 0, "NAK 0001 E905"), "EXPOSE reddedilir");
    CHECK(line_has(cmd("XRAY"), 0, "NAK 0002 E905"), "XRAY reddedilir");
    CHECK(line_has(cmd("PREP"), 0, "NAK 0003 E905"), "PREP reddedilir");
    CHECK(line_has(cmd("ACK-FAULT"), 0, "NAK 0004 E905"), "uzaktan hata onayi reddedilir");
    CHECK(line_has(cmd("RESET"), 0, "NAK 0005 E905"), "RESET reddedilir");
    sim_run(&s, &sm, 100);
    CHECK(s.xray_on_ms == 0 && !s.last_out.preheat, "protokol ile isinlama/hazirlik baslamaz");
}

TEST(saglama_ve_bozuk_satir)
{
    setup();
    tgp_handle_line(&c, "0007 SET KV=61.0*0000\r\n", r, sizeof r);
    CHECK(line_has(r, 0, "NAK 0007 E903"), "yanlis CRC -> NAK E903");
    tgp_handle_line(&c, "0008 SET KV=61.0\r\n", r, sizeof r);
    CHECK(line_has(r, 0, "NAK 0008 E903"), "CRC yok -> NAK E903");
    const size_t n = tgp_handle_line(&c, "XX SET KV=61.0*1234\r\n", r, sizeof r);
    CHECK(n == 0, "SEQ okunamayan satir sessizce atilir");
    CHECK(sm.params.kv != 61.0f, "bozuk satirlar uygulanmadi");
    CHECK(c.bad_lines == 3, "servis sayaci");
    CHECK(!c.link_up, "bozuk satir canlilik saymaz");
}

TEST(yinelenen_istek)
{
    setup();
    char first[TGP_REPLY_MAX];
    send_seq(40, "SET KV=60.0");
    strcpy(first, r);
    sm.params.kv = 65.0f;                    /* panelden degisti */
    send_seq(40, "SET KV=60.0");             /* cevap kayboldu, yazilim yeniden gonderdi */
    CHECK(strcmp(first, r) == 0, "ayni SEQ: onceki cevap aynen tekrarlanir");
    CHECK(sm.params.kv == 65.0f, "ayni SEQ: komut yeniden uygulanmaz");
    send_seq(41, "SET KV=60.0");
    CHECK(sm.params.kv == 60.0f, "yeni SEQ uygulanir");
}

TEST(cekim_kaydi_ve_durum)
{
    setup();
    CHECK(line_has(cmd("LAST?"), 0, "NAK 0001 E902"), "cekim yokken LAST?");
    cmd("SET KV=70.0 MA=20 MS=100 FO=L");
    s.hs1 = true; sim_run(&s, &sm, 4100);
    CHECK(line_has(cmd("STATUS?"), 0, "STATUS ST=READY"), "durum READY");
    s.hs2 = true; sim_run(&s, &sm, 300);
    char ev[TGP_REPLY_MAX];
    tgp_fmt_done(&c, &sm.last_record, ev, sizeof ev);
    CHECK(valid_lines(ev) == 1 && line_has(ev, 0, "STATE ST=DONE XN=1 KV=70.0 MA=20 MS=100 AKV="),
          "DONE olayi, cekim sayaci");
    CHECK(strstr(ev, " AMS=100 ") && strstr(ev, " MEAS=1 CUT=0 END=E000*"), "gerceklesen sure ve bitis");
    CHECK(strstr(ev, " MAS=2.0") || strstr(ev, " MAS=1.9"), "olculen mAs ~2");
    const char *a = cmd("LAST?");
    CHECK(line_has(a, 0, "STATE ST=DONE XN=1 KV=70.0"), "LAST? ayni kayit");
}

TEST(hata_kodu_durumda)
{
    setup();
    cmd("SET KV=70.0 MA=20 MS=100 FO=L");
    s.hs1 = true; sim_run(&s, &sm, 1000);
    s.hs2 = true; sim_run(&s, &sm, 10);        /* erken 2. kademe */
    CHECK(line_has(cmd("STATUS?"), 0, "STATUS ST=LOCKED CTRL=PC LINK=TGP MODE=RAD ERR=E101"), "durumda E101");
    char ev[TGP_REPLY_MAX];
    tgp_fmt_state(&c, ev, sizeof ev);
    CHECK(valid_lines(ev) == 1 && line_has(ev, 0, "STATE ST=LOCKED ERR=E101"), "STATE olayi");
    tgp_fmt_error(&c, 'E', 101, false, ev, sizeof ev);
    CHECK(line_has(ev, 0, "ERROR E=E101 CLR=0"), "ERROR olayi");
}

TEST(sinir_bildirimi)
{
    setup();
    const char *a = cmd("LIMITS?");
    CHECK(valid_lines(a) == 2, "floroskopi kapali: 2 satir");
    CHECK(line_has(a, 0, "LIMITS TUBE=DF-161SBR MODE=RAD FO=L KV=40.0-110.0 MA=1-60 MS=1-10000 P=3200"),
          "buyuk fokus satiri");
    CHECK(line_has(a, 1, "LIMITS TUBE=DF-161SBR MODE=RAD FO=S KV=40.0-110.0 MA=1-15 MS=1-10000 P=720"),
          "kucuk fokus satiri");
    sm.cfg.fluoro_enabled = true;
    a = cmd("LIMITS?");
    CHECK(valid_lines(a) == 3 && line_has(a, 2, "LIMITS TUBE=DF-161SBR MODE=FLUORO FO=S KV=40.0-60.0 MA=1-15 MS=1-20000 P=600"),
          "floroskopi satiri");
}

TEST(tek_fokuslu_insert)
{
    setup();
    tube_select(&sm.tube, TUBE_INSERT_D067SB);
    sm.cfg.fluoro_enabled = true;
    const char *a = cmd("LIMITS?");
    CHECK(valid_lines(a) == 1 && line_has(a, 0, "LIMITS TUBE=D-067SB MODE=RAD FO=L KV=50.0-100.0 MA=1-22 MS=1-10000 P=1270"),
          "tek fokus: yalniz FO=L satiri, floroskopi yok");
    CHECK(line_has(cmd("SET KV=70.0 MA=5 MS=100 FO=S"), 0, "NAK 0002 E214"), "kucuk fokus -> E214");
    CHECK(line_has(cmd("SET KV=45.0 MA=5 MS=100 FO=L"), 0, "NAK 0003 E201"), "45 kV < 50 kV -> E201");
    CHECK(strstr(cmd("VERSION?"), "TUBE=D-067SB") != NULL, "VERSION'da insert");
}

TEST(baglanti_kopmasi)
{
    setup();
    CHECK(line_has(cmd("DETREADY R=1"), 0, "ACK"), "DR hazir bildirdi");
    CHECK(c.pc_detector_ready && c.link_up, "hazir ve bagli");
    tgp_tick(&c, 2000);
    CHECK(c.link_up, "2 s: baglanti var");
    tgp_tick(&c, 1500);
    CHECK(!c.link_up && !c.pc_detector_ready, "3 s satir yok: baglanti koptu, hazir dustu");
    cmd("PING");
    CHECK(c.link_up && !c.pc_detector_ready, "geri geldi ama hazir yeniden bildirilmeli");
    tgp_link_lost(&c);
    CHECK(!c.link_up, "link/TCP kopmasi aninda");
}

TEST(isinlamada_kisa_canlilik_esigi)
{
    setup();
    cmd("SET KV=70.0 MA=20 MS=2000 FO=L");
    s.hs1 = true; sim_run(&s, &sm, 4100);
    s.hs2 = true; sim_run(&s, &sm, 50);
    CHECK(sm.state == ST_EXPOSING, "isinlama suruyor");
    cmd("PING");
    tgp_tick(&c, 700);
    CHECK(c.link_up, "700 ms: bagli");
    tgp_tick(&c, 100);
    CHECK(!c.link_up, "isinlamada 750 ms asilinca kopuk");
}

TEST(uzun_satir)
{
    setup();
    char big[300];
    memcpy(big, "0009 ", 5);
    memset(big + 5, 'A', sizeof big - 6); big[sizeof big - 1] = 0;
    tgp_handle_line(&c, big, r, sizeof r);
    CHECK(line_has(r, 0, "NAK 0009 E901"), "asiri uzun satir reddedilir");
}

int main(void)
{
    printf("TGP v1 (rev. 0.5) protokol testleri\n");
    RUN(acilis_degerleri);
    RUN(crc_test_vektorleri);
    RUN(temel_komutlar);
    RUN(kartin_satir_sayaci);
    RUN(ayar_gonderme);
    RUN(mas_teknigi);
    RUN(ayar_reddi);
    RUN(opsiyonlar_ve_doz);
    RUN(pc_manuel_modu);
    RUN(isinlama_sirasinda_ayar_degismez);
    RUN(hazirlikta_set);
    RUN(kesilen_cekim_kaydi);
    RUN(isinlama_komutu_yok);
    RUN(saglama_ve_bozuk_satir);
    RUN(yinelenen_istek);
    RUN(cekim_kaydi_ve_durum);
    RUN(hata_kodu_durumda);
    RUN(sinir_bildirimi);
    RUN(tek_fokuslu_insert);
    RUN(baglanti_kopmasi);
    RUN(isinlamada_kisa_canlilik_esigi);
    RUN(uzun_satir);
    return REPORT();
}
