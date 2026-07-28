idf_component_register(
    SRCS "ble_daikin.c" "daikin_web.c"
    INCLUDE_DIRS "include"
    REQUIRES "bt" "nvs_flash" "esp_http_server" "json"
)
