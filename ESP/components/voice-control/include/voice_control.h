#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define VOICE_MAX_UNITS 8

typedef void (*voice_power_fn)(uint8_t unit_id, bool on);
typedef int (*voice_get_units_fn)(uint8_t *ids, char names[][32], int max);

void voice_control_register_power_cb(voice_power_fn cb);
void voice_control_register_units_cb(voice_get_units_fn cb);

esp_err_t voice_control_init(void);
void voice_control_notify_rename(void);
