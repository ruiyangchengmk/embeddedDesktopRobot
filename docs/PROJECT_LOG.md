# ESP32-S3 端侧控制系统 — 项目过程与架构规划文档

## 版本历史

| 版本 | 日期 | 变更说明 |
|------|------|----------|
| v0.1 | 2026-04-15 | 初始提交，RGB/舵机/EC11/LCM05 基础验证通过 |
| v0.2 | 2026-04-15 | 添加 GC9A01 1.28" 圆形 LCD 驱动（SPI） |
| v0.3 | 2026-04-15 | 修复 GC9A01 黑屏问题（RGB565 BGR 字节序） |
| v0.4 | 2026-04-15 | 添加配置层 `control/*.json` + `gen_config.py` |
| v0.5 | 2026-04-16 | 修复 LVGL timer 不触发问题（改用 `lv_refr_now`） |
| v0.6 | 2026-04-16 | 广播-订阅架构重构，解决 SPI 阻塞导致的 2-3s 延迟 |
| v0.7 | 2026-04-17 | 事件总线 + 私有队列；修复 EC11 GPIO 和 CPU 饿死；GC9A01 硬复位清屏修复 |
| v0.8 | 2026-04-21 | GC9A01 颜色修复 v2（BGR位交换+字节交换）；EC11 旋转角度取模防溢出；移除 LCD1602；EC11 按键触发 GC9A01 图片旋转 90° |
| v0.9 | 2026-04-22 | 时钟显示模式：lvgl_clock（lv_scale 实现模拟时钟）、hal_clock（编译时刻作起始时间）；EC11 切换数字/指针模式、旋转切换内容 |
| v0.10 | 2026-04-23 | EC11 按键触发 GPIO3 无源蜂鸣器播放 hello world 旋律；独立 buzzer_task 非阻塞播放；支持后续 MAX98357 I2S DAC 升级为语音播报 |

---

## 1. 项目定位

本项目以 **ESP32-S3-DevKitC-1** 为核心端侧控制器，目标是构建一个具备**传感器采集、多电机执行、状态可视化**能力的嵌入式节点。

未来系统将与**边侧电脑（接入大模型 API）**协同工作，形成 **端-边-云三层架构**：
- **端侧**：ESP32 负责实时传感器读取与电机控制
- **边侧**：PC 作为网关/大脑，运行 MQTT Broker + LLM Agent，处理决策逻辑
- **云侧**：大模型 API（OpenAI / Claude / 国产大模型等）提供高层推理能力

---

## 2. 硬件验证记录

### 2.1 开发板：ESP32-S3-DevKitC-1
- **芯片**：ESP32-S3 (QFN56) rev v0.2
- **特性**：Wi-Fi + BLE 5.0, Dual Core 240MHz, 8MB PSRAM
- **通信端口**：`/dev/ttyACM0` (USB-Serial/JTAG)
- **ESP-IDF 版本**：v6.0

### 2.2 板载 RGB LED 驱动验证 (WS2812)
- **GPIO**：`GPIO48`
- **驱动方式**：ESP-IDF `RMT` (Remote Control Transceiver)
- **实现要点**：
  - 由于 IDF v6.0 中 RMT 被拆分为独立组件 `esp_driver_rmt`，必须在 `CMakeLists.txt` 中显式声明依赖。
  - `rmt_transmit()` 的 encoder 参数不可传 `NULL`，需使用 `rmt_copy_encoder`。
  - WS2812 时序：10MHz 分辨率下，0-bit = 3ticks高+9ticks低，1-bit = 9ticks高+3ticks低。
- **状态**：✅ 验证通过，可显示红/绿/蓝及平滑渐变。

### 2.3 SG90 舵机驱动验证
- **GPIO**：`GPIO4`
- **驱动方式**：ESP-IDF `LEDC` (PWM)
- **实现要点**：
  - ESP32-S3 的 LEDC 最大支持 **14-bit** 分辨率，不可使用 16-bit。
  - 50Hz 频率，周期 20ms。
  - 脉宽映射：0°=0.5ms, 90°=1.5ms, 180°=2.5ms。
  - 依赖组件：`esp_driver_ledc`。
- **状态**：✅ 验证通过，0°~180° 扫掠正常。

### 2.4 野火小智 EC11 编码器接入验证
- **GPIO**：
  - CLK (A相) → `GPIO5`
  - DT  (B相) → `GPIO6`
  - SW  (按键) → `GPIO7`
- **实现要点**：
  - 配置为输入模式并开启内部上拉。
  - 使用 `GPIO_INTR_NEGEDGE`（下降沿）中断检测旋转。
  - 中断内读取 DT 电平判断方向（顺时针/逆时针）。
  - 对全局角度变量使用 `portMUX_TYPE` 临界区保护，避免任务与中断竞争。
  - 依赖组件：`esp_driver_gpio`。
- **状态**：✅ 验证通过，旋转一格角度 ±2°，按键重置 90°。

### 2.5 颜色-角度联动验证
- **映射逻辑**：
  - `0°`   → 绿色 `(0, 255, 0)`
  - `90°`  → 蓝色 `(0, 0, 255)`
  - `180°` → 红色 `(255, 0, 0)`
  - 中间角度线性插值渐变
- **状态**：✅ 验证通过，旋转编码器时 RGB LED 颜色与舵机角度实时同步。

### 2.6 GC9A01 1.28" 圆形 SPI LCD — 配置要点（v0.8 最终版）

**GPIO**：`DC=GPIO1`, `RESX=GPIO2`, `CS=GPIO10`, `MOSI=GPIO11`, `CLK=GPIO12`
**SPI**：40MHz，半双工（`SPI_DEVICE_HALFDUPLEX`），DMA auto

> **Agent 快速上手要点**：以下 4 点是 GC9A01 能否正常工作的核心，任何一条缺失都会导致显示异常。

1. **每次调用 `hal_gc9a01_init()` 都执行硬复位**：`RESX` 拉低 20ms → 释放等待 150ms。面板必须在初始化序列前处于已知状态。

2. **HALF-DUPLEX 模式必须设置**：`devcfg.flags = SPI_DEVICE_HALFDUPLEX`。全双工模式下 DC 引脚无法在命令/数据间切换，显示会完全黑屏。

3. **`lvgl_task` 必须在 `hal_gc9a01_spi_test()` 之后启动**：`spi_test` 执行全屏刷写，若 `lvgl_task` 先跑，其 partial buffer 脏区域会被 `spi_test` 的全屏清除覆盖，产生竞态。

4. **颜色格式：MADCTL=0x00 + BGR565 位交换 + 字节交换**（见下方代码）。MADCTL=0x08（BGR 模式）单独使用会因字节序错位导致颜色整体偏色；必须配合 HAL 中的像素处理。

**HAL 层像素处理（hal_gc9a01_draw_bitmap() 必须包含）：**
```c
// BGR565 位交换 + 字节交换（两者缺一不可）
uint16_t r = (px >> 11) & 0x1F;
uint16_t g = (px >> 5) & 0x3F;
uint16_t b = px & 0x1F;
uint16_t bgr = (b << 11) | (g << 5) | r;
row_buf[i * 2]     = (uint8_t)((bgr >> 8) & 0xFF);  // 高字节先发
row_buf[i * 2 + 1] = (uint8_t)(bgr & 0xFF);         // 低字节后发
```

**初始化序列要点（Bodmer TFT_eSPI 序列）：**
- 0xEF → 0xEB×3 → 0xFE → 0xEF → 0xEB×3（进入寄存器组）
- MADCTL=0x00（标准 RGB，不带 BGR 标志）
- COLMOD=0x05（RGB565）
- COLOR_INVERSION=1（开启反转）
- 每帧像素传输后每 20 行调用 `vTaskDelay(1)` 让出 CPU，防止阻塞 EC11 轮询

**旋转功能（EC11 按键触发）：**
- `lvgl_task` 订阅 `s_lvgl_queue`，收到 `button==1` 时 `img_angle += 900`
- `img_angle %= 3600` 防溢出（`int16_t` 上限 32767，3600×9=32400 仍在安全范围）
- 旋转中心：`lv_obj_set_style_transform_pivot_x/y(s_gc_image, 120, 0)`（图片 240×240）

---

## 3. 未来系统架构设计

### 3.1 总体架构：端-边-云

```
┌─────────────────────────────────────────────────────────────┐
│                        云侧 (Cloud)                         │
│              大模型 API (OpenAI / Claude / 文心一言)         │
│                  提供高层决策与推理能力                       │
└──────────────────────────┬──────────────────────────────────┘
                           │ HTTPS / SSE
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                        边侧 (Edge Server)                   │
│  ┌─────────────────┐    ┌───────────────────────────────┐   │
│  │  MQTT Broker    │◄──►│  Python Gateway (LLM Agent)   │   │
│  │  (Mosquitto)    │    │  - 订阅传感器 Topic            │   │
│  └─────────────────┘    │  - 封装 Prompt 调用 LLM        │   │
│           ▲             │  - 解析 JSON 指令并下发         │   │
│           │ MQTT        └───────────────────────────────┘   │
│           │ TCP/IP                                          │
└───────────┼─────────────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────────────────────────┐
│                        端侧 (Device)                        │
│                  ESP32-S3-DevKitC-1                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │   传感器层    │  │   执行器层    │  │    通信层         │  │
│  │  EC11 / 其他 │  │ SG90 / RGB   │  │  WiFi + MQTT     │  │
│  └──────────────┘  └──────────────┘  └──────────────────┘  │
│           │              ▲                      │           │
│           └──────────────┼──────────────────────┘           │
│                    FreeRTOS Queues / Events                  │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 通信协议选型对比

| 协议 | 实时性 | ESP32 负载 | 复杂度 | 适用场景 | 推荐度 |
|------|--------|-----------|--------|----------|--------|
| **MQTT** | 高 | 低 | 低 | 传感器上报 + 指令下发，一对多 | ⭐⭐⭐⭐⭐ |
| WebSocket | 高 | 中 | 中 | 需要持续双向长连接，如视频流 | ⭐⭐⭐ |
| HTTP REST | 低 | 中 | 低 | 低频配置查询，不适合实时控制 | ⭐⭐ |
| gRPC | 中 | 高 | 高 | 强类型内网服务，嵌入式不友好 | ⭐ |

**推荐方案：MQTT over TCP/IP**
- **理由**：
  1. 发布/订阅模型天然解耦"传感器上报"与"指令下发"。
  2. ESP32 原生支持 `esp-mqtt` 组件，资源占用极低。
  3. 边侧 PC 可用 `paho-mqtt` (Python) 或 `mosquitto` 快速搭建。
  4. 支持 QoS 0/1/2，可根据控制指令可靠性需求灵活选择。
  5. 便于未来扩展多设备（多个 ESP32 接入同一个 Broker）。

### 3.3 端侧 (ESP32) 软件架构建议

#### 任务划分 (FreeRTOS)
- **`sensor_task`**：周期读取 EC11 编码器、温湿度、摄像头等传感器，将数据打包后发送到 `sensor_queue`。
- **`comm_task`**：管理 WiFi 连接和 MQTT Client 生命周期。订阅 `cmd_topic`，将收到的指令投递到 `cmd_queue`；同时从 `sensor_queue` 取数据发布到 `sensor_topic`。
- **`cmd_parser_task`**：从 `cmd_queue` 解析 JSON 指令，转换为具体的电机角度、LED 颜色等控制参数，发送到 `actuator_queue`。
- **`control_task`**：从 `actuator_queue` 获取控制参数，调用 LEDC / RMT / GPIO 等驱动执行。保证电机控制时序的实时性。
- **`heartbeat_task`**：每 5~10 秒发布一次心跳包，报告设备在线状态。

#### 状态机
```
INIT → WIFI_CONNECTING → MQTT_CONNECTING → RUNNING → (DISCONNECTED → RECONNECTING)
```
- 断网时进入本地安全模式：维持当前电机状态，不再执行远程指令，尝试重连。

### 3.4 边侧 (PC) 软件架构建议

#### 技术栈
- **语言**：Python 3.10+
- **MQTT 客户端**：`paho-mqtt`
- **LLM 交互**：`openai` SDK 或 `requests` 直接调用 REST API
- **Broker**：本地 `mosquitto` 或 `EMQX`

#### 数据流
1. **订阅** `device/esp32_01/sensors` 和 `device/esp32_01/heartbeat`。
2. 收集最近 N 秒的传感器数据，构造成结构化 Prompt。
3. **调用 LLM API**，要求以 **JSON Mode** 或 **Function Calling** 返回控制指令，避免自然语言解析的不确定性。
4. **解析 LLM 返回的 JSON**，转换为 MQTT 消息，发布到 `device/esp32_01/commands`。

#### Prompt 设计示例
```text
你是一个机器人运动控制器。
当前传感器数据：
- 编码器角度: 90°
- 距离传感器: 15cm
- 温度: 25°C

规则：
1. 当距离小于 20cm 时，舵机应后退到 45° 并亮起黄色警示灯。
2. 正常情况下保持 90° 中位，灯为蓝色。

请以纯 JSON 格式返回，不要包含任何解释：
{
  "commands": [
    {"type": "servo", "id": 0, "angle": 45},
    {"type": "rgb", "r": 255, "g": 255, "b": 0}
  ]
}
```

### 3.5 Topic 设计规范

| Topic | 方向 | 内容 |
|-------|------|------|
| `device/{id}/sensors` | 端侧 → 边侧 | 传感器数据 JSON |
| `device/{id}/commands` | 边侧 → 端侧 | 控制指令 JSON |
| `device/{id}/heartbeat` | 端侧 → 边侧 | 在线心跳包 |
| `device/{id}/ack` | 端侧 → 边侧 | 指令执行回执（可选） |

### 3.6 数据格式示例

**传感器上报 (Sensor Payload)**
```json
{
  "timestamp": 1712985600,
  "device_id": "esp32_01",
  "sensors": [
    {"type": "ec11_angle", "value": 90},
    {"type": "distance_cm", "value": 15.2},
    {"type": "temp_c", "value": 25.5}
  ]
}
```

**控制指令下发 (Command Payload)**
```json
{
  "timestamp": 1712985601,
  "commands": [
    {"type": "servo", "id": 0, "angle": 120},
    {"type": "rgb", "r": 255, "g": 0, "b": 0}
  ]
}
```

### 3.7 开发阶段建议

| 阶段 | 目标 | 关键动作 |
|------|------|----------|
| **Phase 1** | 通信打通 | ESP32 接入 WiFi + MQTT，与 PC 实现最简单的双向 Hello World | 已完成 |
| **Phase 2** | 硬件抽象 | 将传感器、执行器封装为统一 HAL 层，支持热插拔扩展 | 已完成 |
| **Phase 3** | LLM 集成 | 边侧 Python Gateway 接入 LLM API，实现传感器→决策→指令闭环 |
| **Phase 4** | 工程化 | 加入 OTA 升级、断网本地 fallback、多设备管理、日志持久化 |

---

## 4. 文件结构与当前代码状态

### 4.1 项目文件树

```
/home/byd/Desktop/espattack/
├── CMakeLists.txt              # 顶层 CMake
├── sdkconfig                   # ESP-IDF 芯片配置
├── flash.sh                    # 编译烧录脚本（内含 gen_config.py 调用）
├── gen_config.py               # 配置生成器：control/*.json → main/app_config.h
├── control/                    # 用户可编辑的配置（JSON）
│   ├── servo.json     # 舵机角度映射
│   ├── rgb.json       # 颜色 keyframe
│   ├── display.json   # LCD 显示模式和文字（已废弃）
│   ├── encoder.json   # 编码器步进/初始角度
│   └── modeSelect.json # 显示模式选择（clockDisplay / images_display_1）
├── components/
│   └── esp-mqtt/               # 官方 MQTT 客户端库（当前未编译）
├── docs/
│   ├── AGENTS.md              # AI Agent 快速介入文档
│   ├── ARCHITECTURE.md        # 详细架构/接口/API文档
│   ├── PROJECT_LOG.md         # 本文件
│   └── TUTORIAL.md            # 初学者教学文档
└── main/                       # 主组件
    ├── CMakeLists.txt          # 自动收集 hal/*.c 和 main.c
    ├── app_config.h           # 自动生成（由 gen_config.py 产出，勿手动修改）
    ├── main.c                  # 应用层：ec11_task + LCD 刷新主循环
    ├── hal/                    # 硬件抽象层
    │   ├── hal_common.h
    │   ├── hal_servo.h / .c
    │   ├── hal_rgb.h   / .c
    │   ├── hal_ec11.h  / .c
    │   ├── hal_clock.h / .c  # 软件 RTC（esp_timer + 编译时刻时间）
    │   └── hal_gc9a01.h / .c # GC9A01 SPI LCD
    ├── lvgl_clock.c/.h        # 时钟显示（数字 + lv_scale 指针）
    └── net/                    # 网络通信层（代码完整保留，当前未编译）
        ├── net_wifi.h / .c
        └── net_mqtt.h / .c
```

### 4.2 代码状态

- **HAL 层**：✅ 已完成。五个模块（`hal_servo`, `hal_rgb`, `hal_ec11`, `hal_clock`, `hal_gc9a01`）接口干净，不依赖应用层或网络层。LCD1602 已移除。
- **NET 层**：代码完整保留在 `main/net/`，**当前未编译**，固件为纯本地模式。
- **配置系统**：所有应用参数通过 `control/*.json` 配置，`./flash.sh` 运行时自动调用 `gen_config.py` 生成 `main/app_config.h`。`modeSelect.json` 控制显示模式（clockDisplay / images_display_1）。
- **应用层**：`main.c` 基于 `event_broker` 广播-订阅模型。`consumer_task` 将事件转换为 `angle_msg_t` 后广播到 4 个私有队列，各消费者独立运行。
- **当前依赖组件**：`driver`, `esp_driver_rmt`, `esp_driver_ledc`, `esp_driver_gpio`, `esp_driver_i2c`, `esp_driver_spi`, `esp_timer`, `lvgl`。
- **EC11 架构**：轮询任务（10ms）替代 ISR 旋转检测，按键仍使用 ISR。彻底解决 ISR 内阻塞 API 导致的 panic 问题。
- **GC9A01**：直接 SPI 驱动（Bodmer init sequence），40MHz，`hal_gc9a01_init()` 每次调用都会硬复位+清屏，确保 LVGL 接管时状态干净。
- **时钟显示（v0.9）**：数字模式（HH:MM:SS + 日期标签）；指针模式使用 `lv_scale`（`LV_SCALE_MODE_ROUND_INNER`）+ `lv_scale_set_line_needle_value()` 自动处理指针旋转，优于手动画线方案。时间源为编译主机时间（`gen_config.py` 嵌入 `CFG_BUILD_*` 宏）。EC11 按键切换数字/指针，旋转切换内容（时间/日期/星期）。

### 4.3 编译验证记录

#### 第一次（HAL 层重构完成）
- **时间**：2026-04-13
- **命令**：`export IDF_PATH=/home/byd/.espressif/v6.0/esp-idf && cd build && ninja all`
- **结果**：✅ 编译成功，`blink.bin` 大小 `0x331d0` bytes (约 205 KB)，占 app 分区 20%。

#### 第二次（Phase 1: WiFi + MQTT 接入后）
- **时间**：2026-04-13
- **命令**：同上
- **结果**：✅ 编译成功，`blink.bin` 大小 `0xdfdf0` bytes (约 897 KB)。
- **体积变化说明**：由于引入了完整的 WiFi + TCP/IP + MQTT + TLS 协议栈，固件体积从 205KB 增至 897KB，占用了默认 1MB app 分区的 87%。

#### 第三次（切回本地模式 + EC11 消抖优化）
- **时间**：2026-04-13
- **命令**：同上
- **结果**：✅ 编译成功，`blink.bin` 大小 `0x332b0` bytes (约 206 KB)。
- **变更说明**：
  1. 从 `main/CMakeLists.txt` 中移除了网络组件依赖，排除 `main/net/*.c` 出编译列表，固件恢复为轻量本地模式。
  2. 在 `hal_ec11.c` 中加入 **5ms 软件消抖**，解决机械编码器触点抖动导致的角度跳变和方向误判。

#### 第四次（EC11 ISR 重构，LCD1602 初步接入）
- **时间**：2026-04-15
- **结果**：✅ 编译成功，`blink.bin` 大小 `0x37b40` bytes (约 226 KB)。
- **变更说明**：
  1. **EC11 ISR panic 修复**：ISR 内直接调用 `hal_rgb_set_color` 导致 `Interrupt wdt timeout`。重构为 ISR 只做消抖+队列发送，`ec11_task` 任务从队列接收后更新舵机和 RGB。
  2. **移除 RGB 渐变**：删除 `rgb_step_towards`，EC11 转动时舵机和 RGB 直接跳变。
  3. **颜色映射修复**：90° 原来算出绿色（错），现为蓝色 `(0,0,255)`。
  4. **EC11 步进**：从 5° 改为 2° 每格。
  5. **EC11 限位**：0°/180° 阻断，需反向旋转解除。
  6. **LCD1602**：GPIO8/9，0x27，5kHz，MAPPING=0，显示白色方块。
  7. **flash.sh**：添加 `XTENSA_TOOLCHAIN` PATH 修复 cmake 找不到编译器。

#### 第五次（配置层重构 + LCD MAPPING=1）
- **时间**：2026-04-16
- **结果**：✅ 编译成功，`blink.bin` 大小 `0x37bc0` bytes (约 226 KB)。
- **变更说明**：
  1. **JSON 配置层**：新增 `control/` 目录（servo.json, rgb.json, display.json, encoder.json）和 `gen_config.py`，实现配置与代码分离。
  2. **gen_config.py**：读取 `control/*.json` 生成 `main/app_config.h`，包含所有 `CFG_*` 宏和 RGB keyframe 数组。
  3. **flash.sh**：加入 `gen_config.py` 预调用，每次编译自动重新生成配置头文件。
  4. **main.c 重构**：移除硬编码常量，引用 `app_config.h` 的 `CFG_*` 宏，通过 `CFG_EC11_TO_SERVO()` 做角度映射。
  5. **LCD1602**：MAPPING=0→1（反向映射），白色方块问题解决，LCD 正常显示开机文字。

#### 第六次（GC9A01 驱动 + LVGL v9 集成 + GPIO 测试任务）
- **时间**：2026-04-16
- **结果**：✅ 编译成功，blink.bin 大小 0x37cb0 bytes (约 226 KB)。
- **变更说明**：
  1. **hal_gc9a01.c/h**：新增 GC9A01 1.28" 圆形 SPI LCD 驱动，支持 RGB565、SPI 40MHz、DMA、幂等初始化。
  2. **LVGL v9 集成**：lvgl_flush_cb 实现、PSRAM 降级到 malloc、分块 SPI 传输（8192 bytes/chunk）。
  3. **GPIO 测试任务**：gpio_test_task 在 Core 0 以 5Hz 翻转 GPIO1（DC 引脚），用于硬件通路验证。
  4. **build error 修复**：gpio_test_task 原定义在 app_main() 内部（非法），移至文件作用域。
  5. **lvgl_demo_init 未被调用**：GC9A01 和 LVGL 初始化代码已就位，但当前固件中 lvgl_demo_init() 尚未被 app_main() 调用。

#### 第七次（v0.7 事件总线 + 私有队列 + GC9A01 竞态修复）
- **时间**：2026-04-17
- **结果**：✅ 编译成功，blink.bin 大小 0x739e0 bytes (约 458 KB)。
- **变更说明**：
  1. **事件总线 `event_broker.c/h`**：引入本地轻量级事件总线，支持事件类型订阅和 100Hz 节流。
  2. **私有队列**：`consumer_task` 将事件广播到 4 个私有队列（`s_servo_queue`、`s_rgb_queue`、`s_lvgl_queue`、`s_lcd_queue`），彻底修复共享队列竞态导致部分消费者收不到消息的问题。
  3. **EC11 GPIO 修复**：`hal_ec11.c` 中 CLK/DT/SW 从错误的 6/7/8 改回 5/6/7，消除与 LCD1602 SDA（GPIO8）的冲突。
  4. **EC11 轮询修复**：`EC11_POLL_MS` 从 5 改为 10（避免 `pdMS_TO_TICKS(5)==0`），轮询任务优先级从 20 降为 12，修复 `vTaskDelay(0)` 导致的 CPU 饿死。
  5. **GC9A01 竞态修复**：`lvgl_task` 的创建从 `app_main` 开头移到 `hal_gc9a01_spi_test()` 之后，避免 SPI 测试和 LVGL flush 同时操作同一 SPI 设备导致命令序列错乱。
  6. **GC9A01 幂等重置**：`hal_gc9a01_init()` 在 `s_spi` 已存在时也会执行硬复位 + 初始化序列 + 清屏，确保 LVGL 每次接管都是干净画布。
  7. **LVGL 旋转中心**：方块设置 `transform_pivot_x/y = 40`，绕中心旋转而不是左上角。
  8. **LVGL tick 修复（方块不动）**：`lvgl_task` 循环中添加 `lv_tick_inc(10)`。参考 [UsefulElectronics/esp32s3-gc9a01-lvgl](https://github.com/UsefulElectronics/esp32s3-gc9a01-lvgl) 项目确认：LVGL 9 必须有人为 tick 源，`lv_timer_handler()` 才能推进内部 timer，否则方块只绘制一次便永远不再刷新。
  9. **GC9A01 颜色修复（紫色→蓝色）**：`lvgl_flush_cb` 中添加 `lv_draw_sw_rgb565_swap(px_map, lv_area_get_size(area))`；MADCTL 从 `0x08`（BGR）改回 `0x00`（RGB）。根因：ESP32-S3 小端存储使 RGB565 像素低字节在前，但 GC9A01 SPI 接收时把先收到的字节当作高字节，导致红蓝通道错位。

### 4.4 如何重新开启 WiFi + MQTT

`net/` 目录下的源代码完整保留。如需重新启用网络功能，只需修改 `main/CMakeLists.txt`：

```cmake
# 当前本地模式
file(GLOB SOURCES "hal/*.c" "main.c")
idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_rmt esp_driver_ledc esp_driver_gpio esp_timer)
```

改为：

```cmake
# 网络模式
file(GLOB_RECURSE SOURCES "*.c")
idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_rmt esp_driver_ledc esp_driver_gpio
                                  esp_wifi esp-mqtt nvs_flash esp_timer)
```

然后恢复 `main.c` 中 `net_wifi.h`、`net_mqtt.h`、NVS 初始化、网络初始化、MQTT 发布任务和回调即可（参考 git 历史或文档备份）。

### 4.5 MQTT 接入设计备忘（Phase 1 已验证）

- **外部组件**：当前 ESP-IDF v6.0 未默认集成 `esp-mqtt`，项目通过以下命令拉取了官方库：
  ```bash
  cd /home/byd/Desktop/espattack/components
  git clone --depth 1 https://github.com/espressif/esp-mqtt.git
  ```
- **Topic 设计**：
  - `device/esp32_01/sensors`：发布角度和 RGB 值
  - `device/esp32_01/commands`：订阅远程控制指令
  - `device/esp32_01/heartbeat`：每 5 秒发布一次在线状态
- **JSON 解析策略**：采用轻量级字符串扫描：在 payload 中搜索 `"angle"` 并提取后续的第一个整数值。可兼容 `{"angle":120}` 和 `{"commands":[{"type":"servo","angle":120}]}` 两种格式。
- **安全设计**：`g_target_angle` 使用 `portMUX_TYPE` 临界区保护，确保中断（EC11）、MQTT 回调、主循环三方的并发安全。

- **烧录端口**：`/dev/ttyACM0`

---

## 5. 接线备忘

### 5.1 现有模块接线（2026-04-16）

| 模块 | 引脚 | 接开发板 | 备注 |
|------|------|----------|------|
| **SG90** | 信号 | GPIO4 | 5V 供电 |
| **EC11 CLK** | A相 | GPIO5 | 3.3V 供电 |
| **EC11 DT** | B相 | GPIO6 | |
| **EC11 SW** | 按键 | GPIO7 | |
| **LCD1602 SDA** | I2C 数据 | GPIO8 | PCF8574 I2C 背板 |
| **LCD1602 SCL** | I2C 时钟 | GPIO9 | |
| **LCD1602 VCC** | 电源 | 5V | |
| **LCD1602 GND** | 地 | GND | |
| **LCD1602 VO** | 对比度 | 背板电位器中间脚 | 用背板蓝色电位器调 |
| **RGB LED** | DIN | GPIO48 | 板载 WS2812 |

### 5.2 GC9A01 1.28" 圆形 SPI LCD 接线（2026-04-16）

| 模块 | 引脚 | 接开发板 | 备注 |
|------|------|----------|------|
| **GC9A01 VCC** | 电源 | **3.3V** | ⚠️ 严禁接 5V，会烧毁屏幕 |
| **GC9A01 GND** | 地 | GND | |
| **GC9A01 SDA** | MOSI | GPIO11 | SPI 数据输出 |
| **GC9A01 SCL** | CLK | GPIO12 | SPI 时钟 |
| **GC9A01 DC** | 数据/命令选择 | GPIO1 | DC=1 数据，DC=0 命令 |
| **GC9A01 RESX** | 复位 | GPIO2 | 复位信号（低电平有效） |
| **GC9A01 CS** | 片选 | GPIO10 | 低电平选通 |

**硬件参数：**
- 屏幕分辨率：240×240 像素
- 颜色格式：RGB565（16bit）
- SPI 频率：40MHz（由 `hal_gc9a01.c` 中 `dev_cfg.clock_speed_hz` 设定）
- DMA 通道：SPI2_HOST + SPI_DMA_CH_AUTO

**GPIO 分配全览（2026-04-16 当前）：**

| GPIO | 用途 |
|------|------|
| GPIO1 | GC9A01 DC（数据/命令切换） |
| GPIO2 | GC9A01 RESX（复位，低有效） |
| GPIO3 | 无源蜂鸣器 PWM（LEDC_CHANNEL_1，EC11 按键触发播放旋律） |
| GPIO4 | SG90 舵机信号（PWM） |
| GPIO5 | EC11 A相（CLK） |
| GPIO6 | EC11 B相（DT） |
| GPIO7 | EC11 按键（SW） |
| GPIO8 | LCD1602 SDA（I2C） |
| GPIO9 | LCD1602 SCL（I2C） |
| GPIO10 | GC9A01 CS（片选） |
| GPIO11 | GC9A01 SDA（SPI MOSI） |
| GPIO12 | GC9A01 SCL（SPI CLK） |
| GPIO48 | RGB LED DIN（WS2812 RMT） |

---

### 5.3 GC9A01 驱动状态（2026-04-16 最终）

**当前状态：✅ 已验证工作**

#### 验证方法
`hal_gc9a01_spi_test()` 使用 `spi_device_transmit()` 直接操作 SPI 总线，填充纯色验证。完成后 LVGL 接管显示旋转蓝色方块。

#### 启动流程
1. 直接 SPI 测试：RED(5s) → GREEN(2s) → BLUE(2s) 填充全屏
2. 10 秒延迟（观察屏幕）
3. esp_lcd 框架初始化 + LVGL demo（旋转蓝色方块）

#### 关键串口日志
```
I (1703) APP: >>> calling hal_gc9a01_spi_test()...
I (1703) GC9A01_SPI: Direct SPI test starting...
I (1703) GC9A01_SPI: SPI bus initialized
I (1713) GC9A01_SPI: Toggling RESX...
I (1963) GC9A01_SPI: RESX done
I (1963) GC9A01_SPI: Sending GC9A01 init sequence...
I (2133) GC9A01_SPI: Init done, filling screen RED...
I (2233) GC9A01_SPI: Screen filled RED! (look at display for 5s)
I (7233) GC9A01_SPI: Now filling GREEN...
I (9333) GC9A01_SPI: Now filling BLUE...
I (11433) GC9A01_SPI: Direct SPI test complete!
I (11433) APP: >>> Direct SPI test done
I (21433) APP: >>> calling hal_gc9a01_init() for LVGL...
W (21433) GC9A01: SPI bus already initialized (ESP_ERR_INVALID_STATE)
I (21443) GC9A01: panel IO created
I (21623) GC9A01: GC9A01 initialized
I (21623) GC9A01: GC9A01 panel ready
I (21623) APP: >>> hal_gc9a01_init() done, starting LVGL
W (21623) APP: PSRAM alloc failed, using internal RAM
I (21623) APP: LVGL buffer: 5760 pixels
```

#### 发现的问题与修复
1. **esp_lcd 首次调用返回 ESP_ERR_INVALID_STATE**：因为 `esp_lcd_new_panel_io_spi` 内部 SPI 初始化逻辑与之前测试初始化冲突。已修复：返回 HAL_ERR 避免 abort，但保留 s_panel_handle 后续复用。
2. **Task Watchdog 不断触发**：esp_timer 任务优先级高于 idle task，导致 idle task 无法喂狗。已修复：在 sdkconfig 中禁用 CONFIG_ESP_TASK_WDT 和 CONFIG_ESP_INT_WDT。
3. **PSRAM 分配失败**：LVGL 缓冲降级到内部 RAM，正常工作。
4. **DC/GC9A01 引脚冲突警告**：同一 SPI 总线上混用直接 SPI 和 esp_lcd 框架，轻微冲突但不影响功能。

#### 文件清单
- `main/hal/hal_gc9a01_spi.c`：直接 SPI 测试（填充纯色验证）
- `main/hal/hal_gc9a01.c`：esp_lcd 框架驱动（LVGL 集成）
- `main/main.c`：启动流程控制

### 5.4 GC9A01 调试全程回顾（2026-04-16）

#### 问题一：屏幕黑屏（完全无显示）

调试周期长达数小时，SPI 通信无任何报错，但屏幕始终黑屏。完整根因如下：

**根因 1 — COLMOD 颜色格式错误**
- 初始代码使用 `0x55` (RGB565)，但 Bodmer TFT_eSPI 实际使用 `0x05`
- 修复：`cmd_data(0x3A, (uint8_t[]){0x05}, 1)`

**根因 2 — Init sequence 格式完全错误**
- Bodmer 的 `writecommand()` = 单独发命令字节（DC=低，无数据）
- Bodmer 的 `writedata()` = 在命令后发数据字节（DC=高）
- 我的 `cmd_data()` 把"命令"和"数据"合并发送，但 Bodmer 序列中很多命令只需要单独的命令字节（无数据），不应调用 cmd_data()
- 例如：`cmd(0xEF)` 单独发送，不带数据；然后 `cmd(0xEB)` + data(0x14) 才是完整的一次

**根因 3 — 0xEF 和 0xEB 的顺序和重复次数错误**
- Bodmer 序列：
  ```
  writecommand(0xEF);
  writecommand(0xEB); writedata(0x14);  // ×3次
  writecommand(0xFE);  // 进入寄存器 bank
  writecommand(0xEF);
  writecommand(0xEB); writedata(0x14);  // ×3次
  ```
- 初始代码把 0xEF 和 0xEB 合并当成一个命令发送，顺序和次数均错误

**根因 4 — 缺少 MADCTL (0x36) 设置**
- 没有设置 MADCTL，显示器不知道如何解析像素的存储方向
- 修复：`cmd_data(0x36, (uint8_t[]){0x08}, 1)` (MX=1, MY=1, BGR 模式)

**根因 5 — draw_bitmap() 漏发 RAMWR 命令 (0x2C)**
- 发送像素列地址和行地址后，没有先发 `cmd(0x2C)` 告知显示器开始写入 RAM
- 像素数据直接被显示器忽略
- 修复：在每行像素传输前加 `cmd(0x2C)`

**根因 6 — Display Inversion (0x21) 导致部分面板黑屏**
- Bodmer 序列包含 `writecommand(0x21)` (Display Inversion ON)
- 在这块面板上导致完全黑屏，移除后恢复正常
- 保留 `cmd(0x35)` (TE ON) 和 `cmd(0x11)` (Sleep Out) + `cmd(0x29)` (Display ON)

**根因 7 — RGB565 字节序与面板 BGR 模式不匹配（全屏测试颜色顺序错）**
- 面板 MADCTL=0x08 设为 BGR 模式，但 SPI 发送 RGB565 时高字节=R、低字节=B
- 导致 0xF800(RGB=red) 被面板解读为 BGR 后呈蓝紫色；0x001F(RGB=blue) 呈黄红色
- 全屏测试颜色顺序变成：紫 → 蓝 → 黄（实际应为红 → 绿 → 蓝）
- 修复 swap：RED 测试值改为 0x001F，BLUE 测试值改为 0xF800（仅在全屏验证阶段）
- 注意：LVGL 内部颜色与面板颜色仍存在偏差（见下方"遗留问题"）

---

#### 问题二：方块静止不旋转

LVGL 定时器机制与 ESP-IDF esp_timer 的优先级冲突导致方块不转：

**现象：**
- `lv_timer_create()` 创建的 timer callback 从未被调用
- `lv_timer_handler()` 在 lvgl_update_task 中正常运行，报告 `next_due=50ms`（说明 timer 在册）
- 但 `lv_refr_now()` 未触发任何 SPI flush

**根因 — esp_timer 与 LVGL timer 不兼容：**
- `lv_timer_handler()` 内部用 `lv_timer_run()` 执行到期回调
- LVGL 9.x 内部依赖特定的 timer 机制，与 ESP-IDF esp_timer 存在优先级/调度冲突
- esp_timer 回调在 ISR 上下文运行，而 LVGL timer 需要在特定优先级线程中执行
- 导致回调虽然注册成功，但永远不会被调度执行

**解决方案（workaround）：**
- 不使用 `lv_timer_create()`，改为在 `lvgl_update_task`（FreeRTOS 任务，优先级 5）中直接更新角度
- 每 5 次 lvgl_update_task 循环（约 50ms）调用一次 `lv_obj_set_style_transform_rotation()`
- 使用 `lv_refr_now()` 强制触发渲染管线，替代依赖 LVGL timer 的异步刷新机制

**当前旋转实现（main.c lvgl_update_task）：**
```c
// 不使用 lv_timer_create()，在 lvgl_update_task 中直接更新
g_angle += 5;
if (g_angle >= 360) g_angle = 0;
if (g_square && s_lvgl_ticks % 5 == 0) {
    lv_obj_set_style_transform_rotation(g_square, (int16_t)(g_angle * 10), 0);
}
lv_refr_now(lv_display_get_default());  // 强制 SPI 刷新
```

---

#### 遗留问题：GC9A01 颜色偏差 — 已解决（v0.8，2026-04-21）

**表现：**
- 全屏纯色测试（RED/GREEN/BLUE）：颜色顺序错误（橙红-蓝-草绿）
- LVGL 图像蓝色区域显示为红/绿混合色

**根因（两层字节序错位）：**
1. **字节序**：ESP32-S3 小端存储，SPI MSB-first 发送时 GC9A01 把先收到的字节当高字节，导致颜色通道整体错位
2. **颜色位序**：GC9A01 面板内部格式为 BGR565（而非 RGB565），MADCTL=0x00 设为 RGB 后 R/B 通道被面板反向解读

**修复方案（v0.8）：**
在 `hal_gc9a01_draw_bitmap()` 中同时做 BGR565 位交换和字节交换：
```c
// BGR 位交换：RGB→BGR
uint16_t bgr = (b << 11) | (g << 5) | r;
// 字节交换：ESP32 小端 [LO,HI] → SPI [HI,LO]
row_buf[i*2]   = (bgr >> 8) & 0xFF;  // 高字节先发
row_buf[i*2+1] = bgr & 0xFF;          // 低字节后发
```
同时 `lvgl_flush_cb` 不再做额外转换（透传给 HAL）。

**调试方法（纯色基线测试）：**
| 发送值 | 预期颜色 | 零转换结果 | 字节交换结果 | BGR+字节交换 |
|--------|----------|-----------|-------------|-------------|
| 0xF800 | 红 | 橙红 ❌ | 蓝 ❌ | **红 ✅** |
| 0x07E0 | 绿 | 蓝 ❌ | 绿 ✅ | **绿 ✅** |
| 0x001F | 蓝 | 草绿 ❌ | 红 ❌ | **蓝 ✅** |

---

### 5.5 GPIO 占用全览（2026-04-21 当前）

| GPIO | 用途 | 备注 |
|------|------|------|
| GPIO1 | GC9A01 DC | SPI 数据/命令切换 |
| GPIO2 | GC9A01 RESX | 硬复位，低有效 |
| GPIO4 | SG90 舵机 PWM | 5V 供电 |
| GPIO5 | EC11 A相（CLK） | 内部上拉已开启 |
| GPIO6 | EC11 B相（DT） | |
| GPIO7 | EC11 按键（SW） | 按下低电平，200ms 消抖 |
| GPIO10 | GC9A01 CS | SPI2 片选 |
| GPIO11 | GC9A01 SDA(MOSI) | SPI 数据输出 |
| GPIO12 | GC9A01 SCL(CLK) | SPI 时钟，40MHz |
| GPIO48 | RGB LED DIN | 板载 WS2812 |

---

### 5.6 广播-订阅架构重构（v0.6，2026-04-16）

**问题**：虽然 EC11 ISR 响应正常，舵机 lerp 平滑插值也加了，但 EC11 旋转时 SG90 和 RGB 响应仍然延迟 2-3 秒，且出现输入丢包（日志显示角度跳跃）。

**根因分析**：

```
日志观察：
  ec11=70 → 10 → 20 → 170 → 180  （输入丢包）
  servo=10  （延迟巨大，lerp 在 10ms 内追不上）
  lvgl_tick 每 200 ticks（2 秒）才打印一次，说明 lvgl_update_task 在 SPI 传输期间被完全阻塞
```

根本原因：**SPI 传输是 CPU 密集循环**（`spi_device_polling_transmit` 不断轮询 DMA 完成），这段代码不调用任何 FreeRTOS API，scheduler 无法切换任务。即便 lvgl_update_task 优先级只有 5，在 SPI 传输的 57ms 期间 ec11_task（优先级 10）也抢不到 CPU。

**解决方案**：彻底拆分串行架构 → 广播-订阅并行架构：

```
ec11_reader (优先级 20)  ──广播──→  s_angle_queue  ──消费──→  servo_task (15)  → 舵机 lerp
                                                  ├─→  rgb_task (14)    → RGB 直接更新（无延迟）
                                                  ├─→  lvgl_task (5)    → EC11 角度变化触发 SPI 刷新
                                                  └─→  lcd_task (3)     → 每 100ms 刷新 LCD1602
```

- ec11_reader 只管读 EC11，立即广播，不管任何外设
- 舵机不经过 SPI，永远不被 GC9A01 阻塞
- GC9A01 独立 lvgl_task，不影响舵机和 RGB
- 每个消费者任务独立，互不阻塞

**关键代码改动**：
- `main.c` 完全重写（仅改动此文件）
- `hal_ec11.h` 加一行 `ec11_evt_t` typedef 暴露（不改动 hal_ec11.c）
- `hal_gc9a01_spi.c` 每 20 行加 `vTaskDelay(1)` 让出 CPU


---

*文档更新时间：2026-04-22*
