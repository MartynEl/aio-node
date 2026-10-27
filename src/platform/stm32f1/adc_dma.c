#include "platform/adc_port.h"
#include <stddef.h>

/* --- Hardware Peripheral Registers Map Addresses (STM32F103) --- */
#define REG_RCC_BASE         0x40021000UL
#define REG_RCC_AHBENR       (*(volatile uint32_t *)(REG_RCC_BASE + 0x14UL))
#define REG_RCC_APB2ENR      (*(volatile uint32_t *)(REG_RCC_BASE + 0x18UL))
#define REG_RCC_APB1ENR      (*(volatile uint32_t *)(REG_RCC_BASE + 0x1CUL))
#define REG_RCC_CFGR         (*(volatile uint32_t *)(REG_RCC_BASE + 0x04UL))

#define REG_GPIOA_BASE       0x40010800UL
#define REG_GPIOA_CRL        (*(volatile uint32_t *)(REG_GPIOA_BASE + 0x00UL))

#define REG_TIM2_BASE        0x40000000UL
#define REG_TIM2_CR1         (*(volatile uint32_t *)(REG_TIM2_BASE + 0x00UL))
#define REG_TIM2_CR2         (*(volatile uint32_t *)(REG_TIM2_BASE + 0x04UL))
#define REG_TIM2_EGR         (*(volatile uint32_t *)(REG_TIM2_BASE + 0x14UL))
#define REG_TIM2_PSC         (*(volatile uint32_t *)(REG_TIM2_BASE + 0x28UL))
#define REG_TIM2_ARR         (*(volatile uint32_t *)(REG_TIM2_BASE + 0x2CUL))

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

    /* Rearm exactly one circular buffer period. */
    REG_DMA1_CNDTR1 = ADC_DMA_TOTAL_SIZE;

    /* Enable DMA channel again. */
    REG_DMA1_CCR1 |= 1UL;

    /* В режиме таймерного триггера ручной взвод SWSTART здесь больше не требуется. */
    REG_NVIC_ISER0 |= NVIC_IRQ_11_DMA1_CH1;
}

bool adc_hardware_init(adc_callback_fn callback)
{
    if (callback == NULL) {
        return false;
    }

    g_adc_dma_callback = callback;

    /* 1. Clock domains. */
    REG_RCC_AHBENR |= (1UL << 0u); /* DMA1EN */
    REG_RCC_APB2ENR |= (1UL << 2u) | (1UL << 9u) | (1UL << 10u); /* IOPAEN, ADC1EN, ADC2EN */
    REG_RCC_APB1ENR |= (1UL << 0u); /* TIM2EN */

    /* ADC prescaler: ADCCLK = PCLK2 / 6.
     * PCLK2 is HSI 8 MHz, so ADCCLK ~= 1.33 MHz.
     */
    REG_RCC_CFGR &= ~(3UL << 14u);
    REG_RCC_CFGR |= (2UL << 14u);

    /* 1.5 Настройка аппаратного триггера TIM2 на частоту 1 кГц (1 мс)
     * При HSI = 8 МГц: делим частоту на 8 (PSC = 7), получаем тики по 1 мкс.
     * Период ARR = 999 даст ровно 1000 мкс (1 мс).
     */
    REG_TIM2_PSC = 7u;
    REG_TIM2_ARR = 999u;
    REG_TIM2_CR2 &= ~(7UL << 4u);
    REG_TIM2_CR2 |= (2UL << 4u); /* MMS = 010: Master Mode = Update (TRGO) */

    /* 2. PA0, PA1, PA2 as analog inputs.
     * STM32F1 GPIO CRL nibble for analog input is 0x2.
     */
    uint32_t gpio_crl = REG_GPIOA_CRL;
    gpio_crl &= ~0x00000FFFUL;
    gpio_crl |=  0x00000222UL;
    REG_GPIOA_CRL = gpio_crl;

    /* 3. DMA1 Channel 1 configuration. */
    REG_DMA1_CCR1 &= ~1UL;
    REG_DMA1_CPAR1  = (uint32_t)&REG_ADC1_DR;
    REG_DMA1_CMAR1  = (uint32_t)(uintptr_t)g_adc_raw_dma_pool;
    REG_DMA1_CNDTR1 = ADC_DMA_TOTAL_SIZE;

    /* DMA1_CCR1: PL high, MSIZE 16-bit, PSIZE 16-bit, MINC, CIRC, HTIE, TCIE */
    REG_DMA1_CCR1 = (2UL << 12u) | 
                    (1UL << 10u) | 
                    (1UL <<  8u) | 
                    (1UL <<  7u) | 
                    (1UL <<  5u) | 
                    (1UL <<  2u) | 
                    (1UL <<  1u);

    /* 4. ADC scan sequence. */
    REG_ADC1_SMPR2 |= (7UL << 0u) | (7UL << 3u) | (7UL << 6u); /* Ch0, Ch1, Ch2: 239.5 cycles */
    REG_ADC1_SQR1 = ((uint32_t)(ADC_CHANNELS_COUNT - 1u) << 20u); /* scan length = 3 */
    REG_ADC1_SQR3 = (0UL << 0u) | (1UL << 5u) | (2UL << 10u);     /* SQ1=ch0, SQ2=ch1, SQ3=ch2 */
    
    REG_ADC1_CR1 |= (1UL << 8u); /* SCAN mode */

    REG_ADC1_CR2 &= ~(7UL << 17u);
    REG_ADC1_CR2 |= (3UL << 17u) | (1UL << 20u); /* EXTSEL = 011 (TIM2_TRGO), EXTTRIG = 1 */

    /* 5. Power up and calibrate. */
    REG_ADC1_CR2 |= (1UL << 0u); /* ADON */

    uint32_t cal_guard = 100000u;
    REG_ADC1_CR2 |= (1UL << 3u); /* RSTCAL */
    while ((REG_ADC1_CR2 & (1UL << 3u)) != 0u) {
        if (--cal_guard == 0u) {
            break;
        }
    }

    cal_guard = 100000u;
    REG_ADC1_CR2 |= (1UL << 2u); /* CAL */
    while ((REG_ADC1_CR2 & (1UL << 2u)) != 0u) {
        if (--cal_guard == 0u) {
            break;
        }
    }

    /* Enable ADC DMA requests. */
    REG_ADC1_CR2 |= (1UL << 8u); /* DMA */

    /* 6. Start first single-scan acquisition. */
    adc_hardware_kick_scan();

    /* 7. Запуск таймера триггеров */
    REG_TIM2_EGR |= (1UL << 0u);
    REG_TIM2_CR1 |= (1UL << 0u); /* CEN = 1 */

    return true;
}

void adc_hardware_stop(void)
{
    REG_TIM2_CR1 &= ~1UL;  /* Выключить таймер триггеров */
    REG_DMA1_CCR1 &= ~1UL; /* Halt DMA1 Channel 1 stream */
    REG_ADC1_CR2 &= ~(1UL << 0u); /* Put ADC1 peripheral into deep sleep */
    g_adc_dma_callback = NULL;
}

/**
 * @brief Asynchronous Hardware Interrupt Routine: DMA1 Channel 1 Handler.
 */
void DMA1_Channel1_IRQHandler(void) __attribute__((interrupt));
void DMA1_Channel1_IRQHandler(void)
{
    uint32_t dma_isr = REG_DMA1_ISR;
    size_t half_chunk_size = ADC_CHANNELS_COUNT * ADC_SAMPLES_PER_CHANNEL;

    if ((dma_isr & (1UL << 2u)) != 0u) { /* HTIF1: Half Transfer Complete Flag */
        REG_DMA1_IFCR = (1UL << 2u); /* Clear pending interrupt bit */
        if (g_adc_dma_callback != NULL) {
            g_adc_dma_callback(&g_adc_raw_dma_pool[0u], half_chunk_size, ADC_EVENT_HALF_COMPLETE);
        }
    }

    if ((dma_isr & (1UL << 1u)) != 0u) { /* TCIF1: Transfer Complete Flag */
        REG_DMA1_IFCR = (1UL << 1u); /* Clear pending interrupt bit */
        if (g_adc_dma_callback != NULL) {
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
