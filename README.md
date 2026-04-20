# ESP32-S3 端侧控制节点

以 **ESP32-S3-DevKitC-1** 为核心的端侧控制节点，通过硬件抽象层 (HAL) 管理传感器与执行器，具备 EC11 编码器、SG90 舵机、WS2812 RGB LED、LCD1602 显示、GC9A01 圆形 LCD。

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
| LCD1602 I2C | SDA | GPIO8 |
| LCD1602 I2C | SCL | GPIO9 |
| GC9A01 SPI | DC | GPIO1 |
| GC9A01 SPI | RESX | GPIO2 |
| GC9A01 SPI | CS | GPIO10 |
| GC9A01 SPI | MOSI | GPIO11 |
| GC9A01 SPI | CLK | GPIO12 |
| WS2812 RGB | DIN | GPIO48 |

## 快速开始

```bash
# 编译 + 烧录 + 监控
./flash.sh

# 仅编译
./flash.sh build

# 仅监控
./flash.sh monitor
```

## 配置系统

所有参数通过 `control/*.json` 配置，无需修改代码：

```bash
control/
├── servo.json     # 舵机角度映射
├── rgb.json       # RGB 颜色关键帧
├── display.json   # LCD 显示模式
└── encoder.json   # 编码器步进/初始角度
```

修改 JSON 后重新 `./flash.sh` 即可生效。

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
│       ├── hal_lcd1602.h/.c   # LCD1602 I2C
│       └── hal_gc9a01.h/.c    # GC9A01 SPI LCD
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
