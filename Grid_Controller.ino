#include <OctoWS2811.h>
#define s1 SerialUSB1

const int numPins = 8;
byte pinList[numPins] = {2, 14, 7, 8, 6, 20, 21, 5};

const int ledsPerStrip = 120;

const int bytesPerLED = 3;  // change to 4 if using RGBW
DMAMEM int displayMemory[ledsPerStrip * numPins * bytesPerLED / 4];
int drawingMemory[ledsPerStrip * numPins * bytesPerLED / 4];

const int config = WS2811_GRB | WS2811_800kHz;

OctoWS2811 leds(ledsPerStrip, displayMemory, drawingMemory, config, numPins, pinList);

void setup() { // Change B1 -> any name for network board as desired
  Serial.println("B1: This may be sent before your PC is able to receive");
  leds.begin();
  leds.show();
  while (!Serial) {}
  Serial.println("B1: Serial Ready");
}

#define RED    0x160000
#define GREEN  0x001600
#define BLUE   0x000016
#define YELLOW 0x101400
#define PINK   0x120009
#define ORANGE 0x100400
#define WHITE  0x101010

char incomingByte = 1;
void loop() {
  receive();
  if (incomingByte == 36) {Off();}
  else if (incomingByte == 65) {Animation1();}
  else if (incomingByte == 66) {Animation2();}
  else if (incomingByte == 67) {Animation3();}
  else if (incomingByte == 37) {run();}
  clear();
  delay(100);
}

void receive() {
  if (s1.available()) {
    incomingByte = s1.read();
  }
}
void clear() {incomingByte = 1;}

void run() {
  while (incomingByte == 37) {
    // display frame
    
    // load next frame

    // increment frame counter

    receive();
    if (incomingByte == 36) {incomingByte = 37;}
  }

}
