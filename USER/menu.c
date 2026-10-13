#include "menu.h"
#include "FreeRTOS.h"
#include "rtc.h"
#include "oled.h"
#include "oledfont.h"  
#include "bmp.h"
#include "queue.h"
#include "task.h"
#include "key.h"
#include "led.h"
#include "uart.h"

// xQueueHandle menu_queue;
xQueueHandle xQueueKey;

menu_page_t menu_stack[MENU_STACK_MAX];//菜单栈
int menu_top = -1;   // 栈顶索引
send_data_t send_data = {0, 3}; // 新建发送数据结构体并初始化
area_weather_t area_weather_data = {0, 0, 0, 0, 0, NULL};// 新建地区天气数据结构体并初始化
TaskHandle_t xWifisendTaskHandle;   // wifi_send_task 的任务句柄

const unsigned char *texts[11][4] = {
    {Hzk4[0] , Hzk4[1] , Hzk4[2] , Hzk4[3] },//设置时间
    {Hzk4[0] , Hzk4[1] , Hzk4[4] , Hzk4[5] },//设置日期
    {Hzk4[6] , Hzk4[7] , Hzk4[4] , Hzk4[5] },//更新时间
    {Hzk4[10], Hzk4[11], Hzk4[8] , Hzk4[9] },//添加闹钟
    {Hzk4[12], Hzk4[13], Hzk4[8] , Hzk4[9] },//删除闹钟
    {Hzk4[14], Hzk4[15], Hzk4[8] , Hzk4[9] },//打开闹钟
    {Hzk4[16], Hzk4[17], Hzk4[8] , Hzk4[9] },//关闭闹钟
    {Hzk4[18], Hzk4[19], Hzk4[20], Hzk4[21]},//选择地区
    {Hzk4[22], Hzk4[23]                    },//杭州
    {Hzk4[24], Hzk4[25]                    },//上海
    {Hzk4[26], Hzk4[27],                   }//深圳
};

const unsigned char *areas[4][4] = {
    {Hzk4[22], Hzk4[23]                    },//杭州
    {Hzk4[24], Hzk4[25]                    },//上海
    {Hzk4[26], Hzk4[27],                   },//深圳
    {0}
};

const menu_item_t alarm_menu[] = {
    {"Alarm Add", alarm_add_page, NULL, 0, texts[3]},
    {"Alarm Delete", alarm_delete_page, NULL, 0, texts[4]},
    {"Alarm Enable", alarm_enable_page, NULL, 0, texts[5]},
    {"Alarm Disable", alarm_disable_page, NULL, 0, texts[6]},
};

const menu_item_t area_select[] = {
    {"hangzhou", send1, NULL, 0, texts[8]},
    {"shanghai", send2, NULL, 0, texts[9]},
    {"shenzhen", send3, NULL, 0, texts[10]},
};

const menu_item_t time_menu[] = {
    {"Set Time", time_set_page, NULL, 0, texts[0]},
    {"Set Date", date_set_page, NULL, 0, texts[1]},
    {"Update Time", date_update_page, NULL, 0, texts[2]},
};

const menu_item_t area_menu[] = {
    {"Select Area", NULL, area_select, 3, texts[7]},
};

const menu_item_t main_menu[] = {
    {"Time", NULL, time_menu, 3},
    {"Alarm", NULL, alarm_menu, 4},
    {"Area", NULL, area_menu, 1},
};

const unsigned char *week_name[] = {
    Hzk3[1], Hzk3[2], Hzk3[3], Hzk3[4], Hzk3[5], Hzk3[6], Hzk3[7]
};

const unsigned char *weather_name[] = {
    BMP4, BMP4, BMP5, BMP6, BMP7, BMP8
};

void menu_push(const menu_item_t *items, uint8_t count)
{
    if (menu_top < MENU_STACK_MAX - 1) {
        menu_top++;
        menu_stack[menu_top].menu_items = items;
        menu_stack[menu_top].menu_index = 0;
        menu_stack[menu_top].menu_item_count = count;
    }
}

void menu_pop(void)
{
    if (menu_top >= 0) {
        menu_top--;
    }

}

menu_page_t *menu_current(void)//当前菜单
{
    if (menu_top >= 0) {
        return &menu_stack[menu_top];
    }
    return NULL;
}

void menu_render(void)//渲染选择菜单
{
    menu_page_t *p = menu_current();
    if (p == NULL) return; // 栈为空，直接返回
    OLED_Clear();
    if (p->menu_items[p->menu_index].next_items == time_menu) {
        // OLED_ShowString(98, 0, (u8*)"Set Time", 16);
        OLED_ShowPicture(32, 0, 96, 8, BMP2);
    }
    else if (p->menu_items[p->menu_index].next_items == alarm_menu) {
        // OLED_ShowString(98, 0, (u8*)"Set Alarm", 16);
        OLED_ShowPicture(32, 0, 96, 8, BMP3);
    }
    else if (p->menu_items[p->menu_index].next_items == area_menu) {
        // OLED_ShowString(98, 0, (u8*)"Select Area", 16);
        OLED_ShowPicture(32, 0, 96, 7, BMP1);
    }
    else {
        int total = p->menu_item_count;
        int idx   = p->menu_index;
        int WIN   = 3;

        int start = idx;
        if (start + WIN > total) start = total - WIN;
        if (start < 0) start = 0;

        for (int row = 0; row < WIN; row++) {
            int item = start + row;
            if (item < total) {
                char line[32];
                int i=0;
                while(i<4 && p->menu_items[item].text[i] != NULL) {
                    sprintf(line, "%c ",
                        (item == idx) ? '>' : ' ');
                    OLED_ShowString(0, row * 18, (u8*)line, 12);
                    OLED_ShowChinese(24+i * 15, row * 18, p->menu_items[item].text[i]);
                    i++;
                }
            }
        }
    }
    OLED_Refresh();
}

void menu_key_handler(key_event_t key)
{
    menu_page_t *p = menu_current();
    if (p == NULL) return; 
    switch (key) {
        case KEY_UP:   if (p->menu_index > 0) {p->menu_index--;}else {p->menu_index = p->menu_item_count - 1;} break;
        case KEY_DOWN: if (p->menu_index < p->menu_item_count - 1) {p->menu_index++;} else {p->menu_index = 0;} break;
        case KEY_OK:
            if (p->menu_items[p->menu_index].action)
                p->menu_items[p->menu_index].action();
            if (p->menu_items[p->menu_index].next_items)
                menu_push(p->menu_items[p->menu_index].next_items,
                          p->menu_items[p->menu_index].next_count);
            break;
        case KEY_BACK: menu_pop(); if (menu_top == -1) main_window_render(); break;
        case NO_PRESS: break;
    }
}

void key_task(void *pvParameters)
{
    key_event_t evt;

    while (1) {
        evt = KEY_Scan();
        if (evt != NO_PRESS) {
            xQueueSend(xQueueKey, &evt, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(20));  // 去抖
    }
}

void oledmenu_task(void *pvParameters)
{
    key_event_t evt;

    main_window_render();   // 开机先显示一次

    while (1) {
        if (xQueueReceive(xQueueKey, &evt, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if(menu_top == -1) {
                // 如果栈为空，按下任意键进入主菜单
                menu_push(main_menu, sizeof(main_menu)/sizeof(main_menu[0]));
                menu_render(); 
                continue;
            }
            menu_key_handler(evt);
            menu_render();
        } else{
            // 没有按键事件,栈为空，刷新主界面时间显示
            if (menu_top == -1) {
                main_window_render();
            }
        }

    }
}

void main_window_render(void)
{
    
    menu_top = -1;

    OLED_Clear();
    char line1[32], line2[32],line3[10], line4[16];
    sprintf(line1, "%02d-%02d-%02d", calendar.w_year % 100, calendar.w_month, calendar.w_date);
    sprintf(line2, "%02d:%02d:%02d", calendar.hour, calendar.min, calendar.sec);
    sprintf(line4, "%d,%d-%d", area_weather_data.current_temp,area_weather_data.area_temperature_high, area_weather_data.area_temperature_low);

    OLED_ShowString(0, 0 , (u8*)line1, 12);//日期
    OLED_ShowString(0, 17, (u8*)line2, 12);//时间

    OLED_ShowChinese(0, 33, Hzk3[0]);//星期
    OLED_ShowChinese(15, 33, week_name[calendar.week]);
    if(send_data.area_index < 3) {
        OLED_ShowChinese(30, 33, areas[send_data.area_index-1][0]);
        OLED_ShowChinese(45, 33, areas[send_data.area_index-1][1]);
    }

    OLED_ShowChinese(0 ,48, Hzk1[0]);//当
    OLED_ShowChinese(15,48, Hzk1[1]);//前
    OLED_ShowChinese(30,48, Hzk1[2]);//气
    OLED_ShowChinese(45,48, Hzk1[3]);//温
    OLED_ShowString(60, 48, (u8*)line4, 12);

    OLED_ShowPicture(80, 0, 120, 5, weather_name[area_weather_data.area_weather]);


    OLED_Refresh();

}

/* ================= 闹钟页 ================= */
#define TIMESET_LONG_PRESS_MS   800   // 长按判定阈值(确认/删除)
#define TIMESET_REPEAT_START_MS 300   // 长按连发起始间隔
#define TIMESET_REPEAT_MIN_MS   60    // 长按连发最快间隔

// 全局只有一个闹钟
static uint8_t alarm_hour = 0;
static uint8_t alarm_min  = 0;
static uint8_t alarm_set  = 0;

// 调节 时/分(field 0=时 1=分)，按 时:分 滚动进位(满60进位，24小时循环)
// 不涉及日期/月份
static void alarm_adjust(uint8_t *hh, uint8_t *mm, uint8_t field, int delta)
{
    int32_t step  = (field == 0) ? 60 : 1;      // 时=60分, 分=1分
    int32_t total = (int32_t)(*hh) * 60 + (int32_t)(*mm);
    int32_t v = total + step * delta;

    v %= 1440;
    if (v < 0) v += 1440;

    *hh = (uint8_t)(v / 60);
    *mm = (uint8_t)(v % 60);
}

// 渲染添加闹钟页，选中字段下方画下划线
static void alarm_add_render(uint8_t hh, uint8_t mm, uint8_t field)
{
    char buf[8];
    sprintf(buf, "%02d:%02d", hh, mm);

    OLED_Clear();
    OLED_ShowString(0, 0, (u8*)"Alarm Add", 12);
    OLED_ShowString(34, 28, (u8*)buf, 24);

    uint8_t x = 34;                 // 时
    if (field == 1) x = 70;         // 分
    OLED_DrawLine(x, 54, (uint8_t)(x + 23), 54);

    OLED_Refresh();
}

// 首次调节一次，若按键仍按住则连发并逐渐加速
static void alarm_repeat_change(uint8_t *hh, uint8_t *mm, uint8_t field, int delta, uint8_t key_id)
{
    uint32_t delay = TIMESET_REPEAT_START_MS;

    alarm_adjust(hh, mm, field, delta);
    alarm_add_render(*hh, *mm, field);

    while ((key_id == 0) ? (KEY0 == 0) : (KEY1 == 0))
    {
        vTaskDelay(pdMS_TO_TICKS(delay));
        if (!((key_id == 0) ? (KEY0 == 0) : (KEY1 == 0)))
            break;
        alarm_adjust(hh, mm, field, delta);
        alarm_add_render(*hh, *mm, field);
        delay = delay * 6 / 10;                 // 每次加速
        if (delay < TIMESET_REPEAT_MIN_MS) delay = TIMESET_REPEAT_MIN_MS;
    }
}

// 把闹钟设到下一个 hour:min 时刻(直接用计数器推进，不做日期/月份运算)
static void alarm_program(uint8_t hour, uint8_t min)
{
    uint32_t now    = RTC_GetCounter();
    uint32_t nowsec = now % 86400;
    uint32_t target = (uint32_t)hour * 3600 + (uint32_t)min * 60;
    uint32_t delta  = (target > nowsec) ? (target - nowsec)
                                        : (target + 86400 - nowsec);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetAlarm(now + delta);
    RTC_WaitForLastTask();
    RTC_ITConfig(RTC_IT_ALR, ENABLE);
    RTC_WaitForLastTask();
}

// 删除闹钟：关闭闹钟中断并清标志
static void alarm_clear(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_ITConfig(RTC_IT_ALR, DISABLE);
    RTC_ClearITPendingBit(RTC_IT_ALR);
    RTC_WaitForLastTask();
    alarm_set = 0;
}

void alarm_add_page(void)
{
    uint8_t hh = alarm_set ? alarm_hour : calendar.hour;
    uint8_t mm = alarm_set ? alarm_min  : calendar.min;
    uint8_t field = 0;      // 0=时 1=分
    key_event_t evt;

    alarm_add_render(hh, mm, field);

    while (1)
    {
        if (xQueueReceive(xQueueKey, &evt, portMAX_DELAY) != pdTRUE)
            continue;

        switch (evt)
        {
        case KEY_UP:
            alarm_repeat_change(&hh, &mm, field, +1, 0);
            break;

        case KEY_DOWN:
            alarm_repeat_change(&hh, &mm, field, -1, 1);
            break;

        case KEY_OK:
        {
            // 区分短按(切换字段)与长按(确认设置)
            TickType_t t0 = xTaskGetTickCount();
            while (KEY2 == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
                if ((xTaskGetTickCount() - t0) >= pdMS_TO_TICKS(TIMESET_LONG_PRESS_MS))
                    break;
            }

            if ((xTaskGetTickCount() - t0) >= pdMS_TO_TICKS(TIMESET_LONG_PRESS_MS))
            {
                // 长按：设置闹钟并退出
                alarm_hour = hh;
                alarm_min  = mm;
                alarm_set  = 1;
                alarm_program(hh, mm);
                return;
            }
            else
            {
                // 短按：时<->分 切换，等按键释放
                while (KEY2 == 0) vTaskDelay(pdMS_TO_TICKS(10));
                field = (uint8_t)((field + 1) % 2);
                alarm_add_render(hh, mm, field);
            }
            break;
        }

        case KEY_BACK:
            return;     // 取消，不保存

        default:
            break;
        }
    }
}

void alarm_delete_page(void)
{
    key_event_t evt;
    char buf[20];

    OLED_Clear();
    OLED_ShowString(0, 0, (u8*)"Delete Alarm", 12);
    if (alarm_set)
        sprintf(buf, "Alarm %02d:%02d", alarm_hour, alarm_min);
    else
        sprintf(buf, "No Alarm");
    OLED_ShowString(0, 24, (u8*)buf, 12);
    OLED_ShowString(0, 44, (u8*)"Hold OK to del", 12);
    OLED_Refresh();

    while (1)
    {
        if (xQueueReceive(xQueueKey, &evt, portMAX_DELAY) != pdTRUE)
            continue;

        switch (evt)
        {
        case KEY_OK:
        {
            // 长按 OK 删除闹钟
            TickType_t t0 = xTaskGetTickCount();
            while (KEY2 == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
                if ((xTaskGetTickCount() - t0) >= pdMS_TO_TICKS(TIMESET_LONG_PRESS_MS))
                    break;
            }

            if ((xTaskGetTickCount() - t0) >= pdMS_TO_TICKS(TIMESET_LONG_PRESS_MS))
            {
                while (KEY2 == 0) vTaskDelay(pdMS_TO_TICKS(10));
                alarm_clear();
                return;     // 返回闹钟菜单
            }
            break;
        }

        case KEY_BACK:
            return;     // 取消返回

        default:
            break;
        }
    }
}

void alarm_enable_page(void)
{
    // Implementation for alarm enable page
    main_window_render();  // For now, just render the main window
}

void alarm_disable_page(void)
{
    // Implementation for alarm disable page
    main_window_render();  // For now, just render the main window
}

/* ================= 设置时间页 ================= */
// 长按/连发参数(TIMESET_LONG_PRESS_MS 等)见上方“闹钟页”处的宏定义

// 调节当前字段：field 0=时 1=分 2=秒
// 按当前字段步长(时=3600s, 分=60s, 秒=1s)加减，并做 时:分:秒 滚动进位/借位
// (满 60 向上进位，减到 0 向下借位，24 小时循环)
static void timeset_adjust(uint8_t *hh, uint8_t *mm, uint8_t *ss, uint8_t field, int delta)
{
    int32_t step  = (field == 0) ? 3600 : ((field == 1) ? 60 : 1);
    int32_t total = (int32_t)(*hh) * 3600 + (int32_t)(*mm) * 60 + (int32_t)(*ss);
    int32_t v = total + step * delta;

    v %= 86400;
    if (v < 0) v += 86400;

    *hh = (uint8_t)(v / 3600);
    *mm = (uint8_t)((v % 3600) / 60);
    *ss = (uint8_t)(v % 60);
}

// 渲染设置页，选中字段下方画下划线
static void timeset_render(uint8_t hh, uint8_t mm, uint8_t ss, uint8_t field)
{
    char buf[12];
    sprintf(buf, "%02d:%02d:%02d", hh, mm, ss);

    OLED_Clear();
    OLED_ShowString(0, 0, (u8*)"Time Set", 12);
    OLED_ShowString(16, 28, (u8*)buf, 24);

    uint8_t x = 16;                     // 时
    if (field == 1)      x = 52;        // 分
    else if (field == 2) x = 88;        // 秒
    OLED_DrawLine(x, 54, (uint8_t)(x + 23), 54);

    OLED_Refresh();
}

// 首次调节一次，若按键仍按住则连发并逐渐加速
static void timeset_repeat_change(uint8_t *hh, uint8_t *mm, uint8_t *ss, uint8_t field, int delta, uint8_t key_id)
{
    uint32_t delay = TIMESET_REPEAT_START_MS;

    timeset_adjust(hh, mm, ss, field, delta);
    timeset_render(*hh, *mm, *ss, field);

    while ((key_id == 0) ? (KEY0 == 0) : (KEY1 == 0))
    {
        vTaskDelay(pdMS_TO_TICKS(delay));
        if (!((key_id == 0) ? (KEY0 == 0) : (KEY1 == 0)))
            break;
        timeset_adjust(hh, mm, ss, field, delta);
        timeset_render(*hh, *mm, *ss, field);
        delay = delay * 6 / 10;                 // 每次加速
        if (delay < TIMESET_REPEAT_MIN_MS) delay = TIMESET_REPEAT_MIN_MS;
    }
}

void time_set_page(void)
{
    uint8_t hh = calendar.hour;
    uint8_t mm = calendar.min;
    uint8_t ss = calendar.sec;
    uint8_t field = 0;      // 0=时 1=分 2=秒
    key_event_t evt;

    timeset_render(hh, mm, ss, field);

    while (1)
    {
        if (xQueueReceive(xQueueKey, &evt, portMAX_DELAY) != pdTRUE)
            continue;

        switch (evt)
        {
        case KEY_UP:
            timeset_repeat_change(&hh, &mm, &ss, field, +1, 0);
            break;

        case KEY_DOWN:
            timeset_repeat_change(&hh, &mm, &ss, field, -1, 1);
            break;

        case KEY_OK:
        {
            // 区分短按(切换字段)与长按(保存退出)
            TickType_t t0 = xTaskGetTickCount();
            while (KEY2 == 0)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
                if ((xTaskGetTickCount() - t0) >= pdMS_TO_TICKS(TIMESET_LONG_PRESS_MS))
                    break;
            }

            if ((xTaskGetTickCount() - t0) >= pdMS_TO_TICKS(TIMESET_LONG_PRESS_MS))
            {
                // 长按：将 时:分:秒 写入 RTC 并退出
                RTC_EnterConfigMode();
                RTC_Set(calendar.w_year, calendar.w_month, calendar.w_date, hh, mm, ss);
                RTC_ExitConfigMode();
                calendar.hour = hh;
                calendar.min  = mm;
                calendar.sec  = ss;
                return;
            }
            else
            {
                // 短按：时->分->秒 循环切换，等按键释放
                while (KEY2 == 0) vTaskDelay(pdMS_TO_TICKS(10));
                field = (uint8_t)((field + 1) % 3);
                timeset_render(hh, mm, ss, field);
            }
            break;
        }

        case KEY_BACK:
            return;     // 取消，不保存

        default:
            break;
        }
    }
}

void date_set_page(void)
{
    // Implementation for date set page
    main_window_render();  // For now, just render the main window
}

void date_update_page(void)
{
    // Implementation for date update page
    send_data.get_time = 0x01; // Set the flag to get time
    xTaskNotify(xWifisendTaskHandle, 0x01, eSetValueWithOverwrite);
    main_window_render();  // For now, just render the main window
}

void send1(void)
{
    // Implementation for sending area code
    send_data.area_index = 0x01;
    xTaskNotify(xWifisendTaskHandle, 0x01, eSetValueWithOverwrite);
    main_window_render();
}

void send2(void)
{
    // Implementation for sending area code
    send_data.area_index = 0x02;
    xTaskNotify(xWifisendTaskHandle, 0x01, eSetValueWithOverwrite);
    main_window_render();
}

void send3(void)
{
    // Implementation for sending area code
    send_data.area_index = 0x03;
    xTaskNotify(xWifisendTaskHandle, 0x01, eSetValueWithOverwrite);
    main_window_render();

}
