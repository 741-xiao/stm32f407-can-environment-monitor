#ifndef __CHINESE_LCD_H
#define __CHINESE_LCD_H

#include "common.h"

/* Built-in 24x24 glyph indexes: 温 度 湿 姓 名 学 号 摄 氏 百 分 比 肖 肃 更 新 正 常 错 误. */
#define LCD_HANZI_ZHENG  16U
#define LCD_HANZI_CHANG  17U
#define LCD_HANZI_CUO    18U
#define LCD_HANZI_WU     19U

void LCD_DisplayChinese(u16 x, u16 y, u8 index);
void LCD_TextUTF8(u16 x, u16 y, const char *text);

#endif
