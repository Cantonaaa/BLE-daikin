#include "ble_daikin.h"
#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_defs.h"

static const char *TAG = "BLE_DAIKIN";

daikin_unit_t units[MAX_UNITS] = {0};
int unit_count = 0;

static bool connected = false;
static uint16_t conn_id = 0;
static uint16_t gattc_if = 0;
static esp_bd_addr_t remote_bda = {0};

static uint16_t handle_command = 0;
static uint16_t handle_status = 0;

#define BLE_SCAN_DURATION 10
#define REMOTE_SERVICE_UUID 0x1800
#define REMOTE_CHAR_UUID 0x2A00

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

static void start_scan(void)
{
    esp_ble_gap_start_scanning(BLE_SCAN_DURATION);
}

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
    if (!connected) {
        start_scan();
    }
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

bool ble_daikin_is_connected(void)
{
    return connected;
}

esp_err_t ble_daikin_set_power(uint8_t unit_id, bool on)
{
    if (!connected) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t cmd[] = {
        0x00, 0x1d, 0x00, on ? 0x4f : 0x4e,
        0x00, 0x45, 0x15, 0x21, 0x12, 0x1f, 0x1c,
        0x84, 0x22, 0x00, 0x00,
        0x67, 0x00, 0x00,
        0x36, 0x00, 0x08,
        0x00, 0x03, unit_id,
        0x00
    };

    uint8_t checksum = 0;
    for (int i = 2; i < sizeof(cmd) - 1; i++) {
        checksum += cmd[i];
    }
    cmd[sizeof(cmd) - 1] = checksum;

    esp_ble_gattc_write_char(gattc_if, conn_id, handle_command, sizeof(cmd), cmd, ESP_GATT_WRITE_TYPE_RSP);
    ESP_LOGI(TAG, "Sending power %s to unit %d", on ? "ON" : "OFF", unit_id);
    return ESP_OK;
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        start_scan();
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        ESP_LOGI(TAG, "Scan started");
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        auto *r = &param->scan_rst;
        if (r->evt_type == ESP_BLE_EVT_CONN_ADV || r->evt_type == ESP_BLE_EVT_NON_CONN_ADV) {
            char name[32] = {0};
            memcpy(name, r->ble_adv, r->adv_data_len > 31 ? 31 : r->adv_data_len);
            ESP_LOGI(TAG, "Found device: %s", name);
        }
        break;
    }
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        ESP_LOGI(TAG, "Scan stopped");
        break;
    default:
        break;
    }
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTC_REG_EVT:
        gattc_if = gattc_if;
        ESP_LOGI(TAG, "GATTC registered");
        start_scan();
        break;
    case ESP_GATTC_OPEN_EVT:
        connected = true;
        conn_id = param->open.conn_id;
        ESP_LOGI(TAG, "Connected");
        esp_ble_gattc_search_service(gattc_if, conn_id, NULL);
        break;
    case ESP_GATTC_SEARCH_RES_EVT:
        if (param->search_res.srvc_id.uuid.uuid16 == 0xFFE0) {
            ESP_LOGI(TAG, "Found Daikin service");
            esp_ble_gattc_get_char_by_uuid(gattc_if, conn_id, param->search_res.start_handle,
                param->search_res.end_handle, param->search_res.srvc_id.uuid.uuid16, &param->search_res.srvc_id);
        }
        break;
    case ESP_GATTC_GET_CHAR_EVT:
        handle_command = param->get_char.char_handle;
        ESP_LOGI(TAG, "Found char handle: 0x%04x", handle_command);
        break;
    case ESP_GATTC_CLOSE_EVT:
        connected = false;
        ESP_LOGI(TAG, "Disconnected");
        break;
    default:
        break;
    }
}
