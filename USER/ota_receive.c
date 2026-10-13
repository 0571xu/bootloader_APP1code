/**************************************
 * 文件名  ：ota_receive.c
 * 描述    ：OTA 帧接收、CRC 校验、Flash 写入
 * 说明    ：配合 uart.c 中串口接收的功能实现
 *********************************************************/

#include "ota_receive.h"
#include "stm32f10x.h"
#include <string.h>

/* ==================== 全局变量 ==================== */

StreamBufferHandle_t uartStreamBuffer = NULL;
QueueHandle_t        xFrameQueue      = NULL;
QueueHandle_t        atQueue          = NULL;
TaskHandle_t xrequest_upgrade_taskHandle;
TaskHandle_t xWifisendTaskHandle      = NULL;

/* 结束帧解析出的固件信息，供 request_upgrade_task 写入 config */
static uint32_t g_firmware_size = 0;
static uint32_t g_firmware_crc  = 0;

/* 帧池 */
static frame_slot_t frame_pool[FRAME_POOL_SIZE];

/* ==================== 帧池管理 ==================== */

frame_slot_t* frame_pool_alloc(void)
{
    for (int i = 0; i < FRAME_POOL_SIZE; i++) {
        if (!frame_pool[i].in_use) {
            frame_pool[i].in_use = 1;
            return &frame_pool[i];
        }
    }
    return NULL;   /* 无空闲 slot */
}

void frame_pool_free(frame_slot_t *slot)
{
    if (slot) {
        slot->in_use = 0;
    }
}

/* ==================== 初始化 ==================== */

void ota_receive_init(void)
{
    memset(frame_pool, 0, sizeof(frame_pool));

    uartStreamBuffer = xStreamBufferCreate(STREAM_BUF_SIZE, 1);   /* 触发水位=1 */
    xFrameQueue      = xQueueCreate(FRAME_QUEUE_DEPTH, sizeof(fw_frame_t));
    atQueue          = xQueueCreate(4, AT_LINE_MAX);

    configASSERT(uartStreamBuffer != NULL);
    configASSERT(xFrameQueue != NULL);
    configASSERT(atQueue != NULL);
}

/* ==================== CRC32 计算（含帧头字段） ==================== */

uint32_t calculate_crc32_ex(uint8_t total, uint8_t current, uint16_t len, uint8_t *data)
{
    uint32_t i;
    uint32_t words;
    uint32_t rest;
    uint32_t *p;
    uint8_t tmp[4];

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_CRC, ENABLE);
    CRC_ResetDR();

    /* 先算 4 字节头：total(1B) + current(1B) + len(2B,小端)，刚好拼成 1 个 32 位字 */
    uint32_t header = ((uint32_t)total)
                    | ((uint32_t)current << 8)
                    | ((uint32_t)len << 16);
    CRC->DR = header;

    /* 再算实际数据，只算 len 字节 */
    p     = (uint32_t *)data;
    words = len / 4;
    rest  = len % 4;

    for (i = 0; i < words; i++) {
        CRC->DR = p[i];
    }

    /* 剩余 1~3 字节，用 0xFF 补齐到 4 字节 */
    if (rest > 0) {
        tmp[0] = 0xFF;
        tmp[1] = 0xFF;
        tmp[2] = 0xFF;
        tmp[3] = 0xFF;
        for (i = 0; i < rest; i++) {
            tmp[i] = data[words * 4 + i];
        }
        CRC->DR = *(uint32_t *)tmp;
    }

    return CRC->DR;
}

/* ==================== 逐字节解析帧状态机 + AT 行处理 ==================== */

void wifi_get_task(void *pvParameters)
{
    uint8_t  byte;
    uint8_t  line[AT_LINE_MAX];
    size_t   line_len = 0;
    parser_t parser;

    /* 初始化解析器 */
    memset(&parser, 0, sizeof(parser));
    parser.state = 0;   /* ST_HEADER_1 */

    while (1)
    {
        /* 一次读一个字节 */
        if (xStreamBufferReceive(uartStreamBuffer, &byte, 1, portMAX_DELAY) != 1)
            continue;

        /* ---------- 状态机 ---------- */
        switch (parser.state)
        {
            
        /* ===== 状态 0：等 0x55，同时处理 AT 行 ===== */
        case 0:   /* ST_HEADER_1 */
            /* AT 行处理：遇到 \n 提交一行 */
            if (byte == '\n' && line_len > 0) {
                line[line_len] = '\0';
                xQueueSend(atQueue, line, 0);
                line_len = 0;
            } else if (byte != '\r') {
                if (line_len < AT_LINE_MAX - 1)
                    line[line_len++] = byte;
            }

            /* 检测帧头起点 */
            if (byte == FRAME_HEADER_1) {
                parser.state = 1;   /* ST_HEADER_2 */
            }
            break;

        /* ===== 状态 1：等 0x44 ===== */
        case 1:   /* ST_HEADER_2 */
            if (byte == FRAME_HEADER_2) {
                /* 帧头确认，从帧池分配 slot */
                parser.slot = frame_pool_alloc();
                if (parser.slot == NULL) {
                    parser.state = 0;   /* 无空闲 slot，丢弃这一帧 */
                    break;
                }
                parser.crc32        = 0;
                parser.crc_byte_idx = 0;
                parser.data_idx     = 0;
                parser.state        = 2;   /* ST_TOTAL */
            } else if (byte == FRAME_HEADER_1) {
                /* 连续 0x55，保持在 ST_HEADER_2 */
                parser.state = 1;
            } else {
                /* 不是 0x44，回起点；当前字节按 AT 处理 */
                if (byte == '\n' && line_len > 0) {
                    line[line_len] = '\0';
                    xQueueSend(atQueue, line, 0);
                    line_len = 0;
                } else if (byte != '\r') {
                    if (line_len < AT_LINE_MAX - 1)
                        line[line_len++] = byte;
                }
                parser.state = 0;
            }
            break;

        /* ===== 状态 2：读总包数 ===== */
        case 2:   /* ST_TOTAL */
            parser.total_packets = byte;
            parser.state = 3;   /* ST_CURRENT */
            break;

        /* ===== 状态 3：读当前包号 ===== */
        case 3:   /* ST_CURRENT */
            parser.current_packet = byte;
            parser.data_len       = 0;
            parser.len_byte_idx   = 0;
            parser.state          = 4;   /* ST_LEN */
            break;

        /* ===== 状态 4：读实际长度（2 字节，小端） ===== */
        case 4:   /* ST_LEN */
            parser.data_len |= (uint16_t)((uint16_t)byte << (parser.len_byte_idx * 8));
            parser.len_byte_idx++;
            if (parser.len_byte_idx >= 2) {
                if (parser.data_len == 0 || parser.data_len > DATA_FRAME_LEN) {
                    /* 非法长度，回起点，释放 slot */
                    frame_pool_free(parser.slot);
                    parser.slot  = NULL;
                    parser.state = 0;
                } else {
                    parser.crc_byte_idx = 0;
                    parser.state        = 5;   /* ST_CRC */
                }
            }
            break;

        /* ===== 状态 5：读 4 字节 CRC32（小端） ===== */
        case 5:   /* ST_CRC */
            parser.crc32 |= ((uint32_t)byte) << (parser.crc_byte_idx * 8);
            parser.crc_byte_idx++;
            if (parser.crc_byte_idx >= 4) {
                parser.data_idx = 0;
                parser.state    = 6;   /* ST_DATA */
            }
            break;

        /* ===== 状态 6：读数据（固定 DATA_FRAME_LEN 字节） ===== */
        case 6:   /* ST_DATA */
            parser.slot->buf[parser.data_idx++] = byte;
            if (parser.data_idx >= DATA_FRAME_LEN) {
                parser.state = 7;   /* ST_TAIL_1 */
            }
            break;

        /* ===== 状态 7：等 0x33 ===== */
        case 7:   /* ST_TAIL_1 */
            if (byte == FRAME_TAIL_1) {
                parser.state = 8;   /* ST_TAIL_2 */
            } else {
                frame_pool_free(parser.slot);
                parser.slot  = NULL;
                parser.state = 0;
            }
            break;

        /* ===== 状态 8：等 0x22，帧完成 ===== */
        case 8:   /* ST_TAIL_2 */
            if (byte == FRAME_TAIL_2) {
                /* 帧完成，校验 CRC */
                parser.crc_calc = calculate_crc32_ex(
                    parser.total_packets,
                    parser.current_packet,
                    parser.data_len,
                    parser.slot->buf
                );

                if (parser.crc_calc == parser.crc32) {
                    /* CRC 通过，交给 Flash 写入 */
                    fw_frame_t frame;
                    frame.total_packets  = parser.total_packets;
                    frame.current_packet = parser.current_packet;
                    frame.data_len       = parser.data_len;
                    frame.crc32          = parser.crc32;
                    frame.slot           = parser.slot;

                    if (xQueueSend(xFrameQueue, &frame, 0) != pdTRUE) {
                        /* 队列满，归还 slot */
                        frame_pool_free(parser.slot);
                    }
                    /* 注意：slot 在这里不 free，由 Flash 任务 free */
                } else {
                    /* CRC 失败，归还 slot */
                    frame_pool_free(parser.slot);
                }
                parser.slot  = NULL;
                parser.state = 0;
                line_len     = 0;
            } else {
                frame_pool_free(parser.slot);
                parser.slot  = NULL;
                parser.state = 0;
            }
            break;

        default:
            parser.state = 0;
            break;
        }
    }
}

/* ==================== Flash 写任务 ==================== */

void task_ota_flash(void *pvParameters)
{
    fw_frame_t frame;
    uint32_t   write_addr;
    uint32_t   received_count = 0;   /* 已成功写入的数据帧数，用于收齐校验 */
    int32_t    erased_page    = -1;  /* 已擦除的 OTA 区页号；-1 表示本次尚未擦除 */

    while (1)
    {
        if (xQueueReceive(xFrameQueue, &frame, portMAX_DELAY) != pdTRUE)
            continue;

        /* 判断是否是结束帧 */
        if (frame.current_packet == PACKET_END) {
            uint32_t fw_size = 0;
            uint32_t fw_crc  = 0;

            /* 结束帧负载：data[0:4]=固件总长(小端), data[4:8]=整包 CRC32(小端) */
            if (frame.data_len >= 8) {
                fw_size = (uint32_t)frame.slot->buf[0]
                        | ((uint32_t)frame.slot->buf[1] << 8)
                        | ((uint32_t)frame.slot->buf[2] << 16)
                        | ((uint32_t)frame.slot->buf[3] << 24);
                fw_crc  = (uint32_t)frame.slot->buf[4]
                        | ((uint32_t)frame.slot->buf[5] << 8)
                        | ((uint32_t)frame.slot->buf[6] << 16)
                        | ((uint32_t)frame.slot->buf[7] << 24);
            }
            frame_pool_free(frame.slot);

            /* 收齐校验：已收数据帧数必须等于总包数，否则丢弃本次升级 */
            if (fw_size != 0 && received_count == frame.total_packets) {
                g_firmware_size = fw_size;
                g_firmware_crc  = fw_crc;
                xTaskNotify(xrequest_upgrade_taskHandle, 0x01, eSetValueWithOverwrite);
            }
            received_count = 0;
            erased_page    = -1;   /* 本次传输结束，下次重新擦除 */
            continue;            /* 结束帧不写 Flash，直接继续 */
        }

        /* 新一轮传输开始，清零计数并强制重新擦除 */
        if (frame.current_packet == 0) {
            received_count = 0;
            erased_page    = -1;
        }

        write_addr = OTA_BUFFER_BASE + ((uint32_t)frame.current_packet * DATA_FRAME_LEN);

        /* 边界保护：不能写到 config 区及以后 */
        if (write_addr + DATA_FRAME_LEN > CONFIG_BASE) {
            frame_pool_free(frame.slot);
            continue;
        }

        /* Flash 只能把 1 写成 0，写入前必须先擦除所在页（按页惰性擦除，每 4 帧一次） */
        {
            int32_t page = (int32_t)((write_addr - OTA_BUFFER_BASE) / FLASH_PAGE_SIZE);
            if (page != erased_page) {
                if (flash_erase(OTA_BUFFER_BASE + (uint32_t)page * FLASH_PAGE_SIZE,
                                FLASH_PAGE_SIZE) != Status_OK) {
                    frame_pool_free(frame.slot);
                    continue;
                }
                erased_page = page;
            }
        }

        /* 写 Flash，只写实际长度，不写 padding */
        if (flash_write(write_addr, frame.slot->buf, frame.data_len) == Status_OK)
            received_count++;   /* 写入成功，计入已收帧数 */
        else {
            /* 写失败，可以记录错误，忽略 */
        }

        /* 归还 slot */
        frame_pool_free(frame.slot);
    }
}

// Flash 擦除
StatusTypeDef flash_erase(uint32_t start_addr, uint32_t size)
{
    uint32_t page_addr;
    uint32_t end_addr = start_addr + size;
    FLASH_Status status;

    // 向下对齐到页起始
    page_addr = start_addr & ~(FLASH_PAGE_SIZE - 1);

    FLASH_Unlock();

    while (page_addr < end_addr) {
        status = FLASH_ErasePage(page_addr);
        if (status != FLASH_COMPLETE) {
            FLASH_Lock();
            return Status_ERROR;
        }
        page_addr += FLASH_PAGE_SIZE;
    }

    FLASH_Lock();
    return Status_OK;
}

// Flash 写入
StatusTypeDef flash_write(uint32_t dest_addr, uint8_t *src, uint32_t size)
{
    uint32_t i;
    uint32_t word;
    FLASH_Status status;

    FLASH_Unlock();

    for (i = 0; i < size; i += 4) {
        // 最后一个不足 4 字节时用 0xFF 补齐（Flash 擦除后默认值）
        uint8_t b0 = (i + 0 < size) ? src[i + 0] : 0xFF;
        uint8_t b1 = (i + 1 < size) ? src[i + 1] : 0xFF;
        uint8_t b2 = (i + 2 < size) ? src[i + 2] : 0xFF;
        uint8_t b3 = (i + 3 < size) ? src[i + 3] : 0xFF;

        word = (uint32_t)b0
             | ((uint32_t)b1 << 8)
             | ((uint32_t)b2 << 16)
             | ((uint32_t)b3 << 24);

        status = FLASH_ProgramWord(dest_addr + i, word);
        if (status != FLASH_COMPLETE) {
            FLASH_Lock();
            return Status_ERROR;
        }
    }

    FLASH_Lock();
    return Status_OK;
}


void request_upgrade_task(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {    
        /* 等待 task_ota_flash 发来的升级通知 */
        xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);

        if (xWifisendTaskHandle != NULL)
            xTaskNotify(xWifisendTaskHandle, 0x02, eSetValueWithOverwrite);
        ota_config_t config;
        config.magic       = OTA_MAGIC;
        config.update_flag = 1;
        config.version     = CURRENT_VERSION;
        config.firmware_size = g_firmware_size;
        config.crc32         = g_firmware_crc;

        __disable_irq();   //关闭全局中断，防止读写过程被打断
        
        // 擦除配置区所在页，写入 config
        flash_erase(CONFIG_BASE, sizeof(ota_config_t));
        flash_write(CONFIG_BASE, (uint8_t*)&config, sizeof(ota_config_t));

        // 软复位
        NVIC_SystemReset();
    }
}
