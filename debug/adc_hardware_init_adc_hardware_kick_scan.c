void adc_hardware_kick_scan(void)
{
    REG_NVIC_ICER0 |= NVIC_IRQ_11_DMA1_CH1;

    REG_DMA1_CCR1 &= ~1UL;
    REG_DMA1_IFCR = 0x0000000FUL;
    REG_DMA1_CNDTR1 = ADC_DMA_TOTAL_SIZE;
    REG_DMA1_CCR1 |= 1UL;

    /* Программный пинок SWSTART для выравнивания автомата АЦП на первом шаге.
     * Делаем это аккуратно через маску, чтобы гарантировать фронт на бите.
     */
    REG_ADC1_CR2 &= ~(1UL << 22u); /* Сброс */
    REG_ADC1_CR2 |= (1UL << 22u);  /* Взвод SWSTART */

    REG_NVIC_ISER0 |= NVIC_IRQ_11_DMA1_CH1;
}


/* Блок 2. Заменяем функцию инициализации на каноничную последовательную */
bool adc_hardware_init(adc_callback_fn callback)
{
    if (callback == NULL) {
        return false;
    }

    g_adc_dma_callback = callback;

    /* 1. Тактирование доменов периферии */
    REG_RCC_AHBENR |= (1UL << 0u); /* DMA1EN */
    REG_RCC_APB2ENR |= (1UL << 2u) | (1UL << 9u) | (1UL << 10u); /* IOPAEN, ADC1EN, ADC2EN */
    REG_RCC_APB1ENR |= (1UL << 0u); /* TIM2EN */

    /* ADC prescaler: ADCCLK = PCLK2 / 6. */
    REG_RCC_CFGR &= ~(3UL << 14u);
    REG_RCC_CFGR |= (2UL << 14u);

    /* 1.5 Настройка аппаратного триггера TIM2 на частоту 1 кГц (1 мс) */
    REG_TIM2_PSC = 7u;
    REG_TIM2_ARR = 999u;
    REG_TIM2_CR2 &= ~(7UL << 4u);
    REG_TIM2_CR2 |= (2UL << 4u); /* MMS = 010: Update event -> TRGO */

    /* 2. PA0, PA1, PA2 as analog inputs. */
    uint32_t gpio_crl = REG_GPIOA_CRL;
    gpio_crl &= ~0x00000FFFUL;
    gpio_crl |=  0x00000222UL;
    REG_GPIOA_CRL = gpio_crl;

    /* 3. DMA1 Channel 1 configuration. */
    REG_DMA1_CCR1 &= ~1UL;
    REG_DMA1_CPAR1  = (uint32_t)&REG_ADC1_DR;
    REG_DMA1_CMAR1  = (uint32_t)(uintptr_t)g_adc_raw_dma_pool;
    REG_DMA1_CNDTR1 = ADC_DMA_TOTAL_SIZE;

    REG_DMA1_CCR1 = (2UL << 12u) | (1UL << 10u) | (1UL << 8u) | (1UL << 7u) | (1UL << 5u) | (1UL << 2u) | (1UL << 1u);

    /* 4. ADC scan sequence. */
    REG_ADC1_SMPR2 |= (7UL << 0u) | (7UL << 3u) | (7UL << 6u);
    REG_ADC1_SQR1 = ((uint32_t)(ADC_CHANNELS_COUNT - 1u) << 20u);
    REG_ADC1_SQR3 = (0UL << 0u) | (1UL << 5u) | (2UL << 10u);     
    REG_ADC1_CR1 |= (1UL << 8u); /* SCAN mode */

    /* Гарантируем, что на этапе калибровки внешние триггеры выключены! */
    REG_ADC1_CR2 &= ~((7UL << 17u) | (1UL << 20u));

    /* 5. Power up and calibrate. */
    REG_ADC1_CR2 |= (1UL << 0u); /* ADON */

    uint32_t cal_guard = 100000u;
    REG_ADC1_CR2 |= (1UL << 3u); /* RSTCAL */
    while ((REG_ADC1_CR2 & (1UL << 3u)) != 0u) { if (--cal_guard == 0u) break; }

    cal_guard = 100000u;
    REG_ADC1_CR2 |= (1UL << 2u); /* CAL */
    while ((REG_ADC1_CR2 & (1UL << 2u)) != 0u) { if (--cal_guard == 0u) break; }

    REG_ADC1_CR2 |= (1UL << 8u); /* DMA Enable */

    /* 6. Безопасно подготавливаем и взводим DMA-поток в бесконфликтном софт-режиме */
    adc_hardware_kick_scan();

    /* 7. И ТОЛЬКО ТЕПЕРЬ атомарно переводим АЦП на рельсы аппаратных таймерных триггеров */
    REG_ADC1_CR2 &= ~(7UL << 17u);
    REG_ADC1_CR2 |= (3UL << 17u) | (1UL << 20u); /* EXTSEL = 011 (TIM2_TRGO), EXTTRIG = 1 */

    /* 8. Запуск счетчика таймера триггеров */
    REG_TIM2_EGR |= (1UL << 0u);
    REG_TIM2_CR1 |= (1UL << 0u); /* CEN = 1 */

    return true;
}