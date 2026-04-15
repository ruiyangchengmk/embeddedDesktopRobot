#include "hal_lcd1602.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "stdarg.h"
#include "stdio.h"

#define I2C_SDA_GPIO        8
#define I2C_SCL_GPIO        9
#define I2C_PORT            I2C_NUM_0
#define I2C_FREQ_HZ         5000   // 5kHz，最保守速度

#define PCF8574_ADDR        0x27   // 直接指定，不探测

// ============================================
// PCF8574 -> LCD1602 引脚映射
// MAPPING=0: 标准映射 (P0=RS, P1=RW, P2=EN, P3=BL, P4=D4, P5=D5, P6=D6, P7=D7)
// MAPPING=1: 反向映射 (用于某些廉价背板)
#define LCD_I2C_MAPPING     1
// ============================================

#if LCD_I2C_MAPPING == 0
    // 标准映射: RS=BIT0, RW=BIT1, EN=BIT2, BL=BIT3, D4-D7=BIT4-7
    #define LCD_DB4       0x10
    #define LCD_DB5       0x20
    #define LCD_DB6       0x40
    #define LCD_DB7       0x80
    #define LCD_RS         0x01
    #define LCD_RW         0x02
    #define LCD_EN         0x04
    #define LCD_BL         0x08
#elif LCD_I2C_MAPPING == 1
    // 反向映射: D7=BIT7, D6=BIT6, D5=BIT5, D4=BIT4, EN=BIT5, RW=BIT6, RS=BIT7
    #define LCD_DB4       0x01
    #define LCD_DB5       0x02
    #define LCD_DB6       0x04
    #define LCD_DB7       0x08
    #define LCD_RS         0x80
    #define LCD_RW         0x40
    #define LCD_EN         0x20
    #define LCD_BL         0x10
#endif

static const char *TAG = "HAL_LCD1602";

static i2c_master_bus_handle_t s_bus_handle = NULL;
static i2c_master_dev_handle_t s_dev_handle = NULL;
static uint8_t s_backlight_val = LCD_BL;
static bool s_initialized = false;

static esp_err_t write_byte(uint8_t data)
{
    if (!s_dev_handle) return ESP_FAIL;
    esp_err_t err = i2c_master_transmit(s_dev_handle, &data, 1, 1000);
    if (err != ESP_OK) {
        // 重试一次
        esp_rom_delay_us(500);
        err = i2c_master_transmit(s_dev_handle, &data, 1, 1000);
    }
    return err;
}

static void lcd_strobe_en(uint8_t cur_data)
{
    // EN 脉冲：LCD 在 EN 下降沿采样数据
    write_byte(cur_data | LCD_EN);  // EN 高，数据不变
    esp_rom_delay_us(1);
    write_byte(cur_data & ~LCD_EN); // EN 低，数据不变（LCD 在此采样）
    esp_rom_delay_us(100);          // LCD 处理时间
}

static void lcd_write_nibble(uint8_t nibble)
{
    uint8_t data = s_backlight_val;
    // 构造 4-bit 数据
    if (nibble & 0x01) data |= LCD_DB4;
    if (nibble & 0x02) data |= LCD_DB5;
    if (nibble & 0x04) data |= LCD_DB6;
    if (nibble & 0x08) data |= LCD_DB7;
    // 预置 EN=0，然后 EN 脉冲
    write_byte(data);
    lcd_strobe_en(data);
}

static void lcd_write_cmd(uint8_t cmd)
{
    // 高4位
    uint8_t hi_data = s_backlight_val | ((cmd >> 4) & 0x0F);
    write_byte(hi_data);
    lcd_strobe_en(hi_data);

    // 低4位
    uint8_t lo_data = s_backlight_val | (cmd & 0x0F);
    write_byte(lo_data);
    lcd_strobe_en(lo_data);

    if (cmd == 0x01 || cmd == 0x02) {
        vTaskDelay(pdMS_TO_TICKS(5));
    } else {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void lcd_write_data(uint8_t data)
{
    // 高4位 + RS=1
    uint8_t hi_data = s_backlight_val | LCD_RS | ((data >> 4) & 0x0F);
    write_byte(hi_data);
    lcd_strobe_en(hi_data);

    // 低4位 + RS=1
    uint8_t lo_data = s_backlight_val | LCD_RS | (data & 0x0F);
    write_byte(lo_data);
    lcd_strobe_en(lo_data);

    vTaskDelay(pdMS_TO_TICKS(2));
}

hal_err_t hal_lcd1602_init(void)
{
    if (s_initialized) return HAL_OK;

    // ========== 1. 创建 I2C 总线 ==========
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    if (i2c_new_master_bus(&bus_cfg, &s_bus_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus");
        return HAL_ERR;
    }

    // ========== 2. 添加设备，不探测直接用 0x27 ==========
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF8574_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    if (i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &s_dev_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device at 0x%02X", PCF8574_ADDR);
        i2c_del_master_bus(s_bus_handle);
        s_bus_handle = NULL;
        return HAL_ERR;
    }
    ESP_LOGI(TAG, "I2C device added at 0x%02X", PCF8574_ADDR);

    // 上电稳定时间
    vTaskDelay(pdMS_TO_TICKS(200));

    // ========== 3. HD44780 初始化 ==========
    vTaskDelay(pdMS_TO_TICKS(50));
    vTaskDelay(pdMS_TO_TICKS(10));

    // 第一次写：0x03 (8-bit 模式唤醒)
    lcd_write_nibble(0x03);
    vTaskDelay(pdMS_TO_TICKS(5));

    // 第二次写：0x03
    lcd_write_nibble(0x03);
    vTaskDelay(pdMS_TO_TICKS(1));

    // 第三次写：0x03
    lcd_write_nibble(0x03);
    vTaskDelay(pdMS_TO_TICKS(1));

    // 第四次写：0x02 -> 切换到 4-bit 模式
    lcd_write_nibble(0x02);
    vTaskDelay(pdMS_TO_TICKS(1));

    // 正式命令
    lcd_write_cmd(0x28);  // 4-bit, 2行, 5x8字体
    lcd_write_cmd(0x08);  // 显示关
    lcd_write_cmd(0x01);  // 清屏
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_cmd(0x06);  // 输入模式：增地址，不移屏
    lcd_write_cmd(0x0C);  // 显示开，光标关，闪烁关

    s_initialized = true;
    ESP_LOGI(TAG, "LCD1602 init OK (addr=0x%02X, mapping=%d)", PCF8574_ADDR, LCD_I2C_MAPPING);
    return HAL_OK;
}

hal_err_t hal_lcd1602_clear(void)
{
    if (!s_initialized) return HAL_ERR_NOT_INIT;
    lcd_write_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(5));
    return HAL_OK;
}

hal_err_t hal_lcd1602_set_cursor(uint8_t row, uint8_t col)
{
    if (!s_initialized) return HAL_ERR_NOT_INIT;
    if (row > 1) row = 1;
    if (col > 15) col = 15;
    uint8_t addr = col + (row == 0 ? 0x00 : 0x40);
    lcd_write_cmd(0x80 | addr);
    return HAL_OK;
}

hal_err_t hal_lcd1602_print(const char *str)
{
    if (!s_initialized) return HAL_ERR_NOT_INIT;
    while (*str) {
        lcd_write_data((uint8_t)*str++);
    }
    return HAL_OK;
}

hal_err_t hal_lcd1602_printf_row0(const char *fmt, ...)
{
    if (!s_initialized) return HAL_ERR_NOT_INIT;
    char buf[17] = {0};
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    hal_lcd1602_set_cursor(0, 0);
    hal_lcd1602_print(buf);
    return HAL_OK;
}

hal_err_t hal_lcd1602_printf_row1(const char *fmt, ...)
{
    if (!s_initialized) return HAL_ERR_NOT_INIT;
    char buf[17] = {0};
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    hal_lcd1602_set_cursor(1, 0);
    hal_lcd1602_print(buf);
    return HAL_OK;
}

hal_err_t hal_lcd1602_backlight(bool on)
{
    if (!s_initialized) return HAL_ERR_NOT_INIT;
    s_backlight_val = on ? LCD_BL : 0x00;
    write_byte(s_backlight_val);
    return HAL_OK;
}
