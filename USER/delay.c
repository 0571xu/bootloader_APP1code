#include "delay.h"
#include "stm32f10x_tim.h"
#include "stm32f10x_rcc.h"

void delay_init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_TimeBaseInitTypeDef TIM_InitStructure;
    TIM_InitStructure.TIM_Period        = 0xFFFF;
    TIM_InitStructure.TIM_Prescaler     = 72 - 1;   // 72MHz / 72 = 1MHz
    TIM_InitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_InitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_InitStructure);

    TIM_Cmd(TIM2, DISABLE);
}

void delay_us(uint32_t us)
{
    TIM2->CNT = 0;
    TIM_Cmd(TIM2, ENABLE);
    while(TIM2->CNT < us);
    TIM_Cmd(TIM2, DISABLE);
    TIM2->CNT = 0;
}

void delay_ms(uint32_t ms)
{
    while(ms--)
    {
        delay_us(1000);
    }
}
