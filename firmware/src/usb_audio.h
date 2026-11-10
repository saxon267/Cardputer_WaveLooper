#pragma once
#include <stdint.h>

// UAC1 speaker device (48 kHz, 16-bit stereo) -> mono 24 kHz ring.
class UsbAudio {
public:
    bool begin();
    bool started() const { return _started; }
    bool streaming() const;
    int  available() const;
    int  read(int16_t* dst, int n);   // pads with silence on underrun
    uint16_t level();                  // input peak, decays per read
    uint32_t overruns() const { return _overruns; }
    uint32_t underruns() const { return _underruns; }
    void reset();

private:
    static void onDataStatic(void* data, uint16_t len);
    void onData(void* data, uint16_t len);

    void*  _ring = nullptr;
    bool   _started = false;
    uint32_t _lastDataMs = 0;
    uint32_t _overruns = 0;
    uint32_t _underruns = 0;
    uint16_t _inPeak = 0;
};

extern UsbAudio usbAudio;
