#!/usr/bin/env python3
# =============================================================================
# Rendu des écrans e-paper pour la documentation (screenshots)
# =============================================================================
# Reproduit FIDÈLEMENT le dessin de src/display.cpp hors matériel : parse les
# polices bitmap Adafruit (FreeMono*pt7b.h), réimplémente les primitives
# GxEPD2/Adafruit_GFX (ligne, rect, triangle, texte) et rejoue chaque écran aux
# mêmes coordonnées.
#
# Usage : python3 tools/render_screens.py
# Sortie : docs/screens/*.png (122x250 portrait, agrandis x3)
#
# ⚠️ Ce script est une COPIE du rendu : si display.cpp change, le régénérer.
# =============================================================================

import re
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FONTS_DIR = ROOT / ".pio/libdeps/upesy_wroom/Adafruit GFX Library/Fonts"
OUT_DIR = ROOT / "docs/screens"

W, H = 122, 250
TANK_FULL_KG = 670.0
BLACK, WHITE = 0, 1  # GxEPD_BLACK / GxEPD_WHITE


def cdiv(a, b):
    """Division entière C (troncature vers zéro, contrairement à //)."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


# =============================================================================
# PARSING DES POLICES ADAFRUIT
# =============================================================================
class Font:
    def __init__(self, bitmaps, glyphs, first, last, yadvance):
        self.bitmaps = bitmaps
        self.glyphs = glyphs          # liste de (bo, w, h, xa, xo, yo)
        self.first = first
        self.last = last
        self.yadvance = yadvance


def parse_font(name):
    text = (FONTS_DIR / f"{name}.h").read_text()
    body = re.search(rf"{name}Bitmaps\[\]\s*PROGMEM\s*=\s*\{{(.*?)\}};",
                     text, re.S).group(1)
    bitmaps = bytes(int(x, 16) for x in re.findall(r"0[xX][0-9a-fA-F]+", body))
    body = re.search(rf"{name}Glyphs\[\]\s*PROGMEM\s*=\s*\{{(.*?)\}};",
                     text, re.S).group(1)
    glyphs = [tuple(int(x) for x in re.findall(r"-?\d+", g))
              for g in re.findall(r"\{([^}]*)\}", body)]
    body = re.search(rf"GFXfont\s+{name}\s+PROGMEM\s*=\s*\{{(.*?)\}};",
                     text, re.S).group(1)
    fields = [f.strip() for f in body.split(",")]
    first, last, yadvance = (int(fields[-3], 0), int(fields[-2], 0),
                             int(fields[-1], 0))
    return Font(bitmaps, glyphs, first, last, yadvance)


# =============================================================================
# CANVAS (émulation Adafruit_GFX)
# =============================================================================
class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.buf = [0] * (w * h)  # 1 = encre (noir)
        self.font = None
        self.cx = self.cy = 0

    # --- primitives bas niveau ---
    def px(self, x, y, ink):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.buf[y * self.w + x] = 1 if ink else 0

    def drawPixel(self, x, y, color):
        self.px(x, y, color == BLACK)

    def fillScreen(self, color):
        self.buf = [1 if color == BLACK else 0] * (self.w * self.h)

    def writeFastHLine(self, x, y, w, color):
        ink = color == BLACK
        for i in range(w):
            self.px(x + i, y, ink)

    def drawFastVLine(self, x, y, h, color):
        ink = color == BLACK
        for i in range(h):
            self.px(x, y + i, ink)

    def drawLine(self, x0, y0, x1, y1, color):
        ink = color == BLACK
        steep = abs(y1 - y0) > abs(x1 - x0)
        if steep:
            x0, y0 = y0, x0
            x1, y1 = y1, x1
        if x0 > x1:
            x0, x1 = x1, x0
            y0, y1 = y1, y0
        dx = x1 - x0
        dy = abs(y1 - y0)
        err = cdiv(dx, 2)
        ystep = 1 if y0 < y1 else -1
        x = x0
        while x <= x1:
            if steep:
                self.px(y0, x, ink)
            else:
                self.px(x, y0, ink)
            err -= dy
            if err < 0:
                y0 += ystep
                err += dx
            x += 1

    def drawRect(self, x, y, w, h, color):
        self.writeFastHLine(x, y, w, color)
        self.writeFastHLine(x, y + h - 1, w, color)
        self.drawFastVLine(x, y, h, color)
        self.drawFastVLine(x + w - 1, y, h, color)

    def fillRect(self, x, y, w, h, color):
        ink = color == BLACK
        for j in range(h):
            for i in range(w):
                self.px(x + i, y + j, ink)

    def fillTriangle(self, x0, y0, x1, y1, x2, y2, color):
        ink = color == BLACK

        def hline(a, b, yy):
            if a > b:
                a, b = b, a
            for xx in range(a, b + 1):
                self.px(xx, yy, ink)

        if y0 > y1:
            y0, y1, x0, x1 = y1, y0, x1, x0
        if y1 > y2:
            y2, y1, x2, x1 = y1, y2, x1, x2
        if y0 > y1:
            y0, y1, x0, x1 = y1, y0, x1, x0
        if y0 == y2:
            hline(min(x0, x1, x2), max(x0, x1, x2), y0)
            return
        dx01, dy01 = x1 - x0, y1 - y0
        dx02, dy02 = x2 - x0, y2 - y0
        dx12, dy12 = x2 - x1, y2 - y1
        sa = sb = 0
        last = y1 if y1 == y2 else y1 - 1
        y = y0
        while y <= last:
            hline(x0 + cdiv(sa, dy01), x0 + cdiv(sb, dy02), y)
            sa += dx01
            sb += dx02
            y += 1
        sa = dx12 * (y - y1)
        sb = dx02 * (y - y0)
        while y <= y2:
            hline(x1 + cdiv(sa, dy12), x0 + cdiv(sb, dy02), y)
            sa += dx12
            sb += dx02
            y += 1

    # --- texte ---
    def setFont(self, font):
        self.font = font

    def setCursor(self, x, y):
        self.cx, self.cy = x, y

    def getTextBounds(self, s, x, y):
        minx, miny, maxx, maxy = 0x7FFF, 0x7FFF, -1, -1
        _x, _y = x, y
        for ch in s:
            c = ord(ch)
            if c == 10:  # '\n'
                _x = 0
                _y += self.font.yadvance
            elif c != 13:
                if self.font.first <= c <= self.font.last:
                    bo, gw, gh, xa, xo, yo = self.font.glyphs[c - self.font.first]
                    if gw > 0 and gh > 0:
                        x1 = _x + xo
                        y1 = _y + yo
                        x2 = x1 + gw - 1
                        y2 = y1 + gh - 1
                        minx, miny = min(minx, x1), min(miny, y1)
                        maxx, maxy = max(maxx, x2), max(maxy, y2)
                    _x += xa
        bx1, by1, bw, bh = x, y, 0, 0
        if maxx >= minx:
            bx1 = minx
            bw = maxx - minx + 1
        if maxy >= miny:
            by1 = miny
            bh = maxy - miny + 1
        return bx1, by1, bw, bh

    def print(self, s):
        for ch in s:
            self._drawChar(ord(ch))

    def _drawChar(self, c):
        f = self.font
        if c == 10:
            self.cx = 0
            self.cy += f.yadvance
            return
        if c == 13 or not (f.first <= c <= f.last):
            return
        bo, gw, gh, xa, xo, yo = f.glyphs[c - f.first]
        if gw > 0 and gh > 0:
            idx = bo * 8
            for row in range(gh):
                for col in range(gw):
                    byte = f.bitmaps[idx >> 3]
                    bit = (byte >> (7 - (idx & 7))) & 1
                    if bit:
                        self.px(self.cx + xo + col, self.cy + yo + row, True)
                    idx += 1
        self.cx += xa


# =============================================================================
# ÉCRANS (port de src/display.cpp)
# =============================================================================
FONT_BOLD9 = parse_font("FreeMonoBold9pt7b")
FONT_BOLD12 = parse_font("FreeMonoBold12pt7b")
FONT_BOLD18 = parse_font("FreeMonoBold18pt7b")
FONT_BOLD24 = parse_font("FreeMonoBold24pt7b")
FONT_MONO9 = parse_font("FreeMono9pt7b")


def centeredX(d, text):
    x1, y1, w1, h1 = d.getTextBounds(text, 0, 0)
    return cdiv(d.w - w1, 2) - x1


def printCentered(d, text, y):
    d.setCursor(centeredX(d, text), y)
    d.print(text)


# --- Aides bouton (2 lignes « geste / action », colonnes alignées) -----------
# Mêmes colonnes que src/display.cpp (kHintLabelX / kHintActionX / kHintY1/2).
HINT_LABEL_X, HINT_ACTION_X = 6, 66
HINT_Y1, HINT_Y2 = 226, 242


def drawHints(d, short_label, short_action, long_label, long_action):
    d.setFont(FONT_MONO9)
    d.setCursor(HINT_LABEL_X, HINT_Y1)
    d.print(short_label)
    d.setCursor(HINT_ACTION_X, HINT_Y1)
    d.print(short_action)
    d.setCursor(HINT_LABEL_X, HINT_Y2)
    d.print(long_label)
    d.setCursor(HINT_ACTION_X, HINT_Y2)
    d.print(long_action)


def drawBatteryIcon(d, percent, charging):
    # Batterie verticale vectorielle (cf. displayDrawBatteryIcon).
    bw, bh = 17, 32
    bx, by = d.w - bw - 4, 5
    nub_w, nub_h = 9, 4
    d.fillRect(bx + (bw - nub_w) // 2, by - nub_h, nub_w, nub_h, BLACK)
    d.fillRect(bx, by, bw, bh, WHITE)
    d.drawRect(bx, by, bw, bh, BLACK)
    d.drawRect(bx + 1, by + 1, bw - 2, bh - 2, BLACK)
    if charging:
        drawChargingBolt(d, bx + 4, by + 6, bw - 8, bh - 12)
    elif percent > 0:
        pct = min(percent, 100)
        fill_h = (bh - 6) * pct // 100
        d.fillRect(bx + 3, by + bh - 3 - fill_h, bw - 6, fill_h, BLACK)


def drawChargingBolt(d, bx, by, bw, bh):
    x0, y0 = bx + cdiv(bw * 62, 100), by
    x1, y1 = bx + cdiv(bw * 10, 100), by + cdiv(bh * 55, 100)
    x2, y2 = bx + cdiv(bw * 48, 100), by + cdiv(bh * 55, 100)
    x3, y3 = bx + cdiv(bw * 38, 100), by + bh
    x4, y4 = bx + cdiv(bw * 92, 100), by + cdiv(bh * 45, 100)
    x5, y5 = bx + cdiv(bw * 54, 100), by + cdiv(bh * 45, 100)
    d.fillTriangle(x0, y0, x1, y1, x2, y2, BLACK)
    d.fillTriangle(x3, y3, x4, y4, x5, y5, BLACK)


def drawZ(d, x, y, size):
    d.drawLine(x, y, x + size, y, BLACK)
    d.drawLine(x + size, y, x, y + size, BLACK)
    d.drawLine(x, y + size, x + size, y + size, BLACK)


def drawSleepingTank(d, cx, top_y):
    bw, bh = 40, 30
    body_top = top_y + 6
    body_bot = body_top + bh
    d.drawRect(cx - bw - 4, top_y, (bw + 4) * 2, 5, BLACK)
    d.drawLine(cx - bw - 4, top_y + 1, cx - bw - 4, top_y + 3, BLACK)
    d.drawLine(cx + bw + 3, top_y + 1, cx + bw + 3, top_y + 3, BLACK)
    d.drawRect(cx - bw, body_top, bw * 2, bh, BLACK)
    throat, hop = 10, 12
    spout_y = body_bot + hop
    d.drawLine(cx - bw, body_bot, cx - throat, spout_y, BLACK)
    d.drawLine(cx + bw, body_bot, cx + throat, spout_y, BLACK)
    d.drawLine(cx - throat, spout_y, cx - throat, spout_y + 4, BLACK)
    d.drawLine(cx + throat, spout_y, cx + throat, spout_y + 4, BLACK)
    d.drawLine(cx - throat, spout_y + 4, cx + throat, spout_y + 4, BLACK)
    d.drawLine(cx - bw + 2, body_bot, cx - bw + 2, spout_y + 6, BLACK)
    d.drawLine(cx + bw - 2, body_bot, cx + bw - 2, spout_y + 6, BLACK)
    d.drawLine(cx - 12, body_top + 10, cx - 3, body_top + 10, BLACK)
    d.drawLine(cx + 3, body_top + 10, cx + 12, body_top + 10, BLACK)
    d.drawLine(cx - 4, body_top + 18, cx + 4, body_top + 18, BLACK)
    drawZ(d, cx + bw + 4, top_y - 2, 7)
    drawZ(d, cx + bw + 12, top_y + 6, 5)
    drawZ(d, cx + bw + 19, top_y + 13, 4)


def drawPellets(d, x0, y0, w, h):
    y = 0
    while y < h:
        off = (y // 3) % 2
        x = off
        while x < w:
            d.drawPixel(x0 + x, y0 + y, BLACK)
            x += 3
        y += 3


def drawOkoFenGauge(d, cx, top_y, body_h, level_pct):
    bw = 42
    body_top = top_y + 6
    body_bot = body_top + body_h
    d.drawRect(cx - bw - 4, top_y, (bw + 4) * 2, 5, BLACK)
    d.drawRect(cx - bw, body_top, bw * 2, body_h, BLACK)
    if level_pct > 0:
        level_pct = min(level_pct, 100)
        fill_h = cdiv(body_h * level_pct, 100)
        fill_top = body_bot - fill_h
        drawPellets(d, cx - bw + 1, fill_top, bw * 2 - 2, fill_h)
        d.drawLine(cx - bw, fill_top, cx + bw, fill_top, BLACK)
    throat, hop = 11, 14
    spout_y = body_bot + hop
    d.drawLine(cx - bw, body_bot, cx - throat, spout_y, BLACK)
    d.drawLine(cx + bw, body_bot, cx + throat, spout_y, BLACK)
    d.drawLine(cx - throat, spout_y, cx - throat, spout_y + 4, BLACK)
    d.drawLine(cx + throat, spout_y, cx + throat, spout_y + 4, BLACK)
    d.drawLine(cx - throat, spout_y + 4, cx + throat, spout_y + 4, BLACK)
    d.drawLine(cx - bw + 2, body_bot, cx - bw + 2, spout_y + 6, BLACK)
    d.drawLine(cx + bw - 2, body_bot, cx + bw - 2, spout_y + 6, BLACK)


def showMain(weight, battery, charging, hbuf="18:00", dbuf="02/10"):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    d.setFont(FONT_MONO9)
    d.setCursor(5, 17)
    d.print(hbuf)
    d.setCursor(5, 32)
    d.print(dbuf)
    drawBatteryIcon(d, battery, charging)
    d.drawLine(0, 38, W, 38, BLACK)
    if weight < 0:
        d.setFont(FONT_BOLD9)
        printCentered(d, "capteurs", 130)
        printCentered(d, "illisibles", 148)
        return d
    pct = min(int(weight / TANK_FULL_KG * 100.0), 100)
    silo_cx, body_top, body_h = W // 2, 62, 96
    body_bot = body_top + body_h
    drawOkoFenGauge(d, silo_cx, body_top - 6, body_h, pct)
    pctbuf = f"{pct}%"
    d.setFont(FONT_BOLD18)
    x1, y1, w1, h1 = d.getTextBounds(pctbuf, 0, 0)
    pct_cy = cdiv(body_top + body_bot, 2)
    cur_x = silo_cx - x1 - cdiv(w1, 2)
    cur_y = pct_cy - y1 - cdiv(h1, 2)
    bx, by = cur_x + x1 - 3, cur_y + y1 - 3
    d.fillRect(bx, by, w1 + 6, h1 + 6, WHITE)
    d.drawRect(bx, by, w1 + 6, h1 + 6, BLACK)
    d.setCursor(cur_x, cur_y)
    d.print(pctbuf)

    wbuf = f"{int(round(weight))}"
    d.setFont(FONT_BOLD24)
    wx1, wy1, ww1, wh1 = d.getTextBounds(wbuf, 0, 0)
    d.setFont(FONT_BOLD9)
    kx1, ky1, kw1, kh1 = d.getTextBounds("kg", 0, 0)
    total_w = ww1 + 5 + kw1
    zone_cx = cdiv(0 + W, 2)
    zone_cy = cdiv(184 + H, 2)
    d.setFont(FONT_BOLD24)
    w_cursor_y = zone_cy - wy1 - cdiv(wh1, 2)
    w_cursor_x = zone_cx - cdiv(total_w, 2) - wx1
    d.setCursor(w_cursor_x, w_cursor_y)
    d.print(wbuf)
    d.setFont(FONT_BOLD9)
    kg_x = w_cursor_x + wx1 + ww1 + 5 - kx1
    kg_y = (w_cursor_y + wy1 + wh1) - ky1 - kh1
    d.setCursor(kg_x, kg_y)
    d.print("kg")
    return d


def showDedock(battery, charging):
    # Dédock CONFIRMÉ (ligne DOUT basse) : écran « Boitier retire ».
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    drawBatteryIcon(d, battery, charging)
    drawSleepingTank(d, W // 2 - 10, 56)
    d.setFont(FONT_BOLD12)
    printCentered(d, "Boitier", 148)
    printCentered(d, "retire", 168)
    d.setFont(FONT_BOLD9)
    printCentered(d, "hors de", 190)
    printCentered(d, "sa base", 206)
    return d


def showSensorsKo(battery, charging):
    # 0/4 sans signal de dédock : panne capteur / câblage.
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    drawBatteryIcon(d, battery, charging)
    drawSleepingTank(d, W // 2 - 10, 56)
    d.setFont(FONT_BOLD12)
    printCentered(d, "Capteurs", 148)
    printCentered(d, "KO", 168)
    d.setFont(FONT_BOLD9)
    printCentered(d, "boitier", 190)
    printCentered(d, "hors base", 206)
    printCentered(d, "ou panne", 224)
    printCentered(d, "capteur", 240)
    return d


def showRefilling(before_kg, bag_count):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    pct = min(int(before_kg / TANK_FULL_KG * 100.0), 100)
    drawOkoFenGauge(d, W // 2, 26, 70, pct)
    cx = W // 2
    for i in range(22):
        d.drawPixel(cx - 38 + ((i * 7) % 76), 2 + ((i * 13) % 16), BLACK)
    cnt = f"x{bag_count}"
    d.setFont(FONT_BOLD24)
    x1, y1, w1, h1 = d.getTextBounds(cnt, 0, 0)
    d.setCursor(cdiv(W - w1, 2) - x1, 172)
    d.print(cnt)
    d.setFont(FONT_MONO9)
    xs, ys, ws, hs = d.getTextBounds("sacs", 0, 0)
    d.setCursor(cdiv(W - ws, 2) - xs, 194)
    d.print("sacs")
    drawHints(d, "court", "+1", "long", "finir")
    return d


def showPriceInput(digits, current_idx):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    d.setFont(FONT_BOLD9)
    printCentered(d, "Prix sac", 24)
    printCentered(d, "(15 kg)", 40)
    digit_w = 26
    comma_dx = 8
    d.setFont(FONT_BOLD24)
    gx, gy, gw, gh = d.getTextBounds("0", 0, 0)
    left_ink = -comma_dx + gx
    right_ink = 2 * digit_w + gx + gw - 1
    base_x = cdiv(W - (right_ink - left_ink + 1), 2) - left_ink
    for i in range(3):
        dx = base_x + i * digit_w - (comma_dx if i == 0 else 0)
        if i == current_idx:
            d.fillRect(dx - 2, 64, digit_w + 4, 42, WHITE)
            d.drawRect(dx - 2, 64, digit_w + 4, 42, BLACK)
        d.setCursor(dx, 100)
        d.print(str(digits[i]))
    d.setCursor(base_x + digit_w - 12 - comma_dx + 2, 104)
    d.print(",")
    d.setFont(FONT_BOLD9)
    printCentered(d, "EUR", 128)
    drawHints(d, "court", "+1", "long", "suite")
    return d


def showRefillDone(delta_kg, price_eur, bag_count):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    d.setFont(FONT_BOLD24)
    printCentered(d, f"+{int(delta_kg)}", 64)
    d.setFont(FONT_BOLD12)
    printCentered(d, "kg", 88)
    d.setFont(FONT_MONO9)
    printCentered(d, f"({bag_count} sac{'s' if bag_count > 1 else ''})", 116)
    d.setFont(FONT_BOLD9)
    printCentered(d, "pour", 148)
    cents = int(round(price_eur * 100))
    printCentered(d, f"{cents // 100},{cents % 100:02d} EUR", 166)
    drawHints(d, "court", "oui", "long", "non")
    return d


def showRefillResult(validated):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    d.setFont(FONT_BOLD12)
    printCentered(d, "Envoye !" if validated else "Annule !", 130)
    return d


# --- Écran du mode configuration (portail web) ------------------------------
# Reproduit drawArc()/drawWifiLogo() de src/display.cpp : GxEPD2 n'a pas de
# primitive arc, on échantillonne l'angle et on pose un carré plein par pas.

def draw_arc(d, cx, cy, radius, a_from, a_to, thickness):
    import math
    for a in range(a_from, a_to + 1):
        rad = math.radians(a)
        x = cx + int(math.sin(rad) * radius)   # int() tronque vers zéro (comme C)
        y = cy - int(math.cos(rad) * radius)
        d.fillRect(x - thickness // 2, y - thickness // 2,
                   thickness, thickness, BLACK)


def draw_wifi_logo(d, cx, base_y):
    d.fillRect(cx - 3, base_y - 3, 7, 7, BLACK)
    draw_arc(d, cx, base_y, 12, -45, 45, 3)
    draw_arc(d, cx, base_y, 24, -45, 45, 3)
    draw_arc(d, cx, base_y, 36, -45, 45, 4)


def showConfigPortal(ap_ssid="Scale-A1B2"):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    d.setFont(FONT_BOLD9)
    printCentered(d, "MODE", 24)
    printCentered(d, "CONFIG", 42)
    draw_wifi_logo(d, W // 2, 120)
    d.setFont(FONT_BOLD9)
    cut = ap_ssid.rfind("-")
    if len(ap_ssid) > 10 and cut >= 0:
        printCentered(d, ap_ssid[:cut + 1], 164)
        printCentered(d, ap_ssid[cut + 1:], 180)
    else:
        printCentered(d, ap_ssid, 168)
    d.setFont(FONT_MONO9)
    printCentered(d, "192.168.4.1", 232)
    return d


# =============================================================================
# SORTIE PNG (grayscale 8 bits, agrandissement nearest)
# =============================================================================
def write_png(path, canvas, scale=3):
    rows = []
    for y in range(canvas.h):
        row = bytearray()
        for x in range(canvas.w):
            v = 0 if canvas.buf[y * canvas.w + x] else 255
            row.extend([v] * scale)
        for _ in range(scale):
            rows.append(bytes(row))
    raw = b"".join(b"\x00" + r for r in rows)
    w, h = canvas.w * scale, canvas.h * scale

    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data +
                struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)
    print(f"  {path.relative_to(ROOT)}  ({w}x{h})")


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    screens = {
        "main": showMain(450.0, 100, False),
        "main_charge": showMain(450.0, 87, True),
        "dedock": showDedock(100, False),
        "dedock_ko": showSensorsKo(100, False),
        # ⚠️ Les écrans de l'ancien parcours de remplissage (versement,
        # saisie de prix, récap, résultat) ont été SUPPRIMÉS du firmware : leurs
        # rendus ne sont donc plus générés. Pour les 5 écrans du parcours
        # actuel, voir `tools/preview_charte.py` (géométrie 1:1) et les
        # maquettes `docs/UI/ECRAN11` → `ECRAN15`.
        "config": showConfigPortal(),
    }
    print("Rendu des écrans :")
    for name, canvas in screens.items():
        write_png(OUT_DIR / f"{name}.png", canvas)


if __name__ == "__main__":
    main()
