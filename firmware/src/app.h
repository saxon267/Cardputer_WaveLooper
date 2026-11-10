#pragma once
#include "config.h"
#include "loop_audio.h"

enum class Screen : uint8_t { BOOT, LOOP, FILES, NO_SD };

struct App {
    Screen   screen = Screen::BOOT;
    LoopState loopState = LoopState::STOP;

    uint8_t  selectedLane = 0;
    uint8_t  bars = 1;
    uint8_t  layers = 4;
    uint16_t bpmX10 = 1200;
    bool     click = true;
    bool     countIn = true;
    bool     loopMode = false;
    bool     settings = false;
    uint8_t  settingsRow = 0;

    bool     usbOk = false;
    bool     usbStream = false;
    bool     sdOk = false;
    uint8_t  battery = 80;

    uint16_t inLevel = 0;
    uint16_t lanePeak[4] = {0, 0, 0, 0};
    bool     laneUsed[4] = {false, false, false, false};
    bool     laneMuted[4] = {false, false, false, false};
    uint16_t phasePermille = 0;
    uint16_t barNow = 1;
    float    loopSec = 2.0f;
    bool     hasLoop = false;
    bool     overdub = false;
    bool     takeFull = false;
    bool     invalid = false;

    char     toast[40] = {0};
    uint32_t toastUntil = 0;

    int      sel = 0;
    int      scroll = 0;
    bool     confirmDelete = false;

    bool     renaming = false;
    char     renameBuf[32] = {0};
    char     renameExt[8] = ".WAV";
    char     renameOld[40] = {0};

    char     lastSaved[64] = {0};
};

extern App app;
