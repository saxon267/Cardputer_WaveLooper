#include "usb_audio.h"

#include <Arduino.h>
#include <freertos/stream_buffer.h>
#include <string.h>

#include "USBAudioCard.h"
#include "config.h"

static USBAudioCard uac(48000, UAC_BPS_16, UAC_SPK_STEREO, UAC_MIC_NONE);
UsbAudio usbAudio;

bool UsbAudio::begin()
{
    StreamBufferHandle_t ring = xStreamBufferCreate(RING_SAMPLES * 2, 1);
    if (!ring) return false;
    _ring = (void*)ring;

    uac.onData(onDataStatic);
    _started = uac.begin();
    _lastDataMs = millis();
    return _started;
}

bool UsbAudio::streaming() const
{
    return _started && (uint32_t)(millis() - _lastDataMs) < 500u;
}

int UsbAudio::available() const
{
    if (!_ring) return 0;
    return (int)(xStreamBufferBytesAvailable((StreamBufferHandle_t)_ring) / 2);
}

int UsbAudio::read(int16_t* dst, int n)
{
    if (!dst || n <= 0) return 0;
    if (!_ring) {
        memset(dst, 0, (size_t)n * sizeof(int16_t));
        return 0;
    }
    const size_t got = xStreamBufferReceive((StreamBufferHandle_t)_ring, dst, (size_t)n * 2, 0) / 2;
    if (got < (size_t)n) {
        memset(dst + got, 0, ((size_t)n - got) * sizeof(int16_t));
        if (streaming()) ++_underruns;
    }
    return (int)got;
}

uint16_t UsbAudio::level()
{
    const uint16_t v = _inPeak;
    _inPeak = (uint16_t)(_inPeak - (_inPeak >> 3));
    return v;
}

void UsbAudio::reset()
{
    if (_ring) xStreamBufferReset((StreamBufferHandle_t)_ring);
}

void UsbAudio::onDataStatic(void* data, uint16_t len)
{
    usbAudio.onData(data, len);
}

void UsbAudio::onData(void* data, uint16_t len)
{
    if (!_ring || !data || len < 8) return;

    // Apply the host volume/mute before mixing down.
    uac.applyVolume(data, len);

    int16_t* p = (int16_t*)data;
    const int ns = (int)len / 2;
    int16_t mono[512];
    int m = 0;

    // 48 kHz stereo -> 24 kHz mono: average four samples (L,R,L,R).
    for (int i = 0; i + 3 < ns && m < 512; i += 4) {
        const int32_t sum = (int32_t)p[i] + p[i + 1] + p[i + 2] + p[i + 3];
        const int16_t s = (int16_t)(sum / 4);
        mono[m++] = s;
        const uint16_t a = (uint16_t)(s < 0 ? -s : s);
        if (a > _inPeak) _inPeak = a;
    }
    if (m <= 0) return;

    const size_t wrote = xStreamBufferSend((StreamBufferHandle_t)_ring, mono,
                                           (size_t)m * sizeof(int16_t), 0);
    if (wrote < (size_t)m * sizeof(int16_t)) ++_overruns;
    _lastDataMs = millis();
}
