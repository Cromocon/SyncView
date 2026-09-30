#ifndef SYNCVIEW_CORE_DISCOVERER_H
#define SYNCVIEW_CORE_DISCOVERER_H

#include <gio/gio.h>
#include <glib.h>
#include <stdint.h>

/*
 * Porting di VideoInfoWorker.probe_video_info (core/video_loader.py): estrae
 * i metadati del primo stream video di un file con GstDiscoverer invece
 * che con ffprobe (vedi docs/MIGRATION_NOTES.md, "Caricamento video e
 * probing metadati"). Nessuna dipendenza da GTK.
 *
 * Campi come nell'originale: path, fps, duration, width, height, codec.
 * Un file senza stream video (solo audio) non è un errore: si ottengono i
 * valori di default, come con ffprobe.
 */
typedef struct {
    char *path;           /* owned: percorso passato dal chiamante */
    double fps;           /* framerate_num/denom; 0.0 = sconosciuto/VFR -> "AUTO FPS" */
    int64_t duration_ms;  /* 0 se sconosciuta. Come nell'originale, nessuno ne dipende */
    int width;            /* pixel codificati; 0 se sconosciuti. Niente correzione PAR/rotazione */
    int height;
    char *codec;          /* owned, mai NULL: nome breve ("h264", "vp8", ...) o "unknown" */
} VideoInfo;

void video_info_free(VideoInfo *info);

/* Info di default (fps 0, dimensioni 0, codec "unknown") per `path`: il fallback dell'originale. */
VideoInfo *video_info_default(const char *path);

/*
 * Errori di probing. L'originale distingueva solo "file non trovato"; qui i
 * casi sono separati per poter dare all'utente un messaggio specifico
 * (ottimizzazione O2) — in particolare MISSING_PLUGINS nomina i decoder mancanti.
 */
#define DISCOVERER_ERROR (discoverer_error_quark())
GQuark discoverer_error_quark(void);

typedef enum {
    DISCOVERER_ERROR_FILE_NOT_FOUND,    /* il file non esiste ("File non trovato") */
    DISCOVERER_ERROR_URI_INVALID,       /* path non convertibile in URI */
    DISCOVERER_ERROR_MISSING_PLUGINS,   /* manca un decoder/demuxer: il messaggio elenca cosa */
    DISCOVERER_ERROR_CORRUPT,           /* file corrotto o formato non riconosciuto */
    DISCOVERER_ERROR_TIMEOUT,           /* il probing ha superato il timeout */
    DISCOVERER_ERROR_BUSY,              /* discoverer occupato */
    DISCOVERER_ERROR_INTERNAL,          /* errore interno di GStreamer / init */
} DiscovererError;

/*
 * TRUE se l'errore impedisce il caricamento (file assente, path invalido,
 * plugin mancanti, file corrotto). FALSE per timeout/busy/interno: l'originale
 * in quei casi caricava comunque con le info di default (il bus della
 * pipeline segnalerà un eventuale errore di playback).
 */
gboolean discoverer_error_is_blocking(DiscovererError code);

#define SYNCVIEW_DISCOVERER_TIMEOUT_MS 10000      /* come il timeout=10 di ffprobe */
#define SYNCVIEW_DISCOVERER_MIN_TIMEOUT_MS 1000   /* limiti della property "timeout" di GstDiscoverer */
#define SYNCVIEW_DISCOVERER_MAX_TIMEOUT_MS 3600000

/*
 * Probing sincrono. Ritorna le info, oppure NULL con error nel dominio
 * DISCOVERER_ERROR. Bloccante: da chiamare da un thread worker o usare la
 * versione asincrona. timeout_ms <= 0 usa SYNCVIEW_DISCOVERER_TIMEOUT_MS; gli altri
 * valori sono limitati a [MIN, MAX]_TIMEOUT_MS (minimo 1 s imposto da GStreamer).
 * Inizializza GStreamer se necessario.
 */
VideoInfo *discoverer_probe_file(const char *path, int timeout_ms, GError **error);

/*
 * Come discoverer_probe_file ma con la semantica dell'originale: gli errori
 * NON bloccanti (timeout/busy/interno) sono registrati con log_error e
 * sostituiti dalle info di default; ritorna NULL con error solo per errori
 * bloccanti.
 */
VideoInfo *discoverer_probe_file_with_fallback(const char *path, int timeout_ms, GError **error);

/*
 * Versione asincrona (equivalente di AsyncVideoLoader.load_video_async per un
 * singolo probing): esegue discoverer_probe_file_with_fallback in un thread
 * del pool GLib; `callback` viene invocata nel main context del chiamante
 * con il risultato, da leggere con discoverer_probe_finish(). Se cancellable
 * viene annullato prima del completamento la callback riceve
 * G_IO_ERROR_CANCELLED (il lavoro già in corso nel thread termina ma il suo
 * risultato è scartato).
 */
void discoverer_probe_async(const char *path, int timeout_ms, GCancellable *cancellable,
                            GAsyncReadyCallback callback, gpointer user_data);

/* Ritorna le info (owned dal chiamante) o NULL con error. */
VideoInfo *discoverer_probe_finish(GAsyncResult *result, GError **error);

/*
 * Frame rate nominale a partire dagli intervalli (ns) tra i timestamp di
 * frame consecutivi: media degli intervalli > 0 entro ±25% della mediana
 * (robusta a outlier e a timestamp arrotondati), agganciata al valore
 * standard più vicino (23.976, 24, 25, 29.97, 30, 48, 50, 59.94, 60, 120)
 * se entro l'1%, altrimenti il valore grezzo. Ritorna 0.0 con meno di
 * SYNCVIEW_FPS_MIN_INTERVALS intervalli validi. Funzione pura.
 */
#define SYNCVIEW_FPS_MIN_INTERVALS 10
double discoverer_nominal_fps_from_intervals(const int64_t *intervals_ns, size_t n);

/*
 * Stima il frame rate nominale leggendo i timestamp dei primi frame dello
 * stream video (pipeline filesrc ! parsebin, senza decodifica; al massimo
 * ~3 s). Usata quando GstDiscoverer non riporta un framerate (framerate
 * 0/1, tipico dei file a frame rate variabile come molti MP4 da telefono),
 * dove l'originale otteneva comunque un valore da r_frame_rate. 0.0 se non
 * stimabile. Bloccante.
 */
double discoverer_estimate_fps(const char *path);

/*
 * Trasforma le stringhe "installer details" di GStreamer
 * ("gstreamer|1.0|app|H.265 decoder|decoder-video/x-h265, ...") in un elenco
 * leggibile ("H.265 decoder, AAC decoder"). Funzione pura, esposta per i test.
 * Ritorna NULL se details è NULL o vuoto. Il chiamante libera con g_free().
 */
char *discoverer_format_missing_plugins(const char *const *details);

#endif /* SYNCVIEW_CORE_DISCOVERER_H */
