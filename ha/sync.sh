#!/usr/bin/env bash
# =============================================================================
# ESP Scale — Synchronisation de la config Home Assistant (packages)
# =============================================================================
# Pousse la config HA versionnée dans le repo vers une installation distante
# (Raspberry Pi / NAS), puis vérifie et redémarre le conteneur Docker HA.
#
#   Repo (source de vérité)          Pi/NAS (exécution)
#   ha/sensors.yaml          ──►     <HA_PACKAGES_DIR>/scale_sensors.yaml
#   ha/helpers.yaml          ──►     <HA_PACKAGES_DIR>/scale_helpers.yaml
#   ...                                (préfixe scale_ par défaut, voir PREFIX)
#
# Usage :
#   ./sync.sh                 copie + backup + check config + restart
#   ./sync.sh --no-restart    copie + backup seulement (check/restart manuels)
#
# Prérequis :
#   - accès SSH au Pi/NAS en clé (pas de mot de passe)
#   - l'utilisateur SSH a les droits docker sur l'hôte
#   - HA_PACKAGES_DIR est le dossier packages CÔTÉ HÔTE (monté dans le
#     conteneur sur /config/packages)
# =============================================================================

set -euo pipefail

# -----------------------------------------------------------------------------
# CONFIGURATION — variables d'environnement, ou fichier ha/sync.local.env
# (ignoré par git : vos valeurs n'arrivent jamais dans le dépôt)
# -----------------------------------------------------------------------------
LOCAL_ENV="$(dirname "${BASH_SOURCE[0]}")/sync.local.env"
# shellcheck source=/dev/null
[[ -f "$LOCAL_ENV" ]] && source "$LOCAL_ENV"

HA_HOST="${HA_HOST:?définir HA_HOST (ssh user@hôte), ex. dans ha/sync.local.env}"
HA_PACKAGES_DIR="${HA_PACKAGES_DIR:-/compose/ha/config/packages}"  # dossier packages côté hôte
HA_CONTAINER="${HA_CONTAINER:-homeassistant}"                    # nom du conteneur HA
PREFIX="${PREFIX:-scale_}"                                       # préfixe fichier ("" = aucun)
BACKUP_DIR="${BACKUP_DIR:-$(dirname "$HA_PACKAGES_DIR")/backups_scale}"

# Service de notification réel (vide = on garde le placeholder
# "notify.mobile_app_<ton_telephone>" tel quel dans automations.yaml).
NOTIFY_SERVICE="${NOTIFY_SERVICE:-}"

# Fichiers à pousser (sensors.yaml est EXCLU : le bloc mqtt: ne peut pas être
# configuré via packages — les capteurs scale sont fusionnés dans le bloc
# mqtt: de configuration.yaml côté HA. dashboard.yaml est aussi exclu : il se
# gère dans l'éditeur de dashboard de HA, pas dans packages/).
FILES=(helpers templates utility_meter automations refill_journal statistics suivi)
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# -----------------------------------------------------------------------------
# Préparation : staging local (substitution des personnalisations)
# -----------------------------------------------------------------------------
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

echo "==> Préparation des fichiers (staging local)"
for f in "${FILES[@]}"; do
  src="$REPO_DIR/$f.yaml"
  dst="$STAGE/$PREFIX$f.yaml"
  if [[ -n "$NOTIFY_SERVICE" ]]; then
    sed "s|notify\.mobile_app_<ton_telephone>|$NOTIFY_SERVICE|g" "$src" > "$dst"
  else
    cp "$src" "$dst"
    if grep -q "notify.mobile_app_<ton_telephone>" "$dst"; then
      echo "    ⚠️  placeholder de notification conservé dans scale_$f.yaml"
      echo "        → définir NOTIFY_SERVICE (ex: NOTIFY_SERVICE=notify.mobile_app_pixel ./sync.sh)"
    fi
  fi
done

# -----------------------------------------------------------------------------
# Backup distant (avant écrasement)
# -----------------------------------------------------------------------------
TS="$(date +%Y%m%d_%H%M%S)"
echo "==> Backup des packages actuels sur $HA_HOST"
# Construit la liste des fichiers à sauvegarder depuis FILES
BACKUP_LIST=""
for f in "${FILES[@]}"; do
  BACKUP_LIST="$BACKUP_LIST ${PREFIX}${f}.yaml"
done
if ! ssh "$HA_HOST" "mkdir -p '$BACKUP_DIR' && cd '$HA_PACKAGES_DIR' && \
    tar czf '$BACKUP_DIR/scale_packages_$TS.tgz'$BACKUP_LIST 2>/dev/null || true"; then
  echo "ERREUR: SSH vers $HA_HOST impossible"
  exit 1
fi

# -----------------------------------------------------------------------------
# Copie des fichiers
# -----------------------------------------------------------------------------
echo "==> Copie vers $HA_HOST:$HA_PACKAGES_DIR"
rsync -az --mkpath "$STAGE"/ "$HA_HOST:$HA_PACKAGES_DIR/" || {
  echo "ERREUR: rsync impossible (rsync installé des deux côtés ?)";
  exit 1;
}

# -----------------------------------------------------------------------------
# Check de config + restart (sauf --no-restart)
# -----------------------------------------------------------------------------
if [[ "${1:-}" == "--no-restart" ]]; then
  echo "==> Terminé (sans restart). Relance HA manuellement puis vérifie"
  echo "    Paramètres → Système → Journaux."
  exit 0
fi

echo "==> Vérification de la configuration dans le conteneur"
if ! ssh "$HA_HOST" "docker exec '$HA_CONTAINER' python -m homeassistant \
      --script check_config -c /config >/dev/null 2>&1"; then
  echo "!! CHECK CONFIG EN ERREUR — HA n'a PAS été redémarré."
  echo "   Rollback possible :"
  echo "   ssh $HA_HOST 'tar xzf $BACKUP_DIR/scale_packages_$TS.tgz -C $HA_PACKAGES_DIR'"
  exit 1
fi
echo "    Config OK"

echo "==> Redémarrage du conteneur $HA_CONTAINER"
ssh "$HA_HOST" "docker restart '$HA_CONTAINER'"

echo "==> Terminé. HA redémarre (~30-60 s) — vérifie sur http://$(echo "$HA_HOST" | cut -d@ -f2):8123"
