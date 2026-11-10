/* Kart M (hayvan modülü) çekirdeği: protokol, R bulucu, solunum fazı, tetik penceresi – yapay sinyallerle */
#include "check.h"
#include "km_app.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ---------- yapay sinyaller ---------- */
static uint32_t lcg = 12345u;
static int32_t noise(int32_t amp) { lcg = lcg * 1664525u + 1013904223u; return (int32_t)((lcg >> 16) % (uint32_t)(2 * amp + 1)) - amp; }

typedef struct { uint32_t rr_ms, qrs_ms; int32_t amp; bool clean; } ecg_gen_t;
/* R tepeleri t = 500 + k·rr anlarında */
static int32_t ecg_uv(const ecg_gen_t *g, uint32_t t)
{
    double v = 0;
    if (t >= 500u - g->qrs_ms) {
        int32_t k = (int32_t)((t + g->rr_ms / 2u - 500u) / g->rr_ms);
        double dt = (double)t - (500.0 + (double)k * g->rr_ms);
        double h = g->qrs_ms / 2.0;
        if (fabs(dt) < h) v += g->amp * (1.0 - fabs(dt) / h);                       /* QRS: üçgen */
        double tt = dt - 0.15 * g->rr_ms, tw = 0.25 * g->rr_ms;
        if (tt > 0 && tt < tw) v += 0.25 * g->amp * sin(3.14159265 * tt / tw);       /* T dalgası */
    }
    if (!g->clean) {
        v += 300.0 * sin(2 * 3.14159265 * 0.3 * t / 1000.0);                         /* taban kayması */
        v += 30.0 * sin(2 * 3.14159265 * 50.0 * t / 1000.0);                         /* şebeke */
        v += noise(15);
    }
    return (int32_t)v;
}

/* solunum (yastık, 10 Pa birimi): 300 ms alma, 400 ms verme, kalan süre düzlük */
static int32_t resp_val(uint32_t t, uint32_t period)
{
    uint32_t ph = t % period; double v = 300.0;
    if (ph < 300u) v += 40.0 * ph / 300.0; else if (ph < 700u) v += 40.0 * (1.0 - (ph - 300.0) / 400.0);
    return (int32_t)v + noise(1) / 2;
}
static bool resp_in_pause(uint32_t t, uint32_t period) { uint32_t ph = t % period; return ph >= 700u; }

/* ---------- CAN yakalama ---------- */
typedef struct {
    km_win_t win[20000]; uint32_t win_t[20000]; int n_win;
    km_revt_t r[2000]; uint32_t r_now[2000]; int n_r;
    km_status_t st; int n_st; km_cfg_ack_t ack; int n_ack; int n_wecg, n_wresp, n_ver; km_cmd_ack_t cack;
    uint32_t now;
} cap_t;
static cap_t cap;
static bool tx(void *ctx, uint32_t id, const uint8_t *d, uint8_t len)
{
    (void)ctx;
    switch (id) {
    case KM_ID_WIN: if (cap.n_win < 20000 && km_unpack_win(d, len, &cap.win[cap.n_win])) cap.win_t[cap.n_win++] = cap.now; break;
    case KM_ID_R_EVT: if (cap.n_r < 2000 && km_unpack_revt(d, len, &cap.r[cap.n_r])) cap.r_now[cap.n_r++] = cap.now; break;
    case KM_ID_STATUS: if (km_unpack_status(d, len, &cap.st)) cap.n_st++; break;
    case KM_ID_CFG_ACK: if (km_unpack_cfg_ack(d, len, &cap.ack)) cap.n_ack++; break;
    case KM_ID_CMD_ACK: (void)km_unpack_cmd_ack(d, len, &cap.cack); break;
    case KM_ID_WAVE_ECG: cap.n_wecg++; break;
    case KM_ID_WAVE_RESP: cap.n_wresp++; break;
    case KM_ID_VERSION: cap.n_ver++; break;
    default: break;
    }
    return true;
}

static km_app_t app;
static void start(void)
{
    km_version_t v = { 0, 0, 1, 0, 2, 0x1234u };
    memset(&cap, 0, sizeof cap); lcg = 12345u;
    km_app_init(&app, tx, 0, &v);
}
static km_cfg_result_t set_cfg(uint32_t t, uint8_t src, uint8_t sp, uint16_t dly, uint16_t win, uint8_t rph)
{
    km_cfg_t c = { src, rph, sp, 0, dly, win }; uint8_t d[8];
    cap.now = t;
    km_app_rx(&app, t, KM_ID_CFG, d, km_pack_cfg(&c, d));
    return (km_cfg_result_t)cap.ack.result;
}
static void hb(uint32_t t, bool wave)
{
    km_hb_t h = { 0, wave ? 1u : 0u, t }; uint8_t d[8];
    km_app_rx(&app, t, KM_ID_HB_A, d, km_pack_hb(&h, d));
}
/* t0..t1 ms çalıştır */
static void run(uint32_t t0, uint32_t t1, const ecg_gen_t *g, uint32_t resp_period, uint8_t lead, bool wave)
{
    for (uint32_t t = t0; t < t1; t++) {
        cap.now = t;
        if (t % 100u == 0u) hb(t, wave);
        if (resp_period && t % 20u == 0u) km_app_resp_sample(&app, t, resp_val(t, resp_period));
        km_app_ecg_sample(&app, t, g ? ecg_uv(g, t) : 0, lead);
    }
}

/* ---------- testler ---------- */
TEST(protokol_gidis_donus)
{
    uint8_t d[24];
    km_win_t w = { 7, true, 123, KM_SRC_BOTH, KM_WINF_ECG_OK, 0xBEEF }, w2;
    CHECK(km_pack_win(&w, d) == 8 && km_unpack_win(d, 8, &w2) && w2.seq == 7 && w2.open && w2.remain_ms == 123 && w2.cfg_crc == 0xBEEF, "pencere");
    CHECK(!km_unpack_win(d, 7, &w2), "yanlis uzunluk reddedilir");
    d[1] = 2; CHECK(!km_unpack_win(d, 8, &w2), "gecersiz durum reddedilir");
    km_revt_t r = { 0x12345678u, 100, -1500 }, r2;
    CHECK(km_pack_revt(&r, d) == 8 && km_unpack_revt(d, 8, &r2) && r2.t_ms == r.t_ms && r2.amp_uv == -1500, "R olayi");
    km_status_t s = { KM_SRC_ECG, KM_ECGQ_NOISE, KM_LEAD_LL, KM_RSRC_FLOW, 420, 615, -12, 0xA55A, KM_ERR_SDP, 1, 3600 }, s2;
    CHECK(km_pack_status(&s, d) == 16 && km_unpack_status(d, 16, &s2) && s2.hr_bpm == 420 && s2.rbase == -12 && s2.uptime_s == 3600 && s2.err == KM_ERR_SDP, "durum");
    km_wave_t wv, wv2; wv.t_ms = 99; for (int i = 0; i < 10; i++) wv.s[i] = (int16_t)(i * 100 - 500);
    CHECK(km_pack_wave(&wv, d) == 24 && km_unpack_wave(d, 24, &wv2) && wv2.s[0] == -500 && wv2.s[9] == 400, "dalga");
    km_cfg_t c = { KM_SRC_ECG, KM_RPH_EXP, KM_SP_RAT, 0, 40, 30 }, c2 = c;
    uint16_t crc = km_cfg_crc(&c); c2.dly_ms = 41;
    CHECK(crc != km_cfg_crc(&c2), "ayar CRC'si alan degisince degisir");
}

TEST(pencere_alicisi_hataya_guvenli)
{
    km_win_rx_t rx; km_win_rx_init(&rx);
    km_win_t w = { 1, true, 50, KM_SRC_ECG, KM_WINF_ECG_OK, 0x1111 };
    CHECK(!km_win_rx_usable(&rx, 0, 10, 0x1111), "mesaj yokken kapali");
    km_win_rx_update(&rx, &w, 1000);
    CHECK(km_win_rx_usable(&rx, 1002, 40, 0x1111), "taze ve yeterli sure: kullanilir");
    CHECK(!km_win_rx_usable(&rx, 1002, 49, 0x1111), "kalan sure yetmiyor");
    CHECK(!km_win_rx_usable(&rx, 1011, 10, 0x1111), "10 ms'den eski mesaj: kapali");
    CHECK(!km_win_rx_usable(&rx, 1002, 10, 0x2222), "ayar CRC'si uyusmuyor: kapali");
    w.open = false; km_win_rx_update(&rx, &w, 1003);
    CHECK(!km_win_rx_usable(&rx, 1003, 1, 0x1111), "kapali mesaji");
}

static void tur_testi(uint8_t sp, uint32_t rr, uint32_t qrs, const char *ad)
{
    ecg_gen_t g = { rr, qrs, 1000, false };
    const km_species_par_t *par = km_species_par(sp);
    start();
    CHECK(set_cfg(0, KM_SRC_OFF, sp, 0, 0, 0) == KM_CFG_OK, ad);
    uint32_t T = 12000u;
    run(1, T, &g, 0, 0, false);
    /* öğrenme bittikten sonraki atımlar */
    uint32_t t_learn = 3u * (60000u / par->hr_min) + 600u;
    int expect = 0, found = 0, max_err = 0, max_lat = 0, extra = 0;
    for (uint32_t tp = 500u; tp + qrs < T; tp += rr) if (tp > t_learn + rr) expect++;
    for (int i = 0; i < cap.n_r; i++) {
        if (cap.r[i].t_ms <= t_learn + rr) continue;
        int32_t k = (int32_t)((cap.r[i].t_ms + rr / 2u - 500u) / rr);
        int err = abs((int)cap.r[i].t_ms - (int)(500u + (uint32_t)k * rr));
        if (err > (int)qrs) { extra++; continue; }
        found++;
        if (err > max_err) max_err = err;
        int lat = (int)(cap.r_now[i] - cap.r[i].t_ms);
        if (lat > max_lat) max_lat = lat;
    }
    CHECK(found >= expect - 1 && found <= expect, "tum atimlar bulundu");
    CHECK(extra == 0, "yanlis R yok (T dalgasi, sebeke, taban kaymasi)");
    CHECK(max_err <= 2, "R zamani +-2 ms");
    CHECK(max_lat <= (int)par->qrs_ms, "karar gecikmesi <= tur QRS suresi");
    uint16_t hr = km_ecg_hr_bpm(&app.ecg), hr_true = (uint16_t)(60000u / rr);
    CHECK(abs((int)hr - (int)hr_true) * 100 <= 3 * (int)hr_true, "kalp hizi +-%3");
    CHECK(cap.st.ecg_q == KM_ECGQ_OK, "kalite OK");
    printf("    %s: beklenen %d, bulunan %d, zaman hatasi <= %d ms, karar gecikmesi <= %d ms, HR %u\n", ad, expect, found, max_err, max_lat, hr);
}
TEST(r_bulucu_fare)  { tur_testi(KM_SP_MOUSE, 100, 8, "fare 600/dk"); }
TEST(r_bulucu_sican) { tur_testi(KM_SP_RAT, 170, 12, "sican 353/dk"); }
TEST(r_bulucu_kedi)  { tur_testi(KM_SP_CAT, 333, 30, "kedi 180/dk"); }
TEST(r_bulucu_kopek) { tur_testi(KM_SP_DOG, 667, 50, "kopek 90/dk"); }

TEST(ayar_denetimi)
{
    start();
    CHECK(set_cfg(0, 9, KM_SP_RAT, 40, 30, 0) == KM_CFG_BAD_SRC, "gecersiz kaynak");
    CHECK(set_cfg(0, KM_SRC_ECG, 9, 40, 30, 0) == KM_CFG_BAD_SPECIES, "gecersiz tur");
    CHECK(set_cfg(0, KM_SRC_ECG, KM_SP_DOG, 30, 30, 0) == KM_CFG_DLY_SMALL && cap.ack.min_dly_ms == 70, "DLY karar gecikmesinden kisa (kopek 70 ms)");
    CHECK(set_cfg(0, KM_SRC_ECG, KM_SP_RAT, 1001, 30, 0) == KM_CFG_DLY_LARGE, "DLY cok buyuk");
    CHECK(set_cfg(0, KM_SRC_ECG, KM_SP_RAT, 40, 4, 0) == KM_CFG_WIN_RANGE, "WIN cok kisa");
    CHECK(set_cfg(0, KM_SRC_RESP, KM_SP_RAT, 40, 100, 0) == KM_CFG_NO_RSRC, "solunum kaynagi yokken RESP reddedilir");
    CHECK(app.gate.cfg.src == KM_SRC_OFF && !app.gate.have_cfg, "reddedilen ayar uygulanmaz");
    CHECK(set_cfg(0, KM_SRC_ECG, KM_SP_RAT, 40, 30, 0) == KM_CFG_OK && cap.ack.cfg_crc == app.gate.cfg_crc, "gecerli ayar");
}

/* pencere kenarlarını çıkar */
static int edges(uint32_t from, uint32_t *open_t, uint32_t *close_t, int max)
{
    int n = 0; bool prev = false;
    for (int i = 0; i < cap.n_win; i++) {
        if (cap.win_t[i] < from) { prev = cap.win[i].open; continue; }
        if (cap.win[i].open && !prev && n < max) open_t[n] = cap.win_t[i];
        if (!cap.win[i].open && prev && n < max) close_t[n++] = cap.win_t[i];
        prev = cap.win[i].open;
    }
    return n;
}

TEST(ekg_penceresi_zamanlamasi)
{
    ecg_gen_t g = { 100, 8, 1000, false };       /* fare 600/dk */
    static uint32_t o[300], c[300];
    start();
    CHECK(set_cfg(0, KM_SRC_ECG, KM_SP_MOUSE, 40, 30, 0) == KM_CFG_OK, "ayar");
    run(1, 6000, &g, 0, 0, false);
    int n = edges(2000, o, c, 300), bad_open = 0, bad_len = 0;
    for (int i = 0; i < n; i++) {
        uint32_t k = (o[i] - 500u) / 100u;                 /* pencere, hangi atımın ardından */
        int d_open = (int)o[i] - (int)(500u + k * 100u + 40u);
        if (abs(d_open) > 2) bad_open++;
        if (abs((int)(c[i] - o[i]) - 30) > 2) bad_len++;
    }
    CHECK(n >= 38 && n <= 41, "her atimda bir pencere (4 s'de ~40)");
    CHECK(bad_open == 0, "acilis = R + DLY +-2 ms");
    CHECK(bad_len == 0, "sure = WIN +-2 ms");
    /* açıkken mesaj aralığı ve kalan süre */
    int gap_bad = 0, rem_bad = 0;
    for (int i = 1; i < cap.n_win; i++) {
        if (cap.win_t[i] < 2000u) continue;
        if (cap.win[i].open && cap.win[i - 1].open) {
            if (cap.win_t[i] - cap.win_t[i - 1] > KM_WIN_TX_OPEN_MS) gap_bad++;
            if (cap.win[i].remain_ms >= cap.win[i - 1].remain_ms) rem_bad++;
        }
        if (cap.win[i].open && cap.win[i].remain_ms > 30u) rem_bad++;
    }
    CHECK(gap_bad == 0, "acikken en gec 2 ms'de bir mesaj");
    CHECK(rem_bad == 0, "kalan sure azalarak gider, WIN'i gecmez");
    CHECK(app.gate.late == 0, "gec kalan atim yok");
}

TEST(elektrot_kopunca_pencere_yok)
{
    ecg_gen_t g = { 170, 12, 1000, false };
    static uint32_t o[300], c[300];
    start();
    set_cfg(0, KM_SRC_ECG, KM_SP_RAT, 40, 30, 0);
    run(1, 4000, &g, 0, 0, false);
    CHECK(edges(2000, o, c, 300) > 5, "once pencereler var");
    run(4000, 6000, &g, 0, KM_LEAD_LA, false);
    int n = 0; for (int i = 0; i < cap.n_win; i++) if (cap.win_t[i] >= 4002u && cap.win_t[i] < 6000u && cap.win[i].open) n++;
    CHECK(n == 0, "kopukken hic acik mesaji yok");
    CHECK(cap.st.ecg_q == KM_ECGQ_LEADOFF && cap.st.lead_mask == KM_LEAD_LA && cap.st.hr_bpm == 0, "durum: LEADOFF-LA");
    run(6000, 9000, &g, 0, 0, false);
    CHECK(edges(7500, o, c, 300) > 5 && cap.st.ecg_q == KM_ECGQ_OK, "takilinca yeniden ogrenir, pencereler doner");
}

TEST(sinyal_yokken_pencere_yok)
{
    start();
    set_cfg(0, KM_SRC_ECG, KM_SP_RAT, 40, 30, 0);
    run(1, 5000, 0, 0, 0, false);                 /* düz hat */
    int n = 0; for (int i = 0; i < cap.n_win; i++) if (cap.win[i].open) n++;
    CHECK(n == 0 && cap.n_r == 0, "duz hatta R ve pencere yok");
    CHECK(cap.st.ecg_q == KM_ECGQ_NOSIG && cap.st.hr_bpm == 0, "durum: sinyal yok");
    CHECK(cap.n_win >= 200, "kapaliyken de 20 ms'de bir pencere mesaji (canlilik)");
}

TEST(solunum_fazi_ve_penceresi)
{
    static uint32_t o[100], c[100];
    start();
    km_app_set_rsrc(&app, KM_RSRC_PILLOW);
    CHECK(set_cfg(0, KM_SRC_RESP, KM_SP_RAT, 50, 200, KM_RPH_EXP) == KM_CFG_OK, "ayar");
    run(1, 20000, 0, 1500, 0, false);             /* 40 nefes/dk */
    CHECK(abs((int)cap.st.rr_x10 - 400) <= 12, "nefes hizi 40/dk +-%3");
    CHECK(cap.st.rsrc == KM_RSRC_PILLOW && cap.st.rbase >= 295 && cap.st.rbase <= 305, "taban basinci ~3,00 kPa");
    int n = edges(8000, o, c, 100), outside = 0, lenbad = 0;
    for (int i = 0; i < n; i++) {
        for (uint32_t t = o[i]; t < c[i]; t++) if (!resp_in_pause(t, 1500)) { outside++; break; }
        if (c[i] - o[i] > 202u) lenbad++;
    }
    CHECK(n >= 7 && n <= 8, "her nefeste bir pencere (12 s'de 8)");
    CHECK(outside == 0, "pencereler yalniz soluk verme sonu duzlugunde");
    CHECK(lenbad == 0, "pencere WIN'i gecmez");
    CHECK(!(cap.st.err & KM_ERR_RBASE), "taban basinci aralikta");
}

TEST(ekg_ve_solunum_birlikte)
{
    ecg_gen_t g = { 170, 12, 1000, false };
    static uint32_t o[300], c[300];
    start();
    km_app_set_rsrc(&app, KM_RSRC_PILLOW);
    CHECK(set_cfg(0, KM_SRC_BOTH, KM_SP_RAT, 40, 30, KM_RPH_EXP) == KM_CFG_OK, "ayar");
    run(1, 20000, &g, 1500, 0, false);
    int n = edges(8000, o, c, 300), outside = 0, bad_r = 0;
    for (int i = 0; i < n; i++) {
        if (!resp_in_pause(o[i], 1500)) outside++;
        uint32_t k = (o[i] - 500u) / 170u;
        if (abs((int)o[i] - (int)(500u + k * 170u + 40u)) > 2) bad_r++;
    }
    CHECK(n >= 16, "duzlukteki atimlarda pencere acilir");
    CHECK(outside == 0, "nefes hareketi sirasinda pencere yok");
    CHECK(bad_r == 0, "acilis yine R + DLY");
    CHECK(n < 60, "tum atimlarda degil (12 s'de ~70 atim)");
}

TEST(ters_kutuplu_ve_kucuk_r)
{
    ecg_gen_t g = { 333, 30, -300, false };       /* kedi, R aşağı yönlü, 0,3 mV */
    start();
    set_cfg(0, KM_SRC_ECG, KM_SP_CAT, 60, 50, 0);
    run(1, 10000, &g, 0, 0, false);
    int ok = 0;
    for (int i = 0; i < cap.n_r; i++) {
        if (cap.r[i].t_ms < 4000u) continue;
        int32_t k = (int32_t)((cap.r[i].t_ms + 166u - 500u) / 333u);
        if (abs((int)cap.r[i].t_ms - (int)(500u + (uint32_t)k * 333u)) <= 2 && cap.r[i].amp_uv < 0) ok++;
    }
    CHECK(ok >= 17 && ok <= 18, "asagi yonlu 0,3 mV R dalgalari bulunur (6 s'de 18)");
    CHECK(cap.st.ecg_q == KM_ECGQ_OK && abs((int)cap.st.hr_bpm - 180) <= 5, "kalite OK, HR 180");
}

TEST(erken_atim_pencereyi_kapatir)
{
    /* DLY + WIN bir sonraki atımı aşıyor: yeni R gelince açık pencere hemen kapanmalı */
    ecg_gen_t g = { 100, 8, 1000, false };
    static uint32_t o[300], c[300];
    start();
    set_cfg(0, KM_SRC_ECG, KM_SP_MOUSE, 60, 80, 0);      /* R+60 … R+140, oysa R-R 100 ms */
    run(1, 5000, &g, 0, 0, false);
    int n = edges(2000, o, c, 300), over = 0;
    for (int i = 0; i < n; i++) if (c[i] - o[i] > 46u) over++;      /* en geç sonraki R kararı (≈ R+100+3) anında kapanır */
    CHECK(n >= 28, "pencereler aciliyor");
    CHECK(over == 0, "pencere sonraki R'de kesilir (<= 46 ms)");
}

TEST(dalga_akisi_ve_canlilik)
{
    ecg_gen_t g = { 170, 12, 1000, true };
    start();
    km_app_set_rsrc(&app, KM_RSRC_PILLOW);
    run(1, 2001, &g, 1500, 0, true);
    CHECK(cap.n_wecg >= 48 && cap.n_wecg <= 50, "EKG dalgasi 25 cerceve/s (250 ornek/s)");
    CHECK(cap.n_wresp >= 9 && cap.n_wresp <= 10, "solunum dalgasi 5 cerceve/s (50 ornek/s)");
    CHECK(cap.n_st >= 19 && cap.st.wave_on == 1 && !(cap.st.err & KM_ERR_HB), "durum 10 Hz, canlilik var");
    int before = cap.n_wecg;
    for (uint32_t t = 2001; t < 3200; t++) { cap.now = t; km_app_ecg_sample(&app, t, ecg_uv(&g, t), 0); }   /* Kart A sustu */
    CHECK(cap.n_wecg - before <= 14, "Kart A susunca dalga 500 ms icinde kapanir");
    CHECK((cap.st.err & KM_ERR_HB) && cap.st.wave_on == 0, "durum: canlilik yok");
}

TEST(komutlar)
{
    uint8_t d[8]; km_cmd_msg_t m = { KM_CMD_VERSION, 0, 0, 0 };
    start();
    km_app_rx(&app, 10, KM_ID_CMD, d, km_pack_cmd(&m, d));
    CHECK(cap.n_ver == 1 && cap.cack.result == 0, "surum");
    m.cmd = KM_CMD_BOOT; m.magic = 0;
    km_app_rx(&app, 11, KM_ID_CMD, d, km_pack_cmd(&m, d));
    CHECK(!app.boot_req && cap.cack.result == 1, "sihirli sayi olmadan onyukleyiciye gecilmez");
    set_cfg(12, KM_SRC_ECG, KM_SP_RAT, 40, 30, 0);
    m.magic = KM_CMD_MAGIC;
    km_app_rx(&app, 13, KM_ID_CMD, d, km_pack_cmd(&m, d));
    CHECK(!app.boot_req, "tetikleme acikken onyukleyiciye gecilmez");
    set_cfg(14, KM_SRC_OFF, KM_SP_RAT, 0, 0, 0);
    km_app_rx(&app, 15, KM_ID_CMD, d, km_pack_cmd(&m, d));
    CHECK(app.boot_req, "tetikleme kapaliyken gecilir");
}

int main(void)
{
    RUN(protokol_gidis_donus);
    RUN(pencere_alicisi_hataya_guvenli);
    RUN(r_bulucu_fare);
    RUN(r_bulucu_sican);
    RUN(r_bulucu_kedi);
    RUN(r_bulucu_kopek);
    RUN(ayar_denetimi);
    RUN(ekg_penceresi_zamanlamasi);
    RUN(elektrot_kopunca_pencere_yok);
    RUN(sinyal_yokken_pencere_yok);
    RUN(solunum_fazi_ve_penceresi);
    RUN(ekg_ve_solunum_birlikte);
    RUN(ters_kutuplu_ve_kucuk_r);
    RUN(erken_atim_pencereyi_kapatir);
    RUN(dalga_akisi_ve_canlilik);
    RUN(komutlar);
    return REPORT();
}
