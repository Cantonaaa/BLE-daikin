idf_component_register(
    SRCS "ble_daikin.c" "daikin_web.c"
    INCLUDE_DIRS "include"
    REQUIRES "nvs_flash" "bt" "esp_http_server" "json" "esp_wifi" "esp_timer"
)

target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--whole-archive" "bt" "-Wl,--no-whole-archive")
