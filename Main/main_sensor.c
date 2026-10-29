#include "common.h"
#include "key.h"
#include "beep.h"
#include "sht30.h"
#include "air_sensor.h"
#include "can_link.h"
#include "ssd1306.h"
#include "led_effect.h"
#include "app_config.h"
#include "app_rtos.h"
#include "serial.h"
#include "wifi_link.h"
#include <stdio.h>

int main(void)
{
    char frame[128];
    HAL_Init();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    Stm32_Clock_Init(336U, 8U, 2U, 7U);
    HAL_NVIC_SetPriority(SysTick_IRQn, 15U, 0U);
    delay_init();
    BEEP_Init();
    BEEP = 0U;
    KEY_Init();
    Serial_Init();
    LED_EffectInit();
    if (SHT30_Init() != HAL_OK) (void)Serial_Write(SERIAL_CONSOLE, "I2C_INIT_ERROR\r\n");
    if (SSD1306_Init() != HAL_OK) (void)Serial_Write(SERIAL_CONSOLE, "OLED_INIT_ERROR\r\n");
    Air_Init();
    if (!CanLink_Init()) (void)Serial_Write(SERIAL_CONSOLE, "CAN_INIT_ERROR\r\n");
    WiFi_Init(HAL_GetTick());
    (void)snprintf(frame, sizeof(frame), "TRI SENSOR BOARD=%u KEY1=REMOTE_ON KEY2=REMOTE_OFF\r\n", (unsigned int)APP_BOARD_ID);
    (void)Serial_Write(SERIAL_CONSOLE, frame);
    (void)Serial_Write(SERIAL_CONSOLE, "FREERTOS_V10.3.1 START\r\n");
    App_RtosStart();
    for (;;) { }
}
