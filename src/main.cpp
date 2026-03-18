#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "Adafruit_NeoPixel.h"
#include "lvgl_port.h"

using namespace esp_panel::board;
using namespace esp_panel::drivers;

#define LED_PIN 4
#define NUM_LEDS 1

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
Board *board = nullptr;

static void btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        Serial.println("Button clicked!");
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    strip.begin();

    board = new Board();
    if (!board->begin()) {
        Serial.println("Board init failed!");
        return;
    }

    auto backlight = board->getBacklight();
    if (backlight) backlight->on();

    // 初始化 LVGL
    lvgl_port_init(board->getLCD(), board->getTouch());

    // 创建 UI
    lvgl_port_lock(-1);

    lv_obj_t *btn = lv_btn_create(lv_screen_active());
    lv_obj_set_size(btn, 200, 80);
    lv_obj_center(btn);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "Hello LVGL!");
    lv_obj_center(label);

    lvgl_port_unlock();

    Serial.println("LVGL ready.");
}

static unsigned long ledPrevMs = 0;
static bool ledOn = false;

void loop() {
    unsigned long now = millis();
    if (now - ledPrevMs >= 1000) {
        ledPrevMs = now;
        ledOn = !ledOn;
        strip.setPixelColor(0, ledOn ? strip.Color(255, 0, 0) : 0);
        strip.show();
    }
    delay(50);
}
