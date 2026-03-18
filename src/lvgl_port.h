#pragma once

#include "esp_display_panel.hpp"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

bool lvgl_port_init(esp_panel::drivers::LCD *lcd, esp_panel::drivers::Touch *tp);
bool lvgl_port_lock(int timeout_ms);
bool lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif
