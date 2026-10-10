#include "platform/adc_port.h"
#include <stddef.h>

/* --- Hardware Peripheral Registers Map Addresses (STM32F103) --- */
#define REG_RCC_BASE         0x40021000UL
#define REG_RCC_AHBENR       (*(volatile uint32_t *)(REG_RCC_BASE + 0x14UL))
#define REG_RCC_APB2ENR      (*(volatile uint32_t *)(REG_RCC_BASE + 0x18UL))
#define REG_RCC_CFGR         (*(volatile uint32_t *)(REG_RCC_BASE + 0x04UL))

#define REG_GPIOA_BASE       0x40010800UL
#define REG_GPIOA_CRL        (*(volatile uint32_t *)(REG_GPIOA_BASE + 0x00UL))

#define REG_DMA1_BASE        0x40020000UL
#define REG_DMA1_ISR         (*(volatile uint32_t *)(REG_DMA1_BASE + 0x00UL))
#define REG_DMA1_IFCR        (*(volatile uint32_t *)(REG_DMA1_BASE + 0x04UL))
#define REG_DMA1_CCR1        (*(volatile uint32_t *)(REG_DMA1_BASE + 0x08UL))
#define REG_DMA1_CNDTR1      (*(volatile uint32_t *)(REG_DMA1_BASE + 0x0CUL))
#define REG_DMA1_CPAR1       (*(volatile uint32_t *)(REG_DMA1_BASE + 0x10UL))
#define REG_DMA1_CMAR1       (*(volatile uint32_t *)(REG_DMA1_BASE + 0x14UL))

#define REG_ADC1_BASE        0x40012400UL
#define REG_ADC1_SR          (*(volatile uint32_t *)(REG_ADC1_BASE + 0x00UL))
#define REG_ADC1_CR1         (*(volatile uint32_t *)(REG_ADC1_BASE + 0x04UL))
#define REG_ADC1_CR2         (*(volatile uint32_t *)(REG_ADC1_BASE + 0x08UL))
#define REG_ADC1_SMPR2       (*(volatile uint32_t *)(REG_ADC1_BASE + 0x10UL))
#define REG_ADC1_SQR1        (*(volatile uint32_t *)(REG_ADC1_BASE + 0x1CUL))
#define REG_ADC1_SQR3        (*(volatile uint32_t *)(REG_ADC1_BASE + 0x24UL))
#define REG_ADC1_DR          (*(volatile uint32_t *)(REG_ADC1_BASE + 0x4CUL))

#define REG_NVIC_BASE        0xE000E100UL
#define REG_NVIC_ISER0       (*(volatile uint32_t *)(REG_NVIC_BASE + 0x00UL))
#define NVIC_IRQ_11_DMA1_CH1 (1UL << 11u)

/* Continuous physical ping-pong double buffer space allocation */
#define ADC_DMA_TOTAL_SIZE   (ADC_CHANNELS_COUNT * ADC_SAMPLES_PER_CHANNEL * 2u)
static adc_raw_t g_adc_raw_dma_pool[ADC_DMA_TOTAL_SIZE];

static adc_callback_fn g_adc_dma_callback = NULL;

bool adc_hardware_init(adc_callback_fn callback)
{
    if (callback == NULL) {
        return false;
    }
    g_adc_dma_callback = callback;

    /* 1. Turn on peripheral clock domains (GPIOA, ADC1, DMA1) */
    REG_RCC_AHBENR |= (1UL << 0u);   /* AHBENR: DMA1EN */
    REG_RCC_APB2ENR |= (1UL << 2u) | (1UL << 9u); /* APB2ENR: IOPAEN, ADC1EN */

    /* Configure ADC Prescaler clock divider: ADCCLK = PCLK2 / 6 (8MHz / 6 ≈ 1.33MHz) */
    REG_RCC_CFGR &= ~(3UL << 14u);
    REG_RCC_CFGR |= (2UL << 14u);

    /* 2. Configure Pin PA0, PA1, PA2 as Analog Inputs (CNF=00, MODE=00) */
    uint32_t gpio_crl = REG_GPIOA_CRL;
    gpio_crl &= ~0x00000FFFUL; /* Wipe configuration flags for pins 0, 1, 2 */
    REG_GPIOA_CRL = gpio_crl;

    /* 3. Configure DMA1 Channel 1 (Tied to ADC1 hardware event) */
    REG_DMA1_CCR1 &= ~1UL; /* Turn off channel for register initialization setup */
    REG_DMA1_CPAR1 = (uint32_t)&REG_ADC1_DR;
    REG_DMA1_CMAR1 = (uint32_t)(uintptr_t)g_adc_raw_dma_pool;
    REG_DMA1_CNDTR1 = ADC_DMA_TOTAL_SIZE;

    /* DMA CCR1 Configuration flags:
     * PL=10 (High priority), MSIZE=01 (16-bit), PSIZE=01 (16-bit),
     * MINC=1 (Memory increment), CIRC=1 (Circular loop),
     * HTIE=1 (Half Transfer Interrupt), TCIE=1 (Transfer Complete Interrupt)
     */
    REG_DMA1_CCR1 = (2UL << 12u) | (1UL << 10u) | (1UL << 8u) |
                    (1UL << 7u)  | (1UL << 5u)  | (1UL << 2u) | (1UL << 1u);

    /* 4. Configure ADC1 Peripherals Scanning Routine registers */
    REG_ADC1_SMPR2 |= (7UL << 0u) | (7UL << 3u) | (7UL << 6u); /* 239.5 cycles sample time for Ch 0,1,2 */
    REG_ADC1_SQR1 = ((uint32_t)(ADC_CHANNELS_COUNT - 1u) << 20u); /* Specify sequence scan length = 3 channels */
    REG_ADC1_SQR3 = (0UL << 0u) | (1UL << 5u) | (2UL << 10u); /* Map sequence: PA0->PA1->PA2 */

    /* Activate Scan mode (SCAN=1) and enable continuous conversion query (CONT=1) with DMA triggers */
    REG_ADC1_CR1 |= (1UL << 8u); /* SCAN bit */
    REG_ADC1_CR2 |= (1UL << 22u) | (1UL << 8u); /* SWSTART trigger, DMA enable bit */

    /* 5. Trigger hardware self-calibration engine to eliminate silicon offsets */
    REG_ADC1_CR2 |= (1UL << 0u); /* ADON: Wake up ADC block from deep sleep */
    REG_ADC1_CR2 |= (1UL << 3u); /* RSTCAL: Reset calibration registers matrix */
    while ((REG_ADC1_CR2 & (1UL << 3u)) != 0u) {}
    REG_ADC1_CR2 |= (1UL << 2u); /* CAL: Start calibration process */
    while ((REG_ADC1_CR2 & (1UL << 2u)) != 0u) {}

    /* 6. Commit vector slots inside NVIC interrupt map and start pipeline */
    // ИСПРАВЛЕНО: Запись в регистр ISER0, а не в базовый адрес
    REG_NVIC_ISER0 |= NVIC_IRQ_11_DMA1_CH1; 
    
    REG_DMA1_CCR1 |= 1UL; /* EN: Fire up DMA engine streaming */
    REG_ADC1_CR2 |= (1UL << 22u); /* SWSTART: Kick off first conversion scan row */

    return true;
}

void adc_hardware_stop(void)
{
    REG_DMA1_CCR1 &= ~1UL; /* Halt DMA1 Channel 1 stream */
    REG_ADC1_CR2 &= ~(1UL << 0u); /* Put ADC1 peripheral into deep sleep */
    g_adc_dma_callback = NULL;
}

/**
 * @brief Asynchronous Hardware Interrupt Routine: DMA1 Channel 1 Handler.
 * Dispatched automatically when DMA fills up a dedicated ping-pong boundary memory frame chunk.
 */
void DMA1_Channel1_IRQHandler(void) __attribute__((interrupt));
void DMA1_Channel1_IRQHandler(void)
{
    uint32_t dma_isr = REG_DMA1_ISR;
    size_t half_chunk_size = ADC_CHANNELS_COUNT * ADC_SAMPLES_PER_CHANNEL;

    if ((dma_isr & (1UL << 2u)) != 0u) { /* HTIF1: Half Transfer Complete Flag */
        REG_DMA1_IFCR = (1UL << 2u); /* Clear pending interrupt bit */
        if (g_adc_dma_callback != NULL) {
            /* Zero-copy: point directly to the first buffer sector */
            g_adc_dma_callback(&g_adc_raw_dma_pool[0u], half_chunk_size, ADC_EVENT_HALF_COMPLETE);
        }
    }

    if ((dma_isr & (1UL << 1u)) != 0u) { /* TCIF1: Transfer Complete Flag */
        REG_DMA1_IFCR = (1UL << 1u); /* Clear pending interrupt bit */
        if (g_adc_dma_callback != NULL) {
            /* Zero-copy: point directly to the second buffer wrap sector */
            g_adc_dma_callback(&g_adc_raw_dma_pool[half_chunk_size], half_chunk_size, ADC_EVENT_FULL_COMPLETE);
        }
    }

    if ((dma_isr & (1UL << 3u)) != 0u) { /* TEIF1: Transmission Hardware Error Flag */
        REG_DMA1_IFCR = (1UL << 3u);
        if (g_adc_dma_callback != NULL) {
            g_adc_dma_callback(NULL, 0u, ADC_EVENT_HARDWARE_ERROR);
        }
    }
}
