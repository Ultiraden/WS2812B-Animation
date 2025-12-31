/*
  Teensy 4.0 + OctoWS2811
  Local-per-map XY LUT + Serial tools + EEPROM persistence
  + USB SERIAL HANDSHAKE (unique board ID discoverable by laptop)
  + SYNC COMMANDS (schedule actions to start together)
  + MAP1 ALT TOGGLE (right-side variant for map 1: 31 24 23 28 8)  [command name kept: map4alt]
  + MAP1 ALT PERSISTENCE in EEPROM (separate tiny record)
  + FLIP X ORIENTATION for the map that starts with 10 LEDs (map 7)

  NOTE:
    The command is still named "map4alt" for convenience, but it now affects MAP 1.

  ------------------------------------------------------------
  SET THESE PER BOARD (flash each Teensy with a unique BOARD_ID)
  ------------------------------------------------------------
    #define BOARD_ID "B1_BACK_LEFT"
    #define BOARD_ID "B2_BACK_RIGHT"
  ------------------------------------------------------------

  Serial: 115200, Newline

  Commands:
    DISCOVER?
    p
    map <mapId> <physLine>     (change mapping in RAM; then: rebuild)
    rebuild
    mapsave | mapload | mapclear
    q <mapId> <x> <yRow>
    dot <mapId> <x> <yRow>
    row0 <mapId>
    wave <mapId> [period] [speedMs]
    waveoff <mapId>
    waveall [period] [speedMs]
    waveclear
    stop

  Sync:
    sync row0 <mapId> <delayMs>
    sync wave <mapId> <delayMs> [period] [speedMs]
    sync waveall <delayMs> [period] [speedMs]
    sync stop <delayMs>

  Map1 alt toggle + persistence (command name is map4alt):
    map4alt        (toggle MAP 1 normal/alt + save to EEPROM)
    map4alt 0      (force MAP 1 normal + save)
    map4alt 1      (force MAP 1 alt + save)
    map4altsave    (save current flag)
    map4altload    (load flag from EEPROM + apply + rebuild)
    map4altclear   (clear flag record in EEPROM)
*/

#include <OctoWS2811.h>
#include <EEPROM.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

// ============================================================
// >>>> SET THESE PER BOARD <<<<
// ============================================================
#define BOARD_ID   "B3_FRONT_RIGHT"   // CHANGE THIS PER TEENSY
#define FW_VERSION "2025-12-29g"
#define CAPS       "row0,wave,waveall,stop,rebuild,mapsave,mapload,map4alt(map1),flipx,sync,discover"

// HELLO beacon period (ms)
static constexpr uint32_t HELLO_PERIOD_MS = 1500;

// =====================
// Hardware config
// =====================
static constexpr uint8_t  NUM_LINES     = 8;
static constexpr uint16_t LEDS_PER_LINE = 120;
static constexpr uint16_t TOTAL_LEDS    = NUM_LINES * LEDS_PER_LINE;

byte pinList[NUM_LINES] = {2, 14, 7, 8, 6, 20, 21, 5};

DMAMEM int displayMemory[TOTAL_LEDS * 3 / 4];
int drawingMemory[TOTAL_LEDS * 3 / 4];

const int config = WS2811_GRB | WS2811_800kHz;
OctoWS2811 leds(LEDS_PER_LINE, displayMemory, drawingMemory, config, NUM_LINES, pinList);

// =====================
// Map -> Physical remap
// =====================
static uint8_t mapToPhys[NUM_LINES] = {0,1,2,3,4,5,6,7};
static inline uint8_t physFromMap(uint8_t mapId) { return mapId < NUM_LINES ? mapToPhys[mapId] : 0; }

static inline void setDefaultMap() {
  for (uint8_t i = 0; i < NUM_LINES; i++) mapToPhys[i] = i;
}

// =====================
// Per-map local XY LUT
// =====================
static constexpr uint8_t MAX_ROWS = 16;
static constexpr uint8_t X_MAX    = 120;

static int16_t LUT[NUM_LINES][MAX_ROWS][X_MAX];
static uint8_t rowCount[NUM_LINES] = {0};
static uint8_t rowLen[NUM_LINES][MAX_ROWS];

// =====================
// Segment tags (no enum)
// =====================
static constexpr uint8_t TAG_V  = 0;
static constexpr uint8_t TAG_H  = 1;
static constexpr uint8_t TAG_HI = 2;

struct Segment {
  uint8_t len;
  uint8_t tag; // TAG_V / TAG_H / TAG_HI
};

static constexpr uint8_t MAX_SEGS = 16;
static uint8_t segCount[NUM_LINES] = {0};
static Segment segs[NUM_LINES][MAX_SEGS];

static const char* tagName(uint8_t t) {
  switch (t) {
    case TAG_V:  return "V";
    case TAG_H:  return "H";
    case TAG_HI: return "Hi";
    default:     return "?";
  }
}

// =====================
// X flip per map (map 7 starts with 10 LEDs, so flip it)
// =====================
static bool flipX[NUM_LINES] = {
  false, false, false, false, false, false, false,
  true  // map 7
};

// =====================
// Helpers
// =====================
static inline void clearAllPixels() {
  for (uint16_t i = 0; i < TOTAL_LEDS; i++) leds.setPixel(i, 0);
}
static inline void show() { leds.show(); }

static inline void clearLUT() {
  for (uint8_t m=0; m<NUM_LINES; m++)
    for (uint8_t y=0; y<MAX_ROWS; y++)
      for (uint8_t x=0; x<X_MAX; x++)
        LUT[m][y][x] = -1;

  for (uint8_t m=0; m<NUM_LINES; m++) rowCount[m] = 0;
}

// Return absolute LED index for local (map, x, yRow)
static inline int16_t lutIndex(uint8_t mapId, uint8_t x, uint8_t yRow) {
  if (mapId >= NUM_LINES) return -1;
  if (yRow >= rowCount[mapId]) return -1;
  if (x >= rowLen[mapId][yRow]) return -1;
  return LUT[mapId][yRow][x];
}

// =====================
// EEPROM persistence for mapToPhys (version + CRC)
// =====================
// Layout in EEPROM:
// [0]   magic/version byte
// [1]   length (should be NUM_LINES)
// [2..9] mapToPhys bytes (8)
// [10]  crc8 over bytes [0..9] (inclusive)
static constexpr uint8_t EEPROM_ADDR_BASE = 0;
static constexpr uint8_t EEPROM_MAGIC     = 0xA7;
static constexpr uint8_t EEPROM_LEN       = NUM_LINES;

static uint8_t crc8_simple(const uint8_t* data, uint8_t len) {
  uint8_t c = 0;
  for (uint8_t i = 0; i < len; i++) c ^= data[i];
  return c;
}

static void eeprom_write_mapping() {
  uint8_t buf[2 + NUM_LINES];
  buf[0] = EEPROM_MAGIC;
  buf[1] = EEPROM_LEN;
  for (uint8_t i = 0; i < NUM_LINES; i++) buf[2 + i] = mapToPhys[i];

  uint8_t crc = crc8_simple(buf, (uint8_t)sizeof(buf));

  EEPROM.update(EEPROM_ADDR_BASE + 0, buf[0]);
  EEPROM.update(EEPROM_ADDR_BASE + 1, buf[1]);
  for (uint8_t i = 0; i < NUM_LINES; i++) EEPROM.update(EEPROM_ADDR_BASE + 2 + i, buf[2 + i]);
  EEPROM.update(EEPROM_ADDR_BASE + 2 + NUM_LINES, crc);

  Serial.println("mapsave: wrote mapping to EEPROM (magic+len+crc).");
}

static bool eeprom_read_mapping(bool verbose) {
  uint8_t buf[2 + NUM_LINES];
  buf[0] = EEPROM.read(EEPROM_ADDR_BASE + 0);
  buf[1] = EEPROM.read(EEPROM_ADDR_BASE + 1);
  for (uint8_t i = 0; i < NUM_LINES; i++) buf[2 + i] = EEPROM.read(EEPROM_ADDR_BASE + 2 + i);
  uint8_t storedCrc = EEPROM.read(EEPROM_ADDR_BASE + 2 + NUM_LINES);

  uint8_t calcCrc = crc8_simple(buf, (uint8_t)sizeof(buf));

  if (buf[0] != EEPROM_MAGIC) {
    if (verbose) Serial.println("mapload: EEPROM magic mismatch (no saved mapping).");
    return false;
  }
  if (buf[1] != EEPROM_LEN) {
    if (verbose) Serial.println("mapload: EEPROM length mismatch.");
    return false;
  }
  if (storedCrc != calcCrc) {
    if (verbose) Serial.println("mapload: CRC mismatch (EEPROM data corrupted).");
    return false;
  }

  for (uint8_t i = 0; i < NUM_LINES; i++) {
    if (buf[2 + i] >= NUM_LINES) {
      if (verbose) Serial.println("mapload: invalid physLine value in EEPROM.");
      return false;
    }
  }

  for (uint8_t i = 0; i < NUM_LINES; i++) mapToPhys[i] = buf[2 + i];

  if (verbose) Serial.println("mapload: loaded mapping from EEPROM (valid CRC).");
  return true;
}

static void eeprom_clear_mapping() {
  EEPROM.update(EEPROM_ADDR_BASE + 0, 0x00);
  Serial.println("mapclear: cleared EEPROM magic (saved mapping removed).");
}

// =====================
// MAP 1 alt profile + persistence
// =====================
static bool map1AltRight = false;

static void applyMap1Profile() {
  if (!map1AltRight) {
    // NORMAL map 1: 31 30 29 30
    segCount[1] = 4;
    segs[1][0] = {31, TAG_V};
    segs[1][1] = {30, TAG_V};
    segs[1][2] = {29, TAG_V};
    segs[1][3] = {30, TAG_V};
  } else {
    // ALT RIGHT map 1: 31 24 23 28 8
    segCount[1] = 5;
    segs[1][0] = {31, TAG_V};
    segs[1][1] = {24, TAG_V};
    segs[1][2] = {23, TAG_V};
    segs[1][3] = {28, TAG_V};
    segs[1][4] = { 8, TAG_V};
  }
}

// EEPROM for map1AltRight:
// [base+0]=magic(0xB1), [base+1]=val(0/1), [base+2]=crc(magic^val)
static constexpr uint16_t EEPROM_MAP1_BASE  = EEPROM_ADDR_BASE + 32;
static constexpr uint8_t  EEPROM_MAP1_MAGIC = 0xB1;

static void eeprom_write_map1alt(bool v) {
  uint8_t val = v ? 1 : 0;
  uint8_t crc = (uint8_t)(EEPROM_MAP1_MAGIC ^ val);
  EEPROM.update(EEPROM_MAP1_BASE + 0, EEPROM_MAP1_MAGIC);
  EEPROM.update(EEPROM_MAP1_BASE + 1, val);
  EEPROM.update(EEPROM_MAP1_BASE + 2, crc);
}

static bool eeprom_read_map1alt(bool* outV, bool verbose) {
  uint8_t magic = EEPROM.read(EEPROM_MAP1_BASE + 0);
  uint8_t val   = EEPROM.read(EEPROM_MAP1_BASE + 1);
  uint8_t crc   = EEPROM.read(EEPROM_MAP1_BASE + 2);

  if (magic != EEPROM_MAP1_MAGIC) {
    if (verbose) Serial.println("map4altload: EEPROM magic mismatch (no saved map1alt).");
    return false;
  }
  if ((uint8_t)(magic ^ val) != crc) {
    if (verbose) Serial.println("map4altload: CRC mismatch (corrupt).");
    return false;
  }
  if (val > 1) {
    if (verbose) Serial.println("map4altload: invalid value.");
    return false;
  }
  *outV = (val == 1);
  if (verbose) Serial.println("map4altload: loaded map1alt from EEPROM.");
  return true;
}

static void eeprom_clear_map1alt() {
  EEPROM.update(EEPROM_MAP1_BASE + 0, 0x00);
  Serial.println("map4altclear: cleared EEPROM magic (saved map1alt removed).");
}

// =====================
// Load your segment lists
// =====================
static void loadSegments() {
  // map 0: 35 13 12 12 12 14 22
  segCount[0] = 7;
  segs[0][0] = {35, TAG_V}; segs[0][1] = {13, TAG_V}; segs[0][2] = {12, TAG_V};
  segs[0][3] = {12, TAG_V}; segs[0][4] = {12, TAG_V}; segs[0][5] = {14, TAG_V};
  segs[0][6] = {22, TAG_V};

  // map 1: normal vs alt (right side)
  applyMap1Profile();

  // map 2: 32 26 24 13 8 (short)
  segCount[2] = 5;
  segs[2][0] = {32, TAG_V}; segs[2][1] = {26, TAG_V}; segs[2][2] = {24, TAG_V}; segs[2][3] = {13, TAG_V}; segs[2][4] = { 8, TAG_V};

  // map 3: 108 12
  segCount[3] = 2;
  segs[3][0] = {108, TAG_V}; segs[3][1] = {12, TAG_V};

  // map 4: 97 13 10
  segCount[4] = 3;
  segs[4][0] = {97, TAG_V}; segs[4][1] = {13, TAG_V}; segs[4][2] = {10, TAG_V};

  // map 5: 60 30 30
  segCount[5] = 3;
  segs[5][0] = {60, TAG_V}; segs[5][1] = {30, TAG_V}; segs[5][2] = {30, TAG_V};

  // map 6: 18 20 21 32 29
  segCount[6] = 5;
  segs[6][0] = {18, TAG_V}; segs[6][1] = {20, TAG_V}; segs[6][2] = {21, TAG_V}; segs[6][3] = {32, TAG_V}; segs[6][4] = {29, TAG_V};

  // map 7: 10 10 8 10 10 10 10 15Hi 20H 4 3 4Hi
  segCount[7] = 12;
  segs[7][0]  = {10, TAG_V};
  segs[7][1]  = {10, TAG_V};
  segs[7][2]  = { 8, TAG_V};
  segs[7][3]  = {10, TAG_V};
  segs[7][4]  = {10, TAG_V};
  segs[7][5]  = {10, TAG_V};
  segs[7][6]  = {10, TAG_V};
  segs[7][7]  = {15, TAG_HI};
  segs[7][8]  = {20, TAG_H};
  segs[7][9]  = { 4, TAG_V};
  segs[7][10] = { 3, TAG_V};
  segs[7][11] = { 4, TAG_HI};
}

// =====================
// Build per-map local grids (WITH flipX support)
// =====================
static void buildLocalGrids() {
  clearLUT();

  for (uint8_t mapId = 0; mapId < NUM_LINES; mapId++) {
    uint8_t physLine = physFromMap(mapId);
    uint16_t base = (uint16_t)physLine * LEDS_PER_LINE;

    uint16_t p = 0;
    rowCount[mapId] = segCount[mapId];

    for (uint8_t row = 0; row < segCount[mapId]; row++) {
      uint8_t L = segs[mapId][row].len;
      rowLen[mapId][row] = L;

      const bool fx = flipX[mapId];

      if (!fx) {
        // normal serpentine
        if ((row % 2) == 0) {
          for (uint8_t x = 0; x < L && p < LEDS_PER_LINE; x++, p++) {
            LUT[mapId][row][x] = (int16_t)(base + p);
          }
        } else {
          for (uint8_t x = 0; x < L && p < LEDS_PER_LINE; x++, p++) {
            LUT[mapId][row][(uint8_t)(L - 1 - x)] = (int16_t)(base + p);
          }
        }
      } else {
        // flipped-x serpentine
        if ((row % 2) == 0) {
          for (uint8_t x = 0; x < L && p < LEDS_PER_LINE; x++, p++) {
            LUT[mapId][row][(uint8_t)(L - 1 - x)] = (int16_t)(base + p);
          }
        } else {
          for (uint8_t x = 0; x < L && p < LEDS_PER_LINE; x++, p++) {
            LUT[mapId][row][x] = (int16_t)(base + p);
          }
        }
      }
    }
  }
}

// =====================
// Handshake / HELLO beacon
// =====================
static uint32_t lastHelloMs = 0;

static void replyDiscover() {
  Serial.print("ID ");
  Serial.print(BOARD_ID);
  Serial.print(" FW ");
  Serial.print(FW_VERSION);
  Serial.print(" CAPS ");
  Serial.println(CAPS);
}

static void printHello(const char* prefix) {
  Serial.print(prefix);
  Serial.print(' ');
  Serial.print(BOARD_ID);
  Serial.print(' ');
  Serial.print(FW_VERSION);
  Serial.print(" CAPS ");
  Serial.println(CAPS);
}

static void helloTick() {
  uint32_t now = millis();
  if (now - lastHelloMs >= HELLO_PERIOD_MS) {
    lastHelloMs = now;
    printHello("HELLO");
  }
}

// =====================
// Sync scheduler (scheduleSync)
// =====================
enum SyncAction : uint8_t {
  SYNC_NONE = 0,
  SYNC_ROW0,
  SYNC_WAVE_ONE,
  SYNC_WAVE_ALL,
  SYNC_STOP
};

static void scheduleSync(SyncAction a, uint32_t delayMs);

static volatile SyncAction pendingAction = SYNC_NONE;
static volatile uint32_t pendingAtMs = 0;

static volatile int      pendingMapId = 0;
static volatile uint8_t  pendingPeriod = 30;
static volatile uint16_t pendingSpeedMs = 25;

static void scheduleSync(SyncAction a, uint32_t delayMs) {
  pendingAction = a;
  pendingAtMs = millis() + delayMs;
}

// =====================
// Multi-wave across local x
// =====================
static bool waveMode = false;
static uint8_t waveMask = 0;
static uint8_t wavePeriod = 30;
static uint16_t waveSpeedMs = 25;
static uint16_t wavePhase = 0;

static inline void clearAll() {
  for (uint16_t i = 0; i < TOTAL_LEDS; i++) leds.setPixel(i, 0);
}

static void renderWaves() {
  clearAll();

  const float twoPi = 6.28318530718f;
  const float maxLevel = 80.0f;

  for (uint8_t mapId = 0; mapId < NUM_LINES; mapId++) {
    if ((waveMask & (1u << mapId)) == 0) continue;

    uint16_t phase = wavePhase + (uint16_t)(mapId * 7);

    for (uint8_t x = 0; x < X_MAX; x++) {
      float t = (float)((x + (phase % wavePeriod))) / (float)wavePeriod;
      float s = (sinf(twoPi * t) + 1.0f) * 0.5f;
      uint8_t level = (uint8_t)(s * maxLevel);
      uint32_t c = ((uint32_t)level << 16) | ((uint32_t)level << 8) | level;

      for (uint8_t row = 0; row < rowCount[mapId]; row++) {
        if (x >= rowLen[mapId][row]) continue;
        int16_t idx = LUT[mapId][row][x];
        if (idx >= 0) leds.setPixel((uint16_t)idx, c);
      }
    }
  }

  show();
}

// =====================
// Commands
// =====================
static void clearAndOff() {
  waveMode = false;
  waveMask = 0;
  clearAllPixels();
  show();
}

static void cmd_map(int mapId, int physLine) {
  if (mapId < 0 || mapId >= (int)NUM_LINES || physLine < 0 || physLine >= (int)NUM_LINES) {
    Serial.println("Usage: map <mapId 0..7> <physLine 0..7>");
    return;
  }
  mapToPhys[(uint8_t)mapId] = (uint8_t)physLine;
  Serial.print("map "); Serial.print(mapId);
  Serial.print(" -> phys "); Serial.println(physLine);
  Serial.println("Now run: rebuild (and mapsave if you want it persistent)");
}

static void cmd_q(int mapId, int x, int yRow) {
  int16_t idx = lutIndex((uint8_t)mapId, (uint8_t)x, (uint8_t)yRow);
  Serial.print("q map="); Serial.print(mapId);
  Serial.print(" x="); Serial.print(x);
  Serial.print(" yRow="); Serial.print(yRow);
  Serial.print(" -> idx="); Serial.println(idx);
}

static void cmd_dot(int mapId, int x, int yRow) {
  clearAndOff();
  int16_t idx = lutIndex((uint8_t)mapId, (uint8_t)x, (uint8_t)yRow);
  if (idx < 0) { Serial.println("dot: invalid/empty"); return; }
  leds.setPixel((uint16_t)idx, 0x202020);
  show();
  Serial.print("dot lit idx="); Serial.println(idx);
}

static void cmd_row0(int mapId) {
  if (mapId < 0 || mapId >= (int)NUM_LINES) {
    Serial.println("Usage: row0 <mapId 0..7>");
    return;
  }
  if (rowCount[mapId] == 0) {
    Serial.println("row0: LUT not built yet. Run: rebuild");
    return;
  }

  clearAndOff();

  uint8_t L = rowLen[mapId][0];
  Serial.print("row0 map "); Serial.print(mapId);
  Serial.print(" length="); Serial.println(L);

  for (uint8_t x = 0; x < L; x++) {
    int16_t idx = lutIndex((uint8_t)mapId, x, 0);
    if (idx >= 0) leds.setPixel((uint16_t)idx, 0x202020);
  }
  show();
}

static void cmd_wave_on(int mapId, int periodOpt, int speedOpt, bool hasP, bool hasS) {
  if (mapId < 0 || mapId >= (int)NUM_LINES) { Serial.println("Usage: wave <mapId 0..7> [period] [speedMs]"); return; }
  if (hasP && periodOpt >= 4 && periodOpt <= 120) wavePeriod = (uint8_t)periodOpt;
  if (hasS && speedOpt  >= 1 && speedOpt  <= 2000) waveSpeedMs = (uint16_t)speedOpt;

  waveMask |= (1u << (uint8_t)mapId);
  waveMode = (waveMask != 0);

  Serial.print("wave ON map "); Serial.print(mapId);
  Serial.print(" mask=0b");
  for (int i=NUM_LINES-1; i>=0; i--) Serial.print((waveMask >> i) & 1);
  Serial.println();
}

static void cmd_wave_off(int mapId) {
  if (mapId < 0 || mapId >= (int)NUM_LINES) { Serial.println("Usage: waveoff <mapId 0..7>"); return; }
  waveMask &= ~(1u << (uint8_t)mapId);
  waveMode = (waveMask != 0);
  Serial.print("wave OFF map "); Serial.println(mapId);
}

static void cmd_wave_all(int periodOpt, int speedOpt, bool hasP, bool hasS) {
  if (hasP && periodOpt >= 4 && periodOpt <= 120) wavePeriod = (uint8_t)periodOpt;
  if (hasS && speedOpt  >= 1 && speedOpt  <= 2000) waveSpeedMs = (uint16_t)speedOpt;

  waveMask = 0xFFu >> (8 - NUM_LINES);
  waveMode = true;
  Serial.println("wave ALL ON");
}

static void cmd_wave_clear() {
  waveMask = 0;
  waveMode = false;
  Serial.println("wave cleared");
}

static void printStatus() {
  Serial.println("\n=== identity ===");
  replyDiscover();

  Serial.print("\nmap1 profile (controlled by map4alt): ");
  Serial.println(map1AltRight ? "ALT RIGHT (31 24 23 28 8)" : "NORMAL (31 30 29 30)");

  Serial.print("flipX map7: ");
  Serial.println(flipX[7] ? "ON" : "OFF");

  Serial.println("\n=== map -> physLine ===");
  for (uint8_t m=0; m<NUM_LINES; m++) {
    Serial.print("map "); Serial.print(m);
    Serial.print(" -> phys "); Serial.println(mapToPhys[m]);
  }

  Serial.println("\n=== per-map rows (length + tag) ===");
  for (uint8_t m=0; m<NUM_LINES; m++) {
    Serial.print("map "); Serial.print(m);
    Serial.print(" rows="); Serial.print(rowCount[m]);
    Serial.print(" : ");
    for (uint8_t r=0; r<rowCount[m]; r++) {
      Serial.print((int)rowLen[m][r]);
      Serial.print(tagName(segs[m][r].tag));
      if (r+1<rowCount[m]) Serial.print(" ");
    }
    Serial.println();
  }

  Serial.print("\nwaveMask=0b");
  for (int i=NUM_LINES-1; i>=0; i--) Serial.print((waveMask >> i) & 1);
  Serial.print(" period="); Serial.print(wavePeriod);
  Serial.print(" speedMs="); Serial.println(waveSpeedMs);

  Serial.println("\nCommands:");
  Serial.println("  DISCOVER?");
  Serial.println("  p");
  Serial.println("  map <mapId> <physLine>   (then: rebuild)");
  Serial.println("  rebuild");
  Serial.println("  mapsave | mapload | mapclear");
  Serial.println("  map4alt [0|1]  (TOGGLES MAP 1 + autosave)");
  Serial.println("  map4altsave | map4altload | map4altclear");
  Serial.println("  q <mapId> <x> <yRow>");
  Serial.println("  dot <mapId> <x> <yRow>");
  Serial.println("  row0 <mapId>");
  Serial.println("  wave <mapId> [period] [speedMs]");
  Serial.println("  waveoff <mapId>");
  Serial.println("  waveall [period] [speedMs]");
  Serial.println("  waveclear");
  Serial.println("  stop");
  Serial.println("  sync row0 <mapId> <delayMs>");
  Serial.println("  sync wave <mapId> <delayMs> [period] [speedMs]");
  Serial.println("  sync waveall <delayMs> [period] [speedMs]");
  Serial.println("  sync stop <delayMs>");
}

// =====================
// Sync executor
// =====================
static void syncTick() {
  if (pendingAction == SYNC_NONE) return;
  if ((int32_t)(millis() - pendingAtMs) < 0) return;

  SyncAction a = pendingAction;
  pendingAction = SYNC_NONE;

  if (a == SYNC_STOP) {
    clearAndOff();
    Serial.println("sync: executed STOP");
    return;
  }

  wavePhase = 0;

  if (a == SYNC_ROW0) {
    cmd_row0(pendingMapId);
    Serial.println("sync: executed ROW0");
    return;
  }
  if (a == SYNC_WAVE_ONE) {
    cmd_wave_on(pendingMapId, (int)pendingPeriod, (int)pendingSpeedMs, true, true);
    Serial.println("sync: executed WAVE one");
    return;
  }
  if (a == SYNC_WAVE_ALL) {
    cmd_wave_all((int)pendingPeriod, (int)pendingSpeedMs, true, true);
    Serial.println("sync: executed WAVE ALL");
    return;
  }
}

// =====================
// Serial parsing
// =====================
static char sbuf[160];
static uint8_t spos = 0;

static void toLowerInPlace(char* s) {
  while (*s) { *s = (char)tolower((unsigned char)*s); s++; }
}

static void handleLine(char *s) {
  if (!s || !*s) return;

  toLowerInPlace(s);

  if (strcmp(s, "discover?") == 0 || strcmp(s, "whoami") == 0) { replyDiscover(); return; }
  if (strcmp(s, "p") == 0) { printStatus(); return; }
  if (strcmp(s, "stop") == 0) { clearAndOff(); Serial.println("stopped/cleared"); return; }
  if (strcmp(s, "rebuild") == 0) { buildLocalGrids(); Serial.println("rebuilt LUT"); return; }

  if (strcmp(s, "mapsave") == 0) { eeprom_write_mapping(); return; }
  if (strcmp(s, "mapload") == 0) {
    bool ok = eeprom_read_mapping(true);
    if (ok) Serial.println("mapload: now run rebuild to apply to LUT.");
    return;
  }
  if (strcmp(s, "mapclear") == 0) {
    eeprom_clear_mapping();
    setDefaultMap();
    Serial.println("mapclear: RAM mapping reset to defaults; now run rebuild.");
    return;
  }

  // map1alt persistence commands (still named map4alt*)
  if (strcmp(s, "map4altsave") == 0) {
    eeprom_write_map1alt(map1AltRight);
    Serial.print("map4altsave: saved "); Serial.println(map1AltRight ? "ALT (map1)" : "NORMAL (map1)");
    return;
  }
  if (strcmp(s, "map4altload") == 0) {
    bool v = false;
    if (eeprom_read_map1alt(&v, true)) {
      map1AltRight = v;
      applyMap1Profile();
      buildLocalGrids();
      Serial.print("map4altload applied -> ");
      Serial.println(map1AltRight ? "ALT RIGHT map1 (31 24 23 28 8)" : "NORMAL map1 (31 30 29 30)");
    }
    return;
  }
  if (strcmp(s, "map4altclear") == 0) { eeprom_clear_map1alt(); return; }

  // map4alt toggle/set (AUTOSAVE) -> affects MAP 1
  if (strcmp(s, "map4alt") == 0) {
    map1AltRight = !map1AltRight;
    eeprom_write_map1alt(map1AltRight);
    applyMap1Profile();
    buildLocalGrids();
    Serial.print("map4alt toggled -> ");
    Serial.println(map1AltRight ? "ALT RIGHT map1 (31 24 23 28 8)" : "NORMAL map1 (31 30 29 30)");
    return;
  }
  int v;
  if (sscanf(s, "map4alt %d", &v) == 1) {
    map1AltRight = (v != 0);
    eeprom_write_map1alt(map1AltRight);
    applyMap1Profile();
    buildLocalGrids();
    Serial.print("map4alt set -> ");
    Serial.println(map1AltRight ? "ALT RIGHT map1 (31 24 23 28 8)" : "NORMAL map1 (31 30 29 30)");
    return;
  }

  if (strcmp(s, "waveclear") == 0) { cmd_wave_clear(); return; }

  int a,b,c;
  if (sscanf(s, "map %d %d", &a, &b) == 2) { cmd_map(a,b); return; }
  if (sscanf(s, "q %d %d %d", &a, &b, &c) == 3) { cmd_q(a,b,c); return; }
  if (sscanf(s, "dot %d %d %d", &a, &b, &c) == 3) { cmd_dot(a,b,c); return; }
  if (sscanf(s, "row0 %d", &a) == 1 && strncmp(s, "row0", 4) == 0) { cmd_row0(a); return; }
  if (sscanf(s, "waveoff %d", &a) == 1 && strncmp(s, "waveoff", 7) == 0) { cmd_wave_off(a); return; }

  int n_all = sscanf(s, "waveall %d %d", &a, &b);
  if (strncmp(s, "waveall", 7) == 0) { cmd_wave_all(a, b, (n_all>=1), (n_all>=2)); return; }

  int n = sscanf(s, "wave %d %d %d", &a, &b, &c);
  if (strncmp(s, "wave", 4) == 0 && n >= 1) { cmd_wave_on(a, b, c, (n>=2), (n>=3)); return; }

  // sync
  if (strncmp(s, "sync ", 5) == 0) {
    char mode[16] = {0};
    int p1=0, p2=0, p3=0, p4=0;

    int nTok = sscanf(s, "sync %15s %d %d %d %d", mode, &p1, &p2, &p3, &p4);
    if (nTok < 2) { Serial.println("ERR sync usage."); return; }

    if (strcmp(mode, "stop") == 0) {
      uint32_t delayMs = (uint32_t)((nTok >= 3) ? max(0, p1) : 0);
      scheduleSync(SYNC_STOP, delayMs);
      Serial.print("OK sync stop delay="); Serial.println(delayMs);
      return;
    }
    if (strcmp(mode, "row0") == 0) {
      if (nTok < 4) { Serial.println("Usage: sync row0 <mapId> <delayMs>"); return; }
      pendingMapId = p1;
      scheduleSync(SYNC_ROW0, (uint32_t)max(0, p2));
      Serial.print("OK sync row0 map="); Serial.print(p1);
      Serial.print(" delay="); Serial.println(max(0, p2));
      return;
    }
    if (strcmp(mode, "waveall") == 0) {
      if (nTok < 3) { Serial.println("Usage: sync waveall <delayMs> [period] [speedMs]"); return; }
      uint32_t delayMs = (uint32_t)max(0, p1);
      uint8_t period = wavePeriod;
      uint16_t speed = waveSpeedMs;
      if (nTok >= 4 && p2 >= 4 && p2 <= 120) period = (uint8_t)p2;
      if (nTok >= 5 && p3 >= 1 && p3 <= 2000) speed = (uint16_t)p3;

      pendingPeriod = period;
      pendingSpeedMs = speed;
      scheduleSync(SYNC_WAVE_ALL, delayMs);

      Serial.print("OK sync waveall delay="); Serial.print(delayMs);
      Serial.print(" period="); Serial.print(period);
      Serial.print(" speedMs="); Serial.println(speed);
      return;
    }
    if (strcmp(mode, "wave") == 0) {
      if (nTok < 4) { Serial.println("Usage: sync wave <mapId> <delayMs> [period] [speedMs]"); return; }
      int mapId = p1;
      uint32_t delayMs = (uint32_t)max(0, p2);
      uint8_t period = wavePeriod;
      uint16_t speed = waveSpeedMs;
      if (nTok >= 5 && p3 >= 4 && p3 <= 120) period = (uint8_t)p3;
      if (nTok >= 6 && p4 >= 1 && p4 <= 2000) speed = (uint16_t)p4;

      pendingMapId = mapId;
      pendingPeriod = period;
      pendingSpeedMs = speed;
      scheduleSync(SYNC_WAVE_ONE, delayMs);

      Serial.print("OK sync wave map="); Serial.print(mapId);
      Serial.print(" delay="); Serial.print(delayMs);
      Serial.print(" period="); Serial.print(period);
      Serial.print(" speedMs="); Serial.println(speed);
      return;
    }

    Serial.println("ERR sync mode unknown. Use: row0 | wave | waveall | stop");
    return;
  }

  Serial.println("Unknown. Try: p | map m l | rebuild | mapsave/mapload/mapclear | map4alt | q/dot | row0 | wave* | stop | DISCOVER? | sync ...");
}

static void pollSerial() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      sbuf[spos] = '\0';
      handleLine(sbuf);
      spos = 0;
    } else {
      if (spos < sizeof(sbuf)-1) sbuf[spos++] = ch;
    }
  }
}

// =====================
// Setup / Loop
// =====================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 1500) {}

  leds.begin();
  clearAllPixels();
  show();

  // Load persisted MAP1 alt flag FIRST so segments build correctly
  bool map1Loaded = false;
  bool map1v = false;
  map1Loaded = eeprom_read_map1alt(&map1v, false);
  if (map1Loaded) map1AltRight = map1v;

  loadSegments();

  // Try EEPROM autoload for mapping
  bool loaded = eeprom_read_mapping(false);
  if (!loaded) setDefaultMap();

  buildLocalGrids();

  Serial.println("\nLocal-per-map XY LUT ready.");
  if (loaded) Serial.println("Boot: loaded mapToPhys from EEPROM (valid CRC).");
  else        Serial.println("Boot: using default mapToPhys (EEPROM empty/invalid).");

  if (map1Loaded) {
    Serial.print("Boot: loaded map1alt from EEPROM -> ");
    Serial.println(map1AltRight ? "ALT RIGHT" : "NORMAL");
  } else {
    Serial.println("Boot: map1alt EEPROM empty/invalid; using current default (NORMAL).");
  }

  printHello("HELLO");
  replyDiscover();
  printStatus();
}

void loop() {
  pollSerial();
  helloTick();
  syncTick();

  if (waveMode) {
    renderWaves();
    wavePhase++;
    delay(waveSpeedMs);
  }
}
