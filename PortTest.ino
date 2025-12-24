#include <OctoWS2811.h>

// ----- CONFIG -----
const int LEDS_PER_STRIP = 120;
const int NUM_STRIPS = 8;

// OctoWS2811 output pins for Teensy 4.0
byte pinList[NUM_STRIPS] = {2, 14, 7, 8, 6, 20, 21, 5};

// Required memory
DMAMEM int displayMemory[LEDS_PER_STRIP * NUM_STRIPS * 3 / 4];
int drawingMemory[LEDS_PER_STRIP * NUM_STRIPS * 3 / 4];

const int config = WS2811_GRB | WS2811_800kHz;
OctoWS2811 leds(LEDS_PER_STRIP, displayMemory, drawingMemory, config, NUM_STRIPS, pinList);

// ----- Brightness control -----
static constexpr uint8_t BRIGHTNESS_70 = 178; // 70% of 255

static inline uint32_t RGB70(uint8_t r, uint8_t g, uint8_t b) {
  r = (uint16_t)r * BRIGHTNESS_70 / 255;
  g = (uint16_t)g * BRIGHTNESS_70 / 255;
  b = (uint16_t)b * BRIGHTNESS_70 / 255;
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// Timing
const uint16_t HOLD_MS = 1500;

void clearAll() {
  for (int i = 0; i < LEDS_PER_STRIP * NUM_STRIPS; i++) {
    leds.setPixel(i, 0);
  }
}

void turnOnPort(uint8_t port) {
  clearAll();

  int base = port * LEDS_PER_STRIP;
  uint32_t color = RGB70(255, 255, 255); // white @ 70%

  for (int i = 0; i < LEDS_PER_STRIP; i++) {
    leds.setPixel(base + i, color);
  }

  leds.show();
}

void setup() {
  leds.begin();
  clearAll();
  leds.show();
}

void loop() {
  for (uint8_t port = 0; port < NUM_STRIPS; port++) {
    turnOnPort(port);
    delay(HOLD_MS);
  }
}
