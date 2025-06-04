#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "bmp280_reader.h"
#include "ads1115_reader.h"
#include "i2c_scanner.h"
#include "esp_http_server.h"
#include "wifi_connect.h"

// Добавим прототип для i2c_init, чтобы инициализировать I2C централизованно
// Это необходимо, чтобы i2c_scanner мог работать, если ads1115_init_if_needed() еще не вызван
void i2c_bus_init_once();

static const char *TAG_TIME = "NTP_TIME";
static const char *TAG_MAIN = "MAIN_APP";

// === Ожидание синхронизации времени ===
void wait_for_time_sync() {
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;

    while (timeinfo.tm_year < (2020 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG_TIME, "Waiting for system time to be set... (%d)", retry);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
}

// === Получение времени через SNTP ===
void obtain_time() {
    ESP_LOGI(TAG_TIME, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    wait_for_time_sync();

    // Установка часового пояса (например, для Киева/Одессы)
    // Проверьте правильность строки "EET-2EEST,M3.5.0/3,M10.5.0/4" для вашего региона.
    // Если вам нужен только UTC, можно опустить эти строки.
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    ESP_LOGI(TAG_TIME, "Current time: %s", asctime(&timeinfo));
}

// === Обработчик HTTP-запроса к /time ===
esp_err_t time_get_handler(httpd_req_t *req) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);

    httpd_resp_send(req, time_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// === Обработчик HTTP-запроса к /i2c_scan ===
esp_err_t i2c_scan_handler(httpd_req_t *req) {
    char result[256];
    // Убедимся, что I2C-шина инициализирована перед сканированием
    // Хотя ads1115_init_if_needed() вызывается в ads1115_read_channel(),
    // для сканера I2C лучше иметь уверенность, что драйвер запущен.
    // В данном случае, i2c_bus_init_once() будет вызвана в app_main.
    scan_i2c_bus(result, sizeof(result));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, result, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// === Обработчик HTTP-запроса к /sensors ===
esp_err_t sensor_handler(httpd_req_t *req) {
    // Внимание: ads1115_init_if_needed() будет вызвана внутри ads1115_read_channel().
    // Если I2C уже инициализирован в app_main, она просто вернется.
    int16_t ch0 = ads1115_read_channel(0);
    int16_t ch1 = ads1115_read_channel(1);

    int32_t temperature_comp;
    uint32_t pressure_comp;
    // Используем новую функцию для чтения BMP280, которая включает компенсацию
    bmp280_read_compensated_data(&temperature_comp, &pressure_comp);

    char response[200];
    // Делим на 100.0, так как функции чтения BMP280 будут возвращать значения с двумя знаками после запятой
    snprintf(response, sizeof(response),
             "{\"A0\": %d, \"A1\": %d, \"temp\": %.2f, \"press\": %.2f}",
             ch0, ch1, (float)temperature_comp / 100.0, (float)pressure_comp / 100.0);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// === Запуск Web-сервера ===
void start_web_server() {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 4096; // Увеличить размер стека для HTTPD, если возникают проблемы

    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI("HTTP", "Server started on port %d", config.server_port); // Исправлено: config.server_port вместо config.uri_match_fn

        httpd_uri_t time_uri = {
            .uri       = "/time",
            .method    = HTTP_GET,
            .handler   = time_get_handler,
            .user_ctx  = NULL
        };
        httpd_uri_t sensors_uri = {
            .uri       = "/sensors",
            .method    = HTTP_GET,
            .handler   = sensor_handler,
            .user_ctx  = NULL
        };
        httpd_uri_t i2c_scan_uri = {
            .uri       = "/i2c_scan",
            .method    = HTTP_GET,
            .handler   = i2c_scan_handler,
            .user_ctx  = NULL
        };

        httpd_register_uri_handler(server, &i2c_scan_uri);
        httpd_register_uri_handler(server, &time_uri);
        httpd_register_uri_handler(server, &sensors_uri);
    } else {
        ESP_LOGE("HTTP", "Failed to start server!");
    }
}

// === Централизованная инициализация I2C-шины ===
// Это чтобы i2c_scanner мог работать независимо от ads1115_reader
static bool i2c_master_initialized = false;
void i2c_bus_init_once() {
    if (i2c_master_initialized) {
        return;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO, // Используем дефайны из ads1115_reader.h
        .scl_io_num = I2C_MASTER_SCL_IO, // Используем дефайны из ads1115_reader.h
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ, // Используем дефайны из ads1115_reader.h
    };

    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf); // Используем дефайны из ads1115_reader.h
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG_MAIN, "i2c_param_config failed: %s", esp_err_to_name(err));
        return;
    }

    err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG_MAIN, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return;
    }
    i2c_master_initialized = true;
    ESP_LOGI(TAG_MAIN, "I2C master driver initialized.");
}


// === Точка входа ===
void app_main(void) {
    // 1. Инициализация NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Инициализация I2C-шины (для ADS1115 и сканера)
    i2c_bus_init_once(); // Вызываем здесь, чтобы I2C был готов для сканера

    // 3. Инициализация Wi-Fi
    wifi_init_sta();

    // 4. Получение времени через SNTP
    obtain_time();

    // 5. Запуск веб-сервера
    start_web_server();

    // Основной цикл приложения (если нужны какие-то периодические действия, кроме веб-сервера)
    // Веб-сервер и другие задачи FreeRTOS будут работать в фоновом режиме.
    while (1) {
        // Пример: Вывод данных ADS1115 в консоль каждые 5 секунд
        // Если вам не нужен постоянный вывод в консоль, можно удалить или изменить эту часть.
        int16_t val0 = ads1115_read_channel(0);
        int16_t val1 = ads1115_read_channel(1);
        printf("CH0: %d | CH1: %d\n", val0, val1);

        // Пример: Вывод данных BMP280 в консоль
        int32_t temperature_console;
        uint32_t pressure_console;
        bmp280_read_compensated_data(&temperature_console, &pressure_console);
        printf("TEMP: %.2f C | PRESS: %.2f hPa\n", (float)temperature_console / 100.0, (float)pressure_console / 100.0);

        vTaskDelay(pdMS_TO_TICKS(5000)); // Задержка 5 секунд
    }
}