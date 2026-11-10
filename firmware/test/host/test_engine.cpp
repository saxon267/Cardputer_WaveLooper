// Host-side tests for the sample-domain loop engine (no Arduino needed).
//   python -m ziglang c++ -std=c++17 -O2 -I ../src test_engine.cpp ../src/loop_audio.cpp -o test_engine.exe
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "loop_audio.h"

static uint8_t g_pool[POOL_BYTES];
static int g_fail = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

static void sineBlock(int16_t* in, uint32_t block, float freq, float amp)
{
    for (int i = 0; i < ENGINE_BLOCK; ++i) {
        const float t = (float)(block * ENGINE_BLOCK + i) / (float)ENGINE_RATE;
        in[i] = (int16_t)(amp * sinf(6.2831853f * freq * t));
    }
}

static void dcBlock(int16_t* in, int16_t value)
{
    for (int i = 0; i < ENGINE_BLOCK; ++i) in[i] = value;
}

static float maxAbsDiff(const int16_t* a, const int16_t* b)
{
    float m = 0.0f;
    for (int i = 0; i < ENGINE_BLOCK; ++i) {
        const float d = fabsf((float)a[i] - (float)b[i]);
        if (d > m) m = d;
    }
    return m;
}

static void testLayout()
{
    LoopEngine e;
    CHECK(e.begin(g_pool, POOL_BYTES));
    CHECK(e.bars() == 1);
    CHECK(e.layers() == 4);
    CHECK(e.loopBlocks() == 200);
    CHECK(e.loopSamples() == 48000);
    CHECK(fabsf(e.loopSeconds() - 2.0f) < 0.01f);
    CHECK(e.capacityOk(4, 1));
    CHECK(!e.capacityOk(4, 2));
    CHECK(e.maxBars(4) == 1);
    CHECK(e.maxBars(2) == 2);
    CHECK(e.maxLayers(2) == 2);
    CHECK(e.maxLayers(1) == 4);

    CHECK(e.setBpm(900));
    CHECK(e.layers() == 3);              // 4 x 267 blocks would overflow
    CHECK(!e.capacityOk(4, 1));
    CHECK(e.maxLayers(1) == 3);
    CHECK(e.loopBlocks() == 267);

    CHECK(e.setBpm(1200));
    CHECK(e.setLayers(4));
    CHECK(e.loopBlocks() == 200);
}

static void testRecordPlayback()
{
    LoopEngine e;
    e.begin(g_pool, POOL_BYTES);
    e.setClick(false);
    e.setCountIn(false);
    e.setBpm(1200);
    e.setBars(1);
    e.setLayers(4);

    int16_t in[ENGINE_BLOCK];
    int16_t out[ENGINE_BLOCK];

    CHECK(e.startRec());
    CHECK(e.state() == LoopState::REC);
    for (uint32_t b = 0; b < e.loopBlocks(); ++b) {
        sineBlock(in, b, 440.0f, 12000.0f);
        e.process(in, out, ENGINE_BLOCK);
    }
    CHECK(e.state() == LoopState::PLAY);
    CHECK(e.layersUsed() == 1);
    CHECK(e.layerUsed(0));

    e.play();
    float maxErr = 0.0f;
    for (uint32_t b = 0; b < e.loopBlocks(); ++b) {
        memset(in, 0, sizeof(in));
        e.process(in, out, ENGINE_BLOCK);
        int16_t ref[ENGINE_BLOCK];
        sineBlock(ref, b, 440.0f, 12000.0f);
        const float d = maxAbsDiff(out, ref);
        if (d > maxErr) maxErr = d;
    }
    printf("  ADPCM max error: %.0f\n", maxErr);
    CHECK(maxErr < 1000.0f);
}

static void testLayers()
{
    LoopEngine e;
    e.begin(g_pool, POOL_BYTES);
    e.setClick(false);
    e.setCountIn(false);

    int16_t in[ENGINE_BLOCK];
    int16_t out[ENGINE_BLOCK];

    // layer 1: 440 Hz sine
    e.startRec();
    for (uint32_t b = 0; b < e.loopBlocks(); ++b) {
        sineBlock(in, b, 440.0f, 10000.0f);
        e.process(in, out, ENGINE_BLOCK);
    }
    CHECK(e.state() == LoopState::PLAY);

    // layer 2: DC 2000 (starts at phase 0)
    CHECK(e.startRec());
    CHECK(e.state() == LoopState::REC);
    for (uint32_t b = 0; b < e.loopBlocks(); ++b) {
        dcBlock(in, 2000);
        e.process(in, out, ENGINE_BLOCK);
        if (b == 0) CHECK(e.layersUsed() == 2);
    }
    CHECK(e.layersUsed() == 2);

    // mix: sine + 2000
    e.play();
    float maxErr = 0.0f;
    for (uint32_t b = 0; b < e.loopBlocks(); ++b) {
        memset(in, 0, sizeof(in));
        e.process(in, out, ENGINE_BLOCK);
        int16_t ref[ENGINE_BLOCK];
        sineBlock(ref, b, 440.0f, 10000.0f);
        for (int i = 0; i < ENGINE_BLOCK; ++i) ref[i] = (int16_t)(ref[i] + 2000);
        const float d = maxAbsDiff(out, ref);
        if (d > maxErr) maxErr = d;
    }
    printf("  layer mix max error: %.0f\n", maxErr);
    CHECK(maxErr < 1000.0f);

    // mute layer 1 -> only DC
    CHECK(e.toggleMute(0));
    e.play();
    int maxV = 0;
    for (uint32_t b = 0; b < e.loopBlocks(); ++b) {
        memset(in, 0, sizeof(in));
        e.process(in, out, ENGINE_BLOCK);
        for (int i = 0; i < ENGINE_BLOCK; ++i) {
            const int v = out[i] < 0 ? -out[i] : out[i];
            if (v > maxV) maxV = v;
        }
    }
    CHECK(maxV > 1900 && maxV < 2100);

    // undo layer 2 -> sine only
    CHECK(e.undo());
    CHECK(e.layersUsed() == 1);
    CHECK(!e.layerUsed(1));
    e.toggleMute(0);              // unmute layer 1 again
    e.play();
    float err2 = 0.0f;
    for (uint32_t b = 0; b < e.loopBlocks(); ++b) {
        memset(in, 0, sizeof(in));
        e.process(in, out, ENGINE_BLOCK);
        int16_t ref[ENGINE_BLOCK];
        sineBlock(ref, b, 440.0f, 10000.0f);
        const float d = maxAbsDiff(out, ref);
        if (d > err2) err2 = d;
    }
    CHECK(err2 < 1000.0f);
}

static void testPartialTake()
{
    LoopEngine e;
    e.begin(g_pool, POOL_BYTES);
    e.setClick(false);
    e.setCountIn(false);

    int16_t in[ENGINE_BLOCK];
    int16_t out[ENGINE_BLOCK];

    // First take cancelled -> nothing recorded.
    e.startRec();
    for (int b = 0; b < 50; ++b) {
        sineBlock(in, (uint32_t)b, 440.0f, 8000.0f);
        e.process(in, out, ENGINE_BLOCK);
    }
    e.stopRec();
    CHECK(e.state() == LoopState::STOP);
    CHECK(e.layersUsed() == 0);

    // Full first take.
    e.startRec();
    for (uint32_t b = 0; b < e.loopBlocks(); ++b) {
        sineBlock(in, b, 440.0f, 8000.0f);
        e.process(in, out, ENGINE_BLOCK);
    }
    CHECK(e.state() == LoopState::PLAY);

    // Second take stopped after 50 blocks -> partial layer, silent tail.
    e.startRec();
    for (int b = 0; b < 50; ++b) {
        dcBlock(in, 1000);
        e.process(in, out, ENGINE_BLOCK);
    }
    e.stopRec();
    CHECK(e.state() == LoopState::PLAY);
    CHECK(e.layersUsed() == 2);

    e.play();
    int lastMax = 0;
    for (uint32_t b = 0; b < e.loopBlocks(); ++b) {
        memset(in, 0, sizeof(in));
        e.process(in, out, ENGINE_BLOCK);
        if (b >= 60) {
            int v = 0;
            for (int i = 0; i < ENGINE_BLOCK; ++i) {
                const int a = out[i] < 0 ? -out[i] : out[i];
                if (a > v) v = a;
            }
            if (v > lastMax) lastMax = v;
        }
    }
    // tail must not contain the DC layer (only the sine remains)
    CHECK(lastMax < 9000);
}

static void testClearLayer()
{
    LoopEngine e;
    e.begin(g_pool, POOL_BYTES);
    e.setClick(false);
    e.setCountIn(false);

    int16_t in[ENGINE_BLOCK];
    int16_t out[ENGINE_BLOCK];

    // Three layers: DC 1000, 2000, 3000.
    for (int l = 0; l < 3; ++l) {
        CHECK(e.startRec());
        for (uint32_t b = 0; b < e.loopBlocks(); ++b) {
            dcBlock(in, (int16_t)(1000 * (l + 1)));
            e.process(in, out, ENGINE_BLOCK);
        }
        CHECK(e.layersUsed() == (uint8_t)(l + 1));
    }

    // Fn+C on layer 2: only that layer disappears.
    CHECK(e.clearLayer(1));
    CHECK(!e.layerUsed(1));
    CHECK(e.layersUsed() == 2);
    e.play();
    for (uint32_t b = 0; b < e.loopBlocks(); ++b) {
        memset(in, 0, sizeof(in));
        e.process(in, out, ENGINE_BLOCK);
    }
    CHECK(out[0] > 3800 && out[0] < 4200);   // 1000 + 3000

    // Clearing an unused layer is a no-op.
    CHECK(!e.clearLayer(1));

    // Undo still removes the most recent take (layer 3).
    CHECK(e.undo());
    CHECK(e.layersUsed() == 1);
    e.play();
    for (uint32_t b = 0; b < e.loopBlocks(); ++b) {
        memset(in, 0, sizeof(in));
        e.process(in, out, ENGINE_BLOCK);
    }
    CHECK(out[0] > 900 && out[0] < 1100);    // only layer 1

    // Clearing the layer that is being recorded cancels the take.
    CHECK(e.startRec());
    for (int b = 0; b < 10; ++b) {
        dcBlock(in, 500);
        e.process(in, out, ENGINE_BLOCK);
    }
    CHECK(e.clearLayer(1));
    CHECK(!e.layerUsed(1));
    CHECK(e.layersUsed() == 1);

    // Clearing the armed first take cancels the arm.
    e.clear();
    e.setCountIn(true);
    CHECK(e.startRec());
    CHECK(e.state() == LoopState::ARMED);
    CHECK(e.clearLayer(e.recLayer()));
    CHECK(e.state() == LoopState::STOP);
    CHECK(e.layersUsed() == 0);
}

static void testCountIn()
{
    LoopEngine e;
    e.begin(g_pool, POOL_BYTES);
    e.setClick(true);
    e.setCountIn(true);
    e.setBpm(1200);
    e.setBars(1);

    int16_t in[ENGINE_BLOCK];
    int16_t out[ENGINE_BLOCK];

    CHECK(e.startRec());
    CHECK(e.state() == LoopState::ARMED);

    const uint32_t bpb = e.loopBlocks();
    for (uint32_t b = 0; b < bpb; ++b) {
        memset(in, 0, sizeof(in));
        e.process(in, out, ENGINE_BLOCK);
    }
    CHECK(e.state() == LoopState::REC);

    for (uint32_t b = 0; b < bpb; ++b) {
        sineBlock(in, b, 330.0f, 6000.0f);
        e.process(in, out, ENGINE_BLOCK);
    }
    CHECK(e.state() == LoopState::PLAY);
    CHECK(e.layersUsed() == 1);
}

static void testClearAndLayerCapacity()
{
    LoopEngine e;
    e.begin(g_pool, POOL_BYTES);
    e.setClick(false);
    e.setCountIn(false);

    int16_t in[ENGINE_BLOCK];
    int16_t out[ENGINE_BLOCK];

    // Fill all four layers at 120 BPM.
    for (int l = 0; l < 4; ++l) {
        CHECK(e.startRec());
        for (uint32_t b = 0; b < e.loopBlocks(); ++b) {
            dcBlock(in, (int16_t)(500 * (l + 1)));
            e.process(in, out, ENGINE_BLOCK);
        }
        CHECK(e.layersUsed() == (uint8_t)(l + 1));
    }
    CHECK(!e.startRec());              // all layers used
    e.play();
    int v = 0;
    for (uint32_t b = 0; b < e.loopBlocks(); ++b) {
        memset(in, 0, sizeof(in));
        e.process(in, out, ENGINE_BLOCK);
        v = out[0];
    }
    CHECK(v > 4000 && v < 5400);       // 500+1000+1500+2000 = 5000

    e.clear();
    CHECK(e.layersUsed() == 0);
    CHECK(e.state() == LoopState::STOP);
}

int main()
{
    printf("loop engine tests\n");
    testLayout();
    testRecordPlayback();
    testLayers();
    testPartialTake();
    testClearLayer();
    testCountIn();
    testClearAndLayerCapacity();

    if (g_fail) {
        printf("RESULT FAIL (%d)\n", g_fail);
        return 1;
    }
    printf("RESULT OK\n");
    return 0;
}
