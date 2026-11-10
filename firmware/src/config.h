#pragma once
#include <stdint.h>

// ---------------------------------------------------------------- hardware
// Cardputer-Adv (ESP32-S3FN8, 8 MB flash, no PSRAM).  Same pins as MidiLooper.
#define PIN_I2C_SDA   8
#define PIN_I2C_SCL   9
#define PIN_I2S_BCLK  41
#define PIN_I2S_LRCK  43
#define PIN_I2S_DOUT  42
#define PIN_SD_SCK    40
#define PIN_SD_MISO   39
#define PIN_SD_MOSI   14
#define PIN_SD_CS     12
#define PIN_TCA_INT   11

// ---------------------------------------------------------------- audio
// USB audio and I2S run at 48 kHz; loops are stored at 24 kHz (2:1 decimation,
// half the memory).  One engine block is 240 samples = 10 ms at 24 kHz.
static constexpr uint32_t AUDIO_RATE   = 48000;
static constexpr uint32_t ENGINE_RATE  = 24000;
static constexpr int      AUDIO_FRAMES = 480;   // 10 ms at 48 kHz
static constexpr int      ENGINE_BLOCK = 240;   // 10 ms at 24 kHz
static constexpr int      RING_SAMPLES = 4096;  // 170 ms capacity
static constexpr int      RING_TARGET  = 576;   // 24 ms prefill
static constexpr int      CLICK_MS     = 28;

// ---------------------------------------------------------------- loop
static constexpr int MAX_LAYERS  = 4;
static constexpr int MAX_BARS    = 4;
static constexpr int POOL_BYTES  = 128 * 1024;  // 4 layers x 1 bar @120 BPM = 99.2 KB
static constexpr int ADPCM_SAMPLES = 240;       // one IMA ADPCM block
static constexpr int ADPCM_BYTES   = 124;       // 4-byte header + 120 bytes payload

// ---------------------------------------------------------------- files
static constexpr int MAX_FILES = 64;
#define LOOP_DIR "/WaveLoop"
#define APP_VERSION "0.1.1"

// ---------------------------------------------------------------- layout
static constexpr int SCR_W = 240;
static constexpr int SCR_H = 135;
static constexpr int TOP_H = 13;
static constexpr int BOT_H = 11;
static constexpr int BOT_Y = SCR_H - BOT_H;

// ---------------------------------------------------------------- Wave Blue
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define C_BG      RGB565(10, 21, 34)     // #0A1522
#define C_BAR     RGB565(16, 30, 47)     // #101E2F
#define C_PANEL   RGB565(20, 38, 58)     // #14263A
#define C_PAD     RGB565(26, 48, 74)     // #1A304A
#define C_EDGE    RGB565(42, 74, 107)    // #2A4A6B
#define C_LINE    RGB565(31, 53, 80)     // #1F3550
#define C_TXT     RGB565(230, 241, 251)  // #E6F1FB
#define C_LT2     RGB565(195, 217, 236)  // #C3D9EC
#define C_DIM     RGB565(139, 168, 196)  // #8BA8C4
#define C_FAINT   RGB565(92, 122, 151)   // #5C7A97
#define C_ACC     RGB565(77, 163, 255)   // #4DA3FF
#define C_ACC_DIM RGB565(18, 58, 94)     // #123A5E
#define C_AMB     RGB565(255, 200, 87)   // #FFC857
#define C_AQUA    RGB565(69, 224, 200)   // #45E0C8
#define C_BLUE    RGB565(127, 178, 255)  // #7FB2FF
#define C_PURPLE  RGB565(167, 139, 250)  // #A78BFA
#define C_RED     RGB565(255, 93, 108)   // #FF5D6C
#define C_GREEN   RGB565(63, 221, 142)   // #3FDD8E
