#ifndef AIR_SENSOR_H
#define AIR_SENSOR_H
#include "stm32f4xx_hal.h"

void Air_Init(void);
HAL_StatusTypeDef Air_Read(uint16_t *light_adc, uint16_t *co2_ppm, uint16_t *hcho_centi);
uint16_t Air_Co2FromAdc(uint16_t adc);
uint16_t Air_HchoFromAdc(uint16_t adc);
#endif
