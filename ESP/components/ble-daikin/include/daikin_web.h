#pragma once
#include "esp_err.h"

esp_err_t daikin_web_init(void);
void daikin_web_load_nvs(void);
void load_wifi_creds(char *ssid, char *pass);
void save_wifi_and_restart(const char *ssid, const char *pass);
void wifi_init(void);

extern void (*daikin_on_rename)(void);
