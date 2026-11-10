/* Kart A – BT817Q en küçük sürücü (açıklama: eve.h). SPI1 PA5/PA6/PA7, CS PA4, PD PB1, INT PB0. */
#include "eve.h"
#include "hw.h"

const eve_timing_t EVE_SATOZ_800x480 = {
    800, 928, 88, 0, 48,
    480, 525, 32, 0, 3,
    2, 1, 0, 0, 1,             /* PCLK = 60 MHz / 2 = 30 MHz → ≈ 61,6 Hz */
};

static void cs(bool lo) { hw_gpio_write('A', 4, !lo); }
static uint32_t g_dl;

void eve_hw_init(void)
{
    hw_gpio_out('A', 4, true);          /* CS */
    hw_gpio_out('B', 1, false);         /* PD: kapalı */
    hw_gpio_in('B', 0, 0);              /* INT (10 k yukarı) */
    hw_gpio_af('A', 5, 0, false); hw_gpio_af('A', 6, 0, false); hw_gpio_af('A', 7, 0, false);
    hw_spi_init(SPI1, 1);               /* 8 MHz / 4 = 2 MHz (< 11 MHz başlangıç sınırı) */
}

static void host(uint8_t cmd, uint8_t par)
{
    cs(true); hw_spi_xfer(SPI1, cmd); hw_spi_xfer(SPI1, par); hw_spi_xfer(SPI1, 0); cs(false);
}

static void addr_wr(uint32_t a) { hw_spi_xfer(SPI1, (uint8_t)(0x80u | ((a >> 16) & 0x3Fu))); hw_spi_xfer(SPI1, (uint8_t)(a >> 8)); hw_spi_xfer(SPI1, (uint8_t)a); }
static void addr_rd(uint32_t a) { hw_spi_xfer(SPI1, (uint8_t)((a >> 16) & 0x3Fu)); hw_spi_xfer(SPI1, (uint8_t)(a >> 8)); hw_spi_xfer(SPI1, (uint8_t)a); hw_spi_xfer(SPI1, 0); }

void eve_wr8(uint32_t a, uint8_t v) { cs(true); addr_wr(a); hw_spi_xfer(SPI1, v); cs(false); }
void eve_wr16(uint32_t a, uint16_t v) { cs(true); addr_wr(a); hw_spi_xfer(SPI1, (uint8_t)v); hw_spi_xfer(SPI1, (uint8_t)(v >> 8)); cs(false); }
void eve_wr32(uint32_t a, uint32_t v)
{
    cs(true); addr_wr(a);
    for (int k = 0; k < 4; k++) hw_spi_xfer(SPI1, (uint8_t)(v >> (8 * k)));
    cs(false);
}
uint8_t eve_rd8(uint32_t a) { cs(true); addr_rd(a); uint8_t v = hw_spi_xfer(SPI1, 0); cs(false); return v; }
uint32_t eve_rd32(uint32_t a)
{
    uint32_t v = 0;
    cs(true); addr_rd(a);
    for (int k = 0; k < 4; k++) v |= (uint32_t)hw_spi_xfer(SPI1, 0) << (8 * k);
    cs(false);
    return v;
}

static bool try_start(bool ext)
{
    hw_gpio_write('B', 1, false); hw_delay_ms(20);
    hw_gpio_write('B', 1, true);  hw_delay_ms(20);
    if (ext) host(0x44, 0);              /* CLKEXT */
    host(0x00, 0);                       /* ACTIVE */
    uint32_t t = hw_ms();
    while ((uint32_t)(hw_ms() - t) < 400u) {
        if (eve_rd8(EVE_REG_ID) == 0x7Cu) {
            while ((eve_rd8(EVE_REG_CPURESET) & 0x07u) && (uint32_t)(hw_ms() - t) < 600u) { }
            return true;
        }
        hw_delay_ms(5);
    }
    return false;
}

bool eve_start(eve_info_t *inf)
{
    inf->ext_clk = true;
    inf->ok = try_start(true);
    if (!inf->ok) { inf->ext_clk = false; inf->ok = try_start(false); }   /* kristal yoksa iç osilatör */
    inf->reg_id = eve_rd8(EVE_REG_ID);
    inf->chip_id = inf->ok ? eve_rd32(EVE_CHIP_ID_ADDR) : 0u;
    return inf->ok;
}

void eve_timing(const eve_timing_t *t, uint8_t bl)
{
    eve_wr8(EVE_REG_PCLK, 0);
    eve_wr16(EVE_REG_HSIZE, t->hsize);   eve_wr16(EVE_REG_HCYCLE, t->hcycle); eve_wr16(EVE_REG_HOFFSET, t->hoffset);
    eve_wr16(EVE_REG_HSYNC0, t->hsync0); eve_wr16(EVE_REG_HSYNC1, t->hsync1);
    eve_wr16(EVE_REG_VSIZE, t->vsize);   eve_wr16(EVE_REG_VCYCLE, t->vcycle); eve_wr16(EVE_REG_VOFFSET, t->voffset);
    eve_wr16(EVE_REG_VSYNC0, t->vsync0); eve_wr16(EVE_REG_VSYNC1, t->vsync1);
    eve_wr8(EVE_REG_SWIZZLE, t->swizzle); eve_wr8(EVE_REG_PCLK_POL, t->pclk_pol);
    eve_wr8(EVE_REG_CSPREAD, t->cspread); eve_wr8(EVE_REG_DITHER, t->dither);
    eve_dl_begin(); eve_dl(0x02000000u); eve_dl(0x26000007u); eve_dl_end();   /* siyah */
    eve_wr8(EVE_REG_GPIO_DIR, (uint8_t)(eve_rd8(EVE_REG_GPIO_DIR) | 0x80u));
    eve_wr8(EVE_REG_GPIO, (uint8_t)(eve_rd8(EVE_REG_GPIO) | 0x80u));           /* DISP */
    eve_wr8(EVE_REG_PCLK, t->pclk_div);
    eve_wr16(EVE_REG_PWM_HZ, 4000);
    eve_wr8(EVE_REG_PWM_DUTY, bl);                                              /* 0..128 */
    eve_wr8(EVE_REG_TOUCH_MODE, 3);                                             /* sürekli */
    eve_wr16(EVE_REG_TOUCH_RZTHRESH, 1200);
}

/* ---- görüntü listesi ---- */
void eve_dl_begin(void) { g_dl = 0; }
void eve_dl(uint32_t c) { if (g_dl < 8192u - 8u) { eve_wr32(EVE_RAM_DL + g_dl, c); g_dl += 4u; } }
void eve_dl_end(void) { eve_dl(0x00000000u); eve_wr8(EVE_REG_DLSWAP, 2); }

#define COLOR(rgb)        (0x04000000u | ((rgb) & 0xFFFFFFu))
#define BEGIN(p)          (0x1F000000u | (p))
#define END_()            0x21000000u
#define V2F(x, y)         (0x40000000u | (((uint32_t)(x) * 16u & 0x7FFFu) << 15) | ((uint32_t)(y) * 16u & 0x7FFFu))
#define HANDLE(h)         (0x05000000u | (h))
#define CELL(c)           (0x06000000u | (c))
#define LINE_W(w)         (0x0E000000u | (w))

void eve_rect(int x0, int y0, int x1, int y1, uint32_t rgb)
{
    eve_dl(COLOR(rgb)); eve_dl(BEGIN(9)); eve_dl(V2F(x0, y0)); eve_dl(V2F(x1, y1)); eve_dl(END_());
}

void eve_text(int x, int y, int font, const char *s, uint32_t rgb)
{
    int adv = font >= 30 ? 22 : font >= 28 ? 15 : font >= 26 ? 11 : 9;
    eve_dl(COLOR(rgb)); eve_dl(BEGIN(1)); eve_dl(HANDLE((uint32_t)font));
    for (; *s; s++, x += adv) {
        if (*s == ' ') continue;
        eve_dl(CELL((uint8_t)*s)); eve_dl(V2F(x, y));
    }
    eve_dl(END_());
}

void eve_test_pattern(const char *title)
{
    static const uint32_t bars[8] = { 0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000 };
    eve_dl_begin();
    eve_dl(0x02000000u); eve_dl(0x26000007u);
    for (int i = 0; i < 8; i++) eve_rect(i * 100, 0, i * 100 + 100, 200, bars[i]);
    for (int i = 0; i < 16; i++) { uint32_t g = (uint32_t)(i * 17); eve_rect(i * 50, 200, i * 50 + 50, 260, (g << 16) | (g << 8) | g); }
    eve_dl(COLOR(0xFFFFFF)); eve_dl(LINE_W(16)); eve_dl(BEGIN(4));
    eve_dl(V2F(1, 1)); eve_dl(V2F(798, 1)); eve_dl(V2F(798, 478)); eve_dl(V2F(1, 478)); eve_dl(V2F(1, 1)); eve_dl(END_());
    eve_text(16, 276, 30, title, 0xFFFFFF);
}

void eve_beep(void)
{
    eve_wr8(EVE_REG_VOL_SOUND, 0xFF);
    eve_wr16(EVE_REG_SOUND, (uint16_t)((69u << 8) | 0x02u));   /* kare dalga, La4 */
    eve_wr8(EVE_REG_PLAY, 1);
    hw_delay_ms(300);
    eve_wr16(EVE_REG_SOUND, 0x0000u); eve_wr8(EVE_REG_PLAY, 1);
}
