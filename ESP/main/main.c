#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"
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

static void sntp_init_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP time synced");
}

static void start_sntp(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(sntp_init_cb);
    esp_sntp_init();
}

static void ble_task(void *arg)
{
    while (1) {
        if (!ble_daikin_is_connected()) {
            ESP_LOGI(TAG, "Connecting BLE...");
            ble_daikin_connect();
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

static void timer_task(void *arg)
{
    while (1) {
        ble_daikin_timer_check();
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void app_main(void)
{
    nvs_flash_init();
    wifi_init();
    start_sntp();
    ble_daikin_init();
    daikin_web_init();

    xTaskCreatePinnedToCore(ble_task, "ble", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(timer_task, "tmr", 3072, NULL, 3, NULL, 0);
}
