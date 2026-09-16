# Architettura — SyncView-C

Questo documento è uno stub, aggiornato progressivamente man mano che i moduli vengono implementati (vedi milestone in [../PLAN.md](../PLAN.md)).

Per il momento, il riferimento architetturale completo è la sezione [Architettura](../PLAN.md#architettura) di `PLAN.md`: toolkit e librerie, struttura directory, moduli core, playback video, timeline widget, export pipeline, dialoghi/stato UI, modalità debug, scorciatoie cross-platform, finestra frameless.

## Stato implementativo

- **M0 (Scaffolding)**: in corso — vedi commit su `SyncView-C` per lo stato aggiornato di ogni sotto-milestone.
- **M1-M8**: non ancora iniziate.

## Note per chi implementa

Man mano che ogni modulo (`core/`, `video/`, `ui/`, `util/`) viene scritto, aggiungere qui una sezione con: responsabilità del modulo, API pubblica principale, decisioni prese durante l'implementazione che si discostano dal piano originale (se ce ne sono, motivarle). Le deviazioni note rispetto al comportamento dell'app Python originale vanno invece in `MIGRATION_NOTES.md`.
