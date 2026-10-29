#include "light.h"
#include "app_config.h"

void Light_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();
    gpio.Pin = APP_LIGHT_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(APP_LIGHT_GPIO, &gpio);
    ADC1->CR2 = 0U;
    ADC1->CR1 = 0U;
    ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0;
    ADC1->SQR1 = 0U;
    ADC1->SQR2 = 0U;
    ADC1->SQR3 = APP_LIGHT_ADC_CHANNEL;
#if APP_LIGHT_ADC_CHANNEL >= 10U
    ADC1->SMPR1 |= 7UL << (3U * (APP_LIGHT_ADC_CHANNEL - 10U));
#else
    ADC1->SMPR2 |= 7UL << (3U * APP_LIGHT_ADC_CHANNEL);
#endif
    ADC1->CR2 = ADC_CR2_ADON;
}

HAL_StatusTypeDef Light_Read(uint16_t *value)
{
    uint32_t started, total = 0U;
    uint8_t count;
    if (value == NULL) return HAL_ERROR;
    for (count = 0U; count < 8U; count++) {
        ADC1->SR = 0U;
        ADC1->CR2 |= ADC_CR2_SWSTART;
        started = HAL_GetTick();
        while (!(ADC1->SR & ADC_SR_EOC)) {
            if ((uint32_t)(HAL_GetTick() - started) >= 5U) return HAL_TIMEOUT;
        }
        total += ADC1->DR & 4095U;
    }
    *value = (uint16_t)((total + 4U) / 8U);
    return HAL_OK;
}
