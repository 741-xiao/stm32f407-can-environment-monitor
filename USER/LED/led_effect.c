#include "led_effect.h"
#include "app_config.h"
#include "stm32f4xx_hal.h"

static volatile uint8_t mode;
static volatile uint32_t effect_ticks;

static void WriteLEDs(uint8_t mask)
{
    GPIO_PinState on = APP_LED_ACTIVE_LOW ? GPIO_PIN_RESET : GPIO_PIN_SET;
    GPIO_PinState off = APP_LED_ACTIVE_LOW ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin(APP_LED0_GPIO, APP_LED0_PIN, mask & 1U ? on : off);
    HAL_GPIO_WritePin(APP_LED1_GPIO, APP_LED1_PIN, mask & 2U ? on : off);
    HAL_GPIO_WritePin(APP_LED2_GPIO, APP_LED2_PIN, mask & 4U ? on : off);
}

void LED_EffectSet(uint8_t effect)
{
    uint32_t primask;
    if (effect == mode) return;
    primask = __get_PRIMASK();
    __disable_irq();
    mode = effect;
    effect_ticks = 0U;
    WriteLEDs(0U);
    __set_PRIMASK(primask);
}

void LED_EffectInit(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t clock = HAL_RCC_GetPCLK1Freq();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_TIM6_CLK_ENABLE();
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    WriteLEDs(0U);
    gpio.Pin = APP_LED0_PIN;
    HAL_GPIO_Init(APP_LED0_GPIO, &gpio);
    gpio.Pin = APP_LED1_PIN;
    HAL_GPIO_Init(APP_LED1_GPIO, &gpio);
    gpio.Pin = APP_LED2_PIN;
    HAL_GPIO_Init(APP_LED2_GPIO, &gpio);
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U) clock *= 2U;
    TIM6->CR1 = 0U;
    TIM6->PSC = clock / 1000000U - 1U;
    TIM6->ARR = 99U;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0U;
    TIM6->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 3U, 0U);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    TIM6->CR1 = TIM_CR1_CEN;
}

void TIM6_DAC_IRQHandler(void)
{
    uint32_t duty, ticks;
    uint8_t mask = 0U;
    if (!(TIM6->SR & TIM_SR_UIF)) return;
    TIM6->SR = 0U;
    ticks = effect_ticks++;
    if (mode == LED_EFFECT_BREATH) {
        duty = (ticks / 100U) % 200U;
        if (duty > 100U) duty = 200U - duty;
        if (ticks % 100U < duty) mask = 7U;
    } else if (mode == LED_EFFECT_FLOW) mask = (uint8_t)(1U << ((ticks / 2500U) % 3U));
    WriteLEDs(mask);
}
