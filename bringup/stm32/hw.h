/*
 * Devreye alma – STM32G474 / STM32G0B1 için küçük yazmaç düzeyi sürücüler.
 * HAL / Cube kullanılmaz: yalnız CMSIS aygıt başlıkları (tools/fetch_cmsis.sh).
 * Saat: HSE 8 MHz (çalışmazsa HSI16); PLL yok. Bütün çevre birimler yoklamalı (kesme yalnız SysTick + takometre).
 */
#ifndef BU_HW_H
#define BU_HW_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#if defined(STM32G474xx)
#include "stm32g4xx.h"
#define HW_G4 1
#elif defined(STM32G0B1xx)
#include "stm32g0xx.h"
#define HW_G0 1
#else
#error "STM32G474xx ya da STM32G0B1xx tanımlı olmalı"
#endif

/* ---- saat ---- */
extern uint32_t hw_sysclk;
bool hw_clock_init(void);              /* true: HSE çalışıyor */
uint32_t hw_ms(void);
void hw_delay_ms(uint32_t ms);
void hw_delay_us(uint32_t us);
extern void (*hw_tick_hook)(void);     /* SysTick içinde, 1 ms (WDI gibi) */

/* ---- GPIO ---- */
GPIO_TypeDef *hw_port(char p);
void hw_gpio_out(char p, int pin, bool level);
void hw_gpio_in(char p, int pin, int pull);   /* 0 yok, 1 yukarı, 2 aşağı */
void hw_gpio_an(char p, int pin);
void hw_gpio_af(char p, int pin, int af, bool open_drain);
void hw_gpio_write(char p, int pin, bool level);
bool hw_gpio_read(char p, int pin);

/* ---- UART (konsol) ---- */
void hw_uart_init(USART_TypeDef *u, uint32_t baud);
void hw_uart_putc(USART_TypeDef *u, char c);
int  hw_uart_getc(USART_TypeDef *u);   /* yoksa −1 */
void hw_uart_puts(USART_TypeDef *u, const char *s);

/* ---- ADC (12 bit, tek çevrim, uzun örnekleme) ---- */
void hw_adc_init(ADC_TypeDef *a);
uint16_t hw_adc_read(ADC_TypeDef *a, int ch);
uint16_t hw_adc_read_avg(ADC_TypeDef *a, int ch, int n);

/* ---- SPI (8 bit, mod 0) ---- */
void hw_spi_init(SPI_TypeDef *s, int br_div_log2);   /* fPCLK / 2^(br+1) */
uint8_t hw_spi_xfer(SPI_TypeDef *s, uint8_t b);

/* ---- I2C (100 kHz) ---- */
void hw_i2c_init(I2C_TypeDef *i);
bool hw_i2c_write(I2C_TypeDef *i, uint8_t addr, const uint8_t *d, int n, bool stop);
bool hw_i2c_read(I2C_TypeDef *i, uint8_t addr, uint8_t *d, int n);
bool hw_i2c_probe(I2C_TypeDef *i, uint8_t addr);

/* ---- FDCAN (klasik CAN 500 kbit/s, yoklamalı) ---- */
typedef struct {
    FDCAN_GlobalTypeDef *can;
    uint32_t ram;          /* ileti RAM'i başlangıcı */
    uint32_t tx, rx_err;
} hw_can_t;
bool hw_can_init(hw_can_t *c, FDCAN_GlobalTypeDef *can, int index, uint32_t kernel_hz);
bool hw_can_send(hw_can_t *c, uint16_t id, const uint8_t d[8]);
bool hw_can_recv(hw_can_t *c, uint16_t *id, uint8_t d[8]);
uint32_t hw_can_psr(hw_can_t *c);
uint32_t hw_can_ecr(hw_can_t *c);

/* ---- iç ölçümler ---- */
float hw_vref(ADC_TypeDef *a);          /* VREF+ / VDDA (V), VREFINT ile */
float hw_mcu_temp(ADC_TypeDef *a, float vref);

/* ---- metin ---- */
void hw_printf_init(USART_TypeDef *u);
void out(const char *s);               /* konsola yaz */
void outln(const char *s);
void out_f(float v, int dec);
void out_u(uint32_t v);
void out_hex(uint32_t v, int digits);

#endif
