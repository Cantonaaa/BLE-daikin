/*
 * BLE-Daikin 主程序
 * - WiFi 管理（STA + AP 配网 + DNS 劫持）
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
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "ble_daikin.h"
#include "daikin_web.h"
#include "voice_control.h"

static const char *TAG = "MAIN";
static int wifi_retry_count = 0;
#define WIFI_MAX_RETRY 5

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

static void start_ap(void);

/* 启动 SNTP 时间同步（定时功能需要 NTP 时间） */
static void start_sntp(void)
{
    setenv("TZ", "CST-8", 1);  // 中国时区 UTC+8
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started, timezone: CST-8");
}

/*
 * DNS 劫持服务器
 * 监听 UDP 53 端口，所有域名都解析为 192.168.4.1
 * 手机连上热点后访问任意网址都会跳到配网页
 */
static void dns_server_task(void *arg)
{
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = INADDR_ANY;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket failed");
        return;
    }
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed");
        close(sock);
        return;
    }

    uint8_t buf[512];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);

    while (1) {
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
        if (len < 0) continue;

        // 只处理标准 DNS 查询（1 个问题、非响应、标准查询）
        if (len < 12 || (buf[0] & 0x80) || buf[2] != 0 || buf[3] != 1) continue;

        // 保持事务 ID，设置 QR=1（响应），RA=1（可用递归）
        buf[0] |= 0x80;         // QR=1 响应
        buf[2] |= 0x80;         // RA=1
        buf[3] |= 0x80;         // 响应码成功

        // 回答问题数 = 1
        buf[6] = 0;
        buf[7] = 1;

        // 跳过问题找到查询名结尾
        int qlen = 12;
        while (qlen < len && buf[qlen]) qlen += buf[qlen] + 1;
        qlen += 5;  // 跳过结束符 00 + Type(2) + Class(2)

        if (qlen + 16 > len) continue;

        // 构造回答记录：Type A (1), Class IN (1), TTL=60s, IP=192.168.4.1
        buf[qlen]     = 0xC0;   // 指针压缩
        buf[qlen + 1] = 0x0C;   // 指向问题名
        buf[qlen + 2] = 0x00;   // Type A
        buf[qlen + 3] = 0x01;
        buf[qlen + 4] = 0x00;   // Class IN
        buf[qlen + 5] = 0x01;
        buf[qlen + 6] = 0x00;   // TTL = 60
        buf[qlen + 7] = 0x00;
        buf[qlen + 8] = 0x00;
        buf[qlen + 9] = 0x3C;
        buf[qlen + 10] = 0x00;  // 数据长度 4
        buf[qlen + 11] = 0x04;
        buf[qlen + 12] = 192;    // 192.168.4.1
        buf[qlen + 13] = 168;
        buf[qlen + 14] = 4;
        buf[qlen + 15] = 1;

        sendto(sock, buf, qlen + 16, 0, (struct sockaddr *)&from, sizeof(from));
    }
}

/* WiFi 事件处理 */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_retry_count++;
        ESP_LOGI(TAG, "WiFi retry %d/%d", wifi_retry_count, WIFI_MAX_RETRY);
        if (wifi_retry_count >= WIFI_MAX_RETRY) {
            ESP_LOGW(TAG, "WiFi failed after %d retries, switching to AP mode", WIFI_MAX_RETRY);
            wifi_retry_count = 0;
            esp_wifi_stop();
            esp_wifi_deinit();
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            esp_wifi_init(&cfg);
            start_ap();
            esp_wifi_start();
        } else {
            esp_wifi_connect();
        }
    } else if (id == IP_EVENT_STA_GOT_IP) {
        wifi_retry_count = 0;
        ESP_LOGI(TAG, "WiFi got IP");
        start_sntp();
    }
}

/* 开启 AP 热点 + DNS 劫持（无 WiFi 配置时自动启动） */
static void start_ap(void)
{
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();  // 需要 STA 接口才能扫描 WiFi
    wifi_config_t ap = {
        .ap = {
            .ssid = "BLE-Daikin",
            .ssid_len = 10,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_APSTA);   // APSTA 模式，支持扫描
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    ESP_LOGI(TAG, "AP: BLE-Daikin (APSTA mode, DNS captive portal)");

    // 启动 DNS 劫持服务器
    xTaskCreatePinnedToCore(dns_server_task, "dns", 3072, NULL, 3, NULL, 0);
}

/*
 * WiFi 初始化
 * - 有保存的 WiFi 凭证 → STA 模式连接
 * - 无凭证 → APSTA 模式（热点 + DNS 劫持配网）
 */
void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    char ssid[32], pass[64];
    load_wifi_creds(ssid, pass);

    if (ssid[0]) {
        ESP_LOGI(TAG, "Connecting to %s", ssid);
        esp_wifi_set_mode(WIFI_MODE_STA);
        wifi_config_t sta = {0};
        strncpy((char*)sta.sta.ssid, ssid, 31);
        strncpy((char*)sta.sta.password, pass, 63);
        esp_wifi_set_config(WIFI_IF_STA, &sta);
        esp_wifi_start();
        esp_wifi_connect();
    } else {
        start_ap();
        esp_wifi_start();
    }
}

/* BLE 连接维护任务 */
static void ble_task(void *arg)
{
    while (1) {
        if (!ble_daikin_is_connected() && !ble_daikin_is_scanning()) {
            ble_daikin_start_scan();
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/* 定时检查任务 */
static void timer_task(void *arg)
{
    while (1) { ble_daikin_timer_check(); vTaskDelay(pdMS_TO_TICKS(30000)); }
}

static void voice_power_cb(uint8_t unit_id, bool on)
{
    ble_daikin_set_power(unit_id, on);
}

static int voice_units_getter(uint8_t *ids, char names[][32], int max)
{
    units_lock();
    int cnt = unit_count < max ? unit_count : max;
    for (int i = 0; i < cnt; i++) {
        ids[i] = units[i].id;
        strncpy(names[i], units[i].name, 31);
        names[i][31] = 0;
    }
    units_unlock();
    return cnt;
}

void app_main(void)
{
    nvs_flash_init();

    wifi_init();
    ble_daikin_init();
    daikin_web_init();
    daikin_web_load_nvs();
    voice_control_register_power_cb(voice_power_cb);
    voice_control_register_units_cb(voice_units_getter);
    daikin_on_rename = voice_control_notify_rename;
    voice_control_init();

    xTaskCreatePinnedToCore(ble_task, "ble", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(timer_task, "tmr", 3072, NULL, 3, NULL, 0);
}
