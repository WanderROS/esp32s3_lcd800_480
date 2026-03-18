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

/* ---------- LED 颜色状态 ---------- */
static uint8_t led_r = 255, led_g = 0, led_b = 0;
static uint8_t led_brightness = 128;
static bool led_color_changed = true;

/* ---------- 调色板 UI 对象 ---------- */
static lv_obj_t *color_panel = nullptr;
static lv_obj_t *slider_r = nullptr;
static lv_obj_t *slider_g = nullptr;
static lv_obj_t *slider_b = nullptr;
static lv_obj_t *slider_bright = nullptr;
static lv_obj_t *preview_box = nullptr;

static void update_preview(void)
{
    lv_obj_set_style_bg_color(preview_box, lv_color_make(led_r, led_g, led_b), 0);
}

static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);

    if (slider == slider_r) led_r = (uint8_t)val;
    else if (slider == slider_g) led_g = (uint8_t)val;
    else if (slider == slider_b) led_b = (uint8_t)val;
    else if (slider == slider_bright) led_brightness = (uint8_t)val;

    led_color_changed = true;
    update_preview();
}

/* 关闭调色板 */
static void close_panel_cb(lv_event_t *e)
{
    if (color_panel) {
        lv_obj_delete(color_panel);
        color_panel = nullptr;
    }
}

/* 创建一个带标签的 slider，返回 slider 对象 */
static lv_obj_t *create_color_slider(lv_obj_t *parent, const char *text,
                                      lv_color_t knob_color, uint8_t init_val)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 50);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, 10, 0);
    lv_obj_set_style_pad_right(row, 20, 0);   /* 给 knob 留出右侧空间 */
    lv_obj_set_style_pad_top(row, 4, 0);
    lv_obj_set_style_pad_bottom(row, 4, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_width(lbl, 30);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);

    lv_obj_t *sl = lv_slider_create(row);
    lv_slider_set_range(sl, 0, 255);
    lv_slider_set_value(sl, init_val, LV_ANIM_OFF);
    lv_obj_set_flex_grow(sl, 1);
    lv_obj_set_height(sl, 20);
    lv_obj_set_style_bg_color(sl, knob_color, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sl, knob_color, LV_PART_INDICATOR);
    lv_obj_add_event_cb(sl, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return sl;
}

/* 按钮点击 → 弹出调色板 */
static void btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (color_panel) return;  /* 已经打开了 */

    /* 面板铺满屏幕 */
    color_panel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(color_panel, 460, 460);
    lv_obj_center(color_panel);
    lv_obj_set_flex_flow(color_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(color_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(color_panel, 20, 0);
    lv_obj_set_style_pad_row(color_panel, 14, 0);
    lv_obj_clear_flag(color_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题 */
    lv_obj_t *title = lv_label_create(color_panel);
    lv_label_set_text(title, LV_SYMBOL_TINT " LED Color");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

    /* RGBB sliders */
    slider_r = create_color_slider(color_panel, "R", lv_palette_main(LV_PALETTE_RED), led_r);
    slider_g = create_color_slider(color_panel, "G", lv_palette_main(LV_PALETTE_GREEN), led_g);
    slider_b = create_color_slider(color_panel, "B", lv_palette_main(LV_PALETTE_BLUE), led_b);
    slider_bright = create_color_slider(color_panel, LV_SYMBOL_IMAGE, lv_palette_main(LV_PALETTE_YELLOW), led_brightness);

    /* 颜色预览 */
    preview_box = lv_obj_create(color_panel);
    lv_obj_set_size(preview_box, LV_PCT(100), 50);
    lv_obj_set_style_border_width(preview_box, 1, 0);
    lv_obj_set_style_radius(preview_box, 8, 0);
    update_preview();

    /* 关闭按钮 */
    lv_obj_t *close_btn = lv_btn_create(color_panel);
    lv_obj_set_size(close_btn, 140, 44);
    lv_obj_add_event_cb(close_btn, close_panel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "OK");
    lv_obj_center(close_lbl);
}

void setup()
{
    Serial.begin(115200);
    delay(2000);

    strip.begin();
    strip.setBrightness(led_brightness);
    strip.setPixelColor(0, strip.Color(led_r, led_g, led_b));
    strip.show();

    board = new Board();
    if (!board->begin()) {
        Serial.println("Board init failed!");
        return;
    }

    auto backlight = board->getBacklight();
    if (backlight) backlight->on();

    lvgl_port_init(board->getLCD(), board->getTouch());

    lvgl_port_lock(-1);

    lv_obj_t *btn = lv_btn_create(lv_screen_active());
    lv_obj_set_size(btn, 200, 80);
    lv_obj_center(btn);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, LV_SYMBOL_TINT " Pick Color");
    lv_obj_center(label);

    lvgl_port_unlock();

    Serial.println("LVGL ready.");
}

void loop()
{
    if (led_color_changed) {
        led_color_changed = false;
        strip.setBrightness(led_brightness);
        strip.setPixelColor(0, strip.Color(led_r, led_g, led_b));
        strip.show();
    }
    delay(50);
}
