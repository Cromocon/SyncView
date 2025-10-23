"""
Gestore Esportazione Video (usa moviepy).
Esegue l'esportazione in un thread separato.
"""

from PyQt6.QtCore import QObject, pyqtSignal, QThread
from pathlib import Path
from typing import List, Optional, Dict, TYPE_CHECKING
import time

# Import moviepy solo se disponibile
try:
    from moviepy.video.io.VideoFileClip import VideoFileClip
    MOVIEPY_AVAILABLE = True
except ImportError:
    MOVIEPY_AVAILABLE = False
    VideoFileClip = None # Definisci come None per Pylance

if TYPE_CHECKING:
    from moviepy.video.io.VideoFileClip import VideoFileClip as VideoFileClipType

from core.markers import Marker
from core.logger import logger
from config.settings import EXPORT_DIR

class VideoExporter(QObject):
    """
    Worker che gestisce l'esportazione video in un thread separato.
    Esporta clip da tutti i video caricati per ogni marker.
    """
    
    # Segnali
    finished = pyqtSignal(str)  # Messaggio di successo
    error = pyqtSignal(str)     # Messaggio di errore
    progress = pyqtSignal(str)  # Messaggio di progresso (es. "Clip 1 di 5...")

    def __init__(self, video_paths: Dict[int, Path], markers: List[Marker], 
                 sec_before: int, sec_after: int,
                 export_dir: Optional[Path] = None,
                 parent: Optional[QObject] = None):
        super().__init__(parent)
        
        # video_paths è un dizionario: {video_index: Path}
        # Contiene solo i video effettivamente caricati
        self.video_paths = video_paths
        self.markers = markers
        self.sec_before_ms = sec_before * 1000
        self.sec_after_ms = sec_after * 1000
        self.export_dir = export_dir if export_dir else EXPORT_DIR
        self.is_running = True

    def run(self):
        """
        Metodo principale del worker. Esegue l'esportazione.
        
        Per ogni marker:
        - Se marker.video_index is None (globale): esporta da tutti i video caricati
        - Se marker.video_index è specificato: esporta solo da quel video
        """
        
        # 1. Controlla se moviepy è disponibile
        if not MOVIEPY_AVAILABLE or VideoFileClip is None:
            self.error.emit("Libreria 'moviepy' non trovata. Impossibile esportare.\n\nAssicurati di averla installata: pip install moviepy")
            return
            
        # 2. Controlla che ci siano video caricati
        if not self.video_paths:
            self.error.emit("Nessun video caricato.")
            return
            
        # 3. Controlla se ci sono markers
        if not self.markers:
            self.error.emit("Nessun marker da esportare.")
            return
            
        logger.log_export_action(
            "Avvio esportazione", 
            f"Video: {len(self.video_paths)}, Markers: {len(self.markers)}, "
            f"Window: (-{self.sec_before_ms // 1000}s, +{self.sec_after_ms // 1000}s)"
        )
        
        try:
            # Carica tutti i video UNA SOLA VOLTA
            video_clips = {}  # Dict[int, VideoFileClip]
            
            for video_idx, video_path in self.video_paths.items():
                if not video_path.exists():
                    logger.log_export_action(f"Video {video_idx+1} saltato", f"File non trovato: {video_path}")
                    continue
                    
                try:
                    logger.log_export_action(f"Caricamento Video {video_idx+1}...", video_path.name)
                    clip = VideoFileClip(str(video_path))
                    video_clips[video_idx] = clip
                    logger.log_export_action(f"Video {video_idx+1} caricato", f"Durata: {clip.duration}s")
                except Exception as e:
                    logger.log_error(f"Errore caricamento Video {video_idx+1}", e)
                    continue
            
            if not video_clips:
                self.error.emit("Nessun video caricato con successo.")
                return
            
            # Calcola il numero totale di clip da esportare
            total_clips = 0
            for marker in self.markers:
                if marker.video_index is None:
                    # Marker globale: una clip per ogni video
                    total_clips += len(video_clips)
                else:
                    # Marker specifico: una clip solo per quel video
                    if marker.video_index in video_clips:
                        total_clips += 1
            
            logger.log_export_action("Clip totali da esportare", str(total_clips))
            
            # Esporta le clip
            clip_counter = 0
            
            for marker_idx, marker in enumerate(self.markers):
                if not self.is_running:
                    self.error.emit("Esportazione annullata.")
                    for clip in video_clips.values():
                        clip.close()
                    return
                
                # Determina da quali video esportare questo marker
                video_indices_to_export = []
                
                if marker.video_index is None:
                    # Marker globale: esporta da tutti i video
                    video_indices_to_export = list(video_clips.keys())
                else:
                    # Marker specifico: esporta solo da quel video
                    if marker.video_index in video_clips:
                        video_indices_to_export = [marker.video_index]
                
                # Esporta per ogni video selezionato
                for video_idx in video_indices_to_export:
                    if not self.is_running:
                        break
                    
                    clip_counter += 1
                    
                    video_clip = video_clips[video_idx]
                    video_duration_sec = video_clip.duration
                    
                    # Calcola tempi in secondi
                    start_ms = marker.timestamp - self.sec_before_ms
                    end_ms = marker.timestamp + self.sec_after_ms
                    
                    start_sec = max(0, start_ms / 1000.0)
                    end_sec = min(video_duration_sec, end_ms / 1000.0)
                    
                    if start_sec >= end_sec:
                        logger.log_export_action(
                            f"Clip saltata", 
                            f"Video {video_idx+1}, Marker {marker_idx+1}: Inizio >= Fine"
                        )
                        continue
                    
                    # Nome file: video_X_marker_Y_timestamp.mp4
                    time_str = self._format_time_filename(marker.timestamp)
                    marker_type = "global" if marker.video_index is None else "specific"
                    clip_name = f"video{video_idx+1}_marker{marker_idx+1}_{marker_type}_{time_str}.mp4"
                    output_path = self.export_dir / clip_name
                    
                    logger.log_export_action(
                        f"Creazione clip {clip_counter}/{total_clips}", 
                        f"{output_path.name} (da {start_sec:.2f}s a {end_sec:.2f}s)"
                    )
                    
                    # Estrai e salva la subclip
                    try:
                        # moviepy 2.x usa 'subclipped' invece di 'subclip'
                        sub_clip = video_clip.subclipped(start_sec, end_sec)
                        
                        # Prova prima con audio
                        export_success = False
                        
                        if sub_clip.audio is not None:
                            try:
                                # Tenta esportazione con audio
                                self.progress.emit(f"Esportazione clip {clip_counter} di {total_clips}...")
                                sub_clip.write_videofile(
                                    str(output_path), 
                                    codec="libx264", 
                                    audio_codec="aac",
                                    preset="ultrafast",
                                    threads=4
                                )
                                export_success = True
                            except (AttributeError, OSError) as audio_error:
                                # Se l'audio fallisce, riprova senza audio
                                logger.log_export_action(
                                    f"⚠ Audio fallito per {output_path.name}", 
                                    f"Esporto solo video: {type(audio_error).__name__}"
                                )
                                # Rimuovi l'audio dalla clip e riprova
                                sub_clip = sub_clip.without_audio()
                        
                        # Se non c'era audio o l'export con audio è fallito, esporta solo video
                        if not export_success:
                            self.progress.emit(f"Esportazione clip {clip_counter} di {total_clips} (solo video)...")
                            sub_clip.write_videofile(
                                str(output_path), 
                                codec="libx264", 
                                audio=False,  # Nessun audio
                                preset="ultrafast",
                                threads=4
                            )
                        
                        sub_clip.close()
                        logger.log_export_action(
                            f"✓ Clip {clip_counter} salvata", 
                            output_path.name
                        )
                        
                    except Exception as e:
                        logger.log_error(f"Errore durante salvataggio clip {output_path.name}", e)
                        # Continua con la prossima clip
            
            # Chiudi tutti i video
            for clip in video_clips.values():
                clip.close()
            
            self.finished.emit(f"Esportazione completata! {clip_counter} clip salvate in '{self.export_dir.name}'.")

        except Exception as e:
            logger.log_error("Errore fatale durante l'esportazione", e)
            self.error.emit(f"Errore: {str(e)}")

    def stop(self):
        """Ferma il processo di esportazione."""
        self.is_running = False
        
    def _format_time_filename(self, ms: int) -> str:
        """Formatta millisecondi in HHhMMmSSsMMMms per nome file."""
        total_seconds = ms / 1000
        hours = int(total_seconds // 3600)
        minutes = int((total_seconds % 3600) // 60)
        seconds = int(total_seconds % 60)
        millis = int(ms % 1000)
        return f"{hours:02d}h{minutes:02d}m{seconds:02d}s{millis:03d}ms"

