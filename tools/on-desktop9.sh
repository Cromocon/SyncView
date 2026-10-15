#!/bin/zsh
# Esegue un comando con le sue finestre sul desktop virtuale «Desktop 9» (KDE Plasma/KWin), senza toccare il desktop attivo.
# Uso: tools/on-desktop9.sh meson test -C build
# Carica uno script KWin temporaneo (tools/desktop9.js), lo scarica a fine comando. Verifica: journalctl --user -b -o cat | grep d9-place
D=$(cd "$(dirname "$0")" && pwd); name=syncview-d9-$$
# La trap prima del caricamento: lo script viene scaricato anche se qualcosa fallisce a metà.
trap 'qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript '$name' >/dev/null 2>&1' EXIT
id=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$D/desktop9.js" $name)
if [[ ! $id =~ '^[0-9]+$' ]]; then
  echo "on-desktop9: caricamento dello script KWin non riuscito (id: '${id}')" >&2
  exit 1
fi
qdbus6 org.kde.KWin /Scripting/Script$id org.kde.kwin.Script.run >/dev/null || exit 1
"$@"
