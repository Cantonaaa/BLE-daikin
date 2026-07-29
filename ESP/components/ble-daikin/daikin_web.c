#include "daikin_web.h"
#include "ble_daikin.h"
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "DAIKIN_WEB";
static httpd_handle_t server = NULL;

static const char *WEB_HTML =
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>BLE Daikin</title>"
    "<style>"
    "*{margin:0;padding:0;box-sizing:border-box;font-family:sans-serif}"
    "body{background:#1a1a2e;color:#eee;padding:20px;max-width:600px;margin:auto}"
    "h1{color:#e94560;margin-bottom:20px;font-size:22px}"
    ".card{background:#16213e;border-radius:12px;padding:16px;margin-bottom:12px}"
    ".card-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}"
    ".name{font-size:18px;font-weight:bold;cursor:pointer}"
    ".name:hover{color:#e94560}"
    ".status-text{font-size:13px;color:#aaa;margin-top:2px}"
    ".timer-row{display:flex;gap:8px;font-size:13px;color:#888;margin-top:6px;flex-wrap:wrap}"
    ".timer-row input{width:52px;background:#0f3460;border:1px solid #2a4a7f;border-radius:4px;color:#eee;padding:3px 6px;font-size:13px}"
    ".timer-row label{color:#888;font-size:13px}"
    ".btn{padding:8px 20px;border:none;border-radius:8px;font-size:14px;cursor:pointer;color:#fff;display:inline-block}"
    ".btn-full{width:100%;padding:12px;margin-bottom:12px}"
    ".btn-on{background:#0f3460}"
    ".btn-on.active{background:#e94560}"
    ".btn-on.active:after{content:'OFF'}"
    ".btn-on:after{content:'ON'}"
    ".btn-primary{background:#2a4a7f}"
    ".btn-connect{background:#533483;padding:6px 16px;font-size:13px}"
    ".item{background:#0d1b3e;border-radius:8px;padding:10px;margin-bottom:8px;display:flex;justify-content:space-between;align-items:center}"
    ".item-name{font-size:14px}"
    ".item-rssi{font-size:12px;color:#888}"
    ".header{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px}"
    ".dot{width:12px;height:12px;border-radius:50%;display:inline-block;margin-right:8px}"
    ".dot.green{background:#00ff88}"
    ".dot.red{background:#ff4444}"
    ".dot.yellow{background:#ffaa00}"
    ".wifi-note{background:#0f3460;border-radius:8px;padding:12px;margin-bottom:16px;font-size:14px;color:#ccc;text-align:center}"
    "</style></head><body>"
    "<div class='header'><h1>BLE Daikin</h1><div><span class='dot red' id='dot'></span><span id='conn'>Starting...</span></div></div>"
    "<div id='main'></div>"
    "<script>"
    "function qs(k){let u=new URLSearchParams(location.search);let v=u.get(k);return v?v:''}"
    "async function load(){"
    "let dot=document.getElementById('dot');"
    "let conn=document.getElementById('conn');"
    "let ws=await(await fetch('/api/wifi/status')).json();"
    "if(!ws.connected){"
    "dot.className='dot yellow';conn.textContent='WiFi Setup';"
    "let h='<div class=\"wifi-note\">Connect to your WiFi to enable remote access</div>';"
    "h+='<button class=\"btn btn-primary btn-full\" onclick=\"wifiScan()\">Scan WiFi</button>';"
    "h+='<div id=\"wifi-list\"></div>';"
    "let ssid=qs('ssid');if(ssid){"
    "h+='<div class=\"card\"><div style=\"margin-bottom:8px\">Connect to <b>'+ssid+'</b></div>';"
    "h+='<input id=\"wp\" style=\"width:100%;padding:8px;border-radius:6px;border:0;background:#0d1b3e;color:#eee;font-size:14px;margin-bottom:8px\" type=\"password\" placeholder=\"WiFi Password\" onkeydown=\"if(event.key==\\'Enter\\')wifiConnect()\">';"
    "h+='<button class=\"btn btn-connect\" onclick=\"wifiConnect()\">Connect</button></div>';"
    "}"
    "document.getElementById('main').innerHTML=h;"
    "}else{"
    "let d=await(await fetch('/api/status')).json();"
    "dot.className='dot green';conn.textContent='Connected'+(d.ip?' \\u2022 '+d.ip:'');"
    "let h='';"
    "if(!d.ble_connected){"
    "h+='<button class=\"btn btn-primary btn-full\" onclick=\"bleScan()\">'+(!d.ble_scanning?'Scan BLE Devices':'Scanning...')+'</button>';"
    "if(d.devices){"
    "for(let dv of d.devices){"
    "h+='<div class=\"item\"><div><div class=\"item-name\">'+dv.name+'</div><div class=\"item-rssi\">'+dv.rssi+' dBm</div></div>';"
    "h+='<button class=\"btn btn-connect\" onclick=\"bleConnect('+dv.index+')\">Connect</button></div>';"
    "}"
    "}}"
    "if(d.ble_connected&&d.units){"
    "for(let u of d.units){"
    "h+='<div class=\"card\"><div class=\"card-hdr\"><div><div class=\"name\" onclick=\"rename('+u.id+')\">'+u.name+'</div>';"
    "h+='<div class=\"status-text\">'+(u.on?'ON':'OFF')+'</div></div>';"
    "h+='<button class=\"btn '+(u.on?'btn-on active':'btn-on')+'\" onclick=\"toggle('+u.id+')\"></button></div>';"
    "h+='<div class=\"timer-row\"><label>ON</label><input type=\"time\" id=\"ton'+u.id+'\" value=\"'+u.timer_on+'\" onchange=\"setTimer('+u.id+',1,this.value)\">';"
    "h+='<label>OFF</label><input type=\"time\" id=\"toff'+u.id+'\" value=\"'+u.timer_off+'\" onchange=\"setTimer('+u.id+',0,this.value)\">';"
    "h+='</div></div>';"
    "}}"
    "document.getElementById('main').innerHTML=h;"
    "}}"
    "async function wifiScan(){document.getElementById('wifi-list').innerHTML='Scanning...';"
    "let a=await(await fetch('/api/wifi/scan')).json();"
    "let h='';"
    "for(let ap of a){"
    "h+='<div class=\"item\"><div><div class=\"item-name\">'+ap.ssid+'</div><div class=\"item-rssi\">'+ap.rssi+' dBm</div></div>';"
    "h+='<a class=\"btn btn-connect\" href=\"?ssid='+encodeURIComponent(ap.ssid)+'\">Select</a></div>';"
    "}"
    "document.getElementById('wifi-list').innerHTML=h;"
    "}"
    "async function wifiConnect(){"
    "let ssid=qs('ssid');"
    "let pass=document.getElementById('wp').value;"
    "if(ssid&&pass){document.getElementById('main').innerHTML='<div class=\"wifi-note\">Connecting... ESP32 will restart.</div>';"
    "await fetch('/api/wifi/connect?ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass));"
    "}}"
    "async function bleScan(){await fetch('/api/ble/scan');setTimeout(load,3000);}"
    "async function bleConnect(i){await fetch('/api/ble/connect?idx='+i);load();}"
    "async function toggle(id){await fetch('/api/toggle?id='+id);load();}"
    "async function rename(id){let n=prompt('Name:');if(n)await fetch('/api/rename?id='+id+'&name='+encodeURIComponent(n));load();}"
    "async function setTimer(id,on,v){let t=v.replace(':','');await fetch('/api/timer?id='+id+'&type='+(on?'timer_on':'timer_off')+'&val='+t);load();}"
    "setInterval(load,5000);load();"
    "</script></body></html>";

static void save_timer_nvs(int idx)
{
    nvs_handle_t nvs;
    if (nvs_open("timers", NVS_READWRITE, &nvs) != ESP_OK) return;
    char key[16];
    snprintf(key, sizeof(key), "ton_%02x", units[idx].id);
    nvs_set_u16(nvs, key, units[idx].timer_on);
    snprintf(key, sizeof(key), "toff_%02x", units[idx].id);
    nvs_set_u16(nvs, key, units[idx].timer_off);
    nvs_commit(nvs);
    nvs_close(nvs);
}

static void load_timers_nvs(void)
{
    nvs_handle_t nvs;
    if (nvs_open("timers", NVS_READONLY, &nvs) != ESP_OK) return;
    for (int i = 0; i < unit_count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "ton_%02x", units[i].id);
        nvs_get_u16(nvs, key, &units[i].timer_on);
        snprintf(key, sizeof(key), "toff_%02x", units[i].id);
        nvs_get_u16(nvs, key, &units[i].timer_off);
    }
    nvs_close(nvs);
}

static void save_name_nvs(int idx)
{
    nvs_handle_t nvs;
    if (nvs_open("names", NVS_READWRITE, &nvs) != ESP_OK) return;
    char key[16];
    snprintf(key, sizeof(key), "n_%02x", units[idx].id);
    nvs_set_str(nvs, key, units[idx].name);
    nvs_commit(nvs);
    nvs_close(nvs);
}

static void load_names_nvs(void)
{
    nvs_handle_t nvs;
    if (nvs_open("names", NVS_READONLY, &nvs) != ESP_OK) return;
    for (int i = 0; i < unit_count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "n_%02x", units[i].id);
        size_t len = sizeof(units[i].name);
        nvs_get_str(nvs, key, units[i].name, &len);
    }
    nvs_close(nvs);
}

void daikin_web_load_nvs(void)
{
    load_timers_nvs();
    load_names_nvs();
}

static void fmt_time(uint16_t t, char *buf, size_t len)
{
    if (!t) { buf[0] = 0; return; }
    snprintf(buf, len, "%02d:%02d", t / 100, t % 100);
}

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, WEB_HTML, strlen(WEB_HTML));
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ble_connected", ble_daikin_is_connected());
    cJSON_AddBoolToObject(root, "ble_scanning", ble_daikin_is_scanning());

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip.ip));
            cJSON_AddStringToObject(root, "ip", ip_str);
        }
    }

    if (!ble_daikin_is_connected() && discovered_count > 0) {
        cJSON *arr = cJSON_AddArrayToObject(root, "devices");
        for (int i = 0; i < discovered_count; i++) {
            cJSON *d = cJSON_CreateObject();
            cJSON_AddNumberToObject(d, "index", i);
            cJSON_AddStringToObject(d, "name", discovered_devices[i].name);
            cJSON_AddNumberToObject(d, "rssi", discovered_devices[i].rssi);
            cJSON_AddItemToArray(arr, d);
        }
    }

    if (ble_daikin_is_connected()) {
        cJSON *arr = cJSON_AddArrayToObject(root, "units");
        for (int i = 0; i < unit_count; i++) {
            cJSON *u = cJSON_CreateObject();
            cJSON_AddNumberToObject(u, "id", units[i].id);
            cJSON_AddBoolToObject(u, "on", units[i].on);
            cJSON_AddStringToObject(u, "name", units[i].name);
            char tbuf[8];
            fmt_time(units[i].timer_on, tbuf, sizeof(tbuf));
            cJSON_AddStringToObject(u, "timer_on", tbuf);
            fmt_time(units[i].timer_off, tbuf, sizeof(tbuf));
            cJSON_AddStringToObject(u, "timer_off", tbuf);
            cJSON_AddItemToArray(arr, u);
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t ble_scan_handler(httpd_req_t *req)
{
    ble_daikin_start_scan();
    httpd_resp_sendstr(req, "{\"ok\":1}");
    return ESP_OK;
}

static esp_err_t ble_connect_handler(httpd_req_t *req)
{
    char buf[16];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char idx_str[4];
        if (httpd_query_key_value(buf, "idx", idx_str, sizeof(idx_str)) == ESP_OK) {
            int idx = atoi(idx_str);
            ble_daikin_connect_to(idx);
        }
    }
    httpd_resp_sendstr(req, "{\"ok\":1}");
    return ESP_OK;
}

static esp_err_t toggle_handler(httpd_req_t *req)
{
    char buf[16];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char id_str[4];
        if (httpd_query_key_value(buf, "id", id_str, sizeof(id_str)) == ESP_OK) {
            int id = atoi(id_str);
            for (int i = 0; i < unit_count; i++) {
                if (units[i].id == id) {
                    units[i].on = !units[i].on;
                    ble_daikin_set_power(id, units[i].on);
                    break;
                }
            }
        }
    }
    httpd_resp_sendstr(req, "{\"ok\":1}");
    return ESP_OK;
}

static esp_err_t rename_handler(httpd_req_t *req)
{
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char id_str[4], name[64];
        if (httpd_query_key_value(buf, "id", id_str, sizeof(id_str)) == ESP_OK &&
            httpd_query_key_value(buf, "name", name, sizeof(name)) == ESP_OK) {
            int id = atoi(id_str);
            for (int i = 0; i < unit_count; i++) {
                if (units[i].id == id) {
                    strncpy(units[i].name, name, sizeof(units[i].name) - 1);
                    save_name_nvs(i);
                    break;
                }
            }
        }
    }
    httpd_resp_sendstr(req, "{\"ok\":1}");
    return ESP_OK;
}

static esp_err_t timer_handler(httpd_req_t *req)
{
    char buf[64];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char id_str[4], type[16], val_str[8];
        if (httpd_query_key_value(buf, "id", id_str, sizeof(id_str)) == ESP_OK &&
            httpd_query_key_value(buf, "type", type, sizeof(type)) == ESP_OK &&
            httpd_query_key_value(buf, "val", val_str, sizeof(val_str)) == ESP_OK) {
            int id = atoi(id_str);
            uint16_t t = atoi(val_str);
            for (int i = 0; i < unit_count; i++) {
                if (units[i].id == id) {
                    if (strcmp(type, "timer_on") == 0) units[i].timer_on = t;
                    if (strcmp(type, "timer_off") == 0) units[i].timer_off = t;
                    save_timer_nvs(i);
                    break;
                }
            }
        }
    }
    httpd_resp_sendstr(req, "{\"ok\":1}");
    return ESP_OK;
}

static esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    uint16_t count = 20;
    wifi_ap_record_t records[20];
    esp_wifi_scan_start(NULL, true);
    esp_wifi_scan_get_ap_records(&count, records);

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < count && i < 20; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (char*)records[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(ap, "auth", records[i].authmode);
        cJSON_AddItemToArray(root, ap);
    }
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t wifi_config_handler(httpd_req_t *req)
{
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char ssid[64], pass[64];
        if (httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid)) == ESP_OK &&
            httpd_query_key_value(buf, "pass", pass, sizeof(pass)) == ESP_OK) {
            save_wifi_and_restart(ssid, pass);
        }
    }
    httpd_resp_sendstr(req, "{\"ok\":1}");
    return ESP_OK;
}

static esp_err_t wifi_status_handler(httpd_req_t *req)
{
    wifi_ap_record_t ap;
    bool connected = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "connected", connected);
    if (connected) {
        cJSON_AddStringToObject(root, "ssid", (char*)ap.ssid);
        cJSON_AddNumberToObject(root, "rssi", ap.rssi);
    }
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t daikin_web_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.lru_purge_enable = true;
    if (httpd_start(&server, &config) != ESP_OK) return ESP_FAIL;

    httpd_register_uri_handler(server, &(httpd_uri_t){.uri = "/", .method = HTTP_GET, .handler = root_handler});
    httpd_register_uri_handler(server, &(httpd_uri_t){.uri = "/api/status", .method = HTTP_GET, .handler = status_handler});
    httpd_register_uri_handler(server, &(httpd_uri_t){.uri = "/api/toggle", .method = HTTP_GET, .handler = toggle_handler});
    httpd_register_uri_handler(server, &(httpd_uri_t){.uri = "/api/rename", .method = HTTP_GET, .handler = rename_handler});
    httpd_register_uri_handler(server, &(httpd_uri_t){.uri = "/api/timer", .method = HTTP_GET, .handler = timer_handler});
    httpd_register_uri_handler(server, &(httpd_uri_t){.uri = "/api/ble/scan", .method = HTTP_GET, .handler = ble_scan_handler});
    httpd_register_uri_handler(server, &(httpd_uri_t){.uri = "/api/ble/connect", .method = HTTP_GET, .handler = ble_connect_handler});
    httpd_register_uri_handler(server, &(httpd_uri_t){.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_handler});
    httpd_register_uri_handler(server, &(httpd_uri_t){.uri = "/api/wifi/connect", .method = HTTP_GET, .handler = wifi_config_handler});
    httpd_register_uri_handler(server, &(httpd_uri_t){.uri = "/api/wifi/status", .method = HTTP_GET, .handler = wifi_status_handler});
    ESP_LOGI(TAG, "Web started");
    return ESP_OK;
}
