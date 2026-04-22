# control/images/ — 图片资源目录

## 目录结构

```
control/images/
├── README.md          # 本文件
├── jpg_to_c.py        # JPG → LVGL C 数组转换脚本
├── jpg/               # 存放原始 JPG 文件（不被 git 跟踪）
│   └── .gitkeep
└── *.c                # 转换后的 LVGL 图片数组（可被 main.c 引用）
    └── resized-image.c  # 当前使用的图片
```

## 使用方法

### 1. 放入 JPG 文件

将 JPG 文件放入 `jpg/` 目录，例如 `jpg/photo.jpg`。

### 2. 转换为 C 文件

```bash
python3 jpg_to_c.py jpg/photo.jpg images/photo.c
```

或指定输入输出：

```bash
python3 control/images/jpg_to_c.py my_picture.jpg
# 输出到: control/images/my_picture.c
```

### 3. 更新 modeSelect.json

在 `modeSelect.json` 中更新 `localPath` 指向新的 `.c` 文件。

### 4. 重新编译烧录

```bash
./flash.sh
```

## 注意事项

- 图片会被 **resize 到 240×240**（适配 GC9A01 屏幕）
- 输出格式：**RGB565**，字节序与 GC9A01 面板兼容（HAL 层会做 BGR 转换）
- `LV_IMAGE_DECLARE(name)` 宏使 LVGL 能直接引用该图片变量
