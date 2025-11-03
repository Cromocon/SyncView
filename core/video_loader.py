"""
Worker thread per il caricamento asincrono dei video.
Implementa lazy loading per migliorare le performance all'avvio.
"""

from PyQt6.QtCore import QThread, pyqtSignal, QObject
from pathlib import Path
import subprocess
import json
from core.logger import logger


class VideoInfoWorker(QObject):
    """Worker per estrarre informazioni video in modo asincrono."""
    
    # Segnali
    info_ready = pyqtSignal(int, dict)  # (video_index, info_dict)
    error_occurred = pyqtSignal(int, str)  # (video_index, error_message)
    
    def __init__(self):
        super().__init__()
        self.video_path = None
        self.video_index = -1
    
    def set_video(self, video_index: int, video_path: Path):
        """Imposta il video da analizzare."""
        self.video_index = video_index
        self.video_path = video_path
    
    def run(self):
        """Esegue l'analisi del video."""
        if not self.video_path or not self.video_path.exists():
            self.error_occurred.emit(self.video_index, "File non trovato")
            return
        
        try:
            info = self.probe_video_info(self.video_path)
            self.info_ready.emit(self.video_index, info)
        except Exception as e:
            logger.log_error(f"Errore probing video Feed-{self.video_index + 1}", str(e))
            self.error_occurred.emit(self.video_index, str(e))
    
    def probe_video_info(self, video_path: Path) -> dict:
        """Estrae informazioni sul video usando ffprobe.
        
        Returns:
            dict: Dizionario con 'fps', 'duration', 'width', 'height', ecc.
        """
        info = {
            'path': video_path,
            'fps': 0.0,
            'duration': 0,
            'width': 0,
            'height': 0,
            'codec': 'unknown'
        }
        
        try:
            # Usa ffprobe per estrarre metadati
            result = subprocess.run(
                ['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                 '-show_entries', 'stream=r_frame_rate,duration,width,height,codec_name',
                 '-of', 'json', str(video_path)],
                capture_output=True,
                text=True,
                timeout=10
            )
            
            if result.returncode == 0:
                data = json.loads(result.stdout)
                if data.get('streams') and len(data['streams']) > 0:
                    stream = data['streams'][0]
                    
                    # FPS
                    fps_str = stream.get('r_frame_rate', '0/1')
                    num, den = map(int, fps_str.split('/'))
                    if den > 0:
                        info['fps'] = num / den
                    
                    # Durata (in secondi, converti in ms)
                    if 'duration' in stream:
                        info['duration'] = int(float(stream['duration']) * 1000)
                    
                    # Dimensioni
                    info['width'] = stream.get('width', 0)
                    info['height'] = stream.get('height', 0)
                    
                    # Codec
                    info['codec'] = stream.get('codec_name', 'unknown')
        
        except (subprocess.TimeoutExpired, FileNotFoundError, json.JSONDecodeError, ValueError) as e:
            logger.log_error(f"Errore ffprobe per {video_path.name}", str(e))
            # Info di fallback
            info['fps'] = 0.0
        
        return info


class AsyncVideoLoader:
    """Gestore per il caricamento asincrono dei video.
    
    Implementa lazy loading con threading per non bloccare l'UI.
    """
    
    def __init__(self):
        self.threads = {}
        self.workers = {}
    
    def load_video_async(self, video_index: int, video_path: Path, 
                        on_ready_callback, on_error_callback):
        """Avvia il caricamento asincrono di un video.
        
        Args:
            video_index: Indice del video player (0-3)
            video_path: Path del file video
            on_ready_callback: Funzione da chiamare quando le info sono pronte
            on_error_callback: Funzione da chiamare in caso di errore
        """
        # Pulisci thread precedente se esiste
        if video_index in self.threads:
            self.cleanup_thread(video_index)
        
        # Crea nuovo worker e thread
        thread = QThread()
        worker = VideoInfoWorker()
        worker.set_video(video_index, video_path)
        
        # Sposta worker nel thread
        worker.moveToThread(thread)
        
        # Connetti segnali
        thread.started.connect(worker.run)
        worker.info_ready.connect(on_ready_callback)
        worker.error_occurred.connect(on_error_callback)
        
        # Pulizia automatica al termine
        worker.info_ready.connect(lambda: self.cleanup_thread(video_index))
        worker.error_occurred.connect(lambda: self.cleanup_thread(video_index))
        
        # Salva riferimenti
        self.threads[video_index] = thread
        self.workers[video_index] = worker
        
        # Avvia thread
        thread.start()
        
        logger.log_user_action(f"Caricamento asincrono avviato", f"Feed-{video_index + 1}: {video_path.name}")
    
    def cleanup_thread(self, video_index: int):
        """Pulisce un thread terminato."""
        if video_index in self.threads:
            thread = self.threads[video_index]
            thread.quit()
            thread.wait(2000)  # Aspetta max 2 secondi
            
            # Rimuovi riferimenti
            if video_index in self.threads:
                del self.threads[video_index]
            if video_index in self.workers:
                del self.workers[video_index]
    
    def cleanup_all(self):
        """Pulisce tutti i thread attivi."""
        for index in list(self.threads.keys()):
            self.cleanup_thread(index)
