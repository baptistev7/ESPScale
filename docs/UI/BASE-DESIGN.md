# Base de design — écrans e-paper (TTGO T5, 122×250)

Référence commune à **tous** les écrans embarqués. Un nouvel écran doit réutiliser
cette grille, ces polices et ces composants — sans réinventer de coordonnées.

## Dalle & contraintes

- Écran **122 × 250 px, portrait**, **1 bit** (blanc pur / noir pur).
- Full refresh ~2,6 s (avec flash), partial zoné ~856 ms (sans flash).
- Après chaque deep sleep, le premier draw est **FULL** (la RAM de trame ne
  survit pas à la coupure d'alimentation écran).
- Marges : **gauche x = 7**, **droite x = 116** ; centrage horizontal **x = 61**.
- Inversé (fond noir + texte blanc) réservé aux **bandeaux d'alerte**.
- Aucun wrap : un libellé trop large est **raccourci** (mesurer avec `textWidthUtf8`).

### Polices

Bitmap dédiées, générées des TTF *JetBrains Mono ExtraBold* et *Oswald Bold* :
plage `0x20–0xFF` → **accents rendus**, et le **signe `€`** (U+20AC) ajouté dans
le slot `0x100` (juste au-dessus de Latin-1) — le décodeur UTF-8 de
`display.cpp` replie `U+20AC` sur ce slot, ce qui évite 8237 entrées de glyphe
(49 ko) pour un seul caractère. En revanche le **tiret cadratin** `—` (U+2014)
n'existe pas : l'écrire en ASCII `—`.

| Police | Hauteur | Usage |
|---|---|---|
| `JbmXb4` | ~7 px | état, libellés de jauge, zone secondaire, pied |
| `JbmXb5` | ~8 px | date de l'en-tête |
| `JbmXb6` | ~9 px | bandeau d'alerte, titre d'écran |
| `OswaldBold10` | ~16 px | unité (KG) |
| `OswaldBold30` | ~49 px | valeur principale (poids) |

## Grille (référence)

| Zone | y (px) | Contenu |
|---|---|---|
| **A — en-tête** | 0 → 20 | séparateur noir à **y = 20**. À gauche (x = 7) : **date** `jj/mm/aa` (`JbmXb5`). À droite : **éclair de charge** (x = 78, 10 × 12) si l'USB est présent, puis **batterie 3 segments** (x = 92, w = 22, h = 10 + ergot). Tout ce bloc (72 → 119) est la **zone de partial** de l'en-tête. |
| **B — contenu** | 20 → 194 | — |
| ↳ **état** | baseline **29** | pastille `●` (r = 2) + libellé, **centré** (`JbmXb4`) |
| ↳ **bandeau d'alerte** (option) | **36 → 56** (h = 20) + **pointe 56 → 63** | **inversé**, texte centré baseline **50** (`JbmXb6`), **pointe vers le bas** centrée (24 px de large, 8 px de profond) |
| ↳ **bandeau de titre** (option) | **66 → 84** (h = 18) + **pointe 84 → 92** | même motif, sous le **titre** (écrans nomade / portail / remplissage) ; texte centré baseline **79**. L'interligne 46 → 66 px peut porter une **légende de héros** (`Ajout réel`, `JbmXb4`, baseline **101**) — voir règle 7 |
| ↳ **zone messages (libre)** | ~57 → 103 | réservée aux messages, au-dessus du poids — hosting des deux bandeaux ci-dessus (avec leur pointe) |
| ↳ **valeur principale** | baseline **152** | poids `OswaldBold30` + unité `OswaldBold10`, **centré** — **position constante sur tous les écrans** |
| ↳ **jauge** | libellés baseline **170** ; barre **176 → 191** (h = 15) | libellé à gauche (`CAPACITÉ`), valeur à droite (`65 %`) ; barre : contour 1 px, remplissage intérieur |
| **C — secondaire (réservée)** | **194 → 216** (h = 22) | **crochets hauts `[ ]`** (barre verticale + ergots 4 px) + message **sur UNE ligne**, centré baseline **208** (`JbmXb4`) |
| **D — pied** | séparateur **y = 221** | l.1 baseline **233** : `● COURT` (gauche) / `MESURER` (droite). l.2 baseline **244** : `— LONG` / `OPTIONS`. **Aucun deux-points** : c'est l'alignement qui fait la colonne. |

## Composants

- `drawHeader(batt, charging)` — **en-tête commun** : date (`JbmXb5`) à gauche,
  éclair de charge + batterie 3 segments à droite, séparateur. Utilisé tel quel
  par tous les écrans — c'est donc **lui** qui porte l'état « sur USB », seul
  signe qui distingue une recharge d'une batterie qui descend. L'éclair est un
  polygone vectoriel 10 × 12 (`drawChargingBolt`) : à cette taille un bitmap
  serait illisible, et il est dans la zone de partial donc gratuit à
  rafraîchir.
- `drawHeaderBatteryZone(batt, charging)` — la **seule zone d'en-tête qui bouge**
  (48 × 19, x 72 → 119 ; le séparateur à y = 20 et la date restent hors fenêtre). Partagée par
  le rendu complet et par les deux partials d'en-tête (écran principal et écran
  nomade) : `x`/`w` multiples de 8, et **tout ce que l'en-tête dessine à droite
  du x = 88 doit y tenir**.
- `drawFooter(action_court, action_long)` — **pied commun** : `● COURT` à gauche,
  l'action alignée **à droite** sur `kMarginR`, puis `— LONG` et son action sur la
  même colonne. **Pas de deux-points** : le nom du bouton à gauche, la valeur à
  droite, et rien entre les deux.
- `drawFooterOne(action)` — pied **réduit à la ligne 1** (`● COURT`) : écran dont
  l'unique geste est un appui court.
- `drawFooterLongOnly(action)` — pied **réduit à la ligne 2** (`— LONG`) : écran
  dont l'unique geste est une **tenue** (ECRAN14). La ligne 2 est celle de la
  tenue, donc le tiret de gauche le dit ; un `● COURT` y serait un geste
  inexistant, donc un mensonge.
- Budget de l'action du pied : **77 px** (116 - 39, la pastille + `COURT` /
  `LONG` occupant 6 → 39). `INFORMATIONS` (66 px) est la plus longue action en
  usage.
- `drawUtf8 / drawUtf8Centered / drawUtf8Right / drawUtf8CenteredInv` — texte
  UTF-8 (accents). `textWidthUtf8(font, texte)` donne la largeur (contrôle
  anti-débordement **avant** validation).
- `drawBattery3Seg(x, y, w, h, pct)` — contour + ergot + 1 à 3 segments pleins.
- `drawDot(cx, cy, r)` — pastille `●` (appui court) ; `drawDash(x, y, w)` — tiret
  `—` (appui long).
- `drawSecondaryBox(msg)` — crochets hauts + message une ligne.
  `drawSecondaryFrame()` — le cadre **pleine largeur** seul (crochets hauts), à
  composer quand la zone accueille autre chose qu'un message (barre d'avancement).
- `drawDisabledRow(row)` — **entrée inactive** : même place et même hauteur de
  barre, mais **contour 1 px + tramé 25 %** (2 px pleins dans une cellule de 4 px)
  au lieu du noir plein — sur 1 bit, le tramage EST le gris. Le libellé reste sur
  fond blanc (donc lisible) et le **curseur saute la ligne**, pour que « choisir »
  n'agisse jamais pour rien. Réservé à une entrée impossible dans le mode
  courant (`REMPLISSAGE` hors base) : jamais pour une entrée absente.
- `drawProgressBar(step, steps)` — barre d'avancement dans la **zone
  secondaire** : 10 cases (5 px de large, pas de 7 px) à gauche, compteur
  `n/3` aligné à droite, le tout dans le cadre pleine largeur. Cases pleines
  selon `(step × 10) / 3`. ** identique sur les 3 étapes du remplissage : elle est
  le seul indicateur d'étape. L'avancement se lit sans lire un chiffre.
- `drawInvertedBanner(top, h, font, texte, text_y)` — **bandeau inversé commun** :
  barre noire pleine largeur (`kMarginL`→`kMarginR`) + texte blanc centré, **prolongée
  par une pointe pleine vers le bas** (base 24 px centrée sur `kCenterX`, 8 px de
  profond) qui désigne le contenu situé en dessous. Utilisé pour les **deux**
  positions de bandeau (alerte sous l'état, titre sous le titre) : ne pas
  redessiner un bandeau à la main.
- `drawHoldBar(remplies)` — **barre de maintien** : 10 cases entre crochets dans
  la zone secondaire (7 px de large, 8 px de pas, 12 px de haut, y = 200), les
  cases pleines en noir et les vides en contour 1 px (sinon elles disparaissent
  sur la dalle). Réservée aux **actions exigeant un appui long maintenu** :
  ne jamais valider une action sensible sur un appui court.
- `drawHoldProgress(écran, pct)` — **remplissage de la barre en partial zoné**
  (fenêtre 104 × 22 sur la zone secondaire), une case toutes les 10 %. Helper
  **commun** aux deux écrans à tenue — le portail (`displayShowPortalHoldProgress`)
  et ECRAN14 (`displayShowRefillConfirmHoldProgress`) — : même dessin, même
  fenêtre, seul l'écran de repli change. C'est ce retour visuel qui rend la tenue
  lisible.
- Barres d'avancement **vs** barres de maintien : la première **compte** des
  étapes (10 cases de 5 px, compteur `n/3` à droite), la seconde **chronomètre**
  une tenue (10 cases de 7 px, sans compteur). Ne pas les confondre.

## Règles

1. **Zone secondaire réservée** sur **tous** les écrans, **même vide**.
2. **Position du poids constante** (`kHeroY`) sur tous les écrans.
3. **Pied toujours en bas** (`● court` / `— long`) — un seul bouton physique. Le
   pied n'annonce **que** les gestes que l'écran accepte, et l'action est alignée
   à droite sans séparateur.
4. Bandeau d'alerte **uniquement** pour un état particulier (ex. `NIVEAU BAS`),
   toujours avec sa **pointe vers le bas** (jamais un rectangle nu).
5. Zones réservées en haut (messages) **au-dessus** du poids, jauge **sous** le poids.
   Exception : la **légende d'une valeur non live** (ex. `DERNIER POIDS CONNU` sur
   le mode nomade) est une contre-note du nombre → elle se place **sous** le poids
   (`kNomadeLabelY = 172`), pas dans la zone de messages.
6. ASCII/Latin-1 seulement ; pas de retour à la ligne.
9. **Une entrée impossible ne disparaît pas** : elle se garde sa place et son
   espacement, avec le traitement « inactive » (contour + tramé), et le curseur
   la saute. Un menu qui se recompose selon l'écran se lit comme un bug.
10. **Aucun écran dans l'ancien style** : ni polices Adafruit `FreeMono*`, ni silo
   ÖkoFEN vectoriel. Un écran qui n'a pas son rendu dans la charte n'est pas
   « presque fini », il est à faire.
7. **Légende de héros** — quand un héros pourrait être confondu avec une autre
   grandeur (ECRAN14/15 : `185 KG` est le poids *versé*, pas le poids du silo), la
   légende se place entre la pointe du bandeau (92) et l'encre du héros (105) :
   baseline **101** en `JbmXb4` (en `JbmXb5`, le jambage du « j » toucherait le
   héros). Le héros, lui, reste **sans `+`** : `+185` ferait 110 px pour 110 px
   utiles et l'unité ne tiendrait plus à côté.
8. **Pas de sous-titre d'étape** dans un parcours séquentiel : l'indicateur
   d'avancement (barre + `n/3`) en zone secondaire suffit, et il se compare d'un
   écran à l'autre. Exception : une **légende de héros** (règle 7), qui n'est pas
   un indicateur d'étape mais une contre-note de la valeur.

## Écrans de référence

- **E01** — mesure connectée : `docs/UI/ECRAN01/` (`[ MESURE STABLE ]`).
- **E02** — niveau bas : `docs/UI/ECRAN02/` (bandeau `NIVEAU BAS` +
  `[ REMPLISSAGE CONSEILLE ]`).
- **E07** — confirmation de portail : `docs/UI/ECRAN07/` (barre de maintien).
- **E11** — remplissage en cours : `docs/UI/ECRAN11/` (suivi en direct).
- **E12** — nombre de sacs : `docs/UI/ECRAN12/` (théorie + barre d'avancement).
- **E13** — prix par sac : `docs/UI/ECRAN13/` (saisie par digits avec case sur le
  digit courant, coût total en `€`, barre pleine).
- **E14** — « TERMINER ? » : `docs/UI/ECRAN14/` (légende `Ajout réel`, héros, deux
  lignes de contexte, barre de maintien, pied `● COURT CORRIGER` /
  `— LONG SAUVER`). L'écran « CORRIGER ? » qui suit l'appui court n'a pas de
  maquette : il reprend la géométrie du menu `OPTIONS` (rendu pixel :
  `docs/screens/refill_fix_0.png`).
- **E15** — « ENREGISTRÉ » : `docs/UI/ECRAN15/` (récap sur 2 lignes en zone
  secondaire, pied `● COURT PRINCIPAL`).
- **E18/E19** — informations : `docs/UI/ECRAN18/` et `docs/UI/ECRAN19/`.
- **E22** — menu `OPTIONS` **hors base** : 4 lignes comme sur la base, mais
  `REMPLISSAGE` est **inactive** (contour + tramé) et le curseur la saute ;
  `docs/UI/ECRAN22/`.
- **E21** — variante « en charge » de l'écran principal : l'en-tête porte
  l'**éclair** (x = 78, 10 × 12) à gauche du bloc batterie. C'est la seule
  différence avec E01 ; `docs/UI/ECRAN21/` (le rendu pixel est
  `tools/preview/main_charge.png`).
- Les **3 étapes du remplissage** (E11/E12/E13) partagent le même squelette :
  titre, bandeau + pointe, héros, contexte, barre d'avancement en zone
  secondaire (avec le compteur `n/3`), pied — **ni sous-titre d'étape, ni
  étiquette au-dessus du héros**. Toute évolution passe par le corps commun
  (`drawRefillStepBody` = `drawRefillHero` + `drawProgressBar`).

## Implémentation

- `src/display.cpp` : helpers + constantes de grille (`kMarginL/R`, `kCenterX`,
  `kHdrLineY`, `kStatusY`, `kTitleBannerTop/H/TextY`, `kBannerPtrHalfW/H`,
  `kHeroY`, `kGaugeLblY`, `kGaugeTop/H`, `kSecTop/Bot`, `kSecY`, `kFootLineY`,
  `kFootY1/2`, `kRefillCtx1Y/2Y`, `kRefillLabelY`, `kHoldCellW/DX`, `kHoldBarY/H`,
  `kMenuRowY0/DY/LastDY`).
- `include/display.hpp` : contrats partagés par l'app (`MenuItem`,
  `kHoldBarCells`) + documentation de chaque écran.
- `include/fonts/` : polices bitmap ; `include/config.hpp` : inclusions.
