#ifndef BMP280_READER_H
#define BMP280_READER_H


#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_NUM_MISO 1
#define PIN_NUM_MOSI 2
#define PIN_NUM_CLK  3
#define PIN_NUM_CS   4

static const char *TAG_BMP = "BMP280";

// SPI config struct
spi_device_handle_t bmp280_spi;

// Инициализация SPI и BMP280
static void bmp280_spi_init() {
    esp_err_t ret;

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1 * 1000 * 1000,   // 1 MHz
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 1,
    };

    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_DISABLED);
    ESP_ERROR_CHECK(ret);

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &bmp280_spi);
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG_BMP, "SPI initialized");
}

// Простая функция чтения одного байта из регистра
static uint8_t bmp280_spi_read_reg(uint8_t reg) {
    uint8_t rx_data, tx_data = reg | 0x80; // Set MSB for read

    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &tx_data,
        .rx_buffer = &rx_data,
    };

    esp_err_t ret = spi_device_transmit(bmp280_spi, &t);
    ESP_ERROR_CHECK(ret);

    return rx_data;
}

// Простая функция записи одного байта в регистр
static void bmp280_spi_write_reg(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg & 0x7F, value};

    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = data,
    };

    esp_err_t ret = spi_device_transmit(bmp280_spi, &t);
    ESP_ERROR_CHECK(ret);
}


static void bmp280_init_and_read(int32_t *temperature, uint32_t *pressure) {
    bmp280_spi_init();

    uint8_t id = bmp280_spi_read_reg(0xD0);
    if (id != 0x58) {
        ESP_LOGE(TAG_BMP, "Device ID mismatch: 0x%02X", id);
        return;
    }

    bmp280_spi_write_reg(0xF4, 0b01010111); // Temp x2, Press x16, Normal mode
    bmp280_spi_write_reg(0xF5, 0b10100000); // Standby 1000ms, filter x16

    vTaskDelay(100 / portTICK_PERIOD_MS);

    uint32_t press_raw = ((uint32_t)bmp280_spi_read_reg(0xF7) << 12) |
                         ((uint32_t)bmp280_spi_read_reg(0xF8) << 4) |
                         ((uint32_t)bmp280_spi_read_reg(0xF9) >> 4);

    uint32_t temp_raw = ((uint32_t)bmp280_spi_read_reg(0xFA) << 12) |
                        ((uint32_t)bmp280_spi_read_reg(0xFB) << 4) |
                        ((uint32_t)bmp280_spi_read_reg(0xFC) >> 4);

    *temperature = (int32_t)(temp_raw / 10);  // Упрощённое преобразование
    *pressure = (uint32_t)(press_raw / 10);   // Упрощённое преобразование

    ESP_LOGI(TAG_BMP, "TEMP: %ld.%02ld°C, PRESS: %ld.%02ld hPa",
             *temperature / 100, *temperature % 100,
             *pressure / 100, *pressure % 100);
}

#endif // BMP280_READER_H
