"""
Pre-build script: patch ESP32_Display_Panel board config
Sets TOUCH_BUS_SKIP_INIT_HOST=1 so Wire manages I2C (shared with audio codec)
"""
Import("env")
import os

board_h = os.path.join(
    env.get("PROJECT_DIR"),
    "lib", "ESP32_Display_Panel", "src", "board", "supported",
    "espressif", "BOARD_ESPRESSIF_ESP32_S3_LCD_EV_BOARD_2.h"
)

if os.path.exists(board_h):
    with open(board_h, "r") as f:
        content = f.read()

    old = "#define ESP_PANEL_BOARD_TOUCH_BUS_SKIP_INIT_HOST        (0)"
    new = "#define ESP_PANEL_BOARD_TOUCH_BUS_SKIP_INIT_HOST        (1)"

    if old in content:
        content = content.replace(old, new)
        with open(board_h, "w") as f:
            f.write(content)
        print("  [patch] TOUCH_BUS_SKIP_INIT_HOST -> 1")
    elif new in content:
        print("  [patch] Already patched")
