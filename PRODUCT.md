# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Stack

Nessuno stack web: SyncView è un'app **desktop nativa** in C con GTK4, GStreamer e SQLite (Linux, Windows, macOS). Lo schema di Impeccable non prevede il desktop e `web` è il valore più vicino solo perché lo stile si esprime in CSS, applicato con `GtkCssProvider`. Il design system vive in `design/` nel repository ed è la fonte unica dei token da cui si genera il CSS GTK4; si sincronizza poi con Claude Design (`/design-sync`).

## Users

Oggi SyncView è usato come **VAR (video assistant referee) per le sfide 1 contro 1 di softair**: un arbitro rivede più riprese dello stesso scontro, affiancate e sincronizzate, per decidere se e quando un colpo è andato a segno. Lavora in tempo breve, su un computer, spesso con più persone che guardano lo schermo.

## Product Purpose

Mostrare fino a 4 video dello stesso evento affiancati e sincronizzati, con precisione al frame, per prendere una decisione arbitrale. Il successo è che l'arbitro raggiunga e giustifichi la decisione senza dubbi sulla sincronia delle sorgenti.

## Positioning

**Sincronia e frame esatto.** Più video con offset indipendenti vengono mossi insieme con precisione al frame; l'interfaccia deve far leggere subito lo sfasamento tra le sorgenti. Un normale lettore video non sa fare né l'una né l'altra cosa.

## Operating Context

- Finestra con griglia 2×2 di video (fino a 4), timeline globale con marker, pannello laterale.
- Modalità sync attiva/disattiva con offset per video in millisecondi, master audio, frame-step esatto (preset in ms e frame esatto), velocità di riproduzione, zoom/pan 1,0–5,0× sul singolo video.
- Marker per categoria con descrizione (SQLite), esportazione di clip tramite ffmpeg e dei marker in CSV.
- Scorciatoie da tastiera molto usate: Spazio, frecce, Home/Fine, M (marker), Ctrl+M.
- Primo avvio: l'app verifica e scarica da sola le proprie dipendenze (GStreamer e plugin) chiedendo l'elevazione di sistema quando serve.

## Capabilities and Constraints

- Interfaccia in italiano.
- Massimo 4 video (`SYNCVIEW_MAX_VIDEOS`).
- Le posizioni sono in millisecondi in tutta l'app; il frame esatto si basa sul framerate reale del video e ricade su 40 ms se sconosciuto.
- Modalità debug con finestre dedicate a log e moduli.
- Decisione aperta: il selettore dei preset di step (l'originale ne ha 4: 40/33/100/200 ms; le impostazioni attuali 3).

## Brand Commitments

Nessuno vincolante: né nome, né logo, né colori, né tono. Il redesign è completo; il progetto originale e la sua identità («Night Ops») sono solo evidenza da superare.

## Evidence on Hand

- Originale Python/Qt sul ramo `main` (stile in `ui/styles.py`), a sola lettura.
- Nessun logo, screenshot di riferimento approvato, testimonianza o dato d'uso: non vanno inventati.
- `PLAN.md` contiene una sezione «Riferimento visivo per l'overhaul UI» (palette «Command», sito esterno) scritta prima di questo redesign: **scartata** dall'utente. Il design lo produce Claude Design a partire da `design/README.md`.

## Product Principles

1. **La decisione prima dell'interfaccia**: i video sono l'oggetto; i controlli si ritirano e non competono con le immagini.
2. **Lo sfasamento si legge, non si calcola**: lo stato di sincronia tra le sorgenti è sempre visibile a colpo d'occhio.
3. **Precisione dichiarata**: ogni valore mostrato (tempo, offset, frame, velocità) dice la sua unità e non arrotonda in silenzio.
4. **Usabile sotto pressione e in pubblico**: leggibile a distanza e su schermi condivisi, utilizzabile da tastiera, senza ambiguità sul comando appena dato.
5. **Si installa e funziona**: i problemi di dipendenze si risolvono nell'app, non in un terminale.

## Accessibility & Inclusion

Contrasto leggibile in entrambi i temi (scuro e chiaro, che seguono il sistema), uso completo da tastiera, nessuna informazione affidata al solo colore (soprattutto sincronia, errore, marker).
