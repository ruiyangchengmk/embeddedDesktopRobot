#!/bin/bash
# 使用 eim v6.0 环境直接烧录

set -e

cd "$(dirname "$0")"
PORT="/dev/ttyACM0"

# 显式设置 ESP-IDF 环境变量，避免 idf_component_manager 报 TypeError
export ESP_IDF_VERSION=6.0.0
export IDF_PYTHON_ENV_PATH="/home/byd/.espressif/tools/python/v6.0/venv"

# eim v6.0 环境的固定路径
PYTHON="/home/byd/.espressif/tools/python/v6.0/venv/bin/python"
IDF_PY="/home/byd/.espressif/v6.0/esp-idf/tools/idf.py"
XTENSA_TOOLCHAIN="/home/byd/.espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin"
export PATH="$XTENSA_TOOLCHAIN:$PATH"

# 如果 build 目录被其他 Python 配置过，直接删掉重建
if [ -f "build/CMakeCache.txt" ]; then
    OLD_PYTHON=$(grep "^PYTHON:UNINITIALIZED=" build/CMakeCache.txt | cut -d'=' -f2 || true)
    if [ -n "$OLD_PYTHON" ] && [ "$OLD_PYTHON" != "$PYTHON" ]; then
        echo "[INFO] build 目录与当前 Python 环境不一致，清理后重建..."
        rm -rf build
    fi
fi

# 每次编译前先生成 app_config.h
echo "[INFO] Generating app_config.h from control/*.json..."
"$PYTHON" gen_config.py --input-dir control --output main/app_config.h

if [ "$1" == "build" ]; then
    "$PYTHON" "$IDF_PY" build
elif [ "$1" == "monitor" ]; then
    "$PYTHON" "$IDF_PY" -p "$PORT" monitor
else
    "$PYTHON" "$IDF_PY" -p "$PORT" build flash monitor
fi
