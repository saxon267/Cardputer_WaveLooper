#!/usr/bin/env python3
"""Render Cardputer_WaveLooper UI mockups (240x135, 3x) from the MidiLooper layout.

The glyphs are read from MidiLooper's font8.h (Silkscreen 8 px, 6x8 cell), so the
mockups use exactly the same type as the firmware.  Only the palette changes.

    python tools/make_ui_mockups.py      # needs Pillow (pip install pillow)
"""

from __future__ import annotations

import re
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]          # .../Cardputer
FONT_H = Path(__file__).resolve().parents[1] / "firmware" / "src" / "font8.h"
OUT = Path(__file__).resolve().parents[1] / "docs" / "ui"
SCALE = 3

W, H = 240, 135
TOP_H, BOT_H = 13, 11
BOT_Y = H - BOT_H

# ---------------------------------------------------------------- palette
# "Wave Blue": dark navy base, one bright blue accent.  Semantics kept from
# MidiLooper: red = REC, green/blue = play, amber = armed/count-in.
BG      = (0x0A, 0x15, 0x22)
BAR     = (0x10, 0x1E, 0x2F)
PANEL   = (0x14, 0x26, 0x3A)
PAD     = (0x1A, 0x30, 0x4A)
EDGE    = (0x2A, 0x4A, 0x6B)
LINE    = (0x1F, 0x35, 0x50)
TXT     = (0xE6, 0xF1, 0xFB)
LT2     = (0xC3, 0xD9, 0xEC)
DIM     = (0x8B, 0xA8, 0xC4)
FAINT   = (0x5C, 0x7A, 0x97)
ACC     = (0x4D, 0xA3, 0xFF)
ACC_DIM = (0x12, 0x3A, 0x5E)
AMB     = (0xFF, 0xC8, 0x57)
AQUA    = (0x45, 0xE0, 0xC8)
BLUE    = (0x7F, 0xB2, 0xFF)
PURPLE  = (0xA7, 0x8B, 0xFA)
RED     = (0xFF, 0x5D, 0x6C)
GREEN   = (0x3F, 0xDD, 0x8E)

LANE_COL = [ACC, AQUA, PURPLE, GREEN]
LANE_NAME = ["TAKE1", "TAKE2", "TAKE3", "TAKE4"]


# ---------------------------------------------------------------- font
def load_fonts():
    text = FONT_H.read_text(encoding="utf-8")
    groups = re.findall(r"\{([^{}]+)\}", text)
    glyphs = []
    for g in groups:
        vals = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", g)]
        if len(vals) == 8:
            glyphs.append(vals)
    assert len(glyphs) == 128, f"expected 128 glyphs, got {len(glyphs)}"
    return glyphs[:64], glyphs[64:]


REG, BOLD = load_fonts()


# ---------------------------------------------------------------- canvas
class Canvas:
    def __init__(self, w=W, h=H):
        self.w, self.h = w, h
        self.buf = bytearray(w * h * 3)

    def px(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 3
            self.buf[i:i + 3] = bytes(c)

    def rect(self, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.px(xx, yy, c)

    def frame(self, x, y, w, h, c):
        self.hline(x, y, w, c)
        self.hline(x, y + h - 1, w, c)
        self.vline(x, y, h, c)
        self.vline(x + w - 1, y, h, c)

    def hline(self, x, y, w, c):
        for xx in range(x, x + w):
            self.px(xx, y, c)

    def vline(self, x, y, h, c):
        for yy in range(y, y + h):
            self.px(x, yy, c)

    def char(self, x, y, ch, c, bold):
        o = ord(ch)
        if o < 32 or o > 95:
            o = ord("?")
        g = (BOLD if bold else REG)[o - 32]
        for row in range(8):
            bits = g[row]
            for b in range(6):
                if bits & (1 << b):
                    self.px(x + b, y + row, c)

    def text(self, x, y, s, c, bold=False):
        for ch in s.upper():
            self.char(x, y, ch, c, bold)
            x += 6

    def text_r(self, right, y, s, c, bold=False):
        self.text(right - 6 * len(s), y, s, c, bold)

    def text_c(self, cx, y, s, c, bold=False):
        self.text(cx - 3 * len(s), y, s, c, bold)

    def image(self):
        img = Image.frombytes("RGB", (self.w, self.h), bytes(self.buf))
        return img.resize((self.w * SCALE, self.h * SCALE), Image.NEAREST)


# ---------------------------------------------------------------- widgets
def dot(cv, x, y, c):
    cv.rect(x, y, 5, 5, c)
    cv.rect(x + 1, y + 1, 1, 1, BAR)


def battery(cv, x, y, pct):
    cv.frame(x, y, 14, 7, FAINT)
    cv.rect(x + 14, y + 2, 2, 3, FAINT)
    n = int(10 * min(pct, 100) / 100)
    cv.rect(x + 2, y + 2, n, 3, GREEN if pct > 25 else RED)


def tab(cv, x, label, active):
    cv.rect(x, 2, 42, 9, ACC if active else PAD)
    cv.frame(x, 2, 42, 9, ACC if active else EDGE)
    cv.text(x + 4, 3, label, BG if active else DIM, True)


def icon_play(cv, x, y, c):
    for j in range(9):
        cv.hline(x, y + j, 8 - abs(4 - j) * 2, c)


def icon_rec(cv, x, y, c):
    cv.rect(x, y, 8, 8, c)


def icon_stop(cv, x, y, c):
    cv.rect(x, y, 8, 8, c)


def icon_armed(cv, x, y, c):
    cv.frame(x, y, 8, 8, c)
    cv.rect(x + 2, y + 2, 4, 4, c)


def icon_folder(cv, x, y, c):
    cv.rect(x, y + 1, 9, 6, c)
    cv.rect(x, y, 4, 2, c)
    cv.rect(x + 2, y + 3, 5, 1, BAR)


def icon_note(cv, x, y, c):
    cv.rect(x + 4, y, 1, 8, c)
    cv.rect(x + 4, y, 3, 1, c)
    cv.rect(x + 1, y + 5, 3, 3, c)
    cv.rect(x, y + 6, 1, 1, c)


def topbar(cv, screen, in_level=0, usb=GREEN, sd=True, batt=80):
    cv.rect(0, 0, W, TOP_H, BAR)
    cv.hline(0, TOP_H - 1, W, LINE)
    tab(cv, 4, "LOOP", screen == "loop")
    tab(cv, 48, "FILE", screen == "file")

    # input level: 8 segments, lit from the left
    cv.text(92, 3, "IN", FAINT)
    for s in range(8):
        x = 108 + s * 4
        cv.rect(x, 4, 3, 5, ACC if s < in_level else LINE)

    dot(cv, 148, 5, usb)
    cv.text(156, 3, "USB", DIM)
    dot(cv, 182, 5, AQUA if sd else RED)
    cv.text(190, 3, "SD", DIM)
    battery(cv, 214, 4, batt)


def progress(cv, y, permille, state, has_loop, bars=1):
    px, pw, ph = 6, 228, 7
    cv.frame(px - 1, y - 1, pw + 2, ph + 2, EDGE)
    cv.rect(px, y, pw, ph, LINE)
    p = max(0, min(1000, permille))
    fw = pw * p // 1000
    if state == "rec":
        cv.rect(px, y, fw, ph, RED)
    elif state == "play":
        cv.rect(px, y, fw, ph, ACC)
    elif has_loop:
        cv.rect(px, y, fw, ph, ACC_DIM)
    beats = 4 * bars
    for i in range(beats + 1):
        x = px + (i * pw // beats if beats else 0)
        bar = (i % 4) == 0
        cv.rect(x, y, 1, ph if bar else 3, FAINT if bar else BG)
    cv.rect(px + fw - 1, y - 2, 2, ph + 4, TXT)


def lane_row(cv, i, y, sel, muted, flash, level, dur, state):
    col = LANE_COL[i]
    if sel:
        cv.rect(2, y, 236, 17, PAD)
        cv.rect(2, y, 2, 17, col)
    if flash:
        cv.rect(2, y, 236, 17, PAD)
        cv.hline(2, y + 16, 236, col)

    cv.text(7, y + 5, f"L{i + 1}", TXT if flash else (FAINT if muted else col), True)
    cv.text(30, y + 5, LANE_NAME[i], TXT if flash else (DIM if not sel else TXT), sel)

    mx, my, cw, cg = 68, y + 5, 5, 1
    cv.rect(mx, my, 18 * (cw + cg) - cg, 6, LINE)
    for s in range(18):
        on = TXT if flash else (FAINT if muted else col)
        cv.rect(mx + s * (cw + cg), my, cw, 6, on if s < level else LINE)

    if state == "rec":
        cv.text_r(204, y + 5, "REC", RED, True)
    elif dur:
        cv.text_r(204, y + 5, dur, DIM)
    else:
        cv.text_r(204, y + 5, "--", DIM)
    cv.text(217, y + 5, "OFF" if muted else "ON", RED if muted else AQUA)


def bottom(cv, left, right=""):
    cv.rect(0, BOT_Y, W, BOT_H, BAR)
    cv.hline(0, BOT_Y - 1, W, LINE)
    cv.text(6, BOT_Y + 1, left, FAINT)
    if right:
        cv.text_r(234, BOT_Y + 1, right, FAINT)


def state_row(cv, kind, label, col, bars=1, bpm=120):
    y = 17
    if kind == "rec":
        icon_rec(cv, 7, y, RED)
    elif kind == "play":
        icon_play(cv, 7, y, GREEN)
    elif kind == "armed":
        icon_armed(cv, 7, y, AMB)
    else:
        icon_stop(cv, 7, y, FAINT)
    cv.text(20, y + 1, label, col, True)
    cv.text(96, y + 1, f"BAR 1/{bars}", TXT, True)
    cv.text(150, y + 1, "2.0S", DIM)
    cv.text_r(234, y + 1, f"{bpm} BPM", DIM)


# ---------------------------------------------------------------- screens
def screen_idle():
    cv = Canvas()
    cv.rect(0, 0, W, H, BG)
    topbar(cv, "loop", in_level=0)
    state_row(cv, "stop", "STOP", FAINT, bars=1)
    progress(cv, 31, 0, "stop", False, bars=1)
    for i in range(4):
        lane_row(cv, i, 44 + i * 18, sel=(i == 0), muted=False, flash=False,
                 level=0, dur="", state="empty")
    bottom(cv, "R REC  ENT PLAY  S SAVE", "TAB FILE")
    return cv


def screen_rec():
    cv = Canvas()
    cv.rect(0, 0, W, H, BG)
    topbar(cv, "loop", in_level=6)
    state_row(cv, "rec", "OVERDUB", ACC, bars=1)
    progress(cv, 31, 680, "rec", True, bars=1)
    lane_row(cv, 0, 44, sel=False, muted=False, flash=False, level=11,
             dur="2.0S", state="ok")
    lane_row(cv, 1, 62, sel=False, muted=False, flash=False, level=5,
             dur="2.0S", state="ok")
    lane_row(cv, 2, 80, sel=True, muted=False, flash=True, level=13,
             dur="", state="rec")
    lane_row(cv, 3, 98, sel=False, muted=False, flash=False, level=0,
             dur="", state="empty")
    bottom(cv, "R STOP  ENT PLAY  S SAVE", "U UNDO")
    return cv


def screen_files():
    cv = Canvas()
    cv.rect(0, 0, W, H, BG)
    topbar(cv, "file", in_level=0)
    cv.rect(0, 13, W, 11, PANEL)
    cv.hline(0, 24, W, LINE)
    icon_folder(cv, 5, 16, AMB)
    cv.text(17, 16, "/WAVELOOP", BLUE, True)
    cv.text_r(234, 16, "4 FILES", FAINT)

    files = [("LOOP_0001.WAV", "96K"), ("LOOP_0002.WAV", "192K"),
             ("TAKE_A.WAV", "48K"), ("TAKE_B.WAV", "96K")]
    for i, (name, size) in enumerate(files):
        y = 27 + i * 16
        sel = i == 0
        if sel:
            cv.rect(0, y, W, 16, ACC)
        icon_note(cv, 6, y + 4, BG if sel else AMB)
        cv.text(18, y + 4, name, BG if sel else TXT, sel)
        cv.text_r(230, y + 4, size, BG if sel else FAINT)
    bottom(cv, "D DEL   N RENAME", "TAB LOOP")
    return cv


def screen_settings():
    cv = screen_idle()
    cv.rect(0, 13, W, H - 13, BG)
    cv.frame(28, 16, 184, 104, EDGE)
    cv.rect(29, 17, 182, 102, PANEL)
    cv.text_c(W // 2, 21, "SETTINGS", BLUE, True)

    rows = [("BPM", "120"), ("BARS", "1"), ("LAYERS", "4"),
            ("RATE", "24K"), ("CLICK", "ON"), ("COUNT-IN", "1 BAR")]
    for i, (label, value) in enumerate(rows):
        y = 32 + i * 12
        if i == 2:
            cv.rect(32, y - 2, 176, 12, PAD)
            cv.rect(32, y - 2, 2, 12, ACC)
        cv.text(36, y, label, ACC if i == 2 else DIM, i == 2)
        cv.text_r(176, y, value, TXT, True)
        cv.text(184, y, "< >", FAINT)
    cv.text_c(W // 2, 108, "UP/DN  LEFT/RIGHT  ESC", FAINT)
    return cv


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    screens = {
        "loop-idle": screen_idle(),
        "loop-rec": screen_rec(),
        "files": screen_files(),
        "settings": screen_settings(),
    }
    for name, cv in screens.items():
        path = OUT / f"{name}.png"
        cv.image().save(path)
        print(f"wrote {path} ({W * SCALE}x{H * SCALE})")


if __name__ == "__main__":
    main()
