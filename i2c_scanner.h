#ifndef I2C_SCANNER_H
#define I2C_SCANNER_H

#include "driver/i2c.h"
#include "esp_log.h" // Для ESP_LOGE
#include "ads1115_reader.h" // Для использования дефайнов I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO

// Эти дефайны дублировали те, что в ads1115_reader.h.
// Лучше использовать те, что уже определены для I2C-мастера.
// #define I2C_PORT I2C_NUM_0 // Использовать I2C_MASTER_NUM
// #define SDA_PIN 19         // Использовать I2C_MASTER_SDA_IO
// #define SCL_PIN 18         // Использовать I2C_MASTER_SCL_IO

// Прототип централизованной инициализации I2C
void i2c_bus_init_once();

void scan_i2c_bus(char *out, size_t maxlen) {
    // Убедимся, что I2C-шины инициализированы перед сканированием
    i2c_bus_init_once();

    char *ptr = out;
    size_t len = 0;

    len += snprintf(ptr + len, maxlen - len, "[");
    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        // Запрос на запись по адресу
        esp_err_t ret = i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        if (ret != ESP_OK) { // Проверка на случай ошибки записи байта (например, неверный адрес)
            i2c_cmd_link_delete(cmd);
            ESP_LOGW("I2C_SCAN", "Error preparing write for address 0x%02X: %s", addr, esp_err_to_name(ret));
            continue;
        }
        i2c_master_stop(cmd);
        // Выполнить команду и подождать 100 тиков
        ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd); // Обязательно удалить команду

        if (ret == ESP_OK) {
            // Успешный ответ, устройство найдено
            len += snprintf(ptr + len, maxlen - len, "\"0x%02X\",", addr);
        } else if (ret != ESP_ERR_TIMEOUT) {
            // Если не ESP_OK и не таймаут, значит какая-то другая ошибка связи
            ESP_LOGD("I2C_SCAN", "Address 0x%02X responded with error: %s", addr, esp_err_to_name(ret));
        }
    }
    if (len > 1 && ptr[len - 1] == ',') { // удалить последнюю запятую, если есть
        len--;
    }
    snprintf(ptr + len, maxlen - len, "]");
}

#endif // I2C_SCANNER_H