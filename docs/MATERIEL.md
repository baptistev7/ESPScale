---
title: Matériel
nav_order: 3
---

# Matériel

Ce qu'il faut, comment le câbler, et les quelques modifications à faire sur la
carte. Les broches font foi dans [`include/pins.hpp`](https://github.com/baptistev7/ESPScale/blob/main/include/pins.hpp).

## Sommaire

1. [Vue d'ensemble](#vue-densemble)
2. [Liste du matériel](#liste-du-matériel)
3. [La carte LilyGO T5 V2.4](#la-carte-lilygo-t5-v24)
4. [Câblage](#câblage)
5. [Modifications de la carte](#modifications-de-la-carte)
6. [Boîtier et base](#boîtier-et-base)
7. [Historique — T5 V2.3.1](#historique--t5-v231)

## Vue d'ensemble

```
            ┌──────────── silo à granulés (ne bouge jamais) ────────────┐
            │                                                            │
   pied 1 ──┤ 4 cellules 50 kg → HX711 ┐                                 │
   pied 2 ──┤ 4 cellules 50 kg → HX711 ├── BASE ── pogo ══ pogo ── BOÎTIER (ESP32 + écran + batterie)
   pied 3 ──┤ 4 cellules 50 kg → HX711 │                 ↑
   pied 4 ──┤ 4 cellules 50 kg → HX711 ┘                 └ « dédock » = retirer le boîtier
```

- **La base** porte les 4 pieds (cellules + HX711) et une moitié du connecteur
  **pogo**. Elle reste sous le silo.
- **Le boîtier** contient la carte, l'écran, la batterie et l'autre moitié du
  pogo. On le **retire de sa base** pour le recharger en USB ou intervenir : c'est
  le **dédock**, un geste normal et récurrent (le firmware passe alors en **mode
  nomade**, voir [FONCTIONNEMENT.md](FONCTIONNEMENT.md#mode-nomade-hors-de-la-base)).

## Liste du matériel

| Élément | Quantité | Remarque |
|---|---|---|
| LilyGO **T5 V2.4** 2.13" (ESP32 + e-paper DEPG0213BN) | 1 | la V2.3.1 marche mais demande d'autres modifs (voir [Historique](#historique--t5-v231)) |
| Cellules de charge **50 kg** | 16 | 4 par pied |
| Module **HX711** | 4 | un par pied |
| Batterie LiPo 1S (connecteur 2 broches 1,25 mm) | 1 | 2000 mAh utilisés pour les calculs |
| Connecteur **pogo** (paire) | 1 | relie boîtier et base |
| Résistance **1 MΩ** | 1 | réveil au retrait / à la remise sur la base |
| Résistance **10 kΩ** | 1 | pull-up de la détection de charge |
| Fil fin | — | ligne CHRG |
| Boîtier + base imprimés | 1 | [`case/v1/`](https://github.com/baptistev7/ESPScale/tree/main/case/v1/) |

## La carte LilyGO T5 V2.4

- **ESP32-D0WDQ6**, micro-USB, puce USB-série **CH9102** : la carte apparaît en
  `/dev/ttyACM*` (pas `ttyUSB*`).
- **Écran** DEPG0213BN (contrôleur SSD1680, 122 × 250 utiles), piloté par le
  driver `EpdPanel` (GDEY0213B74 + LUT partielle du 213_BN).
- **Charge batterie** : TP4054 + régulateur, pas d'IP5306 (aucun registre I2C
  d'état de charge).
- **Nouveauté de la V2.4** : **GPIO 12 coupe l'alimentation de l'écran**
  (régulateur U6, rail `VCC_IO`). LilyGO ne publie que le schéma de la V2.3 ; la
  V2.4 n'en diffère que par cette coupure (et le correctif de reset qui va avec).

### Le piège du rail `VCC_IO`

Le rail `VCC_IO`, coupé avec l'écran, alimente **aussi les pull-ups 10 kΩ du
lecteur micro-SD** (inutilisé), sur **GPIO 2, 13, 14, 15**. Rail coupé, ces
résistances tirent ces broches **vers 0 V** :

- une broche **tenue haute** sur ce réseau le réalimente et consomme
  (**GPIO 14 = +190 à +480 µA**, mesuré) ;
- une entrée sur ce réseau **lit 0** quand l'écran est éteint — ces broches ne
  conviennent pas pour lire un signal (c'est pour ça que CHRG est sur GPIO 34).

Détail et mesures : [CONSOMMATION.md](CONSOMMATION.md#le-réseau-vcc_io).

**GPIO 19** consomme +87 µA si elle est tenue **basse** en sommeil (cause non
identifiée, aucune LED visible) ; haute, en pull-up ou en pull-down : rien
(mesuré). Elle porte la SCK du pied 3, haute pendant tout le sommeil.

### Broches utilisées

| Fonction | GPIO |
|---|---|
| Écran : CS / DC / RST / BUSY / SCK / MOSI | 5 / 17 / 16 / 4 / 18 / 23 |
| Écran : coupure d'alimentation (`EPD_PWR_EN`, haut = alimenté) | 12 |
| Bouton (Button 1, entrée seule, pull-up de la carte) | 39 |
| Batterie (pont diviseur de la carte) | 35 |
| **CHRG** du TP4054 (+ pull-up externe 10 kΩ) | **34** |
| HX711 pied 1 : DOUT / SCK | 25 / 32 |
| HX711 pied 2 : DOUT / SCK | 22 / 21 |
| HX711 pied 3 : DOUT / SCK | 33 / **19** (pas 14, voir plus haut) |
| HX711 pied 4 : DOUT / SCK | 26 / 27 |
| À ne pas utiliser | 0 (BOOT), 2/13/14/15 (réseau `VCC_IO`) |

## Câblage

### HX711 → carte (à travers le pogo)

| HX711 | Carte |
|---|---|
| VCC | 3,3 V (fil **rouge**, commun aux 4 modules) |
| GND | GND (fil **noir**, commun) |
| DOUT | GPIO DOUT du pied (tableau ci-dessus) |
| SCK | GPIO SCK du pied |

Le numéro de pied est celui de `pins.hpp` / `config.hpp` (`Z_FACTOR_1`…) et de
l'écran INFORMATIONS (`Pied 1` … `Pied 4`). Pour savoir quel pied est où :
appuyer sur un coin du silo et regarder quelle ligne monte (OPTIONS →
INFORMATIONS, page 2).

> ⏳ Mapping repris de la V2.3.1, **à revalider sur la base** avec la V2.4.

### Écran

Intégré à la carte (connecteur FPC), rien à câbler.

### Batterie

Connecteur batterie de la carte. Le pont diviseur intégré (GPIO 35) mesure la
tension ; son ratio (`BATTERY_VOLTAGE_RATIO`) se calibre au multimètre (voir
[CALIBRATION.md](CALIBRATION.md#batterie)).

## Modifications de la carte

Trois petites modifications, toutes **côté boîtier** :

### 1. Détection de charge — fil CHRG → GPIO 34 + pull-up 10 kΩ

- Souder un fil sur la broche **CHRG du TP4054** (U11, patte 1 ; drain ouvert,
  tirée à 0 V pendant la charge) et le relier à **GPIO 34**.
- Souder une **10 kΩ entre GPIO 34 et +3V3** (le 3,3 V de l'ESP, **pas**
  `VCC_IO`).
- Coût : ~0 sur batterie (CHRG est ouverte hors charge), 0,33 mA pendant la
  charge, payés par l'USB.
- Sert à afficher l'**éclair** de charge et à **réveiller la carte** quand on
  branche / débranche l'USB en mode nomade (coprocesseur ULP).

> Pas GPIO 13 / 15 (réseau `VCC_IO`, lues à 0 écran éteint), ni GPIO 19 (ne peut
> pas réveiller la carte).

### 2. Réveil au dédock / redock — 1 MΩ sur le DOUT du pied 1

- Souder une **1 MΩ entre GPIO 25 (DOUT du pied 1) et GND**, **côté boîtier**
  (avant le pogo, sinon elle part avec la base).
- Principe : un HX711 alimenté et en veille (SCK tenue haute) **tient DOUT à 1**.
  Boîtier retiré, plus rien ne pilote la ligne : la 1 MΩ la ramène à **0**. La
  carte se réveille sur ce changement — dans un sens comme dans l'autre.
- Coût : 3,3 µA boîtier posé, 0 retiré.
- Activé par `USE_DOCK_WAKE_DOUT = 1` (`config.hpp`). **Sans la résistance, le
  laisser à 0** : une ligne flottante réveillerait la carte en boucle.

### 3. SCK du pied 3 sur GPIO 19 (pas 14)

La SCK d'un HX711 reste **haute pendant tout le sommeil** (c'est ce qui le garde
éteint). Sur GPIO 14, elle réalimentait `VCC_IO` par la pull-up 10 kΩ du lecteur
SD : **+190 à +480 µA**. Sur **GPIO 19**, tenue haute ne coûte rien (mesuré).
Déplacer le fil SCK du pied 3 de GPIO 14 vers GPIO 19, côté boîtier.

> Alternative non retenue : garder GPIO 14 et dessouder sa pull-up 10 kΩ du
> lecteur SD.

## Boîtier et base

Fichiers CAO dans [`case/v1/`](https://github.com/baptistev7/ESPScale/tree/main/case/v1/) : boîtier vertical + base (dock),
connexion pogo, fixation du connecteur, ailettes.

| Fichier | Contenu |
|---|---|
| `PelletScale.3mf` | projet PrusaSlicer complet (objets `Main`, `BackMain`, `Support`, `BackSupport`) |
| `Main.3mf` / `BackMain.3mf` | boîtier, face avant / dos |
| `Support.3mf` / `BackSupport.3mf` | base, dessus / dos |

Les maillages se modifient par script Python (`trimesh` + `manifold3d`).

## Historique — T5 V2.3.1

Carte précédente ; ces modifications **ne sont pas** à refaire sur la V2.4.

1. **Pull-ups 100 kΩ RST/CS → 3V3** (pattes 10 et 12 du FPC) : sans coupure
   d'alimentation de l'écran, CS/RST flottaient en sommeil et le SSD1680 sortait
   d'hibernation (veille 750 → 250 µA). Inutile sur la V2.4.
2. **Fil CHRG → GPIO 19** : fonctionnait, mais GPIO 19 ne peut pas réveiller la
   carte (broche non RTC).
3. Couper l'alimentation de l'écran demandait de souder sous l'écran collé :
   c'est ce que la V2.4 apporte d'origine (GPIO 12).
