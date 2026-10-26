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

/* На этом перемаркированном кристалле 0x40012000 НЕ принимает записи (probe:
 * mww->readback 0), а живой ADC-блок отвечает на 0x40012400 (mww->readback липнет).
 * Generic datasheet map здесь неверен; держимся эмпирики. Смещения остаются
 * каноническими F1 (+0x2C SQR1, +0x34 SQR3, +0x4C DR). */
#define REG_ADC1_BASE        0x40012400UL
#define REG_ADC1_SR          (*(volatile uint32_t *)(REG_ADC1_BASE + 0x00UL))
#define REG_ADC1_CR1         (*(volatile uint32_t *)(REG_ADC1_BASE + 0x04UL))
#define REG_ADC1_CR2         (*(volatile uint32_t *)(REG_ADC1_BASE + 0x08UL))
#define REG_ADC1_SMPR2       (*(volatile uint32_t *)(REG_ADC1_BASE + 0x10UL))
#define REG_ADC1_SQR1        (*(volatile uint32_t *)(REG_ADC1_BASE + 0x2CUL))
#define REG_ADC1_SQR3        (*(volatile uint32_t *)(REG_ADC1_BASE + 0x34UL))
#define REG_ADC1_DR          (*(volatile uint32_t *)(REG_ADC1_BASE + 0x4CUL))

#define REG_NVIC_BASE        0xE000E100UL
#define REG_NVIC_ISER0       (*(volatile uint32_t *)(REG_NVIC_BASE + 0x00UL))
#define REG_NVIC_ICER0       (*(volatile uint32_t *)(REG_NVIC_BASE + 0x04UL))
#define NVIC_IRQ_11_DMA1_CH1 (1UL << 11u)

/* Continuous physical ping-pong double buffer space allocation */
#define ADC_DMA_TOTAL_SIZE   (ADC_CHANNELS_COUNT * ADC_SAMPLES_PER_CHANNEL * 2u)
static adc_raw_t g_adc_raw_dma_pool[ADC_DMA_TOTAL_SIZE];

static adc_callback_fn g_adc_dma_callback = NULL;

void adc_hardware_kick_scan(void)
{
    /* Mask DMA1 Channel 1 IRQ while rearming the stream.
     * This prevents a race between the task context and the DMA ISR.
     */
    REG_NVIC_ICER0 |= NVIC_IRQ_11_DMA1_CH1;

    /* CNDTR may only be modified while the DMA channel is disabled. */
    REG_DMA1_CCR1 &= ~1UL;

    /* Clear all DMA1 Channel 1 flags: GIF1, TCIF1, HTIF1, TEIF1. */
    REG_DMA1_IFCR = 0x0000000FUL;

    /* Rearm exactly one circular buffer period.
     * With ADC_SAMPLES_PER_CHANNEL=1 and ADC_CHANNELS_COUNT=3:
     * ADC_DMA_TOTAL_SIZE = 3 * 1 * 2 = 6 half-words.
     * One 3-channel ADC scan fills the first half and triggers HTIF1.
     */
    REG_DMA1_CNDTR1 = ADC_DMA_TOTAL_SIZE;

    /* Enable DMA channel again. */
    REG_DMA1_CCR1 |= 1UL;

    /* Start one regular conversion scan.
     * CONT is deliberately not used: this clone does not latch ADC_CR2 bit9.
     */
    /* Workaround для перемаркированного клона:
      * Чтобы перезапустить автомат регулярного SCAN-преобразования без режима CONT,
      * необходимо гарантировать фронт на бите SWSTART относительно поднятого ADON.
      * Для этого сбрасываем CR2, а затем атомарно поднимаем SWSTART + DMA + ADON.
      */
    uint32_t cr2_val = REG_ADC1_CR2;
    cr2_val &= ~(1UL << 22u); /* Явно гасим SWSTART */
    REG_ADC1_CR2 = cr2_val;
    
    REG_ADC1_CR2 = cr2_val | (1UL << 22u) | (1UL << 8u) | (1UL << 0u); /* SWSTART + DMA + ADON */

    REG_NVIC_ISER0 |= NVIC_IRQ_11_DMA1_CH1;
}

bool adc_hardware_init(adc_callback_fn callback)
{
    if (callback == NULL) {
        return false;
    }

    g_adc_dma_callback = callback;

    /* 1. Clock domains.
     *
     * Empirical note for this remarked clone:
     * the live ADC register block answers at 0x40012400, not 0x40012000.
     * We keep both ADC clock bits enabled because the exact internal routing
     * of this clone is not documented by the generic datasheet map.
     */
    REG_RCC_AHBENR |= (1UL << 0u); /* DMA1EN */
    REG_RCC_APB2ENR |= (1UL << 2u) | (1UL << 9u) | (1UL << 10u); /* IOPAEN, ADC1EN, ADC2EN */

    /* ADC prescaler: ADCCLK = PCLK2 / 6.
     * PCLK2 is HSI 8 MHz, so ADCCLK ~= 1.33 MHz.
     */
    REG_RCC_CFGR &= ~(3UL << 14u);
    REG_RCC_CFGR |= (2UL << 14u);

    /* 2. PA0, PA1, PA2 as analog inputs.
     * STM32F1 GPIO CRL nibble for analog input is 0x2.
     */
    uint32_t gpio_crl = REG_GPIOA_CRL;
    gpio_crl &= ~0x00000FFFUL;
    gpio_crl |=  0x00000222UL;
    REG_GPIOA_CRL = gpio_crl;

    /* 3. DMA1 Channel 1 configuration.
     *
     * Empirically this live ADC block raises requests on DMA1 Channel 1:
     * previous manual probe moved CNDTR1 from 96 to 93 after one SWSTART.
     */
    REG_DMA1_CCR1 &= ~1UL;
    REG_DMA1_CPAR1  = (uint32_t)&REG_ADC1_DR;
    REG_DMA1_CMAR1  = (uint32_t)(uintptr_t)g_adc_raw_dma_pool;
    REG_DMA1_CNDTR1 = ADC_DMA_TOTAL_SIZE;

    /* DMA1_CCR1:
     * PL=10 high priority
     * MSIZE=16-bit
     * PSIZE=16-bit
     * MINC=1
     * PINC=0
     * CIRC=1
     * HTIE=1
     * TCIE=1
     * EN is set by adc_hardware_kick_scan().
     */
    REG_DMA1_CCR1 = (2UL << 12u) | /* PL high */
                    (1UL << 10u) | /* MSIZE 16-bit */
                    (1UL <<  8u) | /* PSIZE 16-bit */
                    (1UL <<  7u) | /* MINC */
                    (1UL <<  5u) | /* CIRC */
                    (1UL <<  2u) | /* HTIE */
                    (1UL <<  1u);  /* TCIE */

    /* 4. ADC scan sequence. */
    REG_ADC1_SMPR2 |= (7UL << 0u) | (7UL << 3u) | (7UL << 6u); /* Ch0, Ch1, Ch2: 239.5 cycles */
    REG_ADC1_SQR1 = ((uint32_t)(ADC_CHANNELS_COUNT - 1u) << 20u); /* scan length = 3 */
    REG_ADC1_SQR3 = (0UL << 0u) | (1UL << 5u) | (2UL << 10u);     /* SQ1=ch0, SQ2=ch1, SQ3=ch2 */
    REG_ADC1_CR1 |= (1UL << 8u); /* SCAN mode */

    /* 5. Power up and calibrate.
     *
     * Important ordering:
     * ADON -> RSTCAL -> CAL -> DMA enable -> first SWSTART.
     * The previous code wrote SWSTART/CONT before ADON, which is invalid.
     */
    REG_ADC1_CR2 |= (1UL << 0u); /* ADON */

    REG_ADC1_CR2 |= (1UL << 3u); /* RSTCAL */
    uint32_t cal_guard = 100000u;
    while ((REG_ADC1_CR2 & (1UL << 3u)) != 0u) {
        if (--cal_guard == 0u) {
            break;
        }
    }

    REG_ADC1_CR2 |= (1UL << 2u); /* CAL */
    cal_guard = 100000u;
    while ((REG_ADC1_CR2 & (1UL << 2u)) != 0u) {
        if (--cal_guard == 0u) {
            break;
        }
    }

    /* Enable ADC DMA requests.
     *
     * Do NOT rely on CONT here:
     * manual probe proved ADC_CR2 bit9 does not latch on this silicon/clone.
     */
    REG_ADC1_CR2 |= (1UL << 8u); /* DMA */

    /* 6. Start first single-scan acquisition. */
    adc_hardware_kick_scan();

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
