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
