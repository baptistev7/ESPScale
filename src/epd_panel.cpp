#include "epd_panel.hpp"

// =============================================================================
// TABLE DE WAVEFORM PARTIELLE
// =============================================================================
// Copiée verbatim de GxEPD2_213_BN (GxEPD2_213_BN.cpp, lut_partial) : même
// dalle (DEPG0213BN) et même contrôleur (SSD1680). Le GDEY n'en a pas.
const unsigned char EpdPanel::lut_partial[] PROGMEM =
{
  0x0, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x80, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x40, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0xA, 0x0, 0x0, 0x0, 0x0, 0x0, 0x2, 0x1, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0,
};

// NOTE (ne pas retenter) : la LUT partielle Waveshare (EPD_2in13_V3, même
// contrôleur SSD1680) est plus rapide (619 ms mesurés vs 770 ms ici) mais
// INUTILISABLE seule : elle exige le flux complet de Waveshare (reset matériel
// + 0x37 « RAM ping-pong » + 0x22=0xC0 AVANT CHAQUE partiel). Sans lui, la 2e
// mise à jour part en Busy Timeout (10 s). Idem pour la constante 0x0f/0x0c
// appliquée sans leur LUT.

EpdPanel::EpdPanel(int16_t cs, int16_t dc, int16_t rst, int16_t busy) :
  GxEPD2_213_GDEY0213B74(cs, dc, rst, busy)
{
}

// Réimplémentation de _PowerOn() du GDEY (privé là-bas) : séquence 0xe0.
void EpdPanel::_powerOnLocal()
{
  if (!_power_is_on)
  {
    _writeCommand(0x22);
    _writeData(0xe0);
    _writeCommand(0x20);
    _waitWhileBusy("_PowerOn", power_on_time);
  }
  _power_is_on = true;
}

// Réimplémentation de _setPartialRamArea() du GDEY (privé là-bas).
void EpdPanel::_setPartialRamAreaLocal(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  _writeCommand(0x11); // set ram entry mode
  _writeData(0x03);    // x increase, y increase : normal mode
  _writeCommand(0x44);
  _writeData(x / 8);
  _writeData((x + w - 1) / 8);
  _writeCommand(0x45);
  _writeData(y % 256);
  _writeData(y / 256);
  _writeData((y + h - 1) % 256);
  _writeData((y + h - 1) / 256);
  _writeCommand(0x4e);
  _writeData(x / 8);
  _writeCommand(0x4f);
  _writeData(y % 256);
  _writeData(y / 256);
}

// Charge la LUT partielle (une fois par session : init/powerOff remettent
// _using_partial_mode à false).
void EpdPanel::_initPartLocal()
{
  _writeCommand(0x32);
  _writeDataPGM(lut_partial, sizeof(lut_partial));
  _using_partial_mode = true;
}

void EpdPanel::_partialUpdate()
{
  if (!_using_partial_mode) _initPartLocal();
#if !defined(EPD_PARTIAL_POWERON) || EPD_PARTIAL_POWERON
  _powerOnLocal();
#endif
#if defined(EPD_PARTIAL_BORDER) && EPD_PARTIAL_BORDER
  _writeCommand(0x3C); // border waveform (piste 3 du doc)
  _writeData(0x80);
#endif
  _writeCommand(0x22);
  _writeData(0xcc); // sequence partielle BN
  _writeCommand(0x20);
  _waitWhileBusy("_Update_Part", partial_refresh_time);
  _power_is_on = true;
}

void EpdPanel::refresh(bool partial_update_mode)
{
  if (partial_update_mode) refresh(0, 0, WIDTH, HEIGHT);
  else GxEPD2_213_GDEY0213B74::refresh(false); // full refresh GDEY inchangé
}

void EpdPanel::refresh(int16_t x, int16_t y, int16_t w, int16_t h)
{
  if (_initial_refresh) return refresh(false); // 1er update : full obligatoire
  // intersection avec l'écran (reprise du GDEY)
  int16_t w1 = x < 0 ? w + x : w;
  int16_t h1 = y < 0 ? h + y : h;
  int16_t x1 = x < 0 ? 0 : x;
  int16_t y1 = y < 0 ? 0 : y;
  w1 = x1 + w1 < int16_t(WIDTH) ? w1 : int16_t(WIDTH) - x1;
  h1 = y1 + h1 < int16_t(HEIGHT) ? h1 : int16_t(HEIGHT) - y1;
  if ((w1 <= 0) || (h1 <= 0)) return;
  // x1, w1 multiples de 8
  w1 += x1 % 8;
  if (w1 % 8 > 0) w1 += 8 - w1 % 8;
  x1 -= x1 % 8;
  _setPartialRamAreaLocal(x1, y1, w1, h1);
  _partialUpdate();
}

// Le GDEY n'écrit que le buffer current (0x24) : le « previous » (0x26) garde
// l'image précédente, donc le différentiel SSD1680 est faux dès la 2e mise à
// jour → fantôme. On écrit les DEUX buffers, comme le 213_BN.
void EpdPanel::writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImageToPrevious(bitmap, x, y, w, h, invert, mirror_y, pgm); // 0x26 (previous)
  writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);           // 0x24 (current)
}

void EpdPanel::writeImagePartAgain(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                   int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImagePartToPrevious(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm); // 0x26
  writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);           // 0x24
}
