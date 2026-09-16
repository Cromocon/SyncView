#ifndef SYNCVIEW_CORE_SETTINGS_H
#define SYNCVIEW_CORE_SETTINGS_H

#include <stddef.h>

/*
 * Costanti/tunable di dominio, porting di config/settings.py dove
 * applicabile. La palette colori dell'originale (THEME_COLORS, tema Qt
 * "Night Ops") NON viene portata qui: l'overhaul UI usa una palette
 * diversa (vedi PLAN.md, riferimento praesidium.artysan.me) definita
 * in ui/style.css (M7), non come costanti C.
 */

/* Numero massimo di video simultanei (griglia 2x2) — da MAX_VIDEOS. */
#define SYNCVIEW_MAX_VIDEOS 4

/*
 * Formati video supportati (whitelist per estensione, senza verifica del
 * codec reale) — da SUPPORTED_VIDEO_FORMATS. Le estensioni non includono
 * il punto iniziale.
 */
extern const char *const syncview_supported_video_extensions[];
extern const size_t syncview_supported_video_extensions_count;

/*
 * Preset FPS proposti nel dialog FPS personalizzato — da
 * DEFAULT_FPS_OPTIONS, esclusi i valori non numerici "Auto" e
 * "Personalizzato" (sono stati UI, non costanti di dominio).
 * Nota: ui/fps_dialog.py nell'originale usava un set leggermente
 * diverso (23.976 al posto di 59.94) per i bottoni preset del dialog —
 * discrepanza da riconciliare esplicitamente in M5.7, questa lista
 * segue config/settings.py come fonte autoritativa.
 */
extern const double syncview_fps_presets[];
extern const size_t syncview_fps_presets_count;

/*
 * Step di frame-stepping globale in millisecondi, selezionabili da UI —
 * da FRAME_STEP_OPTIONS. Nota: l'originale ui/main_window.py esponeva
 * nel combo box anche un'opzione "33ms" non presente in
 * config/settings.py — da confermare/reintrodurre in M5 se necessaria
 * alla parità funzionale.
 */
extern const int syncview_frame_step_options_ms[];
extern const size_t syncview_frame_step_options_count;

/*
 * Step di frame-stepping per-player, hardcoded nell'originale
 * (VideoPlayerWidget.step_frames, assunzione fissa a 25fps) — non
 * configurabile, applicato indipendentemente dal FPS selezionato.
 */
#define SYNCVIEW_FRAME_STEP_DEFAULT_MS 40

/* Finestra di export di default, in secondi prima/dopo un marker — da
 * DEFAULT_EXPORT_WINDOW. */
#define SYNCVIEW_DEFAULT_EXPORT_WINDOW_SEC 5

/*
 * Range di zoom video (Ctrl+wheel) — da ui/zoomable_video_widget.py,
 * non presente nell'originale config/settings.py.
 */
#define SYNCVIEW_ZOOM_MIN 1.0
#define SYNCVIEW_ZOOM_MAX 5.0
#define SYNCVIEW_ZOOM_STEP 0.1

#endif /* SYNCVIEW_CORE_SETTINGS_H */
