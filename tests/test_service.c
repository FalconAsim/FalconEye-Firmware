/* Servis menüsü: floroskopi / DAP metre opsiyonu, insert seçimi, tek tuşla mod geçişi */
#include "check.h"
#include "hal_sim.h"
#include "service.h"
#include "fe_config.h"
#include <string.h>

static uint8_t fram[32768];
static bool fail_write = false;
static bool f_read(uint32_t a, void *b, size_t n) { memcpy(b, fram + a, n); return true; }
static bool f_write(uint32_t a, const void *b, size_t n)
{
    if (fail_write) return false;
    memcpy(fram + a, b, n);
    return true;
}
static const nv_ops_t ops = { f_read, f_write };

static exposure_sm_t sm;
static sim_t s;
static nvstore_t nv;
static nv_data_t data;
static svc_t svc;
#define PIN 4711u

static void boot(void)
{
    sm_config_t cfg = { .hs_dual_channel = true, .pedal_dual_channel = true, .line_readback = true };
    sm_init(&sm, &cfg);
    nv_load(&nv, &ops, &data);
    svc_apply_boot(&sm, &data.settings);
    svc_init(&svc, &sm, &nv, &data, PIN);
    sim_init(&s);
    sim_run(&s, &sm, 10);
}

static void fresh(void)
{
    memset(fram, 0xFF, sizeof fram);
    fail_write = false;
    boot();
}

TEST(varsayilan_opsiyonlar)
{
    fresh();
    CHECK(!sm.cfg.fluoro_enabled && !sm.cfg.dap_meter_enabled, "varsayilan: floroskopi ve DAP metre kapali");
    CHECK(sm.tube.insert == TUBE_INSERT_DF161SBR, "varsayilan insert DF-161SBR");
    CHECK(!sm_fluoro_available(&sm), "floroskopi kullanilamaz");
    CHECK(sm_toggle_mode(&sm) == SM_SET_FLUORO_DISABLED && sm.params.mode == MODE_RADIOGRAPHY,
          "opsiyon kapaliyken tus etkisiz");
}

TEST(pin_ve_kilit)
{
    fresh();
    CHECK(svc_set_fluoro(&svc, true) == SVC_ERR_NOT_ACTIVE, "servis moduna girmeden ayar yok");
    CHECK(svc_enter(&svc, 1) == SVC_ERR_BAD_PIN, "yanlis PIN 1");
    CHECK(svc_enter(&svc, 2) == SVC_ERR_BAD_PIN, "yanlis PIN 2");
    CHECK(svc_enter(&svc, 3) == SVC_ERR_BAD_PIN, "yanlis PIN 3");
    CHECK(svc_enter(&svc, PIN) == SVC_ERR_LOCKED_OUT, "3 yanlistan sonra dogru PIN de 60 s reddedilir");
    svc_tick(&svc, SVC_LOCKOUT_MS);
    CHECK(svc_enter(&svc, PIN) == SVC_OK && svc.active, "kilit bitti, giris");
    svc_tick(&svc, SVC_IDLE_TIMEOUT_MS);
    CHECK(!svc.active, "10 dk islemsiz: servis modu kapandi");
}

TEST(floroskopi_ac_kapat)
{
    fresh();
    svc_enter(&svc, PIN);
    CHECK(svc_set_fluoro(&svc, true) == SVC_OK && sm_fluoro_available(&sm), "floroskopi acildi");
    CHECK(sm_toggle_mode(&sm) == SM_SET_OK && sm.params.mode == MODE_FLUORO && sm.params.focus == FOCUS_SMALL,
          "tek tus: floroskopi, kucuk fokus");
    CHECK(sm.params.kv == 50.0f && sm.params.ma == 2.0f && sm.params.time_ms == FE_FLUORO_TIME_MAX_MS,
          "ilk floroskopi degerleri");
    CHECK(tube_check(&sm.tube, &sm.params) == TUBE_OK, "floroskopi baslangic degerleri sinir icinde");
    CHECK(sm_toggle_mode(&sm) == SM_SET_OK && sm.params.mode == MODE_RADIOGRAPHY && sm.params.kv == 60.0f,
          "tek tus: radyografiye donus, onceki degerler");

    /* kalıcı: yeniden açılışta korunur, açılış yine radyografi */
    boot();
    CHECK(sm.cfg.fluoro_enabled && sm.params.mode == MODE_RADIOGRAPHY, "yeniden acilis: opsiyon acik, mod RAD");

    /* floroskopi modundayken kapatılırsa radyografiye döner */
    svc_enter(&svc, PIN);
    sm_toggle_mode(&sm);
    CHECK(svc_set_fluoro(&svc, false) == SVC_OK, "floroskopi kapatildi");
    CHECK(sm.params.mode == MODE_RADIOGRAPHY && !sm_fluoro_available(&sm), "kapatinca RAD'a dondu");
    boot();
    CHECK(!sm.cfg.fluoro_enabled, "kapali kaldi");
}

TEST(dap_metre)
{
    fresh();
    svc_enter(&svc, PIN);
    CHECK(svc_set_dap_meter(&svc, true) == SVC_OK && sm.cfg.dap_meter_enabled, "DAP metre acildi");
    boot();
    CHECK(sm.cfg.dap_meter_enabled, "yeniden acilista DAP metre acik");
    svc_enter(&svc, PIN);
    CHECK(svc_set_dap_meter(&svc, false) == SVC_OK && !sm.cfg.dap_meter_enabled, "veteriner: DAP metre kapali");
}

TEST(insert_secimi)
{
    fresh();
    svc_enter(&svc, PIN);
    svc_set_fluoro(&svc, true);
    sm.tube.heat_j = 20000.0f;
    sm.tube.cooldown_ms = 30000u;
    CHECK(svc_set_insert(&svc, TUBE_INSERT_D067SB) == SVC_OK && sm.tube.insert == TUBE_INSERT_D067SB, "D-067SB secildi");
    CHECK(sm.tube.heat_j == 20000.0f && sm.tube.cooldown_ms == 30000u,
          "insert degisimi isi modelini ve soguma beklemesini silmez");
    CHECK(sm.params.focus == FOCUS_LARGE && tube_check(&sm.tube, &sm.params) != TUBE_ERR_NO_SMALL_FOCUS,
          "tek fokuslu insert: acilis degerleri buyuk fokus");
    CHECK(!sm_fluoro_available(&sm) && sm_toggle_mode(&sm) == SM_SET_FLUORO_DISABLED,
          "tek fokuslu insertte floroskopi tusu calismaz (opsiyon acik olsa da)");
    CHECK(svc_set_insert(&svc, TUBE_INSERT_COUNT) == SVC_ERR_PARAM, "gecersiz insert");
    boot();
    CHECK(sm.tube.insert == TUBE_INSERT_D067SB, "insert kalici");

    /* bozuk kayıtlı insert değeri -> varsayılan insert */
    nv_settings_t bad = data.settings;
    bad.tube_insert = 99u;
    CHECK(!svc_apply_boot(&sm, &bad) && sm.tube.insert == TUBE_INSERT_DF161SBR, "gecersiz kayit: DF-161SBR");
}

TEST(buyuk_fokus_secenegi)
{
    fresh();
    CHECK(sm.cfg.large_focus_off, "varsayilan: DF-161SBR yalniz kucuk fokus");
    CHECK(!sm_focus_available(&sm, FOCUS_LARGE) && sm_focus_available(&sm, FOCUS_SMALL), "buyuk fokus secilemez");
    exposure_params_t p = sm.params; p.focus = FOCUS_LARGE;
    CHECK(sm_set_params_ex(&sm, &p) == SM_SET_FOCUS_DISABLED && sm.params.focus == FOCUS_SMALL, "buyuk fokus reddedildi");
    svc_enter(&svc, PIN);
    CHECK(svc_set_large_focus(&svc, true) == SVC_OK && sm_set_params_ex(&sm, &p) == SM_SET_OK, "acilinca secilir");
    CHECK(svc_set_large_focus(&svc, false) == SVC_OK && sm.params.focus == FOCUS_SMALL, "kapatilinca kucuk fokusa doner");
    boot();
    CHECK(sm.cfg.large_focus_off, "ayar kalici");
    svc_enter(&svc, PIN);
    svc_set_insert(&svc, TUBE_INSERT_D067SB);
    CHECK(sm_focus_available(&sm, FOCUS_LARGE) && sm.params.focus == FOCUS_LARGE, "tek fokuslu insertte (tek) fokus her zaman kullanilir");
}

TEST(hazirlikta_ayar_degismez)
{
    fresh();
    svc_enter(&svc, PIN);
    svc_set_fluoro(&svc, true);
    s.hs1 = true;
    sim_run(&s, &sm, 100);
    CHECK(sm.state == ST_PREP, "hazirlik");
    CHECK(svc_set_insert(&svc, TUBE_INSERT_D023) == SVC_ERR_BUSY, "hazirlikta insert degismez");
    CHECK(svc_set_fluoro(&svc, false) == SVC_ERR_BUSY, "hazirlikta opsiyon degismez");
    CHECK(sm_toggle_mode(&sm) == SM_SET_FOCUS_LOCKED && sm.params.mode == MODE_RADIOGRAPHY,
          "hazirlikta mod tusu reddedilir (E213)");
    s.hs1 = false;
    sim_run(&s, &sm, 100);
}

TEST(kayit_hatasi_geri_alinir)
{
    fresh();
    svc_enter(&svc, PIN);
    fail_write = true;
    CHECK(svc_set_fluoro(&svc, true) == SVC_ERR_SAVE, "FRAM yazilamadi");
    CHECK(!sm.cfg.fluoro_enabled && data.settings.fluoro_enabled == 0u, "ayar geri alindi");
    CHECK(svc_set_insert(&svc, TUBE_INSERT_D023) == SVC_ERR_SAVE && sm.tube.insert == TUBE_INSERT_DF161SBR,
          "insert degismedi");
}

/* Son çekimi taklit eder (kalibrasyon son çekimin ayarlarını kullanır) */
static void fake_exposure(float kv, float ma, focus_t f)
{
    sm.last_record.valid = true;
    sm.last_record.reason = END_TIME_DONE;
    sm.last_record.set.kv = kv; sm.last_record.set.ma = ma; sm.last_record.set.time_ms = 100u;
    sm.last_record.set.focus = f; sm.last_record.set.mode = MODE_RADIOGRAPHY;
    sm.last_record.kv_avg = 0.0f; sm.last_record.ma_avg = 0.0f;
    sm.last_record.cal_kv_used = false; sm.last_record.cal_ma_used = false;
}

static int nearf(float a, float b, float t) { return (a > b ? a - b : b - a) <= t; }

TEST(kv_kalibrasyonu)
{
    fresh();
    cal_result_t cr; float v;
    CHECK(svc_cal_kv(&svc, 70.0f, &cr, NULL, &v) == SVC_ERR_NOT_ACTIVE, "servis modu gerekli");
    svc_enter(&svc, PIN);
    sm.last_record.valid = false;
    CHECK(svc_cal_kv(&svc, 70.0f, &cr, NULL, &v) == SVC_ERR_NO_EXPOSURE, "once cekim");
    fake_exposure(70.0f, 5.0f, FOCUS_SMALL);
    sm.last_record.reason = END_RELEASED;
    CHECK(svc_cal_kv(&svc, 68.0f, &cr, NULL, &v) == SVC_ERR_NO_EXPOSURE, "yarida kesilen cekim kabul edilmez");
    fake_exposure(70.0f, 5.0f, FOCUS_SMALL);
    const float v0 = calib_kv_ref_volts(&data.calib, 70.0f);
    CHECK(svc_cal_kv(&svc, 68.0f, &cr, NULL, &v) == SVC_OK && cr == CAL_OK, "68 olculdu: nokta duzeltildi");
    CHECK(v > v0 && nearf(v, v0 + 2.0f * 0.053f, 0.01f), "referans yukseldi (egimle)");
    CHECK(svc_cal_kv(&svc, 69.0f, &cr, NULL, &v) == SVC_ERR_ALREADY_USED, "ayni cekim icin ikinci giris reddedilir");
    fake_exposure(70.0f, 5.0f, FOCUS_SMALL);
    CHECK(svc_cal_kv(&svc, 69.8f, &cr, NULL, &v) == SVC_OK && cr == CAL_IN_TOL, "tolerans icinde: dogrulandi");
    CHECK(data.calib.done & CAL_DONE_KV, "kV kalibrasyon isareti");
    const float vk = calib_kv_ref_volts(&data.calib, 70.0f);
    boot();
    CHECK(nearf(calib_kv_ref_volts(&data.calib, 70.0f), vk, 1e-5f), "FRAM'e yazildi, acilista geri geldi");
    svc_enter(&svc, PIN); fake_exposure(70.0f, 5.0f, FOCUS_SMALL);
    CHECK(svc_cal_kv(&svc, 40.0f, &cr, NULL, &v) == SVC_ERR_PARAM && cr == CAL_ERR_STEP, "%30 ustu fark: olcum hatasi supesi, tablo degismez");
    CHECK(nearf(calib_kv_ref_volts(&data.calib, 70.0f), vk, 1e-5f), "tablo degismedi");
    const float vb = calib_kv_ref_volts(&data.calib, 70.0f);
    fake_exposure(70.0f, 5.0f, FOCUS_SMALL);
    CHECK(svc_cal_kv(&svc, 60.0f, &cr, NULL, &v) == SVC_OK && nearf(v, vb * 1.10f, 0.002f), "tek adimda en cok %10");
}

TEST(ma_kalibrasyonu_fokus_basina)
{
    fresh();
    svc_enter(&svc, PIN);
    cal_result_t cr; float v;
    const float vl0 = calib_ma_ref_volts(&data.calib, CAL_FOCUS_LARGE, 70.0f, 40.0f);
    fake_exposure(60.0f, 4.0f, FOCUS_SMALL);
    const float vs0 = calib_ma_ref_volts(&data.calib, CAL_FOCUS_SMALL, 60.0f, 4.0f);
    CHECK(svc_cal_ma(&svc, 3.6f, &cr, NULL, &v) == SVC_OK && cr == CAL_OK && v > vs0, "kucuk fokus 4 mA: duzeltildi");
    CHECK(nearf(calib_ma_ref_volts(&data.calib, CAL_FOCUS_LARGE, 70.0f, 40.0f), vl0, 1e-6f), "buyuk fokus tablosu etkilenmedi");
    fake_exposure(70.0f, 40.0f, FOCUS_LARGE);
    CHECK(svc_cal_ma(&svc, 37.0f, &cr, NULL, &v) == SVC_OK && cr == CAL_OK, "buyuk fokus 40 mA: duzeltildi");
    fake_exposure(70.0f, 40.0f, FOCUS_LARGE);
    CHECK(svc_cal_ma(&svc, 39.5f, &cr, NULL, &v) == SVC_OK && cr == CAL_IN_TOL && (data.calib.done & CAL_DONE_MA_LARGE), "buyuk fokus dogrulandi");
    fake_exposure(80.0f, 4.0f, FOCUS_SMALL);
    CHECK(svc_cal_ma(&svc, 3.8f, &cr, NULL, &v) == SVC_ERR_PARAM && cr == CAL_ERR_FULL, "kucuk fokusta 4 kV satiri dolu (40/60/70/100)");
    fake_exposure(80.0f, 30.0f, FOCUS_LARGE);
    CHECK(svc_cal_ma(&svc, 29.0f, &cr, NULL, &v) == SVC_OK && data.calib.ma[CAL_FOCUS_LARGE].rows == 2, "buyuk fokusta yeni kV satiri (80) eklendi");
    fake_exposure(60.0f, 10.0f, FOCUS_SMALL);
    CHECK(svc_cal_ma(&svc, 8.0f, &cr, NULL, &v) == SVC_ERR_PARAM && cr == CAL_ERR_LIMIT, "kucuk fokus tam skalasi (5,0 V) asilamaz");
    fake_exposure(60.0f, 10.0f, FOCUS_SMALL);
    CHECK(svc_cal_reset(&svc, false, true) == SVC_OK && data.calib.ma[CAL_FOCUS_LARGE].rows == 1 && !(data.calib.done & CAL_DONE_MA_LARGE),
          "mA tablolari baslangica dondu");
    fail_write = true;
    fake_exposure(60.0f, 4.0f, FOCUS_SMALL);
    const float vb = calib_ma_ref_volts(&data.calib, CAL_FOCUS_SMALL, 60.0f, 4.0f);
    CHECK(svc_cal_ma(&svc, 3.6f, &cr, NULL, &v) == SVC_ERR_SAVE, "FRAM yazilamadi");
    CHECK(nearf(calib_ma_ref_volts(&data.calib, CAL_FOCUS_SMALL, 60.0f, 4.0f), vb, 1e-6f), "tablo geri alindi");
    fail_write = false;
}

TEST(feedback_kalibrasyonu)
{
    fresh();
    svc_enter(&svc, PIN);
    cal_result_t cr, fr; float v;
    fake_exposure(70.0f, 5.0f, FOCUS_SMALL);
    sm.last_record.kv_avg = 0.0f;   /* MEAS=0: EP-MON bağlı değil */
    CHECK(svc_cal_kv(&svc, 68.0f, &cr, &fr, &v) == SVC_OK && fr == CAL_ERR_PARAM, "feedback yoksa yalniz DAC duzeltilir");
    fake_exposure(70.0f, 5.0f, FOCUS_SMALL);
    sm.last_record.kv_avg = 69.0f;  /* kart feedback'i 69 kV sandı, kVp metre 68 */
    const float fb_v = calib_curve_inverse(&data.calib.kvfb_v_to_kv, 69.0f);
    CHECK(svc_cal_kv(&svc, 68.0f, &cr, &fr, &v) == SVC_OK && fr == CAL_OK, "kV feedback noktasi ogrenildi");
    CHECK(nearf(calib_kv_from_fb(&data.calib, fb_v), 68.0f, 0.01f), "ayni feedback gerilimi artik 68 kV okunuyor");
    fake_exposure(60.0f, 4.0f, FOCUS_SMALL);
    sm.last_record.ma_avg = 4.3f;
    const float fbm = calib_curve_inverse(&data.calib.mafb_v_to_ma, 4.3f);
    CHECK(svc_cal_ma(&svc, 3.9f, &cr, &fr, &v) == SVC_OK && fr == CAL_OK, "mA feedback noktasi ogrenildi");
    CHECK(nearf(calib_ma_from_fb(&data.calib, fbm), 3.9f, 0.01f), "IP-MON gerilimi artik 3,9 mA");
    boot();
    CHECK(nearf(calib_kv_from_fb(&data.calib, fb_v), 68.0f, 0.01f), "feedback tablosu FRAM'de");
}

int main(void)
{
    printf("Servis menusu testleri\n");
    RUN(varsayilan_opsiyonlar);
    RUN(pin_ve_kilit);
    RUN(floroskopi_ac_kapat);
    RUN(dap_metre);
    RUN(insert_secimi);
    RUN(buyuk_fokus_secenegi);
    RUN(hazirlikta_ayar_degismez);
    RUN(kayit_hatasi_geri_alinir);
    RUN(kv_kalibrasyonu);
    RUN(ma_kalibrasyonu_fokus_basina);
    RUN(feedback_kalibrasyonu);
    return REPORT();
}
