#include <Arduino.h>


void setup() {
 Serial.begin(115200);
}

void loop() {
  delay(2000);
  Serial.println("hello world!");
}
