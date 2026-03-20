#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "Adafruit_NeoPixel.h"
#include "lvgl_port.h"
#include <math.h>

using namespace esp_panel::board;
using namespace esp_panel::drivers;

#define LED_PIN    4
#define NUM_LEDS   1

/* 色轮参数 */
#define CW_DIAMETER  440
#define CW_RADIUS    (CW_DIAMETER / 2)

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
Board *board = nullptr;

/* LED 颜色状态 */
static uint8_t led_r = 255, led_g = 0, led_b = 0;
static uint8_t led_brightness = 128;
static bool    led_color_changed = true;

/* UI 对象 */
static lv_obj_t *color_panel   = nullptr;
static lv_obj_t *canvas        = nullptr;
static lv_obj_t *knob          = nullptr;
static lv_obj_t *preview_box   = nullptr;
static lv_obj_t *slider_bright = nullptr;

/* canvas 缓冲区 (RGB565, 2 bytes/pixel) */
static uint8_t *cbuf = nullptr;

/* ========== HSV → RGB ========== */
static void hsv_to_rgb(float h, float s, float v,
                       uint8_t *r, uint8_t *g, uint8_t *b)
{
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf, gf, bf;

    if      (h < 60)  { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else              { rf = c; gf = 0; bf = x; }

    *r = (uint8_t)((rf + m) * 255.0f);
    *g = (uint8_t)((gf + m) * 255.0f);
    *b = (uint8_t)((bf + m) * 255.0f);
}

/* ========== 绘制色轮到 canvas ========== */
static void draw_color_wheel(void)
{
    for (int y = 0; y < CW_DIAMETER; y++) {
        for (int x = 0; x < CW_DIAMETER; x++) {
            float dx = x - CW_RADIUS;
            float dy = y - CW_RADIUS;
            float dist = sqrtf(dx * dx + dy * dy);

            lv_color_t c;
            if (dist > CW_RADIUS) {
                c = lv_color_white();  /* 圆外区域用白色 */
            } else {
                float hue = atan2f(dy, dx) * (180.0f / M_PI) + 180.0f;
                float sat = dist / (float)CW_RADIUS;
                uint8_t r, g, b;
                hsv_to_rgb(hue, sat, 1.0f, &r, &g, &b);
                c = lv_color_make(r, g, b);
            }
            lv_canvas_set_px(canvas, x, y, c, LV_OPA_COVER);
        }
    }
}

/* ========== 从触摸坐标取色 ========== */
static void pick_color_at(int32_t x, int32_t y)
{
    float dx = x - CW_RADIUS;
    float dy = y - CW_RADIUS;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist > CW_RADIUS) return;  /* 圆外不响应 */

    float hue = atan2f(dy, dx) * (180.0f / M_PI) + 180.0f;
    float sat = dist / (float)CW_RADIUS;
    hsv_to_rgb(hue, sat, 1.0f, &led_r, &led_g, &led_b);
    led_color_changed = true;

    /* 更新预览 */
    if (preview_box) {
        lv_obj_set_style_bg_color(preview_box, lv_color_make(led_r, led_g, led_b), 0);
    }
    /* 移动 knob 指示器 */
    if (knob) {
        lv_obj_set_pos(knob, x - 9, y - 9);
        lv_obj_set_style_border_color(knob, lv_color_make(led_r, led_g, led_b), 0);
    }
}

/* canvas 触摸事件 */
static void canvas_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSING && code != LV_EVENT_CLICKED) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    /* 用 lv_obj_get_coords 获取 canvas 在屏幕上的绝对位置 */
    lv_area_t coords;
    lv_obj_get_coords(canvas, &coords);

    int32_t cx = point.x - coords.x1;
    int32_t cy = point.y - coords.y1;

    if (cx >= 0 && cx < CW_DIAMETER && cy >= 0 && cy < CW_DIAMETER) {
        pick_color_at(cx, cy);
    }
}

/* 亮度 slider 事件 */
static void bright_event_cb(lv_event_t *e)
{
    lv_obj_t *sl = (lv_obj_t *)lv_event_get_target(e);
    led_brightness = (uint8_t)lv_slider_get_value(sl);
    led_color_changed = true;
}

/* 关闭面板 */
static void close_panel_cb(lv_event_t *e)
{
    if (color_panel) {
        lv_obj_delete(color_panel);
        color_panel = nullptr;
        canvas = nullptr;
        knob = nullptr;
        preview_box = nullptr;
        slider_bright = nullptr;
    }
    if (cbuf) {
        free(cbuf);
        cbuf = nullptr;
    }
}

/* ========== 弹出色轮面板 ========== */
static void btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (color_panel) return;

    /* 面板铺满屏幕，横向三栏布局：左侧控制 | 中间色轮 | 右侧亮度 */
    color_panel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(color_panel, 800, 480);
    lv_obj_set_pos(color_panel, 0, 0);
    lv_obj_set_flex_flow(color_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(color_panel, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(color_panel, 10, 0);
    lv_obj_set_style_pad_column(color_panel, 10, 0);
    lv_obj_set_style_radius(color_panel, 0, 0);
    lv_obj_set_style_border_width(color_panel, 0, 0);
    lv_obj_clear_flag(color_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- 左侧栏：预览色块 + OK 按钮 ---- */
    lv_obj_t *left_col = lv_obj_create(color_panel);
    lv_obj_set_size(left_col, 140, CW_DIAMETER);
    lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left_col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(left_col, 8, 0);
    lv_obj_set_style_pad_row(left_col, 16, 0);
    lv_obj_set_style_border_width(left_col, 0, 0);
    lv_obj_set_style_bg_opa(left_col, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(left_col, LV_OBJ_FLAG_SCROLLABLE);

    /* 颜色预览色块 */
    preview_box = lv_obj_create(left_col);
    lv_obj_set_size(preview_box, 120, 120);
    lv_obj_set_style_radius(preview_box, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(preview_box, 2, 0);
    lv_obj_set_style_border_color(preview_box, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_bg_color(preview_box, lv_color_make(led_r, led_g, led_b), 0);

    /* OK 按钮 */
    lv_obj_t *close_btn = lv_btn_create(left_col);
    lv_obj_set_size(close_btn, 120, 50);
    lv_obj_add_event_cb(close_btn, close_panel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "OK");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(close_lbl);

    /* ---- 中间：色轮 ---- */
    lv_obj_t *cw_cont = lv_obj_create(color_panel);
    lv_obj_set_size(cw_cont, CW_DIAMETER, CW_DIAMETER);
    lv_obj_set_style_pad_all(cw_cont, 0, 0);
    lv_obj_set_style_border_width(cw_cont, 0, 0);
    lv_obj_set_style_bg_opa(cw_cont, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(cw_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Canvas */
    cbuf = (uint8_t *)malloc(LV_CANVAS_BUF_SIZE(CW_DIAMETER, CW_DIAMETER,
                                                  16, LV_DRAW_BUF_STRIDE_ALIGN));
    if (!cbuf) {
        Serial.println("Canvas buf alloc failed!");
        lv_obj_delete(color_panel);
        color_panel = nullptr;
        return;
    }

    canvas = lv_canvas_create(cw_cont);
    lv_canvas_set_buffer(canvas, cbuf, CW_DIAMETER, CW_DIAMETER, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(canvas, canvas_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(canvas, canvas_event_cb, LV_EVENT_CLICKED, NULL);

    draw_color_wheel();

    /* Knob 指示器 */
    knob = lv_obj_create(cw_cont);
    lv_obj_set_size(knob, 18, 18);
    lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(knob, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(knob, 2, 0);
    lv_obj_set_style_border_color(knob, lv_color_white(), 0);
    lv_obj_clear_flag(knob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(knob, CW_RADIUS - 9, CW_RADIUS - 9);

    /* ---- 右侧栏：竖向亮度 slider ---- */
    lv_obj_t *right_col = lv_obj_create(color_panel);
    lv_obj_set_size(right_col, 80, CW_DIAMETER);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(right_col, 8, 0);
    lv_obj_set_style_pad_row(right_col, 8, 0);
    lv_obj_set_style_border_width(right_col, 0, 0);
    lv_obj_set_style_bg_opa(right_col, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(right_col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sun_icon = lv_label_create(right_col);
    lv_label_set_text(sun_icon, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(sun_icon, &lv_font_montserrat_24, 0);

    slider_bright = lv_slider_create(right_col);
    lv_slider_set_range(slider_bright, 0, 255);
    lv_slider_set_value(slider_bright, led_brightness, LV_ANIM_OFF);
    lv_obj_set_width(slider_bright, 20);
    lv_obj_set_flex_grow(slider_bright, 1);
    lv_obj_set_style_bg_color(slider_bright,
                              lv_palette_main(LV_PALETTE_YELLOW), LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider_bright,
                              lv_palette_main(LV_PALETTE_YELLOW), LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider_bright, bright_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/* ========== setup / loop ========== */
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
