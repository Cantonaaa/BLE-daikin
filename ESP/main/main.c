#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "ble_daikin.h"
#include "daikin_web.h"

static const char *TAG = "MAIN";

static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();
}

static void discover_units(void)
{
    unit_count = 4;
    strcpy(units[0].name, "Living Room");
    units[0].id = 1;
    units[0].on = false;
    strcpy(units[1].name, "Master Bed");
    units[1].id = 2;
    units[1].on = false;
    strcpy(units[2].name, "Bedroom 2");
    units[2].id = 3;
    units[2].on = false;
    strcpy(units[3].name, "Study");
    units[3].id = 4;
    units[3].on = false;
    ESP_LOGI(TAG, "Pre-configured %d units", unit_count);
}

void app_main(void)
{
    nvs_flash_init();
    wifi_init();
    discover_units();
    ble_daikin_init();
    daikin_web_init();

    xTaskCreatePinnedToCore([](void*) {
        while (1) {
            if (!ble_daikin_is_connected()) {
                ESP_LOGI(TAG, "Attempting BLE connection...");
                ble_daikin_connect();
            }
            vTaskDelay(pdMS_TO_TICKS(30000));
        }
    }, "ble_task", 4096, NULL, 5, NULL, 1);
}
