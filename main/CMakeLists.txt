# 当前为本地模式：默认编译根目录应用层 *.c、hal/*.c，以及图片/语音资源。
#
# 注意：gen_config.py 由 flash.sh 在编译前调用，生成 main/app_config.h
# app_config.h 由 main.c include，无需在此 CMakeLists 中处理

file(GLOB APP_SOURCES "*.c")
file(GLOB HAL_SOURCES "hal/*.c")
file(GLOB IMAGE_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../control/images/*.c" "${CMAKE_CURRENT_LIST_DIR}/../control/images/c/*.c")
set(AUDIO_ASSETS
    "audio_assets/warning_too_close.pcm"
)

idf_component_register(SRCS ${APP_SOURCES} ${HAL_SOURCES} ${IMAGE_SOURCES}
                       INCLUDE_DIRS "." "${CMAKE_CURRENT_BINARY_DIR}"
                       REQUIRES driver esp_driver_rmt esp_driver_ledc esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_i2s esp_timer lvgl
                       EMBED_FILES ${AUDIO_ASSETS})
