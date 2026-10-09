---
title: Spécification des écrans
nav_order: 10
---

# SPEC — Écrans e-paper et transitions

**Statut** : spécification de l'interface embarquée (dalle 122 × 250, 1 bit).
**Périmètre** : quels écrans existent, ce qui les déclenche, quels gestes ils
acceptent, comment ils se rafraîchissent. **Sources de vérité** : `src/app.cpp`
(déclencheurs, FSM de remplissage, menu `OPTIONS`, confirmation du portail),
`src/display.cpp` (rendu), `src/scheduler.cpp` (sommeils), `src/power.cpp`
(reveils armés), `src/webconfig.cpp` (portail), `src/settings.cpp` (droit d'accès
au portail), `enum DisplayScreen` (`include/state.hpp`).

**Aucun écran n'est plus rendu dans l'ancien style** (polices Adafruit
FreeMono, silo ÖkoFEN vectoriel) : tout passe par les composants de la charte
(`docs/UI/BASE-DESIGN.md`). L'écran « capteurs KO » et l'écran texte « TEST »
ont été supprimés.

**Registre** : les énoncés en **DOIT** sont des règles ; **DEVRAIT** une
recommandation admettant une exception ; **PEUT** une option. Chaque énoncé
non normatif décrit le firmware **tel qu'il est** — ce qui n'est pas câblé est
signalé 🔜, ce qui est câblé autrement que l'écran ne l'annonce est signalé ⚠️.

**Charte UI** : ✅ migré (`docs/UI/ECRANnn`) · 🎨 ancien style · 🔜 à faire.
**Charte de rendu** : `docs/UI/BASE-DESIGN.md` (grille, polices, composants).

| Écran (`DisplayScreen`) | Rendu | Charte |
|---|---|---|
| `MAIN` | principal (poids + jauge) ; variante `CAPTEUR(S) HS` (dernier poids stable) | ✅ `ECRAN01`/`02` |
| `DEDOCK` | MODE NOMADE (dernier poids connu) | ✅ `ECRAN17` |
| `OPTIONS` | menu 4 entrées (dont `FERMER`) — **3 entrées hors base** | ✅ `ECRAN03` / `ECRAN22` |
| `INFORMATIONS` | 2 pages : état général / répartition | ✅ `ECRAN18`/`19` |
| `PORTAL_CONFIRM` | confirmation de démarrage du portail | ✅ `ECRAN07` |
| `CONFIG` | portail actif | ✅ `ECRAN04` |
| `UNCONFIGURED` | « CARTE EN VEILLE » — carte sans configuration, en veille | ✅ (pas de maquette) |
| `REFILL_LIVE` | parcours — étape 1/3, poids versé en direct | ✅ `ECRAN11` |
| `REFILL_BAGS` | parcours — étape 2/3, nombre de sacs | ✅ `ECRAN12` |
| `REFILL_PRICE` | parcours — étape 3/3, prix du sac par digits | ✅ `ECRAN13` |
| `REFILL_CONFIRM` | parcours — « TERMINER ? », le maintien enregistre | ✅ `ECRAN14` |
| `REFILL_FIX` | parcours — « CORRIGER ? » (court sur `REFILL_CONFIRM`) | ✅ (pas de maquette) |
| `REFILL_SAVED` | parcours — « ENREGISTRÉ » (récap, 1 min) | ✅ `ECRAN15` |

---

## 0. Carte des transitions

```
   ┌─────────┐  court : mesure à la demande      ┌───────────────┐
   │  MAIN   │──────────────────────────────────▶│ cycle de mesure│──▶ MAIN / DEDOCK /
   │ poids + │  long : ouvre le menu              │  (pub MQTT si Δ)│
   │ jauge   │──────────────┐                     └───────────────┘
   └────▲────┘              ▼                            ▲
        │            ┌───────────────┐                   │
        │            │   OPTIONS     │ 4 entrées :        │
        │            │ REMPLISSAGE   │                    │
        │            │ PORTAIL RÉGL. │──▶ PORTAL_CONFIRM ──maintien 2,5 s──▶ CONFIG
        │            │ INFORMATIONS  │──▶ INFORMATIONS p1 → p2              (redémarre)
        │            │ FERMER        │──▶ MAIN (dernier poids connu, sans remesure)
        │            └───────┬───────┘        puis deep sleep
        │                    │ court = ligne suivante (partial) · long = choisir
        │
        │   REMPLISSAGE (choix dans le menu) — le prix se saisit APRÈS le versement :
        │
        │   ┌─────────────────┐   ● court   ┌──────────────┐   long VALIDER  ┌──────────────┐
        └──▶│ REFILL_LIVE     │────────────▶│ REFILL_BAGS  │───────────────▶│ REFILL_PRICE │
            │ poids en direct │             │ ● +1 sac     │                │ ● +1 digit   │
            │ (sonde 3 s)     │             └──────────────┘                └──────┬───────┘
            └─────────────────┘                                                  │ mesuré « après »
                                                                                  ▼
                            ┌──────────────────────┐   maintien 2,5 s   ┌────────────────────┐
                            │ REFILL_CONFIRM       │───────────────────▶│ REFILL_SAVED       │
                            │ « TERMINER ? »       │◀───────────────────│ « ENREGISTRÉ »     │
                            │ barre de maintien     │                    │ récap, 1 min       │
                            └──┬───────────▲───────┘                    └─────────┬──────────┘
                     ● court   │           │ RETOUR / correction faite          │ ● court
                               ▼           │ (re-mesure « après »)              │ (ou long)
                            ┌──────────────┴───────┐                            ▼
                            │ REFILL_FIX           │ NOMBRE DE SACS ─▶ REFILL_BAGS (depuis 0)
                            │ « CORRIGER ? »       │ PRIX DU SAC ───▶ REFILL_PRICE
                            │ ● suivant · — choisir│ ANNULER ───────▶ abandon
                            └──────────────────────┘
                     tenue interrompue sur REFILL_CONFIRM : la barre se vide, rien d'autre
                     timeout 10 min (CONFIRM ou FIX) ─▶ abandon (DOCKED + cycle normal)
                     fin de REFILL_SAVED ─▶ MAIN (poids déjà mesuré) puis deep sleep

   Déplacement du boîtier (0/4) pendant le parcours :
     mesure « avant » faite ─▶ DEDOCK + refill_pending = BEFORE_MEASURE ─▶ (redock) ▶ REFILL_LIVE
     mesure « après » faite ─▶ DEDOCK + refill_pending = AFTER_MEASURE  ─▶ (redock) ▶ REFILL_CONFIRM
     (timeout 1 h sans redock → abandon, retour DOCKED)

   DEDOCK (nomade) : court = inerte (le redock se voit par DOUT ou au poll complet)
                     long  = OPTIONS

   Carte SANS configuration :
     démarrage / bouton ─▶ CONFIG ─(court, ou arrêt auto sans enregistrement)─▶
     UNCONFIGURED « CARTE EN VEILLE » + deep sleep 24 h (contrôle batterie seul,
     sans redessin) ; ● court (réveil) ─▶ CONFIG

   ------- hors cycle de publication -------
   CONFIG ──« Tester WiFi + MQTT »──▶ le verdict revient dans la PAGE WEB
           (l'e-paper n'affiche rien pendant le test, ~15-20 s)
```

## 1. Entrées — ce qui décide de l'écran au réveil

**Règle** — un réveil MUST produire exactement un écran, choisi par la cause de
réveil puis par le mode courant. `appRun()` (`src/app.cpp`) :

```
appRun()
 ├─ réseau OU broker non enregistré :
 │    ├─ TIMER ───────────────────────────────────▶ veille (UNCONFIGURED déjà affiché, rien n'est redessiné)
 │    └─ autre réveil ────────────────────────────▶ CONFIG (portail)
 │           └─ sortie sans enregistrement ─────────▶ UNCONFIGURED « CARTE EN VEILLE » + veille 24 h
 ├─ TIMER (créneaux ±30 min autour de l'aspiration, nomade 00:02 / 5 min en charge) ┐
 ├─ COLD_BOOT (reset / power-on) ─────────────────┼──▶ appRunScheduledCycle()
 ├─ GPIO ext0 (ligne DOUT = dédock/redock) ────────┘
 └─ GPIO bouton ───────────────────────────────────▶ appHandleExternalWake()
```

**Règle** — le portail de configuration **DOIT** être atteignable par **deux et
seules** entrées :
1. **au démarrage**, si le réseau **ou** le broker MQTT n'est pas enregistré en
   NVS (`settingsHasStored()` — les deux clés sont exigées). Tout autre réglage
   absent (calibration des pieds, base des topics, identifiants) **DOIT** rester
   à sa valeur par défaut et **NE DOIT PAS** déclencher le portail : une carte
   calibrée pour rien mais connectée est opérationnelle ;
2. **sur demande**, depuis `OPTIONS` → « PORTAIL RÉGLAGES », après la page de
   confirmation (cf. §5).

Le bouton GPIO 39 maintenu au cold boot **NE DOIT PAS** ouvrir le portail : cet
accès était une troisième entrée, non annoncée à l'écran, qui levait un point
d'accès sans le dire.

**Règle** — `MAIN` **NE DOIT PAS** être une session interactive : l'écran
principal **DOIT** toujours finir en deep sleep. Un réveil au bouton donne donc
exactement deux lectures, et **elles sont celles que le pied annonce** :

| Appui au réveil | Effet |
|---|---|
| **court** | mesure à la demande → `appRunScheduledCycle()` (mesure + `MAIN` + publication si Δ) puis sommeil |
| **long** (≥ 1 s) | ouvre `OPTIONS` (`appOptionsMenu`) |

**Règle** — le lancement d'un remplissage **NE DOIT PAS** être attaché à un appui
long sur `MAIN` : il part de l'entrée `REMPLISSAGE` du menu. Un geste unique ne
doit pas enchaîner une saisie de prix puis un enregistrement ; le menu rend chaque
étape explicite et réversible.

`appHandleExternalWake()` selon le **mode courant** (`ScaleMode`) :

| Mode | Appui **court** (●) | Appui **long** (—) |
|---|---|---|
| `DOCKED` / `DEGRADED` | mesure à la demande → `appRunScheduledCycle` | `OPTIONS` (`appOptionsMenu`) |
| `REFILL_POURING` | reprise : on rejoue la phase « versement » depuis le début (`refillPourAndSave`) — c'est l'endroit où l'utilisateur se trouve | idem |
| `REFILL_PRICING` | reprise de la saisie du prix (`refillStepPrice` → mesure « après » → E14) | idem |
| `DEDOCKED` | — (inerte : le redock se voit par la ligne DOUT ou au poll complet) | **`OPTIONS`** : le menu |

La reprise par le bouton n'existe que par sécurité (reset / watchdog au milieu du
parcours) : la granularité de reprise est le **mode**, donc `REFILL_POURING`
reprend au début du versement (étapes 1 et 2 partagent ce mode) et
`REFILL_PRICING` reprend exactement sur la saisie du prix (chiffres et nombre de
sacs sont en RAM RTC).

**Règle** — `DEDOCKED` couvre les deux cas 0/4 (dédock confirmé *et* panne
capteur) : les deux affichent l'écran nomade. Une panne partielle (1-3/4) est `DEGRADED` →
`MAIN` en variante `CAPTEUR(S) HS`.

---

## 2. Sorties — sommeil et réveils armés

**Règle** — toute session interactive **DOIT** se terminer par `goToSleep()`,
qui arme le(s) réveil(s) correspondant(s) au mode courant. `goToSleepFor(sec)`
est la même chose avec une durée imposée.

**Règle** — en veille nomade, le redock réveille la carte par la ligne DOUT :
le timer ne sert qu'à tenir l'écran à jour. Hors charge, elle dort jusqu'à
**00:02** (ou le quart de batterie estimé, `kNomadeQuarterSec` ≈ 94 j, s'il
tombe avant) ; **en charge**, toutes les **5 min** (`kNomadeChargePollSec`) ;
horloge invalide, toutes les heures. Tant que la date affichée est celle du
jour, un réveil est un poll **léger** (la charge seule) ; quand elle a changé
(réveil de 00:02), c'est le poll **complet** (capteurs, garde batterie, synchro
NTP si > 24 h, écran redessiné). Un réveil qui **DOIT** mesurer — redock,
démarrage — **NE DOIT PAS** être traité comme un poll léger
(`appRunScheduledCycle(force_full)`).

| Mode | Sommeil | Réveil(s) armé(s) | Timer |
|---|---|---|---|
| `DOCKED` | prochain créneau = **aspiration ±30 min** (aspiration réglable dans le portail, défaut 18h30 → 18h00 et 19h00 ; +15 s de compensation de dérive RTC ; 12 h si l'heure RTC est invalide) | ext1 bouton + **ext0 DOUT sur LOW** (dédock) | oui |
| `DEGRADED` | idem `DOCKED` | ext1 bouton **seul** | oui |
| `DEDOCKED` — **poll léger** | en charge **5 min**, sinon jusqu'à **00:02** — CHARGE seule (ADC + partial de la zone d'en-tête si les segments ou l'éclair ont changé). Ni HX711, ni publication. Aussi sur appui court | ext1 bouton + **ULP** (`USE_CHRG_ULP`) : DOUT haute = redock, CHRG ≠ éclair affiché pendant 1 s = charge (sans ULP : ext0 DOUT sur HIGH) | oui |
| `DEDOCKED` — **poll complet** | le réveil de **00:02** (date changée) — capteurs, garde batterie, NTP si > 24 h, écran redessiné | idem | oui |
| carte **non configurée** | **24 h** (`kUnconfiguredSleepSec`, 1 semaine sous 3,3 V) — contrôle batterie seul. Écran `UNCONFIGURED` dessiné **une fois** à l'entrée en veille (`appUnconfiguredSleep`), jamais redessiné par les réveils timer | ext1 bouton (→ portail) | oui |
| `REFILL_POURING` / `REFILL_PRICING` | — : le parcours se fait carte **éveillée**, il ne dort jamais dans ce mode | — | — |

Contraintes :
- `DEGRADED` **NE DOIT PAS** armer ext0 : un pied HS peut tenir la ligne DOUT
  basse → réveil en boucle garanti. Le dédock sera vu au cycle suivant.
- Le timer de secours **DOIT** toujours être armé : une carte qui ne dort que sur
  le bouton peut rester muette indéfiniment.
- ext0 **DOIT** être armé sur le niveau **opposé** à l'état courant, sinon le
  niveau déjà actif provoquerait un réveil immédiat en boucle.
- En veille nomade, l'ULP **remplace** ext0 (l'IDF refuse les deux ensemble) et
  attend pour CHRG l'**éclair affiché** : un écart au moment du sommeil réveille
  la carte, qui se corrige. Hors nomade, l'ULP **DOIT** être arrêté (il l'est à
  chaque réveil, `disarmNomadeUlpWakeup()`).
- Urgences batterie, mesurées au réveil avant toute mesure : `< 3,3 V` → cycle
  **sauté** (aucun écran, l'image précédente reste), sommeil = prochain
  créneau ; `< 3,2 V` → **24 h** ; 3ᵉ réveil consécutif en urgence → **1 semaine**
  (`kCriticalSleepSec`).

---

## 3. Cycle planifié — `appRunScheduledCycle()`

**Règles**, dans cet ordre :

1. **Remplissage interrompu** — trouver le mode `REFILL_POURING` /
   `REFILL_PRICING` au réveil veut dire qu'un reset (watchdog, chute de tension)
   a coupé le parcours, puisqu'il ne dort jamais dans ce mode. Le cycle **DOIT**
   le traiter en **abandon** (`refillAbandon()`) : rien n'est publié par le
   parcours, retour `DOCKED`, cycle normal (refill sauvage si versement).
2. **Batterie** — sous `BATTERY_SHUTDOWN_THRESHOLD_V` (3,3 V) le cycle **DOIT**
   être sauté **sans redessiner d'écran** ; l'image précédente reste.
3. **Mesure 4/4** — l'écran est **DOIT** être :
   - `DEDOCK` (MODE NOMADE) si 0/4 : « 0 capteur » veut dire « hors base » ;
   - `MAIN` avec `-1` si 1-3/4 (dégradé) : bandeau `CAPTEUR(S) HS` (gabarit du
     niveau bas, le bandeau HS prime sur `NIVEAU BAS`), état `● KO`, **dernier
     poids stable** (`last_measure_kg`, RAM RTC) avec sa jauge, daté en zone
     secondaire (`DERNIÈRE MESURE` / `STABLE À hh:mm` si du jour, sinon
     `STABLE LE jj/mm`, `STABLE (SANS HEURE)` sans horodatage) — jamais une
     mesure actuelle ; sans aucune mesure : héros `--` et `AUCUNE MESURE` ;
   - `MAIN` (poids + jauge) si la mesure est valide, zone secondaire
     `MESURÉ À hh:mm` (`MESURE STABLE` sans heure valide). Si poids arrondi,
     jauge, bandeau, batterie et éclair sont ceux déjà affichés, l'écran **NE
     DOIT PAS** faire de full : partial plein cadre (date de l'en-tête et heure de
     mesure), ou rien si elles n'ont pas changé non plus. Un retour d'erreur
     annoncée (`announced_error != DOCKED`, ex. redock) **DOIT** publier l'état
     (`error=off`, `quality`) même si le poids n'a pas bougé.
4. **Reprise** — un `refill_pending` (refill interrompu par un dédock) **DOIT**
   reprendre au bon endroit : `BEFORE_MEASURE` → re-mesure « avant » puis
   `REFILL_LIVE` ; `AFTER_MEASURE` → re-mesure « après » puis `REFILL_CONFIRM`
   (rien n'ayant encore été publié). Au-delà de **1 h** sans redock, le flag
   **DOIT** être effacé (abandon propre).
5. **Deep sleep** — durée et réveils selon §2.

**Règle** — l'en-tête **DOIT** porter une date **valide** sur tous les écrans. Le
RTC interne dérive de plusieurs % (oscillateur RC, pas de quartz 32 kHz) et il est
**perdu à chaque coupure d'alimentation** : sans synchro NTP on afficherait
`--/--/--` (arrêt à froid) ou une date fausse de plusieurs dizaines de minutes par
jour. L'heure **DOIT** donc être récupérée sur TOUS les chemins qui dessinent un
écran, y compris ceux qui n'ouvrent pas de session réseau pour publier :

| Chemin | Récupération de l'horloge |
|---|---|
| session réseau déjà ouverte (publication) | `publishBegin()` → `timeInit()` (inchangé) |
| **veille nomade** (transition) | `appSyncClockIfStale()` avant de dessiner l'écran nomade |
| **veille nomade** (poll léger, toutes les heures si l'heure est invalide) | réessai si l'heure est encore invalide, puis **redessin** de l'écran |
| **veille nomade** (poll complet de 00:02) | `appSyncClockIfStale()` (recale la dérive RTC), puis **redessin** : nouvelle date |
| cycle **sans** publication (palier gelé) | `appSyncClockIfStale()` avant `displayShowMain` |

`appSyncClockIfStale()` n'ouvre une session **que** si l'heure est invalide ou
vieille de plus de **24 h** (drapeau `g_ntp_last_sync` en RAM RTC) : le cas
courant ne coûte donc aucun réveil réseau, y compris sur les polls de charge.

**Règle** — l'en-tête **DOIT** montrer l'état de **charge** (éclair à gauche du
bloc batterie) sur **tous** les écrans : c'est le seul signe qui distingue « sur
USB » d'une simple batterie descendante, et l'en-tête est le seul composant
partagé par tous les écrans. Tout ce que l'en-tête dessine à droite du `x = 72`
**DOIT** tenir dans la fenêtre de partial 48 × 19 (éclair 78..87, batterie
92..115) : c'est la seule zone d'en-tête qui bouge, partagée par `MAIN` et par
l'écran nomade. Maquette de référence : `ECRAN21` (variante en charge de
l'écran principal), rendu pixel dans `docs/screens/main_charge.png` (`tools/preview_charte.py --docs`).

**Règle** — un état d'erreur **déjà annoncé** (`announced_error`) **NE DOIT PAS**
redessiner l'écran : en `DEDOCKED` le cycle **PEUT** ne rafraîchir que l'icône
batterie (`displayShowNomadeBattery`, partial zoné) si le % ou la charge a
changé ; en `DEGRADED` il **NE DOIT** rien redessiner du tout.

---

## 4. Parcours de remplissage (6 écrans)

```
   OPTIONS ── REMPLISSAGE ──▶ refillStart
        ├─ garde batterie (3,3 V) ─────────────────────────▶ abandon
        └─ refillPourAndSave()
             ├─ mesure « avant »  ─▶ REFILL_LIVE   (sonde 3 s, partial si ≥ 0,5 kg)
             │      dédock ─▶ DEDOCK + refill_pending = BEFORE_MEASURE (reprise au redock)
             │      ● court ─▶ REFILL_BAGS
             │      long ─ inerte (rien à faire d'autre)
             ├─ REFILL_BAGS    ● court = +1 sac (partial) · long = valider
             │      dédock ─▶ DEDOCK + refill_pending = BEFORE_MEASURE
             ├─ REFILL_PRICE   ● court = +1 digit (partial) · long = digit suivant
             │      long sur le dernier = valider
             │      timeout 10 min ─▶ abandon
             ├─ refillMeasureAfterAndConfirm()   mesure « après », delta
             │      dédock ─▶ DEDOCK + refill_pending = AFTER_MEASURE
             │      delta < 0 ─▶ abandon · capteurs 1-3/4 ─▶ dégradé
             └─ REFILL_CONFIRM  maintien 2,5 s (récap recalculé à chaque passage)
                    ├─ complet ─▶ publication (poids + palier + event) ─▶ REFILL_SAVED
                    │                 └─ ● court (ou long, alias) / 1 min ─▶ MAIN + sommeil
                    ├─ tenue interrompue ─▶ la barre se vide, REFILL_CONFIRM reste
                    ├─ ● court ─▶ REFILL_FIX « CORRIGER ? » (rien n'est publié)
                    │     ├─ NOMBRE DE SACS ─▶ REFILL_BAGS (depuis 0) ─┐
                    │     ├─ PRIX DU SAC ─▶ REFILL_PRICE (1er chiffre) ─┤
                    │     │       mesure « après » ─▶ REFILL_CONFIRM ◀──┘ (nouveau maintien)
                    │     ├─ ANNULER ─▶ abandon (refill sauvage s'il y a eu versement)
                    │     ├─ RETOUR ─▶ REFILL_CONFIRM
                    │     └─ timeout 10 min ─▶ abandon
                    └─ timeout 10 min ─▶ abandon

  abandon = refillAbandon() : rien n'est publié par le parcours, retour DOCKED
            puis cycle de mesure normal → refill sauvage si hausse ≥ ~1 sac
```

**Règle** — les six écrans **DOIVENT** être enchaînés sans deep sleep (une seule
session interactive) ; les gestes sont captés par l'ISR timer, donc rien n'est
perdu pendant un refresh ou le WiFi.

**Règle** — le prix **DOIT** être saisi **après** le versement, pas avant : le
prix se retient mieux que le poids, mais exiger un bouton tenu pendant tout le
versement n'est pas acceptable. Le parcours est donc `poids → sacs → prix →
validation`.

### 4.1 Les 3 étapes ✅

Les étapes partagent le **même squelette** (`drawRefillStepBody()` =
`drawRefillHero()` + `drawProgressBar()`), seule la donnée change :

| # | Héros | Contexte (sous le héros) | Bandeau | Pied |
|---|---|---|---|---|
| 1/3 | poids ajouté + `KG` | `Total : … kg` / `Niveau : … %` | `EN COURS` | `● COURT SUIVANT` (seule action) |
| 2/3 | nombre de sacs | `Théorie : N kg` (nb × `BAG_KG`) | `NOMBRE DE SACS` | `● COURT + 1 SAC` / `— LONG VALIDER` |
| 3/3 | prix saisi **par digits**, une case sur le digit courant | `Total : … €` (sacs × prix) | `PRIX PAR SAC` | `● COURT PLUS` / `— LONG SUIVANT` (`VALIDER` sur le dernier) |

**Règles** communes aux 3 étapes :
- le squelette **DOIT** être celui de `drawRefillStepBody()` : titre `REMPLISSAGE`
  (45), bandeau + pointe (66 → 92), héros (152), contexte (176 / 190), barre
  d'avancement en zone secondaire (10 cases, `(step × 10) / 3` pleines → 3 / 6 /
  10) avec le compteur `n/3`, pied. **Ni sous-titre d'étape, ni étiquette au-dessus
  du héros** : le bandeau nomme l'étape et le héros se lit avec son contexte ;
- la barre d'avancement **DOIT** être identique sur les 3 écrans (mêmes 10 cases,
  même cadre, même position, même compteur `n/3`) : elle est le **seul**
  indicateur d'étape, donc seul le nombre de cases pleines change ;
- toute modification d'une étape **DOIT** passer par le corps commun, sinon les
  trois écrans dérivent l'un de l'autre ;
- les gestes **DOIVENT** être rafraîchis en partial zoné sur la fenêtre
  120 × 122 px (y 94 → 215) : le poids, le nombre de sacs et le prix changent
  pendant la saisie, un full par pas serait inutilisable ;
- le signe `€` **DOIT** être rendu par les polices : il est stocké dans le slot
  `0x100` (juste au-dessus de Latin-1) et `U+20AC` est replié sur ce slot par le
  décodeur UTF-8 (`utf8Next`). Un `last` à `0x20AC` aurait exigé 8237 entrées de
  glyphe (49 ko de flash) pour un seul caractère.

**Règles** de l'étape 1 :
- le poids ajouté **DOIT** être l'élément le plus gros et le plus noir de l'écran ;
- le suivi **DOIT** être un partial zoné (`displayShowRefillLiveUpdate`, zone
  120 × 122 px, y 94 → 215) : le poids revient en vingtaines de kilos, un full par
  lecture (~1,3 s) serait inutilisable ;
- l'app **DOIT** sonder le capteur **toutes les 3 s** et ne rafraîchir que si le
  poids a bougé d'au moins **0,5 kg** (`kLiveEpsKg`) : le poids n'a aucun geste
  pour se rafraîchir (le court avance, le long est inerte), donc sans sondage un
  écran « en direct » ne bougerait pas ; à l'inverse un partial par lecture serait
  du gaspillage (un partial coûte ~856 ms) ;
- le héros reste sans « + » : `+185` ferait 110 px en `OswaldBold30` pour 110 px
  utiles — l'unité ne tiendrait plus à côté, alors que `185 KG` (109 px) garde le
  format de héros commun à tous les écrans. Le « + » se lit dans le contexte
  (`Total : … kg` juste en dessous) ;
- `Total` et `Niveau` **DOIVENT** être empilés sous le héros, comme le contexte des
  étapes 2 et 3.

**Règles** propres :
- étape 2 — le poids **théorique** (`nb × BAG_KG`) **DOIT** suivre sous le héros :
  c'est le repère que le poids réellement versé viendra confirmer ;
- étape 3 — le prix est saisi **par digits** comme sur l'ancien écran de prix :
  `● COURT` = +1 sur le digit courant, `— LONG` = digit suivant, et validation sur
  le dernier (le helper affiche alors `VALIDER`). Une **case entoure le digit en
  cours** ; les trois cases ne tiennent pas à la taille du héros (3 × 34 px +
  virgule + 2 px de cadre de chaque côté = 122 px, la largeur de la dalle), donc
  une seule case, comme sur l'ancien écran ;
- étape 3 — le coût total (`sacs × prix`) **DOIT** être affiché sous le héros, avec
  le signe `€`.

### 4.2 « TERMINER ? » — `REFILL_CONFIRM` ✅

Dernier écran **avant** l'enregistrement. Il met en regard ce qui va être sauvé :
le poids **réellement** ajouté, le nombre de sacs, le coût.

| Élément | Contenu |
|---|---|
| titre | `TERMINER ?` |
| bandeau | `VÉRIFIER` |
| étiquette | `Ajout réel` (`JbmXb4`, y 101 — l'encre 95..103 laisse 2 px sous la pointe du bandeau et 1 px au-dessus du héros) |
| héros | `185 KG` (comme les étapes, sans `+`) |
| contexte | `15 sacs × 15 kg` puis `Coût : 103,50 €` |
| zone secondaire | **barre de maintien** (10 cases, 1 case / 10 %, partial zoné) |
| pied | `● COURT CORRIGER` / `— LONG SAUVER` |

**Règles** :
- l'enregistrement **DOIT** exiger une **tenue** (`kHoldConfirmMs`, 2,5 s, la même
  durée que la confirmation du portail) : une déclaration ne doit pas se perdre sur
  un appui appuyé par erreur ;
- un appui court (relâché avant `kHoldTapMaxMs`, 0,4 s) **DOIT** ouvrir
  « CORRIGER ? » (`REFILL_FIX`) **sans rien publier** ; une tenue interrompue
  **NE DOIT RIEN** faire d'autre que vider la barre (l'écran reste) ; le timeout
  (10 min) **DOIT** abandonner. Règle commune à toutes les barres de maintien
  (`waitHoldGesture()`), portail compris : la barre ne se dessine qu'après 0,4 s,
  ce qui sépare le tap de la tenue sans ambiguïté ;
- la barre de maintien **DOIT** être la même que celle du portail (même dessin,
  même fenêtre 104 × 22) : `drawHoldProgress()` est le helper commun.

### 4.3 « CORRIGER ? » — `REFILL_FIX` ✅

Ouvert par un **appui court** sur `REFILL_CONFIRM`. Rien n'est publié.

| Élément | Contenu |
|---|---|
| titre | `CORRIGER ?` (`OswaldBold10`, y 45, comme `OPTIONS`) |
| lignes | `NOMBRE DE SACS` · `PRIX DU SAC` · `ANNULER` · `RETOUR` — géométrie du menu `OPTIONS` (`menuRowY`), `RETOUR` décalé de 8 px comme `FERMER` |
| sélection | barre noire pleine largeur, texte blanc ; curseur initial sur `NOMBRE DE SACS` |
| zone secondaire | `ANNULER : ENVOI` / `SAUVAGE, SANS PRIX` |
| pied | `● COURT SUIVANT` / `— LONG CHOISIR` |

**Règles** :
- `NOMBRE DE SACS` **DOIT** remettre le compte à **0** (le bouton ne sait que
  +1 : un compte trop haut ne se corrigerait pas autrement), garder le prix,
  publier `refill/state = pouring`, puis enchaîner mesure « après » → E14 ;
- `PRIX DU SAC` **DOIT** reprendre la saisie au 1ᵉʳ chiffre, puis mesure
  « après » → E14 ;
- `ANNULER` **DOIT** abandonner (`refillAbandon`) : le parcours ne publie rien,
  le cycle normal publie le versement en refill sauvage ;
- `RETOUR` **DOIT** réafficher E14 sans rien changer ;
- 10 min sans geste → abandon ;
- le geste qui a quitté E14 **NE DOIT PAS** déplacer ni choisir (file vidée).

### 4.4 « ENREGISTRÉ » — `REFILL_SAVED` ✅

| Élément | Contenu |
|---|---|
| titre | `ENREGISTRÉ` |
| bandeau | `REMPLISSAGE SAUVÉ` (en `JbmXb5` : 102 px ; en `JbmXb6` il ferait 119 px et déborderait du bandeau de 110 px) |
| étiquette | `Ajout réel` |
| héros | `185 KG` |
| zone secondaire | `15 sacs · 103,50 €` puis `0,56 €/kg · 462 kg` (`JbmXb4` : en `JbmXb5` les crochets sortiraient des marges) |
| pied | `● COURT PRINCIPAL` (une seule ligne) |

**Règles** :
- l'écran **DOIT** rester **1 minute** (`kSavedDwellMs`) puis rejoindre `MAIN`
  avec le poids **déjà mesuré** — **sans remesure ni publication** : le poids vient
  d'être mesuré et publié ;
- le retour anticipé **DOIT** être possible par `● COURT` ; l'appui long **PEUT**
  en être un alias (annoncé une seule fois pour ne pas surcharger le pied) ;
- le récap **NE DOIT PAS** comporter plus de deux lignes : la zone secondaire ne
  permet pas davantage, et 4 lignes ne tiendraient pas non plus sous le héros ;
- le prix au kilo affiché est `coût total / poids ajouté` (le coût par kilo
  **versé**, pas celui du stock).

### 4.5 Règles du parcours (dédock, timeouts, publication)

- Un dédock pendant le parcours **DOIT** mettre la FSM en attente
  (`refill_pending`), afficher le MODE NOMADE et reprendre au redock ; timeout
  d'attente **1 h**.
- Saisie, versement et validation : **10 min** d'inactivité (`kRefillSessionTimeoutMs`)
  → abandon : retour `DOCKED` et cycle de mesure normal (refill sauvage si une
  hausse ≥ ~1 sac est mesurée). Un prix saisi mais non confirmé **NE DOIT
  JAMAIS** être publié.
- Le coût affiché est `nb de sacs × prix du sac` ; le poids mesuré n'est qu'une
  vérification (un sac peut peser plus ou moins que `BAG_KG`).

---

## 5. Portail de configuration

### 5.1 Confirmation de démarrage — `PORTAL_CONFIRM`

**Règle** — une action lançant le point d'accès du portail **NE DOIT PAS** être
validée par un appui court : elle **DOIT** exiger un appui **maintenu**, et
l'écran **DOIT** le dire (`MAINTENIR`) et le **montrer** (barre de maintien).

| Geste / état | Effet |
|---|---|
| appui **court** | **quitte** — aucun lancement, l'appelant redessine son écran |
| **tenue 2,5 s** (`kHoldConfirmMs`) | la barre se remplit (10 cases, 1 case / 10 %, partial zoné) puis `webConfigRun()` — la page suivante est le portail |
| relâché avant la fin (après 0,4 s) | **rien** : la barre se vide, l'écran reste (on peut retenter) |
| 30 s sans appui (`kPortalConfirmTimeoutMs`) | abandon |

Rendu : titre `PORTAIL WIFI`, bandeau `RÉGLAGES LOCAUX`, question centrée sur
4 lignes, mot `MAINTENIR`, barre de maintien en zone secondaire, pied
`● COURT QUITTER` / `— LONG LANCER`. Le pied **NE DOIT PAS** porter
`MAINTENIR` (55 px en `JbmXb4` pour 116 px utiles) : l'instruction est portée
par le mot centré.

`kHoldConfirmMs` est **partagé** avec `REFILL_CONFIRM` (§4.2) : les deux écrans
sont des « je valide en tenant », la durée doit donc être la même des deux côtés.

### 5.2 Le portail — `webConfigRun()` (`CONFIG`)

- **Règle** — le portail **NE DOIT PAS** revenir à la normale sans
  **redémarrage** : « Enregistrer » (test WiFi réussi → `settingsSave`) et
  l'arrêt auto (10 min d'inactivité) **DOIVENT** tous deux finir par
  `ESP.restart()`.
- **Règle** — « Tester WiFi + MQTT » **NE DOIT PAS** redessiner l'e-paper : le
  verdict est renvoyé dans la page web, que l'utilisateur est en train de lire
  (le portail est figé ~15-20 s, l'écran ne changerait rien pour lui). L'ancien
  écran texte `TEST` (FreeMono, 10 car./ligne, sans en-tête ni pied) est
  **supprimé**.
- Si le test WiFi d'enregistrement échoue : rien n'est enregistré, le portail reste
  joignable et **l'e-paper ne doit pas être redessiné** (seule la page web est
  renvoyée).
- Le pied est **câblé** : `● COURT REDEMARRER` sur une carte configurée,
  `● COURT VEILLE` sans configuration (l'appui long est accepté, non annoncé).
- **Règle** — sans configuration enregistrée, la sortie (appui ou arrêt auto)
  **NE DOIT PAS** redémarrer (le portail se relancerait indéfiniment, AP
  allumé) : la carte **DOIT** afficher `UNCONFIGURED` puis dormir (§5.3).

### 5.3 Carte en veille sans configuration — `UNCONFIGURED`

| Élément | Contenu |
|---|---|
| titre | `CARTE` / `EN VEILLE` (`OswaldBold10`, y 38 / 60) |
| bandeau | `NON CONFIGURÉ` (`JbmXb6`, position sous titre) |
| corps | `AUCUN WI-FI` / `NI BROKER MQTT` / `ENREGISTRÉ` (`JbmXb5`, centré) |
| zone secondaire | `LE RÉVEIL OUVRE` / `LE PORTAIL WEB` |
| pied | `● COURT RÉVEIL` (une ligne) |

**Règles** : dessiné **une seule fois** (FULL) à l'entrée en veille ; un réveil
timer (contrôle batterie) **NE DOIT PAS** le redessiner (l'e-paper le garde) ;
tout appui réveille la carte et rouvre le portail.

---

## 6. Rafraîchissement (full / partial)

**Règles** :
- Le rafraîchissement **DOIT** être FULL par défaut ; le partial **DOIT** rester
  réservé au retour visuel d'un geste (une zone change, le reste est déjà juste).
- Un FULL forcé **DOIT** tous les 20 partials consécutifs (`kFullRefreshEveryN`,
  anti-ghosting) ; `canPartial()` **DOIT** être faux si l'écran affiché n'est pas
  celui attendu, si le cache est perdu, ou si la dalle n'a pas été réinitialisée
  depuis le réveil.
- Toute zone partial **DOIT** être effacée avant redessin (une barre qui se vide,
  une inversion de texte ou une case qui disparaît laisserait du noir fantôme),
  et ses **x / w DOIVENT** être des multiples de 8 (adressage du SSD1680).

| Écran | Rafraîchissement |
|---|---|
| `MAIN` | **FULL** systématique · partial zoné jauge (120 × 31, y 162) et zone d'en-tête (32 × 19, éclair + batterie) — banc 🔜 |
| `DEDOCK` (nomade) | FULL ; partial zoné de la **zone d'en-tête** (32 × 19, x 88 → 119 : éclair de charge + batterie 3 segments) si le même écran est affiché — c'est la seule zone qui bouge en veille |
| `OPTIONS` | FULL au 1ᵉʳ affichage ; partial zoné (120 px, les 2 lignes concernées) au déplacement de la barre — banc 🔜 |
| `REFILL_LIVE` / `REFILL_BAGS` / `REFILL_PRICE` | FULL au 1ᵉʳ affichage ; **partial zoné du corps de l'étape** (120 × 122, y 94 → 215 : héros, contexte, barre) — chaque variation |
| `REFILL_CONFIRM` | FULL ; partial zoné de la barre de maintien (104 × 22, zone secondaire) à chaque case franchie |
| `PORTAL_CONFIRM` | FULL ; partial zoné de la barre (même fenêtre, helper commun `drawHoldProgress`) |
| `REFILL_FIX` | FULL au 1ᵉʳ affichage ; partial zoné (120 px, les 2 lignes concernées) au déplacement de la barre — même mécanique qu'`OPTIONS` |
| `REFILL_SAVED` / `CONFIG` / `INFORMATIONS` | FULL |
| `UNCONFIGURED` | FULL, une seule fois par entrée en veille |

⚠️ Sur T5 **V2.4** (`USE_EPD_PWR_CUTOFF = 1`) l'alimentation écran est coupée au
sommeil : la RAM de trame ne survit pas → le **premier draw de chaque réveil est
FULL**. Les partials ne s'appliquent donc qu'**au sein d'une même session**
(gestes rapprochés), jamais entre deux réveils.

---

## 7. Effets de bord par transition

**Règle** — toute transition **DOIT** être idempotente côté RAM RTC et ne
**DOIT** publier sur MQTT que ce qui est juste : le palier de poids est la
référence unique pour Home Assistant. Sujets : `scale/value`, `scale/battery`,
`scale/error/on-off`, `scale/quality` (retain) ; `scale/refill/state` (retain),
`scale/refill/event` (sans retain).

| Transition | MQTT | RAM RTC |
|---|---|---|
| → `MAIN` (4/4, palier déplacé) | `value`, `battery`, `error=off`, `quality` (`n/4 ok`) | `last_weight_kg`, `last_battery_pct`, `skip_count=0` |
| → `MAIN` (4/4, palier gelé : hausse < 1 sac) | **rien** | `skip_count++` |
| → `MAIN` `-1` (1-3/4, 1ʳᵉ fois) | `error=on`, `quality` (`n/4 degraded`) | `announced_error = DEGRADED` ; l'écran lit `last_measure_kg` / `last_measure_at` (non modifiés) |
| → `MAIN` `-1` (dégradé stationnaire) | rien | rien (l'image reste) |
| → `DEDOCK` (transition) | `error=on`, `quality` (`0/4`, mode `dedock`) | `announced_error = DEDOCKED` |
| → `DEDOCK` (stationnaire) | rien | partial de la zone d'en-tête si `%` ou charge changé |
| → `REFILL_LIVE` | `refill/state = pouring` | mode `REFILL_POURING`, `refill_before_kg`, `refill_pending = NONE`, `refill_started_at`, `price_digits = {4,0,5}` (défaut **4,05 €**), `refill_bag_count = 0` |
| → `REFILL_BAGS` / `REFILL_LIVE` (refresh) | rien | `refill_bag_count` |
| → `REFILL_PRICE` | `refill/state = pricing` | mode `REFILL_PRICING` |
| → `REFILL_CONFIRM` | **rien** (aucun publish avant le maintien) | `refill_delta_kg` |
| maintien complété → `REFILL_SAVED` | `value`+`battery`+`error=off`+`quality`, puis `refill/state = saved` et `refill/event` (`added_kg`, `bag_price_eur`, `bag_count`) | `commitRefillPalier` : le palier passe **immédiatement** au poids « après » (sinon le cycle suivant re-détecte la hausse → 2ᵉ event → double comptage €) |
| appui court → `REFILL_FIX` | **rien** | chiffres et nombre de sacs conservés |
| maintien relâché avant la fin | **rien** (la barre se vide) | — |
| `REFILL_FIX` → `NOMBRE DE SACS` | `refill/state = pouring` | `refill_bag_count = 0`, mode `REFILL_POURING` |
| `REFILL_FIX` → `ANNULER` | rien par le parcours (abandon : refill sauvage par le cycle normal) | retour `DOCKED` |
| abandon du parcours (timeouts, delta < 0) | rien par le parcours ; le cycle normal enchaîné publie une hausse ≥ ~1 sac en refill sauvage (`bag_count = 0`) | retour `DOCKED` |
| dédock pendant le parcours | **rien** : le chemin appelle `displayShowNomade` directement, sans publish offline | `refill_pending` + mode `DEDOCKED` |
| reprise au redock | rien | `refill_pending = NONE` |
| → `MAIN` en fin de parcours | rien (poids déjà publié, pas de remesure) | mode `DOCKED` |

**Règle** — `refill/state` décrit la **phase du parcours** (`pouring`, `pricing`,
`saved`), **pas** la position physique du boîtier : pendant tout le parcours le
silo est hors de sa base. L'état physique reste porté par `quality.mode`
(`docked` / `dedocked` / `pouring` / `pricing`), et c'est au cycle suivant qu'il
reprend sa valeur réelle. Un `refill/state = docked` **DOIT** être proscrit :
c'était un vestige de l'ancien parcours et cela annonçait un dépôt qui n'a pas eu
lieu.

**Règle** — la session réseau **DOIT** être ouverte **avant** le rendu des écrans
qui portent l'heure (`publishBegin`), et le MQTT **DOIT** être envoyé **après**
le dessin : l'écran ne doit pas attendre la publication. L'inverse vaut pour
`refill/state` : il **DOIT** être publié **avant** le rendu de l'étape concernée
(le bloc est bloquant : WiFi + MQTT, plusieurs secondes si le broker est
injoignable).

---

## 8. Transitions sans redessin (« impasses »)

Cas où le firmware **dort sans changer l'image** — utile pour savoir ce qui reste
affiché après un réveil.

| Cas | Écran qui reste | Effet |
|---|---|---|
| `vbat < 3,3 V` au réveil | image précédente | cycle sauté (ni Wi-Fi, ni publish) → prochain créneau |
| `vbat < 3,2 V` (urgence) | image précédente | 24 h ; 3ᵉ réveil → 1 semaine — un poll léger sous le seuil d'urgence bascule sur le cycle complet |
| veille nomade, segments ou USB changés (poll léger) | `DEDOCK` | partial de la zone d'en-tête (éclair + batterie) — RAM de trame reconstruite au réveil |
| `announced_error == DEGRADED` | `MAIN` `-1` | aucun redessin, **pas même la batterie** |
| `announced_error == DEDOCKED` | `DEDOCK` | partial de la zone d'en-tête (éclair + batterie) seulement si `%` ou charge a changé |
| réveil en mode remplissage (reset en plein parcours) | `REFILL_LIVE` / `REFILL_BAGS` / `REFILL_PRICE` | abandon : cycle normal → `MAIN` |
| refill refusé (batterie trop basse) | `MAIN` | rien de dessiné |
| timeout versement / comptage (10 min) | `REFILL_LIVE` / `REFILL_BAGS` | abandon : cycle normal → `MAIN` |
| timeout saisie du prix (10 min) | `REFILL_PRICE` | abandon : cycle normal → `MAIN` |
| delta < 0 à la validation | `REFILL_CONFIRM` | abandon : cycle normal → `MAIN` |
| appui court sur `REFILL_CONFIRM` | `REFILL_CONFIRM` | « CORRIGER ? », aucun refill publié |
| maintien relâché avant la fin (`REFILL_CONFIRM`, `PORTAL_CONFIRM`) | même écran | la barre se vide, rien d'autre |
| `ANNULER` ou timeout (10 min) sur `REFILL_FIX` | `REFILL_FIX` | abandon : cycle normal → `MAIN` |
| timeout sur `REFILL_CONFIRM` (10 min) | `REFILL_CONFIRM` | abandon : cycle normal → `MAIN` |
| dedock + `refill_pending` > 1 h | `DEDOCK` | flag effacé, abandon propre |
| `PORTAL_CONFIRM` : appui court / timeout | `PORTAL_CONFIRM` | abandon, l'appelant redessine |
| timeout d'inactivité du menu | `OPTIONS` | équivalent de `FERMER` : `MAIN` (dernier poids connu) puis sommeil |
| réveil timer d'une carte non configurée | `UNCONFIGURED` | contrôle batterie, aucun redessin, 24 h |

---

## 9. Gestes annoncés (pied) vs gestes câblés

**Règle** — le pied **DOIT** annoncer exactement les gestes que l'écran accepte,
et **NE DOIT** jamais annoncer un geste inerte. Un écart est un défaut, pas un
choix d'ergonomie.

**Règle** — le pied **NE DOIT PAS** porter de deux-points de séparation : le nom
du bouton reste à gauche (`● COURT` / `— LONG`, ancré sur `kMarginL`), la valeur —
l'action — est alignée à droite sur `kMarginR` (116). C'est cet alignement qui
fait la colonne, pas un séparateur.

**Règle** — un écran dont l'unique geste est un appui long **DOIT** réduire son
pied à la ligne unique du bas (`drawFooterLongOnly`), et un écran dont l'unique
geste est un appui court **DOIT** utiliser `drawFooterOne()`.

| Écran | Pied dessiné | ● Court | — Long |
|---|---|---|---|
| `MAIN` | `● COURT MESURER` / `— LONG OPTIONS` | ✅ cycle complet (+ publish si Δ) | ✅ ouvre `OPTIONS` |
| `DEDOCK` (nomade) | `— LONG OPTIONS` | ❌ inerte (non annoncé, se rendort) | ✅ ouvre `OPTIONS` |
| `OPTIONS` | `● COURT SUIVANT` / `— LONG CHOISIR` | ✅ ligne suivante (partial) | ✅ l'entrée est validée |
| `INFORMATIONS` p1 / p2 | `● COURT AUTRE PAGE` / `— LONG RETOUR` | ✅ bascule p1 ↔ p2 | ✅ retour au menu |
| `PORTAL_CONFIRM` | `● COURT QUITTER` / `— LONG LANCER` | ✅ abandon | ✅ **tenue 2,5 s** → portail |
| `CONFIG` (portail actif) | `● COURT REDEMARRER` (configurée) / `● COURT VEILLE` (sans configuration) | ✅ redémarrage / veille | — (accepté comme le court, non annoncé) |
| `UNCONFIGURED` (carte en veille, sans config) | `● COURT RÉVEIL` (1 ligne) | ✅ réveil → portail | ✅ idem (non annoncé) |
| `REFILL_LIVE` | `● COURT SUIVANT` (1 ligne) | ✅ étape 2/3 | — (inerte) |
| `REFILL_BAGS` | `● COURT + 1 SAC` / `— LONG VALIDER` | ✅ +1 sac (partial) | ✅ étape 3/3 |
| `REFILL_PRICE` | `● COURT PLUS` / `— LONG SUIVANT` (`VALIDER` sur le dernier) | ✅ +1 sur le digit (partial) | ✅ digit suivant / validation |
| `REFILL_CONFIRM` | `● COURT CORRIGER` / `— LONG SAUVER` | ✅ ouvre `REFILL_FIX` | ✅ **tenue 2,5 s** → enregistrement |
| `REFILL_FIX` | `● COURT SUIVANT` / `— LONG CHOISIR` | ✅ ligne suivante (partial) | ✅ sacs / prix / annuler / retour |
| `REFILL_SAVED` | `● COURT PRINCIPAL` (1 ligne) | ✅ écran principal (alias : le long aussi) | — (alias) |

---

## 10. Récapitulatif écran → déclencheur

| Vers l'écran | Depuis / déclencheur |
|---|---|
| `CONFIG` | démarrage sans réseau **ou** broker enregistré ; `PORTAL_CONFIRM` tenu 2,5 s ; appui bouton sur `UNCONFIGURED` |
| `UNCONFIGURED` | sortie du portail (appui ou arrêt auto) sans configuration enregistrée |
| `TEST` | `CONFIG` : « Tester WiFi + MQTT » |
| `MAIN` | cycle planifié (mesure OK, ou 1-3/4 → variante `CAPTEUR(S) HS`) ; fin de `REFILL_SAVED` ; `FERMER` du menu ; retour du re-test du dock |
| `DEDOCK` (nomade) | cycle/réveil : 0/4 **avec** signal de dédock ; dédock pendant le parcours |
| `OPTIONS` | appui long au réveil depuis `DOCKED`/`DEGRADED` |
| `REFILL_LIVE` | `OPTIONS` → `REMPLISSAGE` ; reprise après dédock (`BEFORE_MEASURE`) ; réveil en mode `REFILL_POURING` |
| `REFILL_BAGS` | `● COURT` sur l'étape 1 ; `NOMBRE DE SACS` sur `REFILL_FIX` |
| `REFILL_PRICE` | `— LONG` sur l'étape 2 ; `PRIX DU SAC` sur `REFILL_FIX` ; réveil en mode `REFILL_PRICING` |
| `REFILL_FIX` | appui court sur `REFILL_CONFIRM` |
| `REFILL_CONFIRM` | `— LONG` (validation) sur l'étape 3 ; reprise après dédock (`AFTER_MEASURE`) ; correction terminée ou `RETOUR` sur `REFILL_FIX` |
| `REFILL_SAVED` | maintien de 2,5 s sur `REFILL_CONFIRM` |
| `INFORMATIONS` | entrée `INFORMATIONS` du menu |
| `PORTAL_CONFIRM` | entrée `PORTAIL RÉGLAGES` du menu |

---

## 11. Menu `OPTIONS`

`displayShowOptions()` est câblé (`appOptionsMenu`) et **atteignable** : c'est la
seule voie vers le portail sur un appareil configuré (le cold boot + bouton
n'ouvre plus le portail, cf. §1).

### 11.1 Arbitrage d'entrée — tranché : (a)

Le pied de `MAIN` annonce `— LONG OPTIONS` ; l'appui long ouvre donc le menu, et
le remplissage ne se lance plus que par l'entrée `REMPLISSAGE`. Conséquence
assumée : le remplissage demande **deux gestes** au lieu d'un (menu → entrée 0),
ce qui est le prix de l'explicitation — un appui long unique ne doit plus
enchaîner une saisie de prix puis un enregistrement.

### 11.2 Entrées du menu

**Règle** — le menu fait **TOUJOURS 4 lignes** (base 96, pas 24, `FERMER` à +8),
quelle que soit la situation : l'espacement ne doit pas changer d'un écran à
l'autre, sinon « l'entrée manquante » se lit comme un oubli.

**Règle** — une entrée **impossible dans le mode courant** est **INACTIVE, pas
absente** : elle reste à sa place, sur un **contour 1 px tramé à 25 %** (le
tramage EST le gris sur une dalle 1 bit) au lieu de la barre pleine — le libellé
reste sur fond blanc, donc lisible. Et le **curseur la saute** : « choisir »
agit donc toujours, et l'utilisateur voit de quoi il a sauté. Un simple tramé
sans contour rendait les majuscules illisibles (vérifié au rendu hors matériel) ;
un contour sans tramage se confondait avec une simple sélection.

**Règle** — à l'ouverture, le curseur **DOIT** se poser sur la première ligne
**jouable** (`menuFirstEnabledRow()`) : hors base ce n'est pas `REMPLISSAGE`.
Sinon le menu s'ouvre sur une ligne qui n'est pas sélectionnable — donc sans
barre de sélection, et à deux appuis de la première entrée possible.

| Entrée | Posé sur la base | Hors base |
|---|---|---|
| `REMPLISSAGE` | ✅ sélectionnable → `REFILL_LIVE` | 🚫 **inactive** (tramée, curseur par-dessus) — le silo n'est pas sur sa balance et l'étape 1 commence par une mesure |
| `PORTAIL RÉGLAGES` | ✅ `PORTAL_CONFIRM` → `CONFIG` | ✅ idem |
| `INFORMATIONS` | ✅ les 2 pages | ✅ idem |
| `FERMER` | ✅ écran principal (dernier poids connu) | ✅ écran nomade |

`FERMER` **DOIT** rendre à l'écran du **mode courant**, pas toujours à l'écran
principal : hors base, afficher une jauge de silo et « CONNECTÉ » serait un
mensonge.

| Entrée | Cible | Détail |
|---|---|---|
| `REMPLISSAGE` | `REFILL_LIVE` | `refillStart()` : garde batterie, puis tout le parcours |
| `PORTAIL RÉGLAGES` | `PORTAL_CONFIRM` → `CONFIG` | `appPortalConfirm()` ; si l'utilisateur renonce, le menu est redessiné |
| `INFORMATIONS` | `INFORMATIONS` p1 → p2 | `appShowInformations()` : valeurs réelles (session réseau ouverte, 4 pieds lus), `● COURT AUTRE PAGE` = bascule p1 ↔ p2, `— LONG RETOUR` = menu |
| `FERMER` | écran du mode → deep sleep | **sans remesure** : `MAIN` avec le dernier poids connu si le boîtier est posé, sinon l'écran nomade (aucune mesure n'est possible hors base) |

`FERMER` est **séparé** des trois autres par 8 px (`kMenuLastDY`) : ce n'est pas
une 4ᵉ fonctionnalité, c'est la sortie du menu — et le menu **doit** avoir une
issue explicite, sinon on n'en sort qu'en choisissant une action.

**Règle** — `FERMER` et le timeout d'inactivité (1 min, non annoncé au pied :
le pied annonce les gestes, pas l'absence d'attention) **DOIVENT** produire le
même effet : `MAIN` avec le dernier poids connu, puis deep sleep.

### 11.3 Gestes

- `OPTIONS` : `● COURT` = ligne suivante (la barre bouge → partial zoné, une ligne
  par clic), `— LONG` = valider l'entrée sélectionnée.
- `INFORMATIONS` : `● COURT AUTRE PAGE` = bascule p1 ↔ p2 ; `— LONG RETOUR` = retour au menu
  (aucune confirmation à maintenir depuis les informations).

### 11.4 Points techniques

- L'index de sélection du menu **n'a pas besoin de survivre au sommeil** (on n'y
  dort pas) : une variable locale suffit.
- Le menu réutilise `enterInteractiveSession()` (capture des appuis + absorption
  de l'appui de réveil) : même mécanique que le parcours de remplissage.
- `MenuItem` est un contrat partagé (`display.hpp`) : l'écran définit les libellés
  et la géométrie, l'app dispatche le choix.
- `drawMenuRow()` passe par `menuRowY(i)` pour que la ligne `FERMER` soit
  décalée ; le partial de déplacement de la sélection l'utilise aussi (fenêtre
  entre les deux lignes concernées).
