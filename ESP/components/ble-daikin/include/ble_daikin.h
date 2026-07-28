#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MAX_UNITS 8

typedef struct {
    uint8_t id;
    bool on;
    char name[32];
} daikin_unit_t;

extern daikin_unit_t units[MAX_UNITS];
extern int unit_count;

esp_err_t ble_daikin_init(void);
esp_err_t ble_daikin_connect(void);
esp_err_t ble_daikin_disconnect(void);
bool ble_daikin_is_connected(void);
esp_err_t ble_daikin_set_power(uint8_t unit_id, bool on);
