#!/usr/bin/env python3
"""Fallisce se un test Meson e' stato saltato (SKIP, exit 77) senza essere nella lista dei permessi.

Uso: check_skips.py <testlog.json> <os>   con <os> in linux | macos | windows

Un test che esce con 77 non rompe `meson test`: senza questo controllo la CI resta verde anche quando
i test con finestra non girano (display o plugin mancanti). Ogni voce permessa spiega perche'.
"""
import json
import sys

ALLOWED_SKIPS = {
    # Tutte le piattaforme.
    "all": {
        "real_clips": "serve SYNCVIEW_TEST_CLIPS (cartella di file reali), mai impostata in CI",
    },
    "macos": {
        "main_window": "il runner macOS non ha un display attivo: il caricamento con il video nella finestra non "
                       "termina (problema aperto, test saltato sotto CI in tests/test_main_window.c; vedi PLAN.md M2.8)",
    },
    "windows": {
        "deps_dialog": "l'installatore e' un pkexec finto (script sh) che su Windows non esiste: il test esce sempre "
                       "con 77 (tests/test_deps_dialog.c)",
    },
    "linux": {},
}


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[2] not in ALLOWED_SKIPS or sys.argv[2] == "all":
        print(__doc__)
        return 2
    log_path, platform = sys.argv[1], sys.argv[2]
    allowed = {**ALLOWED_SKIPS["all"], **ALLOWED_SKIPS[platform]}

    tests = []
    with open(log_path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                tests.append(json.loads(line))
    if not tests:
        print(f"{log_path}: nessun test registrato")
        return 1

    skipped = sorted(t["name"].split(":", 1)[-1] for t in tests if t.get("result") == "SKIP")
    unexpected = [n for n in skipped if n not in allowed]
    for name in skipped:
        reason = allowed.get(name)
        print(f"SKIP {'permesso' if reason else 'NON PREVISTO'}: {name}" + (f" - {reason}" if reason else ""))
    print(f"{len(tests)} test, {len(skipped)} saltati, {len(unexpected)} non previsti")
    if unexpected:
        print("Test saltati senza motivo: display (xvfb), gtk4paintablesink o plugin GStreamer mancanti?")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
