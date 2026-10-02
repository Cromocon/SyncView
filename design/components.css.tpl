/*
 * Foglio di stile dei componenti di SyncView (direzione 1b «Strumento di precisione»).
 * Modello: tokens_tool.py sostituisce i segnaposto a doppia graffa con le metriche di tokens.json e antepone i colori (@syncview_*).
 * GTK CSS non ha variabili per misure e raggi, quindi i numeri entrano qui al momento della generazione.
 */

window.syncview {
  background-color: @syncview_bg;
  color: @syncview_text;
  font-family: monospace;
  font-size: {{fs}}px;
}

window.syncview * {
  font-variant-numeric: tabular-nums;
}

/* Anello di focus: 3 px di accento, staccato di 2 px (mai solo colore: ha spessore e distacco). */
window.syncview *:focus-visible {
  outline: {{ring}}px solid @syncview_accent;
  outline-offset: {{gap}}px;
}

/* ---- Barra del titolo ---- */
window.syncview headerbar {
  background: @syncview_surf;
  color: @syncview_text;
  border-bottom: {{bw}}px solid @syncview_border;
  box-shadow: none;
  min-height: 42px;
  padding: 0 {{sp3}}px;
}
window.syncview headerbar .sv-title {
  font-weight: 800;
}
window.syncview headerbar .sv-subtitle {
  color: @syncview_mute;
}
window.syncview headerbar button.titlebutton {
  min-width: 28px;
  min-height: 26px;
  border-radius: {{r_control}}px;
  border: {{bw}}px solid @syncview_line;
  background: transparent;
  color: @syncview_text;
  box-shadow: none;
}
window.syncview headerbar button.titlebutton:hover {
  background: @syncview_surf2;
}

/* ---- Pulsanti ---- */
window.syncview button.sv-btn {
  min-height: 32px;
  padding: 0 {{sp3}}px;
  border-radius: {{r_pill}}px;
  border: {{bw}}px solid @syncview_border;
  background: @syncview_surf2;
  color: @syncview_text;
  font-weight: 700;
  box-shadow: none;
}
window.syncview button.sv-btn:hover {
  background: @syncview_surf;
}
window.syncview button.sv-btn:active {
  background: @syncview_line;
}
window.syncview button.sv-btn:disabled {
  color: @syncview_mute;
  border-color: @syncview_line;
  background: transparent;
}

/* Azione primaria (Play): l'accento è riservato a focus, Play e selezione. */
window.syncview button.sv-primary {
  background: @syncview_accent;
  border-color: @syncview_accent;
  color: @syncview_on_accent;
  font-weight: 800;
  padding: 0 {{sp4}}px;
}
window.syncview button.sv-primary:hover {
  background: @syncview_accent;
  opacity: 0.9;
}
window.syncview button.sv-primary:disabled {
  background: @syncview_line;
  border-color: @syncview_line;
  color: @syncview_mute;
}

/* Passo (−10 −1 +1 +10) */
window.syncview button.sv-step {
  min-height: 26px;
  min-width: 30px;
  padding: 0 {{sp1}}px;
  border-radius: {{r_control}}px;
  border: {{bw}}px solid @syncview_border;
  background: @syncview_surf2;
  color: @syncview_text;
  font-weight: 700;
  box-shadow: none;
}
window.syncview button.sv-step:hover {
  background: @syncview_surf;
}
window.syncview button.sv-step:disabled {
  color: @syncview_mute;
  border-color: @syncview_line;
  background: transparent;
}

/* ---- Riquadro video ---- */
.sv-tile {
  background: #000000;
  border: {{bw}}px solid @syncview_line;
  border-radius: {{r_card}}px;
}
.sv-video {
  background: #000000;
  border-radius: {{r_card}}px;
}

/* Etichette sul video: sempre testo chiaro su nero, leggibili su qualunque immagine. */
.sv-chip {
  background: rgba(0, 0, 0, 0.78);
  color: #FFFFFF;
  border-radius: {{r_pill}}px;
  padding: 2px {{sp2}}px;
  font-size: {{fs_s}}px;
  font-weight: 800;
}
.sv-chip-time {
  font-size: {{fs_m}}px;
  padding: 2px {{sp2}}px;
}
.sv-chip-ch-a {
  background: @syncview_ch_a;
  color: @syncview_on_ch_a;
}
.sv-chip-end {
  background: @syncview_surf;
  color: @syncview_text;
  border: {{bw}}px solid @syncview_line;
}

/* ---- Schede di stato (nessun video, caricamento, errore) ---- */
.sv-card {
  background: @syncview_surf;
  color: @syncview_text;
  border: {{bw}}px solid @syncview_line;
  border-radius: {{r_card}}px;
  padding: {{sp4}}px {{sp5}}px;
}
.sv-card .sv-card-title {
  font-weight: 800;
  font-size: {{fs_m}}px;
}
.sv-card .sv-card-detail {
  color: @syncview_mute;
}
.sv-card.sv-error {
  border-color: @syncview_err;
  background: @syncview_err_soft;
}
.sv-card.sv-error .sv-card-title {
  color: @syncview_err_text;
}
.sv-card.sv-error .sv-card-detail {
  color: @syncview_text;
}
.sv-card progressbar trough {
  min-height: 6px;
  margin: 0;
  padding: 0;
  border-radius: {{r_pill}}px;
  background: @syncview_line;
  border: 0 solid transparent;
}
.sv-card progressbar progress {
  min-height: 6px;
  margin: 0;
  padding: 0;
  border-radius: {{r_pill}}px;
  background: @syncview_text;
  border: 0 solid transparent;
}

/* ---- Tempo e timeline ---- */
window.syncview .sv-time {
  font-size: {{fs_xl}}px;
  font-weight: 800;
}
window.syncview .sv-time-total {
  color: @syncview_mute;
}

window.syncview scale.sv-seek trough {
  min-height: 6px;
  margin: 0;
  padding: 0;
  border-radius: {{r_pill}}px;
  background: @syncview_line;
  border: 0 solid transparent;
}
window.syncview scale.sv-seek highlight {
  min-height: 6px;
  margin: 0;
  padding: 0;
  background: @syncview_text;
  border-radius: {{r_pill}}px;
  border: 0 solid transparent;
}
window.syncview scale.sv-seek slider {
  min-width: 16px;
  min-height: 16px;
  border-radius: {{r_pill}}px;
  background: @syncview_text;
  border: {{bw}}px solid @syncview_bg;
  box-shadow: none;
}
window.syncview scale.sv-seek:disabled {
  opacity: 0.5;
}

/* ---- Barra delle scorciatoie ---- */
window.syncview .sv-shortcuts {
  background: @syncview_surf;
  border-top: {{bw}}px solid @syncview_border;
  color: @syncview_mute;
  font-size: {{fs_s}}px;
  padding: {{sp1}}px {{sp3}}px;
}
window.syncview .sv-shortcuts .sv-key {
  color: @syncview_text;
  font-weight: 800;
}

/* ---- Finestre di debug (Log e Moduli) ---- */
.sv-chip-live {
  background: @syncview_surf2;
  color: @syncview_text;
  border: {{bw}}px solid @syncview_border;
  border-radius: {{r_pill}}px;
  padding: 2px {{sp2}}px;
  font-size: {{fs_s}}px;
  font-weight: 800;
}
.sv-chip-live.sv-paused {
  background: @syncview_warn_soft;
  color: @syncview_warn_text;
  border-color: @syncview_warn;
}

/* Filtri per modulo: attivo = accento morbido + segno di spunta nel testo (mai solo colore). */
window.syncview button.sv-filter {
  min-height: 24px;
  padding: 0 {{sp2}}px;
  border-radius: {{r_pill}}px;
  border: {{bw}}px solid @syncview_border;
  background: @syncview_surf2;
  color: @syncview_text;
  font-weight: 700;
  font-size: {{fs_s}}px;
  box-shadow: none;
}
window.syncview button.sv-filter label {
  color: @syncview_text;
}
window.syncview button.sv-filter:checked {
  background: @syncview_accent_soft;
  border-color: @syncview_accent;
}

window.syncview dropdown.sv-dropdown button {
  min-height: 26px;
  padding: 0 {{sp2}}px;
  border-radius: {{r_control}}px;
  border: {{bw}}px solid @syncview_border;
  background: @syncview_surf2;
  color: @syncview_text;
  font-weight: 700;
  box-shadow: none;
}

.sv-log-toolbar {
  padding: {{sp2}}px {{sp3}}px;
  border-bottom: {{bw}}px solid @syncview_line;
}
window.syncview listview.sv-log-list {
  background: @syncview_surf;
  color: @syncview_text;
}
window.syncview listview.sv-log-list row {
  padding: 0 {{sp3}}px;
  background: transparent;
  color: @syncview_text;
}
window.syncview listview.sv-log-list row label.sv-log-module,
window.syncview listview.sv-log-list row label.sv-log-message {
  color: @syncview_text;
}
window.syncview listview.sv-log-list row label.sv-log-time {
  color: @syncview_mute;
}
.sv-log-row {
  padding: 5px 0;
  border-bottom: 1px solid @syncview_line;
  font-size: {{fs_s}}px;
}
.sv-log-time {
  color: @syncview_mute;
}
.sv-log-module {
  font-weight: 800;
}
.sv-lvl {
  padding: 1px {{sp2}}px;
  border-radius: {{r_control}}px;
  border: 1px solid transparent;
  font-weight: 800;
}
window.syncview .sv-log-row label.sv-lvl-info {
  background: @syncview_surf2;
  color: @syncview_text;
  border-color: @syncview_border;
}
window.syncview .sv-log-row label.sv-lvl-warn {
  background: @syncview_warn_soft;
  color: @syncview_warn_text;
  border-color: @syncview_warn;
}
window.syncview .sv-log-row label.sv-lvl-err {
  background: @syncview_err_soft;
  color: @syncview_err_text;
  border-color: @syncview_err;
}
window.syncview .sv-log-row label.sv-lvl-debug {
  background: @syncview_surf;
  color: @syncview_mute;
  border-color: @syncview_line;
}
window.syncview .sv-statusbar {
  background: @syncview_surf2;
  border-top: {{bw}}px solid @syncview_border;
  color: @syncview_mute;
  padding: {{sp2}}px {{sp3}}px;
}
window.syncview .sv-statusbar label {
  color: @syncview_mute;
}

.sv-module-row {
  padding: {{sp2}}px {{sp3}}px;
  border-radius: 10px;
  border: 1px solid @syncview_line;
  background: @syncview_surf;
}
.sv-module-row .sv-module-desc {
  color: @syncview_mute;
  font-size: {{fs_s}}px;
}
.sv-module-row .sv-module-name {
  font-weight: 800;
}
window.syncview switch {
  min-width: 40px;
  min-height: 22px;
  border-radius: {{r_pill}}px;
  border: {{bw}}px solid @syncview_border;
  background: @syncview_surf2;
}
window.syncview switch:checked {
  background: @syncview_accent;
  border-color: @syncview_accent;
}
window.syncview switch slider {
  min-width: 14px;
  min-height: 14px;
  margin: 2px;
  border-radius: {{r_pill}}px;
  border: 0 solid transparent;
  background: @syncview_border;
  box-shadow: none;
}
window.syncview switch:checked slider {
  background: @syncview_on_accent;
}

/* ---- Barre di scorrimento ---- */
window.syncview scrollbar {
  background: transparent;
  border: 0 solid transparent;
}
window.syncview scrollbar trough {
  background: transparent;
  border: 0 solid transparent;
  min-width: 10px;
  min-height: 10px;
}
window.syncview scrollbar slider {
  min-width: 6px;
  min-height: 6px;
  margin: 2px;
  border-radius: {{r_pill}}px;
  border: 0 solid transparent;
  background: @syncview_border;
}
window.syncview scrollbar slider:hover {
  background: @syncview_mute;
}

/* ---- Primo avvio / dipendenze (M2.12) ---- */
window.syncview .sv-dlg-h {
  font-size: {{fs_xl}}px;
  font-weight: 800;
}
window.syncview .sv-dlg-lead {
  color: @syncview_mute;
}
window.syncview .sv-deps-list {
  background: @syncview_surf;
  border: {{bw}}px solid @syncview_line;
  border-radius: {{r_card}}px;
  padding: 0 {{sp3}}px;
}
window.syncview .sv-dep-row {
  padding: {{sp2}}px 0;
  border-bottom: 1px solid @syncview_line;
}
window.syncview .sv-dep-row:last-child {
  border-bottom: 0 solid transparent;
}
window.syncview .sv-dep-name {
  font-weight: 800;
  color: @syncview_text;
}
window.syncview .sv-dep-sub {
  color: @syncview_mute;
  font-size: {{fs_s}}px;
}
window.syncview .sv-badge {
  background: @syncview_surf;
  color: @syncview_text;
  border: {{bw}}px solid @syncview_line;
  border-radius: {{r_pill}}px;
  padding: 1px {{sp2}}px;
  font-size: {{fs_s}}px;
  font-weight: 700;
}
window.syncview .sv-badge.sv-ok {
  background: @syncview_ok_soft;
  color: @syncview_ok_text;
  border-color: @syncview_ok;
}
window.syncview .sv-badge.sv-err {
  background: @syncview_err_soft;
  color: @syncview_err_text;
  border-color: @syncview_err;
}
window.syncview .sv-badge.sv-warn {
  background: @syncview_warn_soft;
  color: @syncview_warn_text;
  border-color: @syncview_warn;
}
window.syncview .sv-dep-row progressbar trough {
  min-height: 6px;
  min-width: 96px;
  margin: 0;
  padding: 0;
  border-radius: {{r_pill}}px;
  background: @syncview_line;
  border: 0 solid transparent;
}
window.syncview .sv-dep-row progressbar progress {
  min-height: 6px;
  margin: 0;
  padding: 0;
  border-radius: {{r_pill}}px;
  background: @syncview_text;
  border: 0 solid transparent;
}
window.syncview .sv-note {
  background: @syncview_surf2;
  color: @syncview_text;
  border: {{bw}}px solid @syncview_line;
  border-radius: {{r_card}}px;
  padding: {{sp3}}px;
}
window.syncview .sv-banner {
  border: {{bw}}px solid @syncview_line;
  border-radius: {{r_card}}px;
  padding: {{sp3}}px;
  font-weight: 800;
  background: @syncview_surf2;
  color: @syncview_text;
}
window.syncview .sv-banner.sv-err {
  background: @syncview_err_soft;
  color: @syncview_err_text;
  border-color: @syncview_err;
}
window.syncview .sv-banner.sv-ok {
  background: @syncview_ok_soft;
  color: @syncview_ok_text;
  border-color: @syncview_ok;
}
window.syncview .sv-banner.sv-warn {
  background: @syncview_warn_soft;
  color: @syncview_warn_text;
  border-color: @syncview_warn;
}
window.syncview .sv-section-title {
  font-weight: 800;
}
window.syncview .sv-os-list {
  background: @syncview_surf;
  border: {{bw}}px solid @syncview_line;
  border-radius: {{r_card}}px;
}
window.syncview .sv-os-list row {
  background: transparent;
  color: @syncview_text;
  padding: {{sp2}}px {{sp3}}px;
  border-bottom: 1px solid @syncview_line;
}
window.syncview .sv-os-list row:selected {
  background: @syncview_accent_soft;
  color: @syncview_text;
}
window.syncview .sv-os-name {
  font-weight: 800;
  color: @syncview_text;
}
window.syncview .sv-os-cmd {
  color: @syncview_text;
}
window.syncview .sv-plan {
  background: @syncview_surf;
  color: @syncview_text;
  border: {{bw}}px solid @syncview_line;
  border-radius: {{r_card}}px;
  padding: {{sp3}}px;
}
window.syncview textview.sv-output,
window.syncview textview.sv-output text {
  background: @syncview_surf;
  color: @syncview_text;
}
window.syncview scrolledwindow.sv-output-box {
  border: {{bw}}px solid @syncview_line;
  border-radius: {{r_card}}px;
}
window.syncview checkbutton.sv-check {
  color: @syncview_text;
}
