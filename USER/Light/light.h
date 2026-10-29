#ifndef LIGHT_H
#define LIGHT_H
#include "stm32f4xx_hal.h"
void Light_Init(void);
HAL_StatusTypeDef Light_Read(uint16_t *value);
#endif
