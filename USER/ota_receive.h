#ifndef OTA_RECEIVE_H
#define OTA_RECEIVE_H

#include "stm32f10x.h"
#include <stdint.h>
#include <string.h>
#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "queue.h"
#include "task.h"

/* ==================== Flash 地址 ==================== */
#define BOOTLOADER_BASE     0x08000000
#define APPLICATION_BASE    0x08003000
#define OTA_BUFFER_BASE     0x0800C000
#define CONFIG_BASE         0x0800F800
#define OTA_BUFFER_SIZE     (36 * 1024)
#define FLASH_PAGE_SIZE     1024

/* ==================== OTA 参数区结构 ==================== */
#define OTA_MAGIC           0xDEADBEEFU
#define CURRENT_VERSION     0x00010000U   /* V1.0 */

#pragma pack(1)
typedef struct {
    uint32_t magic;
    uint32_t update_flag;
    uint32_t version;
    uint32_t firmware_size;
    uint32_t crc32;
} ota_config_t;
#pragma pack()

/* ==================== 帧格式 ==================== */
#define DATA_FRAME_LEN      256
#define FRAME_POOL_SIZE     4
#define STREAM_BUF_SIZE     1024
#define FRAME_QUEUE_DEPTH   4
#define AT_LINE_MAX         128

#define FRAME_HEADER_1      0x55
#define FRAME_HEADER_2      0x44
#define FRAME_TAIL_1        0x33
#define FRAME_TAIL_2        0x22
#define PACKET_END          0xFF

/* ==================== 状态枚举 ==================== */
typedef enum {
    Status_OK      = 0x00,
    Status_ERROR   = 0x01,
    Status_BUSY    = 0x02,
    Status_TIMEOUT = 0x03
} StatusTypeDef;

/* ==================== 数据结构 ==================== */
typedef struct {
    uint8_t  buf[DATA_FRAME_LEN];
    uint8_t  in_use;
} frame_slot_t;

typedef struct {
    uint8_t       total_packets;
    uint8_t       current_packet;
    uint16_t      data_len;          /* 实际长度 2 字节，最大 256 */
    uint32_t      crc32;
    frame_slot_t *slot;
} fw_frame_t;

typedef struct {
    uint8_t   state;
    uint8_t   total_packets;
    uint8_t   current_packet;
    uint16_t  data_len;
    uint32_t  crc32;
    uint32_t  crc_calc;
    uint16_t  data_idx;
    frame_slot_t *slot;
    uint8_t   crc_byte_idx;
    uint8_t   len_byte_idx;          /* 读取 2 字节实际长度用的计数 */
} parser_t;

/* ==================== 全局句柄 ==================== */
extern StreamBufferHandle_t uartStreamBuffer;
extern QueueHandle_t        xFrameQueue;
extern QueueHandle_t        atQueue;
extern TaskHandle_t xrequest_upgrade_taskHandle;
extern TaskHandle_t xWifisendTaskHandle;

/* ==================== 函数声明 ==================== */
void           ota_receive_init(void);
frame_slot_t*  frame_pool_alloc(void);
void           frame_pool_free(frame_slot_t *slot);
uint32_t       calculate_crc32_ex(uint8_t total, uint8_t current, uint16_t len, uint8_t *data);
void           trigger_upgrade(void);
void           wifi_get_task(void *pvParameters);
void           task_ota_flash(void *pvParameters);
void           request_upgrade_task(void *pvParameters);

/* Flash 接口 */
extern StatusTypeDef  flash_erase(uint32_t start_addr, uint32_t size);
extern StatusTypeDef  flash_write(uint32_t dest_addr, uint8_t *src, uint32_t size);

#endif /* OTA_RECEIVE_H */
