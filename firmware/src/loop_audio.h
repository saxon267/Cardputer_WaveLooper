#pragma once
#include <stdint.h>
#include <atomic>
#include "config.h"

enum class LoopState : uint8_t { STOP, ARMED, REC, PLAY };

// Transport commands are posted by the UI task and executed by the audio task
// at a block boundary, so the two never mutate the engine state concurrently.
enum : uint16_t {
    REQ_START    = 1u << 0,
    REQ_STOP_REC = 1u << 1,
    REQ_PLAY     = 1u << 2,
    REQ_STOP     = 1u << 3,
    REQ_CLEAR    = 1u << 4,
    REQ_UNDO     = 1u << 5,
    // One bit per layer: Fn+C clears only the selected track.
    REQ_CLEAR_L0 = 1u << 6,
    REQ_CLEAR_L1 = 1u << 7,
    REQ_CLEAR_L2 = 1u << 8,
    REQ_CLEAR_L3 = 1u << 9,
};

// Sample-domain looper: 4 layers of the same length, IMA ADPCM 4-bit storage.
// The engine is free of Arduino dependencies so it can run in host tests.
class LoopEngine {
public:
    bool begin(uint8_t* pool, int poolBytes);

    // Layout changes are only allowed while no layer is recorded.
    bool setBpm(uint16_t bpmX10);
    bool setBars(uint8_t bars);
    bool setLayers(uint8_t layers);
    void setCountIn(bool on) { _countIn = on; }
    void setClick(bool on);
    void setLoopMode(bool on) { _loopMode = on; }
    bool toggleLoopMode() { _loopMode = !_loopMode; return _loopMode; }

    bool startRec();
    void stopRec();
    bool play();
    void stop();
    void clear();
    bool undo();
    bool clearLayer(uint8_t layer);
    bool toggleMute(uint8_t layer);

    // Thread-safe transport for the firmware: post a command, execute it from
    // process() at the next block boundary.
    void request(uint16_t mask) { _req.fetch_or(mask); }
    bool pending(uint16_t mask) const { return (_req.load() & mask) != 0; }

    // Audio task: exactly ENGINE_BLOCK samples in, same count out.
    void process(const int16_t* in, int16_t* out, int n);

    // ---- state for UI ----
    LoopState state() const { return _state; }
    uint8_t bars() const { return _bars; }
    uint8_t layers() const { return _layers; }
    uint8_t layersUsed() const { return _usedCount; }
    uint16_t bpmX10() const { return _bpmX10; }
    uint32_t loopBlocks() const { return _loopBlocks; }
    uint32_t loopSamples() const { return _loopBlocks * ADPCM_SAMPLES; }
    float loopSeconds() const { return (float)loopSamples() / (float)ENGINE_RATE; }
    uint16_t phasePermille() const;
    uint16_t barNow() const;
    uint8_t recLayer() const { return _recLayer; }
    bool isFirstTake() const { return _firstTake; }
    bool invalid() const { return _invalid; }
    bool loopMode() const { return _loopMode; }
    bool countIn() const { return _countIn; }
    bool click() const { return _clickOn; }
    bool layerUsed(uint8_t i) const { return i < MAX_LAYERS && _layer[i].used; }
    bool layerMuted(uint8_t i) const { return i < MAX_LAYERS && _layer[i].muted; }
    uint16_t layerPeak(uint8_t i) const { return i < MAX_LAYERS ? _layer[i].peak : 0; }
    uint16_t inputPeak() const { return _inPeak; }
    int poolBytes() const { return _poolBytes; }
    int poolUsed() const { return _layers * _layerStride; }

    bool capacityOk(uint8_t layers, uint8_t bars) const;
    uint8_t maxBars(uint8_t layers) const;
    uint8_t maxLayers(uint8_t bars) const;

    // Mix one block of the current layers (used by the WAV writer).
    void mixBlock(uint32_t block, int16_t* out) const;

private:
    struct Layer {
        bool used = false;
        bool muted = false;
        uint32_t validBlocks = 0;
        uint16_t peak = 0;
        int32_t pred = 0;   // ADPCM encoder state
        int32_t idx = 0;
    };

    void recompute();
    void applyLayout();
    uint32_t blocksPerBar() const;
    uint32_t blocksForBars(uint8_t bars) const;
    uint8_t* blockPtr(uint8_t layer, uint32_t block) const;
    void encodeBlock(uint8_t layer, uint32_t block, const int16_t* in);
    void decodeBlock(uint8_t layer, uint32_t block, int16_t* out) const;
    int firstFreeLayer() const;
    void beginRecord(uint8_t layer);
    void commitRecord();
    void cancelRecord();
    void triggerClick(bool accent);
    void mixClick(int32_t* acc, int n);
    void initClick();

    uint8_t* _pool = nullptr;
    int _poolBytes = 0;
    int _layerStride = 0;
    uint32_t _loopBlocks = 200;

    Layer _layer[MAX_LAYERS];
    uint8_t _order[MAX_LAYERS] = {0};
    uint8_t _orderN = 0;
    uint8_t _usedCount = 0;
    uint8_t _recLayer = 0;
    uint32_t _recBlock = 0;
    uint32_t _phaseBlock = 0;
    uint32_t _lastBeat = 0xFFFFFFFFu;
    uint32_t _armBlocks = 0;
    uint32_t _armBeatBlocks = 1;
    uint16_t _inPeak = 0;

    uint8_t _bars = 1;
    uint8_t _layers = 4;
    uint16_t _bpmX10 = 1200;
    bool _countIn = true;
    bool _clickOn = true;
    bool _loopMode = false;
    bool _firstTake = false;
    bool _autoArm = false;
    bool _invalid = false;
    LoopState _state = LoopState::STOP;
    std::atomic<uint16_t> _req{0};

    int16_t _accent[768] = {0};
    int16_t _normal[768] = {0};
    int _clickLen = 0;
    int _clickPos = 0;
    int _clickVol = 80;
    bool _clickActive = false;
    const int16_t* _clickBuf = nullptr;
};

extern LoopEngine looper;
