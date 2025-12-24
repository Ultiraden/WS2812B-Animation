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
static constexpr uint8_t MAX_W = 31;                       // widest row
static constexpr uint8_t rowLen[ROWS] = {31, 30, 29, 30};  // y=0 is top

// XY[x][y] = physical LED index 0..119, or -1 if that (x,y) doesn't exist
int16_t XY[MAX_W][ROWS];

// ---- Animation params ----
static constexpr uint8_t STREAKS = 4;
static constexpr uint8_t TAIL_Y  = 2;     // tail length in rows behind head
static constexpr uint8_t BRIGHT  = 90;    // 0..255
static constexpr uint16_t DELAY_MS = 80;  // speed

// Pick 4 fixed x positions (edit freely).
// Tip: keep <= 28 if you want the streak to exist in ALL rows (row 2 has only 29 pixels: x=0..28).
uint8_t streakX[STREAKS] = {3, 10, 17, 24};

// Each streak has its own y position; motion is always upward (toward y=0)
int8_t yPos[STREAKS] = {3, 2, 1, 0};      // starting rows
static constexpr int8_t DIR_UP = -1;      // constant direction (up)

static inline uint32_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void clearStrip0() {
  for (int i = 0; i < LEDS_PER_STRIP; i++) leds.setPixel(i, 0);
}

// Build XY mapping based on serpentine physical wiring across rows.
// Assumption: LEDs are chained row-by-row in y order 0->1->2->3.
// Even rows go x=0..L-1, odd rows go x=L-1..0.
void buildXY() {
  for (uint8_t x = 0; x < MAX_W; x++)
    for (uint8_t y = 0; y < ROWS; y++)
      XY[x][y] = -1;

  uint16_t base = 0;
  for (uint8_t y = 0; y < ROWS; y++) {
    uint8_t L = rowLen[y];

    if ((y % 2) == 0) {
      for (uint8_t x = 0; x < L; x++) XY[x][y] = base + x;
    } else {
      for (uint8_t x = 0; x < L; x++) XY[x][y] = base + (L - 1 - x);
    }

    base += L;
  }
}

void drawPixelIfExists(uint8_t x, uint8_t y, uint32_t c) {
  if (x >= MAX_W || y >= ROWS) return;
  int16_t idx = XY[x][y];
  if (idx >= 0) leds.setPixel((uint16_t)idx, c);
}

void drawVerticalTail(uint8_t x, int8_t headY) {
  // Head
  drawPixelIfExists(x, (uint8_t)headY, RGB(BRIGHT, BRIGHT, BRIGHT));

  // Tail behind direction of travel (since moving up, tail goes downward)
  for (uint8_t t = 1; t <= TAIL_Y; t++) {
    int8_t ty = headY - DIR_UP * (int8_t)t; // DIR_UP = -1 => ty = headY + t
    if (ty < 0 || ty >= (int8_t)ROWS) continue;

    uint8_t level = (uint8_t)(BRIGHT - (t * (BRIGHT / (TAIL_Y + 1))));
    drawPixelIfExists(x, (uint8_t)ty, RGB(level, level, level));
  }
}

void stepStreak(uint8_t i) {
  // Move upward only (toward y=0). Wrap to bottom when passing top.
  yPos[i] -= 1;
  if (yPos[i] < 0) yPos[i] = ROWS - 1;
}

void setup() {
  leds.begin();
  buildXY();
  clearStrip0();
  leds.show();
}

void loop() {
  clearStrip0();

  // Draw all streaks at their current y
  for (uint8_t i = 0; i < STREAKS; i++) {
    drawVerticalTail(streakX[i], yPos[i]);
  }

  leds.show();

  // Advance each streak
  for (uint8_t i = 0; i < STREAKS; i++) stepStreak(i);

  delay(DELAY_MS);
}
