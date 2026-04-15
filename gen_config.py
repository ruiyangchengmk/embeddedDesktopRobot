#!/usr/bin/env python3
"""
gen_config.py — 从 control/*.json 生成 main/app_config.h

用法:
    python3 gen_config.py [--output main/app_config.h] [--input-dir control]

运行时机:
    - 编译前由 CMake 自定义命令调用
    - 或手动运行: python3 gen_config.py
"""

import json
import os
import sys
import argparse


def load_json(path):
    """加载 JSON，移除顶层名为 "comment" 的键"""
    with open(path, "r", encoding="utf-8") as f:
        obj = json.load(f)
    obj.pop("comment", None)
    return obj


def validate_angle(val, name):
    if not isinstance(val, (int, float)) or val < 0 or val > 180:
        raise ValueError(f"{name} must be 0-180, got {val}")
    return int(val)


def validate_rgb(val, name):
    for c in ["r", "g", "b"]:
        if c not in val or not isinstance(val[c], int) or val[c] < 0 or val[c] > 255:
            raise ValueError(f"{name}.{c} must be 0-255, got {val}")
    return val


def generate_header(configs):
    servo = configs["servo"]
    rgb = configs["rgb"]
    display = configs["display"]
    encoder = configs["encoder"]

    # === Validate ===
    servo_init = validate_angle(servo["initial_angle"], "servo.initial_angle")
    e2s = servo["ec11_to_servo"]
    e_min = validate_angle(e2s["ec11_min"], "servo.ec11_to_servo.ec11_min")
    e_max = validate_angle(e2s["ec11_max"], "servo.ec11_to_servo.ec11_max")
    s_min = validate_angle(e2s["servo_min"], "servo.ec11_to_servo.servo_min")
    s_max = validate_angle(e2s["servo_max"], "servo.ec11_to_servo.servo_max")

    rgb_init = validate_rgb(rgb["initial"], "rgb.initial")
    keyframes = rgb["keyframes"]
    for kf in keyframes:
        validate_angle(kf["angle"], "rgb.keyframes[].angle")
        validate_rgb(kf, "rgb.keyframes[]")

    disp_mode = int(display["runtime_mode"])
    if disp_mode < 1 or disp_mode > 4:
        raise ValueError(f"display.runtime_mode must be 1-4, got {disp_mode}")

    enc_step = int(encoder["step_size"])
    enc_init = validate_angle(encoder["initial_angle"], "encoder.initial_angle")
    enc_reset = validate_angle(encoder["reset_angle"], "encoder.reset_angle")

    # === Generate C header ===
    out = []
    out.append("// ===========================================")
    out.append("// app_config.h — 自动生成，请勿手动修改")
    out.append("// 由 gen_config.py 从 control/*.json 生成")
    out.append("// ===========================================")
    out.append("")
    out.append("#ifndef APP_CONFIG_H")
    out.append("#define APP_CONFIG_H")
    out.append("")
    out.append("/* ---- Encoder ---- */")
    out.append(f"#define CFG_ENCODER_STEP_SIZE   {enc_step}")
    out.append(f"#define CFG_ENCODER_INITIAL    {enc_init}")
    out.append(f"#define CFG_ENCODER_RESET      {enc_reset}")
    out.append("")

    out.append("/* ---- Servo ---- */")
    out.append(f"#define CFG_SERVO_INITIAL       {servo_init}")
    # 内联映射宏 (scaler is fixed-point to avoid float)
    scale_num = s_max - s_min
    scale_den = e_max - e_min
    out.append(f"/* EC11->Servo: servo = servo_min + (ec11 - ec11_min) * (servo_max - servo_min) / (ec11_max - ec11_min) */")
    out.append(f"#define CFG_SERVO_SMIN          {s_min}")
    out.append(f"#define CFG_SERVO_SMAX          {s_max}")
    out.append(f"#define CFG_SERVO_EMIN          {e_min}")
    out.append(f"#define CFG_SERVO_EMAX          {e_max}")
    out.append(f"#define CFG_SERVO_SCALE_NUM     {scale_num}")
    out.append(f"#define CFG_SERVO_SCALE_DEN     {scale_den}")
    # Inline macro - all on one line to avoid backslash-continuation issues
    out.append(f"#define CFG_EC11_TO_SERVO(x) (((x) < CFG_SERVO_EMIN) ? CFG_SERVO_SMIN : (((x) > CFG_SERVO_EMAX) ? CFG_SERVO_SMAX : (CFG_SERVO_SMIN + ((x) - CFG_SERVO_EMIN) * CFG_SERVO_SCALE_NUM / CFG_SERVO_SCALE_DEN)))")
    out.append("")
    out.append("/* ---- RGB ---- */")
    out.append(f"#define CFG_RGB_INITIAL_R       {rgb_init['r']}")
    out.append(f"#define CFG_RGB_INITIAL_G       {rgb_init['g']}")
    out.append(f"#define CFG_RGB_INITIAL_B       {rgb_init['b']}")
    out.append(f"#define CFG_RGB_KEYFRAME_COUNT  {len(keyframes)}")
    out.append("")

    out.append("typedef struct {")
    out.append("    int angle;")
    out.append("    uint8_t r, g, b;")
    out.append("} rgb_keyframe_t;")
    out.append("")
    out.append("static const rgb_keyframe_t s_rgb_keyframes[CFG_RGB_KEYFRAME_COUNT] = {")
    for i, kf in enumerate(keyframes):
        comma = "," if i < len(keyframes) - 1 else ""
        out.append(f"    {{{kf['angle']}, {kf['r']}, {kf['g']}, {kf['b']}}}{comma}")
    out.append("};")
    out.append("")

    out.append("/* ---- Display ---- */")
    out.append(f"#define CFG_DISPLAY_STARTUP_ROW0  \"{display['startup']['row0']}\"")
    out.append(f"#define CFG_DISPLAY_STARTUP_ROW1  \"{display['startup']['row1']}\"")
    out.append(f"#define CFG_DISPLAY_MODE           {disp_mode}")
    out.append(f"#define CFG_DISPLAY_CUSTOM_TEXT   \"{display['custom_text']}\"")
    out.append("")
    out.append("#endif /* APP_CONFIG_H */")

    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser(description="Generate app_config.h from control/*.json")
    parser.add_argument("--input-dir", default="control",
                        help="Directory containing JSON config files")
    parser.add_argument("--output", default="main/app_config.h",
                        help="Output header file path")
    args = parser.parse_args()

    base = os.path.dirname(os.path.abspath(__file__))
    input_dir = os.path.join(base, args.input_dir)
    output_path = os.path.join(base, args.output)

    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    files = {
        "servo": os.path.join(input_dir, "servo.json"),
        "rgb": os.path.join(input_dir, "rgb.json"),
        "display": os.path.join(input_dir, "display.json"),
        "encoder": os.path.join(input_dir, "encoder.json"),
    }

    configs = {}
    for name, path in files.items():
        if not os.path.exists(path):
            print(f"ERROR: Config file not found: {path}", file=sys.stderr)
            sys.exit(1)
        configs[name] = load_json(path)

    try:
        header = generate_header(configs)
    except Exception as e:
        print(f"ERROR: Config validation failed: {e}", file=sys.stderr)
        sys.exit(1)

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(header)

    print(f"Generated: {output_path}")
    print(f"  Servo initial: {configs['servo']['initial_angle']} deg")
    print(f"  Servo range: [{configs['servo']['ec11_to_servo']['servo_min']}, {configs['servo']['ec11_to_servo']['servo_max']}] from EC11 [{configs['servo']['ec11_to_servo']['ec11_min']}, {configs['servo']['ec11_to_servo']['ec11_max']}]")
    print(f"  RGB initial: ({configs['rgb']['initial']['r']}, {configs['rgb']['initial']['g']}, {configs['rgb']['initial']['b']})")
    print(f"  Display mode: {configs['display']['runtime_mode']}")
    print(f"  Encoder step: {configs['encoder']['step_size']} deg/tick")


if __name__ == "__main__":
    main()
