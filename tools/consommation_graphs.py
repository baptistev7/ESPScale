#!/usr/bin/env python3
"""Graphiques de docs/CONSOMMATION.md (SVG pur, sans dépendance) -> docs/img/.

Usage : python3 tools/consommation_graphs.py [ppk2_out/cycles.csv]
Les chiffres des graphiques 2 à 5 sont ceux de docs/CONSOMMATION.md.
"""
import csv, math, os, sys

OUT = os.path.join(os.path.dirname(__file__), "..", "docs", "img")
W, H = 760, 380
C = {"cold": "#c0392b", "nopub": "#2980b9", "pub": "#e67e22", "skip": "#7f8c8d",
     "veille": "#2c3e50", "cycles": "#e67e22", "auto": "#c0392b", "ok": "#27ae60"}


def svg(name, title, body):
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, name), "w") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" '
                f'font-family="sans-serif" font-size="12"><rect width="{W}" height="{H}" '
                f'fill="#fff"/><text x="{W/2}" y="22" text-anchor="middle" font-size="15" '
                f'font-weight="bold" fill="#222">{title}</text>{body}</svg>')
    print(name)


def t(x, y, s, a="middle", fill="#333", extra=""):
    return f'<text x="{x:.1f}" y="{y:.1f}" text-anchor="{a}" fill="{fill}" {extra}>{s}</text>'


def bars(name, title, labels, values, colors, unit, fmt="{:.0f}"):
    L, R, T, B = 60, W - 20, 45, H - 60
    mx = max(values) * 1.15
    bw = (R - L) / len(values)
    b = f'<line x1="{L}" y1="{B}" x2="{R}" y2="{B}" stroke="#999"/>'
    for i, (lab, v, c) in enumerate(zip(labels, values, colors)):
        h = (B - T) * v / mx
        x = L + i * bw + bw * 0.15
        b += f'<rect x="{x:.1f}" y="{B-h:.1f}" width="{bw*0.7:.1f}" height="{h:.1f}" fill="{c}"/>'
        b += t(x + bw * 0.35, B - h - 5, fmt.format(v) + " " + unit, extra='font-weight="bold"')
        for k, line in enumerate(lab.split("\n")):
            b += t(x + bw * 0.35, B + 16 + 14 * k, line)
    svg(name, title, b)


def stacked(name, title, labels, series, unit):
    """series : [(nom, couleur, [valeurs par barre])]"""
    L, R, T, B = 60, W - 150, 45, H - 60
    tot = [sum(s[2][i] for s in series) for i in range(len(labels))]
    mx = max(tot) * 1.15
    bw = (R - L) / len(labels)
    b = f'<line x1="{L}" y1="{B}" x2="{R}" y2="{B}" stroke="#999"/>'
    for i, lab in enumerate(labels):
        y, x = B, L + i * bw + bw * 0.2
        for nom, c, vals in series:
            h = (B - T) * vals[i] / mx
            y -= h
            b += f'<rect x="{x:.1f}" y="{y:.1f}" width="{bw*0.6:.1f}" height="{h:.1f}" fill="{c}"/>'
            if h > 16:
                b += t(x + bw * 0.3, y + h / 2 + 4, f"{vals[i]:.2f}", fill="#fff")
        b += t(x + bw * 0.3, y - 5, f"{tot[i]:.2f} {unit}", extra='font-weight="bold"')
        b += t(x + bw * 0.3, B + 16, lab)
    for k, (nom, c, _) in enumerate(series):
        b += f'<rect x="{R+15}" y="{T+k*22}" width="12" height="12" fill="{c}"/>' + \
             t(R + 32, T + k * 22 + 11, nom, "start")
    svg(name, title, b)


def trace(csv_path):
    L, R, T, B = 60, W - 20, 45, H - 50
    ts, ys = [], []
    with open(csv_path) as f:
        rows = list(csv.DictReader(f))
    n = 50  # ms par pas : on garde le MAX pour ne pas perdre les pics
    for i in range(0, len(rows) - n, n):
        ts.append(i / 1000)
        ys.append(max(float(r["max_uA"]) for r in rows[i:i + n]))
    tmax = ts[-1]
    lo, hi = 1, 6  # log10 µA : 10 µA .. 1 A
    X = lambda s: L + (R - L) * s / tmax
    Y = lambda ua: B - (B - T) * (math.log10(max(ua, 10)) - lo) / (hi - lo)
    b = ""
    for d, lab in ((1, "10 µA"), (2, "100 µA"), (3, "1 mA"), (4, "10 mA"), (5, "100 mA"), (6, "1 A")):
        b += f'<line x1="{L}" y1="{Y(10**d):.1f}" x2="{R}" y2="{Y(10**d):.1f}" stroke="#eee"/>' + \
             t(L - 6, Y(10 ** d) + 4, lab, "end")
    b += f'<polyline fill="none" stroke="#2980b9" stroke-width="1" points="' + \
         " ".join(f"{X(a):.1f},{Y(v):.1f}" for a, v in zip(ts, ys)) + '"/>'
    # réveils de cette mesure (banc : timer 15 s, un cycle sur deux publie)
    for s, lab, c in ((0.5, "démarrage\nà froid", C["cold"]), (23.3, "sans publi.", C["nopub"]),
                      (40.7, "publication", C["pub"]), (61.5, "sans publi.\n(affichage évité)", C["skip"]),
                      (77.4, "publication", C["pub"]), (96.8, "sans publi.", C["nopub"])):
        for k, line in enumerate(lab.split("\n")):
            b += t(X(s) + 4, T + 12 + 13 * k, line, "start", c, 'font-size="11"')
    for s in range(0, int(tmax) + 1, 20):
        b += t(X(s), B + 16, f"{s} s")
    b += t((L + R) / 2, H - 8, "Courant (max par 50 ms, échelle log) — veille 33 µA entre les éveils")
    svg("conso_trace.svg", "Cycle complet au PPK2 (4,0 V)", b)


if __name__ == "__main__":
    trace(sys.argv[1] if len(sys.argv) > 1 else "ppk2_out/cycles.csv")
    bars("conso_cycles.svg", "Charge par cycle (mC, mesuré)",
         ["démarrage\nà froid", "sans publi.\naffichage 1,5 s", "avec publi.", "sans publi.\naff. évité",
          "avec publi.\naff. évité", "sans publi.\npartial 1,2 s"],
         [583, 139, 421, 50, 315, 120],
         [C["cold"], C["nopub"], C["pub"], C["skip"], C["skip"], C["nopub"]], "mC")
    stacked("conso_jour.svg", "Consommation par jour (mAh)",
            ["Optimiste", "Pessimiste"],
            [("Veille", C["veille"], [0.79, 0.96]), ("Cycles + 00:02", C["cycles"], [0.11, 0.42]),
             ("Autodécharge LiPo", C["auto"], [1.33, 3.33])], "mAh")
    bars("conso_autonomie.svg", "Autonomie estimée (mois)",
         ["Optimiste\nsans autodécharge", "Optimiste\navec autodécharge",
          "Pessimiste\nsans autodécharge", "Pessimiste\navec autodécharge"],
         [66, 26.5, 33, 9.8], [C["ok"], C["ok"], C["cold"], C["cold"]], "mois", "{:.0f}")
