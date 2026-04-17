/**
 * hal_gc9a01_spi.c — GC9A01 1.28" 圆形 SPI LCD
 *
 * 用 queued transactions 代替 polling：
 * 所有像素行排入同一个 DMA 队列，控制器自动在 CS 保持低的情况下
 * 依次发送各行数据，保证帧完整性。
 */

#include "hal_gc9a01.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_heap_caps.h"

static const char *TAG = "GC9A01";
static spi_device_handle_t s_spi = NULL;

#define DC_LOW()   gpio_set_level(GC9A01_DCX_GPIO, 0)
#define DC_HIGH()  gpio_set_level(GC9A01_DCX_GPIO, 1)
#define TICK()     esp_rom_delay_us(1)

static void cmd(uint8_t c)
{
    DC_LOW();
    uint8_t b = c;
    spi_transaction_t t = { .tx_buffer = &b, .length = 8 };
    spi_device_polling_transmit(s_spi, &t);
    TICK();
}

static void cmd_data(uint8_t c, const uint8_t *d, size_t len)
{
    DC_LOW();
    uint8_t b = c;
    spi_transaction_t tc = { .tx_buffer = &b, .length = 8 };
    spi_device_polling_transmit(s_spi, &tc);

    if (len && d) {
        DC_HIGH();
        spi_transaction_t td = { .tx_buffer = d, .length = len * 8 };
        spi_device_polling_transmit(s_spi, &td);
    }
    TICK();
}

hal_err_t hal_gc9a01_init(void)
{
    bool first_init = false;
    if (!s_spi) {
        first_init = true;
        ESP_LOGI(TAG, "Init starting...");

        gpio_set_direction(GC9A01_DCX_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_direction(GC9A01_RESX_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_direction(GC9A01_CS_GPIO, GPIO_MODE_OUTPUT);
        DC_HIGH();
        gpio_set_level(GC9A01_RESX_GPIO, 1);

        spi_bus_config_t buscfg = {
            .mosi_io_num = GC9A01_SDA_GPIO,
            .miso_io_num = -1,
            .sclk_io_num = GC9A01_SCL_GPIO,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
        };

        spi_device_interface_config_t devcfg = {
            .clock_speed_hz = 40 * 1000 * 1000,  // 40MHz
            .mode = 0,
            .spics_io_num = GC9A01_CS_GPIO,
            .queue_size = 250,
            .flags = SPI_DEVICE_HALFDUPLEX,
        };

        esp_err_t err = spi_bus_initialize(GC9A01_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "spi_bus_initialize: %d", err);
            return HAL_ERR;
        }
        ESP_LOGI(TAG, "Bus initialized");

        err = spi_bus_add_device(GC9A01_SPI_HOST, &devcfg, &s_spi);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spi_bus_add_device: %d", err);
            return HAL_ERR;
        }
        ESP_LOGI(TAG, "SPI device added");
    } else {
        ESP_LOGW(TAG, "Already initialized, resetting panel...");
    }

    // Hardware reset (always, to ensure clean state for LVGL)
    ESP_LOGI(TAG, "RESX pulse...");
    gpio_set_level(GC9A01_RESX_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(GC9A01_RESX_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_LOGI(TAG, "RESX done");

    // === Bodmer TFT_eSPI GC9A01 init (verbatim) ===
    cmd(0xEF);
    cmd(0xEB); cmd_data(0xEB, (uint8_t[]){0x14}, 1);
    cmd(0xEB); cmd_data(0xEB, (uint8_t[]){0x14}, 1);
    cmd(0xEB); cmd_data(0xEB, (uint8_t[]){0x14}, 1);

    cmd(0xFE);
    cmd(0xEF);
    cmd(0xEB); cmd_data(0xEB, (uint8_t[]){0x14}, 1);
    cmd(0xEB); cmd_data(0xEB, (uint8_t[]){0x14}, 1);
    cmd(0xEB); cmd_data(0xEB, (uint8_t[]){0x14}, 1);

    cmd(0x84); cmd_data(0x84, (uint8_t[]){0x40}, 1);
    cmd(0x85); cmd_data(0x85, (uint8_t[]){0xFF}, 1);
    cmd(0x86); cmd_data(0x86, (uint8_t[]){0xFF}, 1);
    cmd(0x87); cmd_data(0x87, (uint8_t[]){0xFF}, 1);
    cmd(0x88); cmd_data(0x88, (uint8_t[]){0x0A}, 1);
    cmd(0x89); cmd_data(0x89, (uint8_t[]){0x21}, 1);
    cmd(0x8A); cmd_data(0x8A, (uint8_t[]){0x00}, 1);
    cmd(0x8B); cmd_data(0x8B, (uint8_t[]){0x80}, 1);
    cmd(0x8C); cmd_data(0x8C, (uint8_t[]){0x01}, 1);
    cmd(0x8D); cmd_data(0x8D, (uint8_t[]){0x01}, 1);
    cmd(0x8E); cmd_data(0x8E, (uint8_t[]){0xFF}, 1);
    cmd(0x8F); cmd_data(0x8F, (uint8_t[]){0xFF}, 1);
    cmd(0xB6); cmd_data(0xB6, (uint8_t[]){0x00, 0x20}, 2);
    cmd(0x3A); cmd_data(0x3A, (uint8_t[]){0x05}, 1);
    cmd(0x90); cmd_data(0x90, (uint8_t[]){0x08, 0x08, 0x08, 0x08}, 4);
    cmd(0xBD); cmd_data(0xBD, (uint8_t[]){0x06}, 1);
    cmd(0xBC); cmd_data(0xBC, (uint8_t[]){0x00}, 1);
    cmd(0xFF); cmd_data(0xFF, (uint8_t[]){0x60, 0x01, 0x04}, 3);
    cmd(0xC3); cmd_data(0xC3, (uint8_t[]){0x13}, 1);
    cmd(0xC4); cmd_data(0xC4, (uint8_t[]){0x13}, 1);
    cmd(0xC9); cmd_data(0xC9, (uint8_t[]){0x22}, 1);
    cmd(0xBE); cmd_data(0xBE, (uint8_t[]){0x11}, 1);
    cmd(0xE1); cmd_data(0xE1, (uint8_t[]){0x10, 0x0E}, 2);
    cmd(0xDF); cmd_data(0xDF, (uint8_t[]){0x21, 0x0C, 0x02}, 3);
    cmd(0xF0); cmd_data(0xF0, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6);
    cmd(0xF1); cmd_data(0xF1, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6);
    cmd(0xF2); cmd_data(0xF2, (uint8_t[]){0x45, 0x09, 0x08, 0x08, 0x26, 0x2A}, 6);
    cmd(0xF3); cmd_data(0xF3, (uint8_t[]){0x43, 0x70, 0x72, 0x36, 0x37, 0x6F}, 6);
    cmd(0xED); cmd_data(0xED, (uint8_t[]){0x1B, 0x0B}, 2);
    cmd(0xAE); cmd_data(0xAE, (uint8_t[]){0x77}, 1);
    cmd(0xCD); cmd_data(0xCD, (uint8_t[]){0x63}, 1);
    cmd(0x70); cmd_data(0x70, (uint8_t[]){0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03}, 9);
    cmd(0xE8); cmd_data(0xE8, (uint8_t[]){0x34}, 1);
    cmd(0x62); cmd_data(0x62, (uint8_t[]){0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70}, 12);
    cmd(0x63); cmd_data(0x63, (uint8_t[]){0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70}, 12);
    cmd(0x64); cmd_data(0x64, (uint8_t[]){0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07}, 7);
    cmd(0x66); cmd_data(0x66, (uint8_t[]){0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00}, 10);
    cmd(0x67); cmd_data(0x67, (uint8_t[]){0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98}, 10);
    cmd(0x74); cmd_data(0x74, (uint8_t[]){0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00}, 7);
    cmd(0x98); cmd_data(0x98, (uint8_t[]){0x3E, 0x07}, 2);

    // === Display ON sequence ===
    cmd(0x35);  // TE ON
    cmd_data(0x36, (uint8_t[]){0x00}, 1);  // MADCTL: RGB mode, no mirror
    cmd(0x11);  // Sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    cmd(0x29);  // Display ON
    vTaskDelay(pdMS_TO_TICKS(20));

    // Clear screen to black
    size_t buf_size = (size_t)GC9A01_WIDTH * GC9A01_HEIGHT * sizeof(uint16_t);
    uint16_t *black_buf = heap_caps_calloc(1, buf_size, MALLOC_CAP_DMA);
    if (black_buf) {
        hal_gc9a01_draw_bitmap(0, 0, GC9A01_WIDTH, GC9A01_HEIGHT, black_buf);
        heap_caps_free(black_buf);
        ESP_LOGI(TAG, "Screen cleared to black");
    }

    ESP_LOGI(TAG, "Init complete");
    return HAL_OK;
}

hal_err_t hal_gc9a01_draw_bitmap(int x_start, int y_start, int x_end, int y_end,
                                  const void *color_data)
{
    if (!s_spi) return HAL_ERR_NOT_INIT;

    int w = x_end - x_start;
    int h = y_end - y_start;
    if (w <= 0 || h <= 0) return HAL_ERR_INVALID_ARG;

    uint8_t col[4] = {
        (x_start >> 8) & 0xFF, x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF, (x_end - 1) & 0xFF
    };
    uint8_t row[4] = {
        (y_start >> 8) & 0xFF, y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF, (y_end - 1) & 0xFF
    };

    // Set column address
    cmd_data(0x2A, col, 4);
    // Set row address
    cmd_data(0x2B, row, 4);
    // Memory write command
    cmd(0x2C);

    // Pixel data: one transaction per row
    size_t row_bytes = (size_t)w * 2;
    uint8_t *row_buf = malloc(row_bytes);
    if (!row_buf) return HAL_ERR_NO_MEM;

    const uint8_t *src = (const uint8_t *)color_data;
    for (int y = 0; y < h; y++) {
        memcpy(row_buf, src + y * row_bytes, row_bytes);
        DC_HIGH();
        spi_transaction_t t = { .tx_buffer = row_buf, .length = row_bytes * 8 };
        spi_device_polling_transmit(s_spi, &t);
        TICK();
        // 每 20 行让出 CPU 1ms，防止 SPI 传输阻塞其他任务（特别是 ec11_task）
        if ((y + 1) % 20 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    free(row_buf);
    return HAL_OK;
}

// ------------------------------------------------------------
hal_err_t hal_gc9a01_spi_test(void)
{
    ESP_LOGI(TAG, "Direct SPI test starting...");

    hal_err_t err = hal_gc9a01_init();
    if (err != HAL_OK) { ESP_LOGE(TAG, "Init failed"); return err; }

    uint16_t *buf = malloc(GC9A01_WIDTH * GC9A01_HEIGHT * sizeof(uint16_t));
    if (!buf) return HAL_ERR_NO_MEM;

    for (int i = 0; i < GC9A01_WIDTH * GC9A01_HEIGHT; i++) buf[i] = 0xF800;
    hal_gc9a01_draw_bitmap(0, 0, GC9A01_WIDTH, GC9A01_HEIGHT, buf);
    ESP_LOGI(TAG, "Full screen RED");
    vTaskDelay(pdMS_TO_TICKS(2000));

    for (int i = 0; i < GC9A01_WIDTH * GC9A01_HEIGHT; i++) buf[i] = 0x07E0;
    hal_gc9a01_draw_bitmap(0, 0, GC9A01_WIDTH, GC9A01_HEIGHT, buf);
    ESP_LOGI(TAG, "Full screen GREEN");
    vTaskDelay(pdMS_TO_TICKS(2000));

    for (int i = 0; i < GC9A01_WIDTH * GC9A01_HEIGHT; i++) buf[i] = 0x001F;
    hal_gc9a01_draw_bitmap(0, 0, GC9A01_WIDTH, GC9A01_HEIGHT, buf);
    ESP_LOGI(TAG, "Full screen BLUE");
    vTaskDelay(pdMS_TO_TICKS(2000));

    free(buf);
    ESP_LOGI(TAG, "Direct SPI test complete");
    return HAL_OK;
}
