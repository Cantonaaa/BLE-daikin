#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define MAX_UNITS 8
#define MAX_DEVICES 20

typedef struct {
    uint8_t id;
    bool on;
    char name[32];
    uint16_t timer_on;
    uint16_t timer_off;
} daikin_unit_t;

typedef struct {
    uint8_t addr[6];
    char name[32];
    int8_t rssi;
} discovered_device_t;

extern daikin_unit_t units[MAX_UNITS];
extern int unit_count;
extern discovered_device_t discovered_devices[MAX_DEVICES];
extern int discovered_count;

void units_lock(void);
void units_unlock(void);

esp_err_t ble_daikin_init(void);
esp_err_t ble_daikin_start_scan(void);
esp_err_t ble_daikin_connect_to(int device_index);
bool ble_daikin_is_connected(void);
bool ble_daikin_is_scanning(void);
esp_err_t ble_daikin_set_power(uint8_t unit_id, bool on);
void ble_daikin_timer_check(void);
