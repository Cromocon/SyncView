#!/usr/bin/env python3
"""Strumento dei token di SyncView.

  tokens_tool.py build   scrive design/generated/syncview-{light,dark}.css (colori + componenti) da tokens.json e components.css.tpl
  tokens_tool.py check   verifica i contrasti (AA) e che i file generati siano aggiornati; esce con 1 se qualcosa non va

I derivati (tinte morbide, testo sui colori, testo tenue) si calcolano qui con le stesse regole usate da Claude Design nel
prototipo, così tokens.json contiene solo i valori di base.
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent
TOKENS = ROOT / "tokens.json"
TEMPLATE = ROOT / "components.css.tpl"
OUT = ROOT / "generated"

INK = "#0B0F14"
WHITE = "#FFFFFF"


def rgb(h):
    n = int(h[1:], 16)
    return [n >> 16 & 255, n >> 8 & 255, n & 255]


def to_hex(c):
    return "#" + "".join(f"{round(max(0, min(255, v))):02X}" for v in c)


def lin(v):
    v /= 255
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def luminance(h):
    r, g, b = (lin(v) for v in rgb(h))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    x, y = luminance(a), luminance(b)
    return (max(x, y) + 0.05) / (min(x, y) + 0.05)


def mix(a, b, p):
    return to_hex([x * p + y * (1 - p) for x, y in zip(rgb(a), rgb(b))])


def best_on(bg):
    return INK if contrast(INK, bg) >= contrast(WHITE, bg) else WHITE


def tint(color, soft, text):
    """Il colore più vicino a `color` che resta leggibile (>= 4,6:1) sulla tinta morbida."""
    for step in range(20, -1, -1):
        m = mix(color, text, step / 20)
        if contrast(m, soft) >= 4.6:
            return m
    return text


def derive(theme):
    c = dict(theme["colors"])
    for k in ("accent", "ok", "err", "warn"):
        c["on_" + k] = best_on(c[k])
    c["accent_soft"] = mix(c["accent"], c["surf"], 0.22)
    c["ok_soft"] = mix(c["ok"], c["surf"], 0.16)
    c["err_soft"] = mix(c["err"], c["surf"], 0.16)
    c["warn_soft"] = mix(c["warn"], c["surf"], 0.18)
    for k in ("ok", "err", "warn"):
        c[k + "_text"] = tint(c[k], c[k + "_soft"], c["text"])
    for i, ch in enumerate(theme["channels"]):
        letter = "abcd"[i]
        c["ch_" + letter] = ch
        c["on_ch_" + letter] = best_on(ch)
    return c


def pairs(c):
    """(nome, primo piano, sfondo, minimo): 4,5 per il testo, 3 per gli elementi grafici."""
    p = [
        ("Testo / sfondo", c["text"], c["bg"], 4.5),
        ("Testo / superficie", c["text"], c["surf"], 4.5),
        ("Testo / superficie 2", c["text"], c["surf2"], 4.5),
        ("Secondario / sfondo", c["mute"], c["bg"], 4.5),
        ("Secondario / superficie", c["mute"], c["surf"], 4.5),
        ("Secondario / superficie 2", c["mute"], c["surf2"], 4.5),
        ("Su accento (Play)", c["on_accent"], c["accent"], 4.5),
        ("Su successo", c["on_ok"], c["ok"], 4.5),
        ("Testo successo / tenue", c["ok_text"], c["ok_soft"], 4.5),
        ("Su errore", c["on_err"], c["err"], 4.5),
        ("Testo errore / tenue", c["err_text"], c["err_soft"], 4.5),
        ("Su avviso", c["on_warn"], c["warn"], 4.5),
        ("Testo avviso / tenue", c["warn_text"], c["warn_soft"], 4.5),
        ("Testo / accento morbido (filtri attivi)", c["text"], c["accent_soft"], 4.5),
        ("Overlay video (tempo)", WHITE, "#000000", 4.5),
        ("Accento / sfondo (anello focus)", c["accent"], c["bg"], 3.0),
        ("Bordo / sfondo", c["border"], c["bg"], 3.0),
        ("Bordo / superficie", c["border"], c["surf"], 3.0),
        ("Playhead / righello", c["text"], c["bg"], 3.0),
        ("Marker Colpo / righello", c["err"], c["bg"], 3.0),
    ]
    for letter in "abcd":
        p.append((f"Etichetta canale {letter.upper()}", c["on_ch_" + letter], c["ch_" + letter], 4.5))
    return p


def placeholders(theme, tokens):
    """Valori per i {{segnaposto}} del modello dei componenti."""
    m = tokens["metrics"]
    sizes = m["font_sizes_px"]
    values = {
        "bw": theme["border_width"],
        "fs_s": sizes[0], "fs": sizes[1], "fs_m": sizes[2], "fs_l": sizes[3], "fs_xl": sizes[4],
        "ring": m["focus_ring_px"]["ring"], "gap": m["focus_ring_px"]["gap"],
    }
    for i, v in enumerate(m["spacing_px"], start=1):
        values[f"sp{i}"] = v
    for k, v in m["radii_px"].items():
        values["r_" + k] = v
    return values


def render_components(theme, tokens):
    values = placeholders(theme, tokens)
    text = TEMPLATE.read_text(encoding="utf-8")
    missing = sorted(set(re.findall(r"\{\{(\w+)\}\}", text)) - set(values))
    if missing:
        raise SystemExit("segnaposto sconosciuti in components.css.tpl: " + ", ".join(missing))
    return re.sub(r"\{\{(\w+)\}\}", lambda mo: str(values[mo.group(1)]), text)


def css_for(name, theme, tokens):
    c = derive(theme)
    m = tokens["metrics"]
    lines = [
        "/* GENERATO da design/tokens_tool.py a partire da design/tokens.json: non modificare a mano. */",
        f"/* {theme['label']} */",
        "",
    ]
    for key, value in c.items():
        lines.append(f"@define-color syncview_{key} {value};")
    lines += [
        "",
        f"/* Metriche (GTK CSS non ha variabili: i valori si copiano nei selettori dei widget) */",
        f"/*   bordo: {theme['border_width']}px; carattere: {m['font_family']}; dimensioni px: "
        + " ".join(str(s) for s in m["font_sizes_px"])
        + "; spaziatura px: "
        + " ".join(str(s) for s in m["spacing_px"])
        + " */",
        "/*   raggi px: " + " ".join(f"{k}={v}" for k, v in m["radii_px"].items())
        + f"; anello di focus: {m['focus_ring_px']['ring']}px + {m['focus_ring_px']['gap']}px di distacco */",
        "",
    ]
    return "\n".join(lines) + "\n" + render_components(theme, tokens)


def load():
    return json.loads(TOKENS.read_text(encoding="utf-8"))


def build(tokens):
    OUT.mkdir(exist_ok=True)
    for name, theme in tokens["themes"].items():
        (OUT / f"syncview-{name}.css").write_text(css_for(name, theme, tokens), encoding="utf-8")


def check(tokens):
    failed = []
    for name, theme in tokens["themes"].items():
        c = derive(theme)
        for label, fg, bg, minimum in pairs(c):
            ratio = contrast(fg, bg)
            if ratio < minimum:
                failed.append(f"[{name}] {label}: {ratio:.2f}:1 < {minimum}:1 ({fg} su {bg})")
        expected = css_for(name, theme, tokens)
        path = OUT / f"syncview-{name}.css"
        if not path.exists() or path.read_text(encoding="utf-8") != expected:
            failed.append(f"[{name}] {path.name} non aggiornato: esegui tokens_tool.py build")
    return failed


def main(argv):
    if len(argv) != 2 or argv[1] not in ("build", "check"):
        print(__doc__)
        return 2
    tokens = load()
    if argv[1] == "build":
        build(tokens)
        print("generati:", ", ".join(f"syncview-{n}.css" for n in tokens["themes"]))
        return 0
    failed = check(tokens)
    for line in failed:
        print(line)
    if not failed:
        print("token OK: contrasti AA e file generati aggiornati")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
