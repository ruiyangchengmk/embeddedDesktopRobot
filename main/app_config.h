// ===========================================
// app_config.h — 自动生成，请勿手动修改
// 由 gen_config.py 从 control/*.json 生成
// ===========================================

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ---- Encoder ---- */
#define CFG_ENCODER_STEP_SIZE   2
#define CFG_ENCODER_INITIAL    90
#define CFG_ENCODER_RESET      90

/* ---- Servo ---- */
#define CFG_SERVO_INITIAL       90
/* EC11->Servo: servo = servo_min + (ec11 - ec11_min) * (servo_max - servo_min) / (ec11_max - ec11_min) */
#define CFG_SERVO_SMIN          0
#define CFG_SERVO_SMAX          180
#define CFG_SERVO_EMIN          40
#define CFG_SERVO_EMAX          130
#define CFG_SERVO_SCALE_NUM     180
#define CFG_SERVO_SCALE_DEN     90
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

/* ---- Display Mode ---- */
#define CFG_DISPLAY_MODE_STRING "clockDisplay"
#define CFG_MODE_IMAGES_DISPLAY_1 0
#define CFG_MODE_CLOCK_DISPLAY    1
#define CFG_CLOCK_SHOW_DATE       1
#define CFG_CLOCK_SHOW_TIME       1
#define CFG_CLOCK_UPDATE_MS       1000
#define CFG_CLOCK_ANALOG_FACE_R   100
#define CFG_CLOCK_ANALOG_HOUR_LEN 60
#define CFG_CLOCK_ANALOG_MIN_LEN  80
#define CFG_CLOCK_ANALOG_SEC_LEN  90
#define CFG_CLOCK_ANALOG_SHOW_SEC 1

/* ---- Build Timestamp (host PC time when flashed) ---- */
#define CFG_BUILD_YEAR  2026
#define CFG_BUILD_MONTH 4
#define CFG_BUILD_DAY   22
#define CFG_BUILD_HOUR  9
#define CFG_BUILD_MIN   18

#endif /* APP_CONFIG_H */