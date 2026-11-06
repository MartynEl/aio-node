/* src/platform/stm32f1/startup_stm32f1.c */
#include <stdint.h>

/*
 * External symbols defined in Linker Script (.ld).
 */
extern uint32_t _estack;   /* Top of Stack */
extern uint32_t _sidata;   /* Load Address of .data segment (in Flash) */
extern uint32_t _sdata;    /* Start Address of .data segment (in RAM) */
extern uint32_t _edata;    /* End Address of .data segment (in RAM) */
extern uint32_t _sbss;     /* Start Address of .bss segment (in RAM) */
extern uint32_t _ebss;     /* End Address of .bss segment (in RAM) */

/* Forward declarations */
extern int main(void);
void Reset_Handler(void);

/*
 * Default Handler for unimplemented interrupts.
 */
void Default_Handler(void)
{
    for (;;) {}
}

/* Weak aliases for standard Cortex-M3 system exceptions. */
void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVCall_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

/*
 * Peripheral Interrupt Handlers declared in our drivers.
 */
/* From src/platform/stm32f1/adc_dma.c */
void DMA1_Channel1_IRQHandler(void);

/* From src/platform/stm32f1/platform_stm32f1.c */
void PVD_IRQHandler(void);

/* From src/platform/stm32f1/uart1.c */
void USART1_IRQHandler(void);

/**
 * @brief Cortex-M3 Vector Table for STM32F103 medium-density.
 *
 * IMPORTANT:
 * STM32F103 USART1 is IRQ 37.
 * Vector index = 16 + IRQ = 53.
 *
 * Previous table incorrectly placed USART1 at index 55,
 * which corresponds to USART3 / IRQ 39.
 */
__attribute__((section(".isr_vector"), used))
void (* const g_isr_vector[])(void) = {
    /* --- Core Exceptions --- */
    [0]  = (void (*)(void))&_estack,       /* Initial Stack Pointer */
    [1]  = Reset_Handler,                  /* Reset Handler */
    [2]  = NMI_Handler,                    /* Non-Maskable Interrupt */
    [3]  = HardFault_Handler,              /* Hard Fault */
    [4]  = MemManage_Handler,              /* Memory Management Fault */
    [5]  = BusFault_Handler,               /* Bus Fault */
    [6]  = UsageFault_Handler,             /* Usage Fault */
    [7]  = 0,                              /* Reserved */
    [8]  = 0,                              /* Reserved */
    [9]  = 0,                              /* Reserved */
    [10] = 0,                              /* Reserved */
    [11] = SVCall_Handler,                 /* Supervisor Call */
    [12] = DebugMon_Handler,               /* Debug Monitor */
    [13] = 0,                              /* Reserved */
    [14] = PendSV_Handler,                 /* Pendable Service Request */
    [15] = SysTick_Handler,                /* System Tick Timer */

    /* --- External Interrupts (IRQ 0..42 for STM32F103 MD) --- */
    /* Index = 16 + IRQ_Number */
    [16] = Default_Handler,                /* WWDG (IRQ 0) */
    [17] = PVD_IRQHandler,                 /* PVD (IRQ 1) */
    [18] = Default_Handler,                /* TAMPER (IRQ 2) */
    [19] = Default_Handler,                /* RTC (IRQ 3) */
    [20] = Default_Handler,                /* FLASH (IRQ 4) */
    [21] = Default_Handler,                /* RCC (IRQ 5) */
    [22] = Default_Handler,                /* EXTI0 (IRQ 6) */
    [23] = Default_Handler,                /* EXTI1 (IRQ 7) */
    [24] = Default_Handler,                /* EXTI2 (IRQ 8) */
    [25] = Default_Handler,                /* EXTI3 (IRQ 9) */
    [26] = Default_Handler,                /* EXTI4 (IRQ 10) */
    [27] = DMA1_Channel1_IRQHandler,       /* DMA1 Channel 1 (IRQ 11) */
    [28] = Default_Handler,                /* DMA1 Channel 2 (IRQ 12) */
    [29] = Default_Handler,                /* DMA1 Channel 3 (IRQ 13) */
    [30] = Default_Handler,                /* DMA1 Channel 4 (IRQ 14) */
    [31] = Default_Handler,                /* DMA1 Channel 5 (IRQ 15) */
    [32] = Default_Handler,                /* DMA1 Channel 6 (IRQ 16) */
    [33] = Default_Handler,                /* DMA1 Channel 7 (IRQ 17) */
    [34] = Default_Handler,                /* ADC1_2 (IRQ 18) */
    [35] = Default_Handler,                /* USB_HP_CAN_TX (IRQ 19) */
    [36] = Default_Handler,                /* USB_LP_CAN_RX0 (IRQ 20) */
    [37] = Default_Handler,                /* CAN_RX1 (IRQ 21) */
    [38] = Default_Handler,                /* CAN_SCE (IRQ 22) */
    [39] = Default_Handler,                /* EXTI9_5 (IRQ 23) */
    [40] = Default_Handler,                /* TIM1_BRK (IRQ 24) */
    [41] = Default_Handler,                /* TIM1_UP (IRQ 25) */
    [42] = Default_Handler,                /* TIM1_TRG_COM (IRQ 26) */
    [43] = Default_Handler,                /* TIM1_CC (IRQ 27) */
    [44] = Default_Handler,                /* TIM2 (IRQ 28) */
    [45] = Default_Handler,                /* TIM3 (IRQ 29) */
    [46] = Default_Handler,                /* TIM4 (IRQ 30) */
    [47] = Default_Handler,                /* I2C1_EV (IRQ 31) */
    [48] = Default_Handler,                /* I2C1_ER (IRQ 32) */
    [49] = Default_Handler,                /* I2C2_EV (IRQ 33) */
    [50] = Default_Handler,                /* I2C2_ER (IRQ 34) */
    [51] = Default_Handler,                /* SPI1 (IRQ 35) */
    [52] = Default_Handler,                /* SPI2 (IRQ 36) */
    [53] = USART1_IRQHandler,              /* USART1 (IRQ 37) */
    [54] = Default_Handler,                /* USART2 (IRQ 38) */
    [55] = Default_Handler,                /* USART3 (IRQ 39) */
    [56] = Default_Handler,                /* EXTI15_10 (IRQ 40) */
    [57] = Default_Handler,                /* RTCAlarm (IRQ 41) */
    [58] = Default_Handler,                /* USBWakeUp (IRQ 42) */

    /* Pad remaining vectors up to typical max size */
    [59 ... 83] = Default_Handler
};

/**
 * @brief Hardware Reset Entry Point.
 */
void Reset_Handler(void)
{
    /* 1. Copy initialized data from Flash to RAM (.data section) */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;

    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* 2. Zero-initialize uninitialized data in RAM (.bss section) */
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0u;
    }

    /* 3. Set Vector Table Offset Register (VTOR) explicitly. */
    volatile uint32_t *vtor = (volatile uint32_t *)0xE000ED08UL;
    *vtor = 0x08000000UL;

    /* 4. Jump to application entry point */
    (void)main();

    /* Should never reach here. If it does, trap execution. */
    for (;;) {}
}