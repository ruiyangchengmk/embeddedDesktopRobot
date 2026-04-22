# 当前为本地模式：只编译 hal/ 和 main.c
# 如需启用 WiFi + MQTT，改为：file(GLOB_RECURSE SOURCES "*.c")
#
# 注意：gen_config.py 由 flash.sh 在编译前调用，生成 main/app_config.h
# app_config.h 由 main.c include，无需在此 CMakeLists 中处理

file(GLOB HAL_SOURCES "hal/*.c")
file(GLOB IMAGE_SOURCES "control/images/*.c")

idf_component_register(SRCS main.c event_broker.c lvgl_clock.c
                           ${HAL_SOURCES} ${IMAGE_SOURCES}
                       INCLUDE_DIRS "." "${CMAKE_CURRENT_BINARY_DIR}"
                       REQUIRES driver esp_driver_rmt esp_driver_ledc esp_driver_gpio esp_driver_i2c esp_driver_spi esp_timer lvgl)
