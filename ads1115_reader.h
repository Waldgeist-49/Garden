#ifndef ADS1115_READER_H
#define ADS1115_READER_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/task.h"

// Эти дефайны используются и в main.c для i2c_bus_init_once
#define I2C_MASTER_SCL_IO 18
#define I2C_MASTER_SDA_IO 19
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define ADS1115_ADDR 0x48

static const char *TAG_ADS = "ADS1115";

// Прототип централизованной инициализации I2C
void i2c_bus_init_once();

// ads_initialized теперь будет контролировать только инициализацию,
// специфичную для ADS1115, если таковая потребуется,
// или можно будет полностью удалить, если I2C-драйвер инициализируется в app_main.
// Для простоты, оставлю его, но он будет менее критичен.
static bool ads_initialized = false;

// Эта функция теперь просто проверяет, инициализирован ли ADS1115 (если есть какая-то специфичная для него настройка)
// Основная инициализация I2C-драйвера происходит в i2c_bus_init_once()
static void ads1115_init_if_needed() {
    if (ads_initialized) return;

    // Здесь может быть код для специфической настройки ADS1115,
    // например, установка gain или режима, если они не меняются при каждом чтении.
    // В вашем текущем коде вся конфигурация делается в ads1115_read_channel(),
    // поэтому эта функция может быть упрощена или полностью удалена,
    // если вам не нужна какая-либо разовая настройка ADS1115.
    // Для данного кода, её вызов в ads1115_read_channel() просто делает ads_initialized = true.

    ads_initialized = true;
    ESP_LOGI(TAG_ADS, "ADS1115 setup (I2C driver assumed initialized).");
}

int16_t ads1115_read_channel(uint8_t channel) {
    // Убедимся, что I2C-драйвер инициализирован
    // (функция i2c_bus_init_once() вызывается в app_main)
    ads1115_init_if_needed(); // Вызов этой функции теперь просто устанавливает флаг ads_initialized

    uint16_t config = 0x8483; // Single-shot, 16-bit, FSR +/-4.096V (по умолчанию), 128 SPS

    switch (channel) {
        case 0: config |= 0x4000; break; // AIN0/GND
        case 1: config |= 0x5000; break; // AIN1/GND
        case 2: config |= 0x6000; break; // AIN2/GND
        case 3: config |= 0x7000; break; // AIN3/GND
        default:
            ESP_LOGE(TAG_ADS, "Invalid ADS1115 channel: %d", channel);
            return 0;
    }
    // Set 'Start a single conversion' bit (OS)
    config |= 0x8000;

    uint8_t config_bytes[3] = {
        0x01, // Pointer Register to Config Register
        (config >> 8) & 0xFF,
        config & 0xFF
    };

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADS1115_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, config_bytes, 3, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_ADS, "Failed to write ADS1115 config: %s", esp_err_to_name(ret));
        return 0;
    }

    // ADS1115 требуется время для преобразования.
    // Задержка зависит от Sample Rate (SPS). Для 128 SPS это ~7.8 мс. 10 мс достаточно.
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t data[2];
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADS1115_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true); // Pointer to Conversion Register
    i2c_master_start(cmd); // Repeated start
    i2c_master_write_byte(cmd, (ADS1115_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 2, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_ADS, "Failed to read ADS1115 data: %s", esp_err_to_name(ret));
        return 0;
    }

    return (int16_t)((data[0] << 8) | data[1]);
}

#endif