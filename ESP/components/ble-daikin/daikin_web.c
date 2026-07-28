#include "daikin_web.h"
#include "ble_daikin.h"
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "DAIKIN_WEB";
static httpd_handle_t server = NULL;

static const char *WEB_HTML =
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>BLE Daikin Control</title>"
    "<style>"
    "*{margin:0;padding:0;box-sizing:border-box;font-family:sans-serif}"
    "body{background:#1a1a2e;color:#eee;padding:20px;max-width:600px;margin:auto}"
    "h1{color:#e94560;margin-bottom:20px;font-size:24px}"
    ".unit{background:#16213e;border-radius:12px;padding:16px;margin-bottom:12px;display:flex;align-items:center;justify-content:space-between}"
    ".unit-name{font-size:18px;font-weight:bold}"
    ".unit-status{font-size:14px;color:#aaa;margin-top:4px}"
    ".btn{padding:10px 24px;border:none;border-radius:8px;font-size:16px;cursor:pointer;color:#fff;transition:.3s}"
    ".btn-on{background:#0f3460}"
    ".btn-on.active{background:#e94560}"
    ".btn-on.active:after{content:'OFF'}"
    ".btn-on:after{content:'ON'}"
    ".btn-off{background:#533483}"
    ".header{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px}"
    ".status-dot{width:12px;height:12px;border-radius:50%;display:inline-block;margin-right:8px}"
    ".status-dot.on{background:#00ff88}"
    ".status-dot.off{background:#ff4444}"
    ".status-dot.connecting{background:#ffaa00}"
    "</style></head><body>"
    "<div class='header'><h1>BLE Daikin</h1><div id='conn-status'><span class='status-dot off'></span><span id='conn-text'>Disconnected</span></div></div>"
    "<div id='units'></div>"
    "<script>"
    "async function load(){"
    "let r=await fetch('/api/status');let d=await r.json();"
    "document.getElementById('conn-text').textContent=d.connected?'Connected':'Disconnected';"
    "document.querySelector('.status-dot').className='status-dot '+(d.connected?'on':d.connecting?'connecting':'off');"
    "let html='';"
    "for(let u of d.units){"
    "html+='<div class=\"unit\"><div><div class=\"unit-name\">'+u.name+'</div><div class=\"unit-status\">'+(u.on?'ON':'OFF')+'</div></div>';"
    "html+='<button class=\"btn '+(u.on?'btn-on active':'btn-on')+'\" onclick=\"toggle('+u.id+')\"></button></div>';"
    "}"
    "document.getElementById('units').innerHTML=html;"
    "}"
    "async function toggle(id){"
    "await fetch('/api/toggle?id='+id);load();"
    "}"
    "setInterval(load,3000);load();"
    "</script></body></html>";

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, WEB_HTML, strlen(WEB_HTML));
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *units_arr = cJSON_AddArrayToObject(root, "units");
    cJSON_AddBoolToObject(root, "connected", ble_daikin_is_connected());

    for (int i = 0; i < unit_count; i++) {
        cJSON *u = cJSON_CreateObject();
        cJSON_AddNumberToObject(u, "id", units[i].id);
        cJSON_AddBoolToObject(u, "on", units[i].on);
        cJSON_AddStringToObject(u, "name", units[i].name[0] ? units[i].name : "Unknown");
        cJSON_AddItemToArray(units_arr, u);
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
    char buf[16] = {0};
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char id_str[4] = {0};
        if (httpd_query_key_value(buf, "id", id_str, sizeof(id_str)) == ESP_OK) {
            int id = atoi(id_str);
            for (int i = 0; i < unit_count; i++) {
                if (units[i].id == id) {
                    bool new_state = !units[i].on;
                    ble_daikin_set_power(id, new_state);
                    units[i].on = new_state;
                    break;
                }
            }
        }
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

esp_err_t daikin_web_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server");
        return ESP_FAIL;
    }

    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
    httpd_register_uri_handler(server, &root);

    httpd_uri_t status = {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler};
    httpd_register_uri_handler(server, &status);

    httpd_uri_t toggle = {.uri = "/api/toggle", .method = HTTP_GET, .handler = toggle_handler};
    httpd_register_uri_handler(server, &toggle);

    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}
