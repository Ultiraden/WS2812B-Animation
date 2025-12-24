#include <OctoWS2811.h>

// ----- CONFIG -----
const int LEDS_PER_STRIP = 120;
const int NUM_STRIPS = 8;
const int TOTAL_LEDS = LEDS_PER_STRIP * NUM_STRIPS;

// OctoWS2811 output pins for Teensy 4.0
byte pinList[NUM_STRIPS] = {2, 14, 7, 8, 6, 20, 21, 5};

// Required memory
DMAMEM int displayMemory[LEDS_PER_STRIP * NUM_STRIPS * 3 / 4];
int drawingMemory[LEDS_PER_STRIP * NUM_STRIPS * 3 / 4];

const int config = WS2811_GRB | WS2811_800kHz;
OctoWS2811 leds(LEDS_PER_STRIP, displayMemory, drawingMemory, config, NUM_STRIPS, pinList);

// ----- Brightness limit (70%) -----
static constexpr uint8_t BRIGHTNESS_70 = 178; // 70% of 255

static inline uint32_t RGB70(uint8_t r, uint8_t g, uint8_t b) {
  r = (uint16_t)r * BRIGHTNESS_70 / 255;
  g = (uint16_t)g * BRIGHTNESS_70 / 255;
  b = (uint16_t)b * BRIGHTNESS_70 / 255;
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void fillAll(uint32_t color) {
  for (int i = 0; i < TOTAL_LEDS; i++) {
    leds.setPixel(i, color);
  }
  leds.show();
}

void fillAlternating(uint32_t c1, uint32_t c2) {
  for (int i = 0; i < TOTAL_LEDS; i++) {
    leds.setPixel(i, (i & 1) ? c1 : c2);
  }
  leds.show();
}

void setup() {
  leds.begin();
  fillAll(0);
}

void loop() {
  // Christmas flash sequence (all outputs on)
  fillAlternating(RGB70(255, 0, 0), RGB70(0, 255, 0));  // red/green alternating
  delay(400);

  fillAlternating(RGB70(255, 255, 255), RGB70(255, 0, 0)); // white/red alternating
  delay(400);

  fillAlternating(RGB70(0, 255, 0), RGB70(255, 255, 255)); // green/white alternating
  delay(400);

  fillAll(RGB70(255, 0, 0));   // solid red
  delay(250);

  fillAll(RGB70(0, 255, 0));   // solid green
  delay(250);

  fillAll(RGB70(255, 255, 255)); // solid white
  delay(250);

  fillAll(0); // off blink
  delay(200);
}
