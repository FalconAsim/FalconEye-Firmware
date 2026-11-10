/* Kart A – BT817Q (EVE4) en küçük sürücü: kimlik, panel zamanlaması, görüntü listesi, dokunmatik ham değer, ses */
#ifndef BU_EVE_H
#define BU_EVE_H
#include <stdbool.h>
#include <stdint.h>

#define EVE_RAM_DL            0x300000u
#define EVE_REG_ID            0x302000u
#define EVE_REG_FRAMES        0x302004u
#define EVE_REG_FREQUENCY     0x30200Cu
#define EVE_REG_CPURESET      0x302020u
#define EVE_REG_HCYCLE        0x30202Cu
#define EVE_REG_HOFFSET       0x302030u
#define EVE_REG_HSIZE         0x302034u
#define EVE_REG_HSYNC0        0x302038u
#define EVE_REG_HSYNC1        0x30203Cu
#define EVE_REG_VCYCLE        0x302040u
#define EVE_REG_VOFFSET       0x302044u
#define EVE_REG_VSIZE         0x302048u
#define EVE_REG_VSYNC0        0x30204Cu
#define EVE_REG_VSYNC1        0x302050u
#define EVE_REG_DLSWAP        0x302054u
#define EVE_REG_DITHER        0x302060u
#define EVE_REG_SWIZZLE       0x302064u
#define EVE_REG_CSPREAD       0x302068u
#define EVE_REG_PCLK_POL      0x30206Cu
#define EVE_REG_PCLK          0x302070u
#define EVE_REG_VOL_SOUND     0x302084u
#define EVE_REG_SOUND         0x302088u
#define EVE_REG_PLAY          0x30208Cu
#define EVE_REG_GPIO_DIR      0x302090u
#define EVE_REG_GPIO          0x302094u
#define EVE_REG_PWM_HZ        0x3020D0u
#define EVE_REG_PWM_DUTY      0x3020D4u
#define EVE_REG_TOUCH_MODE    0x302104u
#define EVE_REG_TOUCH_RZTHRESH 0x302118u
#define EVE_REG_TOUCH_RAW_XY  0x30211Cu
#define EVE_REG_TOUCH_RZ      0x302120u
#define EVE_CHIP_ID_ADDR      0x0C0000u

/* Panel zamanlaması – SATOZ SAT070AT40 (800 × 480, 40p RGB). Değerler tipik AT070 sınıfı; veri sayfasıyla teyit. */
typedef struct {
    uint16_t hsize, hcycle, hoffset, hsync0, hsync1;
    uint16_t vsize, vcycle, voffset, vsync0, vsync1;
    uint8_t pclk_div, pclk_pol, swizzle, cspread, dither;
} eve_timing_t;
extern const eve_timing_t EVE_SATOZ_800x480;

typedef struct {
    bool ok;            /* REG_ID = 0x7C */
    bool ext_clk;       /* harici kristal ile başladı */
    uint32_t chip_id;   /* 0x0C0000: BT817 → bayt 0 0x08, bayt 1 0x17 */
    uint8_t reg_id;
} eve_info_t;

void eve_hw_init(void);                       /* SPI1, CS, PD uçları */
bool eve_start(eve_info_t *inf);              /* güç aç, saat, kimlik */
void eve_timing(const eve_timing_t *t, uint8_t backlight);
void eve_wr8(uint32_t a, uint8_t v);
void eve_wr16(uint32_t a, uint16_t v);
void eve_wr32(uint32_t a, uint32_t v);
uint32_t eve_rd32(uint32_t a);
uint8_t eve_rd8(uint32_t a);

/* Görüntü listesi (RAM_DL'ye sırayla yazılır) */
void eve_dl_begin(void);
void eve_dl(uint32_t cmd);
void eve_dl_end(void);                        /* DISPLAY + DLSWAP */
void eve_rect(int x0, int y0, int x1, int y1, uint32_t rgb);
void eve_text(int x, int y, int font, const char *s, uint32_t rgb);
void eve_test_pattern(const char *title);     /* renk çubukları + gri basamak + çerçeve */
void eve_beep(void);

#endif
