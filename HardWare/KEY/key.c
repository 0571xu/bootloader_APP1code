#include "stm32f10x.h"
#include "key.h"
#include "uart.h"
#include "ota_receive.h"
#include "delay.h"
#include "stdio.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK战舰STM32开发板
//按键驱动代码	   
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//修改日期:2012/9/3
//版本：V1.0
//版权所有，盗版必究。
//Copyright(C) 广州市星翼电子科技有限公司 2009-2019
//All rights reserved									  
//////////////////////////////////////////////////////////////////////////////////  
					
uint8_t request_flag = 0;//OTA更新请求

//按键初始化函数
void KEY_Init(void) 
{ 
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    
    // 1. 使能 GPIOA 和 AFIO 时钟 (使用外部中断必须开启 AFIO 时钟！)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);

    // 2. 配置 PA1 为上拉输入 (假设按键另一端接 GND，按下为低电平)
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_1; 
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. 将 PA1 映射到 EXTI Line1
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1);

    // 4. 配置 EXTI Line1 (下降沿触发)
    EXTI_InitStructure.EXTI_Line = EXTI_Line1;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;      // 中断模式
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;  // 下降沿触发 (按下瞬间)
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    // 5. 配置 NVIC (中断优先级)
    NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;         // 注意这里是 EXTI1_IRQn
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 7;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}
//中断函数
void EXTI1_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line1) != RESET)
    {
        xTaskNotify(xWifisendTaskHandle, 0x02, eSetValueWithOverwrite);
        EXTI_ClearITPendingBit(EXTI_Line1);  // 必须清除标志位！
    }
}
