#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t daikin_web_init(void);
void daikin_web_load_nvs(void);
void load_wifi_creds(char *ssid, char *pass);
void save_wifi_creds(const char *ssid, const char *pass);
void save_wifi_and_restart(const char *ssid, const char *pass);
bool wait_for_wifi_result(uint32_t timeout_ms);
void wifi_init(void);

extern void (*daikin_on_rename)(void);
