#include "air_sensor.h"
#include "app_config.h"
#include <stddef.h>

static HAL_StatusTypeDef ReadChannel(uint32_t channel, uint16_t *value)
{
    uint32_t started, total = 0U;
    uint8_t count;
    if (value == NULL) return HAL_ERROR;
    ADC1->SQR3 = channel;
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

static void SetSampleTime(uint32_t channel)
{
    if (channel < 10U) ADC1->SMPR2 |= 7UL << (3U * channel);
    else ADC1->SMPR1 |= 7UL << (3U * (channel - 10U));
}

void Air_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();
    gpio.Pin = APP_LIGHT_PIN | APP_CO2_PIN | APP_HCHO_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
    ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0;
    ADC1->CR1 = 0U;
    ADC1->CR2 = 0U;
    ADC1->SQR1 = 0U;
    ADC1->SQR2 = 0U;
    ADC1->SQR3 = 0U;
    SetSampleTime(APP_LIGHT_ADC_CHANNEL);
    SetSampleTime(APP_CO2_ADC_CHANNEL);
    SetSampleTime(APP_HCHO_ADC_CHANNEL);
    ADC1->CR2 = ADC_CR2_ADON;
}

HAL_StatusTypeDef Air_Read(uint16_t *light_adc, uint16_t *co2_ppm, uint16_t *hcho_centi)
{
    uint16_t light, co2_adc, hcho_adc;
    HAL_StatusTypeDef status;
    if (light_adc == NULL || co2_ppm == NULL || hcho_centi == NULL) return HAL_ERROR;
    status = ReadChannel(APP_LIGHT_ADC_CHANNEL, &light);
    if (status != HAL_OK) return status;
    status = ReadChannel(APP_CO2_ADC_CHANNEL, &co2_adc);
    if (status != HAL_OK) return status;
    status = ReadChannel(APP_HCHO_ADC_CHANNEL, &hcho_adc);
    if (status != HAL_OK) return status;
    *light_adc = light;
    *co2_ppm = Air_Co2FromAdc(co2_adc);
    *hcho_centi = Air_HchoFromAdc(hcho_adc);
    return HAL_OK;
}

uint16_t Air_Co2FromAdc(uint16_t adc)
{
    if (adc > 4095U) adc = 4095U;
    return (uint16_t)(APP_CO2_MIN_PPM +
           (uint32_t)adc * (APP_CO2_MAX_PPM - APP_CO2_MIN_PPM) / 4095U);
}

uint16_t Air_HchoFromAdc(uint16_t adc)
{
    if (adc > 4095U) adc = 4095U;
    return (uint16_t)((uint32_t)adc * APP_HCHO_MAX_CENTI / 4095U);
}
