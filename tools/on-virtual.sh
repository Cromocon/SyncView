#!/bin/zsh
# Esegue un comando con le finestre GTK in un KWin Wayland VIRTUALE e isolato (nessuna finestra sullo schermo dell'utente,
# nessun richiamo dell'attenzione, nessun furto del focus). Uso: tools/on-virtual.sh meson test -C build deps_dialog
# Se il compositor non parte, il comando non viene eseguito (mai ripiego sulla sessione reale).
sock=syncview-virt-$$
rt=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}
kwin_wayland --virtual --no-lockscreen --no-global-shortcuts --socket $sock --width 1280 --height 800 >/tmp/$sock.log 2>&1 &
kpid=$!
trap 'kill $kpid 2>/dev/null; rm -f $rt/$sock $rt/$sock.lock /tmp/$sock.log' EXIT
for i in {1..50}; do [[ -S $rt/$sock ]] && break; sleep 0.1; done
[[ -S $rt/$sock ]] || { echo "on-virtual: KWin virtuale non partito" >&2; cat /tmp/$sock.log >&2; exit 1; }
WAYLAND_DISPLAY=$sock GDK_BACKEND=wayland DISPLAY= "$@"
