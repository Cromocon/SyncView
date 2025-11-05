"""
Gestione delle path utilizzate dall'utente.
Memorizza e recupera le ultime path usate per video e export.
"""

from pathlib import Path
import json
from typing import Optional, List, Dict


# Salva in ~/.syncview/ invece che nella directory del progetto
USER_PATHS_FILE = Path.home() / ".syncview" / "user_paths.json"


class UserPathManager:
    """Gestisce il salvataggio e caricamento delle path utilizzate dall'utente."""
    
    def __init__(self):
        self.paths_file = USER_PATHS_FILE
        # Assicura che la directory esista
        self.paths_file.parent.mkdir(parents=True, exist_ok=True)
        self.video_paths: List[Optional[Path]] = [None, None, None, None]  # 4 video player
        self.last_export_dir: Optional[Path] = None
        self.load_paths()
    
    def load_paths(self) -> None:
        """Carica le path salvate dal file JSON."""
        if not self.paths_file.exists():
            return
        
        try:
            with open(self.paths_file, 'r', encoding='utf-8') as f:
                data = json.load(f)
            
            # Carica video paths
            video_paths_data = data.get('video_paths', [None] * 4)
            self.video_paths = [
                Path(p) if p else None 
                for p in video_paths_data
            ]
            
            # Carica export dir
            export_dir_data = data.get('last_export_dir')
            self.last_export_dir = Path(export_dir_data) if export_dir_data else None
            
        except Exception as e:
            print(f"Errore caricamento user paths: {e}")
            # Usa default vuoti
            self.video_paths = [None] * 4
            self.last_export_dir = None
    
    def save_paths(self) -> None:
        """Salva le path correnti nel file JSON."""
        try:
            data = {
                'video_paths': [
                    str(p) if p else None 
                    for p in self.video_paths
                ],
                'last_export_dir': str(self.last_export_dir) if self.last_export_dir else None
            }
            
            # Assicurati che la directory esista
            self.paths_file.parent.mkdir(parents=True, exist_ok=True)
            
            with open(self.paths_file, 'w', encoding='utf-8') as f:
                json.dump(data, f, indent=2)
            
            # Log per confermare il salvataggio
            from core.logger import logger
            logger.log_user_action(
                "user_paths.json salvato",
                f"File: {self.paths_file}"
            )
                
        except Exception as e:
            print(f"Errore salvataggio user paths: {e}")
            import traceback
            traceback.print_exc()
    
    def set_video_path(self, index: int, path: Path) -> None:
        """
        Imposta il path di un video.
        
        Args:
            index: Indice del video (0-3)
            path: Path del file video
        """
        if 0 <= index < 4:
            self.video_paths[index] = path
            self.save_paths()
            # Log per verificare il salvataggio
            from core.logger import logger
            logger.log_user_action(
                f"Percorso salvato in user_paths",
                f"Slot {index}: {path}"
            )
    
    def get_video_path(self, index: int) -> Optional[Path]:
        """
        Ottiene il path di un video.
        
        Args:
            index: Indice del video (0-3)
            
        Returns:
            Path del video o None
        """
        if 0 <= index < 4:
            return self.video_paths[index]
        return None
    
    def clear_video_path(self, index: int) -> None:
        """
        Cancella il path di un video.
        
        Args:
            index: Indice del video (0-3)
        """
        if 0 <= index < 4:
            self.video_paths[index] = None
            self.save_paths()
    
    def set_export_dir(self, path: Path) -> None:
        """
        Imposta la directory di export.
        
        Args:
            path: Path della directory
        """
        self.last_export_dir = path
        self.save_paths()
    
    def get_export_dir(self) -> Optional[Path]:
        """
        Ottiene l'ultima directory di export usata.
        
        Returns:
            Path della directory o None
        """
        return self.last_export_dir
    
    def get_valid_video_paths(self) -> Dict[int, Path]:
        """
        Ottiene tutte le video path che esistono ancora sul filesystem.
        Pulisce automaticamente i percorsi non validi dal file.
        
        Returns:
            Dizionario {index: path} solo per file esistenti
        """
        valid_paths = {}
        paths_changed = False
        
        for i, path in enumerate(self.video_paths):
            if path:
                if path.exists() and path.is_file():
                    valid_paths[i] = path
                else:
                    # Il file non esiste più, rimuovilo
                    self.video_paths[i] = None
                    paths_changed = True
                    from core.logger import logger
                    logger.log_user_action(
                        f"Percorso non valido rimosso",
                        f"Slot {i}: {path} (file non trovato)"
                    )
        
        # Se ci sono stati cambiamenti, salva il file aggiornato
        if paths_changed:
            self.save_paths()
        
        return valid_paths


# Istanza globale
user_path_manager = UserPathManager()
