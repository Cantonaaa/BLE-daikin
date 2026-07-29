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
discovered_device_t discovered_devices[MAX_DEVICES] = {0};
int discovered_count = 0;

static bool connected = false;
static bool scanning = false;
static uint16_t conn_handle = 0;
static uint16_t handle_cmd = 0;

static void nimble_host_task(void *arg)
{
    nimble_port_run();
}

static void load_saved_addr(uint8_t *addr)
{
    nvs_handle_t nvs;
    memset(addr, 0, 6);
    if (nvs_open("ble", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = 6;
        nvs_get_blob(nvs, "addr", addr, &len);
        nvs_close(nvs);
    }
}

static void save_saved_addr(const uint8_t *addr)
{
    nvs_handle_t nvs;
    if (nvs_open("ble", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_blob(nvs, "addr", addr, 6);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static int ble_gap_event(struct ble_gap_event *event, void *arg);

static int chr_disc_cb(uint16_t conn, const struct ble_gatt_error *err,
                       const struct ble_gatt_chr *chr, void *arg)
{
    if (err->status == 0 && chr) {
        ESP_LOGI(TAG, "  chr 0x%04x val=0x%04x props=%02x",
                 chr->def_handle, chr->val_handle, chr->properties);
        if ((chr->properties & 0x08) || (chr->properties & 0x10)) {
            handle_cmd = chr->val_handle;
            ESP_LOGI(TAG, "Found writable char: 0x%04x", handle_cmd);
        }
        if ((chr->properties & 0x10) && chr->val_handle == 0x6528) {
            uint8_t val[2] = {0x01, 0x00};
            ble_gattc_write_flat(conn, chr->val_handle + 1, val, 2, NULL, NULL);
            ESP_LOGI(TAG, "Subscribed to notifications on 0x%04x", chr->val_handle);
        }
    }
    return 0;
}

static int svc_disc_cb(uint16_t conn, const struct ble_gatt_error *err,
                       const struct ble_gatt_svc *svc, void *arg)
{
    if (err->status == 0 && svc) {
        char uuid_str[40];
        ble_uuid_to_str(&svc->uuid.u, uuid_str);
        ESP_LOGI(TAG, "  svc 0x%04x-0x%04x %s", svc->start_handle, svc->end_handle, uuid_str);
        ble_gattc_disc_all_chrs(conn, svc->start_handle, svc->end_handle, chr_disc_cb, NULL);
    } else if (err->status == BLE_HS_EDONE) {
        if (!handle_cmd) {
            handle_cmd = 0xf4af;
            ESP_LOGW(TAG, "No writable char found, using 0xf4af");
        }
        ESP_LOGI(TAG, "Ready, cmd handle = 0x%04x", handle_cmd);
        connected = true;
    }
    return 0;
}

static void connect_to_addr(const ble_addr_t *addr)
{
    struct ble_gap_conn_params params = {0};
    params.scan_itvl = 24;
    params.scan_window = 24;
    params.itvl_min = 24;
    params.itvl_max = 40;
    params.latency = 0;
    params.supervision_timeout = 500;
    params.min_ce_len = 0;
    params.max_ce_len = 0;
    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, addr, 5000, &params, ble_gap_event, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "Connecting...");
        scanning = false;
    } else {
        ESP_LOGE(TAG, "Connect failed: %d", rc);
    }
}

static void ble_sync_cb(void)
{
    uint8_t saved[6];
    load_saved_addr(saved);

    if (saved[0] || saved[1] || saved[2] || saved[3] || saved[4] || saved[5]) {
        ble_addr_t addr = {0};
        addr.type = BLE_OWN_ADDR_PUBLIC;
        memcpy(addr.val, saved, 6);
        ESP_LOGI(TAG, "Connecting to saved device");
        connect_to_addr(&addr);
    } else {
        ESP_LOGI(TAG, "No saved device, waiting for user selection");
    }
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        if (discovered_count < MAX_DEVICES && event->disc.length_data > 0) {
            discovered_device_t *d = &discovered_devices[discovered_count];
            memcpy(d->addr, event->disc.addr.val, 6);
            d->rssi = event->disc.rssi;
            d->name[0] = 0;

            struct ble_hs_adv_fields fields;
            if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0
                && fields.name_len > 0) {
                int nl = fields.name_len < 31 ? fields.name_len : 31;
                memcpy(d->name, fields.name, nl);
                d->name[nl] = 0;
            }
            if (!d->name[0])
                snprintf(d->name, sizeof(d->name), "%02x:%02x:%02x:%02x:%02x:%02x",
                         d->addr[0], d->addr[1], d->addr[2],
                         d->addr[3], d->addr[4], d->addr[5]);
            discovered_count++;
        }
        break;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        scanning = false;
        ESP_LOGI(TAG, "Scan complete: %d devices", discovered_count);
        if (!connected) {
            uint8_t saved[6];
            load_saved_addr(saved);
            if (saved[0] || saved[1] || saved[2] || saved[3] || saved[4] || saved[5]) {
                for (int i = 0; i < discovered_count; i++) {
                    if (memcmp(discovered_devices[i].addr, saved, 6) == 0) {
                        ESP_LOGI(TAG, "Auto-connecting to saved device");
                        ble_daikin_connect_to(i);
                        break;
                    }
                }
            }
        }
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            scanning = false;
            struct ble_gap_conn_desc conn_desc;
            if (ble_gap_conn_find(event->connect.conn_handle, &conn_desc) == 0) {
                save_saved_addr(conn_desc.peer_addr.val);
            }
            ESP_LOGI(TAG, "Connected, discovering services...");
            handle_cmd = 0;
            ble_gattc_disc_all_svcs(event->connect.conn_handle, svc_disc_cb, NULL);
        } else {
            ESP_LOGE(TAG, "Connect failed: %d", event->connect.status);
            scanning = false;
        }
        break;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        if (event->notify_rx.attr_handle == 0x6528 && event->notify_rx.om) {
            uint8_t *d = event->notify_rx.om->om_data;
            int l = event->notify_rx.om->om_len;
            if (l >= 20 && d[0]==0xbb && d[1]==0xb9 && d[2]==0xc6 && d[3]==0x18) {
                uint8_t uid = d[9];
                bool st = (l > 15 && d[15] == 0x80 && d[16] == 0x01);
                int idx = find_or_add_unit(uid);
                if (idx >= 0 && units[idx].on != st) {
                    units[idx].on = st;
                    ESP_LOGI(TAG, "Unit 0x%02x %s", uid, st?"ON":"OFF");
                }
            }
        }
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        connected = false;
        handle_cmd = 0;
        ESP_LOGI(TAG, "Disconnected");
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
    esp_nimble_hci_init();
    nimble_port_init();
    ble_hs_cfg.sync_cb = ble_sync_cb;
    nimble_port_freertos_init(nimble_host_task);
    return ESP_OK;
}

esp_err_t ble_daikin_start_scan(void)
{
    if (scanning || connected) return ESP_OK;
    discovered_count = 0;
    scanning = true;
    struct ble_gap_disc_params params = {0};
    params.itvl = 30;
    params.window = 30;
    params.filter_policy = 0;
    params.limited = 0;
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 5000, &params, ble_gap_event, NULL);
    if (rc != 0) { scanning = false; }
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t ble_daikin_connect_to(int device_index)
{
    if (device_index < 0 || device_index >= discovered_count)
        return ESP_ERR_INVALID_ARG;
    discovered_device_t *d = &discovered_devices[device_index];
    ble_addr_t addr = {0};
    addr.type = BLE_OWN_ADDR_PUBLIC;
    memcpy(addr.val, d->addr, 6);
    connect_to_addr(&addr);
    return ESP_OK;
}

bool ble_daikin_is_connected(void) { return connected; }
bool ble_daikin_is_scanning(void) { return scanning; }

esp_err_t ble_daikin_set_power(uint8_t unit_id, bool on)
{
    if (!connected || !handle_cmd) return ESP_ERR_NOT_FOUND;

    uint8_t cmd[24] = {
        0x00,0x1d,0x00,on?0x4f:0x4e, 0x00,0x45,0x15,0x21,
        0x12,0x1f,0x1c, on?0x82:0x84, on?0x21:0x22, 0x00,0x00,
        0x67,0x00,0x00, 0x36,0x00,0x08, 0x00,0x03,unit_id, 0x00
    };
    uint8_t ck = 0;
    for (int i = 2; i < 23; i++) ck += cmd[i];
    cmd[23] = ck;

    int rc = ble_gattc_write_flat(conn_handle, handle_cmd, cmd, sizeof(cmd), NULL, NULL);
    ESP_LOGI(TAG, "Power %s unit 0x%02x via 0x%04x (rc=%d)",
             on?"ON":"OFF", unit_id, handle_cmd, rc);
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
