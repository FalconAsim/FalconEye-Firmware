/* Kutu iç sıcaklığı ve fan izleme testleri (thermal_mon) */
#include "check.h"
#include "thermal_mon.h"
#include "error_codes.h"
#include <math.h>

static thermal_mon_t t;

static void run(float tc, float hz, uint32_t ms) { for (uint32_t i = 0; i < ms; i += 100u) thermal_step(&t, tc, hz, 100u); }

static void init_default(void) { thermal_cfg_t c; thermal_default_cfg(&c); thermal_init(&t, &c); }

TEST(fan_surekli_varsayilan)
{
    init_default();
    CHECK(t.fan_cmd, "acilista fan acik");
    run(20.0f, 50.0f, 10000u);
    CHECK(t.fan_cmd, "soguk kutuda da fan acik (surekli mod)");
    CHECK(!t.warn_fan && !t.warn_temp, "uyari yok");
}

TEST(sicaklik_uyarisi_histerezis)
{
    init_default();
    run(54.9f, 50.0f, 1000u);
    CHECK(!t.warn_temp, "54,9 C uyari yok");
    run(55.0f, 50.0f, 100u);
    CHECK(t.warn_temp, "55 C W606");
    run(52.0f, 50.0f, 1000u);
    CHECK(t.warn_temp, "52 C hala uyari (histerezis)");
    run(50.0f, 50.0f, 100u);
    CHECK(!t.warn_temp, "50 C birakildi");
    CHECK(fabsf(t.t_max_c - 55.0f) < 0.01f, "en yuksek 55 C kaydedildi");
}

TEST(fan_durdu_uyarisi)
{
    init_default();
    run(30.0f, 0.0f, 2900u);
    CHECK(!t.warn_fan, "baslangic beklemesinde uyari yok");
    run(30.0f, 0.0f, 4900u);
    CHECK(!t.warn_fan, "dusuk takometre 5 s dolmadan uyari yok");
    run(30.0f, 0.0f, 300u);
    CHECK(t.warn_fan, "W607");
    run(30.0f, 50.0f, 1000u);
    CHECK(t.warn_fan, "W607 kalici (fan geri gelse de onay ister)");
    thermal_ack_fan(&t);
    CHECK(!t.warn_fan, "onayla temizlendi");
    run(30.0f, 10.0f, 5200u);
    CHECK(t.warn_fan, "yavas fan (600 dev/dk alti) de W607");
}

TEST(termostatik_mod)
{
    thermal_cfg_t c; thermal_default_cfg(&c); c.fan_always_on = false; thermal_init(&t, &c);
    run(25.0f, 0.0f, 10000u);
    CHECK(!t.fan_cmd, "25 C fan kapali");
    CHECK(!t.warn_fan, "fan kapaliyken takometre denetlenmez");
    run(35.0f, 50.0f, 100u);
    CHECK(t.fan_cmd, "35 C fan acik");
    run(32.0f, 50.0f, 1000u);
    CHECK(t.fan_cmd, "32 C hala acik (histerezis)");
    run(30.0f, 0.0f, 100u);
    CHECK(!t.fan_cmd, "30 C kapandi");
}

TEST(sensor_arizasi_fani_acar)
{
    thermal_cfg_t c; thermal_default_cfg(&c); c.fan_always_on = false; thermal_init(&t, &c);
    run(25.0f, 0.0f, 1000u);
    CHECK(!t.fan_cmd, "on kosul: fan kapali");
    run(NAN, 50.0f, 100u);
    CHECK(t.sensor_bad && t.fan_cmd, "NaN: sensor arizasi, fan acik");
    run(-60.0f, 50.0f, 100u);
    CHECK(t.sensor_bad && t.fan_cmd, "-60 C: gecersiz, fan acik");
    CHECK(fabsf(t.t_c - 25.0f) < 0.01f, "gecersiz okuma son degeri bozmaz");
}

TEST(hata_kodlari_tabloda)
{
    const fe_error_t *a = fe_warning_box_temp(), *b = fe_warning_fan();
    CHECK(a && a->code == 606 && a->prefix == 'W', "W606");
    CHECK(b && b->code == 607 && b->prefix == 'W', "W607");
}

int main(void)
{
    RUN(fan_surekli_varsayilan);
    RUN(sicaklik_uyarisi_histerezis);
    RUN(fan_durdu_uyarisi);
    RUN(termostatik_mod);
    RUN(sensor_arizasi_fani_acar);
    RUN(hata_kodlari_tabloda);
    return REPORT();
}
