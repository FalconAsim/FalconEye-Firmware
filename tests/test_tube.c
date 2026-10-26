/* Tüp koruması testleri - Canon DF-161SBR sınırlarına göre */
#include "check.h"
#include "tube.h"

static exposure_params_t rad(float kv, float ma, uint32_t ms, focus_t f)
{
    exposure_params_t p = { kv, ma, ms, f, MODE_RADIOGRAPHY };
    return p;
}

static exposure_params_t fluoro(float kv, float ma, uint32_t ms)
{
    exposure_params_t p = { kv, ma, ms, FOCUS_SMALL, MODE_FLUORO };
    return p;
}

TEST(kv_alt_siniri_40)
{
    tube_state_t st; tube_init(&st);
    exposure_params_t p = rad(30, 10, 100, FOCUS_LARGE);
    CHECK(tube_check(&st, &p) == TUBE_ERR_KV_LOW, "30 kV reddedilmeli (tup alt siniri 40 kV)");
    p = rad(40, 10, 100, FOCUS_LARGE);
    CHECK(tube_check(&st, &p) == TUBE_OK, "40 kV kabul edilmeli");
}

TEST(kv_ust_siniri_jenerator)
{
    tube_state_t st; tube_init(&st);
    exposure_params_t p = rad(115, 10, 100, FOCUS_LARGE);
    CHECK(tube_check(&st, &p) == TUBE_ERR_KV_HIGH, "115 kV jenerator ustu, reddedilmeli");
}

TEST(kucuk_fokus_15mA)
{
    tube_state_t st; tube_init(&st);
    exposure_params_t p = rad(40, 16, 100, FOCUS_SMALL);
    CHECK(tube_check(&st, &p) == TUBE_ERR_MA_FOCUS_MAX, "kucuk fokusta 16 mA reddedilmeli");
}

TEST(yuk_egrisi)
{
    /* 100 kV, buyuk fokus, 0.1 s: egri ~41 mA, %10 pay ile ~36.9 */
    float m = tube_rating_ma_max(FOCUS_LARGE, 100, 100);
    CHECK(m > 36.0f && m < 37.5f, "100 kV / 0.1 s limiti ~36.9 mA olmali");
    tube_state_t st; tube_init(&st);
    exposure_params_t p = rad(100, 30, 100, FOCUS_LARGE);
    CHECK(tube_check(&st, &p) == TUBE_OK, "100 kV 30 mA 0.1 s uygun olmali");
    /* 0.1 s'de buyuk fokusta once jenerator gucu (3.2 kW) sinirlar */
    p = rad(100, 40, 100, FOCUS_LARGE);
    CHECK(tube_check(&st, &p) == TUBE_ERR_GEN_POWER, "100 kV 40 mA = 4 kW, jenerator siniri");
    /* 10 s'de egri ~28.5 mA, %10 pay ile ~25.7: 27 mA egriyi asar (2.7 kW) */
    p = rad(100, 27, 10000, FOCUS_LARGE);
    CHECK(tube_check(&st, &p) == TUBE_ERR_RATING, "100 kV 27 mA 10 s yuk egrisini asar");
    /* uzun surede limit duser */
    float m10 = tube_rating_ma_max(FOCUS_LARGE, 100, 10000);
    CHECK(m10 < m, "10 s limiti 0.1 s limitinden dusuk olmali");
}

TEST(radyografi_10s_siniri)
{
    tube_state_t st; tube_init(&st);
    exposure_params_t p = rad(60, 5, 10001, FOCUS_LARGE);
    CHECK(tube_check(&st, &p) == TUBE_ERR_TIME_MAX, "radyografi 10 s ustu reddedilmeli");
}

TEST(jenerator_gucu)
{
    tube_state_t st; tube_init(&st);
    exposure_params_t p = rad(70, 50, 100, FOCUS_LARGE);   /* 3500 W */
    CHECK(tube_check(&st, &p) == TUBE_ERR_GEN_POWER, "3.5 kW > 3.2 kW reddedilmeli");
}

TEST(floroskopi_sinirlari)
{
    tube_state_t st; tube_init(&st);
    exposure_params_t p = fluoro(60, 10, 20000);  /* 600 W */
    CHECK(tube_check(&st, &p) == TUBE_OK, "60 kV 10 mA 20 s floroskopi uygun");
    p = fluoro(60, 12, 10000);                    /* 720 W */
    CHECK(tube_check(&st, &p) == TUBE_ERR_FLUORO_POWER, "600 W ustu reddedilmeli");
    p = fluoro(60, 50, 20000);
    CHECK(tube_check(&st, &p) == TUBE_ERR_MA_FOCUS_MAX, "50 mA kucuk fokusta imkansiz");
    p = fluoro(70, 5, 10000);
    CHECK(tube_check(&st, &p) == TUBE_ERR_FLUORO_KV, "floroskopi 60 kV ustu reddedilmeli");
    p = fluoro(50, 10, 25000);
    CHECK(tube_check(&st, &p) == TUBE_ERR_TIME_MAX, "floroskopi 20 s ustu reddedilmeli");
    p.focus = FOCUS_LARGE; p.time_ms = 10000;
    CHECK(tube_check(&st, &p) == TUBE_ERR_FLUORO_FOCUS, "floroskopi buyuk fokus reddedilmeli");
}

TEST(gorev_dongusu)
{
    tube_state_t st; tube_init(&st);
    tube_exposure_done(&st, FOCUS_LARGE, 1000);   /* 1:40 -> 40 s */
    exposure_params_t p = rad(70, 20, 100, FOCUS_LARGE);
    CHECK(tube_check(&st, &p) == TUBE_ERR_COOLDOWN, "bekleme suresince reddedilmeli");
    tube_tick(&st, 0, 39999);
    CHECK(tube_check(&st, &p) == TUBE_ERR_COOLDOWN, "39.999 s sonra hala beklemede");
    tube_tick(&st, 0, 1);
    CHECK(tube_check(&st, &p) == TUBE_OK, "40 s sonra serbest");
}

TEST(anot_isi_modeli)
{
    tube_state_t st; tube_init(&st);
    /* 600 W surekli: denge 35.5 kJ'u gecmemeli */
    for (int i = 0; i < 20 * 60; i++) tube_tick(&st, 600.0f, 1000);
    CHECK(st.heat_j <= 35500.0f, "600 W'ta 20 dk sonunda kapasite asilmamali");
    CHECK(tube_heat_fraction(&st) > 0.95f, "600 W dengesi kapasiteye yakin olmali");
    /* dolu anotla buyuk isinlama reddedilir */
    exposure_params_t p = rad(80, 30, 2000, FOCUS_LARGE);   /* 4.8 kJ */
    CHECK(tube_check(&st, &p) == TUBE_ERR_HEAT, "dolu anotta reddedilmeli");
    /* sogudukca serbest kalir */
    for (int i = 0; i < 300; i++) tube_tick(&st, 0.0f, 1000);
    CHECK(tube_check(&st, &p) == TUBE_OK, "5 dk sogumadan sonra izin verilmeli");
}

int main(void)
{
    printf("Tup korumasi testleri (Canon DF-161SBR)\n");
    RUN(kv_alt_siniri_40);
    RUN(kv_ust_siniri_jenerator);
    RUN(kucuk_fokus_15mA);
    RUN(yuk_egrisi);
    RUN(radyografi_10s_siniri);
    RUN(jenerator_gucu);
    RUN(floroskopi_sinirlari);
    RUN(gorev_dongusu);
    RUN(anot_isi_modeli);
    return REPORT();
}
