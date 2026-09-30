#!/usr/bin/env python3
"""
Verifica statica del vincolo architetturale della modalità debug (PLAN.md):
fuori da src/core/logger.c nessun modulo deve
  - scrivere direttamente su stdout/stderr (printf, fprintf, puts, g_print, ...),
  - valutare localmente la modalità debug (SYNCVIEW_DEBUG, debug_enabled).
Così un modulo che "dimentica" il logger centrale viene scoperto da `meson test`.

Uso: check_no_adhoc_logging.py <directory src>
"""
import pathlib
import re
import sys

LOGGER_FILES = {"core/logger.c", "core/logger.h"}

FORBIDDEN = [
    (re.compile(r"(?<![\w])(printf|fprintf|vprintf|vfprintf|puts|putchar|perror|g_print|g_printerr)\s*\("),
     "output diretto su stdout/stderr: usare le funzioni log_*() di core/logger.h"),
    (re.compile(r"\b(stderr|stdout)\b"),
     "uso diretto di stderr/stdout: usare le funzioni log_*() di core/logger.h"),
    (re.compile(r"SYNCVIEW_DEBUG|debug_enabled"),
     "controllo locale della modalità debug: deve stare solo in core/logger.c"),
]


def strip_comments_and_strings(text: str) -> str:
    """Rimuove commenti e letterali stringa/char preservando i numeri di riga."""
    pattern = re.compile(
        r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.DOTALL)

    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))

    return pattern.sub(blank, text)


def main() -> int:
    root = pathlib.Path(sys.argv[1])
    violations = []

    for path in sorted(root.rglob("*")):
        if path.suffix not in (".c", ".h"):
            continue
        rel = path.relative_to(root).as_posix()
        if rel in LOGGER_FILES:
            continue

        code = strip_comments_and_strings(path.read_text(encoding="utf-8"))
        for lineno, line in enumerate(code.splitlines(), start=1):
            for regex, reason in FORBIDDEN:
                if regex.search(line):
                    violations.append(f"{rel}:{lineno}: {reason}")

    for v in violations:
        print(v, file=sys.stderr)
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
