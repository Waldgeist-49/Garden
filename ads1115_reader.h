#ifndef ADS1115_READER_H
#define ADS1115_READER_H

#include <stdint.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/task.h"

#define I2C_MASTER_SCL_IO 18
#define I2C_MASTER_SDA_IO 19
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define ADS1115_ADDR 0x48

static const char *TAG_ADS = "ADS1115";

static void init_ads1115() {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));
}

static int16_t ads1115_read_channel(uint8_t channel) {
    uint16_t config = 0x8483; // Однократное измерение, PGA ±4.096V, 128 SPS

    switch (channel) {
        case 0: config |= (0x4000); break; // AIN0
        case 1: config |= (0x5000); break; // AIN1
        case 2: config |= (0x6000); break; // AIN2
        case 3: config |= (0x7000); break; // AIN3
        default: return 0;
    }

    uint8_t config_bytes[3] = {
        0x01,
        (config >> 8) & 0xFF,
        config & 0xFF
    };

    // Отправляем конфигурацию
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADS1115_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, config_bytes, 3, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_ADS, "Failed to write config");
        return 0;
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // Ждём завершения преобразования

    // Читаем результат
    uint8_t data[2];
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADS1115_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true); // Устанавливаем указатель на регистр данных
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADS1115_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 2, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_ADS, "Failed to read data");
        return 0;
    }

    return (int16_t)((data[0] << 8) | data[1]);
}

#endif
