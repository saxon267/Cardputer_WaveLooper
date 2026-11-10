#pragma once
#include <stdint.h>

class AudioOut {
public:
    bool begin();
    void start();
    bool codecOk() const { return _codecOk; }
    uint8_t cpu() const { return _cpu; }
    uint32_t underruns() const { return _ur; }
    uint32_t blocks() const { return _blocks; }

private:
    static void taskEntry(void* arg);
    void taskLoop();

    bool     _codecOk = false;
    void*    _task = nullptr;
    volatile uint8_t  _cpu = 0;
    volatile uint32_t _ur = 0;
    volatile uint32_t _blocks = 0;
    int16_t  _last = 0;
};

extern AudioOut audioOut;
