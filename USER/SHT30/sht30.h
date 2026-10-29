#ifndef __SHT30_H
#define __SHT30_H

#include "stm32f4xx_hal.h"

/* SHT30 default address is 0x44 (HAL uses the 8-bit address 0x88). */
#define SHT30_I2C_ADDRESS       (0x44U << 1)

extern I2C_HandleTypeDef hi2c1;

HAL_StatusTypeDef SHT30_Init(void);
HAL_StatusTypeDef SHT30_ReadRaw(uint16_t *temperature, uint16_t *humidity);
int SHT30_Lock(uint32_t timeout_ms);
void SHT30_Unlock(void);

#endif
