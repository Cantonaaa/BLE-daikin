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
static uint16_t handle_cmd = 0;

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
    if (connected) {
        esp_ble_gattc_close(gattc_if, conn_id);
        connected = false;
    }
    return ESP_OK;
}

bool ble_daikin_is_connected(void) { return connected; }

static void start_scan(void)
{
    scanning = true;
    esp_ble_scan_params_t sp = {
        .scan_type = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval = 0x50,
        .scan_window = 0x30,
    };
    esp_ble_gap_set_scan_params(&sp);
}

esp_err_t ble_daikin_set_power(uint8_t unit_id, bool on)
{
    if (!connected || !handle_cmd)
        return ESP_ERR_NOT_FOUND;

    uint8_t cmd[24] = {
        0x00, 0x1d, 0x00, on ? 0x4f : 0x4e,
        0x00, 0x45, 0x15, 0x21, 0x12, 0x1f, 0x1c,
        on ? 0x82 : 0x84, on ? 0x21 : 0x22, 0x00, 0x00,
        0x67, 0x00, 0x00,
        0x36, 0x00, 0x08,
        0x00, 0x03, unit_id,
        0x00
    };
    uint8_t ck = 0;
    for (int i = 2; i < (int)sizeof(cmd) - 1; i++) ck += cmd[i];
    cmd[sizeof(cmd) - 1] = ck;

    esp_ble_gattc_write_char(gattc_if, conn_id, handle_cmd, sizeof(cmd), cmd, ESP_GATT_WRITE_TYPE_RSP);
    ESP_LOGI(TAG, "Power %s unit %d", on ? "ON" : "OFF", unit_id);
    return ESP_OK;
}

static bool name_matches(const char *name)
{
    if (!name || !name[0]) return false;
    return (strstr(name, "Daikin") || strstr(name, "DAIKIN") ||
            strstr(name, "daikin") || strstr(name, "MTK") ||
            strstr(name, "mtk"));
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        esp_ble_gap_start_scanning(30);
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        ESP_LOGI(TAG, "Scanning...");
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        uint8_t *adv = param->scan_rst.ble_adv;
        int len = param->scan_rst.adv_data_len;
        for (int i = 0; i < len - 1; ) {
            int flen = adv[i++];
            if (flen == 0 || i + flen > len) break;
            if (adv[i] == 0x09 || adv[i] == 0x08) {
                char name[32];
                int nl = (flen - 1 < 31) ? (flen - 1) : 31;
                memcpy(name, adv + i + 1, nl);
                name[nl] = 0;
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
        ESP_LOGI(TAG, "Scan stopped");
        break;
    default:
        break;
    }
}

static int find_or_add_unit(uint8_t id)
{
    for (int i = 0; i < unit_count; i++)
        if (units[i].id == id) return i;
    if (unit_count < MAX_UNITS) {
        int idx = unit_count++;
        units[idx].id = id;
        snprintf(units[idx].name, sizeof(units[idx].name), "Unit %d", id);
        ESP_LOGI(TAG, "Discovered unit %d (id=0x%02x)", unit_count, id);
        return idx;
    }
    return -1;
}

static void handle_unit_status_6528(const uint8_t *data, int len)
{
    if (len < 20 || memcmp(data, "\xbb\xb9\xc6\x18\x00\x02\x00\x10", 8))
        return;
    uint8_t uid = data[9];
    int idx = find_or_add_unit(uid);
    if (idx >= 0) {
        bool state = (len > 19 && data[19] == 0x80 && data[20] == 0x01);
        if (units[idx].on != state) {
            units[idx].on = state;
            ESP_LOGI(TAG, "Unit 0x%02x %s", uid, state ? "ON" : "OFF");
        }
    }
}

static void handle_unit_status_660c(const uint8_t *data, int len)
{
    if (len < 12 || memcmp(data, "\xbb\xb9\xc6\x18\x00\x0a\x00\x08", 8))
        return;
    uint8_t uid = data[10];
    int idx = find_or_add_unit(uid);
    if (idx >= 0) {
        bool state = (uid == 0x1b);
        if (units[idx].on != state) {
            units[idx].on = state;
            ESP_LOGI(TAG, "Unit 0x%02x %s", uid, state ? "ON" : "OFF");
        }
    }
}

static void discover_handles(void)
{
    esp_ble_gattc_search_service(gattc_if, conn_id, NULL);
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTC_REG_EVT:
        gattc_if = param->reg.app_id;
        start_scan();
        break;
    case ESP_GATTC_OPEN_EVT:
        if (param->open.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "Connect failed");
            vTaskDelay(pdMS_TO_TICKS(5000));
            start_scan();
            break;
        }
        connected = true;
        conn_id = param->open.conn_id;
        ESP_LOGI(TAG, "Connected to Daikin");
        discover_handles();
        break;
    case ESP_GATTC_SEARCH_RES_EVT: {
        uint16_t sh = param->search_res.start_handle;
        uint16_t eh = param->search_res.end_handle;
        ESP_LOGD(TAG, "Service: 0x%04x-0x%04x", sh, eh);
        for (uint16_t h = sh; h <= eh; h++) {
            esp_ble_gattc_get_char_by_handle(gattc_if, conn_id, h, ESP_GATT_DB_ALL);
        }
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:
        ESP_LOGI(TAG, "Discovery complete");
        if (!handle_cmd) {
            ESP_LOGW(TAG, "Command handle not found, using default 0xf4af");
            handle_cmd = 0xf4af;
        }
        break;
    case ESP_GATTC_GET_CHAR_EVT: {
        uint16_t h = param->get_char.char_handle;
        if (h == 0xf4af) {
            handle_cmd = h;
            ESP_LOGI(TAG, "Found command handle 0xf4af");
            esp_ble_gattc_reg_for_notify(gattc_if, conn_id, h);
        }
        if (h == 0x6528) {
            ESP_LOGI(TAG, "Found status handle 0x6528");
            esp_ble_gattc_reg_for_notify(gattc_if, conn_id, h);
        }
        if (h == 0x660c) {
            ESP_LOGI(TAG, "Found status handle 0x660c");
            esp_ble_gattc_reg_for_notify(gattc_if, conn_id, h);
        }
        break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        uint16_t h = param->reg_for_notify.handle;
        ESP_LOGI(TAG, "Registered for notify on 0x%04x", h);
        break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
        uint16_t h = param->notify.handle;
        if (h == 0x6528)
            handle_unit_status_6528(param->notify.value, param->notify.value_len);
        if (h == 0x660c)
            handle_unit_status_660c(param->notify.value, param->notify.value_len);
        break;
    }
    case ESP_GATTC_WRITE_CHAR_EVT:
        ESP_LOGD(TAG, "Write complete status=%d", param->write.status);
        break;
    case ESP_GATTC_CLOSE_EVT:
        connected = false;
        ESP_LOGI(TAG, "Disconnected, reconnecting...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        start_scan();
        break;
    default:
        break;
    }
}
