---
title: Dépannage
nav_order: 9
---

# Dépannage

Problèmes rencontrés, leur cause et leur correctif. Pour les questions d'usage
(« elle n'affiche pas l'heure », « elle ne s'est pas réveillée »), voir aussi la
FAQ du [guide utilisateur](GUIDE-UTILISATEUR.md).

## Le WiFi ne se connecte pas

- Vérifier le SSID et le mot de passe dans le portail (`OPTIONS` →
  `PORTAIL RÉGLAGES`) : la connexion est testée à l'enregistrement, rien n'est
  écrit si elle échoue.
- Vérifier la portée (le portail affiche le signal de chaque réseau visible).
- Augmenter `WIFI_CONNECT_TIMEOUT_SEC` (`config.hpp`).

## Le MQTT ne se connecte pas

- Le champ **serveur** attend un **hôte seul** (`192.168.1.2`), pas une URL
  (un `http://` collé est retiré automatiquement).
- Vérifier le port et l'**authentification** si le broker en exige une.
- Bouton **« Tester WiFi + MQTT »** du portail : il dit quel maillon échoue, sans
  attendre un cycle de mesure.

## La lecture des capteurs est instable

- Vérifier le câblage et le contact du pogo.
- Vérifier l'alimentation 3,3 V.
- Augmenter `SENSOR_READ_ATTEMPTS`.
- Rappel : moins de 4 pieds valides → la mesure est ignorée (mode dégradé ou
  nomade), jamais extrapolée.

## La veille consomme trop

Mesurer au PPK2 et comparer au plancher (27 µA) : méthode et diagnostics dans
[CONSOMMATION.md](CONSOMMATION.md). Les causes déjà rencontrées :

- **Une broche tenue haute sur le réseau `VCC_IO`** (GPIO 2, 13, 14, 15) :
  +190 à +480 µA. Cas de GPIO 14 quand elle portait la SCK du pied 3.
- **Des broches laissées dans leur état de démarrage** (pull-ups internes) :
  ~165 µA.
- **GPIO 19 tenue basse** en sommeil : +87 µA.
- **HX711 rallumés pendant le sommeil** (~7-10 mA, voir ci-dessous).

## Veille à ~7-10 mA avec les HX711 branchés — RÉSOLU

- **Cause** : c'est le niveau haut de PD_SCK qui garde un HX711 en veille, or le
  domaine numérique de l'ESP32 s'éteint en deep sleep. Un `hold` posé sur une
  broche encore pilotée par la matrice numérique ne tient rien : SCK retombait et
  les 4 modules se rallumaient.
- **Correctif** : les SCK sur broches **RTC** (32, 14, 27) sont basculées **sous
  le domaine RTC** (`rtc_gpio_init` + sortie haute), qui reste alimenté ; la SCK
  numérique (21) est tenue par `gpio_hold_en()` + `gpio_deep_sleep_hold_en()`.
- ⚠️ `gpio_hold_en()` est **silencieusement inopérant** sur les broches RTC.

## L'heure dérive — RÉSOLU

- **Symptôme** : l'heure affichée avait jusqu'à **+45 min** d'avance.
- **Causes** : la synchro NTP ne se faisait jamais (le test « heure valide »
  était toujours vrai après la première synchro, et l'attente ne prouvait pas
  qu'une réponse NTP était arrivée) ; et l'écran était dessiné avant la synchro.
- **Pourquoi ça dérive** : l'horloge de sommeil de l'ESP32 est un **oscillateur
  RC interne** (pas de quartz 32 kHz sur la carte), précis à quelques %.
- **Correctif** : `timeInit()` attend une vraie réponse SNTP
  (`sntp_get_sync_status()`), avant l'affichage ; les veilles sans réseau
  resynchronisent si l'heure a plus de 24 h.
- **Limite** : les réveils eux-mêmes peuvent tomber quelques minutes à côté du
  créneau ; le suivant se recale.

## L'éclair de charge ne suit pas

- CHRG doit être sur **GPIO 34 avec une pull-up 10 kΩ vers +3V3**. Sur GPIO 13
  ou 15, la ligne est lue « en charge » en permanence (réseau `VCC_IO`).
- **Sans batterie** sur USB, le TP4054 peut annoncer une charge : cas hors
  limite, à ignorer.

## Flasher écrit sur le mauvais appareil

`platformio.ini` désigne la carte par son identifiant stable
(`/dev/serial/by-id/…`). Si l'upload ne trouve pas la carte, remplacer
l'identifiant par celui de la sienne (`ls /dev/serial/by-id/`). Ne pas revenir à
`/dev/ttyACM0` : un autre appareil (un PPK2, par exemple) peut l'occuper.

## Historique — T5 V2.3.1 : veille à ~750 µA

Sans coupure d'alimentation de l'écran, CS et RST (GPIO 5 / 16) flottaient en
sommeil et le SSD1680 sortait d'hibernation. Correctif matériel : pull-ups
100 kΩ CS/RST → 3V3 (veille 250 µA). Ne concerne plus la V2.4, dont l'écran est
coupé par GPIO 12.
