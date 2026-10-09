---
title: Calibration
nav_order: 5
---

# Calibration

Régler les 4 pieds pour que la balance donne le bon poids, comprendre ce qui
compte vraiment dans la mesure, et calibrer la lecture de la batterie.

## Sommaire

1. [Calibrer les pieds (portail)](#calibrer-les-pieds-portail)
2. [Ce qui compte : l'échelle, pas le zéro](#ce-qui-compte--léchelle-pas-le-zéro)
3. [Contraintes du montage](#contraintes-du-montage)
4. [Limites connues](#limites-connues)
5. [Batterie](#batterie)

## Calibrer les pieds (portail)

Onglet **CALIBRATION** du portail (voir [CONFIGURATION.md](CONFIGURATION.md#le-portail-web)).
Chaque pied a deux facteurs : **Z** (le zéro) et **C** (l'échelle, en points
bruts par kg). Ils sont stockés en NVS et appliqués **immédiatement, sans
redémarrage**.

Le tableau compare, pour chaque pied, la valeur **figée au repère** (gris) et la
valeur **actuelle** (gras), en brut et en kg, avec une colonne `Δ raw` et une
ligne **TOTAL**. La ligne du pied le plus chargé s'allume.

**Le silo est fixe** : on ne peut ni le déposer pour tarer, ni charger un pied
seul. D'où la procédure :

1. **Tare des 4 pieds** — silo en place, rien de posé. Le bouton remplit les 4
   `Z` avec la valeur brute courante ; **Enregistrer** (les pieds lisent 0 kg).
2. **Échelle d'un pied** — cliquer **« Repère (avant) »**, poser le **poids de
   test** sur ce pied, saisir le poids réellement posé en haut de page (défaut
   1,96 kg ; **~10 kg recommandé**, ex. un bidon de 10 L), puis **« Δ → C »** sur
   la ligne du pied (`C = Δraw / poids`). **Enregistrer** : le pied doit afficher
   le poids posé.
3. **Répéter** pour les 3 autres pieds.

**Contrôle** : poser le poids n'importe où — la ligne **TOTAL** doit augmenter
exactement du poids posé (la somme ne dépend pas de la répartition).

**Précision** : l'erreur relative sur C vaut celle de `Δraw` divisée par le
poids ; avec 1,96 kg on ne sollicite qu'~1 % de la capacité d'un pied (200 kg) —
assez pour vérifier, mais ~10 kg donnent une échelle nettement plus fiable.

> ⚠️ **Modifier Z décale le poids publié.** La balance ne publie que des
> baisses (voir [MQTT-HOME-ASSISTANT.md](MQTT-HOME-ASSISTANT.md#le-cliquet-de-publication)) :
> une baisse de tare serait comptée comme de la **consommation** par Home
> Assistant. La tare est une opération de **mise en service**.

Tant que rien n'est enregistré, les valeurs par défaut sont les constantes
`Z_FACTOR_x` / `C_FACTOR_x` de `config.hpp`. La sauvegarde de la calibration ne
teste pas le WiFi (une calibration de banc marche sans routeur).

## Ce qui compte : l'échelle, pas le zéro

La consommation se calcule sur des **différences** de poids :

- une erreur de **zéro** constante s'annule dans chaque différence → aucun effet
  sur la conso, seulement sur le stock affiché ;
- une erreur d'**échelle** (C) se retrouve dans **chaque** différence → biais
  proportionnel sur toute la consommation. **C'est le paramètre critique.**

## Contraintes du montage

- **4 pieds**, chacun = **4 cellules de 50 kg** + **un HX711** → 200 kg par pied,
  **800 kg** au total, pour un silo plein à `TANK_FULL_KG` = 670 kg (~84 % de la
  capacité si la charge est bien répartie).
- **Le silo ne quitte jamais sa base** (remplissage par soufflerie) et **n'est
  jamais vide** : les cellules restent chargées en permanence, le poids propre du
  silo fait partie du zéro. **Aucune référence n'est jamais atteignable** : le
  zéro est posé une fois pour toutes à la mise en service.
- Certains pieds sont **quasi inaccessibles**.
- **Le dédock ne crée pas de fausse conso** : boîtier retiré → 0 capteur sur 4 →
  rien n'est publié ; au retour, la charge est inchangée. Seul reste possible un
  petit décalage dû à un ré-appui différent du pogo.

## Limites connues

- ✅ **Conso fantôme par dérive — traitée** par le cliquet de publication : une
  dérive réversible (température, fluage) ne s'accumule plus en fausse conso.
  Seule une excursion vers le bas ≥ 1 kg reste comptée une fois.
- ✅ **Saturation d'un pied — traitée** : une lecture au-delà de la capacité
  physique (`FOOT_CAPACITY_KG` = 200 kg) invalide le pied, la mesure 4/4 échoue
  et **rien n'est publié** (plutôt qu'un faux remplissage).
- **Total négatif ramené à 0** (`sensors.cpp`) : ne concerne qu'une mesure 4/4
  dont la somme serait négative (calibration ou câblage faux) — à garder en tête
  si un câblage bouge.
- **Strict 4/4** : si un seul pied manque, la mesure est ignorée. Aucune
  extrapolation — un poids faux est pire qu'une absence.

## Batterie

Le pourcentage affiché et les seuils de protection (3,3 / 3,2 V) dépendent du
ratio du pont diviseur, `BATTERY_VOLTAGE_RATIO` (`config.hpp`) :

1. Mesurer la tension réelle de la batterie au multimètre.
2. Lire la tension vue par la carte (`readBatteryVoltage()`, logs série).
3. `ratio = tension_réelle / tension_lue × ratio_actuel`, puis recompiler.
