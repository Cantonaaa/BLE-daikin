#include "ble_daikin.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_nimble_hci.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char *TAG = "BLE_DAIKIN";

daikin_unit_t units[MAX_UNITS] = {0};
int unit_count = 0;

static bool connected = false;
static uint16_t conn_handle = 0;

#define HANDLE_CMD  0xf4af

static int ble_gap_event(struct ble_gap_event *event, void *arg);

static void ble_sync_cb(void)
{
    struct ble_gap_disc_params disc_params = {0};
    disc_params.itvl = 30;
    disc_params.window = 30;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;
    ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 30000, &disc_params, ble_gap_event, NULL);
    ESP_LOGI(TAG, "Scanning...");
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0)
            break;
        if (fields.name_len > 0) {
            char name[32];
            int nl = fields.name_len < 31 ? fields.name_len : 31;
            memcpy(name, fields.name, nl);
            name[nl] = 0;
            ESP_LOGI(TAG, "Found: %s", name);
            if (strstr(name, "Daikin") || strstr(name, "DAIKIN") || strstr(name, "MTK")) {
                ble_gap_disc_cancel();
                struct ble_gap_conn_params params = {0};
                params.scan_itvl = 24;
                params.scan_window = 24;
                params.itvl_min = 24;
                params.itvl_max = 40;
                params.latency = 0;
                params.supervision_timeout = 500;
                params.min_ce_len = 0;
                params.max_ce_len = 0;
                ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &event->disc.addr, 30000, &params, ble_gap_event, NULL);
            }
        }
        break;
    }
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            connected = true;
            ESP_LOGI(TAG, "Connected!");
        } else {
            ESP_LOGE(TAG, "Connect failed");
            ble_sync_cb();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        connected = false;
        ESP_LOGI(TAG, "Disconnected, reconnecting...");
        ble_sync_cb();
        break;
    default:
        break;
    }
    return 0;
}

static int find_or_add_unit(uint8_t id)
{
    for (int i = 0; i < unit_count; i++)
        if (units[i].id == id) return i;
    if (unit_count < MAX_UNITS) {
        int i = unit_count++;
        units[i].id = id;
        snprintf(units[i].name, sizeof(units[i].name), "Unit %d", id);
        ESP_LOGI(TAG, "Unit %d discovered (0x%02x)", unit_count, id);
        return i;
    }
    return -1;
}

esp_err_t ble_daikin_init(void)
{
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    esp_nimble_hci_init();
    nimble_port_init();
    ble_hs_cfg.sync_cb = ble_sync_cb;
    nimble_port_freertos_init((TaskFunction_t)nimble_port_run);
    return ESP_OK;
}

esp_err_t ble_daikin_connect(void) { return ESP_OK; }
esp_err_t ble_daikin_disconnect(void) { return ESP_OK; }
bool ble_daikin_is_connected(void) { return connected; }

esp_err_t ble_daikin_set_power(uint8_t unit_id, bool on)
{
    if (!connected) return ESP_ERR_NOT_FOUND;

    uint8_t cmd[24] = {
        0x00,0x1d,0x00,on?0x4f:0x4e, 0x00,0x45,0x15,0x21,
        0x12,0x1f,0x1c, on?0x82:0x84, on?0x21:0x22, 0x00,0x00,
        0x67,0x00,0x00, 0x36,0x00,0x08, 0x00,0x03,unit_id, 0x00
    };
    uint8_t ck = 0;
    for (int i = 2; i < 23; i++) ck += cmd[i];
    cmd[23] = ck;

    int rc = ble_gattc_write_no_rsp_flat(conn_handle, HANDLE_CMD, cmd, sizeof(cmd));
    ESP_LOGI(TAG, "Power %s unit 0x%02x (rc=%d)", on?"ON":"OFF", unit_id, rc);
    return (rc == 0) ? ESP_OK : ESP_FAIL;
}

void ble_daikin_timer_check(void)
{
    struct timeval tv;
    struct tm tm;
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm);
    uint16_t now = tm.tm_hour * 100 + tm.tm_min;
    for (int i = 0; i < unit_count; i++) {
        if (units[i].timer_on && now == units[i].timer_on && !units[i].on) {
            ble_daikin_set_power(units[i].id, true);
            units[i].on = true;
        }
        if (units[i].timer_off && now == units[i].timer_off && units[i].on) {
            ble_daikin_set_power(units[i].id, false);
            units[i].on = false;
        }
    }
}
