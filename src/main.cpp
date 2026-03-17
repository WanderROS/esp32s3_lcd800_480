#include <Arduino.h>
#include "Adafruit_NeoPixel.h"

#define LED_PIN 4
#define NUM_LEDS 1

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  strip.begin();
}

void loop() {
  strip.setPixelColor(0, 255, 0, 0); // 红色
  strip.show();
  delay(1000);
  
  strip.setPixelColor(0, 0, 0, 0); // 关闭
  strip.show();
  delay(1000);
}