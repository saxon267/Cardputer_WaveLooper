#include "audio_out.h"

#include <Arduino.h>
#include <M5Cardputer.h>
#include <driver/i2s.h>

#include "config.h"
#include "loop_audio.h"
#include "usb_audio.h"

AudioOut audioOut;

// ---------------------------------------------------------------- ES8311
static bool es8311Write(uint8_t reg, uint8_t val)
{
    return M5Cardputer.In_I2C.writeRegister8(0x18, reg, val, 400000);
}

static uint8_t dacReg(uint8_t volume)
{
    if (volume == 0) return 0x00;
    if (volume <= 50) return (uint8_t)(0x60 + (volume * (0xBF - 0x60)) / 50);
    return (uint8_t)(0xC0 + ((volume - 51) * (0xFF - 0xC0)) / 49);
}

static void es8311Init(uint8_t volume)
{
    es8311Write(0x00, 0x80);
    es8311Write(0x01, 0xB5);
    es8311Write(0x02, 0x18);
    es8311Write(0x09, 0x0C);
    es8311Write(0x0A, 0x0C);
    es8311Write(0x0D, 0x01);
    es8311Write(0x12, 0x00);
    es8311Write(0x13, 0x10);
    es8311Write(0x32, dacReg(volume));
    es8311Write(0x37, 0x08);
    delay(30);
}

static bool installI2s()
{
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = AUDIO_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 4;
    cfg.dma_buf_len          = ENGINE_BLOCK;   // 240 frames = 5 ms
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;
    cfg.fixed_mclk           = 0;
    cfg.mclk_multiple        = I2S_MCLK_MULTIPLE_256;
    cfg.bits_per_chan        = I2S_BITS_PER_CHAN_16BIT;

    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = PIN_I2S_BCLK;
    pins.ws_io_num    = PIN_I2S_LRCK;
    pins.data_out_num = PIN_I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    return i2s_set_pin(I2S_NUM_0, &pins) == ESP_OK;
}

// ---------------------------------------------------------------- engine
bool AudioOut::begin()
{
    _codecOk = M5Cardputer.In_I2C.scanID(0x18, 400000);
    if (_codecOk) es8311Init(78);
    i2s_driver_uninstall(I2S_NUM_0);
    return installI2s();
}

void AudioOut::start()
{
    xTaskCreatePinnedToCore(taskEntry, "wavelooper-audio", 8192, this, 20,
                            (TaskHandle_t*)&_task, 1);
}

void AudioOut::taskEntry(void* arg)
{
    static_cast<AudioOut*>(arg)->taskLoop();
}

void AudioOut::taskLoop()
{
    static int16_t in[ENGINE_BLOCK];
    static int16_t out[ENGINE_BLOCK];
    static int16_t frames[ENGINE_BLOCK * 4];   // 480 stereo frames (2x upsample)

    // Wait for the USB stream (or 1 s) so the ring starts at its target fill.
    const uint32_t t0 = millis();
    while (usbAudio.available() < RING_TARGET && (millis() - t0) < 1000) {
        vTaskDelay(1);
    }

    uint32_t cpuAcc = 0;
    int      cpuN = 0;

    while (true) {
        const uint32_t start = micros();

        usbAudio.read(in, ENGINE_BLOCK);
        looper.process(in, out, ENGINE_BLOCK);

        // 24 kHz -> 48 kHz: zero-order hold plus midpoint interpolation.
        for (int i = 0; i < ENGINE_BLOCK; ++i) {
            const int16_t a = out[i];
            const int16_t mid = (int16_t)(((int32_t)_last + a) >> 1);
            const int idx = i * 4;
            frames[idx + 0] = a;
            frames[idx + 1] = a;
            frames[idx + 2] = mid;
            frames[idx + 3] = mid;
            _last = a;
        }

        size_t written = 0;
        const esp_err_t err = i2s_write(I2S_NUM_0, frames, sizeof(frames), &written, portMAX_DELAY);
        if (err != ESP_OK) {
            _ur = _ur + 1;
            vTaskDelay(1);
        }
        _blocks = _blocks + 1;

        const uint32_t spent = micros() - start;
        cpuAcc += spent;
        if (++cpuN >= 32) {
            const uint32_t blockUs = (uint32_t)((uint64_t)ENGINE_BLOCK * 1000000ull / ENGINE_RATE);
            uint32_t pct = cpuAcc * 100u / (blockUs * (uint32_t)cpuN);
            if (pct > 100) pct = 100;
            _cpu = (uint8_t)pct;
            cpuAcc = 0;
            cpuN = 0;
        }
    }
}
