/* Kalibrasyon ve kalıcı saklama testleri */
#include "check.h"
#include <stddef.h>
#include "calib.h"
#include "nvstore.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static int near(float a, float b, float tol) { return fabsf(a - b) <= tol; }

/* Başlangıç tablosu: eski panelden ölçülen EP-REF / IP-REF (29.10.2026) */
TEST(nominal_olcek)
{
    calib_t c; calib_default(&c);
    CHECK(calib_valid(&c), "baslangic tablosu gecerli");
    CHECK(near(calib_kv_ref_volts(&c, 40), 1.86f, 1e-4f), "40 kV = 1.86 V (olculen)");
    CHECK(near(calib_kv_ref_volts(&c, 70), 3.45f, 1e-4f), "70 kV = 3.45 V (olculen)");
    CHECK(near(calib_kv_ref_volts(&c, 100), 5.04f, 1e-4f), "100 kV = 5.04 V (olculen)");
    CHECK(near(calib_kv_ref_volts(&c, 75), (3.45f + 3.98f) / 2, 1e-4f), "ara deger");
    CHECK(near(calib_kv_ref_volts(&c, 110), 5.563f, 1e-3f), "110 kV dogrusal uzatma");
    CHECK(near(calib_ma_ref_volts(&c, 60, 10), 4.725f, 1e-3f), "60 kV 10 mA = 4.72 V (olculen)");
    CHECK(near(calib_ma_ref_volts(&c, 60, 2), 0.831f, 1e-3f), "60 kV 2 mA = 0.81 V (olculen)");
    CHECK(calib_ma_ref_volts(&c, 100, 5) < calib_ma_ref_volts(&c, 70, 5) &&
          calib_ma_ref_volts(&c, 70, 5) < calib_ma_ref_volts(&c, 40, 5), "kV artinca mA referansi duser");
    CHECK(near(calib_ma_ref_volts(&c, 100, 5) / calib_ma_ref_volts(&c, 40, 5), 0.78f, 1e-3f), "100/40 kV orani 0.78");
    CHECK(calib_ma_ref_volts(&c, 40, 15) <= 6.0f, "DAC tavani 6 V");
    CHECK(near(calib_kv_from_fb(&c, 3.0f), 55.0f, 1e-3f), "FB 3 V = 55 kV");
    CHECK(near(calib_ma_from_fb(&c, 1.2f), 10.0f, 1e-3f), "FB 1.2 V = 10 mA");
}

TEST(dac_adc_donusumu)
{
    CHECK(calib_volts_to_dac(6.0f) == 4095, "6 V = tam skala");
    CHECK(calib_volts_to_dac(0.0f) == 0, "0 V = 0");
    CHECK(calib_volts_to_dac(9.0f) == 4095, "6 V ustu kirpilir (donanim tavani)");
    CHECK(calib_volts_to_dac(-1.0f) == 0, "negatif kirpilir");
    CHECK(calib_volts_to_dac(NAN) == 0, "NaN -> 0 V (guvenli taraf)");
    CHECK(abs((int)calib_volts_to_dac(3.0f) - 2048) <= 1, "3 V = yarim skala");
    CHECK(near(calib_adc_to_volts(4095), 6.6f, 1e-3f), "ADC tam skala = 6.6 V");
}

TEST(cok_noktali_kalibrasyon)
{
    calib_t c; calib_default(&c);
    /* kVp metre: 70 kV ayarinda 68.4 olculdu -> referansi yukseltmek gerekir */
    cal_curve_t *k = &c.kv_to_v;
    k->n = 0;
    calib_curve_set_point(k, 40, 2.20f);
    calib_curve_set_point(k, 110, 6.00f);
    calib_curve_set_point(k, 70, 3.907f);   /* sirasiz ekleme */
    CHECK(k->n == 3 && k->x[1] == 70.0f, "noktalar siralandi");
    CHECK(near(calib_kv_ref_volts(&c, 70), 3.907f, 1e-4f), "kalibre nokta tam");
    CHECK(near(calib_kv_ref_volts(&c, 55), (2.20f + 3.907f) / 2, 1e-3f), "ara deger");
    CHECK(calib_valid(&c), "tablo gecerli");
    calib_curve_set_point(k, 70, 3.95f);
    CHECK(k->n == 3 && near(k->y[1], 3.95f, 1e-6f), "ayni nokta guncellendi");
}

TEST(gecersiz_tablo_reddi)
{
    calib_t c; calib_default(&c);
    c.kv_to_v.y[1] = 7.0f;
    CHECK(!calib_valid(&c), "6 V ustu referans reddedilir");
    calib_default(&c);
    c.kv_to_v.n = 3; c.kv_to_v.x[2] = 30.0f; c.kv_to_v.y[2] = 6.0f;
    CHECK(!calib_valid(&c), "x azalan reddedilir");
    calib_default(&c);
    c.kvfb_v_to_kv.y[1] = NAN;
    CHECK(!calib_valid(&c), "NaN reddedilir");
}

TEST(ma_kv_bagimli)
{
    calib_t c; calib_default(&c);
    c.ma.rows = 2;
    c.ma.kv[0] = 50; c.ma.kv[1] = 90;
    cal_curve_t two = { .n = 2, .x = { 0.0f, 50.0f }, .y = { 0.0f, 6.0f } };
    c.ma.ma_to_v[0] = two;                  /* 50 kV */
    c.ma.ma_to_v[1] = two;
    c.ma.ma_to_v[1].y[1] = 5.4f;            /* 90 kV: uzay yuku etkisi, daha az flaman */
    const float v50 = calib_ma_ref_volts(&c, 50, 25);
    const float v90 = calib_ma_ref_volts(&c, 90, 25);
    const float v70 = calib_ma_ref_volts(&c, 70, 25);
    CHECK(near(v50, 3.0f, 1e-3f) && near(v90, 2.7f, 1e-3f), "satir degerleri");
    CHECK(near(v70, 2.85f, 1e-3f), "kV arasinda ara deger");
    CHECK(calib_valid(&c), "gecerli");
}

/* --- FRAM simulasyonu --- */
static uint8_t fram[32768];
static int fail_write_after = -1;   /* yazma sirasinda guc kesilmesi */
static bool f_read(uint32_t a, void *b, size_t n) { memcpy(b, fram + a, n); return true; }
static bool f_write(uint32_t a, const void *b, size_t n)
{
    if (fail_write_after >= 0) {    /* kismi yazma */
        memcpy(fram + a, b, (size_t)fail_write_after < n ? (size_t)fail_write_after : n);
        return false;
    }
    memcpy(fram + a, b, n);
    return true;
}
static const nv_ops_t ops = { f_read, f_write };

TEST(bos_fram_varsayilan)
{
    memset(fram, 0xFF, sizeof fram);
    nvstore_t nv; nv_data_t d;
    CHECK(nv_load(&nv, &ops, &d) == NV_LOAD_DEFAULTS, "bos FRAM -> varsayilan");
    CHECK(calib_valid(&d.calib), "varsayilan kalibrasyon gecerli");
    CHECK(d.settings.detector_profile == DET_PROFILE_AED, "varsayilan profil AED");
}

TEST(kaydet_yukle)
{
    memset(fram, 0xFF, sizeof fram);
    nvstore_t nv; nv_data_t d;
    nv_load(&nv, &ops, &d);
    calib_curve_set_point(&d.calib.kv_to_v, 70, 3.9f);
    d.settings.exposure_count = 1234;
    CHECK(nv_save(&nv, &d), "kayit 1");
    d.settings.exposure_count = 1235;
    CHECK(nv_save(&nv, &d), "kayit 2 (diger kopyaya)");
    nvstore_t nv2; nv_data_t e;
    CHECK(nv_load(&nv2, &ops, &e) == NV_LOAD_OK, "iki kopya gecerli");
    CHECK(e.settings.exposure_count == 1235, "en yeni kopya secildi");
    CHECK(near(calib_kv_ref_volts(&e.calib, 70), 3.9f, 1e-5f), "kalibrasyon geri geldi");
}

TEST(yazarken_guc_kesilmesi)
{
    memset(fram, 0xFF, sizeof fram);
    nvstore_t nv; nv_data_t d;
    nv_load(&nv, &ops, &d);
    d.settings.exposure_count = 10; nv_save(&nv, &d);
    d.settings.exposure_count = 11; nv_save(&nv, &d);
    d.settings.exposure_count = 12;
    fail_write_after = 100;          /* kayit ortasinda kesildi */
    CHECK(!nv_save(&nv, &d), "yazma basarisiz bildirildi");
    fail_write_after = -1;
    nvstore_t nv2; nv_data_t e;
    CHECK(nv_load(&nv2, &ops, &e) == NV_LOAD_ONE_BAD, "bozuk kopya tespit edildi");
    CHECK(e.settings.exposure_count == 11, "son saglam kopya yuklendi");
    e.settings.exposure_count = 13;
    CHECK(nv_save(&nv2, &e), "bozuk kopya onarilarak yazildi");
    nvstore_t nv3; nv_data_t g;
    CHECK(nv_load(&nv3, &ops, &g) == NV_LOAD_OK && g.settings.exposure_count == 13, "iki kopya yine saglam");
}

TEST(bit_bozulmasi)
{
    memset(fram, 0xFF, sizeof fram);
    nvstore_t nv; nv_data_t d;
    nv_load(&nv, &ops, &d);
    d.settings.exposure_count = 77; nv_save(&nv, &d);
    d.settings.exposure_count = 78; nv_save(&nv, &d);   /* B */
    fram[NV_SLOT_B_ADDR + 40] ^= 0x04;                   /* tek bit hatasi */
    nvstore_t nv2; nv_data_t e;
    CHECK(nv_load(&nv2, &ops, &e) == NV_LOAD_ONE_BAD && e.settings.exposure_count == 77,
          "CRC bit hatasini yakaladi, A kopyasina donuldu");
}

TEST(gecersiz_kalibrasyon_yazilmaz)
{
    memset(fram, 0xFF, sizeof fram);
    nvstore_t nv; nv_data_t d;
    nv_load(&nv, &ops, &d);
    d.calib.kv_to_v.y[1] = 8.0f;
    CHECK(!nv_save(&nv, &d), "gecersiz tablo reddedildi");
}

/* Firmware v0.8 (kayıt sürümü 1) FRAM'i: kalibrasyon korunarak okunur */
TEST(surum1_kaydi_donusturulur)
{
    typedef struct { float gp; uint8_t lang, det, fl, pc; uint32_t cnt; } s1_t;
    typedef struct { uint32_t magic; uint16_t version, length; uint32_t seq; } h_t;
    typedef struct { h_t h; struct { calib_t calib; s1_t st; } d; uint32_t crc; } r1_t;
    memset(fram, 0xFF, sizeof fram);
    static r1_t r;
    memset(&r, 0, sizeof r);
    r.h.magic = NV_MAGIC; r.h.version = 1u; r.h.length = (uint16_t)sizeof(r.d); r.h.seq = 5u;
    calib_default(&r.d.calib);
    r.d.calib.kv_to_v.y[1] *= 0.98f;               /* ayarlanmış kalibrasyon */
    r.d.st.gp = 3000.0f; r.d.st.fl = 1u; r.d.st.cnt = 1234u; r.d.st.det = DET_PROFILE_SYNC;
    r.crc = nv_crc32(&r, offsetof(r1_t, crc));
    memcpy(fram + NV_SLOT_A_ADDR, &r, sizeof r);
    nvstore_t nv; nv_data_t d;
    CHECK(nv_load(&nv, &ops, &d) == NV_LOAD_ONE_BAD, "surum 1 kaydi okundu");
    CHECK(d.calib.kv_to_v.y[1] == r.d.calib.kv_to_v.y[1], "kalibrasyon korundu");
    CHECK(d.settings.gen_power_max_w == 3000.0f && d.settings.fluoro_enabled == 1u &&
          d.settings.exposure_count == 1234u && d.settings.detector_profile == DET_PROFILE_SYNC, "eski ayarlar korundu");
    CHECK(d.settings.tube_insert == 0u && d.settings.dap_meter_enabled == 0u, "yeni alanlar varsayilan");
    CHECK(nv_save(&nv, &d), "surum 2 olarak kaydedildi");
    nvstore_t nv2; nv_data_t e;
    CHECK(nv_load(&nv2, &ops, &e) == NV_LOAD_OK && e.settings.exposure_count == 1234u && nv2.seq == 6u,
          "iki kopya gecerli, sira numarasi devam");
}

int main(void)
{
    printf("Kalibrasyon ve kalici saklama testleri\n");
    RUN(nominal_olcek);
    RUN(dac_adc_donusumu);
    RUN(cok_noktali_kalibrasyon);
    RUN(gecersiz_tablo_reddi);
    RUN(ma_kv_bagimli);
    RUN(bos_fram_varsayilan);
    RUN(kaydet_yukle);
    RUN(yazarken_guc_kesilmesi);
    RUN(bit_bozulmasi);
    RUN(gecersiz_kalibrasyon_yazilmaz);
    RUN(surum1_kaydi_donusturulur);
    return REPORT();
}
