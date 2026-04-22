#!/usr/bin/env python3
"""
jpg_to_c.py — 将 control/images/jpg/ 目录下的 JPG 文件转换为 LVGL C 数组格式

用法:
    python3 jpg_to_c.py [输入.jpg] [输出.c]

示例:
    python3 jpg_to_c.py photo.jpg my_image.c

输入:
    control/images/jpg/ 目录下的任意 JPG 文件
    默认 resize 到 240x240 (GC9A01 屏幕尺寸)

输出:
    生成 .c 文件，与 LVGL 的 LVGL_IMAGE_DECLARE 格式兼容
    输出到 control/images/ 目录
"""

import sys
import os
from PIL import Image

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
JPG_DIR = os.path.join(SCRIPT_DIR, "jpg")
OUTPUT_DIR = SCRIPT_DIR
TARGET_W = 240
TARGET_H = 240


def rgb_to_rgb565(r, g, b):
    """RGB888 → RGB565"""
    rv = (r >> 3) & 0x1F
    gv = (g >> 2) & 0x3F
    bv = (b >> 3) & 0x1F
    return (rv << 11) | (gv << 5) | bv


def convert_jpg_to_c(jpg_path, output_path, width=TARGET_W, height=TARGET_H):
    """读取 JPG，resize 到 240x240，输出为 RGB565 C 数组"""
    img = Image.open(jpg_path)
    img = img.convert("RGB")
    img = img.resize((width, height), Image.Resampling.LANCZOS)

    pixels = list(img.getdata())
    assert len(pixels) == width * height, f"Pixel count mismatch: {len(pixels)}"

    out = []
    out.append("#ifdef __has_include")
    out.append("    #if __has_include(\"lvgl.h\")")
    out.append("        #ifndef LV_LVGL_H_INCLUDE_SIMPLE")
    out.append("            #define LV_LVGL_H_INCLUDE_SIMPLE")
    out.append("        #endif")
    out.append("    #endif")
    out.append("#endif")
    out.append("")
    out.append("#if defined(LV_LVGL_H_INCLUDE_SIMPLE)")
    out.append("    #include \"lvgl.h\"")
    out.append("#else")
    out.append("    #include \"lvgl/lvgl.h\"")
    out.append("#endif")
    out.append("")
    out.append("#ifndef LV_ATTRIBUTE_MEM_ALIGN")
    out.append("    #define LV_ATTRIBUTE_MEM_ALIGN")
    out.append("#endif")
    out.append("")
    base = os.path.splitext(os.path.basename(output_path))[0]
    up_name = base.upper()
    attr_name = f"LV_ATTRIBUTE_IMAGE_{up_name}"
    out.append(f"#ifndef {attr_name}")
    out.append(f"    #define {attr_name}")
    out.append("#endif")
    out.append("")
    out.append(f"const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST {attr_name} uint8_t {base}_map[] = {{")

    BYTES_PER_LINE = 16
    for y in range(height):
        row_pixels = pixels[y * width:(y + 1) * width]
        for i, (r, g, b) in enumerate(row_pixels):
            rgb565 = rgb_to_rgb565(r, g, b)
            lo = rgb565 & 0xFF
            hi = (rgb565 >> 8) & 0xFF
            comma = "," if not (y == height - 1 and i == len(row_pixels) - 1) else ""
            if i % BYTES_PER_LINE == 0:
                out.append(f"  0x{lo:02x}, 0x{hi:02x}{comma}")
            else:
                out.append(f"  0x{lo:02x}, 0x{hi:02x}{comma}")

    out.append("};")
    out.append("")

    var_name = f"LV_IMAGE_DECLARE({base})"

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(out))

    size = os.path.getsize(output_path)
    print(f"Generated: {output_path} ({size} bytes, {width}x{height} RGB565)")


def main():
    if len(sys.argv) >= 2:
        jpg_path = sys.argv[1]
        if not os.path.isabs(jpg_path):
            jpg_path = os.path.join(os.getcwd(), jpg_path)
        if len(sys.argv) >= 3:
            output_path = sys.argv[2]
        else:
            base = os.path.splitext(os.path.basename(jpg_path))[0]
            output_path = os.path.join(OUTPUT_DIR, f"{base}.c")
    else:
        if not os.path.exists(JPG_DIR):
            print(f"JPG directory not found: {JPG_DIR}")
            print("Put your .jpg files in that directory.")
            sys.exit(1)
        files = [f for f in os.listdir(JPG_DIR) if f.lower().endswith((".jpg", ".jpeg", ".png", ".bmp"))]
        if not files:
            print(f"No image files found in {JPG_DIR}")
            print("Put your .jpg files there and re-run.")
            sys.exit(1)
        print(f"Found images in {JPG_DIR}:")
        for f in files:
            print(f"  {f}")
        print(f"\nUsage: python3 {sys.argv[0]} <input.jpg> [output.c]")
        print(f"Or: python3 {sys.argv[0]} photo.jpg my_image.c")
        sys.exit(0)

    if not os.path.exists(jpg_path):
        print(f"File not found: {jpg_path}")
        sys.exit(1)

    convert_jpg_to_c(jpg_path, output_path)


if __name__ == "__main__":
    main()
