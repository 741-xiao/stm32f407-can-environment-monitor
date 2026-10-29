#include "led.h"
#include "beep.h"
#include "key.h"
#include "lcd.h"

/*********************************************************************************
*********************启明欣欣 STM32F407应用开发板(高配版)*************************
**********************************************************************************
* 文件名称: 例程5 LCD显示主函数main()                                            *
* 文件简述：LCD显示实验                                                          *
* 创建日期：2018.08.30                                                           *
* 版    本：V1.0                                                                 *
* 作    者：Clever                                                               *
* 说    明：按键控制LED亮灭与蜂鸣器开断                                          *
* 淘宝店铺：https://shop125046348.taobao.com                                     *
* 声    明：本例程代码仅用于学习参考                                             *
**********************************************************************************
*********************************************************************************/

int main(void)
{ 
	u8 i=1;
  u16 num;
	
  HAL_Init();                    	//初始化HAL库    
  Stm32_Clock_Init(336,8,2,7);  	//设置时钟,168Mhz
	delay_init();     //延时函数初始化
	LED_Init();				//LED初始化
	BEEP_Init();      //蜂鸣器初始化
	KEY_Init();       //按键初始化
 	LCD_Init();           //初始化LCD FSMC接口和显示驱动
	BRUSH_COLOR=RED;      //设置画笔颜色为红色
	
	LCD_DisplayString(10,10,24,"Illuminati STM32");	
	LCD_DisplayString(20,40,16,"Author:Clever");
	LCD_DisplayString(30,80,24,"4.TFTLCD TEST");
	LCD_DisplayString(50,110,16,"KEY0:Screen Test");	
	LCD_DisplayString(50,130,16,"KEY1:Number Test");				
  
	while(1) 
	{		 
		key_scan(0);
		
		if(keydown_data==KEY0_DATA)
		  {	
				while(i)
				 {	
				    switch(i)
							{							
								case 1:LCD_Clear(BLACK);break;
								case 2:LCD_Clear(BLUE);break;
								case 3:LCD_Clear(RED);break;
								case 4:LCD_Clear(MAGENTA);break;
								case 5:LCD_Clear(GREEN);break;
								case 6:LCD_Clear(CYAN);break; 
								case 7:LCD_Clear(YELLOW);break;
								case 8:LCD_Clear(BRRED);break;
								case 9:LCD_Clear(GRAY);break;
								case 10:LCD_Clear(BROWN);break;
								case 11:LCD_Clear(WHITE);break;
							}      					 
							i++;
							LED0=!LED0;	 
							delay_ms(1000);
							if(i==12)
							 { 
							    i=0;
								  LCD_DisplayString(10,10,24,"Illuminati STM32F4");	
									LCD_DisplayString(20,40,16,"Author:Clever");
									LCD_DisplayString(30,80,24,"4.TFTLCD TEST");
									LCD_DisplayString(50,110,16,"KEY0:Screen Test");	
									LCD_DisplayString(50,130,16,"KEY1:Number Test");	
							 }		
					}							
			}

		if(keydown_data==KEY1_DATA)
		  {
			   LCD_Clear(WHITE);				
				 LCD_DisplayString(20,20,24,"Number Test");		
				 num=8357;
				 LCD_DisplayNum(40,60,num,6,24,0);
				 LCD_DisplayNum(40,90,num,6,24,1);
			}
	  delay_ms(5);	
	}
}

