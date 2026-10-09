---
title: MQTT et Home Assistant
nav_order: 8
---

# MQTT et Home Assistant

Ce que la balance publie, quand, et comment Home Assistant en tire le stock, la
consommation et les coûts.

## Sommaire

1. [Les sujets MQTT](#les-sujets-mqtt)
2. [Le cliquet de publication](#le-cliquet-de-publication)
3. [Remplissages](#remplissages)
4. [Home Assistant](#home-assistant)

## Les sujets MQTT

Le broker, le port, la base des topics (`scale` par défaut) et
l'authentification se règlent dans le portail. Base `scale` :

| Topic | Exemple | Description |
|---|---|---|
| `scale/value` | `450` | Poids de granulés en kg, **entier** (retain) — voir le cliquet |
| `scale/battery` | `87` | Batterie en % (retain) |
| `scale/version` | `1.0.0` | Version du firmware (retain) |
| `scale/error/on-off` | `on` / `off` | `on` = aucun capteur ne répond : boîtier retiré **ou** panne (retain) — la cause est dans `quality.mode` |
| `scale/quality` | `{"valid":4,"mode":"ok"}` | Qualité de la mesure (retain) — `mode` = `ok` / `degraded` (1-3 pieds) / `dedock` (0/4) |
| `scale/refill/state` | `pouring` / `pricing` / `saved` | Étape du remplissage en cours, telle qu'affichée (retain) |
| `scale/refill/event` | `{"added_kg":300.5,"bag_price_eur":6.49,"bag_count":3}` | Remplissage validé (sans retain) |

- Client ID : `espscale-<mac>` ; nom réseau : `pellet-scale` (`WIFI_HOSTNAME`).
- Test rapide : `mosquitto_sub -h <broker> -t "scale/#" -v`.

## Le cliquet de publication

La balance est une **jauge qui ne descend qu'entre deux remplissages**. Le dernier
poids publié — le **palier** — est gardé en mémoire RTC (il survit au sommeil) :

- **Baisse ≥ 1 kg** (`MQTT_SEND_DELTA_KG`) → le palier descend et est publié :
  c'est de la **consommation**.
- **Hausse < 14 kg** (~1 sac) → **rien n'est publié** : bruit ou dérive
  (thermique, fluage). Le palier reste au minimum ; une dérive réversible ne
  s'accumule donc pas en fausse conso.
- **Hausse ≥ 14 kg** sans passer par le parcours de remplissage →
  **« remplissage sauvage »** : le palier est relevé et publié, plus un
  `refill/event` avec `bag_count: 0`.
- **Heartbeat** (tous les `MQTT_HEARTBEAT_CYCLES` cycles), **batterie**
  (variation ≥ 5 %) et **nouvelle tentative** après échec republient le palier
  **inchangé** (rafraîchit le retain après un redémarrage du broker).
- **Retour sur la base** après un dédock : la fin d'erreur
  (`error/on-off = off`, `quality`) est **toujours** publiée.

Résultat : Home Assistant ne voit que des **baisses** (conso) et des
**remplissages** — jamais de hausse parasite. Le seuil anti-bruit vit dans la
balance, pas dans HA. Le WiFi ne s'allume que s'il y a quelque chose à publier.

## Remplissages

Deux façons d'ajouter des granulés :

1. **Par le parcours de remplissage** sur la balance (voir
   [GUIDE-UTILISATEUR.md](GUIDE-UTILISATEUR.md)) : nombre de sacs et prix saisis,
   puis `refill/event` avec `added_kg` (poids réellement mesuré),
   `bag_price_eur` et `bag_count`. Coût = `bag_count × bag_price_eur` (fidèle à
   l'achat) ; `added_kg` est une vérification — un sac pèse rarement exactement
   son poids nominal.
2. **Sans le parcours** (ou parcours abandonné) : la hausse ≥ 14 kg est vue au
   cycle suivant comme **remplissage sauvage** (`bag_count: 0`) ; HA le valorise
   au **prix moyen courant** du stock et l'ajoute au journal comme « appoint
   détecté ». La compta reste correcte sans action.

Un parcours **n'envoie jamais** un prix non confirmé : il faut maintenir le bouton
2,5 s sur « TERMINER ? ». Après un remplissage validé, le palier est relevé au
nouveau poids : le cycle suivant ne le recompte pas en remplissage sauvage.

## Home Assistant

Le dossier [`ha/`](https://github.com/baptistev7/ESPScale/tree/main/ha/) contient toute la configuration : capteurs MQTT,
helpers (valeur €, prix moyen), automatisations (conso, remplissage, alerte),
compteurs journaliers / mensuels / annuels, consommation moyenne sur 14 jours
glissants, journal des remplissages et un dashboard « Silo » (cartes HACS :
Mushroom, ApexCharts Card, Button Card, Fluid Level Background Card, card-mod).

**Installation et déploiement (`sync.sh`)** :
[`ha/README_install.md`](https://github.com/baptistev7/ESPScale/blob/main/ha/README_install.md).

**Architecture « le poids fait foi »** : le stock affiché dans HA **est** le poids
publié (`scale_poids`) — pas de stock comptable qui pourrait dériver. Seule la
**valeur en €** est comptable (alimentée par les remplissages, consommée au prix
moyen pondéré). L'event de remplissage ne touche pas au stock en kg, seulement à
la valeur et au journal.

À savoir avant de toucher aux automatisations :

- **Le zéro du poids est celui de la calibration** : refaire la tare décale
  l'historique déjà cumulé dans HA.
- **Deux seuils distincts** : « niveau bas » de l'écran (en **kg**, réglé dans le
  portail, aucune notification) et alerte HA (`scale_alert_days`, en **jours**,
  déclenche la notification).
- **L'heure d'aspiration n'est pas publiée** : si HA pilote l'aspiration, la caler
  sur le même horaire que la balance, sinon l'aspiration est comptée en conso
  ou en appoint.
