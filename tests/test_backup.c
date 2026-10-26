/* Yedek zamanlayıcı testleri */
#include "check.h"
#include "backup_timer.h"

static backup_timer_t b;

static uint32_t expose(uint32_t ms_line_active)
{
    /* hattı ms_line_active boyunca aktif tutar; ENABLE düşerse jeneratör de durur
     * (AND kapısı) -> gerçek ışınlama süresini döner */
    uint32_t real = 0;
    for (uint32_t t = 0; t < ms_line_active; t++) {
        const bool line = b.enable;      /* hat ancak ENABLE açıkken aktif olabilir */
        bt_tick(&b, line, 1);
        if (line) real++;
        if (!b.enable) break;
    }
    bt_tick(&b, false, 1);
    return real;
}

TEST(varsayilan_kapali)
{
    bt_init(&b);
    CHECK(!b.enable && b.state == BT_IDLE, "acilista ENABLE kapali");
    bt_tick(&b, false, 5000);
    CHECK(!b.enable, "ARM olmadan acilmaz");
}

TEST(normal_cekim)
{
    bt_init(&b);
    CHECK(bt_arm(&b, 100, BT_MODE_RAD, 1), "ARM kabul");
    CHECK(b.enable && b.limit_ms == 120, "sinir 100*1.1+10 = 120 ms");
    uint32_t real = 0;
    for (int t = 0; t < 100; t++) { bt_tick(&b, true, 1); real++; }
    bt_tick(&b, false, 1);               /* ana MCU zamaninda kesti */
    CHECK(real == 100 && b.state == BT_IDLE && !b.enable, "normal bitis, yeni ARM gerekir");
    CHECK(b.trip == BT_TRIP_NONE, "trip yok");
}

TEST(ana_mcu_kilitlenir)
{
    bt_init(&b);
    bt_arm(&b, 100, BT_MODE_RAD, 2);
    const uint32_t real = expose(5000);  /* ana MCU X-RAY ON'u hic kesmiyor */
    CHECK(real <= 121, "yedek zamanlayici 120 ms'de kesti");
    CHECK(b.state == BT_TRIPPED && b.trip == BT_TRIP_TIME, "TRIP kilitlendi");
    CHECK(!bt_arm(&b, 100, BT_MODE_RAD, 3), "TRIP'te yeniden silahlanamaz");
    CHECK(!bt_clear(&b, true), "hat aktifken CLEAR reddedilir");
    CHECK(bt_clear(&b, false) && b.state == BT_IDLE, "hat pasifken CLEAR");
}

TEST(mutlak_sinir)
{
    bt_init(&b);
    CHECK(!bt_arm(&b, 10001, BT_MODE_RAD, 4), "radyografi 10 s ustu silahlanmaz");
    CHECK(!bt_arm(&b, 0, BT_MODE_RAD, 4), "0 ms silahlanmaz");
    CHECK(bt_arm(&b, 9500, BT_MODE_RAD, 4) && b.limit_ms == 10000, "sinir mutlak 10 s'ye kirpilir");
    bt_init(&b);
    CHECK(bt_arm(&b, 20000, BT_MODE_FLUORO, 5) && b.limit_ms == 20000, "floroskopi 20 s");
    CHECK(!bt_arm(&b, 20001, BT_MODE_FLUORO, 6) || b.limit_ms == 20000, "floroskopi 20 s ustu yok");
}

TEST(arm_suresi_dolar)
{
    bt_init(&b);
    bt_arm(&b, 100, BT_MODE_RAD, 7);
    bt_tick(&b, false, 900);
    CHECK(b.enable, "900 ms: hala silahli");
    bt_arm(&b, 100, BT_MODE_RAD, 7);    /* ayni mesaj: yenileme */
    bt_tick(&b, false, 900);
    CHECK(b.enable, "yenilendi");
    bt_tick(&b, false, 200);
    CHECK(!b.enable && b.state == BT_IDLE, "1 s yenilenmezse silahsizlanir");
}

TEST(silahsiz_isinlama_ariza)
{
    bt_init(&b);
    bt_tick(&b, true, 1);                /* AND kapisi arizali: hat aktif */
    CHECK(b.state == BT_TRIPPED && b.trip == BT_TRIP_UNARMED_XRAY, "silahsiz isinlama -> TRIP");
}

TEST(ard_arda_cekim_engellenir)
{
    bt_init(&b);
    bt_arm(&b, 50, BT_MODE_RAD, 8);
    for (int t = 0; t < 50; t++) bt_tick(&b, true, 1);
    bt_tick(&b, false, 1);
    CHECK(!b.enable, "cekimden sonra ENABLE dustu");
    bt_tick(&b, true, 1);
    CHECK(b.state == BT_TRIPPED, "yeni ARM olmadan ikinci cekim -> TRIP");
}

TEST(cerceve_ve_crc)
{
    bt_init(&b);
    uint8_t f[BT_FRAME_LEN];
    bt_encode(BT_MSG_ARM, 250, 0, 9, f);
    CHECK(bt_decode_and_apply(&b, f, false) && b.limit_ms == 285, "cerceve cozuldu, sinir 285 ms");
    bt_init(&b);
    bt_encode(BT_MSG_ARM, 250, 0, 10, f);
    f[5] ^= 0x01;                        /* sure bozuldu */
    CHECK(!bt_decode_and_apply(&b, f, false) && !b.enable, "CRC hatasi: silahlanmaz");
    bt_encode(BT_MSG_ARM, 250, 7, 11, f);
    CHECK(!bt_decode_and_apply(&b, f, false), "gecersiz mod reddedilir");
}

int main(void)
{
    printf("Yedek zamanlayici testleri\n");
    RUN(varsayilan_kapali);
    RUN(normal_cekim);
    RUN(ana_mcu_kilitlenir);
    RUN(mutlak_sinir);
    RUN(arm_suresi_dolar);
    RUN(silahsiz_isinlama_ariza);
    RUN(ard_arda_cekim_engellenir);
    RUN(cerceve_ve_crc);
    return REPORT();
}
