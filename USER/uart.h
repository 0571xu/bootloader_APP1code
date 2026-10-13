#ifndef __UART_H
#define __UART_H 

#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "stream_buffer.h"

//#define EN_USART1_RX  1   // 1 = 启用串口接收中断；0 = 不启用
#define CercleBufferSize  1024          // 宏，不占 RAM，定义缓冲区大小
#define AT_LINE_MAX   128
#define DATA_FRAME_LEN 19

extern QueueHandle_t atQueue;
extern QueueHandle_t dataQueue;
extern StreamBufferHandle_t uartStreamBuffer; // 声明的 StreamBuffer 句柄
extern uint8_t CercleBuffer[CercleBufferSize]; // 用于DMA存储接收到的数据的缓冲区

void uart_init(u32 bound);
void USART1_IRQHandler(void);
void wifi_send_task(void *pvParameters);
void data_parse_task(void *pvParameters);
void wifi_get_task(void *pvParameters);
static uint8_t bcd2dec(uint8_t b);
void StreamBuffer_Init(void);
uint8_t* Data_pack(uint16_t data);
void uart1_send_at(const char *cmd);
void uart1_send_bytes(uint8_t *buf, uint16_t len);
int wait_response(const char *expect, uint32_t timeout_ms);
int wait_connect_response(uint32_t timeout_ms);
void connect_init(int status);



#endif
