#ifndef SENSORS_H
#define SENSORS_H

#include <stdio.h>
#include "driver/adc.h"

#define MAX_SENSORS 4

// Структура для описания датчика
typedef struct {
    int gpio;
    adc1_channel_t adc_channel;
    int value;
} MoistureSensor;

MoistureSensor sensors[MAX_SENSORS];

// Инициализация всех датчиков
void init_sensors() {
    // Подключи нужные пины, пример:
    sensors[0].gpio = 0;
    sensors[0].adc_channel = ADC1_CHANNEL_0;

    sensors[1].gpio = 1;
    sensors[1].adc_channel = ADC1_CHANNEL_1;

    // Добавь при необходимости больше датчиков

    for (int i = 0; i < MAX_SENSORS; i++) {
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(sensors[i].adc_channel, ADC_ATTEN_DB_11);
    }
}

// Чтение всех значений
void update_sensor_values() {
    for (int i = 0; i < MAX_SENSORS; i++) {
        sensors[i].value = adc1_get_raw(sensors[i].adc_channel);
    }
}

// Генерация строки с результатами
void get_sensor_data_string(char *out, size_t max_len) {
    update_sensor_values();
    snprintf(out, max_len,
        "Sensor 1: %d\nSensor 2: %d\n",
        sensors[0].value, sensors[1].value
    );
}

#endif // SENSORS_H
