/* Kutu iç sıcaklığı, çekim kilidi (E221) ve fan izleme testleri (thermal_mon) */
#include "check.h"
#include "thermal_mon.h"
#include "error_codes.h"
#include "exposure_sm.h"
#include "fe_config.h"
#include "hal_sim.h"
#include <math.h>
#include <string.h>

static thermal_mon_t t;

static void run(float ntc, float mcu, float hz, uint32_t ms) { for (uint32_t i = 0; i < ms; i += 100u) thermal_step(&t, ntc, mcu, hz, 100u); }
static void init_default(void) { thermal_cfg_t c; thermal_default_cfg(&c); thermal_init(&t, &c); }

TEST(fan_surekli_varsayilan)
{
    init_default();
    CHECK(t.fan_cmd, "acilista fan acik");
    run(20.0f, 25.0f, 50.0f, 10000u);
    CHECK(t.fan_cmd, "soguk kutuda da fan acik (surekli mod)");
    CHECK(!t.warn_fan && !t.warn_temp && !t.lock && !t.warn_sensor, "uyari ve kilit yok");
    CHECK(t.src == TSRC_NTC, "kaynak Kart A NTC");
}

TEST(kilit_50_birakma_47)
{
    init_default();
    run(40.0f, 45.0f, 50.0f, 1000u);
    CHECK(!t.lock && !t.warn_temp, "40 C serbest");
    run(47.0f, 50.0f, 50.0f, 100u);
    CHECK(t.warn_temp && !t.lock, "47 C: W606 on uyari, kilit yok");
    run(49.9f, 52.0f, 50.0f, 100u);
    CHECK(!t.lock, "49,9 C kilit yok");
    run(50.0f, 52.0f, 50.0f, 100u);
    CHECK(t.lock, "50 C: E221 kilit");
    run(48.0f, 50.0f, 50.0f, 1000u);
    CHECK(t.lock, "48 C hala kilitli (histerezis)");
    run(47.0f, 49.0f, 50.0f, 100u);
    CHECK(!t.lock, "47 C kilit birakildi");
    CHECK(t.warn_temp, "47 C W606 suruyor");
    run(45.0f, 47.0f, 50.0f, 100u);
    CHECK(!t.warn_temp, "45 C W606 birakildi");
    CHECK(fabsf(t.t_max_c - 50.0f) < 0.01f, "en yuksek 50 C kaydedildi");
}

TEST(acilis_ilk_okumaya_gore)
{
    init_default();
    CHECK(t.lock, "okuma gelmeden kilitli");
    run(48.5f, 50.0f, 50.0f, 100u);
    CHECK(!t.lock, "ilk okuma 48,5 C (< 50): acilista kilit yok");
    init_default();
    run(51.0f, 50.0f, 50.0f, 100u);
    CHECK(t.lock, "ilk okuma 51 C: kilitli");
}

TEST(yedek_sensor_ve_arizasi)
{
    init_default();
    run(NAN, 44.0f, 50.0f, 1000u);
    CHECK(t.src == TSRC_MCU && t.warn_sensor, "NTC yok: ic sensor + W608");
    CHECK(!t.lock, "ic sensor 44 C: kilit yok");
    run(NAN, 51.0f, 50.0f, 100u);
    CHECK(t.lock, "yedek sensorle de 50 C kilidi calisir");
    run(NAN, NAN, 50.0f, 100u);
    CHECK(t.src == TSRC_NONE && t.lock && t.warn_sensor && t.fan_cmd, "hic sensor yok: kilit + W608 + fan");
    run(30.0f, NAN, 50.0f, 100u);
    CHECK(!t.lock && !t.warn_sensor, "NTC geri geldi: serbest");
}

TEST(can_mesaji_0x330)
{
    tbox_msg_t m = { 7, 452, FE_TBOX_NONE, 0 }, r;
    uint8_t b[8]; tbox_pack(&m, b); tbox_unpack(b, &r);
    CHECK(r.seq == 7 && r.t_brd_d == 452 && r.t_ext_d == FE_TBOX_NONE, "paketle / ac");
    CHECK(fabsf(tbox_ntc_c(&r) - 45.2f) < 0.01f, "yalniz kart NTC");
    r.t_ext_d = 488;
    CHECK(fabsf(tbox_ntc_c(&r) - 48.8f) < 0.01f, "iki NTC: yuksek olan");
    r.t_brd_d = -125; r.t_ext_d = -50;
    CHECK(fabsf(tbox_ntc_c(&r) + 5.0f) < 0.01f, "eksi sicaklik");
    r.t_brd_d = r.t_ext_d = FE_TBOX_NONE;
    CHECK(isnan(tbox_ntc_c(&r)), "ikisi de yok: NaN");
    r.t_brd_d = 1500;
    CHECK(isnan(tbox_ntc_c(&r)), "150 C gecersiz");
}

TEST(fan_durdu_uyarisi)
{
    init_default();
    run(30.0f, 35.0f, 0.0f, 2900u);
    CHECK(!t.warn_fan, "baslangic beklemesinde uyari yok");
    run(30.0f, 35.0f, 0.0f, 4900u);
    CHECK(!t.warn_fan, "5 s dolmadan uyari yok");
    run(30.0f, 35.0f, 0.0f, 300u);
    CHECK(t.warn_fan, "W607");
    run(30.0f, 35.0f, 50.0f, 1000u);
    CHECK(t.warn_fan, "W607 kalici (onay ister)");
    thermal_ack_fan(&t);
    CHECK(!t.warn_fan, "onayla temizlendi");
    run(30.0f, 35.0f, 10.0f, 5200u);
    CHECK(t.warn_fan, "yavas fan da W607");
}

TEST(termostatik_mod)
{
    thermal_cfg_t c; thermal_default_cfg(&c); c.fan_always_on = false; thermal_init(&t, &c);
    run(25.0f, 30.0f, 0.0f, 10000u);
    CHECK(!t.fan_cmd, "25 C fan kapali");
    CHECK(!t.warn_fan, "fan kapaliyken takometre denetlenmez");
    run(35.0f, 40.0f, 50.0f, 100u);
    CHECK(t.fan_cmd, "35 C fan acik");
    run(32.0f, 37.0f, 50.0f, 1000u);
    CHECK(t.fan_cmd, "32 C hala acik");
    run(30.0f, 35.0f, 0.0f, 100u);
    CHECK(!t.fan_cmd, "30 C kapandi");
}

TEST(hata_kodlari_tabloda)
{
    const fe_error_t *a = fe_warning_box_temp(), *b = fe_warning_fan(), *s = fe_warning_temp_sensor();
    const fe_error_t *e = fe_error_for(END_BOX_HOT, TUBE_OK);
    CHECK(a && a->code == 606 && a->prefix == 'W', "W606");
    CHECK(b && b->code == 607 && b->prefix == 'W', "W607");
    CHECK(s && s->code == 608 && s->prefix == 'W', "W608");
    CHECK(e && e->code == 221 && e->prefix == 'E', "E221");
}

/* Durum makinesi: kilit yeni hazırlığı engeller, devam eden ışınlamayı kesmez */
static exposure_sm_t sm;
static sim_t s;
static void boot(void)
{
    sm_config_t cfg = { .hs_dual_channel = true, .line_readback = true, .cap_charge_ms = FE_CAP_CHARGE_DELAY_MS };
    sm_init(&sm, &cfg); sm_load_boot_defaults(&sm); sim_init(&s);
    sim_run(&s, &sm, FE_CAP_CHARGE_DELAY_MS + FE_CAP_SETTLE_MS + 100u);
    exposure_params_t p = { 70, 20, 1000, FOCUS_LARGE, MODE_RADIOGRAPHY };
    sm_set_params(&sm, &p); sim_run(&s, &sm, 300u);
}

TEST(sm_kilit_yeni_hazirligi_engeller)
{
    boot();
    s.box_hot = true;
    s.hs1 = true; sim_run(&s, &sm, 500u);
    CHECK(sm.state == ST_LOCKED && sm.last_reason == END_BOX_HOT, "1. kademe: E221, hazirlik yok");
    CHECK(!s.last_out.preheat, "filaman surulmedi");
    s.hs1 = false; sim_run(&s, &sm, 200u);
    s.box_hot = false;
    s.hs1 = true; sim_run(&s, &sm, 4500u);
    CHECK(sm.state == ST_READY, "sogudu: normal hazirlik");
    s.hs1 = false; sim_run(&s, &sm, 200u);
}

TEST(sm_devam_eden_isinlama_kesilmez)
{
    boot();
    s.hs1 = true; sim_run(&s, &sm, 4500u);
    CHECK(sm.state == ST_READY, "hazir");
    s.hs2 = true; sim_run(&s, &sm, 300u);
    s.box_hot = true;            /* ışınlama sırasında 50 °C'yi geçti */
    sim_run(&s, &sm, 1000u);
    CHECK(sm.last_record.valid && sm.last_record.reason == END_TIME_DONE, "isinlama sure dolana kadar surdu");
    s.hs1 = s.hs2 = false; sim_run(&s, &sm, 200u);
    s.hs1 = true; sim_run(&s, &sm, 300u);
    CHECK(sm.last_reason == END_BOX_HOT, "sonraki cekim kilitli");
}

int main(void)
{
    RUN(fan_surekli_varsayilan);
    RUN(kilit_50_birakma_47);
    RUN(acilis_ilk_okumaya_gore);
    RUN(yedek_sensor_ve_arizasi);
    RUN(can_mesaji_0x330);
    RUN(fan_durdu_uyarisi);
    RUN(termostatik_mod);
    RUN(hata_kodlari_tabloda);
    RUN(sm_kilit_yeni_hazirligi_engeller);
    RUN(sm_devam_eden_isinlama_kesilmez);
    return REPORT();
}
