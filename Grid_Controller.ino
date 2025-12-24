#include <OctoWS2811.h>

#define CTRL SerialUSB1   // control channel from PC
// #define CTRL Serial     // <- use this instead if USB Type = "Serial"

#define BOARD_ID 1   // change per grid Teensy

const int numPins = 8;
byte pinList[numPins] = {2, 14, 7, 8, 6, 20, 21, 5};
const int ledsPerStrip = 120;
const int bytesPerLED = 3;

DMAMEM int displayMemory[ledsPerStrip * numPins * bytesPerLED / 4];
int drawingMemory[ledsPerStrip * numPins * bytesPerLED / 4];

const int config = WS2811_GRB | WS2811_800kHz;
OctoWS2811 leds(ledsPerStrip, displayMemory, drawingMemory, config, numPins, pinList);

void announce(Stream& port, const char* which) {
  port.print("GRID BOARD=");
  port.print(BOARD_ID);
  port.print(" PORT=");
  port.println(which); // DBG or CTRL
}

static bool playing = false;  // run mode

void setup() {
  leds.begin();
  leds.show();

  Serial.begin(115200);   // debug port (optional)
  CTRL.begin(0);          // USB CDC ignores baud, but begin() is good practice

  announce(Serial, "DBG");
  announce(CTRL, "CTRL");
}

volatile uint16_t stepCount = 0;

void readCommands() {
  for (;;) {
    int b = pollHandshake(CTRL, "CTRL");
    if (b < 0) break;

    switch (b) {
      case '>': if (stepCount < 65535) stepCount++; break;
      case 'A': playing = false; Animation1(); break; 
      case 'B': playing = false; Animation2(); break;
      case 'C': playing = false; Animation3(); break; 
      case '%': playing = true;  break;
      case '$': playing = false; Off(); break; 
    }
  }
}

int pollHandshake(Stream& port, const char* which) {
  if (!port.available()) return -1;

  int b = port.read();
  if (b == '?') {
    announce(port, which);
    return -1;            // handshake byte consumed
  }
  return b;               // real data byte
}

void loop() {
  pollHandshake(Serial, "DBG");  // debug-only handshake
  readCommands();

  if (stepCount > 0) {
  stepCount--;
  stepFrame(); 
  } else if (playing) stepFrame();
}

void Animation1() {

}

void Animation2() {

}

void Animation3() {

}

void stepFrame() {

}

void Off() {

}