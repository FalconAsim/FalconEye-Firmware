/* Işınlama durum makinesi senaryo testleri (simülatörle) */
#include "check.h"
#include "hal_sim.h"
#include "error_codes.h"
#include "fe_config.h"
#include <math.h>

static exposure_sm_t sm;
static sim_t s;

static void setup(bool det_required)
{
    sm_config_t cfg = { .detector_ready_required = det_required,
                        .hs_dual_channel = true, .pedal_dual_channel = true,
                        .line_readback = true, .fluoro_enabled = true };
    sm_init(&sm, &cfg);
    sim_init(&s);
    sim_run(&s, &sm, 10);   /* cihaz açıldı, butonlar serbest */
}

static void set_rad(float kv, float ma, uint32_t ms, focus_t f)
{
    exposure_params_t p = { kv, ma, ms, f, MODE_RADIOGRAPHY };
    sm_set_params(&sm, &p);
}

static void release_all(void) { s.hs1 = s.hs2 = s.pedal = false; }

/* 1. kademe -> hazır olana kadar bekle */
static void prep_to_ready(void)
{
    s.hs1 = true;
    sim_run(&s, &sm, 4100);
}

TEST(normal_radyografi)
{
    setup(false);
    set_rad(70, 20, 100, FOCUS_LARGE);
    s.hs1 = true;
    sim_run(&s, &sm, 1000);
    CHECK(sm.state == ST_PREP, "1. kademede HAZIRLIK");
    CHECK(s.last_out.preheat && !s.last_out.ready_indicator, "PreHeat acik, hazir degil");
    sim_run(&s, &sm, 3100);
    CHECK(sm.state == ST_READY, "4 s sonunda HAZIR");
    CHECK(s.last_out.ready_indicator, "hazir gostergesi yanmali");
    CHECK(s.xray_on_ms == 0, "hazirlikta isinlama yok");

    s.hs2 = true;
    sim_run(&s, &sm, 300);
    CHECK(sm.state == ST_LOCKED, "isinlama sonrasi KILIT");
    CHECK(sm.last_record.reason == END_TIME_DONE, "normal bitis");
    CHECK(s.xray_on_ms >= 99 && s.xray_on_ms <= 101, "X-RAY ON ~100 ms");
    CHECK(fabsf(sm.last_record.mas_measured - 2.0f) < 0.15f, "olculen mAs ~2.0");
    CHECK(!s.xray_without_rl5, "RL5 olmadan X-RAY ON hic olmamali");

    sim_run(&s, &sm, 2000);
    CHECK(sm.state == ST_LOCKED, "buton basiliyken kilitte kalir (ard arda cekim yok)");
    release_all();
    sim_run(&s, &sm, 5);
    CHECK(sm.state == ST_IDLE, "tam birakinca BEKLE");
}

TEST(erken_ikinci_kademe)
{
    setup(false);
    set_rad(70, 20, 100, FOCUS_LARGE);
    s.hs1 = true;
    sim_run(&s, &sm, 1500);
    s.hs2 = true;
    sim_run(&s, &sm, 5000);
    CHECK(sm.state == ST_LOCKED, "hazir olmadan 2. kademe -> KILIT");
    CHECK(sm.last_reason == END_EARLY_STAGE2, "neden: erken 2. kademe");
    CHECK(s.xray_on_ms == 0, "isinlama olmamali");
    CHECK(!s.last_out.preheat, "PreHeat dusmeli");

    s.hs2 = false;   /* sadece 2. kademeyi birakmak yetmez */
    sim_run(&s, &sm, 5000);
    CHECK(sm.state == ST_LOCKED, "1. kademe basiliyken kilit kalkmamali");

    release_all();
    sim_run(&s, &sm, 5);
    CHECK(sm.state == ST_IDLE, "tam birakinca BEKLE");
    prep_to_ready();
    CHECK(sm.state == ST_READY, "bastan siralama ile HAZIR");
    s.hs2 = true;
    sim_run(&s, &sm, 200);
    CHECK(sm.last_record.reason == END_TIME_DONE && s.xray_on_ms > 0, "sonra normal cekim");
}

TEST(dogrudan_ikinci_kademe)
{
    setup(false);
    s.hs1 = true; s.hs2 = true;   /* buton tek seferde sonuna kadar */
    sim_run(&s, &sm, 6000);
    CHECK(sm.state == ST_LOCKED && s.xray_on_ms == 0, "dogrudan 2. kademe isinlamaz");
}

TEST(isinlamada_birakma)
{
    setup(false);
    set_rad(70, 10, 1000, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 315);
    s.hs2 = false;
    sim_run(&s, &sm, 50);
    CHECK(sm.last_record.reason == END_EARLY_RELEASE, "neden: buton erken birakildi");
    CHECK(sm.last_record.actual_ms < sm.last_record.set.time_ms, "gerceklesen < ayarlanan");
    CHECK(s.xray_on_ms >= 295 && s.xray_on_ms <= 305, "~300 ms'de kesildi");
    CHECK(!s.last_out.xray_on, "X-RAY ON kapali");
}

TEST(kapi_acilirsa)
{
    setup(false);
    set_rad(70, 10, 1000, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 200);
    s.door_ok = false;
    sim_run(&s, &sm, 2);
    CHECK(sm.last_record.reason == END_INTERLOCK, "kapi acilinca kesildi");
    CHECK(!s.last_out.xray_on, "X-RAY ON kapali");

    setup(false);
    s.door_ok = false;
    s.hs1 = true;
    sim_run(&s, &sm, 5000);
    CHECK(sm.state == ST_LOCKED && sm.last_reason == END_INTERLOCK, "kapi acikken hazirlik baslamaz");
    CHECK(s.xray_on_ms == 0 && !s.last_out.preheat, "PreHeat yok");
}

TEST(filaman_hazir_olmazsa)
{
    setup(false);
    s.filament_broken = true;
    s.hs1 = true;
    sim_run(&s, &sm, 8000);
    CHECK(sm.state == ST_LOCKED && sm.last_reason == END_FILAMENT_TIMEOUT, "filaman zaman asimi");
    CHECK(!s.last_out.ready_indicator, "hazir yanmamali");
}

TEST(kv_tolerans_hatasi)
{
    setup(false);
    set_rad(80, 20, 1000, FOCUS_LARGE);
    s.kv_gain_error = 0.30f;   /* kV %30 dusuk */
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 200);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_KV_LOW, "kV %70 -> 'kV dusuk' HATA");
    CHECK(s.xray_on_ms < 100, "hata hizla kesmeli (<100 ms)");
    release_all();
    sim_run(&s, &sm, 100);
    CHECK(sm.state == ST_FAULT, "birakmak hatayi kaldirmaz, onay gerekir");
    CHECK(sm_ack_fault(&sm), "onay kabul edildi");
    sim_run(&s, &sm, 5);
    CHECK(sm.state == ST_IDLE, "onay + birakilmis buton -> BEKLE");
}

TEST(jenerator_alarmi)
{
    setup(false);
    set_rad(70, 10, 1000, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 100);
    s.gen_alarm = true;
    sim_run(&s, &sm, 2);
    CHECK(sm.state == ST_FAULT && sm.last_record.reason == END_GEN_ALARM, "alarm -> HATA");
    CHECK(!s.last_out.xray_on, "X-RAY ON kapali");
}

TEST(yedek_zamanlayici)
{
    setup(false);
    set_rad(70, 10, 1000, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 100);
    s.backup_ok = false;
    sim_run(&s, &sm, 2);
    CHECK(sm.state == ST_FAULT && sm.last_record.reason == END_BACKUP_TIMER, "yedek zamanlayici -> HATA");
}

TEST(tup_korumasi_reddi)
{
    setup(false);
    set_rad(70, 20, 100, FOCUS_SMALL);   /* kucuk fokusta 20 mA */
    s.hs1 = true;
    sim_run(&s, &sm, 5000);
    CHECK(sm.state == ST_LOCKED && sm.last_reason == END_TUBE_REJECT, "tup korumasi reddetti");
    CHECK(sm.last_tube_result == TUBE_ERR_MA_FOCUS_MAX, "neden: fokus akim siniri");
    CHECK(!s.last_out.preheat && s.xray_on_ms == 0, "hazirlik baslamadi");
}

TEST(gorev_dongusu_beklemesi)
{
    setup(false);
    set_rad(60, 20, 1000, FOCUS_LARGE);   /* 1 s -> 40 s bekleme */
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 1100);
    release_all();
    sim_run(&s, &sm, 10);
    s.hs1 = true;
    sim_run(&s, &sm, 10);
    CHECK(sm.last_reason == END_TUBE_REJECT && sm.last_tube_result == TUBE_ERR_COOLDOWN,
          "40 s dolmadan yeni cekim reddedilir");
    release_all();
    sim_run(&s, &sm, 41000);
    prep_to_ready();
    CHECK(sm.state == ST_READY, "bekleme sonrasi serbest");
}

TEST(dedektor_hazir_bekleme)
{
    setup(true);
    s.detector_ready = false;
    s.hs1 = true;
    sim_run(&s, &sm, 6000);
    CHECK(sm.state == ST_PREP && !s.last_out.ready_indicator, "dedektor hazir degilken HAZIR yanmaz");
    s.detector_ready = true;
    sim_run(&s, &sm, 2);
    CHECK(sm.state == ST_READY, "dedektor hazir olunca HAZIR");

    setup(true);
    s.detector_ready = false;
    s.hs1 = true;
    sim_run(&s, &sm, 15000);
    CHECK(sm.state == ST_LOCKED && sm.last_reason == END_DETECTOR_TIMEOUT, "dedektor zaman asimi");
}

TEST(hazirlik_cok_uzun)
{
    setup(false);
    s.hs1 = true;
    sim_run(&s, &sm, 31000);
    CHECK(sm.state == ST_LOCKED && sm.last_reason == END_PREP_TIMEOUT, "30 s sonra hazirlik biter");
    CHECK(!s.last_out.preheat, "flaman korunur");
}

TEST(floroskopi_pedal)
{
    setup(false);
    exposure_params_t p = { 50, 10, 20000, FOCUS_SMALL, MODE_FLUORO };
    CHECK(sm_set_params(&sm, &p), "floroskopi ayari kabul");
    s.pedal = true;
    sim_run(&s, &sm, 4200);
    CHECK(sm.state == ST_EXPOSING, "pedal + hazir -> otomatik isinlama");
    CHECK(s.last_out.focus_small, "kucuk fokus secili");
    sim_run(&s, &sm, 5000);
    s.pedal = false;
    sim_run(&s, &sm, 5);
    CHECK(sm.last_record.reason == END_RELEASED, "pedal birakilinca durdu");
    CHECK(sm.fluoro_cumulative_ms >= 5000, "kumulatif sure sayildi");

    /* 20 s siniri */
    setup(false);
    sm_set_params(&sm, &p);
    s.pedal = true;
    sim_run(&s, &sm, 30000);
    CHECK(sm.last_record.reason == END_TIME_DONE, "20 s'de otomatik bitis");
    CHECK(s.xray_on_ms >= 19990 && s.xray_on_ms <= 20010, "X-RAY ON ~20 s");
}

TEST(floroskopi_5dk_uyarisi)
{
    setup(false);
    exposure_params_t p = { 40, 15, 20000, FOCUS_SMALL, MODE_FLUORO };
    sm_set_params(&sm, &p);
    for (int i = 0; i < 16 && !sm.fluoro_alert; i++) {
        s.pedal = true;
        sim_run(&s, &sm, 24100);
        s.pedal = false;
        sim_run(&s, &sm, 10);
        sim_run(&s, &sm, 410000);   /* 1:20 gorev dongusu beklemesi */
    }
    CHECK(sm.fluoro_alert, "5 dk kumulatif floroskopide uyari");
    CHECK(s.last_out.buzzer, "uyari sesi aktif");
    sm_fluoro_reset_cumulative(&sm);
    CHECK(!sm.fluoro_alert && sm.fluoro_cumulative_ms == 0, "operator sifirladi");
}

TEST(isinlamada_ayar_degismez)
{
    setup(false);
    set_rad(70, 20, 1000, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 100);
    CHECK(sm.state == ST_EXPOSING, "isinlama suruyor");
    exposure_params_t p = { 100, 30, 1000, FOCUS_LARGE, MODE_RADIOGRAPHY };
    CHECK(sm_set_params_ex(&sm, &p) == SM_SET_BUSY, "isinlamada ayar degismez");
    CHECK(sm.params.kv == 70.0f, "kV ayni kaldi");
}

/* Arayüz tanımı rev. 0.4 §4: hazırlıkta kV/mA/süre kabul, ısınma baştan */
TEST(hazirlikta_deger_degisimi)
{
    setup(false);
    set_rad(70, 20, 100, FOCUS_LARGE);
    prep_to_ready();
    CHECK(sm.state == ST_READY, "HAZIR");
    const uint32_t w0 = sm.warmup_count;
    exposure_params_t p = { 80, 25, 120, FOCUS_LARGE, MODE_RADIOGRAPHY };
    CHECK(sm_set_params_ex(&sm, &p) == SM_SET_OK, "hazirken kV/mA/sure kabul");
    CHECK(sm.params.kv == 80.0f && sm.params.ma == 25.0f && sm.params.time_ms == 120u, "yeni degerler");
    CHECK(sm.state == ST_PREP && sm.warmup_count == w0 + 1u, "hazir lambasi sondu, isinma bastan");
    sim_run(&s, &sm, 1);
    CHECK(!s.last_out.ready_indicator, "hazir gostergesi kapali");
    sim_run(&s, &sm, 3900);
    CHECK(sm.state == ST_PREP, "4 s dolmadan hazir olmaz");
    sim_run(&s, &sm, 200);
    CHECK(sm.state == ST_READY, "4 s sonra yeniden HAZIR");
    s.hs2 = true;
    sim_run(&s, &sm, 300);
    CHECK(sm.last_record.reason == END_TIME_DONE && sm.last_record.set.kv == 80.0f, "yeni degerle cekim");

    /* Aynı değerle SET ısınmayı yeniden başlatmaz */
    release_all(); sim_run(&s, &sm, 10);
    sim_run(&s, &sm, 40000);   /* soguma */
    prep_to_ready();
    CHECK(sm.state == ST_READY, "HAZIR (2)");
    CHECK(sm_set_params_ex(&sm, &p) == SM_SET_OK && sm.state == ST_READY, "degismeyen deger: hazir kalir");
}

TEST(hazirken_degisim_ve_ikinci_kademe)
{
    setup(false);
    set_rad(70, 20, 100, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true;                  /* operatör 2. kademeye basmak üzere */
    exposure_params_t p = { 75, 20, 100, FOCUS_LARGE, MODE_RADIOGRAPHY };
    sm_set_params_ex(&sm, &p);     /* tam o anda değer değişti */
    sim_run(&s, &sm, 50);
    CHECK(s.xray_on_ms == 0, "hazir lambasi gorulmeden cekim yok");
    CHECK(sm.state == ST_LOCKED && sm.last_reason == END_EARLY_STAGE2, "E101: butonu tamamen birakin");
    CHECK(fe_error_for(sm.last_reason, TUBE_OK)->code == 101, "kod E101");
}

TEST(hazirlikta_fokus_ve_mod_degismez)
{
    setup(false);
    set_rad(60, 10, 100, FOCUS_LARGE);
    prep_to_ready();
    exposure_params_t p = { 60, 10, 100, FOCUS_SMALL, MODE_RADIOGRAPHY };
    CHECK(sm_set_params_ex(&sm, &p) == SM_SET_FOCUS_LOCKED, "hazirlikta fokus degismez");
    exposure_params_t q = { 50, 10, 5000, FOCUS_LARGE, MODE_FLUORO };
    CHECK(sm_set_params_ex(&sm, &q) == SM_SET_FOCUS_LOCKED, "hazirlikta mod degismez");
    CHECK(sm.state == ST_READY && sm.params.focus == FOCUS_LARGE, "hazir kaldi, deger ayni");
    CHECK(fe_error_by_code(213) != NULL, "E213 tabloda");
}

TEST(degisimler_hazirlik_suresini_uzatmaz)
{
    setup(false);
    set_rad(70, 20, 100, FOCUS_LARGE);
    s.hs1 = true;
    for (int i = 0; i < 9; i++) {           /* her 3,5 s'de bir değer değişir */
        sim_run(&s, &sm, 3500);
        exposure_params_t p = { (float)(70 + i + 1), 20, 100, FOCUS_LARGE, MODE_RADIOGRAPHY };
        sm_set_params_ex(&sm, &p);
    }
    sim_run(&s, &sm, 100);
    CHECK(sm.state == ST_LOCKED && sm.last_reason == END_PREP_TIMEOUT,
          "toplam 30 s'yi asinca hazirlik biter (E102)");
}

/* Arayüz tanımı rev. 0.4 §5: PC modunda bağlantı zorunlu */
TEST(pc_modu_baglanti_yokken_hazirlik_baslamaz)
{
    setup(false);
    CHECK(sm_set_pc_link(&sm, true, false), "PC modu");
    s.pc_link_ok = false;
    set_rad(70, 20, 100, FOCUS_LARGE);
    s.hs1 = true;
    sim_run(&s, &sm, 100);
    CHECK(sm.state == ST_LOCKED && sm.last_reason == END_LINK_DOWN, "baglanti yok: hazirlik yok (E701)");
    CHECK(!s.last_out.preheat, "PreHeat yok");
    CHECK(fe_error_for(END_LINK_DOWN, TUBE_OK)->code == 701, "kod E701");
    release_all(); sim_run(&s, &sm, 10);
    CHECK(sm_set_pc_link(&sm, false, false), "Manuel moda gecis (BEKLE'de)");
    s.hs1 = true;
    sim_run(&s, &sm, 4100);
    CHECK(sm.state == ST_READY, "Manuel modda baglanti gerekmez");
    CHECK(!sm_set_pc_link(&sm, true, false), "hazirlikta mod degistirilemez");
}

TEST(pc_modu_hazirlikta_kopma)
{
    setup(false);
    sm_set_pc_link(&sm, true, false);
    set_rad(70, 20, 100, FOCUS_LARGE);
    prep_to_ready();
    s.pc_link_ok = false;
    sim_run(&s, &sm, 2);
    CHECK(sm.state == ST_LOCKED && sm.last_reason == END_LINK_DOWN, "hazirlik iptal");
    CHECK(!s.last_out.preheat, "PreHeat kapandi");
    s.pc_link_ok = true;
    s.hs2 = true;
    sim_run(&s, &sm, 300);
    CHECK(s.xray_on_ms == 0, "baglanti donse de tam birakmadan cekim yok");
}

TEST(pc_modu_isinlamada_kopma_keser)
{
    setup(false);
    sm_set_pc_link(&sm, true, false);
    set_rad(60, 10, 2000, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 500);
    CHECK(sm.state == ST_EXPOSING, "isinlama suruyor");
    s.pc_link_ok = false;
    const uint32_t before = s.xray_on_ms;
    sim_run(&s, &sm, 1);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_LINK_LOST, "isinlama kesildi, HATA (E702)");
    CHECK(!s.last_out.xray_on && !s.last_out.preheat, "X-RAY ON ve PreHeat ayni adimda kapali");
    CHECK(s.xray_on_ms == before, "kesmeden sonra isinlama yok");
    CHECK(sm.last_record.cut && sm.last_record.actual_ms < 2000u, "kayitta CUT, gerceklesen sure");
    CHECK(fe_error_for(END_LINK_LOST, TUBE_OK)->code == 702 &&
          fe_error_for(END_LINK_LOST, TUBE_OK)->severity == SEV_ERROR, "E702 onay gerektirir");
    s.pc_link_ok = true;
    sim_run(&s, &sm, 100);
    CHECK(sm.state == ST_FAULT, "baglanti donunce kendiliginden kalkmaz");
    CHECK(sm_ack_fault(&sm) && sm.state == ST_LOCKED, "panelden onay");
}

TEST(pc_modu_rs232_kopma)
{
    setup(false);
    sm_set_pc_link(&sm, true, true);
    set_rad(60, 10, 2000, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true; sim_run(&s, &sm, 300);
    s.pc_link_ok = false; sim_run(&s, &sm, 1);
    CHECK(sm.last_reason == END_LINK_LOST_SERIAL && fe_error_for(sm.last_reason, TUBE_OK)->code == 703,
          "RS-232 kopmasi E703");
}

TEST(fokus_rolesi_yuk_altinda_anahtarlanmaz)
{
    setup(false);
    sm.cfg.focus_relay_readback = true;
    CHECK(sm.params.focus == FOCUS_SMALL && sm.params.mode == MODE_RADIOGRAPHY,
          "acilis: kucuk fokus, radyografi (floroskopi degil)");
    CHECK(s.last_out.focus_small && s.focus_fb, "acilista kucuk fokus rolesi cekili");
    set_rad(70, 20, 100, FOCUS_LARGE);     /* büyük fokusa geç */
    sim_run(&s, &sm, 100);
    CHECK(!s.last_out.focus_small && !s.focus_fb, "beklemede buyuk fokus");
    set_rad(60, 10, 100, FOCUS_SMALL);
    s.hs1 = true;                     /* fokus degisir degismez 1. kademe */
    sim_run(&s, &sm, 20);
    CHECK(sm.state == ST_IDLE && !s.last_out.preheat, "role oturmadan PreHeat yok");
    sim_run(&s, &sm, 4200);
    CHECK(sm.state == ST_READY && s.last_out.focus_small && s.focus_fb, "kucuk fokusta HAZIR");
    s.hs2 = true; sim_run(&s, &sm, 300);
    CHECK(sm.last_record.reason == END_TIME_DONE, "cekim tamam");
    CHECK(!s.hot_switch, "role filaman surulurken hic hareket etmedi");
    release_all(); sim_run(&s, &sm, 10);
    set_rad(70, 20, 100, FOCUS_LARGE);
    sim_run(&s, &sm, 100);
    CHECK(!s.last_out.focus_small && !s.focus_fb, "beklemede buyuk fokusa dondu");
}

TEST(fokus_rolesi_takili)
{
    setup(false);
    sm.cfg.focus_relay_readback = true;
    set_rad(70, 20, 100, FOCUS_LARGE);
    sim_run(&s, &sm, 100);            /* röle büyük fokusta */
    s.focus_stuck = true;             /* röle büyük fokusta takılı */
    set_rad(60, 10, 100, FOCUS_SMALL);
    s.hs1 = true;
    sim_run(&s, &sm, 300);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_FOCUS_RELAY, "E406, hazirlik baslamadi");
    CHECK(s.xray_on_ms == 0 && !s.last_out.preheat, "isinlama yok");
    CHECK(fe_error_for(END_FOCUS_RELAY, TUBE_OK)->code == 406 &&
          fe_error_for(END_FOCUS_RELAY, TUBE_OK)->severity == SEV_SERVICE, "E406 servis");
}

TEST(fokus_rolesi_isinlamada_birakti)
{
    setup(false);
    sm.cfg.focus_relay_readback = true;
    set_rad(60, 10, 2000, FOCUS_SMALL);
    prep_to_ready(); sim_run(&s, &sm, 100);
    s.hs2 = true; sim_run(&s, &sm, 200);
    CHECK(sm.state == ST_EXPOSING, "isinlama");
    s.focus_fb = false; s.focus_stuck = true;   /* röle düştü (büyük fokusa geçti) */
    sim_run(&s, &sm, FE_FOCUS_MISMATCH_MS + 2);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_FOCUS_RELAY, "isinlama kesildi E406");
}

TEST(referans_cikisi_geri_okuma)
{
    setup(false);
    sm.cfg.ref_readback = true;
    set_rad(70, 20, 100, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true; sim_run(&s, &sm, 300);
    CHECK(sm.last_record.reason == END_TIME_DONE, "saglam DAC: normal cekim");
    release_all(); sim_run(&s, &sm, 10);
    s.dac_kv_error = 30.0f;            /* beklemede DAC 30 kV'ta takılı */
    sim_run(&s, &sm, 30);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_REF_READBACK, "beklemede yuksek referans: E407");
    CHECK(fe_error_for(END_REF_READBACK, TUBE_OK)->code == 407, "kod E407");
}

TEST(referans_isinlamada_sapti)
{
    setup(false);
    sm.cfg.ref_readback = true;
    set_rad(70, 20, 2000, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true; sim_run(&s, &sm, 300);
    s.dac_ma_error = 5.0f;             /* mA referansı 5 mA yüksek */
    sim_run(&s, &sm, FE_REF_MISMATCH_MS + 2);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_REF_READBACK, "isinlama kesildi E407");
    CHECK(!s.last_out.xray_on, "X-RAY ON kapali");
}

TEST(tek_fokuslu_insertte_acilis)
{
    setup(false);
    tube_select(&sm.tube, TUBE_INSERT_D067SB);
    sm_load_boot_defaults(&sm);
    CHECK(sm.params.focus == FOCUS_LARGE && sm.params.mode == MODE_RADIOGRAPHY, "tek fokus: buyuk (tek) fokus");
    CHECK(tube_check(&sm.tube, &sm.params) == TUBE_OK, "acilis degerleri D-067SB sinirlari icinde");
    tube_select(&sm.tube, TUBE_INSERT_DF161SBR);
    sm_load_boot_defaults(&sm);
    CHECK(tube_check(&sm.tube, &sm.params) == TUBE_OK, "acilis degerleri DF-161SBR kucuk fokusta uygun");
}

TEST(kalibrasyon_yok_uyarisi)
{
    const fe_error_t *w = fe_warning_no_calibration();
    CHECK(w && w->prefix == 'W' && w->code == 602 && w->severity == SEV_WARNING, "W602");
}

TEST(manuel_modda_kopma_etkisiz)
{
    setup(false);
    sm_set_pc_link(&sm, false, false);
    set_rad(60, 10, 500, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 100);
    s.pc_link_ok = false;
    sim_run(&s, &sm, 600);
    CHECK(sm.last_record.reason == END_TIME_DONE && !sm.last_record.cut, "Manuel: cekim normal biter");
}

TEST(acilista_takili_buton)
{
    sm_config_t cfg = { .hs_dual_channel = true, .pedal_dual_channel = true };
    sm_init(&sm, &cfg);
    sim_init(&s);
    s.hs1 = true;                     /* cihaz acilirken buton basili */
    sim_run(&s, &sm, 6000);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_BUTTON_STUCK, "acilista takili buton -> HATA");
    CHECK(!s.last_out.preheat && s.xray_on_ms == 0, "hazirlik ve isinlama yok");
}

TEST(isinlama_sonrasi_takili_buton)
{
    setup(false);
    set_rad(70, 10, 100, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 12000);          /* buton birakilmiyor */
    CHECK(s.xray_on_ms >= 99 && s.xray_on_ms <= 101, "yalnizca bir kez, ayarli sure kadar isinladi");
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_BUTTON_STUCK, "10 s sonra 'buton takili' HATA");
    CHECK(sm_ack_fault(&sm), "onay");
    sim_run(&s, &sm, 11000);
    CHECK(sm.state == ST_FAULT, "buton hala takiliysa tekrar HATA, isinlama yok");
    CHECK(s.xray_on_ms <= 101, "ek isinlama olmadi");
}

TEST(bekleme_sirasinda_kisa_devre)
{
    setup(false);
    s.hs2_no_short = true;            /* 2. kademe hatti kisa devre, operator basmiyor */
    sim_run(&s, &sm, 100);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_INPUT_CHANNEL, "kisa devre -> kablo arizasi HATA");
    s.hs1 = true;
    sim_run(&s, &sm, 6000);
    CHECK(s.xray_on_ms == 0 && !s.last_out.preheat, "arizali butonla calismaz");
}

TEST(isinlamada_kisa_devre)
{
    setup(false);
    set_rad(70, 10, 2000, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 300);
    s.hs2_no_short = true;            /* kablo kisa devre oldu */
    s.hs2 = false;                    /* operator birakti: NO kisa devre yuzunden kapali gorunuyor */
    sim_run(&s, &sm, 2);
    CHECK(!s.last_out.xray_on, "NC kanali 'birakildi' dedigi an isinlama durdu");
    CHECK(s.xray_on_ms < 300, "ayarli 2 s'ye ulasmadi");
    sim_run(&s, &sm, 30);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_INPUT_CHANNEL, "ardindan kablo arizasi HATA");
}

TEST(kopuk_nc_kablosu)
{
    setup(false);
    s.hs2_nc_open = true;             /* NC teli kopuk */
    sim_run(&s, &sm, 100);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_INPUT_CHANNEL, "kopuk kablo -> HATA");
}

TEST(pedal_kanal_arizasi)
{
    setup(false);
    exposure_params_t p = { 50, 10, 20000, FOCUS_SMALL, MODE_FLUORO };
    sm_set_params(&sm, &p);
    s.pedal = true;
    sim_run(&s, &sm, 5000);
    CHECK(sm.state == ST_EXPOSING, "floroskopi basladi");
    s.pedal_b_short = true;
    s.pedal = false;                  /* pedal birakildi ama B kanali kisa devre */
    sim_run(&s, &sm, 2);
    CHECK(!s.last_out.xray_on, "A kanali birakildi dedigi an durdu");
    sim_run(&s, &sm, 30);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_INPUT_CHANNEL, "pedal kanal arizasi HATA");
}

TEST(acil_stop_isinlamada)
{
    setup(false);
    set_rad(70, 10, 2000, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 300);
    s.estop_ok = false;               /* acil stop basildi */
    sim_run(&s, &sm, 1);
    CHECK(!s.last_out.xray_on && !s.last_out.preheat, "acil stop: isinlama ve PreHeat aninda kesildi");
    CHECK(sm.state == ST_FAULT && sm.last_record.reason == END_ESTOP, "neden: acil stop");
    release_all();
    sim_run(&s, &sm, 100);
    CHECK(!sm_ack_fault(&sm) || sm.state != ST_IDLE, "basiliyken onay calismayi baslatmaz");
    sim_run(&s, &sm, 10);
    CHECK(sm.state == ST_FAULT, "acil stop basili oldukca HATA");
    s.estop_ok = true;                /* acil stop geri cevrildi */
    sim_run(&s, &sm, 1000);
    CHECK(sm.state == ST_FAULT, "birakmak tek basina yetmez, elle reset gerekir");
    CHECK(sm_ack_fault(&sm), "elle reset");
    sim_run(&s, &sm, 10);
    CHECK(sm.state == ST_IDLE, "reset sonrasi BEKLE");
}

TEST(acil_stop_kanal_arizasi)
{
    setup(false);
    s.estop_ch2_short = true;         /* 2. kanal kisa devre */
    s.estop_ok = false;               /* basildi: yalniz 1. kanal acildi */
    sim_run(&s, &sm, 5);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_ESTOP, "tek kanal acilsa da durdurur");
    s.estop_ok = true;
    sm_ack_fault(&sm);
    s.estop_ok = false;
    sim_run(&s, &sm, 200);
    CHECK(sm.state == ST_FAULT, "HATA durumunda kalir");
    s.estop_ok = true;
    s.estop_ch2_short = false;
    s.estop_ok = true;
    /* yalnizca kanal tutarsizligi: 1. kanal acik, 2. kanal kapali, isinlama yokken */
    setup(false);
    s.estop_ch2_short = true;
    s.estop_ok = false;
    sim_run(&s, &sm, 150);
    CHECK(sm.state == ST_FAULT, "kanallar tutarsiz -> HATA");
}

TEST(xray_hatti_gnd_kisa_devre_beklemede)
{
    setup(false);
    s.xray_line_short = true;          /* kablo yalitimi bozuldu, hat GND'ye degdi */
    sim_run(&s, &sm, 20);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_LINE_FAULT, "beklemede hat arizasi yakalandi");
    CHECK(!s.hv_from_short, "Inter-Lock kapali oldugu icin jenerator isinlamadi");
    s.hs1 = true;
    sim_run(&s, &sm, 6000);
    CHECK(!s.last_out.preheat && !s.last_out.gen_interlock, "arizada PreHeat ve Inter-Lock verilmez");
    CHECK(!s.hv_from_short && s.xray_on_ms == 0, "hic isinlama yok");
}

TEST(xray_hatti_kisa_devre_hazirlikta)
{
    setup(false);
    s.hs1 = true;
    sim_run(&s, &sm, 2000);            /* hazirlik: Inter-Lock kapali */
    s.xray_line_short = true;
    sim_run(&s, &sm, 20);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_LINE_FAULT, "hazirlikta kisa devre -> HATA");
    sim_run(&s, &sm, 100);
    CHECK(!s.last_out.gen_interlock, "Inter-Lock acildi, jenerator durduruldu");
    CHECK(s.kv < 1.0f, "kV sondu");
}

TEST(xray_hatti_kopuk)
{
    setup(false);
    set_rad(70, 10, 500, FOCUS_LARGE);
    s.xray_line_open = true;
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 100);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_LINE_FAULT, "komut var, hat pasif -> HATA");
}

TEST(normal_cekimde_hat_hatasi_yok)
{
    setup(false);
    set_rad(70, 20, 300, FOCUS_LARGE);
    prep_to_ready();
    CHECK(s.last_out.gen_interlock, "hazirlikta Inter-Lock kapali");
    s.hs2 = true;
    sim_run(&s, &sm, 500);
    CHECK(sm.last_record.reason == END_TIME_DONE, "normal bitis, yanlis hat alarmi yok");
    release_all();
    sim_run(&s, &sm, 10);
    CHECK(!s.last_out.gen_interlock, "bekleme durumunda Inter-Lock acik");
}

TEST(tup_termigi_ayri_neden)
{
    setup(false);
    s.thermal_ok = false;
    s.hs1 = true;
    sim_run(&s, &sm, 100);
    CHECK(sm.last_reason == END_TUBE_THERMAL, "termik ayri neden olarak raporlanir");
}

TEST(kv_yuzde85_siniri)
{
    setup(false);
    set_rad(80, 20, 1000, FOCUS_LARGE);
    s.kv_gain_error = 0.10f;            /* %90: sinir icinde */
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 1200);
    CHECK(sm.last_record.reason == END_TIME_DONE, "%90 kV kabul edilir");

    setup(false);
    set_rad(80, 20, 1000, FOCUS_LARGE);
    s.kv_gain_error = 0.18f;            /* %82: sinirin altinda */
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 300);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_KV_LOW, "%82 kV -> E302");
    CHECK(s.xray_on_ms < 100, "hizla kesildi");
}

TEST(kv_yuksek)
{
    setup(false);
    set_rad(80, 20, 1000, FOCUS_LARGE);
    s.kv_gain_error = -0.15f;           /* %115 */
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 300);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_KV_HIGH, "%115 kV -> E306");
}

TEST(isinlama_suresi_olcumu)
{
    setup(false);
    set_rad(70, 20, 100, FOCUS_LARGE);
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 300);
    CHECK(sm.state == ST_LOCKED && sm.last_record.reason == END_TIME_DONE, "normal cekim");
    CHECK(sm.last_record.irr_ms >= 95 && sm.last_record.irr_ms <= 105,
          "olculen isinlama suresi ayarlanana yakin (100 ms)");
}

TEST(isinlama_suresi_sapmasi)
{
    setup(false);
    set_rad(70, 20, 10, FOCUS_LARGE);   /* 10 ms kisa cekim */
    s.rise_tau_ms = 8.0f;               /* yavas jenerator: kV gec yukseliyor */
    prep_to_ready();
    s.hs2 = true;
    sim_run(&s, &sm, 300);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_TIME_DEV, "sure sapmasi -> E307");
    CHECK(sm.last_record.irr_ms < 9, "olculen sure ayarlanandan kisa");
    CHECK(fe_error_for(END_TIME_DEV, TUBE_OK)->code == 307, "kod E307");
}

TEST(erken_birakma_kodu)
{
    CHECK(fe_error_for(END_EARLY_RELEASE, TUBE_OK)->code == 106, "erken birakma E106");
    CHECK(fe_error_for(END_EARLY_STAGE2, TUBE_OK)->code == 101, "flaman isinmadan 2. kademe E101");
    CHECK(fe_error_for(END_RELEASED, TUBE_OK) == NULL, "floroskopide pedal birakma normal");
}

TEST(floroskopi_opsiyonu_kapali)
{
    sm_config_t cfg = { .hs_dual_channel = true, .pedal_dual_channel = true };
    sm_init(&sm, &cfg);
    exposure_params_t p = { 50, 10, 10000, FOCUS_SMALL, MODE_FLUORO };
    CHECK(!sm_set_params(&sm, &p), "opsiyon kapaliyken floroskopi ayari reddedilir");
}

TEST(guc_siniri_ayari)
{
    sm_config_t cfg = { .hs_dual_channel = true, .pedal_dual_channel = true, .gen_power_max_w = 5000.0f };
    sm_init(&sm, &cfg);
    exposure_params_t p = { 70, 50, 100, FOCUS_LARGE, MODE_RADIOGRAPHY };   /* 3.5 kW */
    CHECK(tube_check(&sm.tube, &p) == TUBE_OK, "5 kW jeneratorde 3.5 kW kabul");
    sm_config_t cfg2 = { .hs_dual_channel = true };
    sm_init(&sm, &cfg2);
    CHECK(tube_check(&sm.tube, &p) == TUBE_ERR_GEN_POWER, "varsayilan 3.2 kW'ta reddedilir");
}

TEST(erken_kademe_sonrasi_basili_tutma)
{
    setup(false);
    s.hs1 = true;
    sim_run(&s, &sm, 1000);
    s.hs2 = true;
    sim_run(&s, &sm, 15000);          /* E101 sonrasi operator 15 s basili tutuyor */
    CHECK(sm.state == ST_LOCKED && sm.last_reason == END_EARLY_STAGE2,
          "15 s: yaniltici 'buton takili' servis hatasi verilmez, E101 kalir");
    sim_run(&s, &sm, 50000);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_BUTTON_STUCK, "60 s sonra takili sayilir");
}

int main(void)
{
    printf("Isinlama durum makinesi testleri\n");
    RUN(normal_radyografi);
    RUN(erken_ikinci_kademe);
    RUN(dogrudan_ikinci_kademe);
    RUN(isinlamada_birakma);
    RUN(kapi_acilirsa);
    RUN(filaman_hazir_olmazsa);
    RUN(kv_tolerans_hatasi);
    RUN(jenerator_alarmi);
    RUN(yedek_zamanlayici);
    RUN(tup_korumasi_reddi);
    RUN(gorev_dongusu_beklemesi);
    RUN(dedektor_hazir_bekleme);
    RUN(hazirlik_cok_uzun);
    RUN(floroskopi_pedal);
    RUN(floroskopi_5dk_uyarisi);
    RUN(isinlamada_ayar_degismez);
    RUN(acilista_takili_buton);
    RUN(isinlama_sonrasi_takili_buton);
    RUN(bekleme_sirasinda_kisa_devre);
    RUN(isinlamada_kisa_devre);
    RUN(kopuk_nc_kablosu);
    RUN(pedal_kanal_arizasi);
    RUN(acil_stop_isinlamada);
    RUN(acil_stop_kanal_arizasi);
    RUN(xray_hatti_gnd_kisa_devre_beklemede);
    RUN(xray_hatti_kisa_devre_hazirlikta);
    RUN(xray_hatti_kopuk);
    RUN(normal_cekimde_hat_hatasi_yok);
    RUN(tup_termigi_ayri_neden);
    RUN(kv_yuzde85_siniri);
    RUN(kv_yuksek);
    RUN(isinlama_suresi_olcumu);
    RUN(isinlama_suresi_sapmasi);
    RUN(erken_birakma_kodu);
    RUN(floroskopi_opsiyonu_kapali);
    RUN(guc_siniri_ayari);
    RUN(erken_kademe_sonrasi_basili_tutma);
    RUN(hazirlikta_deger_degisimi);
    RUN(hazirken_degisim_ve_ikinci_kademe);
    RUN(hazirlikta_fokus_ve_mod_degismez);
    RUN(degisimler_hazirlik_suresini_uzatmaz);
    RUN(pc_modu_baglanti_yokken_hazirlik_baslamaz);
    RUN(pc_modu_hazirlikta_kopma);
    RUN(pc_modu_isinlamada_kopma_keser);
    RUN(pc_modu_rs232_kopma);
    RUN(manuel_modda_kopma_etkisiz);
    RUN(fokus_rolesi_yuk_altinda_anahtarlanmaz);
    RUN(fokus_rolesi_takili);
    RUN(fokus_rolesi_isinlamada_birakti);
    RUN(referans_cikisi_geri_okuma);
    RUN(referans_isinlamada_sapti);
    RUN(tek_fokuslu_insertte_acilis);
    RUN(kalibrasyon_yok_uyarisi);
    return REPORT();
}
