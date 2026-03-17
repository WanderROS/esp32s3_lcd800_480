#include <Arduino.h>
#include <esp_display_panel.hpp>
#include "Adafruit_NeoPixel.h"

using namespace esp_panel::board;
using namespace esp_panel::drivers;

#define LED_PIN 4
#define NUM_LEDS 1
#define TOUCH_READ_PERIOD_MS 30

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
Board *board = nullptr;

void setup() {
    Serial.begin(115200);
    delay(2000);
    // NeoPixel 初始化
    strip.begin();

    // 初始化开发板（LCD + Touch + Backlight）
    board = new Board();
    if (!board->begin()) {
        Serial.println("Board init failed!");
        return;
    }

    // 打开背光
    auto backlight = board->getBacklight();
    if (backlight) {
        backlight->on();
    }

    // 用蓝色填满屏幕
    auto lcd = board->getLCD();
    if (lcd) {
        int w = lcd->getFrameWidth();
        int h = lcd->getFrameHeight();
        int color_bits = lcd->getFrameColorBits();
        int bytes_per_pixel = color_bits / 8;

        // 逐行绘制蓝色，避免一次性分配过大内存
        uint8_t *line = (uint8_t *)heap_caps_malloc(w * bytes_per_pixel, MALLOC_CAP_SPIRAM);
        if (line) {
            // 填充蓝色像素数据（RGB565: 0x001F）
            for (int i = 0; i < w; i++) {
                if (bytes_per_pixel == 2) {
                    // RGB565 蓝色 = 0x001F, 小端存储
                    line[i * 2] = 0x1F;
                    line[i * 2 + 1] = 0x00;
                } else if (bytes_per_pixel == 3) {
                    // RGB888 蓝色
                    line[i * 3] = 0x00;     // R
                    line[i * 3 + 1] = 0x00; // G
                    line[i * 3 + 2] = 0xFF; // B
                }
            }
            for (int y = 0; y < h; y++) {
                lcd->drawBitmap(0, y, w, 1, line);
            }
            free(line);
            Serial.printf("LCD filled blue (%dx%d, %d bpp)\n", w, h, color_bits);
        }
    }

    Serial.println("Setup done. Touch the screen...");
}

static unsigned long ledPrevMs = 0;
static bool ledOn = false;

void loop() {
    unsigned long now = millis();

    // 每 1s 切换 LED 状态
    if (now - ledPrevMs >= 1000) {
        ledPrevMs = now;
        ledOn = !ledOn;
        strip.setPixelColor(0, ledOn ? strip.Color(255, 0, 0) : 0);
        strip.show();
    }

    // 读取触摸并串口打印
    auto touch = board->getTouch();
    if (touch) {
        touch->readRawData(-1, -1, TOUCH_READ_PERIOD_MS);
        std::vector<TouchPoint> points;
        touch->getPoints(points);
        int i = 0;
        for (auto &pt : points) {
            Serial.printf("Touch(%d): x=%d, y=%d, strength=%d\n", i++, pt.x, pt.y, pt.strength);
        }
    }

    delay(TOUCH_READ_PERIOD_MS);
}
