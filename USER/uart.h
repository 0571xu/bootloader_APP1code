#ifndef __UART_H
#define __UART_H 

#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "stream_buffer.h"

//#define EN_USART1_RX  1   // 1 = 启用串口接收中断；0 = 不启用
#define CercleBufferSize  512         // DMA 环形接收缓冲区大小（必须远大于单帧 268 字节）
#define DATA_FRAME_LEN      256          // 每包数据固定 256 字节
#define FRAME_POOL_SIZE     4            // 帧池里 4 个 slot
#define FRAME_QUEUE_DEPTH   4            // 帧队列深度
#define AT_LINE_MAX         128          // AT 行最大长度

/* 帧格式常量 */
#define FRAME_HEADER_1      0x55
#define FRAME_HEADER_2      0x44
#define FRAME_TAIL_1        0x33
#define FRAME_TAIL_2        0x22

/* 帧总长（固定）：2(头) + 1(总) + 1(当前) + 2(实长) + 4(CRC) + 256(数据) + 2(尾) */
#define FRAME_TOTAL_LEN     (2 + 1 + 1 + 2 + 4 + DATA_FRAME_LEN + 2)   // = 268

/* 结束帧的特殊包号 */
#define PACKET_END          0xFF

/* ==================== 数据结构 ==================== */

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
