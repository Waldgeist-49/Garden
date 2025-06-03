idf_component_register(
        SRCS "main.c"
        INCLUDE_DIRS "."
        REQUIRES
        nvs_flash
        esp_http_server
        esp_netif
        esp_wifi
        driver
)
