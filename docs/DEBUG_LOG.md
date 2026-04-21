# 开发调试记录 — 根因分析手册

本文档记录项目所有历史问题的根本原因（Root Cause Analysis），供日后调试参考。

---

## 1. GC9A01 黑屏问题（7 个根本原因）

**问题描述**：GC9A01 初始化完成后屏幕始终黑屏，无任何显示。

### 1.1 Bodmer 初始化序列格式错误

**现象**：发送初始化命令后屏幕无反应。

**根因**：错误地合并了 command + data 到同一条 SPI transaction。

```c
// 错误做法（两条数据混在一起，控制器无法识别）
spi_transaction_t t = { .tx_buffer = (uint8_t[]){0xEF, 0xEB, 0x14}, .length = 24 };
spi_device_polling_transmit(s_spi, &t);
```

**正确做法**：`writecommand()` 只发命令字节，`writedata()` 只发数据字节，必须分开两条 transaction。

```c
cmd(0xEF);              // 单命令 transaction
cmd_data(0xEB, data, 1); // 命令后跟数据 transaction
```

### 1.2 缺少 MADCTL 寄存器设置

**现象**：屏幕有背光但全黑。

**根因**：未设置 MADCTL（Memory Data Access Control）寄存器，导致像素方向全部错误。

**修复**：初始化序列中添加 `cmd_data(0x36, (uint8_t[]){0x08}, 1);`（MX=1, MY=1, BGR=1，竖屏模式）。

### 1.3 RGB565 字节序与面板 BGR 模式不匹配

**现象**：显示颜色整体偏蓝/紫，与预期不符。

**根因**：RGB565 实际传输顺序是 **BGR**，面板在 MADCTL=0x08 时工作于 BGR 模式。

| 预期颜色 | 错误字节序（R=255,G=0,B=0） | 正确字节序 |
|----------|---------------------------|-----------|
| 红色 | `0x00F8`（B=0,G=0,R=255） | `0xF800`（R=255,B=0） |

**修复**：发送像素数据前对 RGB565 做字节交换，或在 fillRect 后做 `color >>= 8 \| color << 8`。

### 1.4 缺少 Display ON 序列

**现象**：初始化完成，但屏幕仍黑。

**根因**：未发送 `0x29`（Display ON）命令。

**修复**：在初始化末尾添加：
```c
cmd(0x11);  // Sleep out
vTaskDelay(pdMS_TO_TICKS(120));
cmd(0x29);  // Display ON
```

### 1.5 未移除 0x21（反显）命令

**现象**：颜色显示异常，对比度极低。

**根因**：`0x21`（Display Inversion ON）与面板不兼容。

**修复**：从初始化序列中删除所有 `cmd(0x21);`。

### 1.6 SPI 时序问题（DC 线未正确切换）

**现象**：部分命令生效，部分命令不生效。

**根因**：DC（Data/Command）引脚切换时序不正确，导致命令被当作数据或反之。

**修复**：
```c
#define DC_LOW()  gpio_set_level(GC9A01_DCX_GPIO, 0)  // 命令模式
#define DC_HIGH() gpio_set_level(GC9A01_DCX_GPIO, 1)  // 数据模式
// 在 cmd() 前确保 DC=LOW，cmd_data() 数据阶段确保 DC=HIGH
```

### 1.7 SPI 模式设置错误

**现象**：初始化命令发送后无反应。

**根因**：SPI `mode` 错误（mode=3 当面板需要 mode=0）。

**修复**：devcfg 设置 `.mode = 0`（CPOL=0, CPHA=0）。

---

## 2. LVGL Timer 不触发问题

**问题描述**：`lv_timer_create` 创建的回调函数从未被执行，方块不旋转。

**现象**：LVGL 初始化成功，但方块停在初始位置不动。

### 根因分析

`lv_timer_handler` 运行依赖 esp_timer 定时器回调，但该回调在 FreeRTOS tickless 模式下可能不触发：

1. ESP-IDF 默认启用 tickless 空闲模式，CPU 空闲时进入 deep sleep
2. esp_timer 回调依赖硬件定时器中断
3. 当优先级高于当前任务的中断触发时，tickless 模式可能被异常唤醒但 timer wheel 不推进
4. lv_timer_handler 检查 timer 队列时发现没有到期 timer，直接返回不做任何处理

**验证**：在 `lv_timer_handler` 前加日志，观察 lvgl_tick 间隔异常大（约 2 秒一次，正常应为 10ms 级）。

### 解决方案

弃用 `lv_timer_create`，改用 FreeRTOS 任务 + 轮询 + `lv_refr_now()`：

```c
// 不使用 lv_timer_create，直接在 FreeRTOS 任务中轮询
static void lvgl_update_task(void *arg) {
    while (1) {
        if (xQueueReceive(s_angle_queue, &msg, 0) == pdTRUE) {
            lv_obj_set_style_transform_rotation(s_gc_square, angle * 10, 0);
            lv_refr_now(disp);  // 强制立即刷新
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

---

## 3. SPI 阻塞导致的 2-3 秒延迟与输入丢包

**问题描述**：EC11 旋转时，舵机和 RGB 响应延迟 2-3 秒，且快速旋转时输入丢包。

### 根因分析

`hal_gc9a01_draw_bitmap` 使用 `spi_device_polling_transmit` 逐行传输像素：

```c
for (int y = 0; y < h; y++) {
    spi_transaction_t t = { .tx_buffer = row_buf, .length = row_bytes * 8 };
    spi_device_polling_transmit(s_spi, &t);  // CPU 阻塞等待 SPI 完成
}
```

这是一段 **CPU 密集循环**：
- 传输 240×240 像素需要约 57ms（40MHz SPI）
- 这 57ms 内 scheduler 无法切换任务（polling 不断轮询 DMA 状态寄存器）
- 即便 ec11_task 优先级更高，也抢不到 CPU
- 中断（EC11 旋转）堆积在队列中，等 SPI 传输完后才处理 → 延迟
- 中断过多时队列溢出 → 丢包

### 关键发现

在 lvgl_task 中打印日志观察到 `lvgl_tick` 在 SPI 传输期间完全停止：
```
I (3715) APP: [lvgl] tick=10000
I (3915) APP: [lvgl] tick=20000  ← 应为 10ms，实际过了 10 秒
```

### 解决方案

1. **广播-订阅架构**：EC11 读取与 SPI 显示完全分离
2. **CPU 让步**：每传输 20 行调用 `vTaskDelay(1)`，主动让出 CPU
3. **优先级分离**：ec11_reader 最高优先级（20），舵机/消费任务次高（15/14），GC9A01 独立低优先级（5）

---

## 4. EC11 角度与舵机 1:1 映射问题

**问题描述**：舵机实际角度范围与 EC11 限位（0°~180°）完全相同，无法满足"EC11 0°~180° 映射到舵机 30°~150°"的需求。

**解决方案**：通过 `control/servo.json` 配置任意范围映射：

```json
{
  "ec11_to_servo": {
    "ec11_min": 0, "ec11_max": 180,
    "servo_min": 30, "servo_max": 150
  }
}
```

生成宏 `CFG_EC11_TO_SERVO(x)` 使用线性映射公式：
```
servo_angle = servo_min + (ec11_angle - ec11_min) * (servo_max - servo_min) / (ec11_max - ec11_min)
```

---

## 5. `ec11_evt_t` 类型未定义错误

**问题描述**：编译报错 `unknown type name 'ec11_evt_t'`。

**根因**：`ec11_evt_t` 在 `hal_ec11.c` 内部定义为匿名 struct，未暴露在头文件中。

**错误修复尝试**：
1. 在 `hal_ec11.h` 添加 typedef → 导致 hal_ec11.c 中 "conflicting types" 错误
2. hal_ec11.c 的 struct 定义与新增 typedef 冲突

**正确解决方案**：在 main.c 中定义与 hal_ec11.c 内部布局一致的 local struct：

```c
typedef struct {
    hal_ec11_event_t event;
    int angle;
} ec11_evt_t;
```

---

## 6. RGB 颜色与舵机角度联动逻辑问题

**问题描述**：颜色变化不够平滑，存在跳变。

**根因**：颜色插值逻辑中 `t` 计算错误导致非线性过渡。

**修复**：使用浮点插值：
```c
float t = (float)delta / range;
*r = (uint8_t)(kf[i].r + t * (kf[i + 1].r - kf[i].r));
```

---

## 7. CMake 串口占用问题

**问题描述**：执行 `.flash.sh` 时报错 `device busy`。

**根因**：之前运行的 `idf.py monitor` 或其他串口工具持有 `/dev/ttyACM0`。

**解决方案**：烧录前杀死所有占用串口的进程：
```bash
pkill -f idf.py
pkill -f monitor
```

---

## 8. WS2812 RMT 驱动 `NULL` encoder 崩溃

**问题描述**：调用 `rmt_transmit()` 时程序崩溃。

**根因**：ESP-IDF v6.0 中 `rmt_transmit()` 的 `encoder` 参数不可传 `NULL`。

**解决方案**：使用 `rmt_copy_encoder`：
```c
rmt_new_simple_encoder(&encoder_config, &encoder);
rmt_transmit(encoder, &tx_channel, colors, size, &transmit_config);
```

---

## 调试工具清单

| 工具 | 用途 |
|------|------|
| `idf.py monitor` | 串口日志输出 |
| `esptool.py` | 烧录固件 |
| `xtensa-esp32s3-elf-gdb` | 调试崩溃日志 |
| `spi_master_polling_test` | SPI 裸机测试 |
| `gpio_get_level()` | 直接读取 GPIO 状态 |

---

## 9. LVGL 9 方块不旋转（`lv_tick_inc` 缺失）

**问题描述**：GC9A01 上能看到方块，但旋转 EC11 时方块永远停在初始位置。

### 根因分析

LVGL 9 的 `lv_timer_handler()` 内部依赖 `lv_tick_get()` 来判断 timer 是否到期。
默认实现中，`lv_tick_get()` 返回的值只能通过 `lv_tick_inc()` 递增：

```c
uint32_t lv_tick_get(void)
{
    if(state_p->tick_get_cb)
        return state_p->tick_get_cb();
    // 否则读取 sys_time，该值仅由 lv_tick_inc() 更新
}
```

项目中没有任何代码调用 `lv_tick_inc()`，导致：
1. `lv_tick_get()` 永远返回初始值（≈0）
2. `lv_timer_handler()` 第一次调用时会执行初始渲染（画出方块）
3. 后续调用时，因为 tick 没有增长，LVGL 认为没有任何 timer 到期，直接返回，不做任何重绘
4. 因此 `lv_obj_set_style_transform_rotation()` 虽然修改了样式，但永远不会被渲染到屏幕上

### 参考验证

参考项目 [UsefulElectronics/esp32s3-gc9a01-lvgl](https://github.com/UsefulElectronics/esp32s3-gc9a01-lvgl) 使用 `esp_timer` 每 2ms 调用 `lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS)`，这是 LVGL 的标准心跳机制。

### 修复方案

在 `lvgl_task` 的 while 循环中，每次调用 `lv_timer_handler()` 前手动推进 tick：

```c
while (1) {
    lv_tick_inc(10);        // 告诉 LVGL 过去了 10ms
    lv_timer_handler();     // 现在 timer 会正常推进
    vTaskDelay(pdMS_TO_TICKS(10));
}
```

---

## 10. GC9A01 颜色偏差（紫色/橙色而非正确颜色）

**问题描述**：纯色测试红/蓝/绿均显示错误颜色；LVGL 图像中蓝色区域显示为红/绿混合色。

### 根因分析

GC9A01 的 SPI 总线接收存在两层理解分歧：

1. **字节序问题**：ESP32-S3 小端存储，RGB565 像素 0xF800（红）在内存中为 `[0x00, 0xF8]`（低字节在前）。GC9A01 的 SPI MSB-first 模式把先收到的字节当作高字节，因此面板收到 `0x00F8`（纯绿），而非预期的 `0xF800`（红）。

2. **颜色位序问题**：GC9A01 内部面板存储格式为 **BGR565**（面板数据手册规定），而非 RGB565。MADCTL=0x00 设为 RGB 模式后，像素通道解释仍然是 BGR——即面板把我们的 R 位当成 B，把 B 位当成 R。

单独使用字节交换（SWAP-only）：红→蓝、蓝→红，但颜色不纯（通道位权重错位）。
单独使用 BGR 位交换（BGR-only）：红色消失，完全错误。
**两者叠加（BGR 位交换 + 字节交换）**：红/蓝/绿完全正确。

### 正确修复

```c
// hal_gc9a01_draw_bitmap() 像素处理循环
for (int i = 0; i < w; i++) {
    uint16_t px = src[y * w + i];
    /* BGR565 位交换：把 RGB 像素的 R 和 B 通道互换 */
    uint16_t r = (px >> 11) & 0x1F;
    uint16_t g = (px >> 5) & 0x3F;
    uint16_t b = px & 0x1F;
    uint16_t swapped = (b << 11) | (g << 5) | r;  // BGR 格式

    /* 字节交换：ESP32 小端 [LO,HI] → SPI 发送 [HI,LO] */
    row_buf[i * 2]     = (uint8_t)((swapped >> 8) & 0xFF);  // 高字节先发
    row_buf[i * 2 + 1] = (uint8_t)(swapped & 0xFF);         // 低字节后发
}
```

同时保持：
- `MADCTL=0x00`（标准 RGB 模式）
- `COLOR_INVERSION=1`（反转显示）
- `lvgl_flush_cb` 中 **不做任何转换**，直接透传给 HAL

### 调试方法

用 `hal_gc9a01_spi_test()` 做纯色基线测试：
- `0xF800`（红）→ 应显示红
- `0x07E0`（绿）→ 应显示绿
- `0x001F`（蓝）→ 应显示蓝

如果顺序是蓝-绿-红，说明只需要字节交换。如果红色消失/变成其他色，说明需要字节交换 + BGR 位交换叠加。

---

## 调试工具清单

| 工具 | 用途 |
|------|------|
| `idf.py monitor` | 串口日志输出 |
| `esptool.py` | 烧录固件 |
| `xtensa-esp32s3-elf-gdb` | 调试崩溃日志 |
| `spi_master_polling_test` | SPI 裸机测试 |
| `gpio_get_level()` | 直接读取 GPIO 状态 |

---

*最后更新：2026-04-21*
