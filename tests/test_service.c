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

int main(void)
{
    printf("Servis menusu testleri\n");
    RUN(varsayilan_opsiyonlar);
    RUN(pin_ve_kilit);
    RUN(floroskopi_ac_kapat);
    RUN(dap_metre);
    RUN(insert_secimi);
    RUN(hazirlikta_ayar_degismez);
    RUN(kayit_hatasi_geri_alinir);
    return REPORT();
}
