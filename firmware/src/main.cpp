#include <Arduino.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "app.h"
#include "audio_out.h"
#include "config.h"
#include "library.h"
#include "loop_audio.h"
#include "ui.h"
#include "usb_audio.h"
#include "wav_store.h"

App app;
static Preferences prefs;
static uint8_t s_pool[POOL_BYTES];

// ---------------------------------------------------------------- helpers
static void setToast(const char* msg)
{
    strncpy(app.toast, msg, sizeof(app.toast) - 1);
    app.toast[sizeof(app.toast) - 1] = 0;
    app.toastUntil = millis() + 1800;
}

static void requestSync(uint8_t mask)
{
    looper.request(mask);
    const uint32_t t0 = millis();
    while (looper.pending(mask) && (millis() - t0) < 50) delay(1);
}

static void adjustScroll()
{
    if (app.sel < app.scroll) app.scroll = app.sel;
    if (app.sel >= app.scroll + 6) app.scroll = app.sel - 5;
    if (app.scroll < 0) app.scroll = 0;
}

static void refreshLibrary()
{
    if (!app.sdOk) return;
    if (!library.scan(LOOP_DIR)) {
        SD.mkdir(LOOP_DIR);
        library.scan(LOOP_DIR);
    }
    const int n = library.count();
    if (app.sel >= n) app.sel = n ? n - 1 : 0;
    if (app.sel < 0) app.sel = 0;
    adjustScroll();
}

// ---------------------------------------------------------------- actions
static void toggleRecord()
{
    if (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED) {
        const bool wasFirst = looper.isFirstTake();
        looper.request(REQ_STOP_REC);
        setToast(wasFirst ? "TAKE CANCELED" : "LOOP PLAY");
        return;
    }
    if (looper.invalid()) {
        setToast("BPM LOW");
        return;
    }
    if (looper.layersUsed() >= looper.layers()) {
        setToast("LAYERS FULL");
        return;
    }
    looper.request(REQ_START);
    setToast(looper.layersUsed() == 0 ? (looper.countIn() ? "COUNT IN" : "REC") : "OVERDUB");
}

static void togglePlay()
{
    if (looper.state() == LoopState::PLAY) {
        looper.request(REQ_STOP);
        setToast("STOPPED");
    } else if (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED) {
        looper.request(REQ_STOP_REC);
        setToast("STOPPED");
    } else if (looper.layersUsed() == 0) {
        setToast("REC FIRST");
    } else {
        looper.request(REQ_PLAY);
    }
}

static void saveLoop()
{
    requestSync(REQ_STOP_REC | REQ_STOP);
    if (!looper.layersUsed()) {
        setToast("NOTHING TO SAVE");
        return;
    }

    char path[96] = {0};
    if (!wavSaveMix(looper, LOOP_DIR, path, sizeof(path))) {
        setToast(app.sdOk ? "SAVE FAIL" : "NO SD");
        return;
    }
    strncpy(app.lastSaved, path, sizeof(app.lastSaved) - 1);
    setToast("SAVED");
    refreshLibrary();
}

static void clearLoop()
{
    requestSync(REQ_STOP_REC | REQ_CLEAR);
    setToast("CLEARED");
}

static void toggleLaneMute(uint8_t lane)
{
    if (!looper.toggleMute(lane)) {
        setToast("NO TAKE");
        return;
    }
    char b[16];
    snprintf(b, sizeof(b), "L%d %s", (int)lane + 1, looper.layerMuted(lane) ? "MUTE" : "ON");
    setToast(b);
}

static void adjustBpm(int delta)
{
    if (looper.layersUsed() > 0) {
        setToast("CLEAR FIRST");
        return;
    }
    int v = (int)app.bpmX10 + delta * 10;
    if (v < 400) v = 400;
    if (v > 2400) v = 2400;
    app.bpmX10 = (uint16_t)v;
    if (!looper.setBpm(app.bpmX10)) {
        setToast("CLEAR FIRST");
        return;
    }
    prefs.putUShort("bpm", app.bpmX10);
    char b[12];
    snprintf(b, sizeof(b), "%u BPM", app.bpmX10 / 10);
    setToast(b);
}

static void cycleBars(int delta)
{
    if (looper.layersUsed() > 0) {
        setToast("CLEAR FIRST");
        return;
    }
    int b = (int)app.bars + delta;
    if (b < 1) b = MAX_BARS;
    if (b > MAX_BARS) b = 1;
    if (!looper.capacityOk(app.layers, (uint8_t)b)) {
        setToast("NO ROOM");
        return;
    }
    app.bars = (uint8_t)b;
    looper.setBars(app.bars);
    prefs.putUChar("bars", app.bars);
    char t[16];
    snprintf(t, sizeof(t), "%u BAR", app.bars);
    setToast(t);
}

static void cycleLayers(int delta)
{
    if (looper.layersUsed() > 0) {
        setToast("CLEAR FIRST");
        return;
    }
    static const uint8_t opts[] = {1, 2, 4};
    int idx = 0;
    for (int i = 0; i < 3; ++i) if (opts[i] == app.layers) idx = i;
    idx += delta;
    if (idx < 0) idx = 2;
    if (idx > 2) idx = 0;
    const uint8_t l = opts[idx];
    if (!looper.capacityOk(l, app.bars)) {
        setToast("NO ROOM");
        return;
    }
    app.layers = l;
    looper.setLayers(l);
    prefs.putUChar("layers", l);
    char t[16];
    snprintf(t, sizeof(t), "%u LAYERS", (unsigned)l);
    setToast(t);
}

static void toggleClick()
{
    app.click = !app.click;
    looper.setClick(app.click);
    prefs.putBool("click", app.click);
    setToast(app.click ? "CLICK ON" : "CLICK OFF");
}

static void toggleCountIn()
{
    app.countIn = !app.countIn;
    looper.setCountIn(app.countIn);
    prefs.putBool("cnt", app.countIn);
    setToast(app.countIn ? "COUNT-IN ON" : "COUNT-IN OFF");
}

static void toggleLoopMode()
{
    const bool on = looper.toggleLoopMode();
    prefs.putBool("loop", on);
    setToast(on ? "LOOP MODE ON" : "LOOP MODE OFF");
}

static void adjustSetting(int dir)
{
    switch (app.settingsRow) {
        case 0: adjustBpm(dir * 5); break;
        case 1: cycleBars(dir); break;
        case 2: cycleLayers(dir); break;
        case 3: toggleClick(); break;
        case 4: toggleCountIn(); break;
        case 5: toggleLoopMode(); break;
        default: break;
    }
}

// ---------------------------------------------------------------- files
static void deleteSelected()
{
    app.confirmDelete = false;
    if (!app.sdOk || app.sel < 0 || app.sel >= library.count()) {
        setToast("NO FILE");
        return;
    }
    char path[96];
    strncpy(path, library.at(app.sel).path, sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    if (!SD.remove(path)) {
        setToast("DELETE FAIL");
        return;
    }
    refreshLibrary();
    setToast("DELETED");
}

static char renameLast[32] = {0};
static bool renameSeen = false;

static void startRename()
{
    if (!app.sdOk || app.sel < 0 || app.sel >= library.count()) {
        setToast("NO FILE");
        return;
    }
    const FileEntry& e = library.at(app.sel);
    const char* dot = strrchr(e.name, '.');
    size_t stem = dot ? (size_t)(dot - e.name) : strlen(e.name);
    if (stem > sizeof(app.renameBuf) - 1) stem = sizeof(app.renameBuf) - 1;

    memcpy(app.renameBuf, e.name, stem);
    app.renameBuf[stem] = 0;
    strncpy(app.renameExt, (dot && strlen(dot) < sizeof(app.renameExt)) ? dot : ".WAV",
            sizeof(app.renameExt) - 1);
    app.renameExt[sizeof(app.renameExt) - 1] = 0;
    strncpy(app.renameOld, e.name, sizeof(app.renameOld) - 1);
    app.renameOld[sizeof(app.renameOld) - 1] = 0;

    app.renaming = true;
    app.confirmDelete = false;
    renameSeen = false;
    renameLast[0] = 0;
}

static void cancelRename()
{
    app.renaming = false;
    setToast("CANCELED");
}

static void finishRename()
{
    if (!app.renaming) return;
    app.renaming = false;

    if (!app.sdOk || app.sel < 0 || app.sel >= library.count()) {
        setToast("NO FILE");
        return;
    }
    if (!app.renameBuf[0]) {
        setToast("EMPTY NAME");
        return;
    }

    char newName[48];
    snprintf(newName, sizeof(newName), "%s%s", app.renameBuf, app.renameExt);

    char oldPath[96];
    strncpy(oldPath, library.at(app.sel).path, sizeof(oldPath) - 1);
    oldPath[sizeof(oldPath) - 1] = 0;

    char target[128];
    snprintf(target, sizeof(target), LOOP_DIR "/%s", newName);

    if (strcasecmp(oldPath, target) == 0) {
        setToast("UNCHANGED");
        return;
    }
    if (SD.exists(target)) {
        setToast("NAME EXISTS");
        return;
    }
    if (!SD.rename(oldPath, target)) {
        setToast("RENAME FAIL");
        return;
    }

    refreshLibrary();
    for (int i = 0; i < library.count(); ++i) {
        if (strcmp(library.at(i).name, newName) == 0) {
            app.sel = i;
            break;
        }
    }
    adjustScroll();
    setToast("RENAMED");
}

// ---------------------------------------------------------------- keys
static void handleKeys()
{
    auto& ks = M5Cardputer.Keyboard.keysState();

    static bool pEnter = false, pSpace = false, pDel = false, pTab = false;
    static bool pUp = false, pDown = false, pLeft = false, pRight = false;
    static bool pR = false, pS = false, pM = false, pG = false, pC = false;
    static bool pD = false, pN = false, pMinus = false, pPlus = false, pU = false, pL = false;
    static bool pD1 = false, pD2 = false, pD3 = false, pD4 = false;

    const bool enter = ks.enter;
    const bool space = ks.space;
    const bool del   = ks.del;
    const bool tab   = ks.tab;
    const bool up    = M5Cardputer.Keyboard.isKeyPressed(';');
    const bool down  = M5Cardputer.Keyboard.isKeyPressed('.');
    const bool left  = M5Cardputer.Keyboard.isKeyPressed(',');
    const bool right = M5Cardputer.Keyboard.isKeyPressed('/');
    const bool rkey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('r');
    const bool skey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('s');
    const bool mkey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('m');
    const bool gkey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('g');
    const bool ckey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('c');
    const bool dkey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('d');
    const bool nkey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('n');
    const bool ukey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('u');
    const bool lkey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('l');
    const bool minus = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('-');
    const bool plus  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('=');
    const bool d1 = M5Cardputer.Keyboard.isKeyPressed('1');
    const bool d2 = M5Cardputer.Keyboard.isKeyPressed('2');
    const bool d3 = M5Cardputer.Keyboard.isKeyPressed('3');
    const bool d4 = M5Cardputer.Keyboard.isKeyPressed('4');

    const bool eEnter = enter && !pEnter;
    const bool eSpace = space && !pSpace;
    const bool eDel   = del && !pDel;
    const bool eTab   = tab && !pTab;
    const bool eUp    = up && !pUp;
    const bool eDown  = down && !pDown;
    const bool eLeft  = left && !pLeft;
    const bool eRight = right && !pRight;
    const bool eR = rkey && !pR;
    const bool eS = skey && !pS;
    const bool eM = mkey && !pM;
    const bool eG = gkey && !pG;
    const bool eC = ckey && !pC;
    const bool eD = dkey && !pD;
    const bool eN = nkey && !pN;
    const bool eU = ukey && !pU;
    const bool eL = lkey && !pL;
    const bool eMinus = minus && !pMinus;
    const bool ePlus  = plus && !pPlus;
    const bool eD1 = d1 && !pD1;
    const bool eD2 = d2 && !pD2;
    const bool eD3 = d3 && !pD3;
    const bool eD4 = d4 && !pD4;

    pEnter = enter; pSpace = space; pDel = del; pTab = tab;
    pUp = up; pDown = down; pLeft = left; pRight = right;
    pR = rkey; pS = skey; pM = mkey; pG = gkey; pC = ckey;
    pD = dkey; pN = nkey; pMinus = minus; pPlus = plus; pU = ukey;
    pL = lkey;
    pD1 = d1; pD2 = d2; pD3 = d3; pD4 = d4;

    if (app.renaming) {
        char cur[32] = {0};
        int clen = 0;
        for (char c : ks.word) {
            if (c < 32 || c > 126) continue;
            if (strchr("/\\:*?\"<>|", c)) continue;
            if (clen < (int)sizeof(cur) - 1) cur[clen++] = c;
        }
        if (!renameSeen) {
            strncpy(renameLast, cur, sizeof(renameLast) - 1);
            renameLast[sizeof(renameLast) - 1] = 0;
            renameSeen = true;
        } else {
            int count[128] = {0};
            for (const char* p = renameLast; *p; ++p) count[(uint8_t)*p]++;
            for (int i = 0; i < clen; ++i) {
                const uint8_t c = (uint8_t)cur[i];
                if (count[c] > 0) { --count[c]; continue; }
                const size_t n = strlen(app.renameBuf);
                if (n < sizeof(app.renameBuf) - 1) {
                    app.renameBuf[n] = (char)c;
                    app.renameBuf[n + 1] = 0;
                }
            }
            strncpy(renameLast, cur, sizeof(renameLast) - 1);
            renameLast[sizeof(renameLast) - 1] = 0;
        }

        if (eDel) {
            const size_t n = strlen(app.renameBuf);
            if (n) app.renameBuf[n - 1] = 0;
        }
        if (eEnter) finishRename();
        else if (eTab) cancelRename();
        return;
    }

    if (app.confirmDelete) {
        if (eEnter || eSpace) deleteSelected();
        else if (eDel || eD) { app.confirmDelete = false; setToast("CANCELED"); }
        return;
    }

    if (app.settings) {
        if (eUp)   app.settingsRow = (app.settingsRow + 5) % 6;
        if (eDown) app.settingsRow = (app.settingsRow + 1) % 6;
        if (eLeft) adjustSetting(-1);
        if (eRight) adjustSetting(+1);
        if (eEnter) {
            if (app.settingsRow >= 3) adjustSetting(+1);
            else app.settings = false;
        }
        if (eDel || eG || eTab) app.settings = false;
        return;
    }

    if (eTab) {
        if (app.screen == Screen::LOOP) {
            refreshLibrary();
            app.screen = Screen::FILES;
            setToast("FILES");
        } else if (app.screen == Screen::FILES) {
            app.screen = Screen::LOOP;
        }
        return;
    }

    if (app.screen == Screen::LOOP) {
        if (eR || eSpace) toggleRecord();
        if (eEnter) togglePlay();
        if (eS) saveLoop();
        if (eC) clearLoop();
        if (eM) toggleClick();
        if (eL) toggleLoopMode();
        if (eU) {
            const bool has = looper.layersUsed() > 0 ||
                             looper.state() == LoopState::REC ||
                             looper.state() == LoopState::ARMED;
            if (!has) {
                setToast("NOTHING TO UNDO");
            } else {
                looper.request(REQ_UNDO);
                setToast("UNDO");
            }
        }
        if (eG) { app.settings = true; app.settingsRow = 0; }

        if (!ks.fn && eMinus) adjustBpm(-1);
        if (!ks.fn && ePlus)  adjustBpm(+1);

        if (!ks.fn && eLeft)  adjustBpm(-1);
        if (!ks.fn && eRight) adjustBpm(+1);
        if (ks.fn && eLeft)  adjustBpm(-5);
        if (ks.fn && eRight) adjustBpm(+5);

        if (!ks.fn && eUp)   app.selectedLane = (app.selectedLane + 3) % 4;
        if (!ks.fn && eDown) app.selectedLane = (app.selectedLane + 1) % 4;
        if (ks.fn && eUp)   cycleBars(+1);
        if (ks.fn && eDown) cycleBars(-1);

        if (eD1) { if (ks.fn) toggleLaneMute(0); else app.selectedLane = 0; }
        if (eD2) { if (ks.fn) toggleLaneMute(1); else app.selectedLane = 1; }
        if (eD3) { if (ks.fn) toggleLaneMute(2); else app.selectedLane = 2; }
        if (eD4) { if (ks.fn) toggleLaneMute(3); else app.selectedLane = 3; }

        if (eDel) {
            looper.request(REQ_STOP_REC | REQ_STOP);
            setToast("STOPPED");
        }
        return;
    }

    if (app.screen == Screen::FILES) {
        const int n = library.count();
        if (eUp && n)   { app.sel = (app.sel + n - 1) % n; adjustScroll(); }
        if (eDown && n) { app.sel = (app.sel + 1) % n; adjustScroll(); }
        if (eD && n) app.confirmDelete = true;
        if (eN && n) startRename();
        if (eEnter) {
            if (n) setToast("LOAD NYI");   // v0.1: save only
            else   setToast("NO FILE");
        }
        if (eDel) app.screen = Screen::LOOP;
        return;
    }
}

// ---------------------------------------------------------------- serial
static void handleSerial()
{
    while (Serial.available()) {
        const int c = Serial.read();
        if (c < 0) break;
        if (c == 'i' || c == 'I') {
            Serial.printf("[loop] state=%d bars=%u bpm=%u layers=%u used=%u phase=%u in=%u heap=%u\n",
                          (int)looper.state(), looper.bars(), looper.bpmX10() / 10,
                          looper.layers(), looper.layersUsed(), looper.phasePermille(),
                          (unsigned)app.inLevel, (unsigned)ESP.getFreeHeap());
        } else if (c == 'r' || c == 'R') {
            toggleRecord();
        } else if (c == 'p' || c == 'P') {
            togglePlay();
        } else if (c == 's' || c == 'S') {
            saveLoop();
        } else if (c == 'c' || c == 'C') {
            clearLoop();
        }
    }
}

// ---------------------------------------------------------------- state
static void syncApp()
{
    app.loopState     = looper.state();
    app.hasLoop       = looper.layersUsed() > 0;
    app.overdub       = (looper.state() == LoopState::REC) && !looper.isFirstTake();
    app.bars          = looper.bars();
    app.layers        = looper.layers();
    app.bpmX10        = looper.bpmX10();
    app.loopMode      = looper.loopMode();
    app.countIn       = looper.countIn();
    app.click         = looper.click();
    app.phasePermille = looper.phasePermille();
    app.barNow        = looper.barNow();
    app.loopSec       = looper.loopSeconds();
    app.invalid       = looper.invalid();
    app.usbOk         = usbAudio.started();
    app.usbStream     = usbAudio.streaming();
    app.sdOk          = library.mounted();

    for (int i = 0; i < 4; ++i) {
        app.laneUsed[i]  = looper.layerUsed((uint8_t)i);
        app.laneMuted[i] = looper.layerMuted((uint8_t)i);
        app.lanePeak[i]  = looper.layerPeak((uint8_t)i);
    }
}

// ---------------------------------------------------------------- setup
void setup()
{
    Serial.begin(115200);
    delay(150);
    Serial.printf("\n[wavelooper] boot v%s heap=%u\n", APP_VERSION, (unsigned)ESP.getFreeHeap());

    auto cfg = M5.config();
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setBrightness(140);

    prefs.begin("wavelooper", false);
    app.bpmX10       = prefs.getUShort("bpm", 1200);
    app.bars         = prefs.getUChar("bars", 1);
    app.layers       = prefs.getUChar("layers", 4);
    app.click        = prefs.getBool("click", true);
    app.countIn      = prefs.getBool("cnt", true);
    app.loopMode     = prefs.getBool("loop", false);
    app.selectedLane = prefs.getUChar("lane", 0);
    if (app.bpmX10 < 400 || app.bpmX10 > 2400) app.bpmX10 = 1200;
    if (app.bars < 1 || app.bars > MAX_BARS) app.bars = 1;
    if (app.layers < 1 || app.layers > MAX_LAYERS) app.layers = MAX_LAYERS;
    if (app.selectedLane > 3) app.selectedLane = 0;

    ui.begin();
    ui.boot(0, 0);

    looper.begin(s_pool, POOL_BYTES);
    looper.setBpm(app.bpmX10);
    looper.setBars(app.bars);
    looper.setLayers(app.layers);
    looper.setClick(app.click);
    looper.setCountIn(app.countIn);
    looper.setLoopMode(app.loopMode);

    audioOut.begin();
    usbAudio.begin();
    audioOut.start();

    app.sdOk = library.begin();
    if (app.sdOk) {
        SD.mkdir(LOOP_DIR);
        refreshLibrary();
    }

    const int batt = M5.Power.getBatteryLevel();
    app.battery = (batt >= 0 && batt <= 100) ? (uint8_t)batt : 80;

    app.screen = Screen::LOOP;
    ui.boot(100, library.count());
    delay(1200);
    ui.render();

    Serial.printf("[wavelooper] codec=%d sd=%d files=%d heap=%u\n",
                  audioOut.codecOk() ? 1 : 0, app.sdOk ? 1 : 0, library.count(),
                  (unsigned)ESP.getFreeHeap());
}

// ---------------------------------------------------------------- loop
void loop()
{
    M5Cardputer.update();
    syncApp();

    static bool wasStream = false;
    if (wasStream && !app.usbStream &&
        (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED)) {
        looper.request(REQ_STOP_REC);
        setToast("USB LOST");
    }
    wasStream = app.usbStream;

    app.inLevel = usbAudio.level();

    handleSerial();
    handleKeys();
    syncApp();

    static uint32_t lastBat = 0;
    if (millis() - lastBat > 10000) {
        lastBat = millis();
        const int b = M5.Power.getBatteryLevel();
        if (b >= 0 && b <= 100) app.battery = (uint8_t)b;
    }

    static uint32_t lastRender = 0;
    if (millis() - lastRender >= 33) {
        lastRender = millis();
        ui.render();
    }

    delay(2);
}
