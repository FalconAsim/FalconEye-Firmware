/* Devreye alma – en küçük başlangıç kodu ve kesme tablosu (G474 / G0B1) */
#include "hw.h"

extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
int main(void);

void Reset_Handler(void);
void Default_Handler(void) { for (;;) { } }
void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));
#if HW_G4
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI15_10_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
#define N_IRQ 102
#else
void EXTI4_15_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
#define N_IRQ 32
#endif

typedef void (*vec_t)(void);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
#pragma GCC diagnostic ignored "-Wpedantic"
__attribute__((section(".isr_vector"), used))
const vec_t g_vectors[16 + N_IRQ] = {
    [0] = (vec_t)(uintptr_t)&_estack,
    [1 ... 15 + N_IRQ] = Default_Handler,     /* boş girişler: beklenmeyen kesme sonsuz döngüde kalır */
    [12] = 0, [13] = 0, [7] = 0, [8] = 0, [9] = 0, [10] = 0,   /* ayrılmış */
    [1] = Reset_Handler,
    [2] = NMI_Handler,
    [3] = HardFault_Handler,
#if HW_G4
    [4] = MemManage_Handler,
    [5] = BusFault_Handler,
    [6] = UsageFault_Handler,
    [16 + EXTI15_10_IRQn] = EXTI15_10_IRQHandler,
#else
    [16 + EXTI4_15_IRQn] = EXTI4_15_IRQHandler,
#endif
    [11] = SVC_Handler,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
};
#pragma GCC diagnostic pop

void Reset_Handler(void)
{
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss; d++) *d = 0u;
#if HW_G4
    SCB->CPACR |= (0xFu << 20);       /* FPU (CP10, CP11) */
    __DSB(); __ISB();
#endif
    main();
    for (;;) { }
}
