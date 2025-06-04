#ifndef BMP280_READER_H
#define BMP280_READER_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h> // Для fmodf, если используется

#define PIN_NUM_MISO 1
#define PIN_NUM_MOSI 2
#define PIN_NUM_CLK  3
#define PIN_NUM_CS   4

static const char *TAG_BMP = "BMP280";

// SPI config struct
spi_device_handle_t bmp280_spi;

// Структура для хранения калибровочных параметров BMP280
typedef struct {
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;

    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;
    int32_t t_fine; // Переменная, используемая в компенсации
} bmp280_calib_param_t;

static bmp280_calib_param_t bmp280_calib;
static bool bmp280_initialized = false; // Флаг инициализации датчика

// Простая функция чтения одного байта из регистра
static uint8_t bmp280_spi_read_reg(uint8_t reg) {
    uint8_t rx_data;
    // Для чтения MSB (бит 7) должен быть установлен в 1.
    // Адрес регистра передается как 0x80 | reg_addr
    uint8_t tx_data = reg | 0x80;

    spi_transaction_t t = {
        .length = 8,        // Длина транзакции в битах (1 байт)
        .tx_buffer = &tx_data, // Буфер для отправки
        .rx_buffer = &rx_data, // Буфер для приема
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA, // Указываем, что используем внутренние буферы
    };

    esp_err_t ret = spi_device_transmit(bmp280_spi, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_BMP, "Failed to read BMP280 register 0x%02X: %s", reg, esp_err_to_name(ret));
        return 0;
    }
    return rx_data;
}

// Простая функция записи одного байта в регистр
static void bmp280_spi_write_reg(uint8_t reg, uint8_t value) {
    // Для записи MSB (бит 7) должен быть установлен в 0.
    uint8_t tx_data[2] = {reg & 0x7F, value};

    spi_transaction_t t = {
        .length = 16,       // Длина транзакции в битах (2 байта: адрес + данные)
        .tx_buffer = tx_data, // Буфер для отправки
    };

    esp_err_t ret = spi_device_transmit(bmp280_spi, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_BMP, "Failed to write BMP280 register 0x%02X with 0x%02X: %s", reg, value, esp_err_to_name(ret));
    }
}

// Функция для чтения 16-битного регистра
static uint16_t bmp280_read_reg16(uint8_t reg_addr) {
    uint8_t msb = bmp280_spi_read_reg(reg_addr);
    uint8_t lsb = bmp280_spi_read_reg(reg_addr + 1);
    return (uint16_t)((msb << 8) | lsb);
}

// Функция для чтения калибровочных параметров
static void bmp280_read_calibration_params() {
    bmp280_calib.dig_T1 = bmp280_read_reg16(0x88);
    bmp280_calib.dig_T2 = (int16_t)bmp280_read_reg16(0x8A);
    bmp280_calib.dig_T3 = (int16_t)bmp280_read_reg16(0x8C);

    bmp280_calib.dig_P1 = bmp280_read_reg16(0x8E);
    bmp280_calib.dig_P2 = (int16_t)bmp280_read_reg16(0x90);
    bmp280_calib.dig_P3 = (int16_t)bmp280_read_reg16(0x92);
    bmp280_calib.dig_P4 = (int16_t)bmp280_read_reg16(0x94);
    bmp280_calib.dig_P5 = (int16_t)bmp280_read_reg16(0x96);
    bmp280_calib.dig_P6 = (int16_t)bmp280_read_reg16(0x98);
    bmp280_calib.dig_P7 = (int16_t)bmp280_read_reg16(0x9A);
    bmp280_calib.dig_P8 = (int16_t)bmp280_read_reg16(0x9C);
    bmp280_calib.dig_P9 = (int16_t)bmp280_read_reg16(0x9E);

    ESP_LOGI(TAG_BMP, "Calibration parameters read.");
    // Можете добавить вывод для отладки:
    // ESP_LOGI(TAG_BMP, "dig_T1: %u, dig_T2: %d, dig_T3: %d", bmp280_calib.dig_T1, bmp280_calib.dig_T2, bmp280_calib.dig_T3);
    // ESP_LOGI(TAG_BMP, "dig_P1: %u, ..., dig_P9: %d", bmp280_calib.dig_P1, bmp280_calib.dig_P9);
}


// Инициализация SPI и BMP280 (вызывается один раз)
static void bmp280_init() {
    if (bmp280_initialized) return;

    esp_err_t ret;

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0, // 0 = использовать размер по умолчанию
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1 * 1000 * 1000,   // 1 MHz
        .mode = 0,                           // SPI mode 0
        .spics_io_num = PIN_NUM_CS,          // CS pin
        .queue_size = 1,                     // Only one transaction in queue at a time
        .command_bits = 0,                   // Нет командных битов (адрес передается в tx_buffer)
        .address_bits = 0,                   // Нет адресных битов
        .dummy_bits = 0,
    };

    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_DISABLED);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_BMP, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK(ret); // Остановить выполнение, если шина не инициализирована
    }


    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &bmp280_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_BMP, "spi_bus_add_device failed: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK(ret); // Остановить выполнение
    }

    ESP_LOGI(TAG_BMP, "SPI initialized");

    uint8_t id = bmp280_spi_read_reg(0xD0); // Read ID register
    if (id != 0x58) {
        ESP_LOGE(TAG_BMP, "BMP280 Device ID mismatch: 0x%02X (expected 0x58). Check wiring and power.", id);
        bmp280_initialized = false; // Установка флага в false, если инициализация не удалась
        return;
    }
    ESP_LOGI(TAG_BMP, "BMP280 device ID: 0x%02X", id);

    // Сброс датчика
    bmp280_spi_write_reg(0xE0, 0xB6); // Reset command
    vTaskDelay(20 / portTICK_PERIOD_MS); // Wait for reset to complete

    // Чтение калибровочных параметров
    bmp280_read_calibration_params();

    // Конфигурация датчика
    bmp280_spi_write_reg(0xF4, 0b01010111); // ctrl_meas: temp_oversample x2, press_oversample x16, Normal mode
    // Standby time 1000ms, filter x16, SPI 4-wire
    bmp280_spi_write_reg(0xF5, 0b10100000); // config: t_sb=1000ms, filter=x16, spi3w_en=0

    bmp280_initialized = true;
    ESP_LOGI(TAG_BMP, "BMP280 initialized and configured.");
}


// === Компенсация температуры (из даташита BMP280) ===
// Возвращает температуру в градусах Цельсия, масштабированную на 100
// Например, 2578 означает 25.78 °C
static int32_t compensate_temperature_int32(int32_t uncomp_temp) {
    int32_t var1, var2;

    var1 = ((((uncomp_temp >> 3) - ((int32_t)bmp280_calib.dig_T1 << 1))) * ((int32_t)bmp280_calib.dig_T2)) >> 11;
    var2 = (((((uncomp_temp >> 4) - ((int32_t)bmp280_calib.dig_T1)) *
              ((uncomp_temp >> 4) - ((int32_t)bmp280_calib.dig_T1))) >> 12) *
            ((int32_t)bmp280_calib.dig_T3)) >> 14;

    bmp280_calib.t_fine = var1 + var2;
    return (bmp280_calib.t_fine * 5 + 128) >> 8;
}

// === Компенсация давления (из даташита BMP280) ===
// Возвращает давление в Паскалях, масштабированное на 100 (например, 9632500 означает 963.25 hPa)
static uint32_t compensate_pressure_int32(int32_t uncomp_press) {
    int32_t var1, var2;
    uint32_t p;

    var1 = (((int32_t)bmp280_calib.t_fine) >> 1) - 64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * ((int32_t)bmp280_calib.dig_P6);
    var2 = var2 + ((var1 * ((int32_t)bmp280_calib.dig_P5)) << 1);
    var2 = (var2 >> 2) + (((int32_t)bmp280_calib.dig_P4) << 16);
    var1 = (((bmp280_calib.dig_P3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) +
            (((int32_t)bmp280_calib.dig_P2) * var1)) >> 18;
    var1 = (((((var1 * ((int32_t)bmp280_calib.dig_P9)) >> 13) * (var1 >> 3)) >> 25) +
            (((int32_t)bmp280_calib.dig_P8) * var1)) >> 19;
    var1 = ((var1 + (((int32_t)bmp280_calib.dig_P7) << 1)) >> 4);

    if (var1 == 0) {
        return 0; // Avoid division by zero
    }
    p = (((uint32_t)(((int32_t)1048576) - uncomp_press) - (var2 >> 12))) * 3125;
    if (p < 0x80000000) {
        p = (p << 1) / ((uint32_t)var1);
    } else {
        p = (p / (uint32_t)var1) * 2;
    }
    var1 = (((int32_t)bmp280_calib.dig_P9) * ((int32_t)(((p >> 3) * (p >> 3)) >> 13))) >> 3;
    var2 = (((int32_t)(p >> 2)) * ((int32_t)bmp280_calib.dig_P8)) >> 15;
    p = (uint32_t)((int32_t)p + ((var1 + var2 + bmp280_calib.dig_P7) >> 4));

    return p; // Pressure in Pa
}


// Новая функция для инициализации и чтения компенсированных данных BMP280
void bmp280_read_compensated_data(int32_t *temperature, uint32_t *pressure) {
    bmp280_init(); // Убедимся, что датчик инициализирован

    if (!bmp280_initialized) {
        ESP_LOGE(TAG_BMP, "BMP280 not initialized. Cannot read data.");
        *temperature = 0;
        *pressure = 0;
        return;
    }

    // Дождемся завершения преобразования
    // В Normal mode преобразования происходят автоматически с заданным standby.
    // Если бы был Single-shot mode, нужно было бы ждать установки бита.
    vTaskDelay(100 / portTICK_PERIOD_MS); // Даем время для одного цикла измерения

    // Чтение сырых данных температуры и давления
    // BMP280 выдает 20-битные значения, поэтому объединяем 3 байта
    int32_t uncomp_press = ((int32_t)bmp280_spi_read_reg(0xF7) << 16) |
                           ((int32_t)bmp280_spi_read_reg(0xF8) << 8) |
                           ((int32_t)bmp280_spi_read_reg(0xF9));
    uncomp_press >>= 4; // Сырые данные давления имеют 16 бит + 4 младших бита

    int32_t uncomp_temp = ((int32_t)bmp280_spi_read_reg(0xFA) << 16) |
                          ((int32_t)bmp280_spi_read_reg(0xFB) << 8) |
                          ((int32_t)bmp280_spi_read_reg(0xFC));
    uncomp_temp >>= 4; // Сырые данные температуры имеют 16 бит + 4 младших бита

    // Компенсация
    *temperature = compensate_temperature_int32(uncomp_temp);
    *pressure = compensate_pressure_int32(uncomp_press);

    // Логирование (для отладки)
    // Температура теперь масштабирована на 100 (25.78 -> 2578), давление в Паскалях.
    ESP_LOGI(TAG_BMP, "Uncomp. Temp: %ld, Uncomp. Press: %ld", (long int)uncomp_temp, (long int)uncomp_press);
    ESP_LOGI(TAG_BMP, "Comp. TEMP: %ld (%ld.%02ld C), Comp. PRESS: %lu (%lu.%02lu hPa)",
             (long int)*temperature, (long int)*temperature / 100, (long int)abs(*temperature % 100),
             (unsigned long)*pressure, (unsigned long)*pressure / 100, (unsigned long)(*pressure % 100)); // Делим на 100 для hPa, но исходно в Паскалях

    // Перевод давления из Паскалей в гектоПаскали (hPa)
    // 1 hPa = 100 Pa
    // Если вам нужны hPa, делите *pressure на 100, но тогда учтите это при отображении.
    // Сейчас *pressure - это Паскали, поэтому для вывода в hPa / 100.0, как в main.c.
}

#endif // BMP280_READER_H