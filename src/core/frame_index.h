#ifndef SYNCVIEW_CORE_FRAME_INDEX_H
#define SYNCVIEW_CORE_FRAME_INDEX_H

#include <glib.h>

/*
 * Indice dei frame di un video a framerate VARIABILE: i timestamp di presentazione (PTS, ns, in ordine crescente) di tutti
 * i frame, letti all'uscita del demuxer senza decodificare. Con durate irregolari (registrazioni di gioco, molti video da
 * telefono) il passo «esatto» per frame non può contare su 1/fps: serve sapere dove inizia e finisce ogni frame reale.
 * Nessuna dipendenza da GTK.
 */
typedef struct {
    gint64 *pts;  /* ns, strettamente crescenti; owned */
    guint n;
    gint64 last_end_ns;  /* fine dell'ultimo frame (pts dell'ultimo + ultimo intervallo) */
} FrameIndex;

/* Prende il possesso di `sorted_pts` (da g_malloc): ordina, elimina i duplicati e calcola la fine dell'ultimo frame. */
FrameIndex *frame_index_new(gint64 *pts, guint n);
void frame_index_free(FrameIndex *index);

/* Indice del frame che contiene l'istante `t_ns` (l'ultimo con pts <= t; 0 se t è prima del primo). */
guint frame_index_find(const FrameIndex *index, gint64 t_ns);
gint64 frame_index_start(const FrameIndex *index, guint i);
gint64 frame_index_end(const FrameIndex *index, guint i);

/*
 * I frame hanno durate abbastanza irregolari da rendere inutile 1/fps? Vero se più del 3% degli intervalli si scosta dalla
 * mediana di oltre il 20% (e di oltre 2 ms: l'arrotondamento in ms di WebM/MKV non conta). Servono almeno
 * FRAME_INDEX_MIN_FRAMES timestamp, altrimenti falso. Funzione pura (`pts` non deve essere ordinato).
 */
#define FRAME_INDEX_MIN_FRAMES 30
gboolean frame_index_timestamps_irregular(const gint64 *pts, guint n);

/*
 * Legge i timestamp dei frame video di `path` (pipeline filesrc ! parsebin, nessuna decodifica). Dopo i primi
 * FRAME_INDEX_SAMPLE_FRAMES verifica se sono irregolari: se no si ferma subito e ritorna NULL (framerate costante: lo
 * step per frame usa 1/fps); se sì legge tutto il file (al massimo FRAME_INDEX_SCAN_TIMEOUT_S) e ritorna l'indice.
 * Bloccante, da un thread di lavoro. `cancelled` (può essere NULL) è letto con accesso atomico: se diventa != 0 ritorna
 * NULL subito.
 */
#define FRAME_INDEX_SAMPLE_FRAMES 150
#define FRAME_INDEX_SCAN_TIMEOUT_S 120
FrameIndex *frame_index_scan(const char *path, const gint *cancelled);

#endif /* SYNCVIEW_CORE_FRAME_INDEX_H */
