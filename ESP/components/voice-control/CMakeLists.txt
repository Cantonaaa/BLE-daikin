idf_component_register(
    SRCS "voice_control.c" "pinyin.c"
    INCLUDE_DIRS "include"
    REQUIRES "esp-sr" "esp_driver_i2s"
)
