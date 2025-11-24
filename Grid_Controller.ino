#include <OctoWS2811.h>

const int numPins = 8;
byte pinList[numPins] = {2, 14, 7, 8, 6, 20, 21, 5};

const int ledsPerStrip = 120;

const int bytesPerLED = 3;  // change to 4 if using RGBW
DMAMEM int displayMemory[ledsPerStrip * numPins * bytesPerLED / 4];
int drawingMemory[ledsPerStrip * numPins * bytesPerLED / 4];

const int config = WS2811_GRB | WS2811_800kHz;

OctoWS2811 leds(ledsPerStrip, displayMemory, drawingMemory, config, numPins, pinList);

#define S1 Serial1 // RX: 0 TX: 1
#define S2 Serial4 // RX: 16 TX: 17

void setup() {
  leds.begin();
  leds.show();
  Serial.begin(38400);
  S1.begin(38400);
  S2.begin(38400);

}

#define RED    0x160000
#define GREEN  0x001600
#define BLUE   0x000016
#define YELLOW 0x101400
#define PINK   0x120009
#define ORANGE 0x100400
#define WHITE  0x101010

void loop() {


}

void write()
