#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "bmp280_reader.h"
#include "ads1115_reader.h"
#include "esp_http_server.h"
#include "wifi_connect.h"     // ✅ Подключаем твой Wi-Fi модуль

static const char *TAG_TIME = "NTP_TIME";


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

    // Устанавливаем временную зону для Украины
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    ESP_LOGI(TAG_TIME, "Current time: %s", asctime(&timeinfo));
}

void get_sensor_data_string(char *out, size_t maxlen) {
    float temp = bmp280_read_temperature();
    float pressure = bmp280_read_pressure();
    float soil0 = ads1115_read_channel(0);
    float soil1 = ads1115_read_channel(1);

    snprintf(out, maxlen,
             "{\"temperature\": %.2f, \"pressure\": %.2f, \"soil0\": %.2f, \"soil1\": %.2f}",
             temp, pressure, soil0, soil1);
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

esp_err_t sensor_handler(httpd_req_t *req) {
    int16_t ch0 = ads1115_read_channel(0);
    int16_t ch1 = ads1115_read_channel(1);

    int32_t temperature;
    uint32_t pressure;
    bmp280_init_and_read(&temperature, &pressure);

    char response[200];
    snprintf(response, sizeof(response),
             "{\"A0\": %d, \"A1\": %d, \"temp\": %.2f, \"press\": %.2f}",
             ch0, ch1, temperature / 100.0, pressure / 100.0);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}



esp_err_t sensors_get_handler(httpd_req_t *req) {
    char sensor_data[128];
    get_sensor_data_string(sensor_data, sizeof(sensor_data));
    httpd_resp_send(req, sensor_data, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// === Запуск Web-сервера ===
void start_web_server() {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_start(&server, &config);


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
    httpd_register_uri_handler(server, &sensors_uri);
    httpd_register_uri_handler(server, &time_uri);
}



// === Точка входа ===
void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init_sta();        // Подключаемся к Wi-Fi
    obtain_time();          // Получаем время через NTP
    init_ads1115();
    start_web_server();     // Запускаем веб-сервер
    int32_t temp;
    uint32_t pres;
    bmp280_init_and_read(&temp, &pres);
}
