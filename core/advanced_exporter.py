"""
Advanced Export System for SyncView.
Versione 3.0 - Export avanzato con:
- Export paralleli con pool di processi
- Hardware acceleration (NVENC, QSV)
- Preset qualità configurabili
- Export queue con resume capability
- Retry automatico su fallimenti transienti
"""

from PyQt6.QtCore import QObject, pyqtSignal
from pathlib import Path
from typing import List, Optional, Dict, Tuple, Any
from dataclasses import dataclass, field, asdict
from enum import Enum
import json
import time
import subprocess
import multiprocessing as mp
from concurrent.futures import ProcessPoolExecutor, as_completed
from datetime import datetime

from core.markers import Marker
from core.logger import logger


class ExportQuality(Enum):
    """Preset di qualità per l'export."""
    FAST_PREVIEW = "ultrafast"      # Per preview veloci
    MEDIUM = "medium"                # Bilanciato
    HIGH = "slow"                    # Alta qualità
    BEST = "veryslow"                # Massima qualità (molto lento)


class HardwareEncoder(Enum):
    """Encoder hardware disponibili."""
    NONE = "libx264"                 # Software encoding (default)
    NVENC = "h264_nvenc"             # NVIDIA GPU
    QSV = "h264_qsv"                 # Intel Quick Sync Video
    VAAPI = "h264_vaapi"             # Linux VA-API
    VIDEOTOOLBOX = "h264_videotoolbox"  # macOS VideoToolbox


class ExportStatus(Enum):
    """Stati di un export."""
    PENDING = "pending"
    IN_PROGRESS = "in_progress"
    COMPLETED = "completed"
    FAILED = "failed"
    RETRYING = "retrying"
    CANCELLED = "cancelled"


@dataclass
class ExportJob:
    """Rappresenta un singolo job di export."""
    job_id: str
    video_path: Path
    video_index: int
    marker_index: int
    marker_timestamp: int
    start_ms: int
    end_ms: int
    output_path: Path
    status: ExportStatus = ExportStatus.PENDING
    retry_count: int = 0
    max_retries: int = 3
    error_message: Optional[str] = None
    progress: float = 0.0
    start_time: Optional[float] = None
    end_time: Optional[float] = None
    
    def to_dict(self) -> Dict[str, Any]:
        """Converte il job in dizionario per serializzazione."""
        data = asdict(self)
        # Converti Path in str e Enum in value
        data['video_path'] = str(self.video_path)
        data['output_path'] = str(self.output_path)
        data['status'] = self.status.value
        return data
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'ExportJob':
        """Crea un job da dizionario."""
        data['video_path'] = Path(data['video_path'])
        data['output_path'] = Path(data['output_path'])
        data['status'] = ExportStatus(data['status'])
        return cls(**data)


class HardwareAccelerationDetector:
    """Rileva encoder hardware disponibili sul sistema."""
    
    @staticmethod
    def detect_available_encoders() -> List[HardwareEncoder]:
        """Rileva quali encoder hardware sono disponibili."""
        available = [HardwareEncoder.NONE]  # Software encoding sempre disponibile
        
        try:
            # Testa FFmpeg
            result = subprocess.run(
                ['ffmpeg', '-hide_banner', '-encoders'],
                capture_output=True,
                text=True,
                timeout=5
            )
            
            encoders_output = result.stdout.lower()
            
            # Controlla NVENC (NVIDIA)
            if 'h264_nvenc' in encoders_output or 'nvenc' in encoders_output:
                available.append(HardwareEncoder.NVENC)
                
            # Controlla QSV (Intel)
            if 'h264_qsv' in encoders_output or 'qsv' in encoders_output:
                available.append(HardwareEncoder.QSV)
                
            # Controlla VA-API (Linux)
            if 'h264_vaapi' in encoders_output or 'vaapi' in encoders_output:
                available.append(HardwareEncoder.VAAPI)
                
            # Controlla VideoToolbox (macOS)
            if 'h264_videotoolbox' in encoders_output or 'videotoolbox' in encoders_output:
                available.append(HardwareEncoder.VIDEOTOOLBOX)
                
            logger.log_export_action(
                "Hardware encoders rilevati",
                f"{len(available)} disponibili: {[e.name for e in available]}"
            )
            
        except (subprocess.TimeoutExpired, FileNotFoundError, Exception) as e:
            logger.log_export_action(
                "⚠ Rilevamento hardware encoders fallito",
                f"Uso solo software encoding: {type(e).__name__}"
            )
        
        return available


class ExportQueue:
    """Gestisce una coda di export con persistenza su disco."""
    
    def __init__(self, queue_file: Optional[Path] = None):
        # Se non specificato, usa la directory home dell'utente
        if queue_file is None:
            queue_file = Path.home() / ".syncview" / "export_queue.json"
            queue_file.parent.mkdir(parents=True, exist_ok=True)
        self.queue_file = queue_file
        self.jobs: Dict[str, ExportJob] = {}
        self.load_queue()
    
    def add_job(self, job: ExportJob) -> None:
        """Aggiunge un job alla coda."""
        self.jobs[job.job_id] = job
        self.save_queue()
    
    def add_jobs(self, jobs: List[ExportJob]) -> None:
        """Aggiunge multipli job alla coda."""
        for job in jobs:
            self.jobs[job.job_id] = job
        self.save_queue()
    
    def get_pending_jobs(self) -> List[ExportJob]:
        """Ottiene tutti i job pendenti."""
        return [job for job in self.jobs.values() 
                if job.status in (ExportStatus.PENDING, ExportStatus.RETRYING)]
    
    def get_failed_jobs(self) -> List[ExportJob]:
        """Ottiene tutti i job falliti."""
        return [job for job in self.jobs.values() if job.status == ExportStatus.FAILED]
    
    def update_job(self, job_id: str, **kwargs) -> None:
        """Aggiorna un job nella coda."""
        if job_id in self.jobs:
            for key, value in kwargs.items():
                setattr(self.jobs[job_id], key, value)
            self.save_queue()
    
    def remove_completed_jobs(self) -> int:
        """Rimuove i job completati dalla coda."""
        before = len(self.jobs)
        self.jobs = {
            job_id: job for job_id, job in self.jobs.items()
            if job.status != ExportStatus.COMPLETED
        }
        removed = before - len(self.jobs)
        if removed > 0:
            self.save_queue()
        return removed
    
    def clear_queue(self) -> None:
        """Svuota completamente la coda."""
        self.jobs.clear()
        self.save_queue()
    
    def save_queue(self) -> None:
        """Salva la coda su disco."""
        try:
            self.queue_file.parent.mkdir(parents=True, exist_ok=True)
            data = {
                'version': '3.0',
                'timestamp': time.time(),
                'jobs': [job.to_dict() for job in self.jobs.values()]
            }
            with open(self.queue_file, 'w') as f:
                json.dump(data, f, indent=2)
        except Exception as e:
            logger.log_error("Errore salvataggio coda export", e)
    
    def load_queue(self) -> None:
        """Carica la coda da disco."""
        try:
            if self.queue_file.exists():
                with open(self.queue_file, 'r') as f:
                    data = json.load(f)
                
                self.jobs = {
                    job_dict['job_id']: ExportJob.from_dict(job_dict)
                    for job_dict in data.get('jobs', [])
                }
                
                logger.log_export_action(
                    "Coda export caricata",
                    f"{len(self.jobs)} job ripristinati"
                )
        except Exception as e:
            logger.log_error("Errore caricamento coda export", e)
            self.jobs = {}


def export_clip_ffmpeg(job: ExportJob, quality: ExportQuality, 
                       encoder: HardwareEncoder) -> Tuple[bool, Optional[str]]:
    """
    Esporta una singola clip usando FFmpeg direttamente.
    Questa funzione viene eseguita in un processo separato.
    
    Returns:
        Tuple[bool, Optional[str]]: (success, error_message)
    """
    try:
        # Calcola durata e tempi
        start_sec = job.start_ms / 1000.0
        duration_sec = (job.end_ms - job.start_ms) / 1000.0
        
        # Costruisci comando FFmpeg
        cmd = [
            'ffmpeg',
            '-y',  # Sovrascrivi output
            '-ss', str(start_sec),  # Start time
            '-i', str(job.video_path),  # Input
            '-t', str(duration_sec),  # Duration
            '-c:v', encoder.value,  # Video codec
        ]
        
        # Aggiungi opzioni specifiche per encoder
        if encoder == HardwareEncoder.NVENC:
            cmd.extend(['-preset', 'p4'])  # NVENC preset (p1-p7)
            cmd.extend(['-b:v', '5M'])  # Bitrate
        elif encoder == HardwareEncoder.QSV:
            cmd.extend(['-preset', 'medium'])
            cmd.extend(['-b:v', '5M'])
        elif encoder in (HardwareEncoder.VAAPI, HardwareEncoder.VIDEOTOOLBOX):
            cmd.extend(['-b:v', '5M'])
        else:
            # Software encoding
            cmd.extend(['-preset', quality.value])
            cmd.extend(['-crf', '23'])  # Constant Rate Factor
        
        # Audio codec
        cmd.extend(['-c:a', 'aac', '-b:a', '192k'])
        
        # Output
        cmd.append(str(job.output_path))
        
        # Esegui FFmpeg
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=300  # 5 minuti timeout
        )
        
        if result.returncode == 0:
            return (True, None)
        else:
            error_msg = result.stderr[-500:] if result.stderr else "Unknown error"
            return (False, f"FFmpeg error: {error_msg}")
            
    except subprocess.TimeoutExpired:
        return (False, "Export timeout (>5 minuti)")
    except Exception as e:
        return (False, f"{type(e).__name__}: {str(e)}")


class AdvancedVideoExporter(QObject):
    """
    Export manager avanzato con supporto per:
    - Export paralleli
    - Hardware acceleration
    - Export queue con resume
    - Retry automatico
    """
    
    # Segnali
    finished = pyqtSignal(str)  # Messaggio di successo
    error = pyqtSignal(str)     # Messaggio di errore
    progress = pyqtSignal(str, int, int)  # (messaggio, current, total)
    job_completed = pyqtSignal(str)  # job_id completato
    job_failed = pyqtSignal(str, str)  # (job_id, error_message)
    
    def __init__(self, 
                 video_paths: Dict[int, Path],
                 markers: List[Marker],
                 sec_before: int,
                 sec_after: int,
                 export_dir: Path,  # Ora obbligatorio
                 quality: ExportQuality = ExportQuality.MEDIUM,
                 max_workers: Optional[int] = None,
                 enable_hardware: bool = True,
                 max_retries: int = 3,
                 parent: Optional[QObject] = None):
        super().__init__(parent)
        
        self.video_paths = video_paths
        self.markers = markers
        self.sec_before_ms = sec_before * 1000
        self.sec_after_ms = sec_after * 1000
        self.export_dir = export_dir  # Ora sempre fornito dal chiamante
        self.quality = quality
        self.enable_hardware = enable_hardware
        self.is_running = True
        self.max_retries = max_retries
        
        # Determina numero di worker (default: CPU count - 1, min 1)
        cpu_count = mp.cpu_count()
        self.max_workers = max_workers if max_workers else max(1, cpu_count - 1)
        
        # Export queue
        self.queue = ExportQueue()        # Rileva hardware encoder disponibili
        self.available_encoders = HardwareAccelerationDetector.detect_available_encoders()
        
        # Seleziona miglior encoder
        self.selected_encoder = self._select_best_encoder()
        
        logger.log_export_action(
            "AdvancedVideoExporter inizializzato",
            f"Workers: {self.max_workers}, Encoder: {self.selected_encoder.name}, "
            f"Quality: {self.quality.name}"
        )
    
    def _select_best_encoder(self) -> HardwareEncoder:
        """Seleziona il miglior encoder disponibile."""
        if not self.enable_hardware:
            return HardwareEncoder.NONE
        
        # Ordine di preferenza
        preference_order = [
            HardwareEncoder.NVENC,
            HardwareEncoder.QSV,
            HardwareEncoder.VIDEOTOOLBOX,
            HardwareEncoder.VAAPI,
            HardwareEncoder.NONE
        ]
        
        for encoder in preference_order:
            if encoder in self.available_encoders:
                return encoder
        
        return HardwareEncoder.NONE
    
    def run(self):
        """Esegue l'export con parallelizzazione."""
        try:
            # Verifica FFmpeg
            if not self._check_ffmpeg():
                self.error.emit(
                    "FFmpeg non trovato!\n\n"
                    "Installa FFmpeg per usare l'export avanzato:\n"
                    "- Ubuntu/Debian: sudo apt install ffmpeg\n"
                    "- macOS: brew install ffmpeg\n"
                    "- Windows: Scarica da ffmpeg.org"
                )
                return
            
            # Verifica prerequisiti
            if not self.video_paths:
                self.error.emit("Nessun video caricato.")
                return
            
            if not self.markers:
                self.error.emit("Nessun marker da esportare.")
                return
            
            # Crea directory export con timestamp "Export d.h:m:s"
            now = datetime.now()
            export_folder_name = f"Export {now.day:02d}.{now.hour:02d}:{now.minute:02d}:{now.second:02d}"
            self.export_dir = self.export_dir / export_folder_name
            self.export_dir.mkdir(parents=True, exist_ok=True)
            
            logger.log_export_action(
                "Cartella export creata",
                str(self.export_dir)
            )
            
            # Genera job di export
            jobs = self._create_export_jobs()
            
            if not jobs:
                self.error.emit("Nessun job valido da esportare.")
                return
            
            logger.log_export_action(
                "Avvio export parallelo",
                f"{len(jobs)} job, {self.max_workers} workers, {self.selected_encoder.name}"
            )
            
            # Aggiungi job alla coda
            self.queue.add_jobs(jobs)
            
            # Esegui export parallelo
            self._execute_parallel_export(jobs)
            
        except Exception as e:
            logger.log_error("Errore fatale nell'export avanzato", e)
            self.error.emit(f"Errore: {str(e)}")
    
    def _check_ffmpeg(self) -> bool:
        """Verifica che FFmpeg sia disponibile."""
        try:
            subprocess.run(
                ['ffmpeg', '-version'],
                capture_output=True,
                timeout=5
            )
            return True
        except (subprocess.TimeoutExpired, FileNotFoundError):
            return False
    
    def _create_export_jobs(self) -> List[ExportJob]:
        """Crea la lista di job di export."""
        jobs = []
        job_id_counter = 0
        
        for marker_idx, marker in enumerate(self.markers):
            # Determina video da esportare per questo marker
            video_indices = []
            
            if marker.video_index is None:
                # Marker globale: tutti i video
                video_indices = list(self.video_paths.keys())
            else:
                # Marker specifico
                if marker.video_index in self.video_paths:
                    video_indices = [marker.video_index]
            
            # Crea job per ogni video
            for video_idx in video_indices:
                video_path = self.video_paths[video_idx]
                
                if not video_path.exists():
                    logger.log_export_action(
                        f"Video {video_idx + 1} saltato",
                        "File non trovato"
                    )
                    continue
                
                # Calcola tempi
                start_ms = max(0, marker.timestamp - self.sec_before_ms)
                end_ms = marker.timestamp + self.sec_after_ms
                
                # Nome file output: "Clip X m:s->m:s.mp4" dove X è il numero della sorgente
                # Converti millisecondi in minuti e secondi
                start_min = int(start_ms / 1000 / 60)
                start_sec = int((start_ms / 1000) % 60)
                end_min = int(end_ms / 1000 / 60)
                end_sec = int((end_ms / 1000) % 60)
                
                # Formato: "Clip X m:s->m:s.mp4"
                filename = f"Clip {video_idx + 1} {start_min}:{start_sec:02d}->{end_min}:{end_sec:02d}.mp4"
                output_path = self.export_dir / filename
                
                # Crea job
                job = ExportJob(
                    job_id=f"job_{job_id_counter:04d}",
                    video_path=video_path,
                    video_index=video_idx,
                    marker_index=marker_idx,
                    marker_timestamp=marker.timestamp,
                    start_ms=start_ms,
                    end_ms=end_ms,
                    output_path=output_path,
                    max_retries=self.max_retries
                )
                
                jobs.append(job)
                job_id_counter += 1
        
        return jobs
    
    def _execute_parallel_export(self, jobs: List[ExportJob]) -> None:
        """Esegue l'export dei job in parallelo."""
        total_jobs = len(jobs)
        completed_jobs = 0
        failed_jobs = 0
        
        # Usa ProcessPoolExecutor per vero parallelismo (non limitato da GIL)
        with ProcessPoolExecutor(max_workers=self.max_workers) as executor:
            # Sottometti tutti i job
            future_to_job = {
                executor.submit(
                    export_clip_ffmpeg,
                    job,
                    self.quality,
                    self.selected_encoder
                ): job
                for job in jobs
            }
            
            # Processa i risultati man mano che completano
            for future in as_completed(future_to_job):
                if not self.is_running:
                    # Cancellazione richiesta
                    executor.shutdown(wait=False, cancel_futures=True)
                    self.error.emit("Export cancellato dall'utente.")
                    return
                
                job = future_to_job[future]
                
                try:
                    success, error_msg = future.result(timeout=10)
                    
                    if success:
                        # Export riuscito
                        completed_jobs += 1
                        self.queue.update_job(
                            job.job_id,
                            status=ExportStatus.COMPLETED,
                            progress=1.0,
                            end_time=time.time()
                        )
                        self.job_completed.emit(job.job_id)
                        
                        logger.log_export_action(
                            f"✓ Job {completed_jobs}/{total_jobs} completato",
                            job.output_path.name
                        )
                    else:
                        # Export fallito - tenta retry
                        if job.retry_count < job.max_retries:
                            # Retry
                            job.retry_count += 1
                            self.queue.update_job(
                                job.job_id,
                                status=ExportStatus.RETRYING,
                                retry_count=job.retry_count,
                                error_message=error_msg
                            )
                            
                            logger.log_export_action(
                                f"⟳ Retry {job.retry_count}/{job.max_retries}",
                                f"{job.output_path.name}: {error_msg}"
                            )
                            
                            # Re-sottometti il job
                            future_to_job[executor.submit(
                                export_clip_ffmpeg,
                                job,
                                self.quality,
                                self.selected_encoder
                            )] = job
                        else:
                            # Retry esauriti
                            failed_jobs += 1
                            self.queue.update_job(
                                job.job_id,
                                status=ExportStatus.FAILED,
                                error_message=error_msg,
                                end_time=time.time()
                            )
                            self.job_failed.emit(job.job_id, error_msg or "Unknown error")
                            
                            logger.log_export_action(
                                f"✗ Job fallito dopo {job.max_retries} retry",
                                f"{job.output_path.name}: {error_msg}"
                            )
                    
                    # Aggiorna progresso
                    self.progress.emit(
                        f"Export {completed_jobs + failed_jobs}/{total_jobs}",
                        completed_jobs + failed_jobs,
                        total_jobs
                    )
                    
                except Exception as e:
                    failed_jobs += 1
                    error_msg = f"{type(e).__name__}: {str(e)}"
                    self.queue.update_job(
                        job.job_id,
                        status=ExportStatus.FAILED,
                        error_message=error_msg,
                        end_time=time.time()
                    )
                    self.job_failed.emit(job.job_id, error_msg)
                    
                    logger.log_error(f"Errore export job {job.job_id}", e)
        
        # Export completato
        success_msg = f"Export completato!\n\n"
        success_msg += f"✓ Successi: {completed_jobs}/{total_jobs}\n"
        
        if failed_jobs > 0:
            success_msg += f"✗ Falliti: {failed_jobs}/{total_jobs}\n"
        
        success_msg += f"\nFile salvati in: {self.export_dir.name}"
        
        # Pulisci job completati dalla coda
        removed = self.queue.remove_completed_jobs()
        
        logger.log_export_action(
            "Export parallelo completato",
            f"Successi: {completed_jobs}, Falliti: {failed_jobs}, "
            f"Job rimossi dalla coda: {removed}"
        )
        
        self.finished.emit(success_msg)
    
    def stop(self):
        """Ferma il processo di export."""
        self.is_running = False
        logger.log_export_action("Export stop richiesto", "Cancellazione in corso...")
    
    def resume_failed_jobs(self) -> int:
        """Riprova ad esportare i job falliti."""
        failed_jobs = self.queue.get_failed_jobs()
        
        if not failed_jobs:
            return 0
        
        # Reset status e retry count
        for job in failed_jobs:
            self.queue.update_job(
                job.job_id,
                status=ExportStatus.PENDING,
                retry_count=0,
                error_message=None
            )
        
        logger.log_export_action(
            "Resume job falliti",
            f"{len(failed_jobs)} job rimessi in coda"
        )
        
        return len(failed_jobs)
    
