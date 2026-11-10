#include "wav_store.h"

#include <Arduino.h>
#include <SD.h>
#include <string.h>

#include "config.h"
#include "loop_audio.h"

static void put16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

bool wavSaveMix(const LoopEngine& eng, const char* dir, char* outPath, int outLen)
{
    const uint32_t dataLen = eng.loopSamples() * 2u;
    char path[96] = {0};
    bool found = false;

    for (int i = 1; i <= 9999; ++i) {
        snprintf(path, sizeof(path), "%s/LOOP_%04d.WAV", dir, i);
        if (!SD.exists(path)) {
            found = true;
            break;
        }
    }
    if (!found) return false;

    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;

    uint8_t h[44] = {0};
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36u + dataLen);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16u);
    put16(h + 20, 1);                       // PCM
    put16(h + 22, 1);                       // mono
    put32(h + 24, ENGINE_RATE);
    put32(h + 28, ENGINE_RATE * 2u);        // byte rate
    put16(h + 32, 2);                       // block align
    put16(h + 34, 16);                      // bits
    memcpy(h + 36, "data", 4);
    put32(h + 40, dataLen);
    if (f.write(h, sizeof(h)) != sizeof(h)) {
        f.close();
        return false;
    }

    int16_t block[ENGINE_BLOCK];
    const uint32_t blocks = eng.loopBlocks();
    for (uint32_t b = 0; b < blocks; ++b) {
        eng.mixBlock(b, block);
        if (f.write((const uint8_t*)block, sizeof(block)) != sizeof(block)) {
            f.close();
            return false;
        }
        if ((b & 31u) == 0) yield();
    }
    f.close();

    if (outPath && outLen > 0) {
        strncpy(outPath, path, (size_t)outLen - 1);
        outPath[outLen - 1] = 0;
    }
    return true;
}
