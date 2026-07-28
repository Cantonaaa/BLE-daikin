#include "daikin_web.h"
#include "ble_daikin.h"
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "DAIKIN_WEB";
static httpd_handle_t server = NULL;

static const char *WEB_HTML =
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>BLE Daikin</title>"
    "<style>"
    "*{margin:0;padding:0;box-sizing:border-box;font-family:sans-serif}"
    "body{background:#1a1a2e;color:#eee;padding:20px;max-width:600px;margin:auto}"
    "h1{color:#e94560;margin-bottom:20px}"
    ".unit{background:#16213e;border-radius:12px;padding:16px;margin-bottom:12px}"
    ".unit-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}"
    ".unit-name{font-size:18px;font-weight:bold;cursor:pointer}"
    ".unit-name:hover{color:#e94560}"
    ".unit-status{font-size:13px;color:#aaa;margin-top:2px}"
    ".timer-row{display:flex;gap:8px;font-size:13px;color:#888;margin-top:6px}"
    ".timer-row input{width:52px;background:#0f3460;border:1px solid #2a4a7f;border-radius:4px;color:#eee;padding:3px 6px;font-size:13px}"
    ".timer-row label{color:#888}"
    ".btn{padding:8px 20px;border:none;border-radius:8px;font-size:14px;cursor:pointer;color:#fff}"
    ".btn-on{background:#0f3460}"
    ".btn-on.active{background:#e94560}"
    ".btn-on.active:after{content:'OFF'}"
    ".btn-on:after{content:'ON'}"
    ".header{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px}"
    ".status-dot{width:12px;height:12px;border-radius:50%;display:inline-block;margin-right:8px}"
    ".status-dot.on{background:#00ff88}"
    ".status-dot.off{background:#ff4444}"
    "</style></head><body>"
    "<div class='header'><h1>BLE Daikin</h1><div><span class='status-dot off' id='dot'></span><span id='conn'>Disconnected</span></div></div>"
    "<div id='units'></div>"
    "<script>"
    "async function load(){"
    "let r=await fetch('/api/status');let d=await r.json();"
    "document.getElementById('conn').textContent=d.connected?'Connected':'Disconnected';"
    "document.getElementById('dot').className='status-dot '+(d.connected?'on':'off');"
    "let h='';"
    "for(let u of d.units){"
    "h+='<div class=\"unit\"><div class=\"unit-hdr\"><div><div class=\"unit-name\" onclick=\"rename('+u.id+')\">'+u.name+'</div>';"
    "h+='<div class=\"unit-status\">'+(u.on?'ON':'OFF')+'</div></div>';"
    "h+='<button class=\"btn '+(u.on?'btn-on active':'btn-on')+'\" onclick=\"toggle('+u.id+')\"></button></div>';"
    "h+='<div class=\"timer-row\">';"
    "h+='<label>ON</label><input type=\"time\" id=\"ton'+u.id+'\" value=\"'+u.timer_on+'\" onchange=\"settimer('+u.id+',1,this.value)\">';"
    "h+='<label>OFF</label><input type=\"time\" id=\"toff'+u.id+'\" value=\"'+u.timer_off+'\" onchange=\"settimer('+u.id+',0,this.value)\">';"
    "h+='</div></div>';"
    "}"
    "document.getElementById('units').innerHTML=h;"
    "}"
    "async function toggle(id){await fetch('/api/toggle?id='+id);load();}"
    "async function rename(id){let n=prompt('Name:');if(n)await fetch('/api/rename?id='+id+'&name='+encodeURIComponent(n));load();}"
    "async function settimer(id,on,v){let n='timer_'+(on?'on':'off');await fetch('/api/timer?id='+id+'&type='+n+'&val='+v.replace(':',''));load();}"
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
    cJSON_AddBoolToObject(root, "connected", ble_daikin_is_connected());
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
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    cJSON_Delete(root);
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

esp_err_t daikin_web_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;
    if (httpd_start(&server, &config) != ESP_OK) return ESP_FAIL;

        httpd_uri_t r = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
    httpd_register_uri_handler(server, &r);
    httpd_uri_t s = {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler};
    httpd_register_uri_handler(server, &s);
    httpd_uri_t t = {.uri = "/api/toggle", .method = HTTP_GET, .handler = toggle_handler};
    httpd_register_uri_handler(server, &t);
    httpd_uri_t n = {.uri = "/api/rename", .method = HTTP_GET, .handler = rename_handler};
    httpd_register_uri_handler(server, &n);
    httpd_uri_t m = {.uri = "/api/timer", .method = HTTP_GET, .handler = timer_handler};
    httpd_register_uri_handler(server, &m);
    ESP_LOGI(TAG, "Web started");
    return ESP_OK;
}
