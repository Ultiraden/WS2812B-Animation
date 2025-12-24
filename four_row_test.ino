#include <OctoWS2811.h>

// ---- Hardware config (pin 2 = Octo output 0) ----
const int LEDS_PER_STRIP = 120;
const int NUM_STRIPS = 8;
byte pinList[NUM_STRIPS] = {2, 14, 7, 8, 6, 20, 21, 5};

DMAMEM int displayMemory[LEDS_PER_STRIP * NUM_STRIPS * 3 / 4];
int drawingMemory[LEDS_PER_STRIP * NUM_STRIPS * 3 / 4];

const int config = WS2811_GRB | WS2811_800kHz;
OctoWS2811 leds(LEDS_PER_STRIP, displayMemory, drawingMemory, config, NUM_STRIPS, pinList);

// ---- Grid geometry ----
static constexpr uint8_t ROWS = 4;
static constexpr uint8_t rowLen[ROWS] = {31, 30, 29, 30};
static constexpr uint16_t TOTAL = 120;

// path[i] = physical LED index (0..119) for the i-th position along the serpentine path
uint16_t pathMap[TOTAL];

// ---- Animation params ----
static constexpr uint8_t  STREAKS = 4;
static constexpr uint8_t  TAIL = 10;        // tail length in pixels
static constexpr uint8_t  BRIGHT = 80;      // 0..255
static constexpr uint16_t DELAY_MS = 25;    // speed

uint16_t head = 0;

static inline uint32_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void clearStrip0() {
  for (int i = 0; i < LEDS_PER_STRIP; i++) leds.setPixel(i, 0);
}

// Build serpentine mapping for 4 rows of uneven lengths.
// Assumes rows are connected end-to-end with serpentine direction flips.
// Row 0 goes left->right, row 1 right->left, etc.
void buildPathMap() {
  uint16_t idx = 0;
  uint16_t base = 0;

  for (uint8_t r = 0; r < ROWS; r++) {
    uint8_t L = rowLen[r];

    if ((r % 2) == 0) {
      // even row: forward
      for (uint8_t x = 0; x < L; x++) pathMap[idx++] = base + x;
    } else {
      // odd row: reverse
      for (int x = (int)L - 1; x >= 0; x--) pathMap[idx++] = base + (uint16_t)x;
    }

    base += L;
  }
  // idx should equal 120
}

void drawStreakAt(uint16_t pathHead, uint32_t color) {
  for (uint8_t t = 0; t < TAIL; t++) {
    int32_t p = (int32_t)pathHead - t;
    if (p < 0) p += TOTAL;

    uint8_t level = (uint8_t)(BRIGHT - (t * (BRIGHT / TAIL)));
    // scale color by level (simple grayscale fade)
    // If you want true color scaling, we can do per-channel scaling.
    uint32_t c = RGB(level, level, level);

    uint16_t ledIndex = pathMap[(uint16_t)p];
    leds.setPixel(ledIndex, c);
  }
}

void setup() {
  leds.begin();
  buildPathMap();
  clearStrip0();
  leds.show();
}

void loop() {
  clearStrip0();

  // 4 streak heads spaced evenly along the 120-length path
  for (uint8_t s = 0; s < STREAKS; s++) {
    uint16_t offset = (uint16_t)((TOTAL * s) / STREAKS);
    uint16_t ph = (head + offset) % TOTAL;
    drawStreakAt(ph, RGB(255, 255, 255));
  }

  leds.show();

  head = (head + 1) % TOTAL;
  delay(DELAY_MS);
}
