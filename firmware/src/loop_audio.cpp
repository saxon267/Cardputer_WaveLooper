#include "loop_audio.h"

#include <math.h>
#include <string.h>

LoopEngine looper;

// ---------------------------------------------------------------- IMA ADPCM
// 4-bit IMA ADPCM, 240 samples per block: 4-byte header (predictor + step
// index) followed by 120 bytes of nibbles.  Each block is self-contained, so
// playback can start at any block of the loop.
static const int16_t kStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

static const int8_t kIndex[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

static inline int16_t clamp16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static int16_t adpcmDecode(int32_t& pred, int32_t& idx, uint8_t nib)
{
    const int32_t step = kStep[idx];
    int32_t delta = step >> 3;
    if (nib & 4) delta += step;
    if (nib & 2) delta += step >> 1;
    if (nib & 1) delta += step >> 2;
    pred += (nib & 8) ? -delta : delta;
    pred = clamp16(pred);
    idx += kIndex[nib & 7];
    if (idx < 0) idx = 0;
    if (idx > 88) idx = 88;
    return (int16_t)pred;
}

static uint8_t adpcmEncode(int32_t& pred, int32_t& idx, int16_t sample)
{
    const int32_t step = kStep[idx];
    int32_t diff = (int32_t)sample - pred;
    uint8_t nib = 0;
    if (diff < 0) {
        nib = 8;
        diff = -diff;
    }
    int32_t delta = step >> 3;
    if (diff >= step) {
        nib |= 4;
        diff -= step;
        delta += step;
    }
    if (diff >= (step >> 1)) {
        nib |= 2;
        diff -= step >> 1;
        delta += step >> 1;
    }
    if (diff >= (step >> 2)) {
        nib |= 1;
        delta += step >> 2;
    }
    pred += (nib & 8) ? -delta : delta;
    pred = clamp16(pred);
    idx += kIndex[nib & 7];
    if (idx < 0) idx = 0;
    if (idx > 88) idx = 88;
    return nib;
}

// ---------------------------------------------------------------- lifecycle
bool LoopEngine::begin(uint8_t* pool, int poolBytes)
{
    _pool = pool;
    _poolBytes = poolBytes;
    _bars = 1;
    _layers = MAX_LAYERS;
    _bpmX10 = 1200;
    _countIn = true;
    _clickOn = true;
    _loopMode = false;
    _clickVol = 80;
    initClick();
    recompute();
    clear();
    return !_invalid;
}

void LoopEngine::initClick()
{
    _clickLen = (int)((uint64_t)ENGINE_RATE * CLICK_MS / 1000);
    if (_clickLen > (int)(sizeof(_accent) / sizeof(_accent[0]))) {
        _clickLen = (int)(sizeof(_accent) / sizeof(_accent[0]));
    }
    if (_clickLen <= 0) return;

    const float dur = _clickLen / (float)ENGINE_RATE;
    const float attack = 0.002f;
    const float tail = 0.004f;

    auto render = [&](int16_t* dst, float freq, float gain) {
        for (int i = 0; i < _clickLen; ++i) {
            const float t = i / (float)ENGINE_RATE;
            float env = expf(-t * 130.0f);
            if (t < attack) env *= t / attack;
            const float left = dur - t;
            if (left < tail) env *= left / tail;
            if (env < 0.0f) env = 0.0f;
            float v = sinf(6.2831853f * freq * t) * env * gain;
            if (v > 20000.0f) v = 20000.0f;
            if (v < -20000.0f) v = -20000.0f;
            dst[i] = (int16_t)v;
        }
    };

    render(_accent, 2000.0f, 15000.0f);
    render(_normal, 1350.0f, 9500.0f);
}

void LoopEngine::applyLayout()
{
    _layerStride = (int)(_loopBlocks * ADPCM_BYTES);
}

uint32_t LoopEngine::blocksPerBar() const
{
    // 4/4, 24 kHz, 240-sample blocks: 240000 / bpmX10 blocks per bar.
    const uint32_t b = (240000u + _bpmX10 / 2u) / _bpmX10;
    return b ? b : 1;
}

uint32_t LoopEngine::blocksForBars(uint8_t bars) const
{
    return (uint32_t)bars * blocksPerBar();
}

bool LoopEngine::capacityOk(uint8_t layers, uint8_t bars) const
{
    if (layers < 1 || bars < 1) return false;
    const uint32_t need = (uint32_t)layers * blocksForBars(bars) * ADPCM_BYTES;
    return need <= (uint32_t)_poolBytes;
}

uint8_t LoopEngine::maxBars(uint8_t layers) const
{
    for (int b = MAX_BARS; b >= 1; --b) {
        if (capacityOk(layers, (uint8_t)b)) return (uint8_t)b;
    }
    return 0;
}

uint8_t LoopEngine::maxLayers(uint8_t bars) const
{
    for (int l = MAX_LAYERS; l >= 1; --l) {
        if (capacityOk((uint8_t)l, bars)) return (uint8_t)l;
    }
    return 0;
}

void LoopEngine::recompute()
{
    if (_bars < 1) _bars = 1;
    if (_bars > MAX_BARS) _bars = MAX_BARS;
    if (_layers < 1) _layers = 1;
    if (_layers > MAX_LAYERS) _layers = MAX_LAYERS;

    for (;;) {
        _loopBlocks = blocksForBars(_bars);
        if ((uint32_t)_layers * _loopBlocks * ADPCM_BYTES <= (uint32_t)_poolBytes) {
            _invalid = false;
            break;
        }
        if (_bars > 1) { --_bars; continue; }
        if (_layers > 1) { --_layers; continue; }
        _invalid = true;
        break;
    }
    applyLayout();
    if (_phaseBlock >= _loopBlocks) _phaseBlock = 0;
}

bool LoopEngine::setBpm(uint16_t bpmX10)
{
    if (_usedCount > 0) return false;
    if (bpmX10 < 400) bpmX10 = 400;
    if (bpmX10 > 4000) bpmX10 = 4000;
    _bpmX10 = bpmX10;
    recompute();
    _phaseBlock = 0;
    return true;
}

bool LoopEngine::setBars(uint8_t bars)
{
    if (_usedCount > 0) return false;
    _bars = bars;
    recompute();
    _phaseBlock = 0;
    return true;
}

bool LoopEngine::setLayers(uint8_t layers)
{
    if (_usedCount > 0) return false;
    _layers = layers;
    recompute();
    _phaseBlock = 0;
    return true;
}

void LoopEngine::setClick(bool on)
{
    _clickOn = on;
    if (!on) _clickActive = false;
}

// ---------------------------------------------------------------- transport
int LoopEngine::firstFreeLayer() const
{
    for (int i = 0; i < _layers; ++i) {
        if (!_layer[i].used) return i;
    }
    return -1;
}

bool LoopEngine::startRec()
{
    if (_state == LoopState::REC || _state == LoopState::ARMED) return false;
    if (_invalid) return false;

    const int l = firstFreeLayer();
    if (l < 0) return false;

    _recLayer = (uint8_t)l;
    _firstTake = (_usedCount == 0);

    if (_firstTake) {
        _state = LoopState::ARMED;
        _phaseBlock = 0;
        _lastBeat = 0xFFFFFFFFu;
        _armBlocks = _countIn ? blocksPerBar() : 0;
        _armBeatBlocks = blocksPerBar() / 4;
        if (_armBeatBlocks == 0) _armBeatBlocks = 1;
        if (_countIn) {
            triggerClick(true);
        } else {
            beginRecord(_recLayer);
        }
    } else {
        _state = LoopState::ARMED;
        if (_phaseBlock == 0) beginRecord(_recLayer);
    }
    return true;
}

void LoopEngine::beginRecord(uint8_t layer)
{
    Layer& L = _layer[layer];
    L.used = true;
    L.muted = false;
    L.validBlocks = 0;
    L.pred = 0;
    L.idx = 0;
    L.peak = 0;
    ++_usedCount;
    _recLayer = layer;
    _recBlock = 0;
    _phaseBlock = 0;
    _lastBeat = 0xFFFFFFFFu;
    _state = LoopState::REC;
}

void LoopEngine::commitRecord()
{
    Layer& L = _layer[_recLayer];
    if (_recBlock == 0) {
        cancelRecord();
        return;
    }
    L.validBlocks = _recBlock;
    if (_orderN < MAX_LAYERS) _order[_orderN++] = _recLayer;
    _state = LoopState::PLAY;
    _phaseBlock = 0;
    _lastBeat = 0xFFFFFFFFu;
    if (_loopMode && firstFreeLayer() >= 0) _autoArm = true;
}

void LoopEngine::cancelRecord()
{
    Layer& L = _layer[_recLayer];
    L.used = false;
    L.muted = false;
    L.validBlocks = 0;
    L.peak = 0;
    if (_usedCount > 0) --_usedCount;
    _state = (_usedCount > 0) ? LoopState::PLAY : LoopState::STOP;
    _phaseBlock = 0;
    _recBlock = 0;
}

void LoopEngine::stopRec()
{
    if (_state == LoopState::ARMED) {
        _state = LoopState::STOP;
        _phaseBlock = 0;
        _firstTake = false;
        return;
    }
    if (_state != LoopState::REC) return;
    if (_firstTake) {
        cancelRecord();
        _state = LoopState::STOP;
    } else {
        commitRecord();
    }
    _firstTake = false;
}

bool LoopEngine::play()
{
    if (_usedCount == 0) return false;
    _state = LoopState::PLAY;
    _phaseBlock = 0;
    _lastBeat = 0xFFFFFFFFu;
    _clickActive = false;
    return true;
}

void LoopEngine::stop()
{
    _state = LoopState::STOP;
    _phaseBlock = 0;
    _firstTake = false;
    _autoArm = false;
    _clickActive = false;
}

void LoopEngine::clear()
{
    for (int i = 0; i < MAX_LAYERS; ++i) {
        _layer[i].used = false;
        _layer[i].muted = false;
        _layer[i].validBlocks = 0;
        _layer[i].peak = 0;
    }
    _orderN = 0;
    _usedCount = 0;
    _state = LoopState::STOP;
    _phaseBlock = 0;
    _recBlock = 0;
    _firstTake = false;
    _autoArm = false;
    _clickActive = false;
    _lastBeat = 0xFFFFFFFFu;
}

bool LoopEngine::undo()
{
    if (_state == LoopState::ARMED) {
        stopRec();
        return true;
    }
    if (_state == LoopState::REC) {
        cancelRecord();
        return true;
    }
    if (_orderN == 0) return false;

    --_orderN;
    const uint8_t l = _order[_orderN];
    _layer[l].used = false;
    _layer[l].muted = false;
    _layer[l].validBlocks = 0;
    _layer[l].peak = 0;
    if (_usedCount > 0) --_usedCount;
    _state = (_usedCount > 0) ? LoopState::PLAY : LoopState::STOP;
    _phaseBlock = 0;
    return true;
}

bool LoopEngine::clearLayer(uint8_t layer)
{
    if (layer >= MAX_LAYERS) return false;

    // Clearing the track that is currently armed/recording cancels that take.
    if (_recLayer == layer &&
        (_state == LoopState::REC || _state == LoopState::ARMED)) {
        if (_state == LoopState::ARMED) {
            _state = LoopState::STOP;
            _phaseBlock = 0;
            _firstTake = false;
        } else {
            cancelRecord();
        }
        return true;
    }

    if (!_layer[layer].used) return false;

    _layer[layer].used = false;
    _layer[layer].muted = false;
    _layer[layer].validBlocks = 0;
    _layer[layer].peak = 0;

    // Drop it from the undo order as well.
    int w = 0;
    for (int i = 0; i < _orderN; ++i) {
        if (_order[i] != layer) _order[w++] = _order[i];
    }
    _orderN = (uint8_t)w;

    if (_usedCount > 0) --_usedCount;
    if (_usedCount == 0) {
        _state = LoopState::STOP;
        _phaseBlock = 0;
    }
    return true;
}

bool LoopEngine::toggleMute(uint8_t layer)
{
    if (layer >= MAX_LAYERS || !_layer[layer].used) return false;
    _layer[layer].muted = !_layer[layer].muted;
    return _layer[layer].muted;
}

// ---------------------------------------------------------------- ADPCM I/O
uint8_t* LoopEngine::blockPtr(uint8_t layer, uint32_t block) const
{
    return _pool + (uint32_t)layer * (uint32_t)_layerStride + block * ADPCM_BYTES;
}

void LoopEngine::encodeBlock(uint8_t layer, uint32_t block, const int16_t* in)
{
    Layer& L = _layer[layer];

    // First block of a take: start the predictor at the first sample and pick a
    // step size from the initial slope.  Without this the 4-bit encoder needs
    // ~1 ms to adapt and the loop start gets an audible transient.
    if (block == 0) {
        L.pred = in[0];
        L.idx = 0;
        int32_t d = (int32_t)in[1] - in[0];
        if (d < 0) d = -d;
        while (L.idx < 88 && kStep[L.idx] < d) ++L.idx;
    }

    uint8_t* p = blockPtr(layer, block);
    p[0] = (uint8_t)(L.pred & 0xFF);
    p[1] = (uint8_t)((L.pred >> 8) & 0xFF);
    p[2] = (uint8_t)L.idx;
    p[3] = 0;
    for (int i = 0; i < ADPCM_SAMPLES; i += 2) {
        const uint8_t lo = adpcmEncode(L.pred, L.idx, in[i]);
        const uint8_t hi = adpcmEncode(L.pred, L.idx, in[i + 1]);
        p[4 + i / 2] = (uint8_t)(lo | (hi << 4));
    }
}

void LoopEngine::decodeBlock(uint8_t layer, uint32_t block, int16_t* out) const
{
    const uint8_t* p = blockPtr(layer, block);
    int32_t pred = (int16_t)(p[0] | ((uint16_t)p[1] << 8));
    int32_t idx = p[2];
    for (int i = 0; i < ADPCM_SAMPLES; ++i) {
        const uint8_t byte = p[4 + i / 2];
        const uint8_t nib = (i & 1) ? (uint8_t)(byte >> 4) : (uint8_t)(byte & 0x0F);
        out[i] = adpcmDecode(pred, idx, nib);
    }
}

// ---------------------------------------------------------------- audio
void LoopEngine::triggerClick(bool accent)
{
    if (!_clickOn || _clickLen <= 0) return;
    _clickBuf = accent ? _accent : _normal;
    _clickPos = 0;
    _clickActive = true;
}

void LoopEngine::mixClick(int32_t* acc, int n)
{
    if (!_clickActive) return;
    for (int i = 0; i < n; ++i) {
        if (_clickPos >= _clickLen) {
            _clickActive = false;
            break;
        }
        acc[i] += (int32_t)_clickBuf[_clickPos++] * _clickVol / 100;
    }
}

void LoopEngine::process(const int16_t* in, int16_t* out, int n)
{
    if (n != ENGINE_BLOCK) {
        if (n > 0 && in && out) memcpy(out, in, (size_t)n * sizeof(int16_t));
        return;
    }

    // Transport commands from the UI task, executed at a block boundary.
    const uint16_t req = _req.exchange(0);
    if (req & REQ_STOP_REC) stopRec();
    if (req & REQ_STOP) stop();
    for (int i = 0; i < MAX_LAYERS; ++i) {
        if (req & (REQ_CLEAR_L0 << i)) clearLayer((uint8_t)i);
    }
    if (req & REQ_CLEAR) clear();
    if (req & REQ_UNDO) undo();
    if (req & REQ_PLAY) play();
    if (req & REQ_START) startRec();

    if (_invalid) {
        memcpy(out, in, (size_t)n * sizeof(int16_t));
        return;
    }

    if (_autoArm) {
        _autoArm = false;
        startRec();
    }

    // ---- arm / count-in
    if (_state == LoopState::ARMED) {
        if (_firstTake) {
            if (_armBlocks > 0) {
                if ((_armBlocks % _armBeatBlocks) == 0) {
                    triggerClick(_armBlocks == blocksPerBar());
                }
                --_armBlocks;
                if (_armBlocks == 0) {
                    triggerClick(true);
                    beginRecord(_recLayer);
                }
            } else {
                beginRecord(_recLayer);
            }
        } else if (_phaseBlock == 0) {
            beginRecord(_recLayer);
        }
    }

    // ---- metronome on the loop grid
    if (_state == LoopState::PLAY || _state == LoopState::REC) {
        const uint32_t beats = (uint32_t)_bars * 4u;
        const uint32_t beat = _loopBlocks ? (uint32_t)((uint64_t)_phaseBlock * beats / _loopBlocks) : 0;
        if (beat != _lastBeat) {
            _lastBeat = beat;
            triggerClick((beat % 4u) == 0);
        }
    }

    int32_t acc[ENGINE_BLOCK];
    for (int i = 0; i < n; ++i) acc[i] = 0;

    // ---- record the input block
    if (_state == LoopState::REC) {
        encodeBlock(_recLayer, _recBlock, in);
        ++_recBlock;
        _layer[_recLayer].validBlocks = _recBlock;
    }

    // ---- playback: sum all unmuted layers (skip the layer being recorded)
    uint16_t peaks[MAX_LAYERS] = {0};
    int16_t tmp[ENGINE_BLOCK];
    for (int l = 0; l < _layers; ++l) {
        Layer& L = _layer[l];
        if (!L.used || L.muted) continue;
        if (_state == LoopState::REC && l == _recLayer) continue;
        if (_phaseBlock >= L.validBlocks) continue;
        decodeBlock((uint8_t)l, _phaseBlock, tmp);
        uint16_t pk = 0;
        for (int i = 0; i < n; ++i) {
            acc[i] += tmp[i];
            const uint16_t v = (uint16_t)(tmp[i] < 0 ? -tmp[i] : tmp[i]);
            if (v > pk) pk = v;
        }
        peaks[l] = pk;
    }

    // ---- input peak + monitor
    uint16_t inPk = 0;
    for (int i = 0; i < n; ++i) {
        const uint16_t v = (uint16_t)(in[i] < 0 ? -in[i] : in[i]);
        if (v > inPk) inPk = v;
    }
    _inPeak = inPk;
    if (_state == LoopState::REC) peaks[_recLayer] = inPk;

    if (_state != LoopState::PLAY) {
        for (int i = 0; i < n; ++i) acc[i] += in[i];
    }

    // ---- level meters (peak with decay)
    for (int l = 0; l < MAX_LAYERS; ++l) {
        Layer& L = _layer[l];
        if (peaks[l] > L.peak) L.peak = peaks[l];
        else L.peak = (uint16_t)(L.peak - (L.peak >> 3));
    }

    // ---- click + clamp
    mixClick(acc, n);
    for (int i = 0; i < n; ++i) out[i] = clamp16(acc[i]);

    // ---- advance / wrap
    if (_state == LoopState::PLAY || _state == LoopState::REC ||
        (_state == LoopState::ARMED && !_firstTake)) {
        ++_phaseBlock;
        if (_phaseBlock >= _loopBlocks) {
            _phaseBlock = 0;
            _lastBeat = 0xFFFFFFFFu;
            if (_state == LoopState::REC) commitRecord();
        }
    } else if (_state == LoopState::STOP) {
        _phaseBlock = 0;
    }
}

uint16_t LoopEngine::phasePermille() const
{
    if (!_loopBlocks) return 0;
    return (uint16_t)((uint32_t)_phaseBlock * 1000u / _loopBlocks);
}

uint16_t LoopEngine::barNow() const
{
    const uint32_t bpb = blocksPerBar();
    uint32_t bar = bpb ? (_phaseBlock / bpb) + 1 : 1;
    if (bar > _bars) bar = _bars;
    if (bar < 1) bar = 1;
    return (uint16_t)bar;
}

void LoopEngine::mixBlock(uint32_t block, int16_t* out) const
{
    int32_t acc[ENGINE_BLOCK];
    for (int i = 0; i < ENGINE_BLOCK; ++i) acc[i] = 0;
    int16_t tmp[ENGINE_BLOCK];
    for (int l = 0; l < _layers; ++l) {
        const Layer& L = _layer[l];
        if (!L.used || L.muted) continue;
        if (block >= L.validBlocks) continue;
        decodeBlock((uint8_t)l, block, tmp);
        for (int i = 0; i < ENGINE_BLOCK; ++i) acc[i] += tmp[i];
    }
    for (int i = 0; i < ENGINE_BLOCK; ++i) out[i] = clamp16(acc[i]);
}
