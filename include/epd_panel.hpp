#pragma once

#include <GxEPD2_BW.h> // tire gdey/GxEPD2_213_GDEY0213B74.h

// =============================================================================
// PANNEAU GDEY0213B74 + LUT PARTIEL DU 213_BN
// =============================================================================
// Le driver GxEPD2_213_GDEY0213B74 n'a PAS de mise à jour partielle réelle :
// pas de _Init_Part(), pas d'écriture du registre 0x32 (table de waveform).
// Son _Update_Part() se contente de 0x22 = 0xfc, ce qui exécute le waveform
// OTP PLEIN ÉCRAN → flash global à chaque « partiel ».
//
// La dalle est en réalité une DEPG0213BN, pour laquelle GxEPD2 fournit le
// driver GxEPD2_213_BN qui charge un lut_partial dédié. On garde le full
// refresh rapide du GDEY (~2,6 s, raison de son choix) et on n'emprunte au BN
// que la séquence partielle : LUT (0x32) + _PowerOn + 0x22 = 0xcc.
class EpdPanel : public GxEPD2_213_GDEY0213B74
{
  public:
    EpdPanel(int16_t cs, int16_t dc, int16_t rst, int16_t busy);

    // Surcharges virtuelles : le full refresh reste celui du GDEY, le partiel
    // passe par notre séquence (LUT BN).
    void refresh(bool partial_update_mode = false) override;
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h) override;

    // CORRECTIF GHOSTING : la mise à jour SSD1680 est DIFFÉRENTIELLE (current
    // 0x24 vs previous 0x26). Le driver GDEY n'écrit que le buffer current dans
    // ces deux méthodes → le « previous » garde l'image d'avant, le différentiel
    // est faux dès la 2e mise à jour et laisse un fantôme. Le 213_BN écrit les
    // DEUX buffers ; on fait pareil.
    void writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false) override;
    void writeImagePartAgain(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                             int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false) override;

  private:
    // Réimplémentations locales : ces méthodes sont privées dans le GDEY.
    void _powerOnLocal();
    void _setPartialRamAreaLocal(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void _partialUpdate();
    void _initPartLocal();

    static const unsigned char lut_partial[] PROGMEM;
};
