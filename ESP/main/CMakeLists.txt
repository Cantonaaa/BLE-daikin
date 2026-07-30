set(COMPONENT_SRCS "main.c")
set(COMPONENT_REQUIRES "ble-daikin" "voice-control" "bt" "nvs_flash" "esp_wifi" "esp_netif" "esp_event" "lwip")
register_component()
