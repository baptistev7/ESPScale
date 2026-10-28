# AGENTS.md — Notes pour agents/outils de dev

## Commandes PlatformIO

Le binaire système `/usr/bin/pio` est cassé (incompatible avec le Python système).
Toujours utiliser l'environnement PlatformIO dédié :

```bash
# Compilation
~/.platformio/penv/bin/pio run

# Flash USB
~/.platformio/penv/bin/pio run --target upload

# Monitor série
~/.platformio/penv/bin/pio device monitor
```

## Divers

- La configuration réseau (WiFi + MQTT) n'est **plus dans le code** : elle est
  stockée en NVS et saisie via le portail web embarqué (voir
  `docs/CONFIGURATION.md`). La carte sans config démarre en portail.
- La carte apparaît par son identifiant stable dans `platformio.ini`
  (`/dev/serial/by-id/…`) : un PPK2 branché occupe `ttyACM0`.
- Mesures de consommation au PPK2 (source 4,0 V, USB de la carte débranché) :
  méthode dans `docs/CONSOMMATION.md` (le script de mesure n'est pas dans le dépôt).
- Fichiers CAO du boîtier dans `case/v1/` (boîtier vertical + base) :
  `PelletScale.3mf` = projet PrusaSlicer général (objets `Main`, `BackMain`,
  `Support`, `BackSupport`) ; un 3mf par pièce à côté (`Main` = boîtier face
  avant, `BackMain` = son dos, `Support` = base, `BackSupport` = dos de la base).
  Les maillages sont modifiables par script Python (trimesh + manifold3d
  installés via pip3)
