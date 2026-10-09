#pragma once

#include "config.hpp"
#include "state.hpp" // DisplayScreen

#include <Arduino.h>

// =============================================================================
// GESTION DE L'AFFICHAGE E-PAPER
// =============================================================================

// Déclaration de l'écran (définie dans config.hpp)
extern GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)> display;

// Mode de rafraîchissement d'un draw.
enum class RefreshMode : uint8_t {
    FULL,   // redessine tout l'écran (full refresh, ~1,3 s, avec flash)
    PARTIAL // ne rafraîchit qu'une zone (partial, ~856 ms, sans flash)
};

// --- Cycle de vie de la dalle -------------------------------------------------
// La dalle est pilotée par l'appelant : un « wake » la prépare pour un draw, on
// la met hors tension après le draw (displayPanelOff), et en veille profonde
// seulement au moment du deep sleep de l'ESP32 (displayPanelDeepSleep) — c'est
// ce qui préserve la RAM de trame entre deux réveils et permet les partials.

/**
 * @brief Prépare la dalle avant un draw et mémorise l'écran affiché.
 * FULL : reset matériel appuyé (resynchronise un SSD1680 douteux).
 * PARTIAL : pas de reset appuyé (RAM de trame conservée).
 * @param mode   FULL (redessiner tout) ou PARTIAL (zone seulement)
 * @param screen écran qui va être dessiné (cache RTC, décide du mode suivant)
 */
void displayPanelWake(RefreshMode mode, DisplayScreen screen);

/**
 * @brief Coupe l'alimentation de la dalle (image et RAM conservées).
 * À appeler en fin de draw quand l'ESP32 reste éveillé.
 */
void displayPanelOff();

/**
 * @brief Met la dalle en veille profonde (conso minimale).
 * À appeler UNE seule fois, juste avant le deep sleep de l'ESP32.
 */
void displayPanelDeepSleep();

/**
 * @brief Mode « rafale » : tant qu'il est actif, displayPanelOff() ne coupe PAS
 * l'alimentation de la dalle. Le _PowerOn n'est donc payé qu'une fois par
 * rafale au lieu d'une fois par refresh (gain de temps sur les mises à jour
 * rapprochées). displaySetKeepPowered(false) fait le powerOff() de fin.
 */
void displaySetKeepPowered(bool on);

/**
 * @brief Affiche la page principale : silo-jauge ÖkoFEN à gauche (niveau de
 * granulés réel), poids en grand à droite, batterie seule en haut.
 * Toujours en full refresh (affichage de fond, contraste garanti).
 * Capteurs KO (weight < 0) : même gabarit que « niveau bas », bandeau
 * « CAPTEUR(S) HS » et DERNIER poids stable (RAM RTC) avec son heure en zone
 * secondaire — jamais une mesure actuelle.
 * @param weight Poids en kg (négatif = capteurs inaccessibles)
 * @param battery_percent Niveau de batterie (0-100)
 * @param charging true = icône batterie "en charge"
 */
void displayShowMain(float weight, uint8_t battery_percent, bool charging = false);

/**
 * @brief Réveil de minuit en dock : si l'écran principal est à l'écran et que la
 * date a changé, partial de l'en-tête seul (sans capteurs ni réseau).
 * @return false si l'écran principal ne peut pas être rafraîchi en partial
 *         (autre écran, panne, anti-ghosting) : l'appelant fait un cycle complet.
 */
bool displayRefreshMainDate();

/**
 * @brief Résultat du dernier envoi, affiché dans la zone du bas de l'écran
 * principal (« WIFI / MQTT DÉCONNECTÉ — ENVOI EN ERREUR »). Pris en compte au
 * prochain displayShowMain() ; un changement se redessine en partial.
 * @param fault 0 = envoi ok, 1 = WiFi non connecté, 2 = broker MQTT en échec
 */
void displaySetSendFault(uint8_t fault);


/**
 * @brief Affiche le menu OPTIONS (liste verticale de 4 entrées ; l'entrée
 * sélectionnée est une barre noire pleine largeur avec texte blanc).
 * @param selected index sélectionné (0 = REMPLISSAGE .. 3 = VEILLE)
 * @param battery_percent niveau de batterie (icône + « NN % »)
 */
/**
 * @brief Entrées du menu OPTIONS. L'ordre est celui de l'écran ; `FERMER` est la
 * sortie explicite du menu (sinon on n'en sortirait qu'en choisissant une
 * action). C'est l'app qui dispatche le choix (appOptionsMenu).
 *
 * Le menu fait TOUJOURS 4 lignes : hors base, `REMPLISSAGE` est INACTIVE (fond
 * tramé) et le curseur la saute — l'espacement ne change donc jamais d'un écran
 * à l'autre.
 */
enum MenuItem : uint8_t {
    MENU_REFILL = 0,  // lance le parcours de remplissage
    MENU_PORTAL,      // confirmation (maintien) puis portail de réglage
    MENU_INFOS,       // les 2 pages d'informations
    MENU_CLOSE,       // « FERMER » : écran principal puis deep sleep
    MENU_COUNT
};

/** @brief Nombre de lignes du menu (toujours 4 : l'espacement ne varie pas). */
uint8_t menuRowCount();

/** @brief Entrée portée par une ligne du menu. */
MenuItem menuRowItem(uint8_t row);

/**
 * @brief La ligne est-elle jouable dans le mode courant ? Faux uniquement pour
 * `REMPLISSAGE` hors base (le silo n'est pas sur sa balance, et l'étape 1 du
 * parcours commence par une mesure). Elle reste DESSINÉE, sur fond tramé.
 */
bool menuRowEnabled(uint8_t row);

/** @brief Prochaine ligne jouable après `row` : le curseur saute les inactives. */
uint8_t menuNextEnabledRow(uint8_t row);

/** @brief Première ligne jouable — où le curseur se pose à l'ouverture du menu. */
uint8_t menuFirstEnabledRow();

void displayShowOptions(uint8_t selected, uint8_t battery_percent,
                        bool charging = false);

/**
 * @brief Déplace la barre de sélection du menu OPTIONS en partial zoné (les
 * deux lignes concernées sont effacées puis redessinées) : le geste « court »
 * du menu coûte ~856 ms sans flash au lieu d'un full de 1,3 s.
 * Repli automatique sur displayShowOptions() si le menu n'est pas affiché.
 * @param from index actuellement sélectionné (0..3)
 * @param to   index à sélectionner (0..3)
 */
void displayShowOptionsSelection(uint8_t from, uint8_t to,
                                 uint8_t battery_percent,
                                 bool charging = false);

/**
 * @brief Écran d'informations — PAGE 1 « INFORMATIONS » (état général).
 * En-tête et titre communs, PUIS une liste en TABLEAU DEUX COLONNES : libellé
 * flush gauche (x = kMarginL), deux-points en colonne fixe, valeur alignée à
 * gauche sur une colonne commune. Pas de bandeau noir (page sobre) ; le pied
 * propose les deux gestes : `● COURT PAGE SUIV.` et `— LONG RETOUR`.
 * @param battery_percent niveau de batterie (icône 3 segments de l'en-tête)
 * @param charging        icône « en charge »
 * @param wifi_ok         Wi-Fi connecté lors du relevé
 * @param mqtt_ok         broker MQTT joignable lors du relevé
 * @param rssi_dbm        niveau Wi-Fi en dBm (valeur négative), 0 = inconnu
 * @param valid_count     capteurs valides 0-4
 */
void displayShowInfosGeneral(uint8_t battery_percent, bool charging, bool wifi_ok,
                             bool mqtt_ok, int16_t rssi_dbm, uint8_t valid_count);

/**
 * @brief Écran d'informations — PAGE 2 « CAPTEURS » (répartition des 4 pieds).
 * Même structure que la page 1 : titre, tableau à deux colonnes, pagination en
 * zone secondaire (`PAGE 2 / 2`), pied réduit à une ligne (`● COURT RETOUR`)
 * car c'est la dernière page — « page suivante » n'existe plus. Les quatre pieds
 * (`Pied 1` … `Pied 4`) occupent la zone de contenu, le **total tient sur une
 * ligne, comme ligne du tableau**.
 * @param feet_kg poids mesuré par pied (pieds 1 à 4) ; un pied non valide
 *                est passé en < 0 et affiché `--`
 * @param valid_count capteurs valides 0-4
 */
void displayShowInfosSensors(uint8_t battery_percent, bool charging,
                             const float feet_kg[4], uint8_t valid_count);

/**
 * @brief Écran de CONFIRMATION de démarrage du portail : question centrée sur
 * 4 lignes, bandeau « RÉGLAGES LOCAUX », le mot `MAINTENIR` et une **barre de
 * maintien** dans la zone secondaire. Le pied oppose les deux sorties :
 * `● COURT QUITTER` / `— LONG LANCER` — un appui court annule, il faut
 * TENIR pour lancer (le geste long est ainsi volontaire et obligatoire).
 */
/**
 * @brief Nombre de cases de la barre de MAINTIEN (portail et ECRAN14). La boucle
 * de l'app s'en sert pour n'afficher qu'un partial quand une case change.
 */
constexpr uint8_t kHoldBarCells = 10;

void displayShowPortalConfirm(uint8_t battery_percent, bool charging = false);

/**
 * @brief Avance la barre de maintien de l'écran de confirmation (10 cases) en
 * partial zoné sur la seule zone secondaire. @param percent 0-100.
 */
void displayShowPortalHoldProgress(uint8_t percent);

/**
 * @brief Affiche l'écran « MODE NOMADE » (boîtier retiré du dock, dédock
 * CONFIRMÉ : 0/4 + ligne DOUT basse). Aucune mesure possible : affiche le
 * DERNIER poids connu (RAM RTC) + l'heure et l'ancienneté de cette mesure, sous
 * un bandeau inversé « NON CONNECTÉ » — jamais une valeur « actuelle ».
 * En-tête date + batterie, pied « rechercher » (appui court) / « informations »
 * (appui long, hint non câblé).
 */
void displayShowNomade(uint8_t battery_percent, bool charging = false);

/**
 * @brief Rafraîchit SEULEMENT l'icône batterie de l'écran de veille (nomade ou
 * capteurs KO) en partial zoné, sans flash. Repli automatique sur le full si le
 * même écran n'est pas déjà affiché ou si l'anti-ghosting l'exige.
 */
void displayShowNomadeBattery(uint8_t battery_percent, bool charging = false);

/**
 * @brief true si l'icône d'en-tête affichée n'est plus à jour : nombre de
 * segments de la batterie (pas le % exact, qui ne se voit pas) ou éclair.
 */
bool displayBatteryIconChanged(uint8_t battery_percent, bool charging);

/**
 * @brief Remplissage — ÉTAPE 1/3 « EN COURS » : suivi du poids versé. Titre
 * `REMPLISSAGE`, bandeau `EN COURS`, héros (OswaldBold30 + unité) = poids ajouté,
 * contexte sur 2 lignes (`Total : … kg` / `Niveau : … %`), barre d'avancement
 * (10 cases) + compteur `1/3` en zone secondaire, pied `● COURT SUIVANT`
 * (seule action annoncée — le long est inerte).
 * @param added_kg poids ajouté depuis le début du versement
 * @param total_kg poids total du silo
 * @param level_pct niveau du silo en %
 */
void displayShowRefillLive(uint8_t battery_percent, bool charging,
                           float added_kg, float total_kg, uint8_t level_pct);

/**
 * @brief Met à jour le suivi « en cours » (héros, contexte) en partial zoné :
 * le poids revient en vingtaines de kilos pendant le versement, un full par
 * lecture serait inutilisable. Sans effet si l'écran affiché n'est pas
 * `REFILL_LIVE` (l'appelant doit repartir d'un full).
 */
void displayShowRefillLiveUpdate(float added_kg, float total_kg,
                                 uint8_t level_pct);

/**
 * @brief Remplissage — ÉTAPE 2/3 « NOMBRE DE SACS ». Même squelette (titre
 * `REMPLISSAGE`, pas de sous-titre d'étape), bandeau `NOMBRE DE SACS`, le
 * nombre de sacs en héros, contexte `Théorie : N kg` (nb × `BAG_KG`), barre
 * d'avancement + `2/3`. Pied `● COURT + 1 SAC` / `— LONG VALIDER`.
 */
void displayShowRefillBags(uint8_t battery_percent, bool charging,
                           uint8_t bag_count);

/**
 * @brief Met à jour l'étape 2 (nombre de sacs, théorie) en partial zoné. Sans
 * effet si l'écran affiché n'est pas `REFILL_BAGS`.
 */
void displayShowRefillBagsUpdate(uint8_t bag_count);

/**
 * @brief Remplissage — ÉTAPE 3/3 « PRIX PAR SAC ». Même squelette, avec une
 * saisie de prix **par digits** : une case entoure le digit en cours (les trois
 * cases ne tiennent pas à cette taille), contexte `Total : … €` (nb de sacs ×
 * prix), barre pleine + `3/3`. Pied `● COURT PLUS` = +1 sur le digit courant,
 * `— LONG SUIVANT` = digit suivant, `VALIDER` sur le dernier.
 * @param digits      3 digits {unités, dixièmes, centièmes} (6,9,0 → 6,90 €)
 * @param current_idx digit en cours de saisie (0..2)
 * @param bag_count   nombre de sacs (pour le coût total, affiché avec `€`)
 */
void displayShowRefillPrice(uint8_t battery_percent, bool charging,
                            const uint8_t digits[3], uint8_t current_idx,
                            uint8_t bag_count);

/**
 * @brief Met à jour l'étape 3 (prix saisi, coût total) en partial zoné. Sans
 * effet si l'écran affiché n'est pas `REFILL_PRICE`.
 */
void displayShowRefillPriceUpdate(const uint8_t digits[3], uint8_t current_idx,
                                  uint8_t bag_count);

/**
 * @brief Remplissage — VALIDATION (ECRAN14) « TERMINER ? » : ce qui va être
 * enregistré, à savoir le poids RÉEL ajouté (étiquette `Ajout réel` + héros
 * `185 KG`), `N sacs × 15 kg` et le coût total. Barre de MAINTIEN en zone
 * secondaire : il faut TENIR 2,5 s pour sauver ; un appui court ouvre
 * « CORRIGER ? » sans rien publier, une tenue interrompue vide la barre. Pied
 * `● COURT CORRIGER` / `— LONG SAUVER`.
 * @param added_kg poids ajouté mesuré
 * @param bag_count nombre de sacs déclarés
 * @param total_eur coût total (nb de sacs × prix du sac)
 */
void displayShowRefillConfirm(uint8_t battery_percent, float added_kg,
                              uint8_t bag_count, float total_eur,
                              bool charging = false);

/**
 * @brief La barre de maintien se remplit sous le doigt (partial zoné sur la
 * barre, même helper que la confirmation du portail). Repli automatique sur
 * displayShowRefillConfirm() s'il n'y a pas de base valide à rafraîchir.
 */
void displayShowRefillConfirmHoldProgress(uint8_t percent, float added_kg,
                                          uint8_t bag_count, float total_eur);

/**
 * @brief Choix de l'écran « CORRIGER ? » (appui court sur ECRAN14). Même
 * géométrie que le menu OPTIONS ; `RETOUR` est la sortie, décalée en bas.
 */
enum RefillFixItem : uint8_t {
    FIX_BAGS = 0,  // retour au nombre de sacs (étape 2), puis re-mesure → E14
    FIX_PRICE,     // retour au prix du sac (étape 3), puis re-mesure → E14
    FIX_CANCEL,    // abandon : le cycle normal publie un refill « sauvage »
    FIX_BACK,      // retour à E14 sans rien changer
    FIX_COUNT
};

/**
 * @brief Remplissage — « CORRIGER ? » : menu de 4 lignes (`NOMBRE DE SACS`,
 * `PRIX DU SAC`, `ANNULER`, `RETOUR`), zone secondaire qui rappelle ce que fait
 * `ANNULER` (envoi sauvage, sans prix). Pied `● COURT SUIVANT` /
 * `— LONG CHOISIR`, comme OPTIONS.
 */
void displayShowRefillFix(uint8_t selected, uint8_t battery_percent,
                          bool charging = false);

/**
 * @brief Déplace la sélection de « CORRIGER ? » en partial zoné. Repli
 * automatique sur displayShowRefillFix() si l'écran n'est pas affiché.
 */
void displayShowRefillFixSelection(uint8_t from, uint8_t to);

/**
 * @brief Remplissage — « ENREGISTRÉ » (ECRAN15) : le récap de ce qui vient
 * d'être sauvegardé, affiché 1 minute. Héros `185 KG` (étiquette `Ajout
 * réel`), zone secondaire sur 2 lignes : `N sacs · 103,50 €` puis
 * `0,56 €/kg · 462 kg`. Seul `● COURT PRINCIPAL` est annoncé : on revient à
 * l'écran principal SANS remesurer (le poids vient d'être mesuré).
 * @param added_kg poids ajouté enregistré
 * @param bag_count nombre de sacs
 * @param total_eur coût total
 * @param total_kg  poids total du silo après remplissage
 */
void displayShowRefillSaved(uint8_t battery_percent, float added_kg,
                            uint8_t bag_count, float total_eur, float total_kg,
                            bool charging = false);

/**
 * @brief Affiche l'écran du MODE CONFIGURATION (portail web actif) :
 * titre « PORTAIL ACTIF », bandeau inversé « WIFI DE RÉGLAGE », infos groupées
 * (réseau AP, mot de passe WPA2, adresse de la page) et arrêt automatique.
 * @param ap_ssid Nom du réseau AP monté par la carte (ex "Scale-A1B2")
 * @param ap_pass Mot de passe WPA2 de l'AP (8 chiffres, tiré à chaque ouverture)
 * @param configured true = config enregistrée : le pied annonce « REDEMARRER »,
 *                   sinon « VEILLE » (ce que fait l'appui dans chaque cas)
 * @param battery_percent niveau de batterie (icône 3 segments de l'en-tête)
 * @param charging true = icône batterie « en charge »
 */
void displayShowConfigPortal(const char* ap_ssid, const char* ap_pass,
                             bool configured, uint8_t battery_percent,
                             bool charging = false);

/**
 * @brief Écran de VEILLE d'une carte SANS configuration (fermeture du portail
 * sans enregistrement) : titre « CARTE EN VEILLE », bandeau « NON CONFIGURÉ »,
 * rappel que le réveil (appui bouton) rouvre le portail. Pied `● COURT RÉVEIL`.
 */
void displayShowUnconfiguredSleep(uint8_t battery_percent, bool charging = false);
