#!/usr/bin/env python3
"""Rendu hors materiel de la CHARTE UI, ecran par ecran, a l'echelle 1:1.

Rejoue la geometrie de `src/display.cpp` (grille, composants, polices de
`include/fonts/`) avec les Canvas/primitives de `render_screens.py`. Sert a
verifier qu'un ecran ne percute rien et ne deborde pas AVANT le flash sur la
carte — c'est ainsi qu'ont ete valides la legende `Ajout reel` (le jambage du
« j » touchait le héros), le place de `FERMER` dans le menu et les 2 lignes du
recap d'ECRAN15 (crochets hors marges en JbmXb5).

    python3 tools/preview_charte.py       # -> tools/preview/*.png (echelle 3)

Les maquettes `docs/UI/ECRANnn/screen.png` sont le rendu DESIGN (HTML, 427 px,
polices de substitution) ; ce script est le rendu PIXEL, c'est-a-dire la
geometrie reellement envoyee a la dalle.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import render_screens as rs  # noqa: E402

BLACK, WHITE = rs.BLACK, rs.WHITE
W, H = rs.W, rs.H
Canvas = rs.Canvas
cdiv = rs.cdiv

FONTS = ROOT / "include/fonts"


def parse_charte_font(fname, sym):
    text = (FONTS / fname).read_text()
    name = sym
    body = re.search(rf"{name}Bitmaps\[\]\s*PROGMEM\s*=\s*\{{(.*?)\}};", text, rs.re.S).group(1)
    bitmaps = bytes(int(x, 16) for x in re.findall(r"0[xX][0-9a-fA-F]+", body))
    body = re.search(rf"{name}Glyphs\[\]\s*PROGMEM\s*=\s*\{{(.*?)\}};", text, rs.re.S).group(1)
    glyphs = [tuple(int(x) for x in re.findall(r"-?\d+", g))
              for g in re.findall(r"\{([^}]*)\}", body)]
    m = re.search(r"0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+),\s*(\d+)\s*\}", text)
    return rs.Font(bitmaps, glyphs, int(m.group(1), 16), int(m.group(2), 16), int(m.group(3)))


JBM4 = parse_charte_font("JbmXb4.h", "JbmXb4pt8b")
JBM5 = parse_charte_font("JbmXb5.h", "JbmXb5pt8b")
JBM6 = parse_charte_font("JbmXb6.h", "JbmXb6pt8b")
OSW10 = parse_charte_font("OswaldBold10.h", "OswaldBold10pt7b")
OSW30 = parse_charte_font("OswaldBold30.h", "OswaldBold30pt7b")

# --- Grille (src/display.cpp) ---
MARGIN_L, MARGIN_R, CENTER_X = 7, 116, 61
HDR_LINE_Y, STATUS_Y = 20, 29
BANNER_TOP, BANNER_H = 36, 20
HERO_Y, GAUGE_LBL_Y, GAUGE_TOP, GAUGE_H = 152, 170, 176, 15
SEC_TOP, SEC_BOT, SEC_Y, SEC_L1, SEC_L2 = 194, 216, 208, 203, 214
FOOT_LINE_Y, FOOT_Y1, FOOT_Y2 = 221, 233, 244
TITLE_BANNER_TOP, TITLE_BANNER_H, TITLE_BANNER_TEXT_Y = 66, 18, 79

# --- NOUVEAU : pointe du bandeau ---
PTR_HALF_W, PTR_H = 12, 8


def draw_char(d, font, x, y, s, inverted=False):
    d.setFont(font)
    for ch in s:
        cp = ord(ch)
        d.cx, d.cy = x, y
        cp = fold(cp)
        if font.first <= cp <= font.last:
            bo, gw, gh, xa, xo, yo = font.glyphs[cp - font.first]
            if gw and gh:
                idx = bo * 8
                for row in range(gh):
                    for col in range(gw):
                        byte = font.bitmaps[idx >> 3]
                        if (byte >> (7 - (idx & 7))) & 1:
                            d.px(x + xo + col, y + yo + row, not inverted)
                        idx += 1
        x += xa
    return x


def fold(cp):
    return 0x100 if cp == 0x20AC else cp


def text_w(font, s):
    return sum(font.glyphs[fold(ord(c)) - font.first][3]
               for c in s if font.first <= fold(ord(c)) <= font.last)


def draw_utf8_centered(d, font, cx, y, s):
    draw_char(d, font, cx - text_w(font, s) // 2, y, s)


def draw_utf8_right(d, font, x_right, y, s):
    draw_char(d, font, x_right - text_w(font, s), y, s)


def draw_utf8_centered_inv(d, font, cx, y, s):
    draw_char(d, font, cx - text_w(font, s) // 2, y, s, inverted=True)


def draw_battery3seg(d, x, y, w, h, pct):
    d.drawRect(x, y, w, h, BLACK)
    d.fillRect(x + w, y + h // 2 - 2, 2, 4, BLACK)
    seg = 0 if pct == 0 else (1 if pct <= 33 else (2 if pct <= 66 else 3))
    for i in range(seg):
        d.fillRect(x + 2 + i * 6, y + 2, 5, h - 4, BLACK)


# Zone rechargeable de l'en-tête : éclair de charge + batterie 3 segments.
# Partagée par le rendu complet et par les deux partials d'en-tête (MAIN, nomade).
HDR_BOLT_X, HDR_BOLT_Y, HDR_BOLT_W, HDR_BOLT_H = 78, 4, 10, 12
HDR_BATT_ZONE = (72, 0, 48, 19)


def draw_header_battery_zone(d, batt, charging=False):
    x, y, w, h = HDR_BATT_ZONE
    d.fillRect(x, y, w, h, WHITE)
    if charging:
        draw_charging_bolt(d, HDR_BOLT_X, HDR_BOLT_Y, HDR_BOLT_W, HDR_BOLT_H)
    draw_battery3seg(d, 92, 5, 22, 10, batt)


def draw_charging_bolt(d, bx, by, bw, bh):
    """Éclair vectoriel (2 triangles pleins), comme drawChargingBolt()."""
    pts = [(bx + bw * 62 // 100, by), (bx + bw * 10 // 100, by + bh * 55 // 100),
           (bx + bw * 48 // 100, by + bh * 55 // 100),
           (bx + bw * 38 // 100, by + bh), (bx + bw * 92 // 100, by + bh * 45 // 100),
           (bx + bw * 54 // 100, by + bh * 45 // 100)]
    for tri in ((0, 1, 2), (3, 4, 5)):
        (x0, y0), (x1, y1), (x2, y2) = [pts[k] for k in tri]
        d.fillTriangle(x0, y0, x1, y1, x2, y2, BLACK)


def draw_header(d, batt, charging=False, date="02/10/24"):
    draw_char(d, JBM5, MARGIN_L, 13, date)
    draw_header_battery_zone(d, batt, charging)
    d.drawLine(MARGIN_L, HDR_LINE_Y, MARGIN_R, HDR_LINE_Y, BLACK)


def draw_dot(d, cx, cy, r):
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                d.px(x, y, True)


def draw_dash(d, x, y, w):
    d.fillRect(x, y, w, 1, BLACK)


def draw_footer(d, short_r, long_r):
    d.drawLine(MARGIN_L, FOOT_LINE_Y, MARGIN_R, FOOT_LINE_Y, BLACK)
    draw_dot(d, MARGIN_L + 2, 230, 2)
    draw_char(d, JBM4, MARGIN_L + 7, FOOT_Y1, "COURT")
    draw_utf8_right(d, JBM4, MARGIN_R, FOOT_Y1, short_r)
    draw_dash(d, MARGIN_L, 243, 5)
    draw_char(d, JBM4, MARGIN_L + 8, FOOT_Y2, "LONG")
    draw_utf8_right(d, JBM4, MARGIN_R, FOOT_Y2, long_r)


def draw_footer_one(d, short_r):
    d.drawLine(MARGIN_L, FOOT_LINE_Y, MARGIN_R, FOOT_LINE_Y, BLACK)
    draw_dot(d, MARGIN_L + 2, 230, 2)
    draw_char(d, JBM4, MARGIN_L + 7, FOOT_Y1, "COURT")
    draw_utf8_right(d, JBM4, MARGIN_R, FOOT_Y1, short_r)


def draw_footer_long_only(d, long_r):
    """Une seule ligne, celle du bas (celle de la tenue) : `— LONG` + action."""
    d.drawLine(MARGIN_L, FOOT_LINE_Y, MARGIN_R, FOOT_LINE_Y, BLACK)
    draw_dash(d, MARGIN_L, 243, 5)
    draw_char(d, JBM4, MARGIN_L + 8, FOOT_Y2, "LONG")
    draw_utf8_right(d, JBM4, MARGIN_R, FOOT_Y2, long_r)


def draw_secondary_box(d, font, msg):
    mw = text_w(font, msg)
    xL = CENTER_X - mw // 2 - 8
    xR = CENTER_X + mw // 2 + 8
    d.drawLine(xL, SEC_TOP, xL, SEC_BOT, BLACK)
    d.drawLine(xL, SEC_TOP, xL + 4, SEC_TOP, BLACK)
    d.drawLine(xL, SEC_BOT, xL + 4, SEC_BOT, BLACK)
    d.drawLine(xR, SEC_TOP, xR, SEC_BOT, BLACK)
    d.drawLine(xR, SEC_TOP, xR - 4, SEC_TOP, BLACK)
    d.drawLine(xR, SEC_BOT, xR - 4, SEC_BOT, BLACK)
    draw_utf8_centered(d, font, CENTER_X, SEC_Y, msg)


def draw_secondary_box2(d, font, l1, l2):
    mw = max(text_w(font, l1), text_w(font, l2))
    xL = CENTER_X - mw // 2 - 8
    xR = CENTER_X + mw // 2 + 8
    for x, s in ((xL, 1), (xR, -1)):
        d.drawLine(x, SEC_TOP, x, SEC_BOT, BLACK)
        d.drawLine(x, SEC_TOP, x + 4 * s, SEC_TOP, BLACK)
        d.drawLine(x, SEC_BOT, x + 4 * s, SEC_BOT, BLACK)
    draw_utf8_centered(d, font, CENTER_X, SEC_L1, l1)
    draw_utf8_centered(d, font, CENTER_X, SEC_L2, l2)


def draw_inverted_banner(d, top, h, font, text, text_y):
    """Bandeau inverse + pointe plein centree vers le bas (NOUVEAU)."""
    d.fillRect(MARGIN_L, top, MARGIN_R - MARGIN_L + 1, h, BLACK)
    draw_utf8_centered_inv(d, font, CENTER_X, text_y, text)
    bottom = top + h
    d.fillTriangle(CENTER_X - PTR_HALF_W, bottom,
                   CENTER_X + PTR_HALF_W - 1, bottom,
                   CENTER_X, bottom + PTR_H, BLACK)


def show_main(weight, batt, charging=False, low=False, total_kg=670.0,
              last_kg=327.0, fault=0):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt, charging)
    st = ("KO" if weight < 0 else "WIFI DÉCONNECTÉ" if fault == 1
          else "MQTT DÉCONNECTÉ" if fault == 2 else "CONNECTÉ")
    sx = CENTER_X - (text_w(JBM4, st) + 7) // 2
    draw_dot(d, sx + 2, 27, 2)
    draw_char(d, JBM4, sx + 7, STATUS_Y, st)
    ko = weight < 0
    if ko:
        # Dégradé (1-3/4) : bandeau « CAPTEUR(S) HS » à la place de « NIVEAU
        # BAS », et le DERNIER poids stable (jamais une mesure actuelle).
        draw_inverted_banner(d, BANNER_TOP, BANNER_H, JBM6, "CAPTEUR(S) HS", 50)
        if last_kg is None:
            draw_utf8_centered(d, OSW10, CENTER_X, HERO_Y, "--")
            draw_secondary_box(d, JBM4, "AUCUNE MESURE")
            draw_footer(d, "MESURER", "OPTIONS")
            return d
        weight = last_kg
    elif low:
        draw_inverted_banner(d, BANNER_TOP, BANNER_H, JBM6, "NIVEAU BAS", 50)
    wbuf = f"{int(weight)}"
    hw = text_w(OSW30, wbuf)
    uw = text_w(OSW10, "KG")
    x = CENTER_X - (hw + 3 + uw) // 2
    draw_char(d, OSW30, x, HERO_Y, wbuf)
    draw_char(d, OSW10, x + hw + 3, HERO_Y, "KG")
    draw_char(d, JBM4, MARGIN_L, GAUGE_LBL_Y, "CAPACITÉ")
    pct = 0 if weight < 0 else min(100, int(weight * 100 / total_kg))
    draw_utf8_right(d, JBM4, MARGIN_R, GAUGE_LBL_Y, f"{pct} %")
    d.drawRect(MARGIN_L, GAUGE_TOP, MARGIN_R - MARGIN_L + 1, GAUGE_H, BLACK)
    iw = (MARGIN_R - 1) - (MARGIN_L + 2) - 1
    d.fillRect(MARGIN_L + 2, GAUGE_TOP + 2, iw * pct // 100, GAUGE_H - 5, BLACK)
    if ko:
        draw_secondary_box2(d, JBM4, "DERNIÈRE MESURE", "STABLE À 18:12")
    elif fault:
        # Dernier envoi en échec (1 = WiFi, 2 = broker MQTT) : l'état du haut
        # dit ce qui est déconnecté, la zone du bas le constat.
        draw_secondary_box(d, JBM4, "ENVOI EN ERREUR")
    else:
        draw_secondary_box(d, JBM4, "REMPLISSAGE CONSEILLE" if low else "MESURÉ À 18:12")
    draw_footer(d, "MESURER", "OPTIONS")
    return d


def show_nomade(batt, charging=False, has_last=True):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt, charging)
    draw_utf8_centered(d, OSW10, CENTER_X, 38, "MODE")
    draw_utf8_centered(d, OSW10, CENTER_X, 60, "NOMADE")
    draw_inverted_banner(d, TITLE_BANNER_TOP, TITLE_BANNER_H, JBM6, "NON CONNECTÉ",
                         TITLE_BANNER_TEXT_Y)
    if has_last:
        wbuf = "327"
        hw = text_w(OSW30, wbuf)
        uw = text_w(OSW10, "KG")
        x = CENTER_X - (hw + 3 + uw) // 2
        draw_char(d, OSW30, x, HERO_Y, wbuf)
        draw_char(d, OSW10, x + hw + 3, HERO_Y, "KG")
        draw_utf8_centered(d, JBM4, CENTER_X, 172, "DERNIER POIDS CONNU")
        draw_secondary_box2(d, JBM4, "MESURÉ LE 07/10", "À 18:12")
    else:
        draw_utf8_centered(d, OSW10, CENTER_X, HERO_Y, "--")
        draw_secondary_box(d, JBM4, "AUCUNE MESURE")
    draw_footer_long_only(d, "OPTIONS")
    return d


def show_portail(ap_ssid="Scale-A1B2", ap_pass="48213907", configured=True, batt=82,
                 charging=False):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt)
    draw_utf8_centered(d, OSW10, CENTER_X, 38, "PORTAIL")
    draw_utf8_centered(d, OSW10, CENTER_X, 60, "ACTIF")
    draw_inverted_banner(d, TITLE_BANNER_TOP, TITLE_BANNER_H, JBM5,
                         "WIFI DE RÉGLAGES", TITLE_BANNER_TEXT_Y)
    draw_char(d, JBM5, MARGIN_L, 106, "RÉSEAU")
    draw_char(d, JBM5, MARGIN_L, 118, ap_ssid)
    draw_char(d, JBM5, MARGIN_L, 134, "MOT DE PASSE")
    draw_char(d, JBM5, MARGIN_L, 146, ap_pass)
    draw_char(d, JBM5, MARGIN_L, 162, "ADRESSE")
    draw_char(d, JBM5, MARGIN_L, 174, "http://192.168.4.1")
    draw_secondary_box(d, JBM4, "ARRÊT AUTO : 10 MIN")
    draw_footer_one(d, "REDEMARRER" if configured else "VEILLE")
    return d




def show_unconfigured_sleep(batt=82, charging=False):
    """Carte SANS configuration mise en veille (fermeture du portail)."""
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt, charging, date="--/--/--")
    draw_utf8_centered(d, OSW10, CENTER_X, 38, "CARTE")
    draw_utf8_centered(d, OSW10, CENTER_X, 60, "EN VEILLE")
    draw_inverted_banner(d, TITLE_BANNER_TOP, TITLE_BANNER_H, JBM6, "NON CONFIGURÉ",
                         TITLE_BANNER_TEXT_Y)
    draw_utf8_centered(d, JBM5, CENTER_X, 120, "AUCUN WI-FI")
    draw_utf8_centered(d, JBM5, CENTER_X, 136, "NI BROKER MQTT")
    draw_utf8_centered(d, JBM5, CENTER_X, 152, "ENREGISTRÉ")
    draw_secondary_box2(d, JBM4, "LE RÉVEIL OUVRE", "LE PORTAIL WEB")
    draw_footer_one(d, "RÉVEIL")
    return d


# --- ECRAN INFORMATIONS (2 pages) ---
INFO_COLON_X, INFO_COLON_X2 = 58, 46
INFO_TITLE_Y, INFO_ROW1Y, INFO_ROW_DY = 45, 66, 21
INFO_ROW_DY2 = 26


def draw_info_row(d, label, value, y, colon_x):
    draw_char(d, JBM5, MARGIN_L, y, label)
    draw_char(d, JBM5, colon_x, y, ":")
    draw_utf8_right(d, JBM5, MARGIN_R, y, value)


def show_infos_general(batt=82):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt)
    draw_utf8_centered(d, JBM6, CENTER_X, INFO_TITLE_Y, "INFORMATIONS")
    y = INFO_ROW1Y
    rows = [("Batt.", "82 %"), ("Wi-Fi", "OK"), ("MQTT", "OK"),
            ("Signal", "-61 dBm"), ("Capteurs", "4 OK")]
    for i, (l, v) in enumerate(rows):
        draw_info_row(d, l, v, y + i * INFO_ROW_DY, INFO_COLON_X)
    draw_secondary_box(d, JBM4, "PAGE 1 / 2")
    draw_footer(d, "AUTRE PAGE", "RETOUR")
    return d


def show_infos_sensors(batt=82, feet=(82.1, 80.7, 83.9, 80.7)):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt)
    draw_utf8_centered(d, JBM6, CENTER_X, INFO_TITLE_Y, "CAPTEURS")
    total = 0.0
    for i, (lbl, kg) in enumerate(zip(("Pied 1", "Pied 2", "Pied 3", "Pied 4"), feet)):
        total += kg
        draw_info_row(d, lbl, f"{kg:.1f} kg".replace(".", ","), INFO_ROW1Y + i * INFO_ROW_DY2,
                      INFO_COLON_X2)
    draw_info_row(d, "Total", f"{total:.1f} kg".replace(".", ","),
                  INFO_ROW1Y + 4 * INFO_ROW_DY2, INFO_COLON_X2)
    draw_secondary_box(d, JBM4, "PAGE 2 / 2")
    draw_footer(d, "AUTRE PAGE", "RETOUR")
    return d



# --- ECRAN CONFIRMATION DE PORTAIL ---
HOLD_CELLS, HOLD_CELL_W, HOLD_CELL_DX, HOLD_X0 = 10, 7, 8, 22
HOLD_BAR_Y, HOLD_BAR_H, HOLD_BR_X0, HOLD_BR_X1 = 200, 12, 16, 107


def draw_hold_bar(d, filled):
    top, bot = HOLD_BAR_Y - 2, HOLD_BAR_Y + HOLD_BAR_H + 1
    for x, s in ((HOLD_BR_X0, 1), (HOLD_BR_X1, -1)):
        d.drawLine(x, top, x, bot, BLACK)
        d.drawLine(x, top, x + 4 * s, top, BLACK)
        d.drawLine(x, bot, x + 4 * s, bot, BLACK)
    for i in range(HOLD_CELLS):
        x = HOLD_X0 + i * HOLD_CELL_DX
        if i < filled:
            d.fillRect(x, HOLD_BAR_Y, HOLD_CELL_W, HOLD_BAR_H, BLACK)
        else:
            d.drawRect(x, HOLD_BAR_Y, HOLD_CELL_W, HOLD_BAR_H, BLACK)


def show_portal_confirm(batt=82, filled=0):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt)
    draw_utf8_centered(d, JBM6, CENTER_X, 45, "PORTAIL WIFI")
    draw_inverted_banner(d, TITLE_BANNER_TOP, TITLE_BANNER_H, JBM5, "RÉGLAGES LOCAUX",
                         TITLE_BANNER_TEXT_Y)
    for i, line in enumerate(["Démarrer le", "portail Wi-Fi", "de réglage", "local ?"]):
        draw_utf8_centered(d, JBM6, CENTER_X, 106 + i * 20, line)
    draw_utf8_centered(d, JBM5, CENTER_X, 185, "MAINTENIR")
    draw_hold_bar(d, filled)
    draw_footer(d, "QUITTER", "LANCER")
    return d



# --- REMPLISSAGE étape 1/3 « EN COURS » ---
def draw_step_header(d, step=None, steps=3):
    draw_utf8_centered(d, JBM6, CENTER_X, 45, "REMPLISSAGE")


PROG_X, PROG_CW, PROG_DX, PROG_Y, PROG_H, PROG_N = 16, 5, 7, 200, 12, 10


def draw_progress_bar(d, step, steps=3):
    for x, sg in ((MARGIN_L, 1), (MARGIN_R, -1)):
        d.drawLine(x, SEC_TOP, x, SEC_BOT, BLACK)
        d.drawLine(x, SEC_TOP, x + 4 * sg, SEC_TOP, BLACK)
        d.drawLine(x, SEC_BOT, x + 4 * sg, SEC_BOT, BLACK)
    filled = step * PROG_N // steps
    for i in range(PROG_N):
        x = PROG_X + i * PROG_DX
        if i < filled:
            d.fillRect(x, PROG_Y, PROG_CW, PROG_H, BLACK)
        else:
            d.drawRect(x, PROG_Y, PROG_CW, PROG_H, BLACK)
    draw_utf8_right(d, JBM5, MARGIN_R - 9, PROG_Y + 9, f"{step}/{steps}")


def draw_step_body(d, value, unit, ctx1, ctx2, step):
    hw = text_w(OSW30, value)
    uw = text_w(OSW10, unit) if unit else 0
    x = CENTER_X - (hw + (3 + uw if unit else 0)) // 2
    draw_char(d, OSW30, x, HERO_Y, value)
    if unit:
        draw_char(d, OSW10, x + hw + 3, HERO_Y, unit)
    draw_utf8_centered(d, JBM5, CENTER_X, 176, ctx1)
    if ctx2:
        draw_utf8_centered(d, JBM5, CENTER_X, 190, ctx2)
    draw_progress_bar(d, step)


def show_refill_1(batt=82, added=185, total=462, level=92):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt)
    draw_step_header(d, 1)
    draw_inverted_banner(d, TITLE_BANNER_TOP, TITLE_BANNER_H, JBM6, "EN COURS",
                         TITLE_BANNER_TEXT_Y)
    draw_step_body(d, f"{added}", "KG", f"Total : {total} kg",
                   f"Niveau : {level} %", 1)
    draw_footer_one(d, "SUIVANT")
    return d


def show_refill_2(batt=82, bags=15):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt)
    draw_step_header(d, 2)
    draw_inverted_banner(d, TITLE_BANNER_TOP, TITLE_BANNER_H, JBM6, "NOMBRE DE SACS",
                         TITLE_BANNER_TEXT_Y)
    draw_step_body(d, f"{bags}", None, f"Théorie : {bags * 15} kg", None, 2)
    draw_footer(d, "+ 1 SAC", "VALIDER")
    return d


def draw_price_hero(d, digits, cur):
    buf = ""
    ws = []
    for i, dg in enumerate(digits):
        ch = str(dg)
        ws.append(text_w(OSW30, ch))
    wcomma = text_w(OSW30, ",")
    total = ws[0] + wcomma + ws[1] + ws[2]
    x = CENTER_X - total // 2
    xs = [x, x + ws[0] + wcomma, x + ws[0] + wcomma + ws[1]]
    dw = [ws[0], ws[1], ws[2]]
    ink_top, ink_h = HERO_Y - 47, 49
    bx, bw = xs[cur] - 2, dw[cur] + 4
    d.fillRect(bx, ink_top - 2, bw, ink_h + 4, WHITE)
    d.drawRect(bx, ink_top - 2, bw, ink_h + 4, BLACK)
    draw_char(d, OSW30, xs[0], HERO_Y, str(digits[0]))
    draw_char(d, OSW30, xs[0] + ws[0], HERO_Y, ",")
    draw_char(d, OSW30, xs[1], HERO_Y, str(digits[1]))
    draw_char(d, OSW30, xs[2], HERO_Y, str(digits[2]))


def show_refill_3(batt=82, digits=(6, 9, 0), cur=0, bags=15):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt)
    draw_step_header(d, 3)
    draw_inverted_banner(d, TITLE_BANNER_TOP, TITLE_BANNER_H, JBM6, "PRIX PAR SAC",
                         TITLE_BANNER_TEXT_Y)
    price = digits[0] + digits[1] / 10 + digits[2] / 100
    total = f"{price * bags:.2f}".replace(".", ",")
    draw_price_hero(d, digits, cur)
    draw_utf8_centered(d, JBM5, CENTER_X, 176, f"Total : {total} €")
    draw_progress_bar(d, 3)
    draw_footer(d, "PLUS", "VALIDER" if cur >= 2 else "SUIVANT")
    return d


# --- ECRAN14 « TERMINER ? » (validation, maintien) ---
LABEL_Y = 101   # étiquette « Ajout réel » : entre la pointe (92) et le héros (105)


def draw_recap_hero(d, added):
    draw_utf8_centered(d, JBM4, CENTER_X, LABEL_Y, "Ajout réel")
    wbuf = f"{added}"
    hw = text_w(OSW30, wbuf)
    uw = text_w(OSW10, "KG")
    x = CENTER_X - (hw + 3 + uw) // 2
    draw_char(d, OSW30, x, HERO_Y, wbuf)
    draw_char(d, OSW10, x + hw + 3, HERO_Y, "KG")


def dec2(v):
    c = int(round(v * 100))
    return f"{c // 100},{c % 100:02d}"


def show_refill_confirm(batt=82, added=185, bags=15, total_eur=103.50, filled=0):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt)
    draw_utf8_centered(d, JBM6, CENTER_X, 45, "TERMINER ?")
    draw_inverted_banner(d, TITLE_BANNER_TOP, TITLE_BANNER_H, JBM6, "VÉRIFIER",
                         TITLE_BANNER_TEXT_Y)
    draw_recap_hero(d, added)
    draw_utf8_centered(d, JBM5, CENTER_X, 176, f"{bags} sacs × 15 kg")
    draw_utf8_centered(d, JBM5, CENTER_X, 190, f"Coût : {dec2(total_eur)} €")
    draw_hold_bar(d, filled)
    draw_footer(d, "CORRIGER", "SAUVER")
    return d


# --- « CORRIGER ? » (appui court sur ECRAN14) ---
FIX_ITEMS = ["NOMBRE DE SACS", "PRIX DU SAC", "ANNULER", "RETOUR"]


def show_refill_fix(batt=82, sel=0):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt)
    draw_utf8_centered(d, OSW10, CENTER_X, 45, "CORRIGER ?")
    for i, label in enumerate(FIX_ITEMS):
        y = menu_row_y(i)
        if i == sel:
            d.fillRect(MARGIN_L, y - MENU_BAR_DY, MARGIN_R - MARGIN_L + 1,
                       MENU_BAR_H, BLACK)
        draw_menu_entry(d, JBM6, CENTER_X, y, label, i == sel)
    draw_secondary_box2(d, JBM4, "ANNULER : ENVOI", "SAUVAGE, SANS PRIX")
    draw_footer(d, "SUIVANT", "CHOISIR")
    return d


# --- ECRAN15 « ENREGISTRÉ » ---
def show_refill_saved(batt=82, added=185, bags=15, total_eur=103.50, total_kg=462):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt)
    draw_utf8_centered(d, JBM6, CENTER_X, 45, "ENREGISTRÉ")
    draw_inverted_banner(d, TITLE_BANNER_TOP, TITLE_BANNER_H, JBM5,
                         "REMPLISSAGE SAUVÉ", TITLE_BANNER_TEXT_Y)
    draw_recap_hero(d, added)
    per_kg = dec2(total_eur / added) if added > 0.5 else "--"
    draw_secondary_box2(d, JBM4, f"{bags} sacs · {dec2(total_eur)} €",
                        f"{per_kg} €/kg · {total_kg} kg")
    draw_footer_one(d, "PRINCIPAL")
    return d


# --- MENU OPTIONS (4 entrées, FERMER séparé) ---
MENU_ROW_Y0, MENU_ROW_DY, MENU_LAST_DY = 96, 24, 8
MENU_BAR_DY, MENU_BAR_H = 13, 18
MENU_ITEMS = ["REMPLISSAGE", "PORTAIL RÉGLAGES", "INFORMATIONS", "FERMER"]


MENU_ROWS_DEDOCK = [104, 136, 176]


def menu_row_y(i, dedock=False):
    """dedock : 3 entrées (pas de REMPLISSAGE), cf. menuRowY() de display.cpp."""
    if dedock:
        return MENU_ROWS_DEDOCK[i]
    return MENU_ROW_Y0 + i * MENU_ROW_DY + (MENU_LAST_DY if i == 3 else 0)


def draw_disabled_row(d, row, style="outline"):
    """Ligne INACTIVE : même place, même hauteur de barre, mais PAS de noir plein.

    style="outline" : simple contour 1 px — le libellé reste sur fond blanc, donc
                     parfaitement lisible ; le « plein » distingue la sélection.
    style="dither"  : tramé 25 % à l'intérieur du contour (le gris sur 1 bit).
    """
    y = menu_row_y(row)
    top = y - MENU_BAR_DY
    w = MARGIN_R - MARGIN_L + 1
    if style == "dither":
        for yy in range(top + 1, top + MENU_BAR_H - 1, 4):
            for xx in range(MARGIN_L + 1, MARGIN_R, 4):
                d.fillRect(xx, yy, 2, 2, BLACK)
    d.drawRect(MARGIN_L, top, w, MENU_BAR_H, BLACK)
    draw_menu_entry(d, JBM6, CENTER_X, y, MENU_ITEMS[row], False)


def show_options_dedock(batt=82, sel=1, style="outline"):
    """Menu hors base : 4 lignes comme sur la base, mais `REMPLISSAGE` est
    INACTIVE (fond tramé) et le curseur la saute."""
    if sel is None:                    # curseur sur la première ligne jouable
        sel = next(i for i in range(len(MENU_ITEMS)) if i != 0)
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt)
    draw_utf8_centered(d, OSW10, CENTER_X, 45, "OPTIONS")
    for i, label in enumerate(MENU_ITEMS):
        y = menu_row_y(i)
        if i == 0:                      # REMPLISSAGE : inactive hors base
            draw_disabled_row(d, i, style)
            continue
        if i == sel:
            d.fillRect(MARGIN_L, y - MENU_BAR_DY, MARGIN_R - MARGIN_L + 1,
                       MENU_BAR_H, BLACK)
        draw_menu_entry(d, JBM6, CENTER_X, y, label, i == sel)
    draw_footer(d, "SUIVANT", "CHOISIR")
    return d


def draw_menu_entry(d, font, cx, y, s, selected, track=1):
    w = sum(font.glyphs[fold(ord(c)) - font.first][3] for c in s) - track * (len(s) - 1)
    x = cx - w // 2
    for ch in s:
        draw_char(d, font, x, y, ch, inverted=selected)
        x += font.glyphs[fold(ord(ch)) - font.first][3] - track


def show_options(batt=82, sel=0):
    d = Canvas(W, H)
    d.fillScreen(WHITE)
    draw_header(d, batt)
    draw_utf8_centered(d, OSW10, CENTER_X, 45, "OPTIONS")
    for i, label in enumerate(MENU_ITEMS):
        y = menu_row_y(i)
        if i == sel:
            d.fillRect(MARGIN_L, y - MENU_BAR_DY, MARGIN_R - MARGIN_L + 1,
                       MENU_BAR_H, BLACK)
        draw_menu_entry(d, JBM6, CENTER_X, y, label, i == sel)
    draw_footer(d, "SUIVANT", "CHOISIR")
    return d


OUT = Path(__file__).resolve().parent / "preview"
OUT.mkdir(exist_ok=True)
rs.ROOT = OUT  # write_png affiche un chemin relatif a ROOT
screens = {
    "main_low": show_main(100.0, 82, low=True),
    "main": show_main(327.0, 82),
    "nomade": show_nomade(82),
    "portail": show_portail(),
    "portail_non_configure": show_portail(configured=False),
    "infos_1": show_infos_general(),
    "infos_2": show_infos_sensors(),
    "refill_1": show_refill_1(),
    "refill_2": show_refill_2(),
    "refill_3": show_refill_3(),
    "refill_3_b": show_refill_3(digits=(6, 3, 0), cur=2),
    "portal_confirm_0": show_portal_confirm(filled=0),
    "portal_confirm_5": show_portal_confirm(filled=5),
    "portal_confirm_10": show_portal_confirm(filled=10),
    "refill_confirm_0": show_refill_confirm(filled=0),
    "refill_confirm_5": show_refill_confirm(filled=5),
    "refill_saved": show_refill_saved(),
    "refill_fix_0": show_refill_fix(sel=0),
    "refill_fix_2": show_refill_fix(sel=2),
    "options_0": show_options(sel=0),
    "options_3": show_options(sel=3),
    "options_dedock": show_options_dedock(sel=1, style="outline"),
    "options_dedock_dither": show_options_dedock(sel=1, style="dither"),
    "nomade_footer_options": show_nomade(82),
    "main_charge": show_main(327.0, 82, charging=True),
    "main_wifi_ko": show_main(327.0, 82, fault=1),
    "main_mqtt_ko": show_main(327.0, 82, fault=2),
    "main_degraded": show_main(-1.0, 82),
    "main_degraded_none": show_main(-1.0, 82, last_kg=None),
    "veille_non_configure": show_unconfigured_sleep(),
    "nomade_charge": show_nomade(82, charging=True),
    "nomade_none": show_nomade(82, has_last=False),
}
for name, canvas in screens.items():
    rs.write_png(OUT / f"{name}.png", canvas)

# --docs : copie la sélection embarquée dans le README et le guide utilisateur
# (docs/screens/, versionné) — à relancer après toute modif de display.cpp.
DOCS_SCREENS = [
    "main", "main_low", "main_charge", "main_wifi_ko", "main_mqtt_ko", "main_degraded", "nomade", "nomade_none",
    "options_0", "options_dedock_dither", "infos_1", "infos_2",
    "refill_1", "refill_2", "refill_3", "refill_confirm_5", "refill_fix_0", "refill_saved",
    "portal_confirm_5", "portail", "portail_non_configure", "veille_non_configure",
]
if "--docs" in sys.argv:
    docs = ROOT / "docs/screens"
    docs.mkdir(parents=True, exist_ok=True)
    rs.ROOT = ROOT
    for name in DOCS_SCREENS:
        rs.write_png(docs / f"{name}.png", screens[name])