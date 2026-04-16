# ESP32-S3 端侧控制系统 — 项目过程与架构规划文档

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
│   ├── display.json   # LCD 显示模式和文字
│   └── encoder.json   # 编码器步进/初始角度
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
    │   └── hal_lcd1602.h / .c
    └── net/                    # 网络通信层（代码完整保留，当前未编译）
        ├── net_wifi.h / .c
        └── net_mqtt.h / .c
```

### 4.2 代码状态

- **HAL 层**：✅ 已完成。四个模块（`hal_servo`, `hal_rgb`, `hal_ec11`, `hal_lcd1602`）接口干净，不依赖应用层或网络层。
- **NET 层**：代码完整保留在 `main/net/`，**当前未编译**，固件为纯本地模式。
- **配置系统**（新增 v0.4）：所有应用参数通过 `control/*.json` 配置，`.flash.sh` 运行时自动调用 `gen_config.py` 生成 `main/app_config.h`。无需修改代码即可改舵机映射、LCD 文字、颜色关键帧等。
- **应用层**：`main.c` 引用 `app_config.h` 中的 `CFG_*` 宏，`ec11_task` 通过队列接收事件，更新舵机+RGB。
- **当前依赖组件**：`driver`, `esp_driver_rmt`, `esp_driver_ledc`, `esp_driver_gpio`, `esp_driver_i2c`, `esp_timer`。
- **EC11 ISR 架构**：ISR 只做消抖+队列发送，所有实际控制操作在 `ec11_task`（优先级10）中完成。
- **LCD1602**：I2C 地址 0x27，GPIO8/9，5kHz，**MAPPING=1**（反向映射）。

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

---

*文档更新时间：2026-04-16*
