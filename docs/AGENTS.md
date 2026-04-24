# Agent 快速介入文档 — ESP32-S3 桌面机器人控制节点

> **本文档的目标读者是 AI Agent**。阅读后应能在 3 分钟内理解项目全貌、当前状态、以及如何安全地继续开发。

---

## 1. 项目一句话定位

以 **ESP32-S3-DevKitC-1** 为核心的本地交互节点。当前固件主路径是：

- `EC11` 旋转输入
- `event_broker` 广播
- `servo/rgb/lvgl` 各自消费
- `SG90 + WS2812 + GC9A01` 独立响应

网络层代码保留在仓库中，但当前默认固件不启用。

---

## 2. 硬件平台

- **主控**：ESP32-S3-DevKitC-1 (rev v0.2, 8MB PSRAM)
- **框架**：ESP-IDF v6.0
- **串口**：`/dev/ttyACM0`
- **烧录方式**：在项目根目录运行 `./flash.sh`
- **ESP-IDF 本地路径**：`/home/mikurubeam/.espressif/v6.0/esp-idf`

---

## 3. GPIO 接线表（当前生效）

| 模块 | 信号 | GPIO | 备注 |
|------|------|------|------|
| SG90 舵机 | PWM 信号 | **GPIO4** | 5V 供电 |
| EC11 编码器 | CLK (A相) | **GPIO5** | 内部上拉已开启 |
| EC11 编码器 | DT (B相) | **GPIO6** | |
| EC11 编码器 | SW (按键) | **GPIO7** | 按下低电平 |
| GC9A01 SPI | DC | **GPIO1** | 数据/命令切换 |
| GC9A01 SPI | RESX | **GPIO2** | 硬复位，低有效 |
| GC9A01 SPI | CS | **GPIO10** | SPI 片选 |
| GC9A01 SPI | MOSI | **GPIO11** | SPI 数据输出 |
| GC9A01 SPI | CLK | **GPIO12** | SPI 时钟 |
| WS2812 RGB | DIN | **GPIO48** | 板载 |

---

## 4. 项目结构与关键文件

```
/home/mikurubeam/Desktop/espattack/
├── CMakeLists.txt
├── sdkconfig
├── flash.sh                    # 编译烧录脚本
├── gen_config.py               # 配置生成器：control/*.json → main/app_config.h
├── components/esp-mqtt/        # 官方 MQTT 库（当前未编译进固件）
├── control/                    # 用户可编辑的配置（JSON）
│   ├── servo.json     # 舵机角度映射
│   ├── rgb.json       # 颜色 keyframe
│   └── encoder.json   # 编码器步进/初始角度
├── docs/
│   ├── ARCHITECTURE.md         # 详细架构/接口/API 文档
│   ├── PROJECT_LOG.md          # 过程日志与接线备忘
│   └── AGENTS.md               # 本文件
└── main/
    ├── app_config.h           # 自动生成（由 gen_config.py 产出，勿手动修改）
    ├── main.c                  # 应用层：事件总线 + 消费者任务
    ├── event_broker.c/.h       # 本地事件总线（广播-订阅）
    └── hal/
        ├── hal_common.h        # 通用类型（颜色、错误码）
        ├── hal_servo.h/.c      # SG90 舵机，GPIO4，LEDC 50Hz
        ├── hal_rgb.h/.c        # WS2812，GPIO48，RMT 驱动
        ├── hal_ec11.h/.c       # EC11 编码器，GPIO5/6/7，轮询+ISR
        ├── hal_gc9a01.h/.c     # GC9A01 1.28" 圆形 SPI LCD，直接 SPI
        └── hal_buzzer.h/.c     # 蜂鸣器（代码保留，默认未接入主流程）
```

---

## 4.1 配置系统（control/）

所有应用层参数通过 `control/*.json` 配置，修改后重新编译即可生效，无需改动 HAL 或 main.c。

**构建流程**：
```
control/*.json  →  gen_config.py  →  main/app_config.h  →  main.c 编译
```

每次 `./flash.sh` 会自动先跑 `gen_config.py`，所以只需改 JSON 文件后执行 `./flash.sh` 即可。

### 4.1.1 servo.json — 舵机角度映射

| 字段 | 说明 | 默认值 |
|------|------|--------|
| `initial_angle` | 舵机初始角度（°） | 90 |
| `ec11_to_servo.ec11_min` | EC11 输入下限 | 0 |
| `ec11_to_servo.ec11_max` | EC11 输入上限 | 180 |
| `ec11_to_servo.servo_min` | 舵机输出下限 | 0 |
| `ec11_to_servo.servo_max` | 舵机输出上限 | 180 |

映射公式：`servo = servo_min + (ec11 - ec11_min) * (servo_max - servo_min) / (ec11_max - ec11_min)`

**示例：EC11 0-180 映射到舵机 30-150（常用保护区间）**：
```json
{
  "initial_angle": 90,
  "ec11_to_servo": {
    "ec11_min": 0,
    "ec11_max": 180,
    "servo_min": 30,
    "servo_max": 150
  }
}
```

### 4.1.2 rgb.json — RGB 颜色映射

`keyframes` 为角度→颜色关键帧列表，按 `angle` 升序排列。

**示例：0°绿→90°蓝→180°红（默认）**：
```json
{
  "initial": { "r": 0, "g": 0, "b": 255 },
  "keyframes": [
    { "angle": 0,   "r": 0,   "g": 255, "b": 0   },
    { "angle": 90,  "r": 0,   "g": 0,   "b": 255 },
    { "angle": 180, "r": 255, "g": 0,   "b": 0   }
  ]
}
```

中间角度自动线性插值。

### 4.1.3 encoder.json — 编码器参数

| 字段 | 说明 | 默认值 |
|------|------|--------|
| `step_size` | 每格旋转的角度（°） | 2 |
| `initial_angle` | 初始逻辑角度（°） | 90 |
| `reset_angle` | 按键按下后复位到的角度（°） | 90 |

---

## 5. 当前状态与最近改动

### 当前稳定架构（2026-04-24）

EC11 作为唯一输入源，通过 `event_broker` 发布事件；`consumer_task` 将事件转换为 `angle_msg_t` 后广播到 3 个**私有队列**，各任务独立运行互不阻塞：

```
ec11_reader (优先级 12) ──发布──→ event_broker ──→ consumer_task (18)
                                                          │
                          ├─→ s_servo_queue  → servo_task (15) → 只保留最新目标 + 自适应步进
                          ├─→ s_rgb_queue    → rgb_task (14)   → 颜色直接跳变
                          └─→ s_lvgl_queue   → lvgl_task (5)   → EC11 按键切换图片
```

### 已验证功能
- ✅ SG90 舵机 0°~180° 正常，EC11 限位 0°/180° 阻断
- ✅ EC11 编码器旋转 + 按键正常（步进 **2°/格**，可通过 `control/encoder.json` 修改）
- ✅ WS2812 RGB LED 正常，颜色直接跟随角度跳变（无渐变）
- ✅ GC9A01 1.28" 圆形 LCD 正常，EC11 按键可切换表情图片

### EC11 架构说明
- **旋转检测**：`ec11_reader_task` 以 `2ms` 周期轮询 CLK/DT 电平，通过 `CLK` 下降沿判断方向和步进。完全在任务上下文执行，无需担心 ISR 阻塞问题。
- **按键检测**：SW 使用 GPIO 下降沿中断 + 200ms 消抖，ISR 内直接调用 `event_broker_broadcast`，不调用任何阻塞 API。
- **`consumer_task`**（优先级 18）订阅事件总线，收到事件后立即计算 servo_target 和 RGB 颜色，广播到 3 个私有队列。

### 最近修改记录
1. **舵机初始化修复**：`hal_servo_init()` 在完成初始化后立即下发默认角度，避免上电后中位不稳定。
2. **启动顺序修复**：`servo/rgb` 先初始化，`GC9A01` 初始化完成后再启动 `lvgl_task`，最后才启动 `EC11`。
3. **EC11 轮询优化**：`EC11_POLL_MS` 从历史上的 `10ms` 下调到 `2ms`，降低快速旋转时漏脉冲概率。
4. **舵机队列优化**：`s_servo_queue` 改为单槽覆盖队列，任务只保留最新目标值。
5. **舵机平滑优化**：由固定 `1°/10ms` 改为按误差自适应步进。
6. **显示队列优化**：`s_lvgl_queue` 同样改为覆盖队列，按键切图不会被旧旋转消息淹没。
7. **事件丢弃可观测**：`event_broker` 为队列满场景补充告警日志。
8. **GC9A01 启动修复**：默认关闭启动期 `spi_test`，避免 bring-up 测试影响正式显示任务。

---

## 6. 如何编译与烧录

```bash
cd /home/mikurubeam/Desktop/espattack
./flash.sh          # build + flash + monitor
./flash.sh build    # 仅编译
./flash.sh monitor  # 仅串口监控
```

> `flash.sh` 内部设置了 toolchain PATH，删 build 目录后重建时无需额外配置。

---

## 7. 继续开发时的注意事项

### HAL 层原则
- `hal/` 目录下的代码**只依赖 ESP-IDF 底层 API**，不引用 `main.c` 或网络层变量。
- **禁止在 ISR 内调用任何可能阻塞的 API**（信号量、队列发送阻塞、malloc 等）。
- 新增传感器/执行器时，按 `hal_xxx.h / hal_xxx.c` 的命名规范新建文件，`main/CMakeLists.txt` 会自动递归收集 `.c` 文件。
- SPI 传输中每 20 行必须调用 `vTaskDelay(1)` 让出 CPU，防止阻塞 scheduler。

### GC9A01 调试提示
- 初始化使用 Bodmer TFT_eSPI 序列，`hal_gc9a01_init()` 每次调用都会执行硬复位+初始化+清屏，确保 LVGL 接管时状态干净。
- 正常运行默认**不执行** `hal_gc9a01_spi_test()`；只有做底层 bring-up 时才临时打开 `APP_RUN_GC9A01_SPI_TEST`。
- 当前稳定启动顺序是：`event_broker` → `servo/rgb` → `consumer/servo/rgb task` → `hal_gc9a01_init()` → `lvgl_task` → `hal_ec11_init()`。
- MADCTL=0x00 设置标准 RGB 模式。
- **RGB565 字节序修复**：颜色修正在 `hal_gc9a01_draw_bitmap()` 内部实现（BGR565 位交换 + 字节交换），`lvgl_flush_cb` 无需额外处理。
- **LVGL 心跳**：`lvgl_task` 循环中必须每周期调用 `lv_tick_inc(10)`，否则 `lv_timer_handler()` 永远不会触发重绘。
- 每次 SPI 传输中每 20 行调用 `vTaskDelay(1)` 让出 CPU。

### 重新启用网络层
如需恢复 WiFi + MQTT：
1. 修改 `main/CMakeLists.txt` 的 `REQUIRES`，加入 `esp_wifi esp-mqtt nvs_flash`。
2. 在 `main.c` 中恢复 `net_wifi.h`、`net_mqtt.h` 引用及初始化逻辑。
3. 参考 `docs/ARCHITECTURE.md` 或 `docs/PROJECT_LOG.md` 4.4 节。

---

## 8. 当前交互定义

- **EC11 旋转**：更新逻辑角度，驱动 SG90 与 RGB 同步变化。
- **EC11 按下**：逻辑角度复位到 `CFG_ENCODER_RESET`，同时 GC9A01 切换下一张图片。
- **默认显示模式**：`images_display_2`，循环显示 4 张表情图。
- **已知问题**：无阻塞级问题；若快速旋转仍偶发丢步，优先检查机械编码器本体与供电噪声。

*文档维护者：AI Agent + 人类开发者*  
*最后一次更新：2026-04-24*
