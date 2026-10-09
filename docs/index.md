---
title: Accueil
nav_order: 1
---

# ESP Scale — balance connectée pour silo à granulés

Une balance sur batterie qui pèse un **silo à granulés** posé sur 4 pieds, affiche
le stock sur un **écran e-paper** et le publie en **MQTT** pour **Home
Assistant**. Elle dort presque tout le temps : deux mesures par jour, calées sur
l'aspiration de la chaudière.

| Écran principal | Hors de sa base (recharge) | Portail de réglages |
|---|---|---|
| <img src="screens/main.png" width="150" alt="Écran principal"> | <img src="screens/nomade.png" width="150" alt="Mode nomade"> | <img src="screens/portal_reglages.png" width="220" alt="Portail web, onglet RÉGLAGES"> |

## Par où commencer

1. [Matériel](MATERIEL.md) — ce qu'il faut, le câblage et les modifications de la carte.
2. [Configuration](CONFIGURATION.md) — compiler, flasher, portail WiFi / MQTT.
3. [Calibration](CALIBRATION.md) — pieds et batterie.
4. [MQTT et Home Assistant](MQTT-HOME-ASSISTANT.md) — les capteurs prêts à l'emploi.
5. [Guide utilisateur](GUIDE-UTILISATEUR.md) — l'usage au quotidien.

## Comprendre et mesurer

- [Fonctionnement](FONCTIONNEMENT.md) — cycle de mesure, modes, écrans.
- [Consommation](CONSOMMATION.md) — banc PPK2, mesures, autonomie estimée (10 à
  26 mois), graphiques.
- [Spécification des écrans](SPEC-ecrans.md) — chaque écran e-paper et ses transitions.
- [Dépannage](DEPANNAGE.md).

Le code et l'historique : [dépôt GitHub](https://github.com/baptistev7/ESPScale),
[journal des modifications](https://github.com/baptistev7/ESPScale/blob/main/CHANGELOG.md).
