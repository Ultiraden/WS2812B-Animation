#include <OctoWS2811.h>

const int LEDS_PER_STRIP = 120;
const int NUM_STRIPS = 8;

// OctoWS2811 pin order (output 0 = pin 2)
byte pinList[NUM_STRIPS] = {2, 14, 7, 8, 6, 20, 21, 5};

DMAMEM int displayMemory[LEDS_PER_STRIP * NUM_STRIPS * 3 / 4];
int drawingMemory[LEDS_PER_STRIP * NUM_STRIPS * 3 / 4];

const int config = WS2811_GRB | WS2811_800kHz;
OctoWS2811 leds(LEDS_PER_STRIP, displayMemory, drawingMemory, config, NUM_STRIPS, pinList);

void setup() {
  leds.begin();

  // Turn OFF everything
  for (int i = 0; i < LEDS_PER_STRIP * NUM_STRIPS; i++) {
    leds.setPixel(i, 0);
  }

  // Turn ON ONLY strip on pin 2 (output 0)
  for (int i = 0; i < LEDS_PER_STRIP; i++) {
    leds.setPixel(i, 0x101010);  // soft white
  }

  leds.show();
}

void loop() {
  // intentionally empty — LEDs stay on
}
