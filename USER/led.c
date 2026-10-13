/******************* ********************
 * 文件名  ：led.c
 * 描述    ：led 应用函数库
 *          
 * 实验平台：MINI STM32开发板 基于STM32F103C8T6
 * 硬件连接：-----------------
 *          |   PC13 - LED1   |
 *          |       
 *          |                 |
 *           ----------------- 
 * 库版本  ：ST3.0.0  																										  
*********************************************************/
#include "led.h"


 /***************  配置LED用到的I/O口 *******************/
void LED_GPIO_Config(void)	
{
  // 初始化GPIO
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC|RCC_APB2Periph_GPIOA, ENABLE);

  GPIO_InitTypeDef GPIO_InitStructure;
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOC, &GPIO_InitStructure);

  GPIO_InitTypeDef GPIO_InitStructure2;             //蜂鸣器初始化
  GPIO_InitStructure2.GPIO_Pin = GPIO_Pin_6;
  GPIO_InitStructure2.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStructure2.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOA, &GPIO_InitStructure2);

  GPIO_SetBits(GPIOC, GPIO_Pin_13 );	 // 关闭所有LED
  GPIO_SetBits(GPIOA, GPIO_Pin_6 );	 // 关闭所有LED
}


