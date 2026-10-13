
/**************************************
 * 文件名  ：main.c
 * 描述    ：通过串口调试软件，向板子发送数据，板子接收到数据后，立即回传给电脑。         
 * 实验平台：MINI STM32开发板 基于STM32F103C8T6
 * 库版本  ：ST3.0.0  																										  

*********************************************************/
#include "stm32f10x.h"
#include <string.h>
#include "uart.h"
#include "ota_receive.h"
#include "led.h"
#include "key.h"
#include "delay.h"

// Flash分区基址见 ota_receive.h（BOOTLOADER_BASE / APPLICATION_BASE /
// OTA_BUFFER_BASE / CONFIG_BASE / OTA_BUFFER_SIZE / FLASH_PAGE_SIZE），
// 这里只补充本文件用到的分区大小。
#define BOOTLOADER_SIZE     (12 * 1024)      // 12KB，给 Bootloader 留足余量
#define APPLICATION_SIZE    (36 * 1024)      // 从 0x08003000 到 0x0800C000 = 36KB

// OTA 升级标志结构 ota_config_t 及 OTA_MAGIC / CURRENT_VERSION 定义在 ota_receive.h

/******************************************************************************************/
#define bootloader  0
#define application 1
 
#if bootloader

#define EN_USART1_RX  0   // 1 = 启用串口接收中断；0 = 不启用


// 函数声明
static void jump_to_application(uint32_t address);
static int check_application_valid(uint32_t address);
uint32_t calculate_crc32(uint8_t *data, uint32_t size);
int perform_firmware_update(void);

// CRC32计算
static uint32_t calculate_crc32(uint8_t *data, uint32_t size)
{
    uint32_t i;
    uint32_t *p = (uint32_t *)data;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_CRC, ENABLE);  // 使能 CRC 时钟
    CRC_ResetDR();  // 复位 CRC 数据寄存器到 0xFFFFFFFF

    for (i = 0; i < (size / 4); i++) {
        CRC->DR = p[i];
    }

    return CRC->DR;
}

// 检查应用程序有效性
static int check_application_valid(uint32_t address) {
    uint32_t sp = *(__IO uint32_t*)address;
    
    // 检查栈指针是否在RAM范围内
    if ((sp & 0x2FFE0000) == 0x20000000) {
        return 1;
    }
    return 0;
}

void HAL_DeInit(void)
{
    /* 复位 APB1 总线上的外设 (USART2/3/4/5, TIM2-7, SPI2, I2C1/2 等) */
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_ALL, ENABLE);
    RCC_APB1PeriphResetCmd(RCC_APB1Periph_ALL, DISABLE);

    /* 复位 APB2 总线上的外设 (USART1, SPI1, TIM1, GPIOA-E 等) */
    RCC_APB2PeriphResetCmd(RCC_APB2Periph_ALL, ENABLE);
    RCC_APB2PeriphResetCmd(RCC_APB2Periph_ALL, DISABLE);

}

// 跳转到应用程序
static void jump_to_application(uint32_t address) {
    typedef void (*pFunction)(void);
    pFunction jump_to_app;
    uint32_t jump_address;
    
    // 获取复位向量
    jump_address = *(__IO uint32_t*)(address + 4);
    jump_to_app = (pFunction)jump_address;
    
    // 设置主栈指针
    __set_MSP(*(__IO uint32_t*)address);
    
    // 关闭所有中断
    __disable_irq();//需要在在程序中手动打开
    
    // 复位所有外设
    HAL_DeInit();
    
    // 设置向量表
    SCB->VTOR = address;
    
    // 跳转到应用程序
    jump_to_app();
}

// 执行固件更新
static int perform_firmware_update(void) {
    ota_config_t *config = (ota_config_t*)CONFIG_BASE;
    uint8_t *ota_data = (uint8_t*)OTA_BUFFER_BASE;
    uint32_t calculated_crc;
    int ret = 0;

    // 检查魔术字
    if (config->magic != 0xDEADBEEF) {
        ret = -1;
        goto clear_and_exit;
    }
    
    // 检查更新标志
    if (config->update_flag != 1) {
        ret = -1;
        goto clear_and_exit;
    }
    
    // 验证CRC
    calculated_crc = calculate_crc32(ota_data, config->firmware_size);
    if (calculated_crc != config->crc32) {
        ret = -2;
        goto clear_and_exit;
    }
    
    // 擦除应用程序区域
    if (flash_erase(APPLICATION_BASE, config->firmware_size) != Status_OK) {
        ret = -3;
        goto clear_and_exit;
    }
    
    // 写入新固件
    if (flash_write(APPLICATION_BASE, ota_data, config->firmware_size) != Status_OK) {
        ret = -4;
        goto clear_and_exit;
    }
    
    // 走到这里 = 成功，ret = 0

clear_and_exit:
    // 无论成功失败，都清除 update_flag（避免无限重试）
    {
        ota_config_t clear_config = {0};
        flash_erase(CONFIG_BASE, sizeof(ota_config_t));
        flash_write(CONFIG_BASE, (uint8_t*)&clear_config, sizeof(ota_config_t));
    }
    return ret;
}

// Bootloader主函数
int main(void) {
    
    SystemInit();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    uart_init(115200);
    delay_init();          // 初始化延时
    LED_GPIO_Config();  //led、蜂鸣器初始化

    // ★ 初始化串口（内部会开中断）
    //uart_init(115200);
    // ★ 配置DMA（内部会开中断）
    //DMA_Config(DMA1_Channel5,(u32)&USART1->DR,(u32)CercleBuffer, CercleBufferSize);//DMA1通道5,外设为串口1,存储器为SendBuff,长度SEND_BUF_SIZE.

    char *str3 = "bootloader begin!\r\n";
    int len3 = strlen(str3);
    
    for (int i = 0; i < len3; i++) {
        USART_SendData(USART1, str3[i]);
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    }
    
    // 检查是否需要升级
    ota_config_t *config = (ota_config_t*)CONFIG_BASE;
    if (config->magic == 0xDEADBEEF && config->update_flag == 1) {
        // LED快闪表示正在升级
        for (int i = 0; i < 10; i++) {
            //HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
            //HAL_Delay(100);
            LED1(i);
            delay_ms(100);
            char *str4 = "upgrading!\r\n";
            int len4 = strlen(str4);
            
            for (int i = 0; i < len4; i++) {
                USART_SendData(USART1, str4[i]);
                while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
            }
        }

        // 执行升级
        if (perform_firmware_update() == 0) {
            // 升级成功，LED常亮1秒
            //HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
            //HAL_Delay(1000);
            LED1(1);
            
            delay_ms(400);
            char *str4 = "upgrade success!\r\n";
            int len4 = strlen(str4);
            
            for (int i = 0; i < len4; i++) {
                USART_SendData(USART1, str4[i]);
                while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
            }
        } else {
            // 升级失败，LED慢闪
            for (int i = 0; i < 5; i++) {
                //HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
                //HAL_Delay(500);
                LED1(i);
                delay_ms(100);
                char *str4 = "upgrade fail!\r\n";
                int len4 = strlen(str4);
                
                for (int i = 0; i < len4; i++) {
                    USART_SendData(USART1, str4[i]);
                    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
                }
            }
        }
    }

    // 检查应用程序是否有效
    if (check_application_valid(APPLICATION_BASE)) {
        // 跳转到应用程序
        jump_to_application(APPLICATION_BASE);
    }

    // 如果无法跳转，LED持续慢闪
    while (1) {
        //HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        //HAL_Delay(1000);
        GPIO_SetBits(GPIOC, GPIO_Pin_13 );
        delay_ms(100);
        USART_SendData(USART1,'a');
        GPIO_ResetBits(GPIOC, GPIO_Pin_13 );
        delay_ms(100);
        USART_SendData(USART1,'b');

    }
}

#endif

/******************************************************************************************/

#if application

#define EN_USART1_RX  1   // 1 = 启用串口接收中断；0 = 不启用

#include "freertos.h"
#include "dma.h"

TaskHandle_t xled_taskHandle;


typedef struct
{
    GPIO_TypeDef* GPIOx;      // GPIO端口
    uint16_t GPIO_Pin;        // GPIO引脚
    uint32_t delay_on;       // 延时时间
    uint32_t delay_off;       // 延时时间
} LED_Config;

LED_Config led1_config = {GPIOC, GPIO_Pin_13, 500, 500};

void request_upgrade_task(void *pvParameters);
void led_task(void *pvParameters);

int main(void)
{
    SystemInit();
    SCB->VTOR = 0x08003000;   // 和 APPLICATION_BASE 一致，放在 SystemInit 之后    
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);    
    delay_init();
    LED_GPIO_Config();
    KEY_Init();
    uart_init(115200);
    // ★ 配置DMA（内部会开中断）
    DMA_Config(DMA1_Channel5,(u32)&USART1->DR,(u32)CercleBuffer, CercleBufferSize);//DMA1通道5,外设为串口1,存储器为SendBuff,长度SEND_BUF_SIZE.
    //使用流缓冲区
    StreamBuffer_Init(); // 初始化 StreamBuffer
    __enable_irq();   // 显式打开全局中断    
    


    char *str3 = "jump success!\r\n";
    int len3 = strlen(str3);
    
    for (int i = 0; i < len3; i++) {
        USART_SendData(USART1, str3[i]);
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    }
    
    xTaskCreate((TaskFunction_t )led_task,
                (const char*    )"led_task1",
                (uint16_t       )128,
                (void*          )&led1_config,
                (UBaseType_t    )3,
                (TaskHandle_t*  )&xled_taskHandle);

    /* OTA 接收：逐字节状态机解析帧 -> 写入 OTA 缓冲区 */
    xTaskCreate(wifi_get_task,        "wifi_get",    384, NULL, 3, NULL);
    xTaskCreate(task_ota_flash,       "ota_flash",   256, NULL, 3, NULL);

    /* 升级触发任务：收到结束帧后写 config + 软复位 */
    xTaskCreate(request_upgrade_task, "req_upgrade", 128, NULL, 1, &xrequest_upgrade_taskHandle);

    /* WiFi 收发/AT 任务：负责串口提示与连接 */
    xTaskCreate(wifi_send_task,       "wifi_send",   256, NULL, 1, &xWifisendTaskHandle);

        // 启动调度器
    vTaskStartScheduler();
    
    while (1) {

        // if(request_flag)
        // {
        //     delay_ms(20);   // 去抖：在任务里延时，不在中断里
        //     request_upgrade();
        //     request_flag = 0;
        // }
    }
}


void led_task(void *pvParameters)
{
    LED_Config* config = (LED_Config*)pvParameters;
    
    while(1)
    {
        if(config->GPIO_Pin==GPIO_Pin_13)
        {
            GPIO_ResetBits(config->GPIOx, config->GPIO_Pin);  // LED灭
            vTaskDelay(pdMS_TO_TICKS(config->delay_off));     // 延时
            GPIO_SetBits(config->GPIOx, config->GPIO_Pin);    // LED亮
            vTaskDelay(pdMS_TO_TICKS(config->delay_on));      // 延时
            xTaskNotify(xWifisendTaskHandle, 0x01, eSetValueWithOverwrite); // 发送心跳通知
        }else if(config->GPIO_Pin==GPIO_Pin_1){
            GPIO_SetBits(config->GPIOx, config->GPIO_Pin);    // LED亮
            vTaskDelay(pdMS_TO_TICKS(config->delay_on));      // 延时
            GPIO_ResetBits(config->GPIOx, config->GPIO_Pin);  // LED灭
            vTaskDelay(pdMS_TO_TICKS(config->delay_off));     // 延时
        }
    }
}

#endif





