# ESP Scale — Home Assistant : installation

Fichiers de configuration HA pour le suivi conso/coût du silo.
Chaque fichier a **une seule clé racine** → compatible avec le mode `packages`.

## Architecture : le poids publié EST le stock

La balance (firmware) est la **jauge de vérité**. Elle publie `scale/value`
avec un **cliquet « gel au mini »** : le palier publié ne remonte qu'à un refill.

- Toute **baisse ≥ 1 kg** → publiée (= consommation).
- Toute **hausse < 14 kg** (~1 sac) → **non publiée** (présumée bruit/dérive) :
  le palier reste **gelé à son minimum** — HA n'est donc jamais artificiellement
  gonflé, et une dérive réversible ne s'accumule pas en fausse conso. Le
  heartbeat, la batterie et le retry republient le palier **inchangé**.
- Toute **hausse ≥ 14 kg** hors FSM → « refill sauvage » : poids publié +
  event `scale/refill/event` avec `bag_count: 0`.

Côté HA, **aucun stock comptable** : le stock affiché = `sensor.scale_poids`.
Seule la **valeur €** est comptable (le prix ne se mesure pas).

## Déploiement (recommandé : packages)

1. Copier les fichiers de ce dossier (`ha/`) dans `<config>/packages/` de
   Home Assistant (renomme-les avec un préfixe `scale_` si tu veux les
   distinguer d'autres packages) :
   ```
   packages/
     sensors.yaml
     helpers.yaml
     templates.yaml
     utility_meter.yaml
     automations.yaml
     refill_journal.yaml
     statistics.yaml
     suivi.yaml
   ```
   ⚠️ **`sensors.yaml` n'est PAS un package** : l'intégration MQTT ne se
   configure pas via packages. Son contenu est à **fusionner dans le bloc
   `mqtt:` de `configuration.yaml`** (voir le fichier pour le copier-coller).
   `dashboard.yaml` non plus — c'est une vue à coller dans l'éditeur de
   dashboard (mode YAML), pas une config de packages.

2. Dans `configuration.yaml`, activer les packages :
   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```
3. Redémarrer HA et vérifier (Paramètres → Système → Journaux, ou
   Paramètres → Appareils → vérifier les entités "Scale").

### Déploiement automatisé

`sync.sh` pousse les packages vers l'installation distante (SSH + Docker),
avec backup auto, check config et restart :
```bash
./ha/sync.sh            # backup + copie + check config + restart HA
./ha/sync.sh --no-restart
```
Après le premier déploiement de `statistics.yaml` / `suivi.yaml` : redémarrer HA,
puis régler **Début du suivi** (`input_datetime.scale_tracking_start`) à la date
du premier jour de suivi (et après chaque remise à zéro de l'historique) : la
moyenne glissante en dépend.

Configuration par variables d'environnement ou dans `ha/sync.local.env` (ignoré
par git) : `HA_HOST` (obligatoire, `user@hôte`), `HA_PACKAGES_DIR`,
`HA_CONTAINER`, `NOTIFY_SERVICE` (remplace le placeholder de notification) :
```bash
HA_HOST=user@192.168.1.10
NOTIFY_SERVICE=notify.mobile_app_mon_telephone
```

## Initialisation (une seule fois)

Régler manuellement les helpers suivants pour partir d'un état correct :
- `input_number.scale_stock_value_eur` : ce que le stock ACTUEL (poids
  affiché par `sensor.scale_poids`) a coûté (€).
- `input_number.scale_alert_days` : seuil d'alerte (défaut 10 jours).
Le prix unitaire (`scale_unit_price_eur_kg`) se recalcule automatiquement dès
le premier mouvement (automation D).

## Comportement

- **Les mesures arrivent deux fois par jour**, 30 min avant et 30 min après
  l'aspiration quotidienne (18h30 par défaut → 18h00 et 19h00). L'horaire se règle
  dans le **portail de la balance**, et **rien n'est publié sur MQTT** : si vous
  pilotez l'aspiration depuis HA, c'est à vous de la caler sur le même horaire.
  Concrètement, une aspiration déclenchée à 17h55 est vue comme de la
  consommation du jour par l'automatisation (la mesure de 18h00 est déjà passée),
  et une aspiration à 18h10 est vue comme un **appoint** (la hausse dépasse 14 kg
  entre 18h00 et 19h00).

- **Le poids est NET** : la balance soustrait la tare du silo avant de publier
  (`scale/value`). `sensor.scale_poids` est donc le stock de granulés, pas la
  masse totale. Une tare saisie dans le portail décale **tout** l'historique de
  consommation déjà cumulé côté HA : à refaire seulement en mise en service, pas
  en réajustant le stock (c'est aussi pourquoi l'onglet Calibration prévient
  avant de modifier Z : une baisse de tare serait comptée comme de la conso).
- **Baisse de `sensor.scale_poids`** (fiable, `scale_error=off`) → conso :
  ajoutée au compteur cumulé (→ utility_meter daily/monthly/yearly kg et €),
  valeur du stock décrémentée au prix moyen pondéré.
- **`scale/refill/event` déclaré** (`bag_count > 0`) → valeur +=
  `bag_count × bag_price_eur` (coût réel des sacs achetés). Le stock kg n'est
  pas touché par l'event : c'est le poids publié par la balance (`scale/value`,
  remonté au même moment) qui fait foi.
- **Refill sauvage** (`bag_count: 0`, appoint ≥ 14 kg détecté par la balance)
  → valeur += `added_kg × prix_moyen_courant`. Journal : « appoint détecté ».
- **Jours restants** = `scale_poids` / conso moyenne sur 14 jours glissants → alerte
  quand sous le seuil.
- **Les trois alertes** (silo faible, batterie faible, plus de nouvelles) envoient
  une notification sur le téléphone **et** une notification persistante dans la
  cloche de HA. Elle se ferme toute seule quand l'alerte s'annule (silo rempli,
  batterie au-dessus de 20 %, balance qui publie de nouveau).
- **Alerte batterie faible** : notification quand `scale_batterie` passe sous
  20 %. Sous ~3,3 V la balance ne publie plus, l'alerte doit donc la précéder.
- **Alerte « plus de nouvelles »** : notification si `scale_poids` n'a rien reçu
  depuis 6 jours. La balance ne publie que sur changement, batterie ou
  heartbeat (~4 jours) : un silence de 36 h est normal.
- **Le seuil « niveau bas » de l'écran est un AUTRE réglage**, en kg et non en
  jours : il se règle dans le **portail de la balance** (onglet `REGLAGES`,
  défaut 134 kg, soit 20 % d'une capacité de 670 kg). Il ne déclenche
  **aucune notification** — c'est le bandeau `REMPLISSAGE CONSEILLE` sur
  l'e-paper. Les deux seuils sont indépendants : l'un prévient (HA, en jours),
  l'autre signale sur place (l'écran, en kg).
- **Journal des remplissages** (`refill_journal.yaml`) : chaque refill ajoute
  une entrée datée sur 2 lignes (date, détail) dans `input_text` — affiché
  sur le dashboard (dernier + historique).

## Comment le prix du kg consommé est calculé

Méthode du **coût moyen pondéré** (pas FIFO) :

1. **Au refill** : la valeur € du stock augmente du coût réel du lot =
   `bag_count × bag_price_eur` (ou `added_kg × prix_moyen` si appoint sauvage).
   Le poids, lui, est déjà remonté par la balance.
2. Le **prix unitaire courant** = `valeur_stock ÷ scale_poids` (recalculé à
   chaque mouvement par l'automation D).
3. **À chaque baisse (conso)** : on retire `|Δ| × prix_courant` de la valeur du
   stock, et on ajoute la même somme au coût cumulé consommé.

Conséquence : le kg consommé est valorisé au prix **moyen** de tout le stock
présent au moment de la conso — pas au prix précis du lot qui est brûlé. Ex. :
300 kg à 2 €/kg + 300 kg à 1 €/kg → prix moyen 1,50 €/kg ; consommer 100 kg
compte 150 €, quel que soit le lot réellement aspiré. C'est une approximation
qui lisse les achats ; le coût « cash » exact (Σ des factures de refill) est
disponible dans le **journal des remplissages**.

## Dashboard

`dashboard.yaml` : vue **« Silo »** (mode sections) à coller dans l'éditeur brut
d'un dashboard — silo qui se remplit (niveau, jours restants), consommation
jour / mois / année en kg et en €, histogrammes, valeur du stock, prix au kg,
journal des remplissages.

**Prérequis** :

- les packages de ce dossier, y compris `statistics.yaml` et `suivi.yaml`
  (moyenne glissante, jours restants) ;
- les **cartes HACS** : [Mushroom](https://github.com/piitaya/lovelace-mushroom),
  [ApexCharts Card](https://github.com/RomRider/apexcharts-card) et
  [Button Card](https://github.com/custom-cards/button-card),
  [Fluid Level Background Card](https://github.com/gadgetchnnel/lovelace-fluid-level-background-card)
  (le silo qui se remplit) et [card-mod](https://github.com/thomasloven/lovelace-card-mod),
  chargées comme
  ressources du dashboard (HACS le fait à l'installation) ;
- le silo de 670 kg (`TANK_FULL_KG`) : adapter le maximum et les seuils (134 kg
  pour le niveau bas, 268 kg pour l'orange) dans la carte du silo et dans la
  courbe de poids si la capacité est réglée autrement dans le portail.
