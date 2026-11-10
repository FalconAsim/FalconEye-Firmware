/*
 * FalconEye Kart M – hayvan modülü (STM32G0B1CBU6) – devreye alma firmware'i.
 * Konsol yok: sonuçlar CAN ile Kart A'ya (ekran / J42 konsolu), LED_M ile:
 *   yavaş yanıp sönme (1 Hz) = hepsi tamam · hızlı (5 Hz) = en az bir kalem kaldı · sürekli yanık = CAN başlamadı.
 * ADS1292R (SPI1 mod 1), ABP 0x28 ve SDP810 0x25 (I2C1), CAN (FDCAN1 PA11/PA12, ISOW7742 üzerinden).
 */
#include "hw.h"
#include "bu_core.h"
#include "bu_boards.h"
#include <string.h>

static bool g_hse, g_can_ok, g_ads_ok, g_abp_ok, g_sdp_ok;
static uint8_t g_ads_id;
static hw_can_t g_can;
static bu_peer_t g_a;
static float g_vdda, g_tmcu, g_dp, g_sdp_t, g_abp_kpa;

static void ads_cs(bool lo) { hw_gpio_write('A', 4, !lo); }

static uint8_t ads_read_id(void)
{
    hw_gpio_write('A', 8, false); hw_delay_us(20); hw_gpio_write('A', 8, true);   /* RESET darbesi */
    hw_delay_ms(2);
    ads_cs(true); hw_spi_xfer(SPI1, 0x11); ads_cs(false);                          /* SDATAC */
    hw_delay_us(10);
    ads_cs(true);
    hw_spi_xfer(SPI1, 0x20); hw_delay_us(5); hw_spi_xfer(SPI1, 0x00); hw_delay_us(5);   /* RREG ID, 1 yazmaç */
    uint8_t id = hw_spi_xfer(SPI1, 0x00);
    ads_cs(false);
    return id;
}

static bool abp_read(float *kpa)
{
    uint8_t b[2];
    if (!hw_i2c_read(I2C1, BU_M_ABP_ADDR, b, 2)) return false;
    if (b[0] >> 6) return false;                          /* durum ≠ 0: bayat / hata */
    uint16_t c = (uint16_t)(((b[0] & 0x3Fu) << 8) | b[1]);
    *kpa = ((float)c - 1638.0f) * 10.0f / (14745.0f - 1638.0f);   /* %10–90 aktarım, 0–10 kPa */
    return true;
}

static bool sdp_read(float *dp, float *t)
{
    static bool started;
    if (!started) {
        const uint8_t cmd[2] = { 0x36, 0x15 };            /* sürekli ölçüm, fark basıncı, ortalama */
        if (!hw_i2c_write(I2C1, BU_M_SDP_ADDR, cmd, 2, true)) return false;
        started = true; hw_delay_ms(50);
    }
    uint8_t b[9];
    if (!hw_i2c_read(I2C1, BU_M_SDP_ADDR, b, 9)) { started = false; return false; }
    for (int k = 0; k < 9; k += 3) if (bu_sensirion_crc(b + k, 2) != b[k + 2]) return false;
    int16_t dpr = (int16_t)((b[0] << 8) | b[1]), tr = (int16_t)((b[3] << 8) | b[4]), sc = (int16_t)((b[6] << 8) | b[7]);
    if (sc <= 0) return false;
    *dp = (float)dpr / (float)sc; *t = (float)tr / 200.0f;
    return true;
}

static void setup(void)
{
    hw_gpio_out('B', 3, true);                 /* LED_M yanık: başlıyor */
    hw_gpio_out('A', 4, true);                 /* ADS_CS */
    hw_gpio_out('A', 8, true);                 /* ADS_RST */
    hw_gpio_out('B', 1, false);                /* ADS_START */
    hw_gpio_in('B', 0, 0);                     /* ADS_DRDY */
    g_hse = hw_clock_init();
    hw_adc_init(ADC1);
    hw_gpio_af('A', 5, 0, false); hw_gpio_af('A', 6, 0, false); hw_gpio_af('A', 7, 0, false);
    hw_spi_init(SPI1, 2);                      /* 1 MHz */
    SPI1->CR1 &= ~SPI_CR1_SPE; SPI1->CR1 |= SPI_CR1_CPHA; SPI1->CR1 |= SPI_CR1_SPE;   /* ADS1292R: mod 1 */
    hw_gpio_af('B', 6, 6, true); hw_gpio_af('B', 7, 6, true);
    hw_i2c_init(I2C1);
    hw_gpio_af('A', 11, 3, false); hw_gpio_af('A', 12, 3, false);
    g_can_ok = hw_can_init(&g_can, FDCAN1, 0, hw_sysclk);
    hw_delay_ms(200);                          /* ADS1292R güç açılışı */
}

static void measure(void)
{
    g_vdda = hw_vref(ADC1);
    g_tmcu = hw_mcu_temp(ADC1, bu_is_nan(g_vdda) ? 3.3f : g_vdda);
    g_ads_id = ads_read_id();
    g_ads_ok = g_ads_id == BU_M_ADS1292R_ID;
    g_abp_ok = abp_read(&g_abp_kpa);
    g_sdp_ok = sdp_read(&g_dp, &g_sdp_t);
}

static bool all_ok(void)
{
    return g_ads_ok && g_abp_ok && g_sdp_ok && bu_judge(&BU_M_LVDDA, g_vdda) == BU_PASS && bu_judge(&BU_M_LTEMP, g_tmcu) == BU_PASS;
}

static int16_t i16(float v, float k) { return bu_is_nan(v) ? 0 : (int16_t)(v * k); }

int main(void)
{
    setup();
    uint32_t t = 0; uint8_t seq = 0;
    for (;;) {
        uint32_t now = hw_ms();
        uint16_t id; uint8_t d[8]; bu_hb_t h;
        while (g_can_ok && hw_can_recv(&g_can, &id, d))
            if (id == BU_CAN_HB_A && bu_hb_unpack(d, &h)) bu_peer_seen(&g_a, &h, now);
        if ((uint32_t)(now - t) >= 1000u) {
            t = now; seq++;
            measure();
            bu_hb_t m = { 'M', seq, (uint16_t)(now / 1000u), 0, (int8_t)g_tmcu, 0 };
            if (g_hse) m.status |= BU_ST_HSE;
            if (bu_judge(&BU_M_LVDDA, g_vdda) == BU_PASS) m.status |= BU_ST_RAILS;
            if (g_ads_ok) m.status |= BU_ST_PERIPH;
            if (bu_peer_alive(&g_a, now, 3000u)) m.status |= BU_ST_PEER1;
            if (g_abp_ok && g_sdp_ok) m.status |= BU_ST_SENS;
            uint8_t fl = (uint8_t)((g_abp_ok ? 1u : 0u) | (g_sdp_ok ? 2u : 0u));
            m.aux = (uint16_t)((g_ads_id << 8) | fl);
            bu_hb_pack(&m, d);
            hw_can_send(&g_can, BU_CAN_HB_M, d);
            int16_t a = i16(g_dp, 10.0f), b = i16(g_abp_kpa, 100.0f), c = i16(g_sdp_t, 10.0f);
            uint8_t md[8] = { (uint8_t)a, (uint8_t)(a >> 8), (uint8_t)b, (uint8_t)(b >> 8), (uint8_t)c, (uint8_t)(c >> 8), g_ads_id, fl };
            hw_can_send(&g_can, BU_CAN_M_DATA, md);
        }
        uint32_t per = !g_can_ok ? 0u : (all_ok() ? 500u : 100u);
        hw_gpio_write('B', 3, per == 0u ? true : (bool)((now / per) & 1u));
    }
}
