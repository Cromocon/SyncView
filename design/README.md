# SyncView: brief per Claude Design

Questo documento serve a farti capire il progetto. **Le scelte visive sono tue**: identità, palette, forma, icone, microcopy visivo. Qui trovi solo ciò che è vero del prodotto, ciò che l'interfaccia deve fare, i vincoli tecnici e ciò che è già stato scartato.

L'interfaccia **non esiste ancora**: l'app è in sviluppo e finora ha solo il motore (sincronizzazione, marker, pipeline video). Il tuo design guiderà la costruzione della finestra, quindi può essere ambizioso.

## 1. Cosa è SyncView

Un'app desktop (Linux, Windows, macOS) che mostra **fino a 4 video dello stesso evento, affiancati e sincronizzati**, con precisione al frame. Si possono muovere tutti insieme, avanzare frame per frame, rallentare, ingrandire un video e segnare momenti (marker) da rivedere o esportare.

Cosa la distingue da un lettore video: **sincronia e frame esatto**. Ogni video ha il proprio offset (in millisecondi) rispetto agli altri, e l'interfaccia deve far *leggere* lo sfasamento tra le sorgenti, non farlo calcolare.

## 2. Chi la usa e in quale scena

Oggi è usata come **VAR (video assistant referee) nelle sfide 1 contro 1 di softair**: un arbitro rivede più riprese dello stesso scontro per decidere se, e in quale istante, un colpo è andato a segno. Decide in fretta, davanti ad altre persone.

Scene reali, **tutte e tre**, quindi nessuna ha la precedenza:
- sul campo, all'aperto, portatile in piena luce diurna (riflessi sullo schermo);
- al chiuso, in una tenda o sala arbitri, con luce artificiale o penombra;
- proiettata o su schermo grande, guardata da più persone anche a distanza.

Per questo l'interfaccia ha **due temi, scuro e chiaro, che seguono il sistema**. Nessuno dei due è il «principale»: entrambi vanno progettati come prima scelta e resi leggibili a distanza.

## 3. Cosa deve poter fare l'interfaccia

### Finestra principale
- **Griglia video** da 1 a 4 (di norma 2×2), ogni video in un proprio riquadro. Un riquadro vuoto invita a caricare (trascinando un file o cliccando).
- **Per ogni video**: titolo/identità del canale (oggi «FEED-1…4»), carica, aggiorna, rimuovi, muto, aggiungi marker, indicazione dei fotogrammi al secondo, **zoom 1,0–5,0×** (Ctrl+rotella, verso il cursore) e spostamento, stato («nessun video»), tempo corrente/durata, e comandi propri (−10, −1, play/pausa, +1, +10 frame).
- **Riproduzione globale**: play/pausa, vai all'inizio, vai alla fine, **audio master** (quale video si sente), velocità di riproduzione.
- **Sincronizzazione**: interruttore sync attivo/disattivo, pulsante di risincronizzazione, **offset per video in ms**.
- **Modalità frame**: passo per frame con preset (40 ms, 33 ms, 100 ms, 200 ms) e, in più, **frame esatto** basato sul framerate reale del video; salti da ±1 e ±10; FPS configurabile (con valore personalizzato).
- **Adatta video** (fit) alla finestra.
- **Timeline globale** (elemento centrale): righello con tacche adattive, **playhead** con lettura numerica, **marker** (cliccabili, per categoria, globali o legati a un video), tempo corrente/durata. I marker non si trascinano: si creano da tastiera (Ctrl+M) o dal pulsante del singolo video.
- **Barra del titolo propria** (la finestra non usa le decorazioni del sistema): titolo, aiuto, minimizza/massimizza/chiudi, un indicatore di stato dell'app.
- **Scorciatoie** (devono restare visibili e coerenti): Spazio, Ctrl+O, F1, Ctrl+S, Ctrl+F, Ctrl+R, Home/Fine, M, ←/→ (in modalità frame), Shift+←/→, Ctrl+M, P/N, Ctrl+E, Ctrl+0.

### Dialoghi e altre finestre
- **Gestione marker**: elenco con filtro per categoria, modifica inline della descrizione, esporta CSV, elimina. Nessuna creazione da questo dialogo.
- **Esporta clip**: cartella di destinazione, qualità, secondi prima/dopo il marker; coda di esportazione con avanzamento ed errori.
- **FPS personalizzato.**
- **Aiuto** (scorciatoie e uso).
- **Modalità debug** (strumento tecnico, solo per sviluppatori/supporto): due finestre aggiuntive, una con i log in tempo reale, una per scegliere quali moduli scrivono i log.
- **Primo avvio / dipendenze**: l'app controlla che GStreamer e i suoi plugin ci siano; se mancano li scarica e li installa da sola, chiedendo la password di sistema al momento giusto (la gestisce il sistema operativo, l'app non la vede). Serve una schermata che spieghi cosa manca, cosa sta facendo, e come va a finire, comprese le alternative per chi non vuole l'installazione automatica.

## 4. Concetti che il design deve rendere visibili

- **Lo sfasamento** tra le sorgenti: quanto è in anticipo o in ritardo ogni video, e se la sincronia è attiva.
- **Il momento esatto**: tempo, frame e velocità mostrati sempre con la loro unità, senza arrotondamenti nascosti. I numeri non devono «ballare» quando cambiano.
- **Quale video è «master»** per l'audio e quale ha il focus.
- **I marker**: dove sono, di che categoria, quale è l'attivo.
- **Cosa è appena successo**: l'arbitro deve capire il risultato del comando dato (es. «frame +1» ha mosso tutti i video? solo questo?).

## 5. Stati da progettare (oltre al caso normale)

Nessun video caricato · caricamento in corso (con scheletro) · video non leggibile / formato non supportato (con «riprova») · un video perso durante la riproduzione · file spostato o non trovato · fine del video · sync disattivato / offset non nullo · nessun marker · marker filtrati a zero · errore di analisi (probing) · esportazione in coda / in corso / fallita / completata · dipendenze mancanti / in installazione / installazione fallita · modalità debug attiva · finestra molto piccola e finestra molto grande · schermo ad alta densità (HiDPI).

## 6. Vincoli tecnici (importanti: il risultato diventa CSS GTK4)

- L'app è **nativa in C con GTK4** (non web). Il tema si applica con un foglio CSS di GTK (`GtkCssProvider`). Consegna quindi i token come **valori piatti e nominati** (colori semantici, spaziature, raggi, spessori di bordo, ombre, durate), non come costrutti che esistono solo nel web. Evita ciò che il CSS di GTK non sa fare (layout con grid CSS, `backdrop-filter`, `clip-path`, unità o funzioni solo-web); i widget sono quelli di GTK (pulsanti, interruttori, liste, menu a discesa, scale, finestre di dialogo), la timeline è disegnata a mano su un'area di disegno (Cairo), quindi lì hai libertà totale.
- **Tema scuro e chiaro** che seguono la preferenza del sistema. Vanno previsti due insiemi di token con gli stessi nomi.
- **Tipografia: font monospace di sistema.** Nessun font viene incluso o scaricato. Su Linux sarà la monospace di sistema, su Windows e macOS la loro. Progetta con una scala di dimensioni, non con un font preciso.
- **I video sono disegnati dalla GPU**: evita effetti costosi, trasparenze o animazioni pesanti *sopra* i video. Le sovrapposizioni sui video (nome del canale, tempo) devono restare leggibili su qualunque immagine.
- **Finestra senza decorazioni di sistema**, con barra del titolo propria. Il ridimensionamento col trascinamento dei bordi è ancora da progettare.
- **Interfaccia in italiano.**
- **Tastiera**: tutto raggiungibile da tastiera, con focus visibile.
- **Icone**: vanno incluse nel pacchetto (SVG); niente dipendenza da emoji di sistema (nell'originale c'erano e rendono diversamente su ogni sistema).
- **Animazioni**: GTK le supporta, ma devono restare semplici e rispettare la preferenza di sistema per il movimento ridotto.

## 7. Cosa è libero e cosa no

**Libero**: tutta l'identità visiva, la palette, la forma dei controlli, le icone, il carattere, l'organizzazione dei pannelli, l'aspetto della timeline e dei marker. Anche il nome «SyncView» e il marchio non sono vincolanti.

**Da non fare / già scartato**
- L'aspetto originale «Night Ops» (sfondo quasi nero, monospace, verde oliva/sabbia/rosso mattone, tono militare) è un'**anti-riferimento**: il redesign deve superarlo, non rifinirlo.
- Il riferimento visivo precedente (palette verde-oliva e rame, microcopy maiuscolo monospace, card numerate) è stato **scartato**.
- Evitare l'aspetto generico di un'app GTK/libadwaita: il progetto deve avere un carattere riconoscibile.
- Evitare ornamenti (cornici, bagliori, effetti) che competono con i video: sono l'oggetto della decisione.

## 8. Cosa ti chiedo di produrre

1. **Sistema di token** per entrambi i temi: colori semantici (sfondo, superfici, testo, bordi, stati: successo/errore/avviso/info, evidenziazione, focus, identità dei 4 canali), scala tipografica monospace, spaziatura, raggi, bordi, ombre, durate/easing.
2. **Componenti**: pulsante (primario/secondario/pericoloso/solo icona), interruttore, casella di spunta, menu a discesa, campo numerico, cursore/slider, riquadro video con i suoi comandi, **timeline con righello, playhead e marker**, lettura di tempo/frame/offset, elenco marker, coda di esportazione, indicatori di stato.
3. **Schermate chiave** con dati realistici: finestra principale con 1, 2 e 4 video; gestione marker; esportazione; primo avvio e installazione dipendenze; debug.
4. **Stati** della sezione 5.
5. **Regole d'uso**: gerarchia, quando usare cosa, come cambiano i temi, cosa fare sotto luce forte e su schermo grande.

Per i mockup usa dati d'esempio sintetici e plausibili (tempi come `00:12.480`, offset come `+120 ms`, framerate 25 e 30 fps, categorie di marker); **non inventare** dati d'uso, clienti o prestazioni.

## 9. Dove guardare nel repository

- `PRODUCT.md` (radice): utenti, scopo, vincoli e principi del prodotto.
- `PLAN.md`: piano di sviluppo, sezioni «UI» e «Dialoghi e stato UI» (la parte sul riferimento visivo è superata).
- `docs/ARCHITECTURE.md`: cosa esiste già nel motore.
- Ramo `main`: `ui/main_window.py`, `ui/video_player.py` (struttura e comandi dell'originale, da leggere come inventario funzionale, non come modello visivo) e `ui/styles.py` (l'aspetto da superare).
- Non esistono screenshot di riferimento approvati né logo.
