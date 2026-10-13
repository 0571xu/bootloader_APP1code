#ifndef __UART_H
#define __UART_H 

#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "semphr.h"

#define EN_USART1_RX  1   // 1 = 启用串口接收中断；0 = 不启用
// int bound = 115200;
extern uint8_t Recv[100]; // 接收缓冲区
extern uint8_t rx_cnt; // 接收计数
extern SemaphoreHandle_t uartSemaphore;

void uart_init(u32 bound);
void USART1_IRQHandler(void);
void print_task(void *pvParameters);

#endif
