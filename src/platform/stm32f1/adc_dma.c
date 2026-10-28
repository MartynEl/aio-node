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
#define REG_ADC1_SQR1        (*(volatile uint32_t *)(REG_ADC1_BASE + 0x2CUL))
#define REG_ADC1_SQR3        (*(volatile uint32_t *)(REG_ADC1_BASE + 0x34UL))
#define REG_ADC1_DR          (*(volatile uint32_t *)(REG_ADC1_BASE + 0x4CUL))

#define REG_NVIC_BASE        0xE000E100UL
#define REG_NVIC_ISER0       (*(volatile uint32_t *)(REG_NVIC_BASE + 0x00UL))
#define REG_NVIC_ICER0       (*(volatile uint32_t *)(REG_NVIC_BASE + 0x04UL))
#define NVIC_IRQ_11_DMA1_CH1 (1UL << 11u)

#define ADC_DMA_TOTAL_SIZE   (ADC_CHANNELS_COUNT * ADC_SAMPLES_PER_CHANNEL * 2u)
static adc_raw_t g_adc_raw_dma_pool[ADC_DMA_TOTAL_SIZE];
static adc_callback_fn g_adc_dma_callback = NULL;

void adc_hardware_kick_scan(void)
{
    REG_NVIC_ICER0 |= NVIC_IRQ_11_DMA1_CH1;

    REG_DMA1_CCR1 &= ~1UL;
    REG_DMA1_IFCR = 0x0000000FUL;
    REG_DMA1_CNDTR1 = ADC_DMA_TOTAL_SIZE;
    REG_DMA1_CCR1 |= 1UL;

    /* Восстанавливаем проверенный workaround для регулярного SCAN-преобразования клона:
     * Сбрасываем CR2, а затем атомарно поднимаем SWSTART + DMA + ADON.
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

    /* Тактирование АЦП и DMA (TIM2 больше не нужен!) */
    REG_RCC_AHBENR |= (1UL << 0u); /* DMA1EN */
    REG_RCC_APB2ENR |= (1UL << 2u) | (1UL << 9u) | (1UL << 10u); /* IOPAEN, ADC1EN, ADC2EN */

    REG_RCC_CFGR &= ~(3UL << 14u);
    REG_RCC_CFGR |= (2UL << 14u);

    uint32_t gpio_crl = REG_GPIOA_CRL;
    gpio_crl &= ~0x00000FFFUL;
    gpio_crl |=  0x00000222UL;
    REG_GPIOA_CRL = gpio_crl;

    REG_DMA1_CCR1 &= ~1UL;
    REG_DMA1_CPAR1  = (uint32_t)&REG_ADC1_DR;
    REG_DMA1_CMAR1  = (uint32_t)(uintptr_t)g_adc_raw_dma_pool;
    REG_DMA1_CNDTR1 = ADC_DMA_TOTAL_SIZE;

    REG_DMA1_CCR1 = (2UL << 12u) | (1UL << 10u) | (1UL << 8u) | (1UL << 7u) | (1UL << 5u) | (1UL << 2u) | (1UL << 1u);

    REG_ADC1_SMPR2 |= (7UL << 0u) | (7UL << 3u) | (7UL << 6u);
    REG_ADC1_SQR1 = ((uint32_t)(ADC_CHANNELS_COUNT - 1u) << 20u);
    REG_ADC1_SQR3 = (0UL << 0u) | (1UL << 5u) | (2UL << 10u);     
    REG_ADC1_CR1 |= (1UL << 8u); /* SCAN mode */

    /* Держим триггеры строго в 0 (канонический программный режим v0.9) */
    REG_ADC1_CR2 &= ~((7UL << 17u) | (1UL << 20u));

    REG_ADC1_CR2 |= (1UL << 0u); /* ADON */

    uint32_t cal_guard = 100000u;
    REG_ADC1_CR2 |= (1UL << 3u); /* RSTCAL */
    while ((REG_ADC1_CR2 & (1UL << 3u)) != 0u) { if (--cal_guard == 0u) break; }

    cal_guard = 100000u;
    REG_ADC1_CR2 |= (1UL << 2u); /* CAL */
    while ((REG_ADC1_CR2 & (1UL << 2u)) != 0u) { if (--cal_guard == 0u) break; }

    REG_ADC1_CR2 |= (1UL << 8u); /* DMA Enable */

    /* Стартовый запуск */
    adc_hardware_kick_scan();

    return true;
}

void adc_hardware_stop(void)
{
    REG_DMA1_CCR1 &= ~1UL; 
    REG_ADC1_CR2 &= ~(1UL << 0u); 
    g_adc_dma_callback = NULL;
}

void DMA1_Channel1_IRQHandler(void)
{
    uint32_t dma_isr = REG_DMA1_ISR;
    size_t half_chunk_size = ADC_CHANNELS_COUNT * ADC_SAMPLES_PER_CHANNEL;

    if ((dma_isr & (1UL << 2u)) != 0u) { /* HTIF1 */
        REG_DMA1_IFCR = (1UL << 2u);
        if (g_adc_dma_callback != NULL) {
            g_adc_dma_callback(&g_adc_raw_dma_pool[0u], half_chunk_size, ADC_EVENT_HALF_COMPLETE);
        }
    }
    if ((dma_isr & (1UL << 1u)) != 0u) { /* TCIF1 */
        REG_DMA1_IFCR = (1UL << 1u);
        if (g_adc_dma_callback != NULL) {
            g_adc_dma_callback(&g_adc_raw_dma_pool[half_chunk_size], half_chunk_size, ADC_EVENT_FULL_COMPLETE);
        }
    }
    if ((dma_isr & (1UL << 3u)) != 0u) { /* TEIF1 */
        REG_DMA1_IFCR = (1UL << 3u);
        if (g_adc_dma_callback != NULL) {
            g_adc_dma_callback(NULL, 0u, ADC_EVENT_HARDWARE_ERROR);
        }
    }
}
