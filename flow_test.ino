#include <OctoWS2811.h>

const int LEDS_PER_STRIP = 120;
const int NUM_STRIPS = 8;

// OctoWS2811 pin order (output 0 = pin 2)
byte pinList[NUM_STRIPS] = {2, 14, 7, 8, 6, 20, 21, 5};

DMAMEM int displayMemory[LEDS_PER_STRIP * NUM_STRIPS * 3 / 4];
int drawingMemory[LEDS_PER_STRIP * NUM_STRIPS * 3 / 4];

const int config = WS2811_GRB | WS2811_800kHz;
OctoWS2811 leds(LEDS_PER_STRIP, displayMemory, drawingMemory, config, NUM_STRIPS, pinList);

// Animation parameters
const uint8_t TAIL_LENGTH = 8;
const uint8_t BRIGHTNESS = 64;
const uint16_t DELAY_MS = 25;

static int head = 0;

static inline uint32_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void clearAll() {
  for (int i = 0; i < LEDS_PER_STRIP * NUM_STRIPS; i++) {
    leds.setPixel(i, 0);
  }
}

void setup() {
  leds.begin();
  clearAll();
  leds.show();
}

void loop() {
  clearAll();

  // Draw head + fading tail on strip 0 (pin 2)
  for (int i = 0; i < TAIL_LENGTH; i++) {
    int idx = head - i;
    if (idx < 0) idx += LEDS_PER_STRIP;

    uint8_t level = BRIGHTNESS - (i * (BRIGHTNESS / TAIL_LENGTH));
    leds.setPixel(idx, RGB(level, level, level));
  }

  leds.show();

  head++;
  if (head >= LEDS_PER_STRIP) head = 0;

  delay(DELAY_MS);
}
