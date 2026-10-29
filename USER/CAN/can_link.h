#ifndef CAN_LINK_H
#define CAN_LINK_H
#include <stdint.h>
#include "stm32f4xx_hal.h"

extern CAN_HandleTypeDef hcan1;

int CanLink_Init(void);
int CanLink_Send(uint32_t id, const uint8_t *data, uint8_t length);
int CanLink_Read(uint32_t *id, uint8_t *data, uint8_t *length);
uint32_t CanLink_RxCount(void);
uint32_t CanLink_ErrorCount(void);
#endif
