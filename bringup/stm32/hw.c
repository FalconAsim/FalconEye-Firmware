/* Devreye alma – STM32G474 / STM32G0B1 yazmaç düzeyi sürücüler (açıklama: hw.h) */
#include "hw.h"
#include "bu_core.h"

/* Fabrika kalibrasyon adresleri (G4 ve G0B1 aynı; kalibrasyon VREF+ = 3,0 V) */
#define VREFINT_CAL   (*(const uint16_t *)0x1FFF75AAu)
#define TS_CAL1       (*(const uint16_t *)0x1FFF75A8u)   /* 30 °C */
#define TS_CAL2       (*(const uint16_t *)0x1FFF75CAu)   /* 130 °C */
#define CAL_V 3.0f
#if HW_G4
#define CH_TEMP 16
#define CH_VREFINT 18
#else
#define CH_TEMP 12
#define CH_VREFINT 13
#endif

uint32_t hw_sysclk = 16000000u;
static volatile uint32_t g_ms;
void (*hw_tick_hook)(void);

void SysTick_Handler(void)
{
    g_ms++;
    if (hw_tick_hook) hw_tick_hook();
}

uint32_t hw_ms(void) { return g_ms; }
void hw_delay_ms(uint32_t ms) { uint32_t t = g_ms; while ((uint32_t)(g_ms - t) < ms) { } }
void hw_delay_us(uint32_t us)
{
    volatile uint32_t n = us * (hw_sysclk / 4000000u) + 1u;   /* kaba, döngü başına ≈ 4 çevrim */
    while (n--) { }
}

bool hw_clock_init(void)
{
    bool ok = false;
    RCC->CR |= RCC_CR_HSEON;
    for (volatile uint32_t i = 0; i < 400000u; i++)
        if (RCC->CR & RCC_CR_HSERDY) { ok = true; break; }
    if (ok) {
#if HW_G4
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_1;          /* 10: HSE */
        while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_1) { }
#else
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_0;          /* 001: HSE */
        while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_0) { }
#endif
        hw_sysclk = 8000000u;
    } else {
        RCC->CR &= ~RCC_CR_HSEON;
        hw_sysclk = 16000000u;                                           /* HSI16 */
    }
    SysTick_Config(hw_sysclk / 1000u);
    return ok;
}

/* ---------------- GPIO ---------------- */
GPIO_TypeDef *hw_port(char p) { return (GPIO_TypeDef *)(GPIOA_BASE + (uint32_t)(p - 'A') * 0x400u); }

static void port_clk(char p)
{
#if HW_G4
    RCC->AHB2ENR |= 1u << (p - 'A');
#else
    RCC->IOPENR |= 1u << (p - 'A');
#endif
    (void)RCC->CR;
}

static void mode(GPIO_TypeDef *g, int pin, uint32_t m)
{
    g->MODER = (g->MODER & ~(3u << (2 * pin))) | (m << (2 * pin));
}

void hw_gpio_out(char p, int pin, bool level)
{
    port_clk(p);
    GPIO_TypeDef *g = hw_port(p);
    hw_gpio_write(p, pin, level);            /* önce seviye, sonra çıkış: açılışta darbe yok */
    g->OTYPER &= ~(1u << pin);
    mode(g, pin, 1u);
}

void hw_gpio_in(char p, int pin, int pull)
{
    port_clk(p);
    GPIO_TypeDef *g = hw_port(p);
    g->PUPDR = (g->PUPDR & ~(3u << (2 * pin))) | ((uint32_t)pull << (2 * pin));
    mode(g, pin, 0u);
}

void hw_gpio_an(char p, int pin) { port_clk(p); mode(hw_port(p), pin, 3u); }

void hw_gpio_af(char p, int pin, int af, bool od)
{
    port_clk(p);
    GPIO_TypeDef *g = hw_port(p);
    g->AFR[pin >> 3] = (g->AFR[pin >> 3] & ~(0xFu << (4 * (pin & 7)))) | ((uint32_t)af << (4 * (pin & 7)));
    if (od) g->OTYPER |= 1u << pin; else g->OTYPER &= ~(1u << pin);
    g->OSPEEDR = (g->OSPEEDR & ~(3u << (2 * pin))) | (2u << (2 * pin));
    mode(g, pin, 2u);
}

void hw_gpio_write(char p, int pin, bool level) { hw_port(p)->BSRR = level ? (1u << pin) : (1u << (pin + 16)); }
bool hw_gpio_read(char p, int pin) { return (hw_port(p)->IDR >> pin) & 1u; }

/* ---------------- UART ---------------- */
void hw_uart_init(USART_TypeDef *u, uint32_t baud)
{
#if HW_G4
    if (u == USART2) RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;
    else if (u == USART3) RCC->APB1ENR1 |= RCC_APB1ENR1_USART3EN;
#else
    if (u == USART2) RCC->APBENR1 |= RCC_APBENR1_USART2EN;
#endif
    u->CR1 = 0;
    u->BRR = (hw_sysclk + baud / 2u) / baud;
    u->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void hw_uart_putc(USART_TypeDef *u, char c)
{
    while (!(u->ISR & USART_ISR_TXE_TXFNF)) { }
    u->TDR = (uint8_t)c;
}

int hw_uart_getc(USART_TypeDef *u)
{
    if (u->ISR & USART_ISR_ORE) u->ICR = USART_ICR_ORECF;
    if (u->ISR & USART_ISR_RXNE_RXFNE) return (int)(u->RDR & 0xFFu);
    return -1;
}

void hw_uart_puts(USART_TypeDef *u, const char *s)
{
    while (*s) { if (*s == '\n') hw_uart_putc(u, '\r'); hw_uart_putc(u, *s++); }
}

/* ---------------- ADC ---------------- */
void hw_adc_init(ADC_TypeDef *a)
{
#if HW_G4
    if (a == ADC1 || a == ADC2) {
        RCC->AHB2ENR |= RCC_AHB2ENR_ADC12EN; (void)RCC->AHB2ENR;
        ADC12_COMMON->CCR = (ADC12_COMMON->CCR & ~ADC_CCR_CKMODE) | ADC_CCR_CKMODE_0 | ADC_CCR_VREFEN | ADC_CCR_VSENSESEL;
    } else {
        RCC->AHB2ENR |= RCC_AHB2ENR_ADC345EN; (void)RCC->AHB2ENR;
        ADC345_COMMON->CCR = (ADC345_COMMON->CCR & ~ADC_CCR_CKMODE) | ADC_CCR_CKMODE_0;
    }
    a->CR &= ~ADC_CR_DEEPPWD;
    a->CR |= ADC_CR_ADVREGEN;
    hw_delay_us(50);
    a->CR &= ~ADC_CR_ADCALDIF;
    a->CR |= ADC_CR_ADCAL;
    while (a->CR & ADC_CR_ADCAL) { }
    hw_delay_us(10);
    a->SMPR1 = 0x3FFFFFFFu;     /* kanal 0–9: 640,5 çevrim */
    a->SMPR2 = 0x07FFFFFFu;     /* kanal 10–18 */
    a->ISR = ADC_ISR_ADRDY;
    a->CR |= ADC_CR_ADEN;
    while (!(a->ISR & ADC_ISR_ADRDY)) { }
#else
    RCC->APBENR2 |= RCC_APBENR2_ADCEN; (void)RCC->APBENR2;
    a->CFGR2 = ADC_CFGR2_CKMODE_0;                     /* PCLK/2 */
    a->CR |= ADC_CR_ADVREGEN;
    hw_delay_us(50);
    a->CR |= ADC_CR_ADCAL;
    while (a->CR & ADC_CR_ADCAL) { }
    hw_delay_us(10);
    ADC1_COMMON->CCR |= ADC_CCR_VREFEN | ADC_CCR_TSEN;
    a->SMPR = ADC_SMPR_SMP1;                           /* 160,5 çevrim, bütün kanallar */
    a->ISR = ADC_ISR_ADRDY;
    a->CR |= ADC_CR_ADEN;
    while (!(a->ISR & ADC_ISR_ADRDY)) { }
#endif
}

uint16_t hw_adc_read(ADC_TypeDef *a, int ch)
{
#if HW_G4
    a->SQR1 = (uint32_t)ch << ADC_SQR1_SQ1_Pos;
#else
    a->ISR = ADC_ISR_CCRDY;
    a->CHSELR = 1u << ch;
    while (!(a->ISR & ADC_ISR_CCRDY)) { }
#endif
    a->ISR = ADC_ISR_EOC | ADC_ISR_OVR;
    a->CR |= ADC_CR_ADSTART;
    while (!(a->ISR & ADC_ISR_EOC)) { }
    return (uint16_t)a->DR;
}

uint16_t hw_adc_read_avg(ADC_TypeDef *a, int ch, int n)
{
    uint32_t s = 0;
    for (int i = 0; i < n; i++) s += hw_adc_read(a, ch);
    return (uint16_t)((s + (uint32_t)n / 2u) / (uint32_t)n);
}

float hw_vref(ADC_TypeDef *a)
{
    return bu_vref_from_vrefint(VREFINT_CAL, hw_adc_read_avg(a, CH_VREFINT, 16), CAL_V);
}

float hw_mcu_temp(ADC_TypeDef *a, float vref)
{
    uint16_t code = hw_adc_read_avg(a, CH_TEMP, 16);
    uint16_t c1 = TS_CAL1, c2 = TS_CAL2;
    if (c2 == 0xFFFFu || c2 <= c1) {                   /* TS_CAL2 yoksa: 2,5 mV/°C ortalama eğim */
        float v = (float)code * vref / 4095.0f, v30 = (float)c1 * CAL_V / 4095.0f;
        return 30.0f + (v - v30) / 0.0025f;
    }
    return bu_mcu_temp_c(code, vref, c1, c2, 30.0f, 130.0f, CAL_V);
}

/* ---------------- SPI ---------------- */
void hw_spi_init(SPI_TypeDef *s, int br)
{
#if HW_G4
    if (s == SPI2) RCC->APB1ENR1 |= RCC_APB1ENR1_SPI2EN;
    else if (s == SPI1) RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
#else
    if (s == SPI1) RCC->APBENR2 |= RCC_APBENR2_SPI1EN;
#endif
    s->CR1 = 0;
    s->CR2 = (7u << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;
    s->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | ((uint32_t)br << SPI_CR1_BR_Pos);
    s->CR1 |= SPI_CR1_SPE;
}

uint8_t hw_spi_xfer(SPI_TypeDef *s, uint8_t b)
{
    while (!(s->SR & SPI_SR_TXE)) { }
    *(volatile uint8_t *)&s->DR = b;
    while (!(s->SR & SPI_SR_RXNE)) { }
    return *(volatile uint8_t *)&s->DR;
}

/* ---------------- I2C ---------------- */
void hw_i2c_init(I2C_TypeDef *i)
{
#if HW_G4
    if (i == I2C1) RCC->APB1ENR1 |= RCC_APB1ENR1_I2C1EN;
#else
    if (i == I2C1) RCC->APBENR1 |= RCC_APBENR1_I2C1EN;
#endif
    i->CR1 = 0;
    i->TIMINGR = hw_sysclk == 8000000u ? 0x10420F13u : 0x30420F13u;   /* 100 kHz (RM tablo) */
    i->CR1 = I2C_CR1_PE;
}

static bool i2c_wait(I2C_TypeDef *i, uint32_t flag)
{
    uint32_t t = hw_ms();
    while (!(i->ISR & flag)) {
        if (i->ISR & I2C_ISR_NACKF) return false;
        if ((uint32_t)(hw_ms() - t) > 20u) return false;
    }
    return true;
}

static void i2c_recover(I2C_TypeDef *i)
{
    uint32_t t = hw_ms();
    while (!(i->ISR & I2C_ISR_STOPF) && (uint32_t)(hw_ms() - t) < 5u) { }
    i->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
    if (i->ISR & I2C_ISR_BUSY) { i->CR1 = 0; i->CR1 = I2C_CR1_PE; }
}

bool hw_i2c_write(I2C_TypeDef *i, uint8_t addr, const uint8_t *d, int n, bool stop)
{
    i->CR2 = ((uint32_t)addr << 1) | ((uint32_t)n << I2C_CR2_NBYTES_Pos) | (stop ? I2C_CR2_AUTOEND : 0u) | I2C_CR2_START;
    for (int k = 0; k < n; k++) {
        if (!i2c_wait(i, I2C_ISR_TXIS)) { i2c_recover(i); return false; }
        i->TXDR = d[k];
    }
    if (stop) {
        if (!i2c_wait(i, I2C_ISR_STOPF)) { i2c_recover(i); return false; }
        i->ICR = I2C_ICR_STOPCF;
    } else if (!i2c_wait(i, I2C_ISR_TC)) { i2c_recover(i); return false; }
    return true;
}

bool hw_i2c_read(I2C_TypeDef *i, uint8_t addr, uint8_t *d, int n)
{
    i->CR2 = ((uint32_t)addr << 1) | I2C_CR2_RD_WRN | ((uint32_t)n << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND | I2C_CR2_START;
    for (int k = 0; k < n; k++) {
        if (!i2c_wait(i, I2C_ISR_RXNE)) { i2c_recover(i); return false; }
        d[k] = (uint8_t)i->RXDR;
    }
    if (!i2c_wait(i, I2C_ISR_STOPF)) { i2c_recover(i); return false; }
    i->ICR = I2C_ICR_STOPCF;
    return true;
}

bool hw_i2c_probe(I2C_TypeDef *i, uint8_t addr) { return hw_i2c_write(i, addr, 0, 0, true); }

/* ---------------- FDCAN ---------------- */
/* Basitleştirilmiş ileti RAM'i (G4 / G0 aynı), örnek başına 0x350 bayt:
 * 28 std süzgeç · 8 ext süzgeç · RX FIFO0 3×72 · RX FIFO1 3×72 · TX olay 3×8 · TX tampon 3×72 */
#define RAM_RXF0 0x0B0u
#define RAM_TXB  0x278u
#define ELEM     72u

bool hw_can_init(hw_can_t *c, FDCAN_GlobalTypeDef *can, int index, uint32_t kernel_hz)
{
    c->can = can; c->ram = SRAMCAN_BASE + (uint32_t)index * 0x350u; c->tx = 0; c->rx_err = 0;
#if HW_G4
    RCC->APB1ENR1 |= RCC_APB1ENR1_FDCANEN;
    if (kernel_hz != 8000000u) RCC->CCIPR = (RCC->CCIPR & ~RCC_CCIPR_FDCANSEL) | RCC_CCIPR_FDCANSEL_1;   /* PCLK */
#else
    RCC->APBENR1 |= RCC_APBENR1_FDCANEN;                       /* çekirdek saati varsayılan PCLK = SYSCLK */
#endif
    (void)RCC->CR;
    for (uint32_t a = 0; a < 0x350u; a += 4u) *(volatile uint32_t *)(c->ram + a) = 0u;
    can->CCCR |= FDCAN_CCCR_INIT;
    uint32_t t = hw_ms();
    while (!(can->CCCR & FDCAN_CCCR_INIT)) if ((uint32_t)(hw_ms() - t) > 10u) return false;
    can->CCCR |= FDCAN_CCCR_CCE;
    can->CCCR &= ~(FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE | FDCAN_CCCR_MON | FDCAN_CCCR_TEST);
    /* 500 kbit/s, 16 tq, örnekleme %87,5: 1 + 13 + 2 */
    uint32_t brp = kernel_hz / 8000000u;            /* 8 MHz → 1, 16 MHz → 2 */
    can->NBTP = (1u << FDCAN_NBTP_NSJW_Pos) | ((brp - 1u) << FDCAN_NBTP_NBRP_Pos) | (12u << FDCAN_NBTP_NTSEG1_Pos) | (1u << FDCAN_NBTP_NTSEG2_Pos);
    can->RXGFC = 0u;                                /* eşleşmeyen std/ext → FIFO0, süzgeç yok */
    can->TXBC = 0u;                                 /* TX FIFO */
    can->CCCR &= ~FDCAN_CCCR_INIT;
    t = hw_ms();
    while (can->CCCR & FDCAN_CCCR_INIT) if ((uint32_t)(hw_ms() - t) > 10u) return false;
    return true;
}

bool hw_can_send(hw_can_t *c, uint16_t id, const uint8_t d[8])
{
    if (c->can->TXFQS & FDCAN_TXFQS_TFQF) return false;
    uint32_t put = (c->can->TXFQS & FDCAN_TXFQS_TFQPI) >> FDCAN_TXFQS_TFQPI_Pos;
    volatile uint32_t *e = (volatile uint32_t *)(c->ram + RAM_TXB + put * ELEM);
    e[0] = (uint32_t)(id & 0x7FFu) << 18;
    e[1] = 8u << 16;
    e[2] = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    e[3] = (uint32_t)d[4] | ((uint32_t)d[5] << 8) | ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24);
    c->can->TXBAR = 1u << put;
    c->tx++;
    return true;
}

bool hw_can_recv(hw_can_t *c, uint16_t *id, uint8_t d[8])
{
    uint32_t s = c->can->RXF0S;
    if ((s & FDCAN_RXF0S_F0FL) == 0u) return false;
    uint32_t gi = (s & FDCAN_RXF0S_F0GI) >> FDCAN_RXF0S_F0GI_Pos;
    volatile uint32_t *e = (volatile uint32_t *)(c->ram + RAM_RXF0 + gi * ELEM);
    uint32_t w0 = e[0], w2 = e[2], w3 = e[3];
    c->can->RXF0A = gi;
    if (w0 & (1u << 30)) return false;              /* genişletilmiş kimlik kullanılmıyor */
    *id = (uint16_t)((w0 >> 18) & 0x7FFu);
    for (int k = 0; k < 4; k++) { d[k] = (uint8_t)(w2 >> (8 * k)); d[4 + k] = (uint8_t)(w3 >> (8 * k)); }
    return true;
}

uint32_t hw_can_psr(hw_can_t *c) { return c->can->PSR; }
uint32_t hw_can_ecr(hw_can_t *c) { return c->can->ECR; }

/* ---------------- metin ---------------- */
static USART_TypeDef *g_con;
void hw_printf_init(USART_TypeDef *u) { g_con = u; }
void out(const char *s) { if (g_con) hw_uart_puts(g_con, s); }
void outln(const char *s) { out(s); out("\n"); }
void out_f(float v, int dec) { char b[16]; bu_fmt_f(b, sizeof b, v, dec); out(b); }
void out_u(uint32_t v) { char b[12]; int k = 0; char t[12]; do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v); int j = 0; while (k) b[j++] = t[--k]; b[j] = 0; out(b); }
void out_hex(uint32_t v, int digits) { char b[12]; bu_fmt_hex(b, sizeof b, v, digits); out(b); }
