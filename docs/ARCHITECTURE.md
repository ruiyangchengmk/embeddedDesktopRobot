# ESP32-S3 端侧控制系统 — 架构与开发文档

> **文档目标**：本文档同时服务于人类开发者和 AI Agent。阅读后应能完整理解项目结构、当前实现状态、以及如何在此基础上进行扩展开发。

---

## 1. 项目概述

### 1.1 一句话定位
一个以 **ESP32-S3-DevKitC-1** 为核心的端侧控制节点，通过**硬件抽象层 (HAL)** 管理传感器与执行器，通过 **WiFi + MQTT** 与边侧 PC（运行 LLM Agent）协同工作。

### 1.2 系统愿景（端-边-云三层架构）

```
┌─────────────────────────────────────────────────────────────────┐
│                        云侧 (Cloud)                              │
│              大模型 API (OpenAI / Claude / 国产大模型)            │
│                  提供高层决策与推理能力                           │
└──────────────────────────┬──────────────────────────────────────┘
                           │ HTTPS / SSE
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│                        边侧 (Edge Server)                        │
│  ┌─────────────────┐    ┌────────────────────────────────┐      │
│  │  MQTT Broker    │◄──►│  Python Gateway (LLM Agent)    │      │
│  │  (Mosquitto)    │    │  - 订阅传感器 Topic             │      │
│  └─────────────────┘    │  - 封装 Prompt 调用 LLM         │      │
│           ▲             │  - 解析 JSON 指令并下发          │      │
│           │ MQTT        └────────────────────────────────┘      │
│           │ TCP/IP                                               │
└───────────┼─────────────────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────────────────────────────┐
│                        端侧 (Device)                             │
│                  ESP32-S3-DevKitC-1                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │   传感器层    │  │   执行器层    │  │      通信层           │  │
│  │  EC11 / 其他 │  │ SG90 / RGB   │  │  WiFi + MQTT(Client) │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
│           │              ▲                      │               │
│           └──────────────┼──────────────────────┘               │
│                    FreeRTOS Queues / Events                      │
└─────────────────────────────────────────────────────────────────┘
```

### 1.3 当前进度
- ✅ **Phase 0 完成**：硬件抽象层 (HAL) 已构建并编译验证通过。
- ✅ **Phase 1 完成**：WiFi + MQTT 通信链路已打通并编译验证通过。
- ⏳ **Phase 2 待开发**：边侧 Python Gateway + LLM API 集成。

---

## 2. 文件结构

```
/home/byd/Desktop/espattack/          # 项目根目录
├── CMakeLists.txt                    # ESP-IDF 顶层 CMake
├── sdkconfig                         # ESP-IDF 芯片配置
├── flash.sh                         # 编译烧录脚本（含 gen_config.py 调用）
├── gen_config.py                    # 配置生成器：control/*.json → main/app_config.h
├── control/                         # 用户可编辑配置（JSON）
│   ├── servo.json     # 舵机角度映射
│   ├── rgb.json       # 颜色关键帧
│   └── encoder.json   # 编码器步进/初始角度
├── components/
│   └── esp-mqtt/                     # 外部组件：乐鑫官方 MQTT 客户端库（当前未编译）
├── docs/
│   ├── AGENTS.md                   # AI Agent 快速介入文档
│   ├── PROJECT_LOG.md                # 过程日志（按时间线记录）
│   ├── ARCHITECTURE.md               # 本文件：架构与接口规范
│   └── TUTORIAL.md                   # 初学者教学文档
└── main/                             # 主组件（ESP-IDF 应用入口）
    ├── CMakeLists.txt                # 自动收集 hal/*.c 和 main.c
    ├── app_config.h                 # 自动生成（gen_config.py 产出，勿手动修改）
    ├── main.c                        # 顶层应用逻辑（初始化 + 主循环 + ec11_task）
    ├── hal/                          # 硬件抽象层 (Hardware Abstraction Layer)
    │   ├── hal_common.h              # 通用类型定义（颜色、错误码）
    │   ├── hal_servo.h / .c          # SG90 舵机驱动
    │   ├── hal_rgb.h   / .c          # WS2812 RGB LED 驱动
    │   ├── hal_ec11.h  / .c          # EC11 旋转编码器驱动
    │   └── hal_gc9a01.h / .c        # GC9A01 圆形 LCD 驱动
    └── net/                          # 网络通信层（代码完整，当前未编译）
        ├── net_wifi.h / .c           # WiFi Station 模式
        └── net_mqtt.h / .c           # MQTT 客户端
```

> **设计原则**：
> - `hal/` 内的代码**不依赖任何应用层或网络层逻辑**，只封装底层寄存器/外设操作。
> - `net/` 内的代码**不依赖任何 HAL 细节**，只负责网络连接与协议数据收发。
> - `main.c` 作为**胶水层**，将 `hal` 和 `net` 连接起来。
> - **配置系统**：所有应用参数通过 `control/*.json` 配置，`gen_config.py` 生成 `app_config.h`，代码无需改动即可更改行为。

---

## 3. 硬件清单与接线图

### 3.1 主控板
- **型号**：ESP32-S3-DevKitC-1
- **芯片**：ESP32-S3 (QFN56) rev v0.2
- **特征**：240MHz 双核、Wi-Fi + BLE 5.0、8MB PSRAM、USB-Serial/JTAG 接口
- **当前串口**：`/dev/ttyACM0`
- **开发框架**：ESP-IDF v6.0

### 3.2 已接入外设

| 外设 | 型号/规格 | 功能 |
|------|----------|------|
| RGB LED | WS2812 (板载) | 状态可视化（颜色反馈） |
| 舵机 | SG90 | 角度执行器（0°~180°） |
| 编码器 | 野火小智 EC11 | 人机交互输入（旋转+按键） |
| 显示屏 | GC9A01 1.28" 圆形 SPI LCD | 本地状态显示（旋转图片） |

### 3.3 接线表

| 外设 | 引脚/线色 | ESP32-S3 GPIO | 电源要求 | 备注 |
|------|----------|---------------|---------|------|
| **WS2812** | DIN | **GPIO48** | 板载 | 无需外部接线 |
| **SG90 信号** | 橙/黄 | **GPIO4** | 5V 供电 | USB 供电可直接驱动空载 SG90 |
| **SG90 电源** | 红 | **5V** | - | - |
| **SG90 地线** | 棕/黑 | **GND** | - | - |
| **EC11 CLK** | A相 | **GPIO5** | 3.3V 供电 | 已开启内部上拉 |
| **EC11 DT** | B相 | **GPIO6** | - | - |
| **EC11 SW** | 按键 | **GPIO7** | - | 按下为低电平 |
| **EC11 VCC** | 红 | **3.3V** | - | - |
| **EC11 GND** | 黑 | **GND** | - | - |

---

## 4. 硬件抽象层 (HAL) 详细规范

### 4.1 通用类型：`hal_common.h`

```c
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} hal_rgb_color_t;

typedef enum {
    HAL_OK = 0,
    HAL_ERR = -1,
    HAL_ERR_INVALID_ARG = -2,
    HAL_ERR_NO_MEM = -3,
    HAL_ERR_NOT_INIT = -4,
} hal_err_t;
```

### 4.2 舵机模块：`hal_servo`

**文件**：`main/hal/hal_servo.h`, `main/hal/hal_servo.c`
**底层驱动**：ESP-IDF `LEDC` (PWM)
**默认 GPIO**：`GPIO4`

**接口**：
```c
hal_err_t hal_servo_init(void);               // 初始化 LEDC，默认角度 90°
hal_err_t hal_servo_deinit(void);             // 释放资源
hal_err_t hal_servo_set_angle(int angle);     // angle: 0 ~ 180
int       hal_servo_get_angle(void);          // 获取上一次设置的角度
```

**实现细节**：
- PWM 频率：50Hz（周期 20ms）
- 分辨率：14-bit（ESP32-S3 LEDC 硬件限制，最大 16383）
- 脉宽映射：
  - 0°  → 0.5ms  → duty ≈ 410
  - 90° → 1.5ms  → duty ≈ 1228
  - 180°→ 2.5ms  → duty ≈ 2048

---

### 4.3 RGB LED 模块：`hal_rgb`

**文件**：`main/hal/hal_rgb.h`, `main/hal/hal_rgb.c`
**底层驱动**：ESP-IDF `RMT` (Remote Control Transceiver)
**默认 GPIO**：`GPIO48`（板载 WS2812）

**接口**：
```c
hal_err_t hal_rgb_init(void);
hal_err_t hal_rgb_deinit(void);
hal_err_t hal_rgb_set_color(uint8_t r, uint8_t g, uint8_t b);
hal_err_t hal_rgb_set_color_by_struct(hal_rgb_color_t color);
```

**实现细节**：
- RMT 时钟：10MHz（0.1μs/tick）
- WS2812 时序：
  - `0` bit：0.3μs 高 + 0.9μs 低  → 3 ticks + 9 ticks
  - `1` bit：0.9μs 高 + 0.3μs 低  → 9 ticks + 3 ticks
  - Reset：60μs 低电平 → 600 ticks
- 颜色顺序：GRB（驱动内部自动完成转换）

> **ESP-IDF v6.0 注意事项**：`rmt_transmit()` 的 encoder 参数不可传 `NULL`，必须使用 `rmt_copy_encoder`。

---

### 4.4 编码器模块：`hal_ec11`

**文件**：`main/hal/hal_ec11.h`, `main/hal/hal_ec11.c`
**底层驱动**：ESP-IDF `GPIO` + 中断 (`gpio_isr_service`)
**默认 GPIO**：`CLK=GPIO5`, `DT=GPIO6`, `SW=GPIO7`

> **重要**：ISR 内**禁止调用阻塞 API**（如 `hal_rgb_set_color` 内部会等待 RMT 信号量，在 ISR 调用会导致 `Interrupt wdt timeout`）。所有实际控制操作通过 FreeRTOS 队列从 ISR 传递到任务上下文。

**接口**：
```c
typedef enum {
    EC11_EVENT_ROTATE_CW,   // 顺时针
    EC11_EVENT_ROTATE_CCW,  // 逆时针
    EC11_EVENT_BTN_PRESSED, // 按键按下
} hal_ec11_event_t;

hal_err_t hal_ec11_init(void);
hal_err_t hal_ec11_deinit(void);
QueueHandle_t hal_ec11_get_queue(void);  // 获取事件队列，给 xQueueReceive 用
int hal_ec11_get_angle(void);
void hal_ec11_set_angle(int angle);       // 强制设置逻辑角度，按键重置用
void hal_ec11_set_step(int step);         // 每旋转一格的步进值，默认 2°
```

**实现细节**：
- 中断触发方式：`CLK` 下降沿 (`GPIO_INTR_NEGEDGE`)
- 方向判断：触发时刻读取 `DT` 电平
  - `DT == 1` → 顺时针 (CW)
  - `DT == 0` → 逆时针 (CCW)
- 角度范围：硬限制在 `0° ~ 180°`，两端限位阻断（需反向旋转解除）
- ISR 只做消抖 + 角度更新 + 队列发送，不做其他操作
- 5ms 软件消抖（`EC11_DEBOUNCE_US = 5000`）

---

## 5. 网络通信层 (NET) 详细规范

### 5.1 WiFi 模块：`net_wifi`

**文件**：`main/net/net_wifi.h`, `main/net/net_wifi.c`  
**底层驱动**：ESP-IDF `esp_wifi` + `esp_netif` + `nvs_flash`  
**模式**：Station (STA) 模式

**接口**：
```c
hal_err_t net_wifi_init(void);                        // 初始化并启动 WiFi STA
hal_err_t net_wifi_wait_connected(uint32_t timeout_ms); // 阻塞等待连接成功
```

**实现细节**：
- 使用 FreeRTOS `EventGroup` 等待 `IP_EVENT_STA_GOT_IP` 事件。
- 断线自动重连，最多重试 10 次。
- **配置修改**：请在 `main/net/net_wifi.c` 顶部修改 `WIFI_SSID` 和 `WIFI_PASS` 宏。

### 5.2 MQTT 模块：`net_mqtt`

**文件**：`main/net/net_mqtt.h`, `main/net/net_mqtt.c`  
**底层驱动**：乐鑫官方 `esp-mqtt` 组件（已从 GitHub 拉取到 `components/esp-mqtt/`）  
**协议**：MQTT over TCP

**接口**：
```c
typedef void (*net_mqtt_cmd_cb_t)(int angle);

hal_err_t net_mqtt_init(void);
hal_err_t net_mqtt_register_cmd_callback(net_mqtt_cmd_cb_t cb);
hal_err_t net_mqtt_publish_sensor(int angle, uint8_t r, uint8_t g, uint8_t b);
hal_err_t net_mqtt_publish_heartbeat(void);
```

**Topic 规范**：

| Topic | 方向 | 内容 |
|-------|------|------|
| `device/esp32_01/sensors` | 端 → 边 | 传感器数据 JSON |
| `device/esp32_01/commands` | 边 → 端 | 控制指令 JSON |
| `device/esp32_01/heartbeat` | 端 → 边 | 心跳包 |

**配置修改**：请在 `main/net/net_mqtt.c` 顶部修改 `MQTT_BROKER_URL` 宏为你的 Broker IP。

**JSON 格式示例**：

- **传感器上报**：
  ```json
  {"device_id":"esp32_01","angle":90,"r":0,"g":0,"b":255}
  ```
- **心跳包**：
  ```json
  {"device_id":"esp32_01","status":"online"}
  ```
- **控制指令（支持两种格式）**：
  ```json
  {"angle":120}
  ```
  或
  ```json
  {"commands":[{"type":"servo","angle":120}]}
  ```

**JSON 解析策略**：
- 为避免引入庞大的 JSON 库（当前 ESP-IDF v6.0 未默认集成 cJSON），`net_mqtt.c` 内部使用了一个**轻量级字符串解析器**：在收到的 payload 中搜索 `"angle"` 关键字，并提取其后的第一个整数值。
- 这种策略对嵌入式设备非常友好，既节省了 Flash/RAM，又足够覆盖当前指令格式。

---

## 6. 应用层：`main.c`

### 6.1 职责
`main.c` 负责：
1. 初始化 `event_broker` 事件总线和各消费者私有队列
2. 启动 `consumer_task`（优先级 18）接收事件总线消息，广播给各消费者
3. 启动 `servo_task`、`rgb_task`、`lvgl_task`、`lcd_task` 四个独立消费者
4. 初始化所有 HAL 层模块

### 6.2 FreeRTOS 任务划分

**架构原则：输入（EC11）广播，所有消费者独立订阅，互不阻塞。**

| 任务名 | 优先级 | 周期 | 职责 |
|--------|--------|------|------|
| `ec11_reader` | **12** | 10ms 轮询 | 只读 EC11 角度，立即广播到 `event_broker` |
| `consumer_task` | **18** | 事件驱动 | 订阅事件总线，将 `broker_event_t` 转换为 `angle_msg_t` 并广播到 3 个私有队列 |
| `servo_task` | 15 | 每 10ms lerp | 消费角度，做舵机平滑插值 |
| `rgb_task` | 14 | 事件驱动 | 消费角度，立即更新 RGB 颜色 |
| `lvgl_task` | 5 | EC11 按键触发 | EC11 按下时图片旋转 90 度 |

**设计理由：**
- EC11 读取是最高优先级，任何时候都不能被延迟
- `consumer_task` 将事件总线消息转换为统一的 `angle_msg_t`，并分别发送到 3 个**私有队列**（`s_servo_queue`、`s_rgb_queue`、`s_lvgl_queue`），彻底避免共享队列的竞态
- 消费者各自独立任务，一个消费者阻塞不影响其他消费者
- GC9A01 SPI 传输可能耗时数十毫秒，但舵机和 RGB 不受影响

**LVGL 9 关键配置：**
- `lv_tick_inc(10)` 必须在 `lvgl_task` 的每个循环周期调用，否则 `lv_timer_handler()` 的内部 timer 不会推进，导致屏幕永远不刷新
- `lvgl_flush_cb` 中必须调用 `lv_draw_sw_rgb565_swap()`，补偿 ESP32-S3 小端存储与 GC9A01 SPI 大端期望之间的字节序差异

### 6.3 数据流（广播-订阅模式）

```
EC11 旋转/按键 → hal_ec11.c: ec11_reader_task（10ms 轮询）
               → event_broker_publish(EVENT_TYPE_ENCODER_ROTATE/CLICK)
               → consumer_task: xQueueReceive(s_consumer_queue)
                        → 计算 servo_target 和 RGB 颜色
                        → 广播到 3 个私有队列：
                             xQueueSend(s_servo_queue,  &msg, 0)
                             xQueueSend(s_rgb_queue,    &msg, 0)
                             xQueueSend(s_lvgl_queue,   &msg, 0)

私有队列分别消费：
  → servo_task（优先级 15）：lerp 逼近，每 10ms 写一次舵机
  → rgb_task（优先级 14）：直接更新颜色，无延迟
  → lvgl_task（优先级 5）：EC11 按下时图片旋转 90 度
```

> **为什么使用私有队列而不是共享队列？**
> `xQueueReceive` 是取走语义。如果 3 个任务共享一个队列，高优先级的 `servo_task` 和 `rgb_task` 会抢走绝大多数消息，`lvgl_task` 几乎拿不到。私有队列确保每个消费者都能收到完整的事件流。
>
> **为什么不需要全局锁？**
> 所有共享数据通过队列传递，不存在多任务同时读写同一变量的情况。

### 6.4 颜色策略（应用层决策）

`color_from_angle()` 在 `main.c` 中实现，使用 `app_config.h` 中的 `s_rgb_keyframes[]` 数组进行线性插值，**不属于 HAL 职责**：

| 角度 | 颜色 | RGB 值 |
|------|------|--------|
| 0° | 绿色 | `(0, 255, 0)` |
| 90° | 蓝色 | `(0, 0, 255)` |
| 180° | 红色 | `(255, 0, 0)` |
| 中间 | 线性插值 | 自动计算 |

- **0° ~ 90°**：绿色 → 蓝色（G↓ B↑）
- **90° ~ 180°**：蓝色 → 红色（R↑ B↓）

---

## 7. 编译与烧录

### 7.1 编译命令

```bash
cd /home/byd/Desktop/espattack
export IDF_PATH=/home/byd/.espressif/v6.0/esp-idf
# 若 idf.py 环境正常
idf.py build

# 若 idf.py 环境异常，可直接使用已生成的 build 目录调用 ninja
# cd build && ninja all
```

### 7.2 烧录与监控

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

> 串口常见为 `/dev/ttyACM0`（ESP32-S3 USB-Serial/JTAG）。
> 若复位后停留在 `waiting for download`，请手动按板载 **RST** 按钮进入正常运行。

### 7.3 外部组件说明

由于当前 ESP-IDF v6.0 未默认集成 `esp-mqtt` 组件，项目已通过 GitHub 拉取了官方库：

```bash
cd /home/byd/Desktop/espattack/components
git clone --depth 1 https://github.com/espressif/esp-mqtt.git
```

CMake 会自动识别 `components/` 目录下的子目录并将其纳入构建。

---

## 8. HAL 硬件隔离原则（强制规范）

### 8.1 核心原则：单一硬件变更不得影响其他硬件

**每调整一个硬件，必须只影响那一个硬件的驱动层代码。调整过程中，其他所有硬件的正常工作不受干扰。**

这是嵌入式项目的最重要的工程质量要求。历史上多次调试经验（包括 GC9A01 黑屏数小时、LVGL timer 不工作排查等）证明，违反此原则会导致问题传播——修 A 硬件的代码时 B 硬件的行为悄然改变，导致调试成本急剧增加。

### 8.2 架构保证

```
┌──────────────────────────────────────────────────────┐
│                      main.c                          │
│               （胶水层，应用逻辑）                      │
│                                                       │
│   ec11_task  ──→  hal_servo_set_angle()             │
│       │           hal_rgb_set_color()                 │
│       │           lv_gc9a01_image_rotate()           │
└──────────────────────────────────────────────────────┘
                          │
        ┌─────────────────┼─────────────────┐
        ▼                 ▼                 ▼
   hal_servo.c       hal_rgb.c       hal_ec11.c
   (仅操作 LEDC       (仅操作 RMT       (仅操作 GPIO
    和 GPIO4)         和 GPIO48)       中断和队列)
        │                 │                 │
        ▼                 ▼                 ▼
   不持有任何       不持有任何         不持有任何
   其他外设状态     其他外设状态       其他外设状态
```

**隔离手段：**

| 原则 | 说明 |
|------|------|
| **无共享状态** | `hal_*.c` 之间不共享全局变量。每个模块的状态封装在自己的 `static` 变量里 |
| **无跨层调用** | `hal_servo.c` 不调用 `hal_rgb.c` 的函数，也不引用其头文件 |
| **独立初始化** | 每个 HAL 模块独立初始化，互不依赖。初始化顺序由 `app_main()` 控制 |
| **接口即边界** | `hal_xxx.h` 头文件里声明的接口是模块对外的唯一通道；内部实现细节绝不外泄 |
| **独立 ISR** | GPIO 中断处理函数各自独立，不在 ISR 里调用其他 HAL 模块的函数 |

### 8.3 实际约束（代码层面）

**✅ 正确做法：**
```c
// 在 hal_gc9a01.c 中修复 GC9A01 init，不动其他任何 .c 文件
hal_err_t hal_gc9a01_init(void)
{
    // 只操作 SPI/GPIO1/GPIO2/GPIO10
    // 不调用 hal_servo / hal_rgb / hal_ec11 的任何函数
}
```

**❌ 错误做法：**
```c
// 在 hal_gc9a01.c 的 init 里偷偷改了 SPI 总线配置
// → 导致 hal_ec11 的 GPIO 中断响应异常（同一 SPI 总线共享时钟！）
spi_bus_config_t buscfg = { ... };  // 改了全局 SPI 总线
```

### 8.4 调试时的隔离验证

每次修改一个 HAL 模块后，按以下顺序验证其他硬件**行为未变**：

| 被影响硬件 | 验证方法 |
|-----------|----------|
| 舵机 SG90 | EC11 旋转，舵机角度是否平滑变化 |
| RGB LED | EC11 旋转，颜色是否连续变化 |
| EC11 编码器 | EC11 按键是否触发 GC9A01 图片旋转 |
| GC9A01 LCD | SPI 测试 RED→GREEN→BLUE 三色是否全彩正确 |

---

## 9. 如何扩展新硬件（AI / 开发者指南）

### 8.1 添加新传感器的标准步骤

以添加 "超声波测距模块 HC-SR04" 为例：

**Step 1**：在 `main/hal/` 下新建 `hal_hcsr04.h` 和 `hal_hcsr04.c`

```c
// hal_hcsr04.h
#ifndef HAL_HCSR04_H
#define HAL_HCSR04_H
#include "hal_common.h"

hal_err_t hal_hcsr04_init(void);
hal_err_t hal_hcsr04_deinit(void);
float     hal_hcsr04_get_distance_cm(void);

#endif
```

**Step 2**：在 `hal_hcsr04.c` 中实现驱动，仅依赖 ESP-IDF 底层 API（如 `gpio`, `gptimer`），不引用任何应用层变量。

**Step 3**：在 `main/CMakeLists.txt` 中**无需修改**（已使用 `GLOB_RECURSE` 自动收集）。

**Step 4**：在 `main.c` 的 `app_main()` 中：
```c
hal_hcsr04_init();

while (1) {
    float dist = hal_hcsr04_get_distance_cm();
    // 将 dist 通过 MQTT 上报或用于本地避障逻辑
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

### 8.2 添加新执行器的标准步骤

以添加 "直流减速电机 + L298N 驱动" 为例：

**Step 1**：新建 `hal_motor.h/.c`，提供：
```c
hal_err_t hal_motor_init(void);
hal_err_t hal_motor_set_speed(int left, int right);  // -100 ~ 100
```

**Step 2**：在 `main.c` 中接收 MQTT 指令后调用 `hal_motor_set_speed(...)`。

### 8.3 添加新的网络协议

如果未来需要把 MQTT 换成其他协议（如 WebSocket、HTTP）：
- **不要改 `hal/` 目录**。
- 在 `main/net/` 下新建 `net_ws.h/.c` 或 `net_http.h/.c`。
- 修改 `main.c` 中网络初始化代码，从 `net_mqtt_init()` 切换到新的网络模块。
- HAL 层对此完全无感知。

---

## 9. 版本与变更记录

| 日期 | 版本 | 变更内容 |
|------|------|----------|
| 2026-04-13 | v0.1 | 完成 HAL 层重构（SG90 + WS2812 + EC11），编译验证通过。 |
| 2026-04-13 | v0.2 | 完成 Phase 1：接入 WiFi + MQTT，增加 `net_wifi` 和 `net_mqtt` 模块，编译验证通过。 |
| 2026-04-15 | v0.3 | EC11 ISR 重构为队列模式（修复 `Interrupt wdt timeout`）；颜色映射 90° 修复；移除 RGB 渐变；EC11 步进 5°→2°；flash.sh toolchain PATH 修复。 |
| 2026-04-16 | v0.4 | JSON 配置层（control/ + gen_config.py）；舵机角度映射（EC11→Servo 可配置）。 |
| 2026-04-16 | v0.5 | GC9A01 驱动完全重写（Bodmer init sequence 逐条修正）；LVGL 9 方块旋转（`lv_refr_now()` workaround）；舵机增加角度平滑插值消除抖动；增加 HAL 硬件隔离原则文档。 |
| 2026-04-16 | v0.6 | 广播-订阅架构重构：EC11 广播角度队列 → 4 个独立消费者任务（ec11_reader/servo/rgb/lvgl），彻底消除 SPI 阻塞导致的延迟和输入丢包问题。 |
| 2026-04-17 | v0.7 | **事件总线 + 私有队列**：引入 `event_broker.c` 做本地事件路由；修复 EC11 GPIO 定义错误（5/6/7）；修复 `vTaskDelay(0)` 导致的 CPU 饿死；`hal_gc9a01_init()` 每次调用都执行硬复位+清屏；LVGL 方块设置旋转中心 pivot；**修复 `lv_tick_inc` 缺失导致方块不旋转**；**修复 RGB565 小端字节序导致颜色偏差**；GC9A01 颜色驱动（RGB565→BGR565 像素交换，MADCTL/INVERSION 宏可调）。 |

---

*文档维护者：AI Agent + 人类开发者协同*  
*最后一次更新：2026-04-17（v0.7：事件总线 + 私有队列 + GC9A01 状态重置）*
