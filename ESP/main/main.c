/*
 * BLE-Daikin 主程序
 * - WiFi 管理（STA + AP 配网）
 * - BLE 初始化
 * - Web 服务器启动
 * - 定时任务
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "ble_daikin.h"
#include "daikin_web.h"

static const char *TAG = "MAIN";

/* 从 NVS 读取 WiFi 凭证（SSID/密码） */
void load_wifi_creds(char *ssid, char *pass)
{
    nvs_handle_t nvs;
    ssid[0] = 0; pass[0] = 0;
    if (nvs_open("wifi", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = 32;
        nvs_get_str(nvs, "ssid", ssid, &len);
        len = 64;
        nvs_get_str(nvs, "pass", pass, &len);
        nvs_close(nvs);
    }
}

/* 保存 WiFi 凭证到 NVS */
void save_wifi_creds(const char *ssid, const char *pass)
{
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, "ssid", ssid);
        nvs_set_str(nvs, "pass", pass);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

/* 保存 WiFi 并重启（配网完成后调用） */
void save_wifi_and_restart(const char *ssid, const char *pass)
{
    save_wifi_creds(ssid, pass);
    esp_restart();
}

/* 启动 SNTP 时间同步（定时功能需要 NTP 时间） */
static void start_sntp(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started");
}

/* WiFi 事件处理 */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
    } else if (id == IP_EVENT_STA_GOT_IP) {
        // 获取到 IP 后才同步时间（AP 模式下无 SNTP）
        ESP_LOGI(TAG, "WiFi got IP");
        start_sntp();
    }
}

/* 开启 AP 热点（无 WiFi 配置时自动启动） */
static void start_ap(void)
{
    esp_netif_create_default_wifi_ap();
    wifi_config_t ap = {
        .ap = {
            .ssid = "BLE-Daikin",     // 热点名称
            .ssid_len = 10,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN, // 无密码
        },
    };
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    ESP_LOGI(TAG, "AP: BLE-Daikin");
}

/*
 * WiFi 初始化
 * - 有保存的 WiFi 凭证 → STA 模式连接
 * - 无凭证 → AP 模式（热点配网）
 */
void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();

    // 注册 WiFi 事件监听
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);  // 我们不存 WiFi 配置到 NVS

    char ssid[32], pass[64];
    load_wifi_creds(ssid, pass);

    if (ssid[0]) {
        // 有凭证 → STA 模式
        ESP_LOGI(TAG, "Connecting to %s", ssid);
        esp_wifi_set_mode(WIFI_MODE_STA);
        wifi_config_t sta = {0};
        strncpy((char*)sta.sta.ssid, ssid, 31);
        strncpy((char*)sta.sta.password, pass, 63);
        esp_wifi_set_config(WIFI_IF_STA, &sta);
        esp_wifi_start();
        esp_wifi_connect();
    } else {
        // 无凭证 → AP 模式，等待用户配网
        start_ap();
        esp_wifi_start();
    }
}

/*
 * BLE 连接维护任务（核心 1）
 * 未连接且未扫描时，每 30 秒发起一次扫描
 */
static void ble_task(void *arg)
{
    while (1) {
        if (!ble_daikin_is_connected() && !ble_daikin_is_scanning()) {
            ble_daikin_start_scan();
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/*
 * 定时检查任务（核心 0）
 * 每 30 秒检查所有分机的定时设置，到点自动开关
 */
static void timer_task(void *arg)
{
    while (1) { ble_daikin_timer_check(); vTaskDelay(pdMS_TO_TICKS(30000)); }
}

void app_main(void)
{
    // 初始化 NVS（所有持久化数据都存这里）
    nvs_flash_init();

    // 初始化 WiFi、BLE、Web 服务器
    wifi_init();
    ble_daikin_init();
    daikin_web_init();
    daikin_web_load_nvs();  // 从 NVS 恢复分机名称和定时设置

    // 启动后台任务
    xTaskCreatePinnedToCore(ble_task, "ble", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(timer_task, "tmr", 3072, NULL, 3, NULL, 0);
}
