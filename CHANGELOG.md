# Changelog - ESP Scale

## [Unreleased]

## [1.0.1] - 2026-10-09

Correctif : les mesures n'arrivaient pas toutes au broker, et rien ne le disait.

### Correctifs

- **Publications MQTT perdues** : la carte envoyait ses 5 messages en rafale puis
  coupait le WiFi aussitôt ; seul le premier (`scale/version`) arrivait au
  broker, le poids et la batterie restaient à leur ancienne valeur (mesuré en
  écoutant `scale/#` pendant un cycle). Nagle désactivé et envoi laissé partir
  (150 ms) avant la fermeture : les 5 messages arrivent.

### Écran principal

- **Envoi en échec visible** : quand la mesure n'a pas pu être envoyée, `CONNECTÉ`
  devient `WIFI DÉCONNECTÉ` ou `MQTT DÉCONNECTÉ` en haut et la zone du bas
  affiche `ENVOI EN ERREUR` (redessiné en partial, effacé dès qu'un envoi
  réussit). Limite : un broker qui refuse l'écriture en silence (ACL) n'est pas
  détecté, la carte n'ayant pas d'accusé de réception en QoS 0.

### Home Assistant

- **Jours restants sur une moyenne glissante de 14 jours** (au lieu de la
  consommation du jour, indisponible les jours sans consommation) :
  `statistics.yaml`, `suivi.yaml` (début du suivi), `templates.yaml`.
- **Alertes** : batterie faible (< 20 %) et « plus de nouvelles » (6 jours sans
  publication), en plus de silo faible ; notification sur le téléphone **et**
  dans la cloche de HA, fermée toute seule quand l'alerte s'annule.
- **Dashboard « Silo »** refait (cartes HACS : Mushroom, ApexCharts, Button Card,
  Fluid Level Background Card, card-mod) : niveau du silo qui se remplit,
  consommation, stock, historique.
- `ha/sync.sh` lit ses réglages dans `ha/sync.local.env` (ignoré par git).

## [1.0.0] - 2026-10-08

Première version complète : la balance est mesurée sur sa base (veille,
cycles, autonomie) et sa documentation est publiée en ligne.

### Écran principal

- **Rafraîchissement partiel** quand rien d'important ne change : si le poids
  arrondi, la jauge, le bandeau, la batterie et l'éclair sont ceux déjà
  affichés, plus de full (1,5 s, clignotement noir/blanc) mais un partial plein
  cadre (1,2 s, sans clignotement) — ou rien du tout si la date et l'heure de
  mesure n'ont pas changé non plus. Un full revient tous les 20 partials
  (anti-fantôme), à chaque changement d'écran et au démarrage à froid.
- **`MESURÉ À hh:mm`** remplace `MESURE STABLE` dans la zone du bas : l'heure de
  la dernière mesure (`MESURE STABLE` tant que l'heure n'est pas connue ; le
  niveau bas garde `REMPLISSAGE CONSEILLE`).
- **Réveil de 00:02 sur la base** : la date de l'en-tête passe au jour suivant
  par un partial, sans mesure ni réseau ; les mesures restent aux créneaux de
  l'aspiration.

### Batterie

- **Lecture de la tension corrigée** : elle était **0,25 V trop basse** sur la
  T5 V2.4 (ratio calibré sur la V2.3.1). Elle passe par la calibration de la
  puce (`analogReadMilliVolts`, moyenne de 8 lectures, ratio 2,0) : écart
  résiduel ~0,04 V au PPK2.
- ⚠️ **Le % batterie affiché et publié remonte** (batterie pleine : ~72 % →
  ~94 %) : saut visible dans l'historique Home Assistant à la mise à jour. Les
  seuils de protection (3,3 V / 3,2 V) correspondent maintenant à la tension
  réelle.

### Consommation (mesurée au PPK2)

- Veille sur la base **33 µA** (4 HX711 compris), hors base **29 µA**.
- Cycle de mesure **120 à 140 mC** sans publication, **315 à 420 mC** avec ;
  démarrage à froid 500 à 710 mC (le WiFi + NTP en fait 65 à 75 %).
- Autonomie estimée **~10 mois (pessimiste) à ~26 mois (optimiste)** sur
  2000 mAh, autodécharge de la LiPo comprise : la veille et l'autodécharge
  dominent. Détail, hypothèses et graphiques : `docs/CONSOMMATION.md`.

### Documentation

- **Site en ligne** (GitHub Pages, `docs/`) : accueil, menu, recherche, graphiques
  de consommation.
- README et captures d'écran à jour (`MESURÉ À`).

### Outils

- `tools/consommation_graphs.py` : graphiques de `docs/CONSOMMATION.md` (SVG,
  sans dépendance).

### Retiré

- Les bancs de test compilés dans le firmware : démo d'écrans
  `UI_TEST_DASHBOARD` (et ses partials de démonstration), repères de phase PPK2
  et réveils rapprochés ; le script `ppk2_measure.py`.

## [0.12.1] - 2026-10-08

### Écrans

- **INFORMATIONS : `Capteurs` affichait « 0 OK »** alors que les 4 pieds
  remontaient un poids (menu ouvert par un réveil bouton, sans mesure dans la
  session) : le nombre est maintenant compté sur les lectures de l'écran.
- **Ligne `Mesure` (stable / instable) supprimée** : elle ne disait que « 4
  capteurs sur 4 », doublon de `Capteurs`.

### Matériel

- **SCK du pied 3 : GPIO 14 → GPIO 19.** Sur GPIO 14 (CLK du lecteur SD), la
  SCK tenue haute en veille réalimentait `VCC_IO` par la pull-up SD : +190 à
  +480 µA. GPIO 19 tenue haute ne coûte rien (mesuré) : la veille sur la base
  doit tomber vers ~31 µA + HX711 (à mesurer). Le cas spécial « SCK 3 relâchée
  hors base » est supprimé. **Recâbler le fil SCK du pied 3.**
- Correction : GPIO 19 n'a pas de LED identifiée ; seul son état bas coûte
  (+87 µA). Les mentions « LED » sont retirées.

## [0.12.0] - 2026-10-08

### Documentation

- **README réécrit pour un nouvel utilisateur** : ce que fait la balance, ce
  qu'il faut, démarrage en 5 étapes, index de la documentation. Le détail est
  réparti dans `docs/` : `MATERIEL.md` (carte, câblage, modifications, boîtier),
  `CONFIGURATION.md` (flash, portail, `config.hpp`), `CALIBRATION.md`,
  `MQTT-HOME-ASSISTANT.md`, `FONCTIONNEMENT.md` (cycle, bouton, remplissage,
  mode nomade, écrans), `DEPANNAGE.md`, `TODO.md`. Historique des mesures V2.3.1
  déplacé dans `CONSOMMATION.md`.
- Guide utilisateur : le branchement de l'USB réveille la balance (ULP), plus
  besoin d'appui court.

### Consommation

- **Banc de mesure PPK2** : `tools/ppk2_measure.py` (pilote le PPK2 en source
  4,0 V, découpe éveils / sommeil, CSV) et `tools/sleep_floor/` (diagnostic du
  plancher de veille, une configuration par réveil). Résultats et méthode dans
  le nouveau **`docs/CONSOMMATION.md`**.
- **Le plancher de la carte est de 27 µA**, pas 192 µA : les 192 µA venaient
  de pull-ups internes restées actives sur des broches du réseau `VCC_IO`.
  Mesuré aussi : **GPIO 14 tenue haute = +190 à +480 µA** (pull-up SD vers
  `VCC_IO` coupé), **GPIO 19 tenue basse = +87 µA** (LED), ULP / RTC_PERIPH /
  ext0 = 0.
- **Veille nomade : 386 → 29,1 µA** (mesuré). Hors base, la SCK n°3
  (GPIO 14) n'est plus tenue haute — le HX711 est absent, et la broche
  réalimentait `VCC_IO` par la pull-up SD (`prepareDeepSleep(…,
  sensors_present)`). GPIO 19 (LED de la carte) passe en entrée sans pull au
  lieu d'une pull-down qui la faisait conduire.
- `platformio.ini` désigne la carte par son **identifiant stable**
  (`/dev/serial/by-id/…`) : un PPK2 branché prend `ttyACM0`, et flasher
  « ACM0 » écrivait sur lui.
- `USE_CHRG_ULP` surchargeable à la compilation (`-DUSE_CHRG_ULP=0`) pour les
  mesures comparatives.

### Veille nomade

- **Réveil au début et à la fin de charge** : CHRG passe sur **GPIO 34** (RTC) avec une
  **pull-up externe 10 kΩ vers +3V3**, et le **coprocesseur ULP** le lit toutes les ~200 ms pendant le deep sleep
  (`USE_CHRG_ULP`). Il réveille la carte quand le niveau diffère de l'éclair
  affiché sur 5 lectures consécutives (anti-rebond). Plus besoin d'appui court après avoir branché l'USB.
- **Pas GPIO 13 ni 15** (essayées d'abord) : sur la V2.4, leurs pull-ups 10 kΩ
  du lecteur SD sont sur `VCC_IO`, que GPIO 12 coupe avec l'écran. Coupées,
  elles tirent la ligne vers 0 V (~0,6 V mesurés) : « en charge » en
  permanence, et ~60 µA perdus dans la pull-up interne.
- **Le redock passe aussi par l'ULP** en veille nomade : l'IDF refuse ULP +
  ext0 (« Conflicting wake-up trigger »). L'ULP lit donc aussi la ligne DOUT
  (haute = redock → cycle complet), ext0 reste armé sur la base. L'ULP est
  arrêté à chaque réveil et ne tourne qu'en veille nomade.
- La durée du sommeil nomade suit l'**éclair affiché** (5 min s'il est là), plus
  une relecture de CHRG au dernier moment qui pouvait contredire l'écran.

## [0.11.0] - 2026-10-07

### Veille nomade

- **Plus de réveil toutes les minutes ni de cycle de 15 min.** Le redock
  réveille la carte par la ligne DOUT : le timer ne sert plus qu'à tenir l'écran
  à jour. Hors charge, la carte dort jusqu'à **00:02** (ou le quart de batterie
  estimé, `kNomadeQuarterSec` ≈ 94 j, s'il tombe avant ; réveillée avant minuit
  par la dérive RTC, elle se rerendort). Ce réveil fait le cycle complet :
  capteurs (filet du redock), garde batterie, synchro NTP si > 24 h, écran
  redessiné avec la nouvelle date. **En charge**, réveil toutes les **5 min**
  pour montrer la montée de la jauge ; horloge invalide, toutes les heures.
  Compteur `dedock_light_count` et constantes `kNomadeLightPollSec` /
  `kDedockPollSec` supprimés.
- **Appui court sur l'écran nomade : relit la charge** (il était ignoré). Le
  branchement de l'USB n'est vu qu'au réveil suivant — GPIO 19 n'est pas RTC —
  l'appui court fait apparaître l'éclair tout de suite.
- **Partiel de l'en-tête sans flash au réveil.** Avec la coupure écran de la
  V2.4, la RAM de trame est perdue au sommeil et chaque poll faisait un full.
  L'écran nomade est maintenant redessiné **à l'identique** dans les deux RAM du
  SSD1680 (même heure que son dernier full, nouveau champ RTC `disp_drawn_at`,
  layout `STAY`), puis seul l'en-tête change en partiel. Reset matériel de la
  dalle à chaque première init, partial compris.
- L'icône n'est rafraîchie que si ses **segments** ou l'éclair changent, plus à
  chaque point de % (`displayBatteryIconChanged()`).
- **« IL Y A 2 H 34 » devient « MESURÉ LE 07/10 / À 18:12 »** : l'écran n'étant
  plus redessiné qu'une fois par jour hors charge, une ancienneté resterait figée
  et fausse.

### Matériel

- **Pull-down 1 MΩ posée** sur le DOUT du pied 1 : `USE_DOCK_WAKE_DOUT = 1`
  (réveil immédiat au dédock et au redock). Vérifié sur banc : pas de réveil en
  boucle hors base.
- **Fil CHRG → GPIO 19 reposé** sur la V2.4 ; reste à valider avec une batterie
  (sans batterie, le TP4054 ne signale aucune charge).

### Écrans

- **Informations, page 2** : les pieds s'appellent `Pied 1` … `Pied 4` (la
  numérotation de `config.hpp` / `pins.hpp`) au lieu de `AVG` / `AVD` / `ARG` /
  `ARD`, qui supposaient une position physique jamais vérifiée.
- **Correction : poids par pied mal arrondi** — la partie entière était
  arrondie à part de la décimale (82,7 kg affichait « 83,7 kg »). Idem pour le
  total.
- **Nouvel écran « CARTE EN VEILLE »** (bandeau `NON CONFIGURÉ`, pied
  `● COURT RÉVEIL`) quand une carte sans configuration se met en veille à la
  fermeture du portail : la dalle gardait l'écran du portail, mot de passe d'un
  AP éteint compris. Dessiné une seule fois (les réveils de contrôle batterie ne
  le redessinent pas).
- **Capteur(s) KO** : l'écran principal reprend le gabarit du niveau bas —
  bandeau `CAPTEUR(S) HS`, **dernier poids stable** et sa jauge, daté en zone
  secondaire (`DERNIÈRE MESURE` / `STABLE À hh:mm`, ou `LE jj/mm`). Avant :
  ni poids ni jauge, seulement `CAPTEURS KO`.

### Remplissage

- **Lâcher pendant un maintien ne fait plus rien** : la barre se vide et l'écran
  reste, sur « TERMINER ? » comme sur la confirmation du portail (qui quittait).
  Seul un appui court (< 0,4 s, avant que la barre ne commence) déclenche le
  `● COURT`. Un seul helper pour les deux barres (`waitHoldGesture()`).

- **« TERMINER ? » : `● COURT CORRIGER`** ouvre un nouvel écran
  **« CORRIGER ? »** (menu façon OPTIONS) : `NOMBRE DE SACS` (recompte depuis 0,
  le bouton ne sait que +1), `PRIX DU SAC`, `ANNULER` (abandon → le versement
  part en refill sauvage, sans prix) ou `RETOUR`. Avant, tout relâché renvoyait
  au prix, sans moyen de corriger les sacs ni d'annuler.
- **Correction : l'étape « PRIX PAR SAC » était sautée.** Depuis la refonte,
  valider le nombre de sacs menait directement à « TERMINER ? » avec le prix par
  défaut (4,05 €) ; le prix ne se saisissait qu'en relâchant le maintien.

### Documentation

- **Captures à jour** dans `docs/screens/`, embarquées dans le README et le guide
  utilisateur :
  - écrans e-paper : rendu pixel de `tools/preview_charte.py` (nouvelle option
    `--docs`), 18 écrans dont les variantes ajoutées « capteurs KO » et
    « nomade sans mesure ». L'outil écrivait `REMPLISSAGE CONSEILLÉ` alors que la
    dalle affiche `CONSEILLE` : aligné sur le firmware ;
  - portail web : nouveau banc `tools/portal_preview/` qui compile le **vrai**
    `src/webconfig.cpp` sur PC contre des bouchons et capture les 3 onglets
    (Firefox headless, largeur téléphone).
- **Consommation réécrite pour la T5 V2.4** : seule la veille **à vide**
  (**192 µA**) est mesurée ; veille dockée, cycles et autonomie sont marqués
  « à revalider ». Les mesures V2.3.1 (231 µA, ~10 mois) passent en historique.
- **Matériel** : section réécrite pour la V2.4 (CH9102, coupure écran et ses deux
  pièges, modifs à reporter : fil CHRG, pull-down 1 MΩ, pont batterie) ;
  modifs V2.3.1 en historique. Câblage capteurs : mapping à revalider docké.

### Portail web

- **Tableaux lisibles sur téléphone** (vus sur les captures) : la règle
  `th{width:44%}`, faite pour les tableaux libellé/valeur, écrasait les tableaux
  en colonnes — colonne `C` tronquée et bouton `Δ → C` sur trois lignes en
  calibration, canal du scan WiFi coupé en deux. Classe `grid` pour ces tableaux.
- Onglet ÉTAT : « Poids net » devient « Poids » (il n'y a plus de tare silo).

- **« Rafraîchir l'écran » ne fait plus perdre l'écran du portail** : l'écran
  principal dessiné pour le test restait affiché, sans le pied (comment fermer le
  portail) ni le mot de passe de l'AP. L'écran du portail revient seul après
  ~8 s ; la page reste servie pendant ce temps.

## [0.10.1] - 2026-10-06

### Remplissage

- **Plus jamais d'envoi avec un prix non confirmé.** Sur « TERMINER ? », le
  relâché avant la fin ne faisait rien, et seul le délai de 10 min renvoyait à la
  saisie du prix ; après correction, l'enregistrement partait **sans nouveau
  maintien** et avec le prix, le poids et le nombre de sacs figés **avant** la
  correction. Désormais :
  - relâché avant la fin (anti-rebond 80 ms) → retour à la saisie du prix, curseur
    sur le **1er chiffre** (avant, seul le dernier restait corrigeable) ;
  - après correction, la mesure « après » est refaite et le récap se réaffiche
    **recalculé** : il faut le tenir à nouveau 2,5 s ;
  - délai de 10 min dépassé → **abandon**.
- **Abandon câblé vers le refill sauvage** (`refillAbandon()`) : délais du
  versement, du comptage, du prix ou du récap, et poids en baisse. Le parcours ne
  publie rien lui-même ; il repasse en `DOCKED` et enchaîne un **cycle de mesure
  normal** : une hausse ≥ ~1 sac part en refill sauvage (`bag_count = 0`,
  valorisé au prix moyen côté HA), une baisse en consommation. L'écran revient
  sur l'écran principal au lieu de rester sur l'écran du parcours en veille.
- **Remplissage coupé par un reset** (watchdog, chute de tension) : un réveil qui
  trouve encore le mode `REFILL_*` le traite en abandon (même chemin). Avant, la
  carte se rendormait « bouton seul », **sans timer** : plus aucune mesure tant
  que personne n'appuyait, et l'abandon prévu au bout de 2 h ne pouvait jamais se
  déclencher. Le parcours se faisant carte éveillée, le sommeil sans timer est
  supprimé (`prepareDeepSleep()` arme toujours le timer de secours ;
  `kRefillSleepSec` et `kRefillAbandonSec` retirés).

### Veille nomade et retour sur la base

- **Réveil toutes les minutes en veille nomade** : le code n'ajoutait qu'un poll
  léger 1 min après chaque poll complet (donc un suivi de charge à ~15 min). Un
  compteur RTC (`dedock_light_count`, layout d'état 5) fait maintenant 14 polls
  légers (charge seule) puis 1 complet, soit un réveil par minute et un cycle
  complet toutes les 15 min, comme documenté.
- **Le retour sur la base est toujours publié** : la mesure 4/4 qui suit une
  erreur annoncée (dédock, capteurs KO) publie `error/on-off = off` et `quality`
  même si le poids n'a pas bougé. Avant, HA gardait `error = on` (retain) jusqu'au
  heartbeat (~4 jours) ou à la prochaine baisse.
- **Écran nomade : l'appui court ne fait plus rien.** Il relançait une mesure
  alors que le pied n'annonce que `— LONG OPTIONS` (et qu'hors base, il n'y a
  rien à mesurer) ; la carte se rendort.
- **`USE_DOCK_WAKE_DOUT` repasse à 0** : la pull-down 1 MΩ n'est pas posée, et
  sans elle la ligne flotte hors base avec un réveil armé sur HIGH (risque de
  réveils en boucle). Le redock est vu au poll complet suivant (≤ 15 min).

### Portail web

- **Point d'accès protégé** : WPA2 avec un mot de passe de 8 chiffres tiré à
  chaque ouverture et affiché sur l'écran « PORTAIL ACTIF » (3 groupes : réseau,
  mot de passe, adresse). Il faut être devant la balance pour rejoindre le
  portail.
- **Les mots de passe enregistrés ne sont plus renvoyés dans la page** (WiFi et
  MQTT étaient lisibles dans le source par quiconque rejoignait l'AP). Champ vide
  = inchangé, tant que le réseau / l'utilisateur MQTT est le même.
- **« Tester » n'enregistre plus rien.** `parseForm()` écrivait les réglages du
  silo en NVS, y compris pendant le test et quand l'enregistrement était refusé
  (WiFi KO). Le bouton devient « Tester WiFi + MQTT (sans enregistrer) », placé
  dans la section du broker ; la page réaffiche les valeurs saisies (silo
  compris). Le silo n'est écrit qu'avec le reste, après un test WiFi réussi.
- **Texte de confirmation réparé** : `data-mm='… n'est …'` coupait l'attribut à
  l'apostrophe, la popup affichait un texte tronqué. Attributs passés entre
  guillemets doubles.
- **Tare « silo vide » supprimée** (champ, action `/tare`, récap « charge max »)
  : elle faisait doublon avec le zéro de la calibration, et l'installation ne
  permet pas de vider le silo — appuyée avec des granulés, elle mettait le net à
  ~0 et HA comptait tout le contenu en consommation. La clé NVS `silo_tare` est
  effacée au démarrage ; ⚠️ si elle valait > 0, le poids publié **remonte
  d'autant une fois** (refill sauvage si ≥ 14 kg).
- **Carte sans configuration : plus de portail relancé toutes les 10 min.** À la
  fermeture du portail sans enregistrement, la carte dort (réveil quotidien de
  contrôle batterie seul, 1 semaine sous 3,3 V) ; le bouton rouvre le portail.
  Avant, l'AP restait allumé en continu, sans garde batterie. Le pied de l'écran
  du portail le dit : `● COURT VEILLE` sur une carte sans configuration,
  `● COURT REDEMARRER` sinon.
- **`● COURT REDEMARRER` du portail est câblé** : le pied annonçait une action
  qui ne faisait rien — le pire défaut possible, une interface qui promet un geste
  inexistant. La boucle du portail active la capture des gestes et sort sur appui.
  Le libellé passe de `ARRETER` à `REDEMARRER` parce que c'est ce qui se passe :
  `webConfigRun()` est terminale (elle ne rend jamais la main), donc sortir ne peut
  pas revenir au menu, contrairement à l'écran nomade.
  Deux détails non triviaux :
  - la tenue du bouton qui **lance** le portail est absorbée avant d'armer la
    capture — sinon le relâchement serait compté comme un appui long et le portail
    se refermerait dans la seconde ;
  - la capture n'est active que pendant le portail (l'ISR de timer consomme du
    courant) et coupée avant le redémarrage.
  - ✅ **validé sur la carte** : l'appui ferme bien le portail et la carte
    redémarre. L'absorption de la tenue fait son office : le portail ne
    se referme pas tout seul à l'ouverture.

### Réglages

- **Le poids d'un sac est réglable** (onglet Réglages), en NVS
  (`silo_bag_kg`). C'était la seule valeur d'installation encore compilée avec
  son poids réel dans le parcours (`12 sacs × 15 kg`) : le poids d'un sac varie
  selon le fournisseur et l'humidité du granulé, donc c'est une donnée du site,
  pas du firmware. `BAG_KG` devient un simple défaut de formulaire, comme
  `TANK_FULL_KG`. Bornes 1..50 kg.

### Écrans

- **Informations : `● COURT AUTRE PAGE` sur les deux pages** (au lieu de
  `PAGE SUIV.` / `PAGE PREC.`) : le court bascule simplement 1 ↔ 2.
- Maquettes `ECRAN04` (mot de passe de l'AP), `ECRAN18` et `ECRAN19`
  (`AUTRE PAGE`) mises à jour, captures régénérées.

### Documentation

- Guide utilisateur corrigé : `error/on-off` (`on` = capteurs muets), publication
  des hausses non déclarées (refill sauvage), abandon du parcours (la note
  « appoint » décrivait un comportement inexistant), saisie du prix (`● COURT` =
  +1, `— LONG` = chiffre suivant), délai de redock, pied de la page 2 des
  informations, portail (mot de passe, test).
- README et SPEC alignés (veille nomade, abandon, portail, redock, tare).
- Commentaires obsolètes de la V2.3.1 retirés (`pins.hpp`, `power.cpp`,
  `display.cpp`).
- `FW_VERSION` passe à `0.10.1` : le poids de sac réglable et `REDEMARRER`
  étaient sortis sous le numéro `0.10.0`.

### À faire

- Revalider `BATTERY_VOLTAGE_RATIO` sur la V2.4 (calibré sur la V2.3.1).
- Poser la pull-down 1 MΩ puis repasser `USE_DOCK_WAKE_DOUT` à 1.

## [0.10.0] - 2026-10-05

### Portail web

- **Une page de réglages au lieu de trois, un seul enregistrement, un seul
  redémarrage.** Les onglets WiFi, MQTT et silo n'ont pas d'existence propre :
  leurs champs partagent un formulaire unique (« Réglages »), et un bouton
  « Enregistrer et redémarrer » suffit. Le redémarrage est le seul chemin de
  sortie du portail, c'est donc le geste le plus cher de l'interface — le faire
  trois fois pour configurer trois choses était absurde. Exception assumée :
  « Tare silo vide » reste une action à part, qui applique sa tare sans
  redémarrer (c'est une mesure, pas un réglage).
- **La calibration est un onglet du portail**, plus une page séparée avec son
  propre `<head>`, son CSS et son lien de retour. Le CSS est désormais commun.
- **Confirmation par une popup du portail** au lieu des `confirm()`/`alert()` du
  navigateur : ces derniers affichent le nom du domaine du navigateur et
  ignorent le style de la page. Elle sert aux actions destructrices
  (enregistrer + redémarrer, tare silo vide) et aux messages de la calibration.
- **Carte « Ce point d'accès » supprimée** : rien n'y était modifiable, donc
  c'était de l'occupation d'écran pour rien.
- **« Charge max (tare + capacité) » : le récap était un `<tr>` orphelin**
  (hors de tout `<table>`), donc foster-parenté par le parseur HTML et le bouton
  venait se coller à la ligne — l'alignement était cassé. Le récap est maintenant
  dans son `<table class=derived>`, avec une marge propre.
- **« Rescanner » est devenu un lien (GET), plus un formulaire** : il était
  imbriqué dans le formulaire principal, ce qui est du HTML invalide — le
  navigateur fermait le formulaire externe au milieu, et les champs situés après
  n'étaient plus envoyés. Le rescan est de toute façon une lecture.

- **Le portail devient une coquille à 4 onglets** (ÉTAT / RÉSEAU WI-FI /
  MQTT·HA / SILO), sur la base des maquettes `docs/UI/WEB01`–`WEB07`. Une page
  unique qui empilait tous les champs était illisible sur téléphone, et chaque
  enregistrement touchait à tout.
  - **ÉTAT** : ce que la carte sait maintenant, lu de la RAM — poids net,
    niveau, seuil, date de la dernière mesure, batterie, dernier envoi MQTT,
    mode, prochain réveil, RAM libre. Une valeur inconnue affiche `--` au lieu
    d'un chiffre inventé, et la date n'est montrée que si l'horloge est
    synchronisée (sinon « horloge non synchronisée », pas un 01/01 de l'époque).
  - **RÉSEAU WI-FI** : le scan passe d'une liste déroulante à un **tableau**
    (RSSI, **canal**, sécurité) avec un bouton « Choisir » qui recopie le SSID,
    plus un **Rescanner** manuel (le scan coupe le canal de l'AP le temps de
    l'opération : le faire à la demande, pas à chaque chargement).
  - **MQTT · HA** : + le **tableau des sujets publiés**, généré depuis les mêmes
    chaînes que `mqtt.cpp` — il ne peut pas mentir sur le nom d'un topic.
  - **SILO** : capacité utile, tare « silo vide » et seuil « niveau bas » (kg),
    les 3 en NVS sous leurs propres clés donc **appliqués sans redémarrage**,
    + l'action **Tare silo vide** (le poids mesuré devient la tare ; refusé si
    les 4 pieds ne répondent pas, et borné par la capacité).
  - **Rafraîchir l'écran** : redessine l'e-paper avec le **dernier poids déjà
    mesuré**, pour vérifier l'affichage sans attendre le cycle de 18 h. Ce n'est
    pas une mesure.
  - **« Choisir » ne remplissait le champ SSID qu'avec les SSID d'un seul
    mot** : le SSID était écrit comme un littéral JavaScript (`pick(Ma Box)`
    → `SyntaxError`). Le SSID passe maintenant par un attribut `data-ssid`, où un
    espace, un accent ou un chiffre initial n'a rien de spécial, et toute la
    ligne est cliquable.
  - **Enregistrer depuis un onglet ne cassait plus rien** : chaque onglet n'envoie
    que SES champs, alors que la lecture du formulaire en attendait tous — valider
    le mot de passe WiFi depuis l'onglet Réseau renvoyait « Port MQTT invalide
    (1-65535) » (port absent -> 0) et aurait vidé le broker ; inversement,
    l'onglet MQTT aurait vidé le SSID. `parseForm()` part maintenant de la config
    **stockée** et n'écrase que les champs **présents** dans la requête. Un champ
    caché `mqtt_form` lève l'ambiguïté de la case à cocher (une case non cochée
    n'est pas envoyée : impossible de la distinguer d'un formulaire muet).
  - Les actions renvoient **l'onglet d'où l'on vient** (`/save?t=`, `/test?t=`) :
    le verdict du test réaffichait les champs sur l'onglet ÉTAT au lieu de l'onglet
    MQTT, alors que la page annonce le contraire.
  - `/cal` reste la page de calibration des pieds (opération de banc, protocole
    avant/après) et gagne un lien « Retour au portail » : depuis la coquille à
    onglets on y entrait plus sans pouvoir en sortir.
  - `htmlEscape()` sur **toute** donnée saisie (SSID, mots de passe, champs de
    formulaire) : un SSID contenant `'` ou `"` cassait l'attribut HTML et
    pouvait injecter du JS dans la page.
  - Écarté volontairement : le **miroir de l'écran e-paper**, le profil
    d'affichage, les seuils côté HA, le CSV et le syslog. Le portail configure et
    consulte ; au-delà de la tare et du rafraîchissement, il n'exécute rien.

### Affichage

- **FIN de l'ancien style : plus aucun écran ne lui appartient.** Les deux
  derniers rendus hors charte sont supprimés, et le firmware n'inclut plus les
  5 polices Adafruit `FreeMono*` (~40 ko de flash libérés).
  - **écran « capteurs KO »** (`SENSOR_KO`) : supprimé. Tant que la résistance
    de tirage de la ligne DOUT n'est pas posée, « 0 capteurs » veut dire « hors
    base » → c'est l'écran du **mode nomade** (charte) qui s'affiche. La
    distinction panne / dédock reviendra avec le vrai dédock (TODO(dedock) :
    ligne DOUT + résistance). `enterErrorState()` ne demande donc plus la
    confirmation de dédock, et `DisplayScreen::SENSOR_KO` disparaît.
  - **écran texte « TEST »** (`TEST`, FreeMono 9 pt, 10 car./ligne) : supprimé.
    Pendant « Tester WiFi + MQTT » (~15-20 s), le verdict est renvoyé dans la
    **page web** — que l'utilisateur est en train de lire ; l'e-paper n'affiche
    rien (un full refresh de moins). `DisplayScreen::TEST` disparaît aussi.
  - Code mort supprimé : silo ÖkoFEN endormi, « Zzz », jauge à granulés, corps
    de veille, ancienne batterie verticale, `printCentered` / `centeredX`,
    `displayDrawBatteryIcon`.
- **`● COURT RECHERCHER` retiré de l'écran nomade** : le pied ne montre plus que
  `— LONG OPTIONS`. Le re-test manuel du dock était redondant — le redock réveille
  déjà la carte par la ligne DOUT, et le poll complet de 15 min rattrape sinon. Un
  appui court en veille est donc **iner** et **non annoncé**.
- **La date de l'en-tête est récupérée même en veille nomade** (elle restait
  `--/--/--`). Le RTC interne est perdu à chaque coupure d'alimentation et dérive de
  plusieurs % ; or `timeInit()` n'était appelé que par `publishBegin()`, c'est-à-dire
  **uniquement sur un chemin qui publie**. Le chemin dédock ouvrait le réseau pour
  publier l'« offline » mais **sans** synchro NTP → date invalide, et les polls de
  1 min ne la récupéraient pas non plus. `appSyncClockIfStale()` ouvre désormais
  UNE session sur tous ces chemins, **seulement** si l'heure est invalide ou
  vieille de plus de 24 h (`g_ntp_last_sync` en RAM RTC) ; côté veille nomade, le
  poll léger réessaie et **redessine** l'écran quand il récupère l'heure (la date
  ne change qu'au changement d'écran). Vérifié sur la carte après un reset à
  froid : `Clock: syncing (invalid or older than 24h)` puis `NTP synced: …`.
- **Le menu ne s'ouvre plus sur une ligne inactive** : hors base, la sélection
  initiale est `PORTAIL RÉGLAGES` (`menuFirstEnabledRow()`) — avant, le curseur
  démarrait sur `REMPLISSAGE` qui n'est pas sélectionnable hors base : aucune barre
  de sélection à l'écran, et deux appuis pour atteindre la première entrée
  possible. Sur la base posée, `REMPLISSAGE` reste bien la ligne par défaut.
- **`INFORMATIONS` : `● COURT` fait osciller les deux pages** (autant de fois que
  voulu, c'est une consultation) et `— LONG` rend la main au menu — les deux pages
  annoncent désormais les mêmes gestes. Sans geste pendant 1 min, retour au menu.
- **`REMPLISSAGE` hors base : inactif plutôt qu'absent.** Le menu garde ses
  **4 lignes** (l'espacement ne change pas d'un écran à l'autre) ; la ligne est
  dessinée **contour 1 px + tramé 25 %** — le tramage EST le gris sur une dalle
  1 bit — et le **curseur la saute**, pour que « choisir » n'agisse jamais pour
  rien. Un tramé plein rendait les majuscules illisibles : c'est le rendu hors
  matériel (`tools/preview_charte.py`) qui l'a montré.
- **Veille nomade : surveillance de la charge toutes les minutes.** La carte se
  réveillait toutes les **15 minutes** pour un cycle complet (4 HX711 = 2,1 s,
  publications). Elle alterne désormais deux polls : **léger** (1 min, la CHARGE
  seule : ADC + partial de la zone d'en-tête si le % ou l'USB a changé) et
  **complet** (15 min, inchangé). Un réveil qui doit mesurer — redock par la ligne
  DOUT, démarrage, appui sur le bouton — n'est jamais traité comme un poll léger
  (`appRunScheduledCycle(force_full)`), et le drapeau `dedock_light_poll` est
  remis à zéro dès que le mode n'est plus `DEDOCKED`.
  ⚠️ `STATE_MAGIC` passe à `0x53544157` (layout 4, nouveau drapeau en RAM RTC) :
  **au prochain démarrage l'état RTC est réinitialisé une fois** (le poids publié
  n'est plus mémorisé — le cycle suivant le republie, le journal de remplissage et
  l'OTA ne sont pas concernés).
- **Le menu `OPTIONS` est accessible depuis le MODE NOMADE** : `— LONG` sur
  l'écran de veille ouvre le menu, comme sur l'écran principal (avant : les
  2 pages d'informations). Le geste long veut dire la même chose partout.
  - Hors base, `REMPLISSAGE` **disparaît** du menu : l'étape 1 du parcours commence
    par une mesure, donc l'entrée serait une promesse impossible. Le menu passe à
    **3 entrées** (`PORTAIL RÉGLAGES`, `INFORMATIONS`, `FERMER`), lignes à
    104 / 136 / 176 — `FERMER` garde sa ligne. Maquette `docs/UI/ECRAN22/`.
  - `FERMER` rend l'écran du **mode courant** : écran principal avec le dernier
    poids connu si le boîtier est posé, **écran nomade** sinon (afficher une jauge
    de silo et « CONNECTÉ » hors base serait un mensonge).
  - Le pied du mode nomade passe de `— LONG INFORMATIONS` à `— LONG OPTIONS`
    (maquette `ECRAN17` re-rendue).
- **Maquette de la variante « en charge »** : `docs/UI/ECRAN21/` (écran
  principal + éclair dans l'en-tête), avec le rendu pixel dans
  `tools/preview/main_charge.png`.
- **La CHARGE est de nouveau visible — dans l'en-tête, donc partout.** Le
  passage à la charte avait perdu l'éclair : `drawHeader()` ignorait le flag
  `charging`, qui n'était plus que l'état mémorisé en RAM RTC. L'éclair est
  dessiné à `x = 84`, à gauche du bloc batterie, par `drawHeader()`.
- **Le partial de batterie de l'écran nomade peignait l'ancien style par-dessus
  l'en-tête.** `displayShowNomadeBattery()` rafraîchissait une fenêtre 24 × 40
  où il **redessinait l'ancienne batterie verticale** : le séparateur d'en-tête
  et le bloc 3 segments étaient effacés, et l'écran retombait en FreeMono /
  icône verticale dès que le % de batterie changeait (toutes les 15 min en
  veille nomade). Il passe maintenant par la **zone d'en-tête** de la charte
  (`drawHeaderBatteryZone()`, 32 × 19), **partagée** avec le partial de
  `MAIN` — même fenêtre, même dessin, un seul helper.
- **Les 2 écrans de fin du parcours de remplissage** (maquettes `docs/UI/ECRAN14`
  et `ECRAN15`, ajoutées au banc `-D UI_TEST_DASHBOARD`) :
  - `ECRAN14` **« TERMINER ? »** (bandeau `VÉRIFIER`) : ce qui va être
    enregistré — poids **réellement** ajouté (légende `Ajout réel` + héros
    `185 KG`), `15 sacs × 15 kg`, `Coût : 103,50 €` — puis une **barre de
    maintien** en zone secondaire. Il faut **tenir 2,5 s** pour sauver ; relâché
    avant la fin → retour à la saisie du prix, **rien n'est publié**. Pied
    **réduit à une ligne** `— LONG SAUVER` (nouveau helper
    `drawFooterLongOnly()`) : un `● COURT` y serait un geste inexistant.
  - `ECRAN15` **« ENREGISTRÉ »** (bandeau `REMPLISSAGE SAUVÉ`) : le récap sur
    **2 lignes** de zone secondaire (`15 sacs · 103,50 €` puis
    `0,56 €/kg · 462 kg`), affiché **1 minute**, puis retour à l'écran principal
    **sans remesure**. Seul `● COURT PRINCIPAL` est annoncé (l'appui long en est
    un alias).
  - `drawHoldProgress(écran, pct)` : le partial de la barre de maintien est
    désormais **mutualisé** avec la confirmation du portail (même dessin,
    même fenêtre 104 × 22) ; `kHoldConfirmMs` (2,5 s) est partagé par les deux
    écrans à tenue.
  - `enum DisplayScreen` : `REFILLING`/`PRICE`/`CONFIRM`/`RESULT` deviennent
    `REFILL_LIVE`/`REFILL_BAGS`/`REFILL_PRICE`/`REFILL_CONFIRM`/`REFILL_SAVED`
    (un écran = un identifiant).
  - Placement au pixel près de la légende `Ajout réel` : en `JbmXb5` le jambage
    du « j » touchait l'encre du héros (qui commence à y = 105, la pointe du
    bandeau finissant à 92) → **`JbmXb4` à y = 101** (encre 95..103, 2 px
    dessous et 1 px dessus). Le récap d'`ECRAN15` est en `JbmXb4` pour la même
    raison : en `JbmXb5` les crochets de la zone secondaire sortaient des
    marges (2..120 au lieu de 10..112).
- **Convention des pieds : plus de deux-points.** Le nom du bouton reste à
  gauche (`● COURT` / `— LONG`, ancré sur `kMarginL`), l'action est alignée **à
  droite** sur `kMarginR` — c'est cet alignement qui fait la colonne, pas un
  séparateur. Les 10 appels de pied sont repassés en mots nus, et 2 helpers
  one-line completed the set : `drawFooterOne()` (action courte seule),
  `drawFooterLongOnly()` (action longue seule). Effet de bord bienfaisant :
  **4-5 px de budget en plus** par action (`INFORMATIONS`, la plus longue,
  70 → 66 px pour 77 px utiles).
- **Menu `OPTIONS` : 4 entrées, dont `FERMER`.** Le menu a désormais une **issue
  explicite** — sans elle, on n'en sortait qu'en choisissant une action.
  `FERMER` est **séparé** de 8 px des trois autres lignes (`menuRowY()`), ce n'est
  pas une 4ᵉ fonctionnalité, c'est la sortie. Géométrie inchangée par ailleurs
  (base 96, pas 24) et partial de déplacement de la sélection adapté.
- **`VEILLE` retirée du menu** : l'entrée n'était pas définie ; `FERMER` la rend
  sans objet (dormir = écran principal + deep sleep).
- **Anciens rendus du parcours supprimés** (plus de code mort) : écran
  « versement » (silo + granulés + compteur `xN`), saisie de prix en `FreeMono`,
  récap `+X kg / N sacs / X,XX EUR`, écran résultat `Envoye !` / `Annule !`,
  aide-bouton `court`/`long` en 9 pt (`drawHints`) et leurs zones de partial.

- **Signe `€` dans les 5 polices de la charte** : le glyphset couvrait Latin-1
  (`0x20–0xFF`), le signe euro (`U+20AC`) était donc impossible à afficher. Le
  glyphe a été **ajouté sans toucher aux glyphes existants** (retrouvée la taille
  pixel exacte de chaque police en re-rendant un glyphe de référence et en
  comparant bit à bit : 8 / 10 / 12 px pour JetBrains Mono, 20 / 59 px pour
  Oswald wght=700) puis rangé dans le **slot `0x100`**, juste au-dessus de
  Latin-1 ; `utf8Next()` replie `U+20AC` sur ce slot. Un `last = 0x20AC` aurait
  coûté 8237 entrées de glyphe (49 ko de flash) pour un seul caractère. Vérifié :
  les 224 glyphes d'origine sont bit-identiques, et le € a été relu au rendu
  hors matériel dans les 5 polices.
- **Remplissage — ÉTAPE 3/3 « PRIX PAR SAC » : saisie par digits** (ancien
  parcours de l'écran de prix) + **HARMONISATION des 3 écrans**
  (maquettes `ECRAN11`/`12`/`13` régénérées, banc `-D UI_TEST_DASHBOARD`) :
  - les 3 étapes partagent **un seul squelette** (`drawRefillStepBody`) : titre
    `REMPLISSAGE`, bandeau + pointe, héros, contexte sous le héros, barre
    d'avancement en zone secondaire, pied. **Ni sous-titre `ÉTAPE n / 3` ni
    étiquette** au-dessus du héros (`AJOUTÉ`, `SACS`, `PRIX DU SAC` retirés) : le
    bandeau nomme l'étape et la barre d'avancement — identique sur les 3 écrans,
    10 cases et compteur `n/3` — est le seul indicateur d'étape ;
  - étape 3 : le prix est saisi **par digits**, une **case entoure le digit en
    cours** (les trois cases ne tiennent pas à la taille du héros : 122 px, la
    largeur de la dalle) ; étiquette `PRIX DU SAC`, coût total en contexte
    (`Total : 103,50 €`), barre pleine + `3/3`, pied `● COURT PLUS` /
    `— LONG SUIVANT` (`VALIDER` sur le dernier digit). Partial zoné à chaque
    changement de digit.
  - `drawProgressBar(step)` : barre unique (10 cases) et compteur `n/3` dans le
    cadre de la zone secondaire ; `formatDec2()` pour la virgule française.

- **Remplissage — ÉTAPE 2/3 « NOMBRE DE SACS »** (maquette `docs/UI/ECRAN12`,
  ajoutée au banc) : en-tête d'étape commun (`REMPLISSAGE` + `ÉTAPE 2 / 3` sous
  le titre), bandeau `NOMBRE DE SACS`, le **nombre de sacs en héros**, puis
  `Théorie : N kg` (nb × `BAG_KG` — le repère qui sera comparé au poids réellement
  versé), et dans la zone secondaire la **barre d'avancement** (10 cases) avec le
  compteur `2/3`. Pied `● COURT + 1 SAC` / `— LONG VALIDER`.
  - `displayShowRefillBagsUpdate()` : `+ 1 sac` en partial zoné (même fenêtre que
    l'étape 1) — nombre, théorie et barre avancent d'un coup.
  - Nouveau `drawRefillStepHeader(step, steps)` (titre + sous-titre) et
    `drawProgressBar(step, steps)` ; l'étape 1 perd sa barre de 3 cases au profit
    du sous-titre, la barre d'avancement vivant désormais en zone secondaire.
- **Remplissage — ÉTAPE 1/3 « EN COURS »** (maquette `docs/UI/ECRAN11`, ajouté
  au banc `-D UI_TEST_DASHBOARD`) : le parcours de remplissage passe à **3
  écrans**, avec une **barre d'étapes** (3 cases pleine largeur) qui se remplit
  d'une case par écran, `● COURT SUIVANT` / `— LONG TERMINER`.
  - Rendu : titre `REMPLISSAGE`, bandeau `EN COURS` (pointe vers le bas), étiquette
    `AJOUTÉ`, **poids ajouté en héros** (OswaldBold30 + unité, même format que
    l'écran principal), barre d'étapes, zone secondaire **alignée à gauche** sur
    2 lignes (`Total : 462 kg` / `Niveau : 92 %`).
  - **Suivi en direct** : `displayShowRefillLiveUpdate()` rafraîchit en **partial
    zoné** (120 × 122 px, y 94 → 215) le bloc qui bouge — le poids revient en
    vingtaines de kilos, un full par lecture (~1,3 s) serait inutilisable. Sans
    effet si l'écran affiché n'est pas `REFILLING`.
  - Nouveau composant `drawStepBar(step, steps)` et variante de zone secondaire
    `drawSecondaryBoxLeft2(l1, l2)` (cadre pleine largeur, texte à gauche).
  - ⚠️ Le `+` du brief est porté par le libellé `AJOUTÉ` : `+185` fait 110 px en
    `OswaldBold30` pour 110 px utiles, l'unité ne tiendrait plus à côté, alors que
    `185 KG` (109 px) reste dans les marges et garde le format commun.
  - ⚠️ **Parcours pas encore câblé** (l'ancien écran « versement » reste celui
    utilisé par la FSM), et
    l'ancien écran « versement » (silo + granules + `xN sacs`) reste celui
    utilisé par la FSM — il sera supprimé quand les 3 écrans seront là.

- **Nouvel écran de CONFIRMATION de démarrage du portail** (maquette
  `docs/UI/ECRAN07`, ajouté au banc `-D UI_TEST_DASHBOARD`) : l'AP du portail
  étant **ouvert**, le lancement doit être un geste délibéré.
  - Rendu : titre `PORTAIL WIFI`, bandeau inversé `RÉGLAGES LOCAUX` (en
    `JbmXb5` : 105 px en `JbmXb6` pour 110 px utiles), la question centrée sur
    4 lignes (`Démarrer le` / `portail Wi-Fi` / `de réglage` / `local ?`), le
    mot `MAINTENIR`, puis la **barre de maintien** dans la zone secondaire.
  - Barre de maintien : 10 cases entre crochets (`drawHoldBar`), remplies d'une
    case toutes les 10 % en **partial zoné** (~856 ms sans flash) via
    `displayShowPortalHoldProgress()` — repli sur le full si la base n'est pas
    valide. Cases vides en contour 1 px, sinon elles disparaissent sur la dalle.
  - Gestes (`appPortalConfirm()`, `app.cpp`) : **appui court = quitter** (aucun
    changement d'écran, l'appelant redessine), **tenue de 2,5 s = lancement** →
    `webConfigRun()` (la page suivante est bien le portail), relâché avant la fin
    ou **30 s** sans appui = abandon. Durées `kPortalHoldMs` /
    `kPortalConfirmTimeoutMs` regroupées à côté du texte affiché.
  - `DisplayScreen::PORTAL_CONFIRM` ajouté (layout de `ScaleState` inchangé).
  - ⚠️ La page n'est **atteignable que par le banc** pour l'instant : le câblage
    réel se fait depuis `OPTIONS` → « PORTAIL RÉGLAGES », qui attend l'arbitrage
    de l'appui long sur l'écran principal.
  - Le pied ne pouvait pas porter `: MAINTENIR` (55 px en `JbmXb4` → 123 px pour
    116 px utiles) : l'instruction est donc portée par le mot `MAINTENIR` au
    centre, le pied gardant `: LANCER`.
- **Nouvel écran `INFORMATIONS` sur 2 pages** (maquettes `docs/UI/ECRAN14` /
  `ECRAN15`, ajoutées au banc `-D UI_TEST_DASHBOARD`) : charte reprise des autres
  écrans (en-tête date + batterie, titre centré, zone secondaire réservée,
  pied `● COURT / — LONG`), contenu en **tableau à deux colonnes** — libellé
  flush gauche, « : » en colonne fixe, **valeur alignée à droite** (c'est
  l'alignement qui fait la colonne) :
  - **page 1 « INFORMATIONS »** : `Batt.` / `Wi-Fi` / `MQTT` / `Signal` /
    `Mesure` / `Capteurs`, **sans bandeau**, zone secondaire `PAGE 1 / 2`, pied
    `● COURT PAGE SUIV.` / `— LONG RETOUR` ;
  - **page 2 « CAPTEURS »** : même structure que la page 1 — titre (sans
    pagination), tableau à deux colonnes, **pagination en zone secondaire**
    (`PAGE 2 / 2`), pied **réduit à une ligne** `● COURT RETOUR` (dernière
    page : plus de « page suivante »). Les quatre pieds `AVG` / `AVD` / `ARG` /
    `ARD` (pied non valide affiché `--`) sont étalés sur la zone de contenu et le
    **total tient sur une ligne, comme ligne du tableau**. Le bandeau
    `RÉPARTITION` a été retiré (le titre dit déjà la même chose) et avec lui le
    verdict `équilibré`/`déséquilibré` — le calcul est supprimé, pas de code
    mort.
  - `DisplayScreen::INFORMATIONS` ajouté à l'enum (layout de `ScaleState`
    inchangé, `STATE_MAGIC` non bumpé). Les valeurs sont **passées en
    paramètres** (`displayShowInfosGeneral` / `displayShowInfosSensors`) : le
    rendu ignore le Wi-Fi, un relevé réseau réel coûtera une session, à faire
    sur demande uniquement.
  - ⚠️ Titre en `JbmXb6` et non `OswaldBold10` : « INFORMATIONS » fait 121 px en
    Oswald pour 122 px de dalle (il toucherait les deux bords). Un titre long sur
    cet écran demandera une **coupe condensée d'Oswald** à générer.
  - ⚠️ **Gestes non câblés** : aucun chemin n'atteint encore l'écran (ni
    `OPTIONS`, ni l'appui long du nomade).
- **Partial refresh sur l'écran principal et le menu OPTIONS** (banc
  `-D UI_TEST_DASHBOARD`, pour comparer le coût d'un FULL ~1,3 s / flash à un
  PARTIAL ~856 ms / sans flash) :
  - `displayShowMainGauge()` — **jauge de capacité** seule (120 × 31 px,
    y 162 → 192), zone effacée puis redessinée ;
  - `displayShowMainBattery()` — **icône batterie** de l'en-tête seule
    (32 × 19 px, la fenêtre s'arrête avant le séparateur y = 20) ;
  - `displayShowOptionsSelection(from, to)` — **déplacement de la barre de
    sélection** du menu (fenêtre de 2 lignes, 120 px de large) : l'ancienne
    ligne perd sa barre et la nouvelle la gagne dans le même refresh.
  - Dessin **factorisé** (`levelPct`, `drawLevelGauge`, `drawMenuRow`,
    `kMenuItems`) pour que le full et le partial produisent exactement la même
    zone ; chaque helper retombe sur le full de son écran si `canPartial()` est
    faux. Zones alignées sur 8 px en x (adressage SSD1680). Le banc fait
    descendre la jauge et la batterie sur l'écran principal, puis parcourt les
    4 sélections du menu.
  - ⚠️ Ces zones partielles ne servent **qu'au banc** : en production le poids ne
    change qu'à la mesure (donc FULL de l'écran), et le menu n'est pas encore
    câblé.
- **Pointe vers le bas sur les bandeaux inversés** : les bandeaux ne sont plus de
  simples rectangles — un **triangle plein centré** (24 px de large, 8 px de
  profond) prolonge chaque bandeau vers le bas, vers le contenu qu'il désigne (le
  poids, le libellé « DERNIER POIDS CONNU », les infos réseau). Un seul helper
  `drawInvertedBanner(top, h, font, texte, text_y)` sert désormais **les deux**
  positions de bandeau de la charte : l'alerte sous l'état (`NIVEAU BAS`, pointe
  **56 → 63**) et le bandeau sous titre (`NON CONNECTÉ`, `WIFI DE RÉGLAGES`).
  Le bandeau de titre est remonté de 2 px (66 → 84, pointe **84 → 92**) pour
  laisser 2 px blancs avant le texte suivant sur l'écran le plus serré (mode
  nomade). Géométrie vérifiée hors matériel avant l'édition (rendu 1× avec les
  vraies polices), puis compilée.
- **Mode nomade : « DERNIER POIDS CONNU » passe SOUS le poids** (`kNomadeLabelY`
  = 172, au lieu de 100 au-dessus) : le libellé devient la contre-note du nombre
  plutôt qu'un message d'état, et la zone haute se libère. La pointe du bandeau
  « NON CONNECTÉ » pointe désormais directement sur le poids.
- **Portail : libellés `RÉSEAU` / `ADRESSE` agrandis** (`JbmXb4` → `JbmXb5`) :
  à 7 px de haut ils se lisaient trop clairs sur la dalle 1 bit. Les valeurs
  restant en `JbmXb5` (l'URL ne tient pas en `JbmXb6`), la hiérarchie se joue
  désormais sur la casse (libellés en capitales) et non sur la taille.
- **Maquettes `docs/UI/` regénérées** (`code.html` + `screen.png`, capture
  headless au viewport d'origine) : pointe sur les bandeaux `NIVEAU BAS`
  (ECRAN02), `NON CONNECTÉ` (ECRAN17) et `WIFI DE RÉGLAGES` (ECRAN04) ; libellé
  du mode nomade sous le poids ; libellés du portail agrandis. Les `▲` qui
  encadraient `NIVEAU BAS` sur ECRAN02 (absents du firmware) sont retirés au
  profit de la pointe centrée.
- **Nouvelle charte UI e-paper** (polices dédiées + grille commune), déployée
  écran par écran :
  - polices bitmap générées depuis les TTF de la charte (`include/fonts/`) :
    **JetBrains Mono ExtraBold** (`JbmXb4/5/6`) pour le texte technique,
    **Oswald Bold** (`OswaldBold10/30`) pour les titres et la valeur principale —
    plage `0x20–0xFF`, **accents rendus** (fini l'ASCII seul) ;
  - grille commune dans `src/display.cpp` : en-tête **date + batterie 3 segments**,
    zones fixes, zone secondaire réservée `[ … ]`, pied `● COURT` / `— LONG` ;
  - maquettes de référence dans `docs/UI/ECRANnn/` (`DESIGN.md` + `code.html` +
    `screen.png`) ;
  - écrans déjà migrés : **principal** (dashboard) et **OPTIONS** ; les écrans de
    remplissage/veille utilisent encore l'ancien style (migration en cours).
- **Écran du portail : « PORTAIL ACTIF »** (`displayShowConfigPortal`, maquette
  `docs/UI/ECRAN04`) — remplace l'ancien « MODE CONFIG » (logo WiFi) : en-tête
  date + batterie, titre, bandeau inversé **`WIFI DE RÉGLAGES`** (texte en JbmXb5 :
  la chaîne ferait 112 px en JbmXb6, plus large que le bandeau de 110 px), infos
  groupées alignées à gauche (réseau AP ouvert + adresse **`http://192.168.4.1`**,
  en JbmXb5 car 126 px en JbmXb6), arrêt automatique et pied `● COURT ARRETER`
  (une seule ligne). L'AP étant **ouvert**, aucun mot de passe n'est affiché.
- **Timeout du portail porté de 5 à 10 min** (`PORTAL_TIMEOUT_MIN`, `config.hpp`),
  valeur partagée avec l'écran (« ARRÊT AUTO : 10 MIN ») pour rester exact.
- **Écran « MODE NOMADE »** (boîtier retiré du dock, **remplace** l'ancien
  « Boitier retire ») : `displayShowDedock` → `displayShowNomade`, maquette
  `docs/UI/ECRAN17`. Boîtier hors base (0/4), aucune mesure possible : l'écran
  affiche le **dernier poids connu** sous un bandeau inversé **`NON CONNECTÉ`**
  et le libellé **`DERNIER POIDS CONNU`** (jamais une valeur « actuelle »), avec
  l'heure de la mesure (`MESURÉ À 18:12`) et son ancienneté (`IL Y A 2 H 34`)
  dans la zone secondaire sur **deux lignes**. Pied : **`rechercher`** (appui
  court, **câblé** — re-test immédiat du dock) / **`informations`** (appui long,
  hint dessiné — action à câbler).
- **État RTC étendu** : `last_measure_kg` / `last_measure_at` /
  `has_last_measure` mémorisent la dernière mesure valide (4/4), posée à chaque
  mesure (`recordMeasurement`, cycle planifié + FSM refill). L'horodatage n'est
  posé que si l'heure RTC est valide (`timeIsValid`). `STATE_MAGIC` → layout 3.
- Écran ajouté au banc de validation `-D UI_TEST_DASHBOARD` (boucle
  E01/E02/OPTIONS/`PORTAIL`/`NOMADE`).

### Écrans

- **Revenir des informations ramène AU MENU**, comme l'annonce l'écran. Le
  `return` sortait d'`appOptionsMenu()` et la carte allait dormir : le menu
  disparaissait, alors que le pied affichait `LONG RETOUR`.
- **Page 2 des informations : les deux gestes sont annoncés.** Elle n'affichait
  qu'un `COURT RETOUR`, alors que le court bascule les pages et que le long
  ramène au menu — le geste qui fonctionne n'était pas décrit, et le retour au
  menu n'était pas annoncé. Le libellé devient `PAGE PREC.` (d'ici, le court
  ramène à la page 1, pas à une page 3).
- **Maquette `ECRAN19` réparée** : son `<footer>` était un flex `row` au lieu de
  `flex-col`, si bien que les deux lignes de geste se plaçaient côte à côte et se
  chevauchaient (`PAGE SUIV.` par-dessus `LONG RETOUR`).

### Réglages

- **L'heure d'aspiration est réglable** (onglet Réglages, section 4), en NVS
  (`aspiration_min`, minutes depuis minuit). Les deux réveils ne sont plus des
  heures en dur mais les instants qui **encadrent** l'aspiration de **±30 min**.
  Le défaut 18h30 redonne exactement les réveils historiques de 18h00 et 19h00 :
  une carte déjà en service se comporte donc comme avant.
- Le passage de minuit est géré (aspiration 00h15 → réveils à 23h45 et 00h45), et
  les deux créneaux restent toujours distincts.
- L'heure est affichée dans l'onglet **ÉTAT** (aspiration, heures de mesure,
  prochain réveil) et dans la fiche « écran e-paper » — plus aucun « 18 h / 19 h »
  figé dans l'interface.
- Horaire invalide (`25:00`, `abc`) refusé à l'enregistrement, comme les autres
  champs.

### Comportement

- **Le menu `OPTIONS` est câblé et devient la porte d'entrée du produit.**
  L'arbitrage est tranché : **`— LONG` sur l'écran principal ouvre `OPTIONS`**,
  et le remplissage ne part plus que de l'entrée `REMPLISSAGE` du menu.
  - Raison : le pied de `MAIN` annonçait déjà `— LONG OPTIONS` (l'écart était une
    anomalie, pas un choix), et le parcours est désormais 5 écrans + un maintien
    de 2,5 s — un appui long unique ne doit plus enchaîner une saisie de prix puis
    un enregistrement. Conséquence assumée : le remplissage demande **deux
    gestes** au lieu d'un.
  - `appOptionsMenu()` : `● COURT` = ligne suivante (partial zoné, une ligne par
    clic), `— LONG` = valider l'entrée. Les 4 entrées sont câblées :
    `REMPLISSAGE`, `PORTAIL RÉGLAGES` (→ confirmation → portail),
    `INFORMATIONS` (→ les 2 pages), `FERMER`.
  - Filet de sécurité : 1 min sans geste → même effet que `FERMER` (non annoncé au
    pied : le pied annonce les gestes, pas l'absence d'attention).
- **Règles de réveil de l'écran principal** (elles étaient implicites) :
  `MAIN` **part toujours en deep sleep** (ce n'est pas une session interactive) ;
  au réveil, **appui court = mesure à la demande**, **appui long = menu**. Et
  `FERMER` / le timeout du menu reviennent à `MAIN` **sans remesure** — avec le
  dernier poids connu — puis deep sleep.
- **Parcours de remplissage réécrit** : le prix se saisit désormais **APRÈS** le
  versement (`poids → sacs → prix → validation`), pas avant. L'ancien ordre
  (`prix → versement`) remontait le prix d'un sac avant d'avoir versé, et
  obligait à garder le prix en mémoire pendant tout le versement.
  - Étape 1/3 « EN COURS » : le poids **sonde** le capteur toutes les 3 s et ne
    rafraîchit qu'au-delà de **0,5 kg** — le poids n'a aucun geste pour se
    rafraîchir (`● COURT` avance, le long est inerte), donc sans sondage l'écran
    « en direct » ne bougerait pas ; à l'inverse un partial par lecture
    (~856 ms) serait du gaspillage.
  - `● COURT SUIVANT` est désormais la **seule** action annoncée de l'étape 1, et
    son pied est réduit à une ligne : le long y est inerte, l'annoncer serait un
    mensonge.
  - Reprises après dédock redirigées : `BEFORE_MEASURE` → re-mesure « avant »
    puis étape 1 ; `AFTER_MEASURE` → re-mesure « après » puis `ECRAN14` (plus
    l'ancien écran de confirmation). Timeouts 2 h / 1 h inchangés.
- **`scale/refill/state` : nouveau vocabulaire** `pouring` → `pricing` →
  **`saved`**, et l'ancien `docked` **supprimé**. Il décrivait la phase du
  parcours alors que le silo est **hors de sa base** pendant tout le remplissage :
  annoncer « docked » était faux. L'état physique reste porté par
  `scale/quality.mode` (`docked` / `dedocked` / `pouring` / `pricing`) et reprend
  sa valeur réelle au cycle suivant.
- `ScaleMode::REFILL_ARMED` → **`REFILL_POURING`** (ordinal identique, donc
  `STATE_MAGIC` et le layout de `ScaleState` intacts) ; `stateModeName()` renvoie
  `"pouring"` — **⚠️ le champ `quality.mode` passe de `armed` à `pouring`**
  (visible côté HA).
- **`INFORMATIONS` câblé** : entrée du menu **et** appui long sur l'écran nomade.
  Les valeurs affichées sont réelles (session réseau ouverte pour le WiFi / le
  broker / le signal, une lecture par pied pour la répartition) — un « OK »
  inventé serait un mensonge. `wifiRSSI()` ajouté à l'API réseau.
- **Le portail de configuration n'a plus que DEUX entrées** :
  1. **au démarrage**, si le **réseau ou le broker MQTT** n'est pas enregistré en
     NVS. `settingsHasStored()` exige désormais **les deux** clés
     (`wifi_ssid` **et** `mqtt_server`) : tous les autres réglages — calibration
     des pieds, base des topics, identifiants — ont des **valeurs par défaut** et
     leur absence ne déclenche plus le portail. Une carte calibrée pour rien mais
     connectée est donc opérationnelle.
  2. **sur demande**, depuis `OPTIONS` → « PORTAIL RÉGLAGES », après la page de
     confirmation. Ce chemin est de nouveau **atteignable** : le menu est câblé
     (il ne l'était que sur le banc de test).
- **Suppression de l'accès « bouton maintenu au cold boot »** qui ouvrait le
  portail : c'était une troisième entrée, non annoncée à l'écran, qui levait un
  point d'accès **ouvert** sans le dire.

### Documentation

- **Nouveau guide utilisateur** `docs/GUIDE-UTILISATEUR.md` : ce qu'on voit sur
  l'écran, ce qu'on fait, ce qui se passe ensuite. Explication des deux gestes
  (court / long, et le maintien de 2,5 s), parcours de remplissage étape par
  étape, menu, informations, mode nomade, portail, ce que reçoit Home Assistant,
  et une section « cas rencontrés » (date fausse, poids inchangé, pieds
  déséquilibrés, tare à refaire). Le README ne décrivait les gestes qu'au milieu
  d'une section technique.
- **Revue de l'interface Home Assistant** après les changements :
  - le vocabulaire de `scale/refill/state` était faux dans les commentaires
    (`"docked"|"armed"|"pricing"` — `armed` n'a jamais existé) : c'est
    `pouring`/`pricing`/`saved` ;
  - `scale/error/on-off` ne distingue pas dédock et panne : c'est dit, et la
    cause est renvoyée vers `scale/quality` ;
  - `scale/value` est désormais documenté comme **net** (tare soustraite), avec la
    conséquence : une tare saisie dans le portail décale l'historique déjà cumulé ;
  - le seuil « niveau bas » (kg, écran, portail) et le seuil HA (jours) sont
    explicitement distingués — deux réglages qui ne se déclenchent pas l'un
    l'autre ;
  - l'heure d'aspiration n'étant pas publiée, la doc dit comment caler une
    aspiration pilotée par HA, et ce que la balance interprète sinon
    (conommation ou appoint).
  Aucun changement de comportement HA : uniquement de la documentation, plus
  `scale_poids` ajouté au message de l'alerte silo faible.

- **`docs/ecrans-transitions.md` → `docs/SPEC-ecrans.md`**, réécrit en
  **spécification normative** (DOIT / DEVRAIT / PEUT) : carte des transitions,
  entrées et sorties, garde-fous de la FSM de remplissage, portail et sa page de
  confirmation, politique de rafraîchissement full/partial, effets de bord
  (MQTT + RAM RTC) par transition, transitions sans redessin, table des pieds
  annoncés vs gestes câblés, récapitulatif écran → déclencheur, menu `OPTIONS`,
  reste à faire. Mise à jour pour le parcours à 5 écrans, les règles de réveil,
  le menu à 4 entrées et le vocabulaire MQTT.
- **`docs/UI/BASE-DESIGN.md`** : règle de la **légende de héros** (entre la pointe
  du bandeau et l'encre du héros, `JbmXb4` baseline 101) et rappel que le héros
  reste sans `+` ; distinction **barre d'avancement / barre de maintien** ;
  `drawFooterLongOnly()` ; budget de l'action du pied (77 px) ; pas de
  sous-titre d'étape.
- **`tools/preview_charte.py`** (nouveau) : rendu **hors matériel à l'échelle
  1:1** de chaque écran de la charte, en rejouant la géométrie de
  `src/display.cpp` (grille, composants, polices de `include/fonts/`) avec les
  primitives de `render_screens.py`. C'est l'outil qui a permis de valider, sans
  flasher, la légende `Ajout réel` (le jambage du « j » touchait le héros), le
  placement de `FERMER` dans le menu et les crochets du récap d'`ECRAN15` qui
  sortaient des marges.
- **Code mort restant éliminé** : les deux partials de l'écran principal
  (`displayShowMainGauge` / `displayShowMainBattery`) n'ont aucun appelant en
  production — le cycle fait toujours un full, et la veille nomade a son propre
  partial d'en-tête. Ils sont désormais sous `#ifdef UI_TEST_DASHBOARD`, donc
  absents du firmware livré (avec leurs constantes de zone). Également : une
  variable d'horloge morte dans `sensors.cpp`, et un message d'erreur du portail
  **tronqué** (74 caractères dans un buffer de 64 → le lien « Retour » était
  coupé ; buffer porté à 128). `src/` ne compile plus avec un seul avertissement.
- **Rendus obsolètes supprimés** : `tools/render_screens.py` ne génère plus les
  4 écrans de l'ancien parcours (`refilling`, `price`, `confirm`, `result`) —
  les captures correspondantes (`docs/screens/*.png`) sont supprimées ; le
  README pointe désormais vers les maquettes et `preview_charte.py`.
- **Maquettes** : les 12 ont été régénérées après la convention des pieds ;
  `ECRAN14`/`ECRAN15` (nouvelles, fin de parcours) et l'ancienne paire
  `ECRAN14`/`ECRAN15` (informations) devient **`ECRAN18`/`ECRAN19`** ; `ECRAN03`
  passe à 4 entrées ; `ECRAN11` à un pied d'une ligne ; le compteur `n/3` de
  `ECRAN11`/`12`/`13` était collé à la dernière case (réglé).

### À faire

- **Abandonné — vérifier l'échelle d'après les refills** (`sacs × 15 kg`) : les
  capteurs sont déjà calibrés, une calibration refaite sur banc suffit. L'ancien
  raisonnement reste juste en théorie (une erreur d'échelle touche chaque delta,
  contrairement au zéro qui s'annule) mais il ne justifie plus une vérification
  périodique ici.
- Aligner le topic MQTT `scale/quality` : `dedock` est publié pour une panne
  capteur, alors que seul l'écran distingue les deux cas.
- **Réveil au redock** : la ligne `DOUT` n'est pas encore câblée sur la balance
  de dev. Tant que ce n'est pas fait, une balance reposée sur sa base ne se
  réveille pas avant l'heure de mesure suivante (2 fois par jour).
- **Titres longs** : `INFORMATIONS` fait 121 px en `OswaldBold10` pour 122 px de
  dalle ; une coupe condensée d'Oswald est à générer.

## [0.9.2] - 2026-10-02

### Firmware

- **Réveil immédiat dedock/redock par la ligne DOUT d'un pied (sans
  microswitch)** — remplace le contact de présence jamais câblé
  (`USE_DOCK_SENSOR` / `DOCK_SENSE` GPIO 13 supprimés ; `USE_DOCK_WAKE_DOUT`
  ajouté) :
  - **Principe** : le DOUT du pied 1 (GPIO 25) est chargé par une **pull-down
    externe ~1 MΩ côté ESP**. Un HX711 alimenté et en power-down (SCK tenu haut)
    le pilote à HIGH (boîtier posé) ; pogo ouvert → ligne flottante → LOW
    (boîtier retiré).
  - **Réveil `ext0`** (LOW en DOCKED = dédock, HIGH en DEDOCKED = redock),
    **combiné** à `ext1` (bouton, GPIO 39) et au timer de secours. Correction
    d'un commentaire erroné : ext0 et ext1 sont **combinables** (doc ESP-IDF).
  - Au réveil, un **cycle de mesure re-classe l'état** (0/4 → DEDOCKED, 4/4 →
    DOCKED) et publie s'il y a changement ; le redock revient en DOCKED puis
    dort jusqu'au prochain créneau (18h/19h).
  - **Écran dédié « Boitier retire »** (`displayShowDedock`) quand le dédock est
    confirmé (ligne DOUT basse) ; l'écran « Capteurs KO »
    (`displayShowSensorsKo`) reste pour un 0/4 **sans** signal de dédock (panne
    capteur / câblage).
  - ⚠️ **Câblage requis** (pull-down 1 MΩ côté ESP) et **conso à mesurer au
    PPK2** (`ext0` garde le domaine RTC_PERIPH alimenté) ; à valider sur banc.

## [0.9.1] - 2026-10-02

### Boîtier

- **Boîtier v1 : vertical + base (dock)** dans `case/v1/` (projet PrusaSlicer
  `PelletScale.3mf` + un 3mf par pièce), remplace `case/prusa.3mf`. Base,
  connexion pogo, fixation du connecteur et ailettes : faits. Reste le
  contacteur de présence.

### Affichage

- **Écrans harmonisés + défauts corrigés** (portrait 122×250) :
  - **aides bouton identiques** sur les 3 écrans interactifs (prix, versement,
    confirmation) : deux lignes `court` / `long` en police fine, **colonnes
    alignées** (libellé à gauche, action à colonne fixe) — fini le mélange
    « clic: » / « court: » et les 2 vs 4 lignes selon l'écran ;
  - **virgule décimale** partout : la confirmation affichait `12.15 EUR` (point)
    alors que la saisie du prix affichait `4,05` (virgule) ;
  - **montant de la saisie centré** sur la largeur réelle du bloc (il partait
    ~6 px à gauche, alors que le « EUR » dessous était centré) ;
  - **marges rétablies** : les textes qui frôlaient les bords (`Prix du sac`,
    `clic:+1 sac`, `deconnectee` = 119 px sur 122) sont écourtés (« Prix sac » /
    « boitier hors base »), et l'écran texte du portail coupe à 10 caractères au
    lieu de 11 ;
  - détails : `CONFIG.` → `CONFIG`, `Envoye !` / `Annule !` symétriques.
  - **en-tête du principal allégé** : heure `HH:MM` + date `jj/mm` (année
    retirée) en **police fine** au lieu de gras, barre ramenée à 38 px.
  - **écran de configuration sur une ligne** : nom d'AP et adresse IP ne sont
    plus coupés. Le nom d'AP passe de `ESP-Scale-XXXX` (14 car., 151 px) à
    **`Scale-XXXX`** (10 car., 108 px) — `webconfig.cpp`, docs et écran alignés.
  - **batterie verticale vectorielle** : les 5 bitmaps horizontaux 32×28
    (`include/image.hpp`) sont remplacés par un dessin vectoriel (corps 17×32,
    **niveau continu** au lieu de 5 paliers, borne `+`, éclair en charge) ;
    `image.hpp` est supprimé. La zone de partial du dedock suit
    (`kDedockBattery*` : 96,0 / 24×40).
  ⚠️ Les **accents ne sont pas rendus** : les polices Adafruit embarquées ne
  couvrent que l'ASCII (`0x20–0x7E`, vérifié) — « Envoye ! » / « Annule ! »
  restent sans accent (il faudrait générer une police Latin-1 sur mesure).
  Aperçus régénérés (`docs/screens/*.png`) et `tools/render_screens.py` aligné
  sur `src/display.cpp`.

### Firmware

- **Affichage passé en portrait (122×250)** : rotation 0, tous les écrans
  re-mis en page (principal, dedock, remplissage, prix, confirmation, résultat,
  configuration, test), zones de partial recalées, `tools/render_screens.py`
  aligné. Textes raccourcis pour tenir dans 122 px (11 caractères en 9pt).
  ⚠️ **Non vérifié sur la dalle** : orientation (rotation 0 ou 2), débordements
  (textes de 11 caractères collés aux bords), partials — à vérifier et affiner.

- **T5 V2.4 mise en service — coupure d'alimentation écran activée**
  (`USE_EPD_PWR_CUTOFF = 1`, `FW_VERSION` → **0.9.1**). La V2.4 expose
  nativement **GPIO 12 = EPD_PWR_EN** (coupure de la LDO écran), absent de la
  V2.3.1 où l'idée avait été écartée (soudure sous l'écran collé).
  - **Polarité validée sur banc** : GPIO 12 **LOW = écran coupé**, **HIGH =
    écran alimenté** (~1 mA au PPK).
  - **Deep sleep mesuré : ~190 µA** — identique écran branché ou non, GPIO 12
    LOW ou laissé libre, avec l'exemple officiel comme avec un sketch minimal
    (aucun périphérique) : ce n'est **ni l'écran, ni le GPIO 12, ni le
    firmware**, c'est le **plancher de la carte**. Le « ~30 µA » du README
    LilyGO n'est pas reproductible sur cette révision (issue #66 : carte nue
    ~130 µA ; 30 µA seulement après modif matérielle de l'EN du LDO U6 et
    déplacement des pull-ups R4/R46/IO39).
  - `pins::EPD_PWR_EN` (`pins.hpp`) + `epdPowerCut()`/`epdPowerRestore()`
    (`power.cpp`/`power.hpp`) : coupe l'alimentation écran pendant le deep
    sleep (maintenue via le domaine RTC, même mécanisme que les PD_SCK HX711)
    et la restaure avant tout accès SPI.
  - Contrepartie assumée : la RAM de trame du SSD1680 ne survit plus au
    sommeil → `canPartial()` (`display.cpp`) force un FULL refresh au premier
    draw de chaque réveil quand le flag est actif, à la place du hibernate() +
    pull-ups 100 kΩ CS/RST (qui restent le mécanisme de veille sur V2.3.1).
- **Hostname réseau fixé à `pellet-scale`** (`WIFI_HOSTNAME`, `config.hpp`) :
  `WiFi.setHostname()` est appelé avant `mode(WIFI_STA)` dans `wifiConnect()` et
  avant `mode(WIFI_AP_STA)` dans le portail, sinon le core arduino-esp32 annonce
  son défaut `esp32-<3 derniers octets MAC>` au DHCP. L'identité est donc la même
  en cycle nominal et pendant le portail.
- **Horloge décalée (constaté : +45 min) — la synchro NTP n'était jamais
  effective** : `timeInit()` sortait immédiatement si l'heure RTC était
  « valide », or le RTC **survit au deep sleep** → le test était vrai à vie
  après la première synchro et plus rien ne se resynchronisait. L'attente
  reposait en plus sur `getLocalTime()`, qui rend vrai dès que la date est
  plausible (donc grâce au RTC) et ne prouve **pas** qu'une réponse NTP est
  arrivée — le log « NTP synced » s'affichait sans aucune synchro. `timeInit()`
  attend désormais `SNTP_SYNC_STATUS_COMPLETED` via `sntp_get_sync_status()`
  (mode `SNTP_SYNC_MODE_IMMED`), et il est appelé **AVANT l'affichage** de
  l'écran principal (qui porte l'horloge) : le cycle planifié et l'écran dégradé
  ouvrent la session réseau via `publishBegin()` (WiFi + NTP) avant le rendu,
  puis publient via `publishSend()` — le MQTT reste APRÈS l'affichage. Cause de
  fond de la dérive : `RTC_SLOW_CLK` = oscillateur **RC interne** (le module n'a
  pas de quartz 32,768 kHz), que le `kRtcDriftCompensationSec = 15` (pansement
  fixe) ne pouvait pas compenser. Détaillé dans le README
  (« Horloge décalée à l'écran »).

### Décisions

- **Pas de reprise d'un `refill/event` en échec** ni de trace d'annulation : si
  la publication échoue (ou si l'utilisateur annule), le cycle suivant voit la
  hausse ≥ 14 kg et émet un **refill sauvage** (`bag_count: 0`) — la compta reste
  juste, valorisée au prix moyen. Pas de retry dédié, pas de `state = cancelled`.
- **Pas de sauvegarde/buffer des envois** : une publication manquée est
  simplement republiée au cycle suivant (poids/batterie via `last_send_ok`),
  sans persistance supplémentaire.
- **Pas de compensation en température : décision RETIRÉE (analyse à refaire)** —
  le premier chiffrage était faux (raisonné sur 4 cellules au lieu de 16). Avec
  4 cellules de 50 kg **par pied** et 4 pieds, la dérive de zéro vaut
  ~0,05-0,4 kg/°C, soit **~0,5-2 kg pour 10 °C** : au niveau du seuil de
  publication de 1 kg. Dans un **atelier non isolé, avec la chaudière**, les
  amplitudes sont réelles. À mesurer avant de trancher.
- **Montage réel : silo fixe, jamais vide** — le silo ne quitte jamais sa base
  (remplissage par soufflerie) et n'est jamais vide (remplissage avant la fin) :
  les cellules restent **chargées en permanence** et le poids du silo fait partie
  du **zéro**, posé une fois pour toutes (aucune référence d'étalonnage
  atteignable). Le **« dedock » = retirer le boîtier de sa base** (pogo séparé,
  ex. recharge USB) → 0/4 HX711 → `DEDOCKED`. C'est un geste **réel et
  récurrent** ; comportement cible et contact de présence décrits dans le README.
- **Le silo est taré ENTIER** : le zéro est posé sur la somme des 4 pieds, avec
  le silo en place. Il n'y a donc **pas** de suivi par pied à exposer, ni de
  support multi-réservoirs : ces pistes sont écartées (le poids qui compte est la
  charge totale du silo, telle qu'elle est publiée).

### À faire

- **Capteurs — mesurer le plancher de bruit vs température** (décide du reste) :
  relever poids + température ambiante sur plusieurs jours (dont un cycle de
  chaudière). Si la dérive descendante franchit 1 kg → chaque excursion basse
  compte une (petite) conso fantôme ; le cumul est traité par le cliquet, mais le
  seuil reste à arbitrer.
- **Capteurs — vérifier l'échelle aux refills** : la hausse mesurée doit valoir
  `sacs × 15 kg`. C'est la seule référence d'étalonnage disponible (le zéro, lui,
  ne l'est pas) ; corriger `C` si biais constant (l'erreur d'échelle
  touche **chaque** delta, contrairement au zéro qui s'annule dans les deltas).
- **Réveil matériel immédiat au redock** : `ext0` sur le DOUT d'un pied (GPIO
  RTC 25/33/26) + pull-down externe ~1 MΩ — HIGH docké, LOW dédocké. À câbler
  et valider sur banc (sans lui, redock vu au poll suivant, ≤ 15 min).
  Alternative en réflexion : aimant + capteur magnétique.
- Réglages supplémentaires dans le portail (seuils batterie, horaires).

## [0.9.0] - 2026-09-14

### Firmware

- **Consommation par phase — mesures PPK2** (alimentation 4,0 V) : capteurs
  **1,152 s à 59,2 mA** (68,2 mC), **cycle complet** (réveil + WiFi/MQTT)
  **4,281 s à 71,1 mA** (304,5 mC), **cycle sans changement** (aucune
  publication, donc pas de WiFi) **2,549 s à 55,4 mA** (141,2 mC), deep sleep
  **231,4 µA** (5,196 s, max 251,5 µA). Les phases manquantes se **déduisent** du
  couple de cycles : **affichage ~1,40 s / ~52 mA / ~73 mC** et **WiFi/MQTT
  1,732 s / ~94 mA / 163,2 mC**. L'affichage déduit retrouve la mesure directe
  antérieure (1,190 s / 71,4 mC) — les trois phases sont donc cohérentes.
  → **Autonomie ≈ 5,7 mAh/jour, soit ~10 mois** sur batterie 2000 mAh (~1800 mAh
  utiles, arrêt à 3,3 V), à 2 réveils/jour (1 cycle publiant + 1 muet).
  ✅ La veille **avec les HX711 branchés** est retombée au plancher
  (**231 µA**, contre ~7-10 mA) — voir le point suivant.
- **Maintien du niveau haut de PD_SCK pendant le deep sleep** — correctif écrit
  pour la veille élevée **avec les HX711 branchés** (~7-10 mA, contre ~250 µA
  débranchés : le delta vient du chemin HX711). C'est le niveau haut de SCK qui
  garde un HX711 en power-down ; le domaine numérique s'éteignant au sommeil, il
  faut **tenir** le pad. Le simple verrou ne suffit **pas** (mesuré au
  multimètre : SCK 14 oscillant 2,2-3,3 V, DOUT 22 oscillant, HX711 rallumés) :
  `rtc_gpio_hold_en()` fige un état RTC qui ne reflète pas la sortie numérique.
  Les 3 SCK **RTC** (32/14/27) sont donc désormais **basculés sous le domaine
  RTC** (`rtc_gpio_init` + sortie haute + drive max) — le domaine RTC reste
  alimenté au sommeil, c'est lui qui pilote la ligne. Le seul SCK **numérique**
  (21) garde `gpio_hold_en()` + `gpio_deep_sleep_hold_en()`.
  `powerReleaseSensorHold()` (au début de `sensorsInit()` et dans
  `sensorsPowerOn()`) libère le maintien **et** rend les pads au chemin numérique
  avant de piloter SCK — sinon `power_up()` est un no-op silencieux (HX711
  éteints → faux dedock).
  ✅ **VALIDÉ sur banc** : les 4 SCK à **3,3 V franc** au sommeil (contrôlé hors
  dock, sans HX711 pour tirer la ligne) et veille **231,4 µA** **base branchée**
  — plancher retrouvé, **aucune résistance de pull-up nécessaire**. ✅ Cycle
  réveillé validé : écran principal **4/4** (et non le dedock) — le
  `rtc_gpio_deinit()` rend bien les pads au chemin numérique.
  → `docs/courant-veille-hx711-a-creuser.md`.

## [0.5.0-pre.1] - 2026-09-11

### Firmware

- **Calibration des pieds : tare des 4 pieds d'un coup, repère avant/après,
  total.** La page « Calibration » est adaptée au montage réel (silo **fixe**
  reposant sur les 4 pieds) :
  - le bouton **Tare** porte désormais sur les **4 pieds simultanément** (silo en
    place, rien d'ajouté) — on ne peut pas les tarer un par un ;
  - le calcul de `C` se fait sur une **différence avant/après** (bouton
    **Repère**), parce que le poids de test posé sur un pied influe légèrement sur
    les autres : `C = Δraw / poids` sur le pied concerné seulement ;
  - **le champ demande le poids réellement posé** (défaut **1,96 kg**, le poids
    de référence disponible ; **recommandé ~10 kg**, ex. un bidon de 10 L d'eau —
    l'erreur sur `C` diminue quand le poids augmente) ;
  - **le tableau compare avant/après sur `raw` ET `kg`** : chaque cellule montre
    la valeur **figée au repère** (gris) puis la valeur **actuelle** (gras), avec
    une colonne `Δ raw` et une ligne **TOTAL** traitée pareil. La ligne du pied le
    plus sollicité s'allume (on voit l'influence croisée) et l'état du repère est
    affiché (heure de prise, ou « non pris ») avec de quoi l'effacer ;
  - **bug corrigé** : cliquer « Repère » **avant** le premier relevé figeait des
    valeurs `NaN` et le Δ restait `-` définitivement ; `mark()` prend désormais une
    mesure fraîche avant de figer, et prévient si aucun pied n'est disponible ;
  - relevé serveur passé à 2 échantillons par pied (le Δ sautillait), sondage à 2 s ;
  - présentation retravaillée (cartes, tableau lisible sur mobile), et le lien vers
    la calibration est une vraie carte cliquable sur la page de configuration.

## [0.4.0] - 2026-09-11

### Firmware

- **Calibration des pieds dans le portail web (Z/C), sans recompiler.** Une page
  « Calibration » affiche `raw` + `kg` des 4 pieds (rafraîchis en continu : on
  appuie sur un pied, on voit l'avant/après), avec un bouton **Tare**
  (`Z := raw` courant) et un bouton **Calculer C** depuis un poids connu
  (`C = (raw − Z) / poids`). L'enregistrement va en **NVS** (clés propres,
  distinctes du WiFi/MQTT — enregistrer l'un ne touche pas l'autre) et s'applique
  **immédiatement**, sans redémarrage. La route de sauvegarde ne teste pas le
  WiFi : une calibration de banc doit marcher sans routeur. Les constantes
  compilées (`Z_FACTOR_x` / `C_FACTOR_x`) deviennent les **valeurs par défaut**.
  ⚠️ Modifier `Z` décale le poids publié (le cliquet compte les baisses comme de
  la conso) : opération de mise en service — l'avertissement est affiché sur la
  page.
- **Suppression du mode test matériel et du menu de démarrage.** Bouton GPIO 39
  maintenu au reset → **portail directement** (le menu n'aurait plus qu'une
  option). Le diagnostic CHRG embarqué disparaît avec lui ; l'état de charge
  reste affiché sur l'écran principal (icône batterie + éclair).
- **Filtre de plausibilité des capteurs resserré sur la capacité physique** :
  il acceptait par pied `[-TANK_FULL_KG, TANK_FULL_KG × 1.5]` = **[-670, +1005]
  kg** alors qu'un pied **sature à 200 kg** (4 cellules de 50 kg). Un pied
  surchargé plafonnait donc à une valeur qui franchissait les deux contrôles et
  était publiée comme une hausse légitime — donc un **faux « refill sauvage »**
  (≥ 14 kg) valorisé au prix moyen côté HA. Désormais borné par
  `FOOT_CAPACITY_KG` (200 kg/pied) et `TANK_CAPACITY_KG` (800 kg au total,
  somme des 4 pieds). Une lecture impossible est **rejetée** : le pied devient
  invalide, la mesure 4/4 échoue et **rien n'est publié** (une absence vaut mieux
  qu'une valeur fausse, qui créerait une fausse conso). Les deux voies de lecture
  sont alignées (`sensorsReadTotalWeight` et `sensorsReadFoot`, cette dernière
  servant aussi à la calibration dans le portail).

## [0.3.0] - 2026-09-11

### Firmware

- **Cliquet de publication « gel au mini »** : le palier publié (RAM RTC) ne
  remonte **jamais** sur une hausse < 1 sac (14 kg) — cette hausse n'est **pas
  publiée** et le palier reste gelé à son minimum ; heartbeat, batterie et retry
  republient le palier **inchangé** (et non le poids courant). Une dérive
  réversible du zéro (température, fluage) ne s'accumule donc plus en fausse
  conso : HA ne voit que des **baisses** (conso) et des **refills**. Une hausse
  ≥ 14 kg (refill sauvage) reste le seul cas de remontée du palier.
- **Refill déclaré : le poids du silo est republié** (`scale/value`, retain) en
  même temps que `refill/event`, et le palier RTC est relevé. Avant, seul
  l'event partait : le cycle suivant revoyait la hausse ≥ 14 kg, la publiait et
  émettait un **second** event (`bag_count: 0`) → valeur € comptée **deux fois**
  côté HA. Le stock kg est désormais à jour dès la confirmation.
- **Écran DEDOCKED reformulé** : « Capteurs KO / base deconnectee / ou panne
  capteur ». L'écran est **sur le boîtier** : l'ancien « En veille / en attente
  du reservoir » laissait croire à un silo mobile, et « hors ligne » était faux
  (le 0/4 = base déconnectée ou panne capteur, pas une perte de réseau).

### Affichage : partial refresh et un seul full refresh par rendu

- **`display.init(..., initial=false, ...)`** : l'init ne déclenche plus le
  `clearScreen()` interne de GxEPD2. Avec `initial=true`, chaque affichage
  enchaînait **deux** full refresh — le clearScreen (blanc) puis le rendu du
  contenu. Le rendu écrit la totalité de la RAM de trame avant de rafraîchir,
  donc l'écran reste entièrement défini et le temps d'affichage est divisé par
  ~2 (2,7 s → 1,36 s mesurés).
- **`EpdPanel` (`include/epd_panel.hpp`) : mise à jour partielle.**
  Le driver `GxEPD2_213_GDEY0213B74` (full refresh rapide) n'implémente pas de
  LUT partielle → son « partial » exécutait le waveform OTP plein écran (flash
  global). `EpdPanel` en dérive et n'emprunte au `GxEPD2_213_BN` (driver de la
  dalle DEPG0213BN, même contrôleur SSD1680) que la séquence partielle : LUT
  `0x32` + `_PowerOn` + `0x22 = 0xcc`. Mesuré sur la carte : partiel **~856 ms**
  (vs ~1,36 s en full), **sans flash**. Rien n'est patché dans `.pio/libdeps`
  (non versionné).
- **Fantôme dès la 2e mise à jour — corrigé** : le driver GDEY n'écrit que le
  buffer *current* (`0x24`) dans `writeImageAgain`/`writeImagePartAgain`, alors
  que la mise à jour SSD1680 est **différentielle** (current vs *previous*
  `0x26`) → le *previous* restait l'image d'avant et le différentiel était faux.
  `EpdPanel` surcharge ces deux méthodes pour écrire les **deux** buffers, comme
  le `213_BN`. (C'est aussi ce qui faisait flasher le partiel natif du GDEY.)
- **Partial refresh intégré à l'UI** (cycle de vie de la dalle
  `displayPanelWake/Off/DeepSleep`, `hibernate()` appelé une seule fois dans
  `goToSleep` pour préserver la RAM de trame, cache d'affichage en RAM RTC,
  anti-ghosting `kFullRefreshEveryN`, repli automatique en full si l'écran a
  changé) :
  - **écran principal** : **full** (affichage de fond net et prévisible) ;
  - **dedock** : seule l'**icône batterie** est rafraîchie (partial zoné) quand
    le % ou la charge change ;
  - **remplissage** : **partial zoné** à chaque geste (montant du prix, compteur
    de sacs), **full** au changement de digit et **tous les 10 sacs**.
- **Aucun clic perdu pendant les refresh** : les appuis du bouton sont captés
  par une **ISR de timer matériel (1 kHz)** — et non par la boucle principale,
  qui est bloquée pendant le BUSY de la dalle (~856 ms), les connexions
  WiFi/MQTT et les mesures. Les clics d'une rafale sont rejoués un par un (un
  refresh chacun), l'écran « court après » sans en perdre aucun.
- **Dedock : poll batterie toutes les 15 min + partial zoné.** Le dedock dort
  désormais **15 min** au lieu de 24 h (`kDedockPollSec`, le boîtier est alors
  sur USB pour recharge) et, à chaque réveil, relit batterie + état de charge et
  ne rafraîchit que l'**icône batterie** (partial zoné) si l'état a changé —
  sans WiFi. L'erreur n'est annoncée qu'une fois (mode RTC `announced_error`),
  car le mode est remis à `DOCKED` en début de cycle pour re-tester les capteurs.

## [0.2.0] - 2026-09-11

### Portail de configuration web (WiFi + MQTT)

- **Configuration sans recompiler** : SSID/mot de passe WiFi et serveur/port/base
  des topics MQTT se saisissent dans un **portail captif** embarqué
  (AP `ESP-Scale-XXXX` + `WebServer` + `DNSServer`, aucune lib externe), stocké en
  **NVS** (`settings.hpp`). La connexion WiFi est **testée avant écriture** : une
  config invalide n'est pas enregistrée (pas de verrouillage dehors).
- **Liste des réseaux WiFi détectés** : le portail scanne à son démarrage et
  propose les SSID dans une liste déroulante (avec niveau de signal et « ouvert ») —
  le nom du réseau n'est plus à retaper, seul le mot de passe reste à saisir.
- **Authentification MQTT optionnelle** : case à cocher qui révèle *utilisateur* /
  *mot de passe* (stockés en NVS comme le reste). Décochée, ou utilisateur vide →
  broker **sans authentification** (défaut) : rien n'est envoyé. Côté firmware,
  `mqttConnect()` n'utilise `connect(id, user, pass)` que si un utilisateur est
  configuré.
- **Bouton « Tester WiFi + MQTT »** : teste les valeurs **saisies** (pas encore
  enregistrées) — WiFi puis MQTT — et réaffiche le formulaire prérempli avec le
  verdict (WiFi OK/échec, MQTT connecté/échec, IP obtenue), pour corriger sans
  devoir enregistrer puis attendre un cycle. **Indication visuelle pendant le
  test** : l'e-paper affiche l'étape en cours (`TEST EN COURS` + `WiFi`/`MQTT`,
  détail tronqué à la largeur utile de la police) puis revient à l'écran du
  portail, et le bouton du navigateur passe à « Test en cours... ».
- **Serveur MQTT : champ tolérant au copier-coller d'URL** — un serveur saisi
  sous forme d'URL (`http://192.168.1.2`, `mqtt://broker:1883`) était passé tel
  quel à PubSubClient, qui tentait alors de **résoudre « http://192.168.1.2 » en
  DNS** → connexion MQTT impossible. `settingsNormalize()` retire désormais le
  schéma et le chemin, et récupère le port s'il est collé au serveur. Appliqué au
  **chargement** (les valeurs déjà enregistrées sont réparées sans ressaisie), à
  l'**enregistrement**, et par le portail (bouton de test).
- **Aucune configuration réseau compilée** : `include/secrets.hpp` (et son
  template) supprimés, ainsi que `MQTT_SERVER`, `MQTT_TOPIC_BASE` et
  `WIFI_SSID`/`WIFI_PASSWORD`. Le firmware se compile **sans aucun fichier de
  credentials**. Deux **défauts de formulaire** restent, non propres au site et
  modifiables dans le portail : `MQTT_PORT = 1883` (port standard du protocole)
  et `MQTT_TOPIC_BASE_DEFAULT = "scale"` (base des topics). Les topics MQTT sont
  construits **à l'exécution** (`<base>/value`, …) au lieu de macros concaténées.
- **Démarrage** : sans configuration enregistrée, la carte entre **directement
  dans le portail** (elle n'aurait aucun réseau où publier) ; sinon cycle de
  mesure normal. Dans tous les cas, **bouton GPIO 39 maintenu au reset** → menu
  sur l'e-paper (appui court = test matériel, appui long = portail).
- **Écran du mode configuration** : logo WiFi vectoriel (3 arcs concentriques +
  point, tracés arc par arc faute de primitive dans GxEPD2), **nom du réseau AP**
  en dessous et adresse de la page. Aperçu généré (`docs/screens/config.png`).
- Le portail **coupe après 5 min** d'inactivité (il garde l'ESP éveillé en AP) ;
  sans configuration enregistrée, il redémarre en portail, l'AP reste donc
  disponible jusqu'à la saisie.

## [0.1.0] - 2026-09-10

Première version taguée, version publiée sur MQTT (`scale/version`).

### Firmware

- **Base** : registre GPIO unique (`pins.hpp`), état RTC typé (`state.hpp`),
  ordonnanceur de réveils (`scheduler.hpp`), dispatch par cause de réveil
  (`app.cpp`).
- **Capteurs HX711** : lecture des 4 pieds, **strict 4/4** (aucune extrapolation —
  un poids faux est pire qu'une absence), distinction dedock (0/4) / dégradé
  (1-3/4), publication de la qualité de mesure.
- **Cliquet descendant de publication** : le poids publié ne fait que descendre
  entre deux refills. Baisse ≥ 1 kg → publiée (consommation). Hausse < 14 kg
  (~1 sac) → **non publiée** (bruit présumé, le palier RTC reste gelé → le stock
  HA n'est jamais artificiellement gonflé). Hausse ≥ 14 kg hors FSM → **refill
  sauvage** : poids publié + event `scale/refill/event` (`bag_count: 0`).
- **Mode remplissage au bouton GPIO 39** : FSM **prix d'abord** (saisie x,xx € du
  sac) → **versement** (1 clic = +1 sac, compteur xN ; appui long = finir) →
  mesure « avant/après » → **confirmation** (court = publier, long = annuler).
  Coût total = nombre de sacs × prix du sac ; le poids mesuré alimente le stock.
- **Appui court sur l'écran principal** : mesure à la demande (cycle complet,
  publication MQTT seulement si changement).
- **Garde-fous FSM** : sommeil « bouton seul » pendant un refill (pas de timer
  qui écraserait la FSM), reprise d'un refill interrompu par un dedock
  (`refill_pending`), timeouts d'abandon, fenêtre d'inactivité de 10 min
  (`kRefillSessionTimeoutMs`) — plus de clic fantôme perdu entre deux gestes.
- **5 écrans e-paper vectoriels** (silo ÖkoFEN stylisé) : principal (silo-jauge +
  % centré dans la cuve + poids + date/batterie), remplissage, saisie du prix,
  confirmation puis résultat, dedock (silo endormi). Textes ASCII uniquement.
- **Indicateur de charge** : batterie vide + éclair vectoriel quand `isCharging()`
  (fil CHRG TP4054 → GPIO 19).
- **Mode test matériel** : au démarrage (reset / power-on), bouton GPIO 39
  **maintenu** → diagnostic autonome au bouton + e-paper (CHRG, 4 pieds au repos
  puis sous appui, résumé), sans PC ni console.
- **Cadence de mesure 18h/19h** : la mesure de 18h (juste avant l'aspiration de
  18h30) isole les mouvements de la journée (remplissages) du creux nocturne et
  sert de filet de sécurité contre le masquage d'une conso ; 19h capture la conso
  de la nuit.
- **Protection batterie basse tension** : cycle sauté sous le seuil d'arrêt,
  sommeil prolongé en mode urgence (le T5 n'a pas de déconnexion matérielle).
- **Version** : `FW_VERSION` publiée sur le topic retain `scale/version` à chaque
  connexion MQTT (le broker reflète toujours la version flashée).

### Modifs matérielles

- **Pull-ups 100 kΩ CS/RST → 3V3** (pins 10 et 12 du FPC) : maintiennent le
  SSD1680 en hibernation pendant le deep sleep (GPIO 5/16 non-RTC flottants sinon).
  **Deep sleep mesuré : 750 µA → 250 µA.**
- **Fil CHRG (TP4054, U11 pin 1) → GPIO 19** : détection de charge USB fiable.

### Home Assistant

- Packages `ha/` : capteurs MQTT, architecture **« poids = référence »** (le poids
  publié EST le stock — pas de stock comptable qui dérive), automatisations
  conso/refill/alerte, prix moyen pondéré, compteurs daily/monthly/yearly (kg et €),
  journal des remplissages, dashboard « Granulés » (vue sections), script `sync.sh`
  de déploiement vers l'instance HA (backup + `check_config` + restart).

### Docs & outillage

- `README` : structure, configuration, protocole MQTT, câblage, écrans, matériel
  (contexte + modifs appliquées), comportement dedock cible, calibration.
- `tools/render_screens.py` : rendu des écrans hors matériel (parse les polices
  Adafruit + `image.hpp`, rejoue les primitives GxEPD2) → `docs/screens/*.png`,
  embarqués dans le README. Régénérable après toute modif de `display.cpp`.

