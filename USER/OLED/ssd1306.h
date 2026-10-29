#ifndef SSD1306_H
#define SSD1306_H
#include <stdint.h>
#include "stm32f4xx_hal.h"

#define SSD1306_WIDTH 128U
#define SSD1306_HEIGHT 64U
#define SSD1306_ADDRESS (0x3CU << 1)

HAL_StatusTypeDef SSD1306_Init(void);
void SSD1306_DisplayOn(void);
void SSD1306_DisplayOff(void);
void SSD1306_Clear(void);
void SSD1306_Flush(void);
void SSD1306_Text(uint8_t x, uint8_t page, const char *text);
void SSD1306_Value(uint8_t x, uint8_t page, int32_t value, uint8_t decimals, const char *suffix);
#endif
