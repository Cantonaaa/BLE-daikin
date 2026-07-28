#include "ble_daikin.h"
#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"

static const char *TAG = "BLE_DAIKIN";

daikin_unit_t units[MAX_UNITS] = {0};
int unit_count = 0;

static bool connected = false;
static uint16_t conn_id = 0;
static uint16_t gattc_if = 0;
static bool scanning = false;

#define HANDLE_CMD  0xf4af
#define HANDLE_STS1 0x6528
#define HANDLE_STS2 0x660c

static void start_scan(void);
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

esp_err_t ble_daikin_init(void)
{
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    esp_bluedroid_init();
    esp_bluedroid_enable();
    esp_ble_gap_register_callback(esp_gap_cb);
    esp_ble_gattc_register_callback(esp_gattc_cb);
    esp_ble_gattc_app_register(0);
    return ESP_OK;
}

esp_err_t ble_daikin_connect(void)
{
    if (!connected && !scanning) start_scan();
    return ESP_OK;
}

esp_err_t ble_daikin_disconnect(void)
{
    if (connected) { esp_ble_gattc_close(gattc_if, conn_id); connected = false; }
    return ESP_OK;
}

bool ble_daikin_is_connected(void) { return connected; }

static void start_scan(void)
{
    scanning = true;
    esp_ble_scan_params_t sp = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .scan_interval = 0x50, .scan_window = 0x30,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    };
    esp_ble_gap_set_scan_params(&sp);
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
    esp_ble_gattc_write_char(gattc_if, conn_id, HANDLE_CMD, sizeof(cmd), cmd, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
    ESP_LOGI(TAG, "Send %s to unit 0x%02x", on?"ON":"OFF", unit_id);
    return ESP_OK;
}

static bool name_matches(const char *name)
{
    return name && (strstr(name, "Daikin") || strstr(name, "DAIKIN") || strstr(name, "daikin") || strstr(name, "MTK"));
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        esp_ble_gap_start_scanning(30);
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        uint8_t *adv = param->scan_rst.ble_adv;
        for (int i = 0; i < param->scan_rst.adv_data_len - 1; ) {
            int flen = adv[i++];
            if (!flen || i + flen > param->scan_rst.adv_data_len) break;
            if (adv[i] == 0x09 || adv[i] == 0x08) {
                int nl = (flen - 1 < 31) ? (flen - 1) : 31;
                char name[32]; memcpy(name, adv + i + 1, nl); name[nl] = 0;
                ESP_LOGI(TAG, "Found: %s", name);
                if (name_matches(name)) {
                    esp_ble_gap_stop_scanning();
                    esp_ble_gattc_open(gattc_if, param->scan_rst.bda, BLE_ADDR_TYPE_PUBLIC, true);
                }
            }
            i += flen + 1;
        }
        break;
    }
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        scanning = false;
        break;
    default: break;
    }
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gattc_cb_param_t *param)
{
    (void)gatts_if;
    switch (event) {
    case ESP_GATTC_REG_EVT:
        gattc_if = param->reg.app_id;
        start_scan();
        break;
    case ESP_GATTC_OPEN_EVT:
        if (param->open.status != ESP_GATT_OK) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            start_scan();
            break;
        }
        connected = true;
        conn_id = param->open.conn_id;
        ESP_LOGI(TAG, "Connected");
        break;
    case ESP_GATTC_NOTIFY_EVT: {
        uint16_t h = param->notify.handle;
        uint8_t *v = param->notify.value;
        int l = param->notify.value_len;
        if (h == HANDLE_STS1 && l >= 20 && !memcmp(v, "\xbb\xb9\xc6\x18\x00\x02\x00\x10", 8)) {
            int idx = find_or_add_unit(v[9]);
            if (idx >= 0) {
                bool st = (l > 19 && v[19] == 0x80 && v[20] == 0x01);
                if (units[idx].on != st) { units[idx].on = st; ESP_LOGI(TAG, "Unit 0x%02x %s", v[9], st?"ON":"OFF"); }
            }
        } else if (h == HANDLE_STS2 && l >= 12 && !memcmp(v, "\xbb\xb9\xc6\x18\x00\x0a\x00\x08", 8)) {
            int idx = find_or_add_unit(v[10]);
            if (idx >= 0) {
                bool st = (v[10] == 0x1b);
                if (units[idx].on != st) { units[idx].on = st; ESP_LOGI(TAG, "Unit 0x%02x %s", v[10], st?"ON":"OFF"); }
            }
        }
        break;
    }
    case ESP_GATTC_CLOSE_EVT:
        connected = false;
        vTaskDelay(pdMS_TO_TICKS(5000));
        start_scan();
        break;
    default: break;
    }
}
