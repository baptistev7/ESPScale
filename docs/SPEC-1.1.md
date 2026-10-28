---
title: Spécification 1.1
nav_order: 11
---

# SPEC 1.1 — Remplissage sans réseau, comptage automatique des sacs

**Statut** : spécification de la version 1.1, réalisée dans le firmware 1.1.0 (voir le
CHANGELOG). Elle remplace, pour le
remplissage, les parties de [SPEC-ecrans.md](SPEC-ecrans.md) et de
[MQTT-HOME-ASSISTANT.md](MQTT-HOME-ASSISTANT.md) qu'elle cite. **Pourquoi une 1.1** :
le contrat MQTT change (disparition de `scale/refill/state`).

## 1. Constat (balance 1.0.4)

- Chaque changement d'étape publie un état (`pouring`, `pricing`) : WiFi + MQTT
  rallumés avant d'afficher l'écran suivant, plusieurs secondes d'attente.
- Home Assistant n'utilise pas ces états : ses automatisations reposent sur le
  saut de poids et sur l'événement `scale/refill/event`.
- Le nombre de sacs se compte à la main, un clic par sac.

## 2. Parcours sans réseau

- Du début du remplissage jusqu'à « TERMINER ? » inclus, **aucune session réseau**.
  Les écrans changent aussitôt, y compris « CORRIGER ? » et l'abandon.
- L'abandon (délai, `ANNULER`, Δ négatif) ne publie rien. Le cycle de mesure normal
  qui suit publie le poids ; HA détecte le saut comme un appoint.
- Un redémarrage anormal reste signalé sur `scale/diag`, sans état de remplissage.

## 3. Envoi : trois écrans

Au maintien de « TERMINER ? » (inchangé, 2,5 s) :

1. **« ENVOI EN COURS »** — écran **complet**, dessiné **avant** d'ouvrir le WiFi
   (charte actuelle : en-tête, bandeau de titre, barre de progression, pas de pied
   de page). Aucun geste accepté.
2. **Envoi** — une seule session : WiFi, NTP si nécessaire, MQTT, publication du
   poids « avant » (seulement si le silo a perdu ≥ 1 kg depuis le dernier poids
   publié : la consommation écoulée ne doit pas être perdue ; jamais sur un nouvel
   essai), du poids « après », de la batterie, de la qualité et de
   `scale/refill/event`, puis coupure.
   Délai total environ **20 s** (WiFi 15 s au plus, puis MQTT 5 s).
3. **« ENREGISTRÉ »** — écran existant. Les appuis restent ignorés pendant ses
   5 premières secondes.

**Échec** (WiFi, broker ou délai dépassé, ou un message non parti) : écran **« ENVOI ÉCHOUÉ »**.
`● COURT RÉESSAYER` relance l'étape 2 avec les mêmes données ; `— LONG ABANDONNER`
revient à l'écran principal. Le remplissage est alors vu par HA au saut de poids
(appoint, sans sacs ni prix). Fin de session sans geste : 10 min, comme les autres
écrans du parcours.

Pas de réessai automatique : le premier échec affiche l'écran.

## 4. Suppression de l'état de remplissage

- Le topic `scale/refill/state` n'existe plus : ni publié, ni documenté. Après un
  redémarrage anormal, plus rien à republier : `scale/diag` suffit.
- Home Assistant : suppression du capteur `sensor.scale_etat_remplissage`
  (`ha/sensors.yaml`), de sa ligne du dashboard et des mentions dans le README HA.
  Rien d'autre ne le lit.
- Le message retenu laissé sur les brokers des installations existantes est à
  effacer à la main (publier un message vide, retenu) : à dire dans la note de
  version.
- `scale/refill/event` et `scale/diag` ne changent pas.

## 5. Comptage automatique des sacs

**Hypothèses** : chaque sac est vidé à la main ; un sac ne pèse **jamais moins** de
15 kg ; entre deux sacs, **3 à 4 s au minimum** s'écoulent ; le poids monte d'un
coup ou en quelques secondes.

### 5.1 Détection de paliers

- Pendant le versement, le poids est lu **en tâche de fond environ toutes les
  secondes**, sans rafraîchir l'écran (le rafraîchissement d'affichage reste au
  rythme actuel).
- Un **palier** est atteint quand deux lectures consécutives, à ≥ 2 s d'écart,
  diffèrent de moins de **0,4 kg**.
- La hausse `Δ` entre deux paliers consécutifs s'ajoute à un cumul `acc`. Dès que
  `acc ≥ poids_min_sac − 0,5`, on compte :

  `sacs += floor((acc + 0,5) / poids_min_sac)`, puis `acc = 0`.

  Le reste est **jeté** : il correspond au surpoids des sacs.
- Un palier inférieur à un sac (sac versé en deux fois, appui sur le couvercle) reste
  dans `acc` et s'ajoute au suivant.
- Une baisse du poids vient en déduction du cumul (un appui sur le couvercle qui
  se relâche ne doit pas compter), sans jamais le rendre négatif ; le contrôle du
  Δ négatif de la mesure « après » est inchangé.

**Pourquoi le plancher** : avec +1 kg par sac, `floor(k × 16 / 15)` reste exact
jusqu'à 14 sacs versés d'un coup. Découper aux paliers évite l'accumulation du
surpoids sur un remplissage de 15 sacs et plus. La somme des planchers par palier
ne dépasse jamais le plancher du total : c'est la valeur la plus prudente.

**Le `+0,5`** absorbe le bruit des capteurs (un sac de 15,0 kg lu à 14,8 kg).

### 5.2 Parcours

1. Versement : l'écran montre les kg en direct et **`~ N sacs`** (paliers déjà
   détectés).
2. `— LONG` = « j'ai fini ». La balance attend un palier final stable, puis fait la
   mesure « après ». Le total mesuré fixe le Δ ; le nombre de sacs est celui des
   paliers.
3. **« NOMBRE DE SACS »** s'ouvre avec la valeur calculée. `— LONG` valide ;
   `● COURT` fait **−1**, jusqu'à 1. Au-delà, un tour revient à la valeur calculée
   +3 puis redescend (cas d'un sac manquant, rare).
4. Prix du sac, puis « TERMINER ? » (récap sacs, kg et coût).

Le réglage `bag_kg` du portail devient le **poids minimum d'un sac**, 15 par défaut.
Le coût reste `sacs × prix du sac` (inchangé).

### 5.3 Valeurs retenues (estimées, à affiner sur le matériel)

| Paramètre | Valeur | Origine |
|---|---|---|
| Cadence de lecture en tâche de fond | 1 s | pauses de 3 à 4 s entre sacs |
| Durée d'une lecture des 4 capteurs | ≈ 0,4 s | HX711 à 10 mesures/s, moyenne de 4 |
| Bruit de lecture à l'arrêt | ≈ ±0,15 kg | estimé, 4 capteurs sommés |
| Tolérance de palier | 0,4 kg | ≈ 2,5 × le bruit |
| Écart entre les deux lectures d'un palier | ≥ 2 s | stabilisation du silo après un versement : ≈ 1 à 1,5 s |
| Marge ajoutée au plancher | 0,5 kg | bruit + dérive pendant le remplissage |
| Poids minimum d'un sac | 15 kg | donné |
| Pause minimale entre deux sacs | 3 à 4 s | donné |

Ces valeurs sont des constantes du firmware, regroupées au même endroit. Les affiner
= enregistrer les lectures d'un vrai remplissage (sacs enchaînés, sacs avec pauses,
un sac versé en deux fois) et rejouer la règle de 5.1 dessus.

## 6. Home Assistant

- Aucune automatisation à changer : la détection des appoints et la correction du
  prix du dernier remplissage ne dépendent pas de l'état supprimé.
- *Optionnel* : helper « poids minimum d'un sac » pour afficher `~ N sacs` dans le
  journal des appoints détectés.

## 7. Documentation à mettre à jour avec la 1.1

[SPEC-ecrans.md](SPEC-ecrans.md) (écrans de fin de parcours, comptage),
[GUIDE-UTILISATEUR.md](GUIDE-UTILISATEUR.md), [FONCTIONNEMENT.md](FONCTIONNEMENT.md),
[MQTT-HOME-ASSISTANT.md](MQTT-HOME-ASSISTANT.md), `ha/sensors.yaml`, le dashboard et
`ha/README_install.md`, les images d'écran (`tools/render_screens.py` : « ENVOI EN
COURS », « ENVOI ÉCHOUÉ »), le CHANGELOG avec la note de migration du topic supprimé.

## 8. Hors périmètre

Seuil de l'appui long (1 s), mode automatique de fin de remplissage, apprentissage du
poids d'un sac.

## 9. Réalisation (état dans le firmware `1.1.0-dev`)

| Point de la spec | Où | Écarts |
|---|---|---|
| §2 parcours sans réseau | `src/app.cpp` : plus de `publishRefillState`, `refillAbandon` ne publie rien | — |
| §3 envoi | `refillSave()` : `displayShowRefillSending` → `publishRefillSaved` → `displayShowRefillSendFailed` si échec ; `src/display.cpp` | l'envoi réussit seulement si **tous** les messages sont partis, événement compris |
| §4 état supprimé | `src/mqtt.cpp`, `ha/sensors.yaml`, `ha/dashboard.yaml`, docs | le message retenu est à effacer à la main (note du CHANGELOG) |
| §5 comptage | `src/bagcount.cpp`, `include/bagcount.hpp` (constantes), test hôte `tools/bagcount_test.cpp` | une baisse **déduit** du cumul (sans le rendre négatif) au lieu d'être ignorée ; `~` et `-` à la place de `≈` et `−` (polices) |
| §5.2 parcours | `refillPourAndSave` → `refillFinishAndSave` : versement, mesure « après », sacs, prix, récap | l'étape « NOMBRE DE SACS » vient **après** la mesure « après » (avant : après le prix) |

Règles de reprise : un dédock avant la mesure « après » perd les paliers ; le compte
se déduit alors du seul delta (`bagCountFromDelta`). Le compte automatique ne
dépasse jamais `floor((delta + 0,5) / poids_min_sac)`.

## 10. Recette avant la publication 1.1.0

**État à la publication (2026-10-28)** — vérifié sur la balance : comptage de 10 et
de 6 sacs (15,1 kg par sac), parcours complet sans réseau, « ENVOI EN COURS » puis
« ENREGISTRÉ » (journal série), « ENREGISTRÉ » qui part seul au bout de 20 s, écran
« EN COURS » qui ne se fige plus. **Non vérifié** : envoi du poids « avant » (aucune
baisse ≥ 1 kg à ce moment), échec d'envoi (« ENVOI ÉCHOUÉ », réessai, abandon),
sacs trop remplis, sac versé en deux fois, appui sur le couvercle, dédock en cours
de versement, les non-régressions du §10.4 sur plusieurs jours.

Matériel : balance sur sa base, branchée en USB, capture série et `scale/#` sur le
broker (ouvrir le port série redémarre la carte : attendre la fin du cycle avant
de commencer). Passer ensuite `FW_VERSION` à `1.1.0`.

### 10.1 Comptage des sacs (sur un vrai remplissage)

- [ ] Sacs enchaînés vite (pauses de 3 à 4 s) : `~ N sacs` monte sacs après sacs et
      le nombre prérempli est le bon.
- [ ] Sacs avec de longues pauses (≥ 10 s) : même résultat.
- [ ] Un sac versé en deux fois : un seul sac compté.
- [ ] Un appui sur le couvercle ou le silo pendant le versement : pas de sac en
      plus.
- [ ] Sacs trop remplis (+1 kg chacun) : le compte reste juste ; `● COURT` retire
      un sac si besoin.
- [ ] Au moins 10 sacs d'affilée : pas de dérive du compte.
- [ ] Relever la série des poids (journal série, une ligne par seconde) pour
      affiner `kBagPlateauEpsKg`, `kBagPlateauMs` et `kBagMarginKg`
      (`include/bagcount.hpp`). **Rejouer la règle sur ces relevés** avant de
      changer une constante.
- [ ] `— LONG` pour terminer : la balance attend le palier final (3 à 10 s) et la
      mesure « après » n'est pas faussée par la main sur le silo.

### 10.2 Parcours et écrans

- [ ] Aucun message MQTT entre le début et le maintien de « TERMINER ? »
      (`mosquitto_sub -t 'scale/#'`) : seul le démarrage de session de l'écran
      principal peut publier.
- [ ] Changements d'écran instantanés (étape 1 → sacs → prix → récap).
- [ ] `● COURT` sur l'étape 1 est inerte ; le pied n'annonce que `— LONG TERMINER`.
- [ ] Étape sacs : `- 1 SAC` jusqu'à 1, puis retour à (valeur calculée + 3).
- [ ] « CORRIGER ? » → nombre de sacs / prix : retour au récap sans re-mesure.
- [ ] Maintien de 2,5 s : « ENVOI EN COURS » apparaît **avant** le WiFi, puis
      « ENREGISTRÉ ». Mesurer la durée de l'envoi.
- [ ] « ENREGISTRÉ » : les 5 premières secondes ignorent les appuis.
- [ ] Consommation avant remplissage : laisser brûler (ou verser un poids qui
      baisse) puis lancer un remplissage : `scale/value` reçoit d'abord le poids
      « avant », HA compte la baisse en consommation, puis le remplissage.
- [ ] Échec simulé (WiFi coupé, ou broker arrêté) : « ENVOI ÉCHOUÉ ». `● COURT`
      réessaie (envoi qui réussit après le retour du réseau) ; `— LONG` abandonne
      et revient à l'écran principal.
- [ ] Abandon par délai (10 min) et `ANNULER` : rien n'est publié par le parcours.
- [ ] Dédock pendant le versement puis redock : la reprise fait la mesure
      « après » ; le nombre de sacs se déduit du delta.
- [ ] Les écrans physiques correspondent aux images de `docs/screens/`
      (`refill_1`, `refill_2`, `refill_send`, `refill_send_failed`).

### 10.3 Home Assistant

- [ ] Remplissage déclaré : `refill/event` reçu, journal et détail du dernier
      remplissage à jour, prix au kg ajouté = `sacs × prix ÷ kg`.
- [ ] Échec puis abandon : HA enregistre un appoint (saut de poids, prix courant).
- [ ] Retirer le capteur « Scale État remplissage » de `configuration.yaml`,
      effacer le message retenu (`mosquitto_pub -r -n -t <base>/refill/state`),
      redémarrer HA : plus d'entité, dashboard sans la ligne « État du remplissage ».
- [ ] Pas d'erreur dans le journal de HA au démarrage.

### 10.4 Non-régression

- [ ] Cycles de 18h00 et 19h00 : publication si changement, bandeau `ASPIRATION`
      entre les deux, « MESURÉ LE jj/mm » le lendemain.
- [ ] Portail : le réglage s'appelle « Poids minimum d'un sac », enregistrement et
      rechargement conservent la valeur.
- [ ] `scale/diag` absent après un démarrage normal.
- [ ] Autonomie : une session de remplissage ne laisse pas le WiFi allumé
      (courant de veille inchangé après le retour à l'écran principal).

### 10.5 Publication

Compilation propre, binaire à `1.1.0`, CHANGELOG (section `[1.1.0]` datée, note de
migration du topic), procédure de publication de `AGENTS.md`, puis vérification du
dépôt public (un commit, release « Latest »).
