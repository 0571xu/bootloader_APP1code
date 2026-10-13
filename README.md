# STM32F103C8T6 OTA 串口升级

基于 STM32F103C8T6 的 Bootloader + APP 双镜像 OTA 方案，通过 USART1 逐帧接收固件、写入 Flash 暂存区，再由 Bootloader 拷贝到 APP 区并跳转执行。

- MCU：STM32F103C8T6（Cortex-M3，64KB Flash / 20KB RAM）
- 库：ST 标准外设库 SPL 3.0.0
- RTOS：FreeRTOS（heap_4，`configTOTAL_HEAP_SIZE` = 10KB）
- IDE：Keil uVision5（ARMCC V5.06）
- 串口：USART1，115200 8-N-1（PC 侧用 COM4）

## 一、Flash 分区

| 区域 | 起始地址 | 大小 | 说明 |
|---|---|---|---|
| Bootloader | `0x08000000` | 12 KB | 检查升级标志、拷贝固件、跳转 |
| APP | `0x08003000` | 36 KB | 正常运行的程序 / OTA 接收端 |
| OTA 暂存区 | `0x0800C000` | **14 KB**（`0x0800C000~0x0800F800`） | 新固件先收到这里 |
| 配置区 | `0x0800F800` | 2 KB | `ota_config_t`（magic / 标志 / 大小 / CRC） |

> `FLASH_PAGE_SIZE = 1024`。注意 `ota_receive.h` 里的 `OTA_BUFFER_SIZE` 宏是 **36KB**，写的是 APP 区大小；OTA 暂存区**实际只有 14336 字节**，待升级固件必须 ≤ 14336 且 4 字节对齐。

### 升级标志结构 `ota_config_t`（`#pragma pack(1)`）

```c
typedef struct {
    uint32_t magic;         // OTA_MAGIC = 0xDEADBEEF
    uint32_t update_flag;   // 1 = 需要升级
    uint32_t version;
    uint32_t firmware_size; // 固件总长（字节）
    uint32_t crc32;         // 整包 CRC32
} ota_config_t;
```

## 二、帧格式（固定 268 字节）

```
┌──────┬──────┬────────┬────────┬──────────┬────────┬───────────────────┬──────┬──────┐
│ 0x55 │ 0x44 │ 总包数 │ 当前包 │ 实际长度 │ CRC32  │ 数据（256B 固定） │ 0x33 │ 0x22 │
│  2B  │      │  1B    │  1B    │   2B     │  4B    │ 不足补 0x00       │  2B  │      │
└──────┴──────┴────────┴────────┴──────────┴────────┴───────────────────┴──────┴──────┘
```

- **总包数**：数据帧数量，**不含结束帧**（1 字节，最大 255）
- **当前包号**：`0 ~ 254`；`0xFF` 为结束帧
- **实际长度**：本包有效数据字节数，**2 字节小端**（最大 256）
- **帧总长**：`2 + 1 + 1 + 2 + 4 + 256 + 2 = 268`

### 结束帧（`current_packet = 0xFF`）

| 偏移 | 内容 |
|---|---|
| `data[0:4]` | 固件总长度（小端） |
| `data[4:8]` | 整包 CRC32（小端） |
| `data_len` | 8 |

### CRC32 说明（重要）

采用 **STM32 硬件 CRC 等价算法**：多项式 `0x04C11DB7`，初值 `0xFFFFFFFF`，不反射，无最终异或。

**关键坑**：STM32 硬件 CRC 是「**按 32 位字**送入、**字内 MSB 先出**」，且按小端从内存读字，因此每个 4 字节组的字节处理顺序与标准逐字节 CRC **正好相反**。PC 端脚本必须按「字」计算，否则每一帧校验都会失败。

- 帧内 CRC 范围：4 字节头（`total | current<<8 | len<<16`）组成 1 个字 + 实际数据（不足 4 字节用 `0xFF` 补齐）
- 整包 CRC 范围：整个固件（按 `size/4` 个字，要求 4 字节对齐）

## 三、接收端状态机

`wifi_get_task` 逐字节解析（`ota_receive.c`）：

```
0 等 0x55 ──0x55──▶ 1 等 0x44 ──0x44──▶ 2 读总包数 ─▶ 3 读当前包
   ▲                                              │
   │                                              ▼
   │                                   4 读实际长度(2B) ──非法──▶ 回 0
   │                                              │
   │                                              ▼
   │                                   5 读 CRC32(4B) ─▶ 6 读数据(256B)
   │                                                          │
   │                                                          ▼
   └─── 出错回 0 ◀── 8 等 0x22 ◀── 7 等 0x33 ◀────────────────┘
                        │
                   校验 CRC → 通过则入队 xFrameQueue
```

`task_ota_flash` 出队后写入 Flash：

- 写地址 = `OTA_BUFFER_BASE + current_packet * 256`
- **按页惰性擦除**：同一页（1KB = 4 帧）只擦一次，避免整片擦除期间丢帧
- 边界保护：超过 `CONFIG_BASE` 直接丢弃
- 收齐校验：`received_count == total_packets` 才触发升级

`request_upgrade_task` 收到结束帧通知后：写 `ota_config_t` → `NVIC_SystemReset()`。

## 四、Bootloader 流程

```
上电 → SystemInit → 打印 "bootloader begin!"
     → 读 CONFIG_BASE：magic==0xDEADBEEF && update_flag==1 ?
        是 → 打印 "upgrading!" → perform_firmware_update()
             ├ CRC 校验 OTA 区（firmware_size / 4 个字）
             ├ 擦除 APP 区 → 拷贝 OTA 区 → APP 区
             ├ 无论成败都清除 update_flag
             └ 打印 "upgrade success!" / "upgrade fail!"
     → check_application_valid(0x08003000) ？ → jump_to_application()
     → 否则死循环点灯 + 串口发 'a'/'b'
```

## 五、构建与烧录

`USER/main.c` 顶部切换编译目标：

```c
#define bootloader  0   // 1 = 编译 Bootloader
#define application 1   // 1 = 编译 APP（OTA 接收端）
```

- 编译 Bootloader：`bootloader=1 / application=0` → 烧到 `0x08000000`
- 编译 APP：`bootloader=0 / application=1` → 烧到 `0x08003000`
- 命令行编译（Keil）：

```powershell
C:\Keil_v5\UV4\UV4.exe -j0 -b "USER\STM32-DEMO.uvprojx" -o build_out.txt
```

> APP 内 `SCB->VTOR = 0x08003000`，务必与 `APPLICATION_BASE` 一致。

## 六、上位机发送工具

`ota_send.py`（需 `pyserial`）：

```powershell
pip install pyserial

# 发送 hex / bin
D:/Anaconda/python.exe ota_send.py firmware.hex -p COM4 -b 115200 -d 0.05

# 只打包不发送，检查帧结构 / CRC
D:/Anaconda/python.exe ota_send.py firmware.hex --dry-run
```

| 参数 | 默认 | 说明 |
|---|---|---|
| `firmware` | 必填 | `.bin` 或 Keil 生成的 `.hex` |
| `-p/--port` | `COM4` | 串口号 |
| `-b/--baud` | `115200` | 波特率 |
| `-d/--delay` | `0.05` | 帧间延时（秒） |
| `-l/--listen` | `8.0` | 发完后监听回传的秒数 |
| `--max-size` | `14336` | OTA 缓冲区可用大小（超出会告警） |
| `--dry-run` | — | 只打包打印，不发送 |

发送流程：等固件镜像地址 → 分帧 → 逐帧发送（含结束帧）→ 监听板子回传。

## 七、串口回传字符串

| 字符串 | 来源 | 含义 |
|---|---|---|
| `waiting!` | APP `wifi_send_task` | APP 正常运行的每秒心跳 |
| `start upgrade!` | APP `wifi_send_task` | 收齐固件、准备写 config |
| `bootloader begin!` | Bootloader | 进入 Bootloader |
| `upgrading!` | Bootloader | 正在拷贝固件 |
| `upgrade success!` | Bootloader | 升级成功 |
| `upgrade fail!` | Bootloader | 升级失败（CRC/擦写错误） |

## 八、关键文件

| 文件 | 作用 |
|---|---|
| `USER/main.c` | Bootloader 与 APP 两个 `main()`，分区宏 |
| `USER/ota_receive.c` | 帧解析状态机、Flash 写入、升级触发 |
| `USER/ota_receive.h` | 分区地址、帧格式常量、数据结构 |
| `USER/uart.c` | USART1 + DMA 循环接收 + IDLE 中断 → StreamBuffer |
| `USER/uart.h` | 缓冲区大小、帧常量 |
| `HardWare/DMA/dma.c` | DMA1_Channel5 循环模式配置 |
| `ota_send.py` | PC 端固件发送工具 |
| `1.md` | 帧格式与状态机设计草稿 |

## 九、常见问题

| 现象 | 原因 / 处理 |
|---|---|
| 打开 COM4 报 `PermissionError(13)` | 串口被占用，关掉串口助手 / Keil 调试 / 其它脚本 |
| 只有 `waiting!`，无 `start upgrade!` | 帧 CRC 不匹配（多半是 PC 端 CRC 未按「字序」计算），或接收端固件是旧协议版本 |
| 固件 > 14336 字节 | 超出 OTA 暂存区，需精简固件或重新分区 |
| 监听窗口完全无回传 | TX(PA9) 未接回 PC，或板子未运行接收端 |
| 升级成功后仍跑旧程序 | 收齐校验没过（`received_count != total_packets`），未写 config |

### 缓冲区大小建议

DMA 环形缓冲区（`CercleBufferSize`）必须远大于单帧 268 字节。页擦除会阻塞约 20~40ms，此期间 115200 波特率下约涌入 460 字节，加上一帧突发，**建议 ≥ 1024**。StreamBuffer 同理建议 ≥ 1024。
