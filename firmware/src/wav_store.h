#pragma once
#include <stdint.h>

class LoopEngine;

// Mixes all unmuted layers and writes a 16-bit mono WAV at ENGINE_RATE.
// Returns false if the SD card is missing or the file cannot be written.
bool wavSaveMix(const LoopEngine& eng, const char* dir, char* outPath, int outLen);
