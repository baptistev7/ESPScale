#include "display.hpp"

#include "settings.hpp"
#include "pins.hpp"
#include "power.hpp"
#include "time.hpp"

#include <time.h>
#include <math.h>

// =============================================================================
// DECLARATION DE L'ECRAN
// =============================================================================

GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)> 
display(GxEPD2_DRIVER_CLASS(/*CS*/ (uint8_t)pins::EPD_CS, /*DC*/ (uint8_t)pins::EPD_DC,
                            /*RST*/ (uint8_t)pins::EPD_RST, /*BUSY*/ (uint8_t)pins::EPD_BUSY));

// =============================================================================
// IMPLEMENTATION
// =============================================================================

// =============================================================================
// CYCLE DE VIE DE LA DALLE + PARTIAL REFRESH
// =============================================================================
//
// Full refresh ~1,3 s (avec flash noir/blanc) ; partial ~856 ms sans flash
// (mesuré ; LUT du 213_BN, voir include/epd_panel.hpp).
// Le partial n'est possible que si la RAM de trame du SSD1680 est préservée :
//  - au sein d'une même session (ESP éveillé) : on n'hiberne pas la dalle, donc
//    aucun reset entre deux draws -> RAM intacte ;
//  - entre deux réveils : sur V2.3.1, `hibernate()` + pull-ups 100 kΩ RST/CS
//    gardaient la RAM ; sur V2.4 (coupure écran) elle est perdue, et seul
//    l'écran nomade la reconstruit (displayShowNomadeBattery).

// Nombre de partials consécutifs avant un full refresh de nettoyage (le partial
// laisse du ghosting qui s'accumule).
static const uint8_t kFullRefreshEveryN = 20;

// Dalle initialisée dans CETTE session (RAM : perdue au deep sleep).
static bool g_panel_ready = false;

// Orientation PORTRAIT (122 x 250). Rotation 0 = sens natif de la dalle ; passer
// à 2 si l'affichage apparaît à l'envers une fois le boîtier monté.
static const uint8_t kRotation = 0;

// Zones rafraîchies en partial (coordonnées écran ; en portrait, x et w
// doivent être multiples de 8 — contrainte d'adressage du SSD1680, GxEPD2
// arrondit sinon).

// true si on peut ne rafraîchir qu'une zone de `screen` : la dalle affiche déjà
// cet écran (cache RTC) et l'anti-ghosting n'a pas atteint sa limite.
static bool canPartial(DisplayScreen screen) {
#if USE_EPD_PWR_CUTOFF
    // T5 V2.4 : l'écran est totalement hors tension pendant le deep sleep
    // (epdPowerCut(), GPIO 12) -> la RAM de trame ne survit JAMAIS au
    // sommeil. Le premier draw de la session (dalle pas encore réinitialisée
    // depuis ce réveil) doit être FULL, quoi que dise le cache RTC ci-dessous.
    if (!g_panel_ready) return false;
#endif
    return g_state.disp_screen == screen &&
           g_state.disp_partial_run < kFullRefreshEveryN;
}

void displayPanelWake(RefreshMode mode, DisplayScreen screen) {
    if (!g_panel_ready) {
        // Restaure l'alimentation AVANT tout accès SPI/GPIO à l'écran (no-op
        // tant que USE_EPD_PWR_CUTOFF == 0).
        epdPowerRestore();
        {
            // Reset matériel appuyé du SSD1680 AVANT l'init GxEPD2 : la dalle
            // vient d'être ré-alimentée (GPIO 12), un pulse RST franc et long
            // remet le contrôleur dans un état connu — full comme partial (le
            // poll nomade restaure la RAM de trame puis fait un partial).
            gpio_set_direction(pins::EPD_RST, GPIO_MODE_OUTPUT);
            gpio_set_level(pins::EPD_RST, 0);
            delay(50); // reset asserté
            gpio_set_level(pins::EPD_RST, 1);
            delay(50); // reset relâché, stabilisation
        }
        // initial=false : pas de clearScreen (qui déclencherait un full refresh
        // inutile) et refresh(x,y,w,h) n'est plus promu en full -> partial OK.
        display.init(0, false, 10, false);
        display.setRotation(kRotation);
        g_panel_ready = true;
    }

    if (mode == RefreshMode::FULL) {
        g_state.disp_partial_run = 0;
    } else if (g_state.disp_partial_run < 255) {
        g_state.disp_partial_run++;
    }
    g_state.disp_screen = screen;
}

// Mode « rafale » : voir displaySetKeepPowered(). Pendant une rafale, on garde
// la dalle alimentée, donc le _PowerOn n'est pas refait à chaque refresh.
static bool s_keep_powered = false;

void displaySetKeepPowered(bool on) {
    s_keep_powered = on;
    // Fin de rafale : on coupe réellement l'alimentation.
    if (!on && g_panel_ready) display.powerOff();
}

void displayPanelOff() {
    // En rafale : on laisse la dalle alimentée (le _PowerOn est ainsi payé une
    // seule fois), on coupera à la fin via displaySetKeepPowered(false).
    if (s_keep_powered) return;
    // powerOff() coupe les tensions de la dalle ; le contrôleur reste actif et
    // sa RAM intacte -> un partial reste possible ensuite.
    display.powerOff();
}

void displayPanelDeepSleep() {
    if (!g_panel_ready) return; // dalle jamais initialisée : rien à endormir
    display.hibernate();
    // Coupe l'alimentation écran pour le deep sleep (T5 V2.4, GPIO 12) —
    // no-op tant que USE_EPD_PWR_CUTOFF == 0. Sur V2.3.1, hibernate() +
    // pull-ups 100 kΩ CS/RST restent le mécanisme de veille.
    epdPowerCut();
    g_panel_ready = false;
}

// Éclair vectoriel : polygone classique décomposé en 2 triangles pleins.
// (l'ancien bitmap "charging" ne montrait pas d'éclair lisible).
static void drawChargingBolt(int16_t bx, int16_t by, int16_t bw, int16_t bh) {
    const int16_t x0 = bx + (bw * 62) / 100, y0 = by;
    const int16_t x1 = bx + (bw * 10) / 100, y1 = by + (bh * 55) / 100;
    const int16_t x2 = bx + (bw * 48) / 100, y2 = by + (bh * 55) / 100;
    const int16_t x3 = bx + (bw * 38) / 100, y3 = by + bh;
    const int16_t x4 = bx + (bw * 92) / 100, y4 = by + (bh * 45) / 100;
    const int16_t x5 = bx + (bw * 54) / 100, y5 = by + (bh * 45) / 100;
    display.fillTriangle(x0, y0, x1, y1, x2, y2, GxEPD_BLACK);
    display.fillTriangle(x3, y3, x4, y4, x5, y5, GxEPD_BLACK);
}



// =============================================================================
// ECRAN DEDOCK (boîtier absent de sa base)
// =============================================================================











// =============================================================================
// ÉCRANS DE VEILLE
// =============================================================================
// 0/4 capteurs = le boîtier n'est plus sur sa base → l'écran affiché est celui du
// MODE NOMADE, défini plus bas avec les autres écrans de la charte (c'est lui qui
// a les composants et le pied : `— LONG OPTIONS` seul).
//
// TODO(dedock) : distinguer un vrai dédock (ligne DOUT basse, grâce à la
// résistance de tirage à poser) d'une panne capteur (0/4 alors que le boîtier
// est posé sur sa base). Tant que la résistance n'est pas en place, « 0
// capteurs » veut dire « hors base » : c'est ce qui est fait ici, et
// `enterErrorState()` ne demande plus la confirmation de dédock.
//

// =============================================================================
// CHARTE UI (docs/UI) : en-tête commun, aide bouton, texte UTF-8
// =============================================================================
// Les écrans du dashboard reprennent la charte des maquettes docs/UI :
// en-tête 3 colonnes (heure | date | batterie 3 segments), titre + état,
// valeur principale, jauge, aide bouton. Les polices sont indexées par byte
// (0x20-0xFF) et ce core Adafruit_GFX n'a pas enableUTF8Print : on décode
// l'UTF-8 nous-mêmes pour rendre les accents.

// Décode un codepoint UTF-8 et avance le pointeur.
// LE SIGNE EURO : les polices de la charte couvrent Latin-1 (0x20-0xFF) et leur
// glyphset reste DENSE (une entrée par codepoint). U+20AC est donc logé dans le
// slot juste au-dessus, 0x100, et replié ici sur ce slot — sans quoi il faudrait
// 8237 entrées de glyphe (49 ko de flash) pour un seul caractère.
static const uint16_t kEuroSlot = 0x100;

static uint16_t utf8Next(const char*& s) {
    uint8_t c = (uint8_t)*s++;
    if (c < 0x80) return c;
    uint16_t cp;
    uint8_t n;
    if ((c & 0xE0) == 0xC0)      { cp = c & 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 3; }
    else return c;
    while (n-- && ((uint8_t)*s & 0xC0) == 0x80) cp = (cp << 6) | ((uint8_t)*s++ & 0x3F);
    if (cp == 0x20AC) cp = kEuroSlot;   // € -> slot 0x100 des polices
    return cp;
}

static int16_t textWidthUtf8(const GFXfont* f, const char* s) {
    int16_t w = 0;
    while (*s) {
        uint16_t cp = utf8Next(s);
        if (cp >= f->first && cp <= f->last) w += f->glyph[cp - f->first].xAdvance;
    }
    return w;
}

// Dessine `s` à partir de x, ligne de base y, avec la police `f`.
static void drawUtf8(const GFXfont* f, int16_t x, int16_t y, const char* s) {
    display.setFont(f);
    display.setTextColor(GxEPD_BLACK);
    while (*s) {
        uint16_t cp = utf8Next(s);
        if (cp >= f->first && cp <= f->last) {
            display.drawChar(x, y, (uint8_t)cp, GxEPD_BLACK, GxEPD_WHITE, 1);
            x += f->glyph[cp - f->first].xAdvance;
        }
    }
}

static void drawUtf8Centered(const GFXfont* f, int16_t cx, int16_t y, const char* s) {
    drawUtf8(f, cx - textWidthUtf8(f, s) / 2, y, s);
}
static void drawUtf8Right(const GFXfont* f, int16_t xr, int16_t y, const char* s) {
    drawUtf8(f, xr - textWidthUtf8(f, s), y, s);
}

// Variante inversée (texte blanc sur fond noir), pour les bandeaux d'alerte.
static void drawUtf8Inv(const GFXfont* f, int16_t x, int16_t y, const char* s) {
    display.setFont(f);
    display.setTextColor(GxEPD_WHITE);
    while (*s) {
        uint16_t cp = utf8Next(s);
        if (cp >= f->first && cp <= f->last) {
            display.drawChar(x, y, (uint8_t)cp, GxEPD_WHITE, GxEPD_BLACK, 1);
            x += f->glyph[cp - f->first].xAdvance;
        }
    }
}
static void drawUtf8CenteredInv(const GFXfont* f, int16_t cx, int16_t y, const char* s) {
    drawUtf8Inv(f, cx - textWidthUtf8(f, s) / 2, y, s);
}

// Entrée de menu : texte rendu légèrement resserré (avance -t px) pour tenir
// dans la barre de sélection, inversé si sélectionné.
static int16_t widthMenuEntry(const GFXfont* f, const char* s, int8_t t) {
    int16_t w = 0;
    const char* p = s;
    while (*p) {
        uint16_t cp = utf8Next(p);
        if (cp >= f->first && cp <= f->last) w += f->glyph[cp - f->first].xAdvance - t;
    }
    return w;
}
static void drawMenuEntry(const GFXfont* f, int16_t cx, int16_t y, const char* s,
                          bool selected) {
    const int8_t t = 1;
    display.setFont(f);
    int16_t x = cx - widthMenuEntry(f, s, t) / 2;
    while (*s) {
        uint16_t cp = utf8Next(s);
        if (cp >= f->first && cp <= f->last) {
            display.drawChar(x, y, (uint8_t)cp,
                             selected ? GxEPD_WHITE : GxEPD_BLACK,
                             selected ? GxEPD_BLACK : GxEPD_WHITE, 1);
            x += f->glyph[cp - f->first].xAdvance - t;
        }
    }
}

// Segments pleins de l'icône batterie pour un % donné (0-3).
static uint8_t batterySegments(uint8_t percent) {
    return (percent == 0) ? 0 : (percent <= 33 ? 1 : (percent <= 66 ? 2 : 3));
}

bool displayBatteryIconChanged(uint8_t battery_percent, bool charging) {
    return batterySegments(battery_percent) != batterySegments(g_state.disp_battery) ||
           charging != g_state.disp_charging;
}

// Icône batterie 3 segments (charte) : contour + ergot + N segments remplis.
static void drawBattery3Seg(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t percent) {
    display.drawRect(x, y, w, h, GxEPD_BLACK);
    display.fillRect(x + w, y + h / 2 - 2, 2, 4, GxEPD_BLACK); // ergot
    const uint8_t seg = batterySegments(percent);
    for (uint8_t i = 0; i < seg; i++) {
        display.fillRect(x + 2 + i * 6, y + 2, 5, h - 4, GxEPD_BLACK);
    }
}

// Marqueurs de la zone d'aide : pastille (appui court), tiret (appui long).
static void drawDot(int16_t cx, int16_t cy, int16_t r) {
    display.fillCircle(cx, cy, r, GxEPD_BLACK);
}
static void drawDash(int16_t x, int16_t y, int16_t w) {
    display.fillRect(x, y, w, 1, GxEPD_BLACK);
}

// -----------------------------------------------------------------------------
// GRILLE DE L'ÉCRAN PRINCIPAL (charte docs/UI)
// -----------------------------------------------------------------------------
// Référence de gabarit : la zone secondaire (2 lignes encadrées de crochets
// hauts) est RÉSERVÉE sur tous les écrans de la charte, même vide.
static const int16_t kMarginL   = 7;
static const int16_t kMarginR   = 116;
static const int16_t kCenterX   = 61;
static const int16_t kHdrLineY  = 20;              // séparateur d'en-tête
static const int16_t kStatusY   = 29;              // ligne d'état
static const int16_t kBannerTop = 36, kBannerH = 20; // bandeau d'alerte inversé
static const int16_t kHeroY     = 152;             // ligne de base du poids (juste au-dessus de la jauge)
static const int16_t kGaugeLblY = 170;             // libellés de jauge
static const int16_t kGaugeTop  = 176, kGaugeH = 15;
static const int16_t kSecTop    = 194, kSecBot = 216;  // crochets (2 lignes)
static const int16_t kSecY      = 208;                 // message (1 ligne)
static const int16_t kSecL1Y    = 203, kSecL2Y = 214;  // message (2 lignes)
static const int16_t kFootLineY = 221, kFootY1 = 233, kFootY2 = 244;

// Zone d'EN-TÊTE rafraîchie en partial : éclair de charge + batterie, soit
// x 72 → 119 (éclair 78..87, icône 92..115), y 0 → 18 — le séparateur d'en-tête
// est à y = 20, donc hors fenêtre, et la date (7 → ~55) reste à gauche.
// x/w multiples de 8 (adressage du SSD1680). C'est la seule zone d'en-tête qui
// bouge (le % de batterie, la présence de l'USB) : elle est partagée par l'écran
// principal et l'écran nomade.
static const uint16_t kMainBatteryX = 72, kMainBatteryY = 0;
static const uint16_t kMainBatteryW = 48, kMainBatteryH = 19;
static const int16_t  kHdrBoltX = 78;   // éclair de charge : 10 × 12

// Bandeau inversé placé SOUS un titre (écrans portail et nomade, qui ont un
// titre en plus) : ce n'est donc pas la position du bandeau d'alerte (kBannerTop,
// utilisé quand il n'y a pas de titre, ex. « NIVEAU BAS »).
// Le bandeau est 2 px plus haut qu'avant la POINTE (voir kBannerPtrH) : sa
// pointe doit tenir entre le bas du bandeau et le texte du dessous (2 px blancs
// sur le nomade, où l'en-tête « DERNIER POIDS CONNU » suit immédiatement).
static const int16_t kTitleBannerTop = 66, kTitleBannerH = 18;
static const int16_t kTitleBannerTextY = 79;

// POINTE des bandeaux inversés : triangle PLEIN centré sous le bandeau, qui
// désigne le contenu placé en dessous (le poids, les infos). Demi-largeur de la
// base et profondeur ; 24 × 8 px, la plus grande taille qui laisse 2 px blancs
// avant le texte suivant sur l'écran le plus serré (MODE NOMADE).
static const int16_t kBannerPtrHalfW = 12, kBannerPtrH = 8;

// Légende du poids sur l'écran MODE NOMADE, SOUS la valeur (contre-note du
// nombre) : l'encre du poids descend jusqu'à kHeroY + 1, la légende tient donc
// 12 px plus bas, et la zone secondaire (kSecTop = 194) reste dégagée.
static const int16_t kNomadeLabelY = 172;

// Zone secondaire : message sur UNE ligne, centré dans la zone réservée et
// encadré par des crochets hauts (2 lignes de haut). Espace réservé sur tous
// les écrans de la charte.
static void drawSecondaryBox(const GFXfont* f, const char* msg) {
    const int16_t mw = textWidthUtf8(f, msg);
    const int16_t xL = kCenterX - mw / 2 - 8;
    const int16_t xR = kCenterX + mw / 2 + 8;
    display.drawLine(xL, kSecTop, xL, kSecBot, GxEPD_BLACK);        // [ barre
    display.drawLine(xL, kSecTop, xL + 4, kSecTop, GxEPD_BLACK);
    display.drawLine(xL, kSecBot, xL + 4, kSecBot, GxEPD_BLACK);
    display.drawLine(xR, kSecTop, xR, kSecBot, GxEPD_BLACK);        // ] barre
    display.drawLine(xR, kSecTop, xR - 4, kSecTop, GxEPD_BLACK);
    display.drawLine(xR, kSecBot, xR - 4, kSecBot, GxEPD_BLACK);
    drawUtf8Centered(f, kCenterX, kSecY, msg);
}

// Variante sur DEUX lignes (crochets hauts sur kSecTop..kSecBot) — l'écran
// nomade y met l'heure de mesure ET son ancienneté.
static void drawSecondaryBox2(const GFXfont* f, const char* l1, const char* l2) {
    const int16_t w1 = textWidthUtf8(f, l1);
    const int16_t w2 = textWidthUtf8(f, l2);
    const int16_t mw = (w1 > w2) ? w1 : w2;
    const int16_t xL = kCenterX - mw / 2 - 8;
    const int16_t xR = kCenterX + mw / 2 + 8;
    display.drawLine(xL, kSecTop, xL, kSecBot, GxEPD_BLACK);        // [ barre
    display.drawLine(xL, kSecTop, xL + 4, kSecTop, GxEPD_BLACK);
    display.drawLine(xL, kSecBot, xL + 4, kSecBot, GxEPD_BLACK);
    display.drawLine(xR, kSecTop, xR, kSecBot, GxEPD_BLACK);        // ] barre
    display.drawLine(xR, kSecTop, xR - 4, kSecTop, GxEPD_BLACK);
    display.drawLine(xR, kSecBot, xR - 4, kSecBot, GxEPD_BLACK);
    drawUtf8Centered(f, kCenterX, kSecL1Y, l1);
    drawUtf8Centered(f, kCenterX, kSecL2Y, l2);
}

// Variante de la zone secondaire sur DEUX lignes, alignées à GAUCHE dans un
// cadre pleine largeur (kMarginL → kMarginR) : l'écran de remplissage empile des
// « Total : … / Niveau : … » qui se lisent comme un bloc de données, pas comme un
// message centré. L'inset de 8 px laisse breathing room après le crochet.
static void drawSecondaryFrame() {
    display.drawLine(kMarginL, kSecTop, kMarginL, kSecBot, GxEPD_BLACK);
    display.drawLine(kMarginL, kSecTop, kMarginL + 4, kSecTop, GxEPD_BLACK);
    display.drawLine(kMarginL, kSecBot, kMarginL + 4, kSecBot, GxEPD_BLACK);
    display.drawLine(kMarginR, kSecTop, kMarginR, kSecBot, GxEPD_BLACK);
    display.drawLine(kMarginR, kSecTop, kMarginR - 4, kSecTop, GxEPD_BLACK);
    display.drawLine(kMarginR, kSecBot, kMarginR - 4, kSecBot, GxEPD_BLACK);
}

// Bandeau inversé (fond noir + texte blanc) PROLONGÉ par une pointe vers le bas :
// le triangle plein part du bas du bandeau et pointe vers le contenu désigné
// (le poids sur l'écran principal, le libellé « DERNIER POIDS CONNU » sur le
// nomade, les infos réseau sur le portail). Un seul helper pour les DEUX
// positions de bandeau de la charte (kBannerTop et kTitleBannerTop).
// Le texte est inversé DESSUS le bandeau : la pointe est dessinée après, elle
// n'y touche pas.
static void drawInvertedBanner(int16_t top, int16_t h, const GFXfont* f,
                               const char* text, int16_t text_y) {
    display.fillRect(kMarginL, top, kMarginR - kMarginL + 1, h, GxEPD_BLACK);
    drawUtf8CenteredInv(f, kCenterX, text_y, text);

    const int16_t bottom = top + h;
    display.fillTriangle(kCenterX - kBannerPtrHalfW, bottom,
                         kCenterX + kBannerPtrHalfW - 1, bottom,
                         kCenterX, bottom + kBannerPtrH, GxEPD_BLACK);
}

// Zone rechargeable de l'EN-TÊTE : éclair de charge + batterie 3 segments.
// Partagée par `drawHeader()` (rendu complet) et par les deux partials
// d'en-tête (écran principal, écran nomade) — c'est la SEULE zone d'en-tête qui
// bouge, donc tout ce que l'en-tête dessine à droite du x = 88 doit y tenir.
static void drawHeaderBatteryZone(uint8_t battery_percent, bool charging) {
    display.fillRect(kMainBatteryX, kMainBatteryY, kMainBatteryW, kMainBatteryH,
                     GxEPD_WHITE);
    if (charging) drawChargingBolt(kHdrBoltX, 4, 10, 12);
    drawBattery3Seg(92, 5, 22, 10, battery_percent);
}

// En-tête commun (charte) : date à gauche (JbmXb5), puis CHARGE et batterie à
// droite. L'éclair est le seul signe qui distingue « sur USB » d'une simple
// batterie descendante, et l'en-tête est commun à TOUS les écrans : il se dessine
// donc ici, et nulle part ailleurs.
// `now` explicite : l'écran nomade se redessine à l'identique au réveil, avec
// l'heure de son dernier full (voir displayShowNomadeBattery).
static void drawHeaderAt(time_t now, uint8_t battery_percent, bool charging) {
    struct tm t;
    char dbuf[12];
    localtime_r(&now, &t);
    if (t.tm_year >= 120) {
        strftime(dbuf, sizeof(dbuf), "%d/%m/%y", &t);
    } else {
        strcpy(dbuf, "--/--/--");
    }
    drawUtf8(&JbmXb5pt8b, kMarginL, 13, dbuf);
    drawHeaderBatteryZone(battery_percent, charging);
    display.drawLine(kMarginL, kHdrLineY, kMarginR, kHdrLineY, GxEPD_BLACK);
}

static void drawHeader(uint8_t battery_percent, bool charging = false) {
    drawHeaderAt(time(nullptr), battery_percent, charging);
}

// Pied commun (charte) : geste à gauche, action à droite, alignés comme sur
// tous les écrans interactifs (● COURT / — LONG).
//
// Convention : AUCUN deux-points de séparation. Le nom du bouton reste à gauche
// (● COURT / — LONG, ancré sur kMarginL), la valeur — l'action — est alignée à
// droite sur kMarginR. C'est cet alignement qui fait la colonne, pas un
// séparateur : les deux moitiés du pied sont ainsi comparables d'un écran à
// l'autre.
static void drawFooter(const char* short_right, const char* long_right) {
    display.drawLine(kMarginL, kFootLineY, kMarginR, kFootLineY, GxEPD_BLACK);
    drawDot(kMarginL + 2, 230, 2);
    drawUtf8(&JbmXb4pt8b, kMarginL + 7, kFootY1, "COURT");
    drawUtf8Right(&JbmXb4pt8b, kMarginR, kFootY1, short_right);
    drawDash(kMarginL, 243, 5);
    drawUtf8(&JbmXb4pt8b, kMarginL + 8, kFootY2, "LONG");
    drawUtf8Right(&JbmXb4pt8b, kMarginR, kFootY2, long_right);
}

// Pied réduit à UNE ligne (action « appui court » seule), pour les écrans qui
// n'ont qu'une action — ici le portail (cf. TODO displayShowConfigPortal).
static void drawFooterOne(const char* short_right) {
    display.drawLine(kMarginL, kFootLineY, kMarginR, kFootLineY, GxEPD_BLACK);
    drawDot(kMarginL + 2, 230, 2);
    drawUtf8(&JbmXb4pt8b, kMarginL + 7, kFootY1, "COURT");
    drawUtf8Right(&JbmXb4pt8b, kMarginR, kFootY1, short_right);
}

// Pied réduit à UNE ligne, action « appui LONG » seule : la ligne unique est
// celle du bas (celle du maintien), le tiret de gauche le dit. Réservé aux
// écrans dont l'unique geste est un appui long (cf. écran nomade) : un
// `● COURT` y serait un geste inexistant, donc un mensonge.
static void drawFooterLongOnly(const char* long_right) {
    display.drawLine(kMarginL, kFootLineY, kMarginR, kFootLineY, GxEPD_BLACK);
    drawDash(kMarginL, 243, 5);
    drawUtf8(&JbmXb4pt8b, kMarginL + 8, kFootY2, "LONG");
    drawUtf8Right(&JbmXb4pt8b, kMarginR, kFootY2, long_right);
}

// Capacité UTILE du silo (réglable depuis le portail → NVS) : c'est elle qui
// donne le taux de remplissage, sur tous les écrans.
static uint8_t levelPct(float weight) {
    const float cap = settingsGetSilo().capacity_kg;
    if (cap <= 0.0f) return 0;
    uint8_t pct = (uint8_t)((weight / cap) * 100.0f);
    return pct > 100 ? 100 : pct;
}

static void drawLevelGauge(uint8_t pct) {
    drawUtf8(&JbmXb4pt8b, kMarginL, kGaugeLblY, "CAPACITÉ");
    char pbuf[10];
    snprintf(pbuf, sizeof(pbuf), "%u %%", pct);
    drawUtf8Right(&JbmXb4pt8b, kMarginR, kGaugeLblY, pbuf);
    display.drawRect(kMarginL, kGaugeTop, kMarginR - kMarginL + 1, kGaugeH,
                     GxEPD_BLACK);
    const int16_t iw = (kMarginR - 1) - (kMarginL + 2) - 1;
    if (pct > 0) {
        display.fillRect(kMarginL + 2, kGaugeTop + 2,
                         (int16_t)(iw * pct / 100), kGaugeH - 5, GxEPD_BLACK);
    }
}

// --- Zones de partial refresh de l'écran principal --------------------------
// x et w multiples de 8 (contrainte d'adressage du SSD1680).
// Jauge : 162 → 192, soit sous le poids (encre jusqu'à kHeroY + 1 = 153) et
// avant la zone secondaire (kSecTop = 194).

// Menu OPTIONS : une ligne = barre de sélection (18 px) centrée sur le texte.
// 4 entrées (cf. MenuItem dans display.hpp), dont `FERMER` — le menu DOIT avoir
// une issue explicite. `FERMER` est décalé de kMenuLastDY px sous les trois
// premières lignes : ce n'est pas une 5ᵉ fonctionnalité, c'est la sortie.
static const char* const kMenuItems[MENU_COUNT] = {
    "REMPLISSAGE", "PORTAIL RÉGLAGES", "INFORMATIONS", "FERMER"
};
static const int16_t kMenuRowY0 = 96, kMenuRowDY = 24, kMenuLastDY = 8;
static const int16_t kMenuBarDY = 13, kMenuBarH = 18;   // barre de sélection
// ⚠️ Le menu a TOUJOURS les 4 lignes, même hors base : la ligne `REMPLISSAGE`
// est alors INACTIVE (fond tramé, cf. drawDisabledRow) plutôt qu'absente — sinon
// l'espacement changerait selon l'écran, et une entrée qui disparaît se lit
// comme un oubli. Trame 25 % = 2 px pleins dans une cellule de 4 px.
//
// Le curseur SAUTE par-dessus une ligne inactive : « choisir » agit donc toujours,
// et l'utilisateur voit d'où il a sauté.

// Nombre de lignes affichées (toujours 4 : voir ci-dessus).
uint8_t menuRowCount() { return MENU_COUNT; }

// Entrée portée par une ligne du menu.
MenuItem menuRowItem(uint8_t row) { return (MenuItem)row; }

// L'entrée est-elle jouable dans le mode courant ?
bool menuRowEnabled(uint8_t row) {
    // Hors base, verser est impossible : le silo n'est pas sur sa balance et
    // l'étape 1 du parcours commence par une mesure.
    return stateGetMode() != ScaleMode::DEDOCKED || menuRowItem(row) != MENU_REFILL;
}

// Première ligne JOUABLE : c'est là que le curseur se pose à l'ouverture du
// menu. Hors base ce n'est PAS `REMPLISSAGE` (inerte, donc sans barre pleine) :
// sans cela le menu s'ouvrirait sur une ligne qui n'est pas sélectionnable, et
// il faudrait deux appuis pour rejoindre la première entrée possible.
uint8_t menuFirstEnabledRow() {
    const uint8_t n = menuRowCount();
    for (uint8_t i = 0; i < n; i++) {
        if (menuRowEnabled(i)) return i;
    }
    return 0;
}

// Prochaine ligne JOUABLE après `row` (rotation du curseur).
uint8_t menuNextEnabledRow(uint8_t row) {
    const uint8_t n = menuRowCount();
    for (uint8_t i = 1; i <= n; i++) {
        const uint8_t cand = (uint8_t)((row + i) % n);
        if (menuRowEnabled(cand)) return cand;
    }
    return row;
}

// Ligne de base de la ligne i (la dernière a son décalage de séparation).
static int16_t menuRowY(uint8_t i) {
    return kMenuRowY0 + i * kMenuRowDY + (i == MENU_CLOSE ? kMenuLastDY : 0);
}
// Fenêtre de partial du menu : les lignes vont de x = 7 à 116, on prend la
// largeur alignée sur 8 la plus proche (x = 0, w = 120).
static const uint16_t kMenuZoneX = 0, kMenuZoneW = 120;

// Ligne INACTIVE : même place, même hauteur de barre, mais PAS de noir plein —
// le curseur ne s'y arrête donc jamais. Traitement en deux temps :
//
//   1. un CONTOUR 1 px : le « plein » reste le signe de la sélection, et le
//      libellé garde un fond blanc, donc parfaitement lisible (un simple tramé
//      plein rendait les majuscules illisibles — testé au rendu hors matériel) ;
//   2. un TRAMÉ 25 % à l'intérieur (2 px pleins dans une cellule de 4 px) : sur
//      une dalle 1 bit, le tramage EST le gris — c'est lui qui dit « pas
//      possible ici », sans rien enlever à la lisibilité.
static void drawDisabledRow(uint8_t row) {
    const int16_t y = menuRowY(row);
    const int16_t top = y - kMenuBarDY;
    const int16_t w = kMarginR - kMarginL + 1;
    for (int16_t yy = top + 1; yy < top + kMenuBarH - 1; yy += 4) {
        for (int16_t xx = kMarginL + 1; xx < kMarginR; xx += 4) {
            display.fillRect(xx, yy, 2, 2, GxEPD_BLACK);
        }
    }
    display.drawRect(kMarginL, top, w, kMenuBarH, GxEPD_BLACK);
    drawMenuEntry(&JbmXb6pt8b, kCenterX, y, kMenuItems[menuRowItem(row)], false);
}

// Ligne du menu (barre de sélection + entrée), partagée par le full et le
// partial de déplacement de la sélection.
static void drawMenuRow(uint8_t row, bool selected, uint8_t battery_percent) {
    const int16_t y = menuRowY(row);
    if (!menuRowEnabled(row)) {
        drawDisabledRow(row);   // jamais sélectionnable, donc pas de barre pleine
        return;
    }
    if (selected) {
        display.fillRect(kMarginL, y - kMenuBarDY, kMarginR - kMarginL + 1,
                         kMenuBarH, GxEPD_BLACK);
    }
    drawMenuEntry(&JbmXb6pt8b, kCenterX, y, kMenuItems[menuRowItem(row)], selected);
}

// Écran PRINCIPAL (dashboard, charte docs/UI) : en-tête date | batterie,
// état, valeur principale KG, jauge de niveau, zone secondaire, aide bouton.
// Ce que l'écran principal montre : de quoi le redessiner à l'identique. La RAM
// de trame de la dalle ne survit pas au deep sleep (T5 V2.4) : pour un partial au
// réveil, on réécrit d'abord l'ancienne image (s_main) dans le « previous », puis
// la nouvelle, et seuls les pixels qui diffèrent bougent.
struct MainFrame {
    float    weight;
    uint8_t  batt;
    bool     chg;
    time_t   at;           // date de l'en-tête
    time_t   measured_at;  // heure de mesure de la zone secondaire (0 = inconnue)
    uint8_t  fault;        // dernier envoi : 0 = ok, 1 = WiFi, 2 = MQTT
    uint32_t sig;          // 0 = rien dessiné
};
static RTC_DATA_ATTR uint8_t s_send_fault;
static RTC_DATA_ATTR MainFrame s_main;

// Empreinte de ce qui impose un FULL (poids, jauge, bandeau, batterie, éclair) ;
// la date et l'heure de mesure, elles, passent en partial.
static uint32_t mainSignature(const MainFrame& f) {
    const uint32_t v[] = {
        (uint32_t)lroundf(f.weight), levelPct(f.weight),
        f.weight < settingsGetSilo().low_kg, f.batt, f.chg,
    };
    uint32_t h = 2166136261u;                    // FNV-1a
    for (uint32_t x : v) h = (h ^ x) * 16777619u;
    return h ? h : 1;
}

// `to_previous` : écrit dans la RAM « previous » de la dalle (image déjà à l'écran).
static void drawMainBody(const MainFrame& f, bool to_previous) {
    const float weight = f.weight;
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        drawHeaderAt(f.at, f.batt, f.chg);

        // --- État (pastille + libellé) ---
        {
            // Dernier envoi en échec : l'état dit ce qui est déconnecté.
            const char* st = (weight < 0) ? "KO"
                           : f.fault == 1 ? "WIFI DÉCONNECTÉ"
                           : f.fault == 2 ? "MQTT DÉCONNECTÉ" : "CONNECTÉ";
            const int16_t w = textWidthUtf8(&JbmXb4pt8b, st);
            const int16_t x = kCenterX - (w + 7) / 2; // pastille (r2) + 3 px
            drawDot(x + 2, 27, 2);
            drawUtf8(&JbmXb4pt8b, x + 7, kStatusY, st);
        }

        // Capteurs KO (1-3/4) : même gabarit que « niveau bas », mais le
        // bandeau dit « CAPTEUR(S) HS » et le poids est le DERNIER poids
        // stable (RAM RTC), daté en zone secondaire — jamais une mesure
        // actuelle. Le bandeau HS prime sur « NIVEAU BAS ».
        const bool ko = (weight < 0);
        if (ko) {
            drawInvertedBanner(kBannerTop, kBannerH, &JbmXb6pt8b,
                               "CAPTEUR(S) HS", 50);
        }

        if (ko && !g_state.has_last_measure) {
            drawUtf8Centered(&OswaldBold10pt7b, kCenterX, kHeroY, "--");
            drawSecondaryBox(&JbmXb4pt8b, "AUCUNE MESURE");
        } else {
            const float shown = ko ? g_state.last_measure_kg : weight;
            const uint8_t pct = levelPct(shown);
            // Seuil « niveau bas » en KG (réglable depuis le portail → NVS).
            const bool low = !ko && (shown < settingsGetSilo().low_kg);

            // --- Bandeau d'alerte « niveau bas » (inversé, pointe vers le bas),
            //     sous l'état ---
            if (low) {
                drawInvertedBanner(kBannerTop, kBannerH, &JbmXb6pt8b,
                                   "NIVEAU BAS", 50);
            }

            // --- Valeur principale : poids + KG ---
            char wbuf[8];
            snprintf(wbuf, sizeof(wbuf), "%u", (unsigned)lroundf(shown));
            const int16_t hw = textWidthUtf8(&OswaldBold30pt7b, wbuf);
            const int16_t uw = textWidthUtf8(&OswaldBold10pt7b, "KG");
            const int16_t x = kCenterX - (hw + 3 + uw) / 2;
            drawUtf8(&OswaldBold30pt7b, x, kHeroY, wbuf);
            drawUtf8(&OswaldBold10pt7b, x + hw + 3, kHeroY, "KG");

            // --- Jauge de niveau ---
            drawLevelGauge(pct);

            // --- Zone secondaire (réservée sur tous les écrans) ---
            if (ko) {
                // Date de la mesure : l'heure si elle est du jour, sinon le
                // jour (l'écran reste affiché tant que la panne dure).
                char when[24] = "STABLE (SANS HEURE)";
                if (g_state.last_measure_at > 0) {
                    struct tm m, n;
                    const time_t now = time(nullptr);
                    localtime_r(&g_state.last_measure_at, &m);
                    localtime_r(&now, &n);
                    if (m.tm_yday == n.tm_yday && m.tm_year == n.tm_year) {
                        snprintf(when, sizeof(when), "STABLE À %02d:%02d",
                                 m.tm_hour, m.tm_min);
                    } else {
                        snprintf(when, sizeof(when), "STABLE LE %02d/%02d",
                                 m.tm_mday, m.tm_mon + 1);
                    }
                }
                drawSecondaryBox2(&JbmXb4pt8b, "DERNIÈRE MESURE", when);
            } else if (f.fault) {
                // Le dernier envoi a échoué : mieux vaut le dire que d'afficher
                // une mesure que Home Assistant n'a pas reçue.
                drawSecondaryBox(&JbmXb4pt8b, "ENVOI EN ERREUR");
            } else if (low) {
                drawSecondaryBox(&JbmXb4pt8b, "REMPLISSAGE CONSEILLE");
            } else {
                if (f.measured_at > 0) {
                    struct tm m;
                    char mbuf[20];
                    localtime_r(&f.measured_at, &m);
                    snprintf(mbuf, sizeof(mbuf), "MESURÉ À %02d:%02d", m.tm_hour, m.tm_min);
                    drawSecondaryBox(&JbmXb4pt8b, mbuf);
                } else {
                    drawSecondaryBox(&JbmXb4pt8b, "MESURE STABLE");
                }
            }
        }

        drawFooter("MESURER", "OPTIONS");
    } while (to_previous ? display.nextPageToPrevious() : display.nextPage());
}

static bool sameDay(time_t a, time_t b) {
    struct tm x, y;
    localtime_r(&a, &x);
    localtime_r(&b, &y);
    return x.tm_yday == y.tm_yday && x.tm_year == y.tm_year;
}

// Un partial de l'écran principal suppose que le dernier dessin (s_main) est un
// MAIN sans panne et que l'anti-ghosting le permet.
static bool mainCanPartial() {
    return g_state.disp_screen == DisplayScreen::MAIN && s_main.sig != 0 &&
           g_state.disp_partial_run < kFullRefreshEveryN;
}

static void mainPartial(const MainFrame& f) {
    const bool restore = !g_panel_ready;       // RAM de trame perdue (réveil)
    displayPanelWake(RefreshMode::PARTIAL, DisplayScreen::MAIN);
    display.setPartialWindow(0, 0, display.width(), display.height());
    if (restore) drawMainBody(s_main, true);
    drawMainBody(f, false);
    displayPanelOff();
    s_main = f;
}

void displayShowMain(float weight, uint8_t battery_percent, bool charging) {
    // « Niveau bas » : la zone secondaire n'affiche pas l'heure de mesure.
    const bool low = weight >= 0 && weight < settingsGetSilo().low_kg;
    MainFrame f = { weight, battery_percent, charging, time(nullptr),
                    low ? 0 : g_state.last_measure_at, s_send_fault, 0 };
    if (weight >= 0) {                          // panne : toujours un full
        f.sig = mainSignature(f);
        if (mainCanPartial() && f.sig == s_main.sig) {
            if (sameDay(f.at, s_main.at) && f.fault == s_main.fault &&
                f.measured_at / 60 == s_main.measured_at / 60) {
                Serial.println("Display: unchanged, refresh skipped");
            } else {
                Serial.println("Display: partial (date / measure time / send status)");
                mainPartial(f);
            }
            return;
        }
    }
    displayPanelWake(RefreshMode::FULL, DisplayScreen::MAIN);
    display.setFullWindow();
    drawMainBody(f, false);
    displayPanelOff();
    s_main = f;
    g_state.disp_battery  = battery_percent;
    g_state.disp_charging = charging;
}

void displaySetSendFault(uint8_t fault) { s_send_fault = fault; }

bool displayRefreshMainDate() {
    if (!mainCanPartial() || !timeIsValid()) return false;
    MainFrame f = s_main;
    f.at = time(nullptr);
    if (!sameDay(f.at, s_main.at)) {
        Serial.println("Display: date partial");
        mainPartial(f);
    } else {
        Serial.println("Display: date unchanged");
    }
    return true;
}


// =============================================================================
// ECRAN OPTIONS (menu) — charte docs/UI
// =============================================================================
// Liste verticale de 4 entrées (REMPLISSAGE, PORTAIL RÉGLAGES, INFORMATIONS,
// FERMER) ; l'entrée sélectionnée est une barre noire pleine largeur avec texte
// blanc. En-tête et pied communs à la charte.
//
// GEOMETRIE : 4 lignes à base 96, pas 24 (barres 83..173) et `FERMER` à +8 px
// (barres 163..181) pour qu'elle se lise comme la sortie du menu.
void displayShowOptions(uint8_t selected, uint8_t battery_percent,
                        bool charging) {
    displayPanelWake(RefreshMode::FULL, DisplayScreen::OPTIONS);
    display.setFullWindow();

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        drawHeader(battery_percent, charging);                      // en-tête commun (date + batterie)

        // --- Titre ---
        drawUtf8Centered(&OswaldBold10pt7b, kCenterX, 45, "OPTIONS");

        // --- Entrées ; sélection = barre noire pleine largeur, texte blanc ---
        for (uint8_t i = 0; i < menuRowCount(); i++) {
            drawMenuRow(i, i == selected, battery_percent);
        }

        drawFooter("SUIVANT", "CHOISIR");                 // pied aligné comme les autres
    } while (display.nextPage());

    displayPanelOff();
}

// Déplacement de la barre de sélection en PARTIAL zoné : c'est le geste « court »
// du futur menu (un refresh de moins d'une seconde, sans flash, au lieu d'un full
// de 1,3 s à chaque ligne). La fenêtre couvre les DEUX lignes concernées — l'ancienne
// perd sa barre, la nouvelle la gagne — et on efface avant de redessiner, sinon
// l'inversion laisse du texte fantôme. x/w multiples de 8 (SSD1680).
void displayShowOptionsSelection(uint8_t from, uint8_t to, uint8_t battery_percent,
                                 bool charging) {
    if (to >= menuRowCount() || from >= menuRowCount() ||
        !canPartial(DisplayScreen::OPTIONS)) {
        // Pas de base valide (autre écran affiché, anti-ghosting) : on redessine
        // le menu entier plutôt qu'une zone isolée.
        displayShowOptions(to, battery_percent, charging);
        return;
    }

    const uint8_t lo = (from < to) ? from : to;
    const uint8_t hi = (from < to) ? to : from;
    const int16_t top = menuRowY(lo) - kMenuBarDY - 1;
    const int16_t bot = menuRowY(hi) - kMenuBarDY + kMenuBarH;

    displayPanelWake(RefreshMode::PARTIAL, DisplayScreen::OPTIONS);
    display.setPartialWindow(kMenuZoneX, top, kMenuZoneW, bot - top + 1);
    display.firstPage();
    do {
        display.fillRect(kMenuZoneX, top, kMenuZoneW, bot - top + 1, GxEPD_WHITE);
        for (uint8_t i = lo; i <= hi; i++) {
            drawMenuRow(i, i == to, battery_percent);
        }
    } while (display.nextPage());

    displayPanelOff();
}

// =============================================================================
// ECRAN INFORMATIONS — 2 pages (état général / répartition des quatre pieds)
// =============================================================================
// Charte reprise des autres écrans : en-tête date + batterie, titre centré, zone
// de contenu en TABLEAU DEUX COLONNES (libellé flush gauche, deux-points en
// colonne fixe, valeur sur une colonne commune), zone secondaire réservée, pied
// `● COURT / — LONG`.
//
// ⚠️ Titre en JbmXb6 et non OswaldBold10 : « INFORMATIONS » fait 121 px en
// OswaldBold10 pour 122 px de dalle (il toucherait les deux bords). Un titre long
// sur cette dalle demanderait une coupe condensée d'Oswald (à générer) — d'ici là
// on reste dans les marges.

static const int16_t kInfosTitleY   = 45;   // comme le titre d'OPTIONS
// Colonnes du tableau : libellé flush gauche, deux-points en colonne fixe (juste
// après le libellé le plus long), valeur alignée à DROITE sur kMarginR — c'est
// l'alignement qui fait la colonne du tableau. Page 2 (pieds) : deux-points plus
// près, les libellés (« Pied 1 » … « Pied 4 », « Total ») étant plus courts.
static const int16_t kInfosColonX  = 58;   // page 1 (libellé le plus long : 48 px)
static const int16_t kInfosColonX2 = 46;   // page 2 (« Pied 1 » = 36 px)
static const int16_t kInfosRowDY    = 21;   // interligne du tableau (page 1)
static const int16_t kInfosRow1Y    = 66;   // 1re ligne (les deux pages)
// Page 2 : 5 lignes (les 4 pieds + le total), interligne élargi pour occuper la
// zone de contenu. Le bandeau « RÉPARTITION » a été retiré (le titre dit déjà la
// même chose) et la pagination reste en ZONE SECONDAIRE, comme sur la page 1.
static const int16_t kInfosRowDY2   = 26;

// Une ligne du tableau : `Libellé` flush gauche, « : » en colonne fixe, valeur
// alignée sur la colonne commune (c'est ce qui donne l'effet tableau).
static void drawInfoRow(const char* label, const char* value, int16_t y,
                        int16_t colon_x) {
    drawUtf8(&JbmXb5pt8b, kMarginL, y, label);
    drawUtf8(&JbmXb5pt8b, colon_x, y, ":");
    drawUtf8Right(&JbmXb5pt8b, kMarginR, y, value);
}

void displayShowInfosGeneral(uint8_t battery_percent, bool charging, bool wifi_ok,
                             bool mqtt_ok, int16_t rssi_dbm, uint8_t valid_count) {
    displayPanelWake(RefreshMode::FULL, DisplayScreen::INFORMATIONS);
    display.setFullWindow();

    // Valeurs formatées une fois pour toutes (hors boucle de pages).
    char v_batt[12], v_rssi[12], v_sensors[12];
    snprintf(v_batt, sizeof(v_batt), "%u %%", battery_percent);
    if (rssi_dbm != 0) snprintf(v_rssi, sizeof(v_rssi), "%d dBm", rssi_dbm);
    else               snprintf(v_rssi, sizeof(v_rssi), "--");
    snprintf(v_sensors, sizeof(v_sensors), "%u OK", valid_count);

    display.firstPage();
    uint8_t page = 0;
    bool has_next = true;
    do {
        display.fillScreen(GxEPD_WHITE);

        drawHeader(battery_percent, charging);

        // --- Titre (page 1 : sobre, PAS de bandeau) ---
        drawUtf8Centered(&JbmXb6pt8b, kCenterX, kInfosTitleY, "INFORMATIONS");

        // --- Tableau deux colonnes ---
        const int16_t y = kInfosRow1Y;
        drawInfoRow("Batt.",    v_batt,                    y,               kInfosColonX);
        drawInfoRow("Wi-Fi",    wifi_ok ? "OK" : "KO",     y + kInfosRowDY, kInfosColonX);
        drawInfoRow("MQTT",     mqtt_ok ? "OK" : "KO",     y + 2 * kInfosRowDY, kInfosColonX);
        drawInfoRow("Signal",   v_rssi,                    y + 3 * kInfosRowDY, kInfosColonX);
        drawInfoRow("Capteurs", v_sensors,                 y + 4 * kInfosRowDY, kInfosColonX);

        // --- Zone secondaire : pagination (l'ecran INFORMATION compte 2 pages) ---
        drawSecondaryBox(&JbmXb4pt8b, "PAGE 1 / 2");


        // --- Pied : les deux gestes. « AUTRE PAGE » sur les deux pages : le
        // court bascule 1 ↔ 2, il n'y a ni « suivante » ni « précédente ». ---
        drawFooter("AUTRE PAGE", "RETOUR");

        has_next = display.nextPage();
        page++;
    } while (has_next && page < 8);
    displayPanelOff();
    (void)charging;
}

void displayShowInfosSensors(uint8_t battery_percent, bool charging,
                             const float feet_kg[4], uint8_t valid_count) {
    displayPanelWake(RefreshMode::FULL, DisplayScreen::INFORMATIONS);
    display.setFullWindow();

    // Même numérotation que config.hpp (Z_FACTOR_1 … _4) et pins.hpp.
    static const char* const kFootLabels[4] = { "Pied 1", "Pied 2", "Pied 3", "Pied 4" };

    // Poids des quatre pieds + total (une seule décimale, virgule française,
    // formée depuis les centièmes pour ne pas dépendre du séparateur de « %f »).
    float total = 0.0f;
    char v_feet[4][16];
    for (uint8_t i = 0; i < 4; i++) {
        if (feet_kg[i] >= 0.0f) {
            const unsigned tenths = (unsigned)lroundf(feet_kg[i] * 10);
            snprintf(v_feet[i], sizeof(v_feet[i]), "%u,%u kg",
                     tenths / 10, tenths % 10);
            total += feet_kg[i];
        } else {
            snprintf(v_feet[i], sizeof(v_feet[i]), "--");
        }
    }
    char v_total[16];
    const unsigned total_tenths = (unsigned)lroundf(total * 10);
    snprintf(v_total, sizeof(v_total), "%u,%u kg",
             total_tenths / 10, total_tenths % 10);

    display.firstPage();
    uint8_t page = 0;
    bool has_next = true;
    do {
        display.fillScreen(GxEPD_WHITE);

        drawHeader(battery_percent, charging);

        // --- Titre (2e et dernière page, sans pagination : elle est en bas) ---
        drawUtf8Centered(&JbmXb6pt8b, kCenterX, kInfosTitleY, "CAPTEURS");

        // --- Tableau deux colonnes : un pied par ligne, puis le TOTAL sur sa
        //     propre ligne (une seule décimale) ---
        for (uint8_t i = 0; i < 4; i++) {
            drawInfoRow(kFootLabels[i], v_feet[i], kInfosRow1Y + i * kInfosRowDY2,
                        kInfosColonX2);
        }
        drawInfoRow("Total", v_total, kInfosRow1Y + 4 * kInfosRowDY2, kInfosColonX2);

        // --- Zone secondaire : pagination, comme sur la page 1 ---
        drawSecondaryBox(&JbmXb4pt8b, "PAGE 2 / 2");

        // --- Pied : les DEUX gestes, identiques à la page 1 ---
        drawFooter("AUTRE PAGE", "RETOUR");

        has_next = display.nextPage();
        page++;
    } while (has_next && page < 8);
    displayPanelOff();
    (void)charging;
    (void)valid_count;
}

// =============================================================================
// ECRAN CONFIRMATION DE DÉMARRAGE DU PORTAIL (maintien = lancement)
// =============================================================================
// Le demande est volontairement explicite : une question centrée, le mot
// « MAINTENIR », et une **barre de maintien** dans la zone secondaire qui se
// remplit pendant l'appui. Le pied oppose les deux sorties : `● COURT QUITTER`
// et `— LONG LANCER` — un geste court annule, il faut TENIR pour lancer.

// Barre de maintien : kHoldBarCells cases entre crochets, centrée dans la zone
// secondaire (nb de cases partagé avec l'app — voir display.hpp).
static const int16_t kHoldCellW    = 7, kHoldCellDX = 8;   // 7 px + 1 px d'air
static const int16_t kHoldX0       = 22;                   // 1re case
static const int16_t kHoldBarY     = 200, kHoldBarH = 12;
static const int16_t kHoldBracketX0 = 16, kHoldBracketX1 = 107;

// Zone refreshable seule (la barre) : x/w multiples de 8 (SSD1680).
static const uint16_t kHoldZoneX = 8, kHoldZoneY = 194;
static const uint16_t kHoldZoneW = 104, kHoldZoneH = 22;

static void drawHoldBar(uint8_t filled_cells) {
    if (filled_cells > kHoldBarCells) filled_cells = kHoldBarCells;

    // Crochets hauts, comme la zone secondaire des autres écrans.
    const int16_t top = kHoldBarY - 2, bot = kHoldBarY + kHoldBarH + 1;
    display.drawLine(kHoldBracketX0, top, kHoldBracketX0, bot, GxEPD_BLACK);
    display.drawLine(kHoldBracketX0, top, kHoldBracketX0 + 4, top, GxEPD_BLACK);
    display.drawLine(kHoldBracketX0, bot, kHoldBracketX0 + 4, bot, GxEPD_BLACK);
    display.drawLine(kHoldBracketX1, top, kHoldBracketX1, bot, GxEPD_BLACK);
    display.drawLine(kHoldBracketX1, top, kHoldBracketX1 - 4, top, GxEPD_BLACK);
    display.drawLine(kHoldBracketX1, bot, kHoldBracketX1 - 4, bot, GxEPD_BLACK);

    for (int16_t i = 0; i < kHoldBarCells; i++) {
        const int16_t x = kHoldX0 + i * kHoldCellDX;
        if (i < filled_cells) {
            display.fillRect(x, kHoldBarY, kHoldCellW, kHoldBarH, GxEPD_BLACK);
        } else {
            // Case vide : contour 1 px (sinon elle disparaît sur la dalle).
            display.drawRect(x, kHoldBarY, kHoldCellW, kHoldBarH, GxEPD_BLACK);
        }
    }
}

void displayShowPortalConfirm(uint8_t battery_percent, bool charging) {
    displayPanelWake(RefreshMode::FULL, DisplayScreen::PORTAL_CONFIRM);
    display.setFullWindow();

    display.firstPage();
    uint8_t page = 0;
    bool has_next = true;
    do {
        display.fillScreen(GxEPD_WHITE);

        drawHeader(battery_percent, charging);

        // --- Titre + bandeau entonnoir ---
        drawUtf8Centered(&JbmXb6pt8b, kCenterX, kInfosTitleY, "PORTAIL WIFI");
        // Bandeau en JbmXb5 : « RÉGLAGES LOCAUX » fait 105 px en JbmXb6 pour
        // 110 px utiles — il toucherait les bords du bandeau.
        drawInvertedBanner(kTitleBannerTop, kTitleBannerH, &JbmXb5pt8b,
                           "RÉGLAGES LOCAUX", kTitleBannerTextY);

        // --- Question centrée sur 4 lignes, puis l'instruction ---
        drawUtf8Centered(&JbmXb6pt8b, kCenterX, 106, "Démarrer le");
        drawUtf8Centered(&JbmXb6pt8b, kCenterX, 126, "portail Wi-Fi");
        drawUtf8Centered(&JbmXb6pt8b, kCenterX, 146, "de réglage");
        drawUtf8Centered(&JbmXb6pt8b, kCenterX, 166, "local ?");
        drawUtf8Centered(&JbmXb5pt8b, kCenterX, 185, "MAINTENIR");

        // --- Zone secondaire : la barre de maintien, vide au départ ---
        drawHoldBar(0);

        // --- Les deux sorties : court = quitter, long = lancer ---
        drawFooter("QUITTER", "LANCER");

        has_next = display.nextPage();
        page++;
    } while (has_next && page < 8);
    displayPanelOff();
    g_state.disp_battery  = battery_percent;
    g_state.disp_charging = charging;
}

// La barre se remplit pendant l'appui : une case toutes les 10 %, en partial
// zoné (~856 ms sans flash) — c'est le retour visuel qui rend le maintien lisible.
// PARTAGÉ par les deux écrans qui ont une tenue (portail + ECRAN14) : même
// dessin, même fenêtre, seul l'écran de repli change. `canPartial` faux =>
// l'appelant a déjà refait son écran complet (donc plus de base valide).
static void drawHoldProgress(DisplayScreen screen, uint8_t percent) {
    if (!canPartial(screen)) return;

    displayPanelWake(RefreshMode::PARTIAL, screen);
    display.setPartialWindow(kHoldZoneX, kHoldZoneY, kHoldZoneW, kHoldZoneH);
    display.firstPage();
    do {
        display.fillRect(kHoldZoneX, kHoldZoneY, kHoldZoneW, kHoldZoneH, GxEPD_WHITE);
        drawHoldBar((uint8_t)((percent * kHoldBarCells + 50) / 100));
    } while (display.nextPage());

    displayPanelOff();
}

// Rebranché sur le helper commun : le repli consiste à refaire l'écran complet,
// ce qui ré-arme `canPartial` pour le partial qui suit.
void displayShowPortalHoldProgress(uint8_t percent) {
    if (!canPartial(DisplayScreen::PORTAL_CONFIRM)) {
        // Pas de base valide : on repart de l'écran complet (barre à l'étape).
        displayShowPortalConfirm(g_state.disp_battery, g_state.disp_charging);
    }
    drawHoldProgress(DisplayScreen::PORTAL_CONFIRM, percent);
}

// --- PARCOURS DE REMPLISSAGE (3 écrans) --------------------------------------
// Les 3 étapes partagent le MÊME squelette, construit par drawRefillStepBody() :
//
//   REMPLISSAGE            titre            (JbmXb6, 45)
//   [bandau + pointe]      objet de l'étape (66 → 84, pointe 84 → 92)
//   185 KG                 héros            (OswaldBold30 + OswaldBold10, 152)
//   contexte               1 ou 2 lignes    (JbmXb5, 176 / 190)
//   [barre + n/3]          avancement       (zone secondaire 194 → 216)
//   ● …  /  — …            les deux gestes
//
// Ni sous-titre d'étape, ni étiquette au-dessus du héros : la barre
// d'avancement en zone secondaire est le SEUL indicateur d'étape, et elle est
// identique sur les 3 écrans (mêmes 10 cases, même position, même compteur
// `n/3`) — seul le nombre de cases pleines change. Le contexte (sous le héros)
// porte les données propres à l'étape.
//
// Le signe « € » (U+20AC) existe dans les polices de la charte : il a été
// injecté au slot 0x100 (hors de la plage Latin-1 d'origine) et `utf8Next()`
// le replie sur ce slot.
static const int16_t kRefillCtx1Y = 176, kRefillCtx2Y = 190;

// Fenêtre de partial des 3 étapes : tout ce qui bouge — étiquette, héros,
// contexte, barre — 94 → 215, soit sous la pointe du bandeau (92) et avant le
// séparateur de pied (221). x/w multiples de 8 (adressage du SSD1680).
static const uint16_t kRefillLiveX = 0, kRefillLiveY = 94;
static const uint16_t kRefillLiveW = 120, kRefillLiveH = 122;

static const uint8_t kRefillSteps = 3;

// Titre commun aux 3 étapes.
static void drawRefillTitle() {
    drawUtf8Centered(&JbmXb6pt8b, kCenterX, kInfosTitleY, "REMPLISSAGE");
}

// Barre d'avancement dans la ZONE SECONDAIRE : 10 cases de 5 px (pas de 7 px)
// à gauche, compteur `n/3` aligné à droite dans le même cadre. Cases pleines
// = (step * 10) / 3, soit 3 / 6 / 10 — la dernière étape remplit la barre, ce
// qui se lit comme « terminé ».
static const int16_t kProgX = 16, kProgCellW = 5, kProgCellDX = 7;
static const int16_t kProgY = 200, kProgH = 12, kProgCells = 10;

static void drawProgressBar(uint8_t step) {
    drawSecondaryFrame();
    const uint8_t filled = (uint8_t)((step * kProgCells) / kRefillSteps);
    for (int16_t i = 0; i < kProgCells; i++) {
        const int16_t x = kProgX + i * kProgCellDX;
        if (i < filled) display.fillRect(x, kProgY, kProgCellW, kProgH, GxEPD_BLACK);
        else           display.drawRect(x, kProgY, kProgCellW, kProgH, GxEPD_BLACK);
    }
    char txt[8];
    snprintf(txt, sizeof(txt), "%u/%u", step, kRefillSteps);
    drawUtf8Right(&JbmXb5pt8b, kMarginR - 9, kProgY + 9, txt);
}

// Héros (poids + unité) et contexte sous lui : le bloc de données commun aux
// 3 étapes ET aux 2 écrans de fin de parcours. Coordonnées absolues : le partial
// se contente de rogner à sa fenêtre, donc le full et le partial sont forcément
// le même dessin.
static void drawRefillHero(const char* value, const char* unit,
                           const char* ctx1, const char* ctx2) {
    const int16_t hw = textWidthUtf8(&OswaldBold30pt7b, value);
    const int16_t uw = unit ? textWidthUtf8(&OswaldBold10pt7b, unit) : 0;
    const int16_t x = kCenterX - (hw + (unit ? 3 + uw : 0)) / 2;
    drawUtf8(&OswaldBold30pt7b, x, kHeroY, value);
    if (unit) drawUtf8(&OswaldBold10pt7b, x + hw + 3, kHeroY, unit);

    drawUtf8Centered(&JbmXb5pt8b, kCenterX, kRefillCtx1Y, ctx1);
    if (ctx2) drawUtf8Centered(&JbmXb5pt8b, kCenterX, kRefillCtx2Y, ctx2);
}

// Corps d'une étape : héros + contexte + barre d'avancement. La barre est le
// SEUL indicateur d'étape (ni sous-titre, ni étiquette d'étape).
static void drawRefillStepBody(const char* value, const char* unit,
                               const char* ctx1, const char* ctx2,
                               uint8_t step) {
    drawRefillHero(value, unit, ctx1, ctx2);
    drawProgressBar(step);
}

// « 12,34 » : virgule française formée depuis les centièmes, pour ne pas
// dépendre du séparateur décimal de « %f ».
static void formatDec2(char* buf, size_t n, float v) {
    const unsigned c = (unsigned)lroundf(v * 100.0f);
    snprintf(buf, n, "%u,%02u", c / 100, c % 100);
}

// --- ÉTAPE 1/3 : EN COURS (poids ajouté en direct) ---------------------------
// Zone de partial du suivi « en cours » : tout ce qui bouge quand le silo se
// remplit — 94 → 215, soit sous la pointe du bandeau (92) et avant le
// séparateur de pied (221).
void displayShowRefillLive(uint8_t battery_percent, bool charging,
                           float added_kg, float total_kg, uint8_t level_pct) {
    displayPanelWake(RefreshMode::FULL, DisplayScreen::REFILL_LIVE);
    display.setFullWindow();
    display.firstPage();
    uint8_t page = 0;
    bool has_next = true;
    do {
        display.fillScreen(GxEPD_WHITE);
        drawHeader(battery_percent, charging);
        drawRefillTitle();
        drawInvertedBanner(kTitleBannerTop, kTitleBannerH, &JbmXb6pt8b,
                           "EN COURS", kTitleBannerTextY);

        char v[8];
        snprintf(v, sizeof(v), "%u", (unsigned)lroundf(added_kg));
        char c1[20], c2[16];
        snprintf(c1, sizeof(c1), "Total : %u kg", (unsigned)lroundf(total_kg));
        snprintf(c2, sizeof(c2), "Niveau : %u %%", level_pct);
        drawRefillStepBody(v, "KG", c1, c2, 1);

        drawFooterOne("SUIVANT");

        has_next = display.nextPage();
        page++;
    } while (has_next && page < 8);
    displayPanelOff();
    g_state.disp_battery  = battery_percent;
    g_state.disp_charging = charging;
}

// Le poids revient en vingtaines de kilos pendant le versement : un full par
// lecture (~1,3 s) serait inutilisable, donc seul le bloc qui change est
// rafraîchi, en partial zoné.
void displayShowRefillLiveUpdate(float added_kg, float total_kg,
                                 uint8_t level_pct) {
    if (!canPartial(DisplayScreen::REFILL_LIVE)) return;
    char v[8];
    snprintf(v, sizeof(v), "%u", (unsigned)lroundf(added_kg));
    char c1[20], c2[16];
    snprintf(c1, sizeof(c1), "Total : %u kg", (unsigned)lroundf(total_kg));
    snprintf(c2, sizeof(c2), "Niveau : %u %%", level_pct);

    displayPanelWake(RefreshMode::PARTIAL, DisplayScreen::REFILL_LIVE);
    display.setPartialWindow(kRefillLiveX, kRefillLiveY, kRefillLiveW,
                             kRefillLiveH);
    display.firstPage();
    do {
        display.fillRect(kRefillLiveX, kRefillLiveY, kRefillLiveW, kRefillLiveH,
                         GxEPD_WHITE);
        drawRefillStepBody(v, "KG", c1, c2, 1);
    } while (display.nextPage());
    displayPanelOff();
}

// --- ÉTAPE 2/3 : NOMBRE DE SACS ---------------------------------------------
void displayShowRefillBags(uint8_t battery_percent, bool charging,
                           uint8_t bag_count) {
    displayPanelWake(RefreshMode::FULL, DisplayScreen::REFILL_BAGS);
    display.setFullWindow();

    display.firstPage();
    uint8_t page = 0;
    bool has_next = true;
    do {
        display.fillScreen(GxEPD_WHITE);
        drawHeader(battery_percent, charging);
        drawRefillTitle();
        drawInvertedBanner(kTitleBannerTop, kTitleBannerH, &JbmXb6pt8b,
                           "NOMBRE DE SACS", kTitleBannerTextY);

        char n[8], theory[20];
        snprintf(n, sizeof(n), "%u", bag_count);
        snprintf(theory, sizeof(theory), "Théorie : %u kg",
                 (unsigned)lroundf(bag_count * settingsGetSilo().bag_kg));
        drawRefillStepBody(n, NULL, theory, NULL, 2);

        drawFooter("+ 1 SAC", "VALIDER");

        has_next = display.nextPage();
        page++;
    } while (has_next && page < 8);
    displayPanelOff();
    g_state.disp_battery  = battery_percent;
    g_state.disp_charging = charging;
}

void displayShowRefillBagsUpdate(uint8_t bag_count) {
    if (!canPartial(DisplayScreen::REFILL_BAGS)) return;
    char n[8], theory[20];
    snprintf(n, sizeof(n), "%u", bag_count);
    snprintf(theory, sizeof(theory), "Théorie : %u kg",
             (unsigned)lroundf(bag_count * settingsGetSilo().bag_kg));

    displayPanelWake(RefreshMode::PARTIAL, DisplayScreen::REFILL_BAGS);
    display.setPartialWindow(kRefillLiveX, kRefillLiveY, kRefillLiveW,
                             kRefillLiveH);
    display.firstPage();
    do {
        display.fillRect(kRefillLiveX, kRefillLiveY, kRefillLiveW, kRefillLiveH,
                         GxEPD_WHITE);
        drawRefillStepBody(n, NULL, theory, NULL, 2);
    } while (display.nextPage());
    displayPanelOff();
}

// --- ÉTAPE 3/3 : PRIX PAR SAC (saisie par digits) --------------------------
// Saisie reprise de l'ancien écran de prix : le prix est composé digit par
// digit (`d[0]` unités, `d[1]` dixièmes, `d[2]` centièmes) et une CASE entoure
// le digit en cours de saisie.
//
// On ne peut pas encadrer les TROIS digits à cette taille : le héros fait
// 3 × 34 px + la virgule, et un cadre de 2 px autour de chaque digit
// déborderait la dalle (122 px). D'où la case unique autour du digit édité,
// comme sur l'ancien écran.
static void drawPriceHero(const uint8_t digits[3], uint8_t current_idx) {
    char buf[2] = {0, 0};
    // TkHeroY = 152, OswaldBold30 : encre de y-47 à y+1.
    const int16_t ink_top = kHeroY - 47, ink_h = 49;

    // Largeur totale = somme des avances + virgule, pour centrer l'ensemble.
    int16_t w[4];
    buf[0] = '0' + digits[0];           w[0] = textWidthUtf8(&OswaldBold30pt7b, buf);
    w[1] = textWidthUtf8(&OswaldBold30pt7b, ",");
    buf[0] = '0' + digits[1];           w[2] = textWidthUtf8(&OswaldBold30pt7b, buf);
    buf[0] = '0' + digits[2];           w[3] = textWidthUtf8(&OswaldBold30pt7b, buf);
    const int16_t total = w[0] + w[1] + w[2] + w[3];

    int16_t x = kCenterX - total / 2;
    const int16_t xs[3] = { (int16_t)x, (int16_t)(x + w[0] + w[1]),
                           (int16_t)(x + w[0] + w[1] + w[2]) };
    const int16_t digit_w[3] = { w[0], w[2], w[3] };

    // La case D'ABORD : fond blanc (elle efface le partial précédent), cadre
    // 1 px, 2 px d'air autour de l'encre. Puis les chiffres par-dessus — sinon
    // le blanc mangerait le digit étant édité.
    const int16_t bx = xs[current_idx] - 2, bw = digit_w[current_idx] + 4;
    display.fillRect(bx, ink_top - 2, bw, ink_h + 4, GxEPD_WHITE);
    display.drawRect(bx, ink_top - 2, bw, ink_h + 4, GxEPD_BLACK);

    // Chiffres puis virgule, dans l'ordre x,xx.
    buf[0] = '0' + digits[0];
    drawUtf8(&OswaldBold30pt7b, xs[0], kHeroY, buf);
    drawUtf8(&OswaldBold30pt7b, xs[0] + w[0], kHeroY, ",");
    buf[0] = '0' + digits[1];
    drawUtf8(&OswaldBold30pt7b, xs[1], kHeroY, buf);
    buf[0] = '0' + digits[2];
    drawUtf8(&OswaldBold30pt7b, xs[2], kHeroY, buf);
}

static void refillPriceBody(const uint8_t digits[3], uint8_t current_idx,
                            uint8_t bag_count) {
    const float price = digits[0] + digits[1] / 10.0f + digits[2] / 100.0f;
    const unsigned cents = (unsigned)lroundf(bag_count * price * 100.0f);
    char line[24];
    char eur[12];
    formatDec2(eur, sizeof(eur), (float)cents / 100.0f);
    snprintf(line, sizeof(line), "Total : %s €", eur);
    // Le héros de cette étape est dessiné par drawPriceHero() (cases de saisie) :
    // le corps commun ne reçoit donc pas de valeur, seulement le contexte.
    drawRefillStepBody("", NULL, line, NULL, 3);
    drawPriceHero(digits, current_idx);
}

void displayShowRefillPrice(uint8_t battery_percent, bool charging,
                            const uint8_t digits[3], uint8_t current_idx,
                            uint8_t bag_count) {
    displayPanelWake(RefreshMode::FULL, DisplayScreen::REFILL_PRICE);
    display.setFullWindow();

    display.firstPage();
    uint8_t page = 0;
    bool has_next = true;
    do {
        display.fillScreen(GxEPD_WHITE);
        drawHeader(battery_percent, charging);
        drawRefillTitle();
        drawInvertedBanner(kTitleBannerTop, kTitleBannerH, &JbmXb6pt8b,
                           "PRIX PAR SAC", kTitleBannerTextY);

        refillPriceBody(digits, current_idx, bag_count);

        drawFooter("PLUS", current_idx >= 2 ? "VALIDER" : "SUIVANT");

        has_next = display.nextPage();
        page++;
    } while (has_next && page < 8);
    displayPanelOff();
    g_state.disp_battery  = battery_percent;
    g_state.disp_charging = charging;
}

void displayShowRefillPriceUpdate(const uint8_t digits[3], uint8_t current_idx,
                                  uint8_t bag_count) {
    if (!canPartial(DisplayScreen::REFILL_PRICE)) return;

    displayPanelWake(RefreshMode::PARTIAL, DisplayScreen::REFILL_PRICE);
    display.setPartialWindow(kRefillLiveX, kRefillLiveY, kRefillLiveW,
                             kRefillLiveH);
    display.firstPage();
    do {
        display.fillRect(kRefillLiveX, kRefillLiveY, kRefillLiveW, kRefillLiveH,
                         GxEPD_WHITE);
        refillPriceBody(digits, current_idx, bag_count);
    } while (display.nextPage());

    displayPanelOff();
}

// =============================================================================
// ECRAN « TERMINER ? » (ECRAN14) — le MAINTIEN enregistre le remplissage
// =============================================================================
// Dernier écran AVANT l'enregistrement : il met en regard ce qui va être sauvé
// (poids RÉEL ajouté, nombre de sacs, coût). Même principe que la confirmation
// du portail : une déclaration ne doit pas se perdre sur un appui appuyé par
// erreur, donc il faut TENIR — la barre de maintien se remplit sous le doigt.
//
// Deux gestes :
//   - `— LONG SAUVER` : la TENUE de 2,5 s enregistre ;
//   - `● COURT CORRIGER` : un appui court n'enregistre RIEN et ouvre
//     « CORRIGER ? » — revenir au nombre de sacs, au prix, annuler (refill
//     sauvage) ou revenir ici. Une tenue interrompue vide la barre, c'est tout.
//
// L'étiquette « Ajout réel » lève l'ambiguïté du héros : 185 kg est ce qu'on a
// versé, pas le poids du silo (qui est en zone secondaire sur ECRAN15).
//
// Placement au pixel près : la pointe du bandeau finit à 92, l'encre du héros
// (OswaldBold30) commence à 105, il reste 13 px. En JbmXb5 le jambage du « j »
// (2 px sous la ligne de base) TOUCHAIT le héros : d'où JbmXb4 à y = 101, soit
// une encre 95..103 — 2 px sous la pointe, 1 px au-dessus du héros.
static const int16_t kRefillLabelY = 101;

// Étiquette + héros : commun aux deux écrans de fin de parcours.
static void drawRefillRecapHero(const char* added) {
    drawUtf8Centered(&JbmXb4pt8b, kCenterX, kRefillLabelY, "Ajout réel");
    drawRefillHero(added, "KG", NULL, NULL);
}

void displayShowRefillConfirm(uint8_t battery_percent, float added_kg,
                              uint8_t bag_count, float total_eur, bool charging) {
    displayPanelWake(RefreshMode::FULL, DisplayScreen::REFILL_CONFIRM);
    display.setFullWindow();

    display.firstPage();
    uint8_t page = 0;
    bool has_next = true;
    do {
        display.fillScreen(GxEPD_WHITE);
        drawHeader(battery_percent, charging);

        drawUtf8Centered(&JbmXb6pt8b, kCenterX, kInfosTitleY, "TERMINER ?");
        drawInvertedBanner(kTitleBannerTop, kTitleBannerH, &JbmXb6pt8b,
                           "VÉRIFIER", kTitleBannerTextY);

        char v[8], c1[24], c2[24], eur[12];
        snprintf(v, sizeof(v), "%u", (unsigned)lroundf(added_kg));
        snprintf(c1, sizeof(c1), "%u sacs × %u kg", bag_count,
             (unsigned)lroundf(settingsGetSilo().bag_kg));
        formatDec2(eur, sizeof(eur), total_eur);
        snprintf(c2, sizeof(c2), "Coût : %s €", eur);

        drawRefillRecapHero(v);
        drawUtf8Centered(&JbmXb5pt8b, kCenterX, kRefillCtx1Y, c1);
        drawUtf8Centered(&JbmXb5pt8b, kCenterX, kRefillCtx2Y, c2);

        // Zone secondaire : la barre de maintien, vide au départ.
        drawHoldBar(0);

        drawFooter("CORRIGER", "SAUVER");

        has_next = display.nextPage();
        page++;
    } while (has_next && page < 8);
    displayPanelOff();
    g_state.disp_battery  = battery_percent;
    g_state.disp_charging = charging;
}

// La barre se remplit sous le doigt : partial zoné sur la barre seule (même
// helper que le portail). Les données servent au repli « pas de base valide ».
void displayShowRefillConfirmHoldProgress(uint8_t percent, float added_kg,
                                          uint8_t bag_count, float total_eur) {
    if (!canPartial(DisplayScreen::REFILL_CONFIRM)) {
        displayShowRefillConfirm(g_state.disp_battery, added_kg, bag_count,
                                 total_eur, g_state.disp_charging);
    }
    drawHoldProgress(DisplayScreen::REFILL_CONFIRM, percent);
}

// =============================================================================
// ECRAN « CORRIGER ? » (appui court sur ECRAN14)
// =============================================================================
// Menu de la charte OPTIONS (mêmes lignes, même barre de sélection, `RETOUR`
// décalé en bas comme `FERMER`) : corriger les sacs ou le prix, ANNULER le
// parcours, ou revenir au récap. Annuler n'efface pas le versement : le cycle
// normal le publie en refill « sauvage » (sans prix, valorisé au prix moyen
// côté HA) — la zone secondaire le rappelle, c'est le seul choix sans retour.
static const char* const kFixItems[FIX_COUNT] = {
    "NOMBRE DE SACS", "PRIX DU SAC", "ANNULER", "RETOUR"
};

static void drawFixRow(uint8_t row, bool selected) {
    const int16_t y = menuRowY(row);   // FIX_BACK (3) prend le décalage de FERMER
    if (selected) {
        display.fillRect(kMarginL, y - kMenuBarDY, kMarginR - kMarginL + 1,
                         kMenuBarH, GxEPD_BLACK);
    }
    drawMenuEntry(&JbmXb6pt8b, kCenterX, y, kFixItems[row], selected);
}

void displayShowRefillFix(uint8_t selected, uint8_t battery_percent,
                          bool charging) {
    displayPanelWake(RefreshMode::FULL, DisplayScreen::REFILL_FIX);
    display.setFullWindow();

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        drawHeader(battery_percent, charging);
        drawUtf8Centered(&OswaldBold10pt7b, kCenterX, 45, "CORRIGER ?");
        for (uint8_t i = 0; i < FIX_COUNT; i++) drawFixRow(i, i == selected);
        drawSecondaryBox2(&JbmXb4pt8b, "ANNULER : ENVOI", "SAUVAGE, SANS PRIX");
        drawFooter("SUIVANT", "CHOISIR");
    } while (display.nextPage());

    displayPanelOff();
    g_state.disp_battery  = battery_percent;
    g_state.disp_charging = charging;
}

// Même partial que displayShowOptionsSelection() : les deux lignes concernées.
void displayShowRefillFixSelection(uint8_t from, uint8_t to) {
    if (to >= FIX_COUNT || from >= FIX_COUNT ||
        !canPartial(DisplayScreen::REFILL_FIX)) {
        displayShowRefillFix(to, g_state.disp_battery, g_state.disp_charging);
        return;
    }

    const uint8_t lo = (from < to) ? from : to;
    const uint8_t hi = (from < to) ? to : from;
    const int16_t top = menuRowY(lo) - kMenuBarDY - 1;
    const int16_t bot = menuRowY(hi) - kMenuBarDY + kMenuBarH;

    displayPanelWake(RefreshMode::PARTIAL, DisplayScreen::REFILL_FIX);
    display.setPartialWindow(kMenuZoneX, top, kMenuZoneW, bot - top + 1);
    display.firstPage();
    do {
        display.fillRect(kMenuZoneX, top, kMenuZoneW, bot - top + 1, GxEPD_WHITE);
        for (uint8_t i = lo; i <= hi; i++) drawFixRow(i, i == to);
    } while (display.nextPage());

    displayPanelOff();
}

// =============================================================================
// ECRAN « ENREGISTRÉ » (ECRAN15) — récap, affiché 1 minute
// =============================================================================
// Le refill est parti ; cet écran ne propose plus rien à décider, il CONFIRME
// (bandeau « REMPLISSAGE SAUVÉ »). Le récap tient en 2 lignes de zone
// secondaire : le nombre de sacs et le coût d'un côté, le prix au kilo et le
// poids total du silo de l'autre.
//
// Le pied ne propose que `● COURT PRINCIPAL` : un appui court (ou long, alias)
// ramène à l'écran principal SANS remesurer — le poids vient d'être mesuré. Le
// retour automatique (1 min) est le filet de sécurité si l'utilisateur s'en va.
void displayShowRefillSaved(uint8_t battery_percent, float added_kg,
                            uint8_t bag_count, float total_eur, float total_kg,
                            bool charging) {
    displayPanelWake(RefreshMode::FULL, DisplayScreen::REFILL_SAVED);
    display.setFullWindow();

    display.firstPage();
    uint8_t page = 0;
    bool has_next = true;
    do {
        display.fillScreen(GxEPD_WHITE);
        drawHeader(battery_percent, charging);

        drawUtf8Centered(&JbmXb6pt8b, kCenterX, kInfosTitleY, "ENREGISTRÉ");
        // Bandeau en JbmXb5 : « REMPLISSAGE SAUVÉ » fait 119 px en JbmXb6, il
        // dépasserait les 110 px du bandeau.
        drawInvertedBanner(kTitleBannerTop, kTitleBannerH, &JbmXb5pt8b,
                           "REMPLISSAGE SAUVÉ", kTitleBannerTextY);

        char v[8], eur[12], unit[12], l1[28], l2[28];
        snprintf(v, sizeof(v), "%u", (unsigned)lroundf(added_kg));
        formatDec2(eur, sizeof(eur), total_eur);
        snprintf(l1, sizeof(l1), "%u sacs · %s €", bag_count, eur);
        // Prix au kilo verses (coût / poids ajouté) : 103,50 / 185 = 0,56.
        if (added_kg > 0.5f) {
            formatDec2(unit, sizeof(unit), total_eur / added_kg);
            snprintf(l2, sizeof(l2), "%s €/kg · %u kg", unit,
                     (unsigned)lroundf(total_kg));
        } else {
            snprintf(l2, sizeof(l2), "%u kg", (unsigned)lroundf(total_kg));
        }

        drawRefillRecapHero(v);
        // JbmXb4 : en JbmXb5 les deux lignes font 102 px et les crochets de la
        // zone secondaire sortiraient des marges (2..120 au lieu de 10..112).
        drawSecondaryBox2(&JbmXb4pt8b, l1, l2);

        drawFooterOne("PRINCIPAL");

        has_next = display.nextPage();
        page++;
    } while (has_next && page < 8);
    displayPanelOff();
    g_state.disp_battery  = battery_percent;
    g_state.disp_charging = charging;
}



// =============================================================================
// ECRAN « MODE NOMADE » (boîtier retiré du dock) — charte docs/UI (ECRAN17)
// =============================================================================
// Le boîtier est hors de sa base (0/4) : AUCUNE mesure n'est possible. L'écran
// affiche donc le DERNIER poids connu (mémorisé en RAM RTC) + l'heure et
// l'ancienneté de cette mesure. Le bandeau inversé « NON CONNECTÉ » et le
// libellé « DERNIER POIDS CONNU » disent sans ambiguïté que ce n'est PAS une
// mesure actuelle.
//
// Seul le geste long est câblé (voir `appHandleExternalWake`) : il ouvre le
// menu, comme sur l'écran principal — un appui long veut toujours dire « le
// menu ». L'appui court est inerte (le redock est détecté par la ligne DOUT, ou au
// poll complet de 15 min).
// Dessine l'écran nomade tel qu'il était à l'instant `now` (la date d'en-tête
// en dépend) : le même `now` redonne la même image, pixel pour pixel.
static void drawNomadeScreen(time_t now, uint8_t battery_percent, bool charging) {
    const bool has_last = g_state.has_last_measure;

    // Date et heure ABSOLUES de la dernière mesure : l'écran n'est redessiné
    // qu'une fois par jour hors charge, une ancienneté (« il y a 12 min »)
    // resterait figée et deviendrait fausse.
    char when[20] = "MESURE SANS HEURE";
    char at[12]   = "";
    if (has_last && g_state.last_measure_at > 0) {
        struct tm t;
        localtime_r(&g_state.last_measure_at, &t);
        snprintf(when, sizeof(when), "MESURÉ LE %02d/%02d", t.tm_mday, t.tm_mon + 1);
        snprintf(at, sizeof(at), "À %02d:%02d", t.tm_hour, t.tm_min);
    }

    display.fillScreen(GxEPD_WHITE);

    drawHeaderAt(now, battery_percent, charging);

    // Titre en Oswald, sur DEUX lignes (« MODE NOMADE » = 117 px > 122).
    drawUtf8Centered(&OswaldBold10pt7b, kCenterX, 38, "MODE");
    drawUtf8Centered(&OswaldBold10pt7b, kCenterX, 60, "NOMADE");

    // Bandeau inversé « NON CONNECTÉ » (« pas de mesure actuelle »), prolongé
    // d'une pointe vers le bas vers le poids ci-dessous.
    drawInvertedBanner(kTitleBannerTop, kTitleBannerH, &JbmXb6pt8b,
                       "NON CONNECTÉ", kTitleBannerTextY);

    // Valeur principale, puis SON libellé SOUS elle : « DERNIER POIDS CONNU »
    // est une légende de la valeur, pas un message d'état — sous le nombre, il
    // se lit comme sa contre-note (et la zone haute reste libre).
    if (has_last) {
        char wbuf[8];
        snprintf(wbuf, sizeof(wbuf), "%u",
                 (unsigned)lroundf(g_state.last_measure_kg));
        const int16_t hw = textWidthUtf8(&OswaldBold30pt7b, wbuf);
        const int16_t uw = textWidthUtf8(&OswaldBold10pt7b, "KG");
        const int16_t x = kCenterX - (hw + 3 + uw) / 2;
        drawUtf8(&OswaldBold30pt7b, x, kHeroY, wbuf);
        drawUtf8(&OswaldBold10pt7b, x + hw + 3, kHeroY, "KG");
        drawUtf8Centered(&JbmXb4pt8b, kCenterX, kNomadeLabelY,
                         "DERNIER POIDS CONNU");
        if (g_state.last_measure_at > 0) drawSecondaryBox2(&JbmXb4pt8b, when, at);
        else                             drawSecondaryBox(&JbmXb4pt8b, when);
    } else {
        drawUtf8Centered(&OswaldBold10pt7b, kCenterX, kHeroY, "--");
        drawSecondaryBox(&JbmXb4pt8b, "AUCUNE MESURE");
    }

    // Pied : UNE seule action. `● COURT` n'ouvre que le menu (l'appui long) :
    // rien d'autre à faire ici, et un geste court annoncé qui ne fait rien
    // serait un mensonge. Le redock, lui, se voit tout seul (ligne DOUT) ou au
    // poll complet de 15 min — le « rechercher » d'avant était redondant.
    drawFooterLongOnly("OPTIONS");
}

void displayShowNomade(uint8_t battery_percent, bool charging) {
    const time_t now = time(nullptr);

    displayPanelWake(RefreshMode::FULL, DisplayScreen::DEDOCK);
    display.setFullWindow();

    display.firstPage();
    uint8_t page = 0;
    bool has_next = true;
    do {
        drawNomadeScreen(now, battery_percent, charging);
        has_next = display.nextPage();
        page++;
    } while (has_next && page < 8);
    displayPanelOff();
    g_state.disp_battery  = battery_percent;
    g_state.disp_charging = charging;
    g_state.disp_drawn_at = now;
}

// Rafraîchit SEULEMENT la zone batterie/éclair de l'EN-TÊTE (partial, sans
// flash). En veille nomade c'est la seule chose qui bouge (le % et la présence
// de l'USB), et le poll léger de chaque minute ne redessine que ça — c'est ce
// qui permet de surveiller une recharge USB sans flasher tout l'écran.
//
// Avec la coupure écran (V2.4), la RAM de trame est perdue au sommeil, or le
// partial du SSD1680 est différentiel (0x26 = ce que montre la dalle, 0x24 = la
// cible) et rafraîchit TOUTE la dalle : il faut donc que les deux RAM
// contiennent l'écran entier, exact. On le redessine à l'identique (même `now`
// que son dernier full, même batterie affichée) dans 0x26, puis l'écran avec la
// NOUVELLE batterie dans 0x24 : seule la zone batterie diffère.
void displayShowNomadeBattery(uint8_t battery_percent, bool charging) {
    if (g_state.disp_screen != DisplayScreen::DEDOCK ||
        g_state.disp_partial_run >= kFullRefreshEveryN ||
        g_state.disp_drawn_at == 0) {
        // Pas de base valide (écran différent, cache perdu, anti-ghosting) :
        // on redessine tout plutôt que de rafraîchir une zone isolée.
        displayShowNomade(battery_percent, charging);
        return;
    }

    const bool restore = !g_panel_ready;   // RAM de trame perdue (réveil)
    displayPanelWake(RefreshMode::PARTIAL, DisplayScreen::DEDOCK);

    if (restore) {
        // 0x26 (previous) ← l'écran tel que la dalle le montre.
        display.setPartialWindow(0, 0, display.width(), display.height());
        display.firstPage();
        do {
            drawNomadeScreen(g_state.disp_drawn_at, g_state.disp_battery,
                             g_state.disp_charging);
        } while (display.nextPageToPrevious());
        // 0x24 (current) ← même écran, nouvelle batterie ; le partial ne fait
        // bouger que les pixels qui diffèrent.
        display.firstPage();
        do {
            drawNomadeScreen(g_state.disp_drawn_at, battery_percent, charging);
        } while (display.nextPage());
    } else {
        // Même session : la RAM de trame est intacte, la zone suffit.
        display.setPartialWindow(kMainBatteryX, kMainBatteryY, kMainBatteryW,
                                 kMainBatteryH);
        display.firstPage();
        do {
            drawHeaderBatteryZone(battery_percent, charging);
        } while (display.nextPage());
    }

    displayPanelOff();
    g_state.disp_battery  = battery_percent;
    g_state.disp_charging = charging;
}

// =============================================================================
// ECRAN « CARTE EN VEILLE » (carte SANS configuration)
// =============================================================================
// Fermeture du portail sans rien enregistrer (appui ou délai) : la carte n'a
// aucun cycle de mesure possible et dort (réveil timer = contrôle batterie
// seul). Sans cet écran, la dalle garderait l'écran du portail — mot de passe
// d'un AP éteint compris. Le pied annonce le seul geste utile : l'appui réveille
// la carte, qui rouvre le portail (le long aussi, non annoncé).
void displayShowUnconfiguredSleep(uint8_t battery_percent, bool charging) {
    displayPanelWake(RefreshMode::FULL, DisplayScreen::UNCONFIGURED);
    display.setFullWindow();

    display.firstPage();
    uint8_t page = 0;
    bool has_next = true;
    do {
        display.fillScreen(GxEPD_WHITE);

        drawHeader(battery_percent, charging);

        drawUtf8Centered(&OswaldBold10pt7b, kCenterX, 38, "CARTE");
        drawUtf8Centered(&OswaldBold10pt7b, kCenterX, 60, "EN VEILLE");
        drawInvertedBanner(kTitleBannerTop, kTitleBannerH, &JbmXb6pt8b,
                           "NON CONFIGURÉ", kTitleBannerTextY);

        drawUtf8Centered(&JbmXb5pt8b, kCenterX, 120, "AUCUN WI-FI");
        drawUtf8Centered(&JbmXb5pt8b, kCenterX, 136, "NI BROKER MQTT");
        drawUtf8Centered(&JbmXb5pt8b, kCenterX, 152, "ENREGISTRÉ");

        drawSecondaryBox2(&JbmXb4pt8b, "LE RÉVEIL OUVRE", "LE PORTAIL WEB");

        drawFooterOne("RÉVEIL");

        has_next = display.nextPage();
        page++;
    } while (has_next && page < 8);
    displayPanelOff();
    g_state.disp_battery  = battery_percent;
    g_state.disp_charging = charging;
}

// =============================================================================
// ECRAN DU MODE CONFIGURATION (portail web actif) — charte docs/UI (ECRAN04)
// =============================================================================
// « PORTAIL ACTIF » : titre + bandeau inversé « WIFI DE RÉGLAGE », infos
// groupées alignées à gauche (réseau AP, mot de passe WPA2, adresse), zone
// secondaire (arrêt automatique) et pied de la charte. Le mot de passe est tiré
// à chaque ouverture : il n'existe que sur cet écran, donc il faut être devant la
// balance pour rejoindre le portail.
//
// Le pied annonce « ● COURT REDEMARRER », et c'est câblé : la boucle du portail
// (`webConfigRun`) active la capture des gestes et sort sur appui. Le libellé dit
// REDEMARRER et non ARRETER parce que c'est ce qui se passe — le portail est une
// fonction terminale, il ne rend jamais la main à l'application, donc « sortir »
// ne peut pas revenir au menu (à l'inverse de l'écran nomade, où RELEVER LE
// MENU fonctionne). Un pied qui annonce une action approximative est un pied
// qui ment.
void displayShowConfigPortal(const char* ap_ssid, const char* ap_pass,
                             bool configured, uint8_t battery_percent,
                             bool charging) {
    displayPanelWake(RefreshMode::FULL, DisplayScreen::CONFIG);
    display.setFullWindow();

    display.firstPage();
    uint8_t page = 0;
    bool has_next = true;
    do {
        display.fillScreen(GxEPD_WHITE);

        drawHeader(battery_percent, charging);

        // Titre en Oswald (charte), sur DEUX lignes : « PORTAIL ACTIF » fait
        // 120 px d'un seul tenant (> 122 px de dalle, il toucherait les bords).
        drawUtf8Centered(&OswaldBold10pt7b, kCenterX, 38, "PORTAIL");
        drawUtf8Centered(&OswaldBold10pt7b, kCenterX, 60, "ACTIF");

        // Bandeau inversé « WIFI DE RÉGLAGES » (même motif que le bandeau d'alerte,
        // pointe vers le bas comprise). Texte en JbmXb5 : « WIFI DE RÉGLAGES »
        // fait 112 px en JbmXb6, soit plus large que le bandeau (110 px) — il
        // dépasserait sur les bords.
        drawInvertedBanner(kTitleBannerTop, kTitleBannerH, &JbmXb5pt8b,
                           "WIFI DE RÉGLAGES", kTitleBannerTextY);

        // Infos groupées (libellé au-dessus de la valeur), alignées à gauche.
        // Valeurs en JbmXb5 : « http://192.168.4.1 » fait 108 px en J5 mais
        // 126 px en J6 (déborderait les 122 px de la dalle) — les deux valeurs
        // restent donc à la même taille.
        // Libellés en JbmXb5 (et non JbmXb4) : à cette taille un libellé en 4 se lit
        // clair — trop faible pour une dalle 1 bit. Les valeurs restent en JbmXb5
        // aussi (l'URL ne tient pas en 6) : la hiérarchie se joue alors sur la
        // casse et les majuscules, pas sur la taille.
        // Trois groupes libellé/valeur (12 px), 16 px entre groupes, entre la
        // pointe du bandeau (92) et la zone secondaire (kSecTop = 194).
        drawUtf8(&JbmXb5pt8b, kMarginL, 106, "RÉSEAU");
        drawUtf8(&JbmXb5pt8b, kMarginL, 118, ap_ssid);
        drawUtf8(&JbmXb5pt8b, kMarginL, 134, "MOT DE PASSE");
        drawUtf8(&JbmXb5pt8b, kMarginL, 146, ap_pass);
        drawUtf8(&JbmXb5pt8b, kMarginL, 162, "ADRESSE");
        drawUtf8(&JbmXb5pt8b, kMarginL, 174, "http://192.168.4.1");

        // Zone secondaire réservée : arrêt automatique (valeur partagée avec le
        // webconfig -> l'écran affiche la durée réellement appliquée).
        char auto_stop[28];
        snprintf(auto_stop, sizeof(auto_stop), "ARRÊT AUTO : %u MIN",
                 (unsigned)PORTAL_TIMEOUT_MIN);
        drawSecondaryBox(&JbmXb4pt8b, auto_stop);

        // Pied : une seule ligne — seule action possible ici, un appui court.
        // Elle dit ce qui se passe vraiment : une carte configurée redémarre
        // (et reprend ses mesures), une carte sans configuration se met en
        // veille (le bouton rouvrira le portail).
        drawFooterOne(configured ? "REDEMARRER" : "VEILLE");

        has_next = display.nextPage();
        page++;
    } while (has_next && page < 8);
    displayPanelOff();
}
