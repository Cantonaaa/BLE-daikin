idf_component_register(
    SRCS "ble_daikin.c" "daikin_web.c"
    INCLUDE_DIRS "include"
    REQUIRES "nvs_flash" "esp_http_server" "json" "esp_wifi" "esp_timer"
)
