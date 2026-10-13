//=======================================
//初始化IO 串口1 
//bound:波特率
//=======================================
#include "uart.h"
#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"

StreamBufferHandle_t uartStreamBuffer = NULL;
QueueHandle_t atQueue;    // 存 AT 文本行
QueueHandle_t dataQueue;  // 存 19 字节数据帧

uint8_t CercleBuffer[CercleBufferSize];
volatile uint16_t read_pos = 0; // 用于跟踪从 DMA 缓冲区读取数据的位置

uint8_t send_buf[100]; //发送缓冲区



void uart_init(u32 bound){
    //GPIO端口设置
    GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	 
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1|RCC_APB2Periph_GPIOA|RCC_APB2Periph_AFIO, ENABLE);	//使能USART1，GPIOA时钟以及复用功能时钟
     //USART1_TX   PA.9
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; //PA.9
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;	//复用推挽输出
    GPIO_Init(GPIOA, &GPIO_InitStructure);
   
    //USART1_RX	  PA.10
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;//浮空输入
    GPIO_Init(GPIOA, &GPIO_InitStructure);  

   //Usart1 NVIC 配置

    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=8 ;//抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;		//子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器
  
   //USART 初始化设置

	USART_InitStructure.USART_BaudRate = bound;//一般设置为9600;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式

    USART_Init(USART1, &USART_InitStructure); //初始化串口
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);//开启中断
    USART_Cmd(USART1, ENABLE);                    //使能串口 

#if EN_USART1_RX	
//	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);//开启相关中断，使用DMA后关闭这这个标志
    USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);
	USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);
    
#endif
}

// =======================================
// 串口1中断服务程序
// =======================================
void USART1_IRQHandler(void)
{
    uint8_t data;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if(USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)//空闲中断
    {
        data = USART1->SR;
        data = USART1->DR;//串口空闲中断的中断标志只能通过先读SR寄存器，再读DR寄存器清除！

        // DMA 当前写到的位置
        uint16_t write_pos = CercleBufferSize - DMA_GetCurrDataCounter(DMA1_Channel5);

        // 从 read_pos 到 write_pos 的数据长度
        uint16_t len;
        if(write_pos >= read_pos)
        {
            len = write_pos - read_pos;           // 没回卷
        }
        else
        {
            len = CercleBufferSize - read_pos + write_pos;  // 回卷了，两段，read_pos到缓冲区末尾 + 缓冲区开头到write_pos
        }

        if(len > 0)
        {
            // 分两段送流缓冲区（处理回卷）
            if(read_pos + len <= CercleBufferSize)
            {
                // 没回卷，一次送
                xStreamBufferSendFromISR(uartStreamBuffer, &CercleBuffer[read_pos], len, &xHigherPriorityTaskWoken);
            }
            else
            {
                // 回卷，分两段
                uint16_t first = CercleBufferSize - read_pos;
                uint16_t second = len - first;
                xStreamBufferSendFromISR(uartStreamBuffer, &CercleBuffer[read_pos], first, &xHigherPriorityTaskWoken);
                xStreamBufferSendFromISR(uartStreamBuffer, &CercleBuffer[0], second, &xHigherPriorityTaskWoken);
            }

            // 更新读指针
            read_pos = write_pos;
        }
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

//wifi发送任务
void wifi_send_task(void *pvParameters)
{
	// uint8_t buf[100];
    uint32_t notifyValue;

    connect_init(0);

	while(1)
	{
		// Receive = xStreamBufferReceive(uartStreamBuffer, buf, sizeof(buf), portMAX_DELAY); // 从 StreamBuffer 中接收数据
        if(xTaskNotifyWait(0, 0, &notifyValue, portMAX_DELAY) == pdTRUE)
        {
            if(notifyValue == 0x01)
            {

            }
            else if(notifyValue == 0x02)
            {
                // 发送心跳

            }
            else if(notifyValue == 0x03)
            {
                // 检测连接状态
                connect_init(0);
            }
		}
        vTaskDelay(pdMS_TO_TICKS(10));
	}
}

void wifi_get_task(void *pvParameters)
{
    uint8_t byte;
    uint8_t line[AT_LINE_MAX];
    size_t  line_len = 0;
    uint8_t frame[DATA_FRAME_LEN];
    size_t  frame_len = 0;

    while (1)
    {
        // 一次读一个字节，方便按状态切分
        if (xStreamBufferReceive(uartStreamBuffer, &byte, 1, portMAX_DELAY) != 1)
            continue;
        /* ---------- 状态1：还没锁定帧头，先按 AT 行处理 ---------- */
        if (frame_len == 0)
        {
            // 遇 \r\n 结尾，认为一行 AT 应答结束
            if (byte == '\n' && line_len > 0)
            {
                line[line_len] = '\0';
                // 送入 AT 队列
                xQueueSend(atQueue, line, 0);
                line_len = 0;
                continue;
            }

            // 普通的 AT 文本
            if (line_len < AT_LINE_MAX - 1)
                line[line_len++] = byte;

            // 如果当前字节是 0x55，可能是数据帧头，检查下一个字节
            if (byte == 0x55)
            {
                // 把 0x55 放进 frame，等下一个字节确认是不是 0x44
                frame[0] = 0x55;
                frame_len = 1;
            }
            continue;
        }

        /* ---------- 状态2：已收 0x55，等 0x44 ---------- */
        if (frame_len == 1)
        {
            if (byte == 0x44)
            {
                frame[1] = 0x44;
                frame_len = 2;
            }
            else
            {
                // 不是 0x44，把 0x55 当普通文本，重新处理当前字节
                if (line_len < AT_LINE_MAX - 1)
                    line[line_len++] = 0x55;

                frame_len = 0;

                // 当前字节按 AT 文本处理
                if (byte == '\n' && line_len > 0) {
                    line[line_len] = '\0';
                    xQueueSend(atQueue, line, 0);
                    line_len = 0;
                } else if (line_len < AT_LINE_MAX - 1) {
                    line[line_len++] = byte;
                    if (byte == 0x55) { frame[0] = 0x55; frame_len = 1; }
                }
            }
            continue;
        }

        /* ---------- 状态3：攒数据帧到 19 字节 ---------- */
        frame[frame_len++] = byte;

        if (frame_len == DATA_FRAME_LEN)
        {
            // 校验帧尾
            if (frame[17] == 0x33 && frame[18] == 0x22)
            {
                xQueueSend(dataQueue, frame, 0);   // 送数据队列
            }
            // 无论是否合法，都重置状态
            frame_len = 0;
            line_len  = 0;
        }
    }
}

static uint8_t bcd2dec(uint8_t b)
{
    return (b >> 4) * 10 + (b & 0x0F);
}

void StreamBuffer_Init(void)
{
    uartStreamBuffer = xStreamBufferCreate(2048, 1);
    atQueue   = xQueueCreate(8, AT_LINE_MAX);
    dataQueue = xQueueCreate(4, DATA_FRAME_LEN);
	if(uartStreamBuffer == NULL || atQueue == NULL || dataQueue == NULL)
	{
		// 创建失败，处理错误
		while(1);
	}

}


uint8_t* Data_pack(uint16_t data)
{
    // 这里可以实现数据打包的逻辑，比如添加帧头、帧尾、校验等
    // 目前需要的信号帧格式为55 44 [时间更新功能位] [有效位] [地区码功能位] [内容位] 00 00 33 22
    send_buf[0] = 0x55;
    send_buf[1] = 0x44;
    send_buf[2] = 0x01; //时间更新功能
    send_buf[3] = data; // 有效位
    send_buf[4] = 0x02; //地区码功能
    send_buf[5] = data; // 地区码内容 00不更新01-03对应各地区
    send_buf[6] = 0x00;//保留位
    send_buf[7] = 0x00;
    send_buf[8] = 0x33;
    send_buf[9] = 0x22;

    return send_buf;
}

void uart1_send_at(const char *cmd)
{
    while(*cmd)
    {
        while((USART1->SR & 0x40) == 0);
        USART1->DR = *cmd++;
    }
    while((USART1->SR & 0x40) == 0);
    USART1->DR = '\r';
    while((USART1->SR & 0x40) == 0);
    USART1->DR = '\n';
}

void uart1_send_bytes(uint8_t *buf, uint16_t len)
{
    for(uint16_t i = 0; i < len; i++)
    {
        while((USART1->SR & 0x40) == 0);
        USART1->DR = buf[i];
    }
}

int wait_response(const char *expect, uint32_t timeout_ms)
{
    char line[AT_LINE_MAX];
    TickType_t start = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms))
    {
        if (xQueueReceive(atQueue, line, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            if (strstr(line, expect) != NULL)   return 1;
            if (strstr(line, "ERROR") != NULL ||
                strstr(line, "FAIL")  != NULL)  return 0;
        }
    }
    return 0;
}

// 返回值说明：
//  3  -> STATUS:3 （已建立 TCP/UDP 连接）
//  1  -> STATUS:1 或 5 （未连接 WiFi / 未获取 IP）
//  2  -> STATUS:2 或 4 （已获取 IP 但未建立 TCP，或连接已断开）
// -1  -> 模块返回 ERROR 或 FAIL
//  0  -> 超时
int wait_connect_response(uint32_t timeout_ms)
{
    char line[AT_LINE_MAX];
    TickType_t start = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms))
    {
        if (xQueueReceive(atQueue, line, pdMS_TO_TICKS(500)) == pdTRUE)
        {
            char *p = strstr(line, "STATUUS:");
            if (p && strlen(p) > 8)
            {
                char st = p[8];
                if (st == '3') return 3;
                if (st == '1' || st == '5') return 1;
                if (st == '2' || st == '4') return 2;
            }
            if(p == NULL)
            {
                p = strstr(line, "STATUS:");
                if (p && strlen(p) > 7)
                {
                    char st = p[7];
                    if (st == '3') return 3;
                    if (st == '1' || st == '5') return 1;
                    if (st == '2' || st == '4') return 2;
                }
            }

            if (strstr(line, "ERROR") || strstr(line, "FAIL"))
                return -1;
        }
    }
    return 0;
}

void data_parse_task(void *pvParameters)
{
    uint8_t frame[DATA_FRAME_LEN];

    while (1)
    {
        if (xQueueReceive(dataQueue, frame, portMAX_DELAY) == pdTRUE)
        {
            if (frame[2] == 0x01 && frame[3] == 0x01)
            {

            }

            if (frame[11] == 0x02 && frame[12] == 0x01)
            {

            }
        }
    }
}

void connect_init(int status)
{
    int i = status;
    int retry = 0;

    /* ---------- 1. 退出透传 ---------- */
    uart1_send_bytes("+++", 3);
    if (wait_response("+++", 1500) != 1) {
        vTaskDelay(pdMS_TO_TICKS(500));
        uart1_send_bytes("+++", 3);
        wait_response("+++", 1500);
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    /* ---------- 2. 进入 AT 配置流程 ---------- */
    while (i != 3 && retry < 3)
    {
        if (i == 0)
        {
            uart1_send_at("ATE0");
            wait_response("OK", 1500);

            uart1_send_at("AT+UART=115200,8,1,0,1");
            while(!wait_response("OK", 1500));

            uart1_send_at("AT+CWMODE=1");
            while(wait_response("OK", 1500));
            i++;
        }

        if (i == 1)
        {
            uart1_send_at("AT+CWJAP=\"TP-LINK_3372\",\"12345678\"");
            while(wait_response("OK", 5000));   // 连 WiFi 慢，超时给大点

            i++;
        }

        if (i == 2)
        {
            uart1_send_at("AT+CIPSTART=\"TCP\",\"192.168.1.103\",7755");
            while(wait_response("OK", 3000));//
            i++;
        }
        // uart1_send_at("AT+CIPSTATUS");
        // i = wait_connect_response(3000);
        // vTaskDelay(pdMS_TO_TICKS(1500));
        retry++;
    }
    /* ---------- 3. 进入透传 ---------- */
    uart1_send_at("AT+CIPMODE=1");
    while(wait_response("OK", 1500));

    uart1_send_at("AT+CIPSEND");
    while(wait_response(">", 1500));
}
