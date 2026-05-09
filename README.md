# ESP32-S3 桌面机器人控制节点

以 **ESP32-S3-DevKitC-1** 为核心的本地交互节点，当前固件聚焦一条稳定的本地控制链路：

- `EC11` 旋转控制逻辑角度
- `SG90` 平滑跟随目标角度
- `WS2812` 按角度实时变色
- `GC9A01` 在按下 `EC11` 按键时切换图片
- `HC-SR04` 超声波实时测距，屏幕顶部显示距离

当前版本以本地模式为主，网络层代码仍保留在仓库中，但默认不参与固件运行。

## 硬件平台

- **主控**：ESP32-S3-DevKitC-1 (rev v0.2, 8MB PSRAM)
- **框架**：ESP-IDF v6.0
- **串口**：`/dev/ttyACM0`

## GPIO 接线

| 模块 | 信号 | GPIO |
|------|------|------|
| SG90 舵机 | PWM | GPIO4 |
| EC11 编码器 | CLK | GPIO5 |
| EC11 编码器 | DT | GPIO6 |
| EC11 编码器 | SW | GPIO7 |
| GC9A01 SPI | DC | GPIO1 |
| GC9A01 SPI | RESX | GPIO2 |
| GC9A01 SPI | CS | GPIO10 |
| GC9A01 SPI | MOSI | GPIO11 |
| GC9A01 SPI | CLK | GPIO12 |
| WS2812 RGB | DIN | GPIO48 |
| HC-SR04 超声波 | Trig | GPIO8 |
| HC-SR04 超声波 | Echo | GPIO9 |

说明：
- `GPIO3` 上仍有蜂鸣器相关代码，但当前默认启动链路未启用。
- `GC9A01` 正常启动时会先执行 `hal_gc9a01_init()`，再启动 `lvgl_task`。

## 快速开始

```bash
# 编译 + 烧录 + 监控（推荐）
./flash.sh

# 仅编译
./flash.sh build

# 仅监控
./flash.sh monitor
```

如果你直接使用本机 ESP-IDF 环境，也可以：

```bash
source /home/mikurubeam/.espressif/v6.0/esp-idf/export.sh
idf.py build flash monitor
```

## 配置系统

所有应用层参数通过 `control/*.json` 配置，无需直接改 `main.c`：

```bash
control/
├── servo.json     # 舵机角度映射
├── rgb.json       # RGB 颜色关键帧
├── encoder.json   # 编码器步进/初始角度
└── modeSelect.json # 显示模式选择
```

修改 JSON 后重新 `./flash.sh` 即可生效。

当前版本会在构建前校验配置：
- `servo.json` 中 `ec11_max` 必须大于 `ec11_min`
- `rgb.json` 中 `keyframes` 至少 1 项，且角度必须严格升序、不能重复
- `encoder.json` 中 `step_size` 必须为正整数
- `modeSelect.json` 中时钟模式的 `updateIntervalMs` 必须为正整数

常用项：
- `control/encoder.json`：EC11 步进角、初始角、按键复位角
- `control/servo.json`：EC11 到舵机的映射范围
- `control/rgb.json`：角度到 RGB 关键帧映射
- `control/modeSelect.json`：在 `images_display_1`、`images_display_2`、`clockDisplay` 间切换

## 项目结构

```
embeddedDesktopRobot/
├── CMakeLists.txt
├── sdkconfig
├── flash.sh
├── gen_config.py
├── control/                    # 用户配置
├── main/
│   ├── app_config.h           # 自动生成
│   ├── main.c
│   ├── event_broker.c/.h      # 本地事件总线
│   └── hal/
│       ├── hal_common.h
│       ├── hal_servo.h/.c     # SG90 舵机
│       ├── hal_rgb.h/.c       # WS2812 RGB
│       ├── hal_ec11.h/.c      # EC11 编码器
│       ├── hal_gc9a01.h/.c    # GC9A01 SPI LCD
│       └── hal_buzzer.h/.c    # 蜂鸣器（代码保留，当前默认未启用）
└── docs/
    ├── AGENTS.md              # AI Agent 快速介入文档
    ├── ARCHITECTURE.md        # 架构/API 文档
    ├── PROJECT_LOG.md         # 开发日志
    ├── DEBUG_LOG.md           # 根因分析手册
    └── TUTORIAL.md            # 初学者教学文档
```

## 文档

- [AI Agent 快速介入文档](docs/AGENTS.md) — 项目全貌、接线表、配置说明
- [架构文档](docs/ARCHITECTURE.md) — HAL 接口、任务设计
- [过程日志](docs/PROJECT_LOG.md) — 接线备忘、开发记录
- [调试记录](docs/DEBUG_LOG.md) — 根因分析手册
- [教学文档](docs/TUTORIAL.md) — 初学者入门指南

## 当前运行行为

- 上电后先初始化 `servo/rgb`，再初始化 `GC9A01`，最后启动 `EC11`
- 所有任务创建都会检查返回值；若 `GC9A01` 初始化失败，则不会继续启动 `lvgl_task`
- `EC11` 旋转检测采用 `2ms` 轮询，按键采用 GPIO 中断 + `200ms` 消抖
- `hal_ec11_get_queue()` 返回独立的 EC11 事件队列，队列元素类型为 `hal_ec11_msg_t`
- `event_broker_publish()` 带 `1ms` burst guard，用于过滤极端机械抖动
- `servo_task` 只保留最新目标值，并按误差自适应步进，避免快速旋转时严重滞后
- `rgb_task` 也只保留最新颜色目标，避免无意义的旧颜色积压
- `lvgl_task` 默认使用 `images_display_2` 模式，按下 `EC11` 会在 4 张表情图间切换
