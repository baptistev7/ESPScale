#!/usr/bin/env bash
# Captures du portail web pour la doc : compile la vraie génération de page
# (src/webconfig.cpp) contre des bouchons, écrit le HTML de chaque onglet, puis
# le capture avec Firefox headless (largeur téléphone) -> docs/screens/portal_*.png
#
#   tools/portal_preview/render.sh
#
# À relancer après toute modif de src/webconfig.cpp.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fw=$(grep -o 'FW_VERSION "[^"]*"' "$root/include/config.hpp" | cut -d'"' -f2)
g++ -std=gnu++17 -DFW_VERSION="\"$fw\"" -I"$here/stubs" -I"$root/include" \
    -o "$work/pp" "$here/portal_preview.cpp"
"$work/pp" "$work"

for tab in etat reglages cal; do
    mkdir -p "$work/prof_$tab"
    # Fenêtre très haute : Firefox capture la fenêtre, pas la page ; le fond
    # en trop est coupé par crop.py.
    firefox --headless --no-remote --profile "$work/prof_$tab" \
        --window-size=412,3200 --screenshot "$work/$tab.png" \
        "file://$work/$tab.html" >/dev/null 2>&1
done
mkdir -p "$root/docs/screens"
python3 "$here/crop.py" \
    "$work/etat.png" "$root/docs/screens/portal_etat.png" \
    "$work/reglages.png" "$root/docs/screens/portal_reglages.png" \
    "$work/cal.png" "$root/docs/screens/portal_calibration.png"
