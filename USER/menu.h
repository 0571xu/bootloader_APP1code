#ifndef __MENU_H
#define __MENU_H

#include "stm32f10x.h"
#include "stdio.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "key.h"

#define MENU_STACK_MAX 9

typedef struct menu_item_s 
{
    const char *next_menu_name;
    void (*action)(void);  // 可选：确认时执行的动作
    const struct menu_item_s *next_items;   // 子菜单数组
    uint8_t next_count;    // 子菜单项数
    const unsigned char **text; // 可选：显示的文本内容
}menu_item_t;

typedef struct
{
    const menu_item_t *menu_items;
    uint8_t menu_index;
    uint8_t menu_item_count;
}menu_page_t;


extern menu_page_t menu_stack[MENU_STACK_MAX];
extern xQueueHandle xQueueKey;
typedef struct
{
    uint8_t get_time; //是否获取时间
    uint8_t area_index; // 当前选择的地区索引
}send_data_t;

typedef struct
{
    uint8_t area_weather; //地区天气
    uint8_t area_temperature_high; //地区最高温度
    uint8_t area_temperature_low; //地区最低温度
    uint8_t area_humidity; //地区湿度
    uint8_t current_temp; //当前温度
}area_weather_t;

extern area_weather_t area_weather_data; // 全局地区天气数据结构体
extern send_data_t send_data; // 全局发送数据结构体
extern TaskHandle_t xWifisendTaskHandle;   // wifi_send_task 的任务句柄

void oledmenu_task(void *pvParameters);
void menu_push(const menu_item_t *items, uint8_t count);
void menu_pop(void);
menu_page_t *menu_current(void);
void menu_render(void);
void menu_key_handler(key_event_t key);
void key_task(void *pvParameters);
void main_window_render(void);
void alarm_add_page(void);
void alarm_delete_page(void);
void alarm_enable_page(void);
void alarm_disable_page(void);
void time_set_page(void);
void date_set_page(void);
void date_update_page(void);
void send1(void);
void send2(void);
void send3(void);

#endif
