// ===========================================
// app_config.h — 自动生成，请勿手动修改
// 由 gen_config.py 从 control/*.json 生成
// ===========================================

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ---- Encoder ---- */
#define CFG_ENCODER_STEP_SIZE   10
#define CFG_ENCODER_INITIAL    90
#define CFG_ENCODER_RESET      90

/* ---- Servo ---- */
#define CFG_SERVO_INITIAL       90
/* EC11->Servo: servo = servo_min + (ec11 - ec11_min) * (servo_max - servo_min) / (ec11_max - ec11_min) */
#define CFG_SERVO_SMIN          0
#define CFG_SERVO_SMAX          180
#define CFG_SERVO_EMIN          0
#define CFG_SERVO_EMAX          180
#define CFG_SERVO_SCALE_NUM     180
#define CFG_SERVO_SCALE_DEN     180
#define CFG_EC11_TO_SERVO(x) (((x) < CFG_SERVO_EMIN) ? CFG_SERVO_SMIN : (((x) > CFG_SERVO_EMAX) ? CFG_SERVO_SMAX : (CFG_SERVO_SMIN + ((x) - CFG_SERVO_EMIN) * CFG_SERVO_SCALE_NUM / CFG_SERVO_SCALE_DEN)))

/* ---- RGB ---- */
#define CFG_RGB_INITIAL_R       0
#define CFG_RGB_INITIAL_G       0
#define CFG_RGB_INITIAL_B       255
#define CFG_RGB_KEYFRAME_COUNT  3

typedef struct {
    int angle;
    uint8_t r, g, b;
} rgb_keyframe_t;

static const rgb_keyframe_t s_rgb_keyframes[CFG_RGB_KEYFRAME_COUNT] = {
    {0, 0, 255, 0},
    {90, 0, 0, 255},
    {180, 255, 0, 0}
};

/* ---- Display ---- */
#define CFG_DISPLAY_STARTUP_ROW0  "  Hello World!"
#define CFG_DISPLAY_STARTUP_ROW1  "  ESP32-S3 OK!"
#define CFG_DISPLAY_MODE           1
#define CFG_DISPLAY_CUSTOM_TEXT   "  Ready!"

#endif /* APP_CONFIG_H */