# Agent 快速介入文档 — ESP32-S3 端侧控制系统

> **本文档的目标读者是 AI Agent**。阅读后应能在 3 分钟内理解项目全貌、当前状态、以及如何安全地继续开发。

---

## 1. 项目一句话定位

以 **ESP32-S3-DevKitC-1** 为核心的端侧控制节点，通过硬件抽象层 (HAL) 管理传感器与执行器，具备 WiFi + MQTT 能力（当前为本地模式）。

---

## 2. 硬件平台

- **主控**：ESP32-S3-DevKitC-1 (rev v0.2, 8MB PSRAM)
- **框架**：ESP-IDF v6.0
- **串口**：`/dev/ttyACM0`
- **烧录方式**：在项目根目录运行 `./flash.sh`

---

## 3. GPIO 接线表（当前生效）

| 模块 | 信号 | GPIO | 备注 |
|------|------|------|------|
| SG90 舵机 | PWM 信号 | **GPIO4** | 5V 供电 |
| EC11 编码器 | CLK (A相) | **GPIO5** | 内部上拉已开启 |
| EC11 编码器 | DT (B相) | **GPIO6** | |
| EC11 编码器 | SW (按键) | **GPIO7** | 按下低电平 |
| LCD1602 I2C | SDA | **GPIO8** | PCF8574T 背板 |
| LCD1602 I2C | SCL | **GPIO9** | PCF8574T 背板 |
| WS2812 RGB | DIN | **GPIO48** | 板载 |

**LCD1602 电源**：VCC → 5V，GND → GND。对比度由背板上的蓝色电位器调节。

---

## 4. 项目结构与关键文件

```
/home/byd/Desktop/espattack/
├── CMakeLists.txt
├── sdkconfig
├── flash.sh                    # 编译烧录脚本
├── gen_config.py               # 配置生成器：control/*.json → main/app_config.h
├── components/esp-mqtt/        # 官方 MQTT 库（当前未编译进固件）
├── control/                    # 用户可编辑的配置（JSON）
│   ├── servo.json     # 舵机角度映射
│   ├── rgb.json       # 颜色 keyframe
│   ├── display.json   # LCD 显示模式和文字
│   └── encoder.json   # 编码器步进/初始角度
├── docs/
│   ├── ARCHITECTURE.md         # 详细架构/接口/API 文档
│   ├── PROJECT_LOG.md          # 过程日志与接线备忘
│   └── AGENTS.md               # 本文件
└── main/
    ├── app_config.h           # 自动生成（由 gen_config.py 产出，勿手动修改）
    ├── main.c                  # 应用层：ec11_task + LCD 刷新主循环
    └── hal/
        ├── hal_common.h        # 通用类型（颜色、错误码）
        ├── hal_servo.h/.c      # SG90 舵机，GPIO4，LEDC 50Hz
        ├── hal_rgb.h/.c        # WS2812，GPIO48，RMT 驱动
        ├── hal_ec11.h/.c       # EC11 编码器，GPIO5/6/7，队列ISR
        └── hal_lcd1602.h/.c    # LCD1602 + PCF8574，I2C GPIO8/9
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

### 4.1.3 display.json — LCD 显示

| 字段 | 说明 |
|------|------|
| `startup.row0/row1` | 开机显示的两行文字 |
| `runtime_mode` | 运行时显示模式（1-4，见下表）|
| `custom_text` | mode 2/4 时 Row1/Row0 显示的自定义文字 |

**LCD 显示模式**：

| 模式 | Row 0 | Row 1 |
|------|-------|-------|
| 1 | `Angle: XXX deg` | `R:XXX G:XXX B:XXX` |
| 2 | `Angle: XXX deg` | `custom_text` |
| 3 | `Angle: XXX deg` | （空白）|
| 4 | `custom_text` | `Angle: XXX deg` |

### 4.1.4 encoder.json — 编码器参数

| 字段 | 说明 | 默认值 |
|------|------|--------|
| `step_size` | 每格旋转的角度（°） | 2 |
| `initial_angle` | 初始逻辑角度（°） | 90 |
| `reset_angle` | 按键按下后复位到的角度（°） | 90 |

---

## 5. 当前状态与最近改动

### 已验证功能
- ✅ SG90 舵机 0°~180° 正常，EC11 限位 0°/180° 阻断
- ✅ EC11 编码器旋转 + 按键正常（当前步进 **2°/格**）
- ✅ WS2812 RGB LED 正常，颜色直接跟随角度跳变（无渐变）
- ⚠️ LCD1602 I2C 背板通信已打通，但显示白色方块（问题未解决）

### EC11 架构（重要变更）
ISR 只负责消抖检测，通过 FreeRTOS 队列将事件发送到 `ec11_task`，**不允许在 ISR 里调用任何阻塞 API**（如 `hal_rgb_set_color` 内部会等待 RMT 信号量，在 ISR 里调用会触发 `Interrupt wdt timeout`）。

```
EC11 ISR → xQueueSend → ec11_task (优先级10) → hal_servo_set_angle + hal_rgb_set_color
```

### 最近修改记录
1. **EC11 ISR 重构**：移除 ISR 内回调，改为队列事件。修复了 `Interrupt wdt timeout` panic。
2. **EC11 限位**：0°/180° 限位阻断，EC11 反方向旋转才能解除。
3. **RGB 响应**：移除 `rgb_step_towards` 渐变，EC11 转动时颜色直接跳变。
4. **颜色映射修复**：90° 原来错误算出绿色，现在正确为蓝色 `(0,0,255)`。
5. **EC11 步进**：从 5° 改为 2° 每格。
6. **LCD 驱动**：`MAPPING=0`（标准映射），`lcd_strobe_en` 策略已还原原始方式。
7. **flash.sh**：添加了 `XTENSA_TOOLCHAIN` PATH 修复 cmake 工具链路径问题。
8. **网络层**：`main/net/` 目录下代码完整保留，但 **当前未编译**（固件为本地模式）。
9. **LCD 驱动**：`MAPPING=1`（反向映射），部分廉价 PCF8574 背板需要反向映射才能正确显示文字。

---

## 6. 如何编译与烧录

```bash
cd /home/byd/Desktop/espattack
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

### LCD1602 调试提示
- 如果屏幕出现满屏灰色块，通常是 **PCF8574 引脚映射** 问题。当前 `MAPPING=1`（反向映射）：
  ```c
  #define LCD_I2C_MAPPING     1   // 0=标准映射, 1=反向映射
  ```
- 如果屏幕只有背光没有字，**先旋转背板上的蓝色电位器调对比度**，再查软件。
- 出现 `ESP_ERR_INVALID_RESPONSE` 时，代码已内置一次重试，若仍频繁报错需检查杜邦线接触或上拉电阻。

### 重新启用网络层
如需恢复 WiFi + MQTT：
1. 修改 `main/CMakeLists.txt` 的 `REQUIRES`，加入 `esp_wifi esp-mqtt nvs_flash`。
2. 在 `main.c` 中恢复 `net_wifi.h`、`net_mqtt.h` 引用及初始化逻辑。
3. 参考 `docs/ARCHITECTURE.md` 或 `docs/PROJECT_LOG.md` 4.4 节。

---

## 8. 快速联系人机接口

- **用户当前关注点**：LCD1602 显示白色方块（待解决）、EC11+RGB 响应是否顺畅。
- **已知未解决**：LCD1602 显示问题（MAPPING 已切换为 1，请验证是否正常显示）。

*文档维护者：AI Agent + 人类开发者*  
*最后一次更新：2026-04-15*
