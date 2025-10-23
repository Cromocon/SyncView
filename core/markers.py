"""
Sistema di gestione markers per timeline video.
Permette di annotare punti specifici nei video con etichette, colori e descrizioni.
"""

from typing import List, Dict, Optional
from dataclasses import dataclass, field, asdict
from datetime import datetime
import json
from pathlib import Path


@dataclass
class Marker:
    """Rappresenta un marker sulla timeline."""
    
    timestamp: int  # Posizione in millisecondi
    color: str  # Colore esadecimale (es. "#ff0000")
    description: str = ""  # Descrizione dettagliata opzionale
    category: str = "default"  # Categoria per filtri
    video_index: Optional[int] = None  # Indice del video (0-3), None = tutti i video
    created_at: str = field(default_factory=lambda: datetime.now().isoformat())
    id: Optional[str] = None  # ID univoco auto-generato
    
    def __post_init__(self):
        """Genera ID univoco se non presente."""
        if self.id is None:
            self.id = f"marker_{self.timestamp}_{datetime.now().timestamp()}"
    
    def to_dict(self) -> Dict:
        """Converte marker in dizionario per serializzazione."""
        return asdict(self)
    
    @classmethod
    def from_dict(cls, data: Dict) -> 'Marker':
        """Crea marker da dizionario."""
        # Rimuovi il campo 'label' se presente (compatibilità con versioni vecchie)
        if 'label' in data:
            del data['label']
        return cls(**data)


class MarkerManager:
    """Gestisce la collezione di markers per un progetto."""
    
    # Colori predefiniti per markers
    DEFAULT_COLORS = {
        'red': '#e74c3c',      # Rosso - Eventi critici
        'yellow': '#f39c12',   # Giallo - Attenzione
        'green': '#2ecc71',    # Verde - Positivo
        'blue': '#3498db',     # Blu - Info
        'purple': '#9b59b6',   # Viola - Speciale
        'orange': '#e67e22',   # Arancio - Alert
        'cyan': '#1abc9c',     # Ciano - Nota
        'pink': '#e91e63',     # Rosa - Highlight
    }
    
    # Categorie predefinite
    DEFAULT_CATEGORIES = [
        'default',
        'action',      # Azioni tattiche
        'event',       # Eventi importanti
        'note',        # Note generali
        'highlight',   # Momenti salienti
        'review',      # Da rivedere
    ]
    
    def __init__(self, project_path: Optional[Path] = None):
        """
        Inizializza il manager dei markers.
        
        Args:
            project_path: Path del file di progetto (opzionale)
        """
        self.markers: List[Marker] = []
        self.project_path = project_path
        self.auto_save_enabled = True
        self._modified = False
    
    def add_marker(self, timestamp: int, color: str = '#3498db', 
                   description: str = "", category: str = "default", 
                   video_index: Optional[int] = None) -> Marker:
        """
        Aggiunge un nuovo marker.
        
        Args:
            timestamp: Posizione in millisecondi
            color: Colore esadecimale
            description: Descrizione opzionale
            category: Categoria del marker
            video_index: Indice del video (0-3), None = tutti i video (SYNC mode)
            
        Returns:
            Il marker creato
        """
        marker = Marker(
            timestamp=timestamp,
            color=color,
            description=description,
            category=category,
            video_index=video_index
        )
        self.markers.append(marker)
        self.markers.sort(key=lambda m: m.timestamp)  # Mantieni ordinato
        self._modified = True
        
        if self.auto_save_enabled and self.project_path:
            self.save()
        
        return marker
    
    def remove_marker(self, marker_id: str) -> bool:
        """
        Rimuove un marker per ID.
        
        Args:
            marker_id: ID del marker da rimuovere
            
        Returns:
            True se rimosso, False se non trovato
        """
        for i, marker in enumerate(self.markers):
            if marker.id == marker_id:
                self.markers.pop(i)
                self._modified = True
                
                if self.auto_save_enabled and self.project_path:
                    self.save()
                
                return True
        return False
    
    def update_marker(self, marker_id: str, **kwargs) -> Optional[Marker]:
        """
        Aggiorna attributi di un marker.
        
        Args:
            marker_id: ID del marker da aggiornare
            **kwargs: Attributi da modificare (label, color, description, etc.)
            
        Returns:
            Il marker aggiornato o None se non trovato
        """
        for marker in self.markers:
            if marker.id == marker_id:
                for key, value in kwargs.items():
                    if hasattr(marker, key):
                        setattr(marker, key, value)
                
                # Riordina se timestamp cambiato
                if 'timestamp' in kwargs:
                    self.markers.sort(key=lambda m: m.timestamp)
                
                self._modified = True
                
                if self.auto_save_enabled and self.project_path:
                    self.save()
                
                return marker
        return None
    
    def get_marker_at(self, timestamp: int, tolerance: int = 500) -> Optional[Marker]:
        """
        Trova marker vicino a un timestamp.
        
        Args:
            timestamp: Timestamp di riferimento in ms
            tolerance: Tolleranza in ms (default 500ms = 0.5s)
            
        Returns:
            Il marker più vicino entro la tolleranza o None
        """
        closest = None
        min_distance = tolerance
        
        for marker in self.markers:
            distance = abs(marker.timestamp - timestamp)
            if distance <= min_distance:
                min_distance = distance
                closest = marker
        
        return closest
    
    def get_markers_in_range(self, start: int, end: int) -> List[Marker]:
        """
        Ottiene tutti i markers in un range temporale.
        
        Args:
            start: Timestamp iniziale in ms
            end: Timestamp finale in ms
            
        Returns:
            Lista di markers nel range
        """
        return [m for m in self.markers if start <= m.timestamp <= end]
    
    def get_markers_by_category(self, category: str) -> List[Marker]:
        """Filtra markers per categoria."""
        return [m for m in self.markers if m.category == category]
    
    def get_markers_by_color(self, color: str) -> List[Marker]:
        """Filtra markers per colore."""
        return [m for m in self.markers if m.color == color]
    
    def get_markers_for_video(self, video_index: int) -> List[Marker]:
        """Filtra markers per un video specifico.
        
        Args:
            video_index: Indice del video (0-3)
            
        Returns:
            Lista di markers che appartengono al video o sono globali (video_index=None)
        """
        return [m for m in self.markers if m.video_index is None or m.video_index == video_index]
    
    def get_next_marker(self, current_timestamp: int) -> Optional[Marker]:
        """
        Trova il prossimo marker dopo il timestamp corrente.
        
        Args:
            current_timestamp: Timestamp attuale in ms
            
        Returns:
            Il prossimo marker o None
        """
        for marker in self.markers:
            if marker.timestamp > current_timestamp:
                return marker
        return None
    
    def get_previous_marker(self, current_timestamp: int) -> Optional[Marker]:
        """
        Trova il marker precedente al timestamp corrente.
        
        Args:
            current_timestamp: Timestamp attuale in ms
            
        Returns:
            Il marker precedente o None
        """
        for marker in reversed(self.markers):
            if marker.timestamp < current_timestamp:
                return marker
        return None
    
    def clear_all(self):
        """Rimuove tutti i markers."""
        self.markers.clear()
        self._modified = True
        
        if self.auto_save_enabled and self.project_path:
            self.save()
    
    def save(self, path: Optional[Path] = None) -> bool:
        """
        Salva markers su file JSON.
        
        Args:
            path: Path del file (usa project_path se None)
            
        Returns:
            True se salvato con successo
        """
        save_path = path or self.project_path
        
        if not save_path:
            return False
        
        try:
            data = {
                'version': '3.0',
                'created_at': datetime.now().isoformat(),
                'markers': [m.to_dict() for m in self.markers]
            }
            
            with open(save_path, 'w', encoding='utf-8') as f:
                json.dump(data, f, indent=2, ensure_ascii=False)
            
            self._modified = False
            return True
            
        except Exception as e:
            print(f"Errore salvataggio markers: {e}")
            return False
    
    def load(self, path: Optional[Path] = None) -> bool:
        """
        Carica markers da file JSON.
        
        Args:
            path: Path del file (usa project_path se None)
            
        Returns:
            True se caricato con successo
        """
        load_path = path or self.project_path
        
        if not load_path or not Path(load_path).exists():
            return False
        
        try:
            with open(load_path, 'r', encoding='utf-8') as f:
                data = json.load(f)
            
            self.markers.clear()
            
            for marker_data in data.get('markers', []):
                marker = Marker.from_dict(marker_data)
                self.markers.append(marker)
            
            self.markers.sort(key=lambda m: m.timestamp)
            self._modified = False
            return True
            
        except Exception as e:
            print(f"Errore caricamento markers: {e}")
            return False
    
    def export_csv(self, path: Path) -> bool:
        """
        Esporta markers in formato CSV.
        
        Args:
            path: Path del file CSV
            
        Returns:
            True se esportato con successo
        """
        try:
            import csv
            
            with open(path, 'w', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                writer.writerow(['Timestamp (ms)', 'Time', 'Category', 
                               'Color', 'Description', 'Created At'])
                
                for marker in self.markers:
                    # Converti timestamp in formato HH:MM:SS.mmm
                    total_seconds = marker.timestamp / 1000
                    hours = int(total_seconds // 3600)
                    minutes = int((total_seconds % 3600) // 60)
                    seconds = int(total_seconds % 60)
                    millis = int(marker.timestamp % 1000)
                    time_str = f"{hours:02d}:{minutes:02d}:{seconds:02d}.{millis:03d}"
                    
                    writer.writerow([
                        marker.timestamp,
                        time_str,
                        marker.category,
                        marker.color,
                        marker.description,
                        marker.created_at
                    ])
            
            return True
            
        except Exception as e:
            print(f"Errore export CSV: {e}")
            return False
    
    def get_statistics(self) -> Dict:
        """
        Ottiene statistiche sui markers.
        
        Returns:
            Dizionario con statistiche
        """
        stats = {
            'total': len(self.markers),
            'by_category': {},
            'by_color': {},
        }
        
        for marker in self.markers:
            # Conta per categoria
            if marker.category not in stats['by_category']:
                stats['by_category'][marker.category] = 0
            stats['by_category'][marker.category] += 1
            
            # Conta per colore
            if marker.color not in stats['by_color']:
                stats['by_color'][marker.color] = 0
            stats['by_color'][marker.color] += 1
        
        return stats
    
    @property
    def is_modified(self) -> bool:
        """Indica se ci sono modifiche non salvate."""
        return self._modified
    
    @property
    def count(self) -> int:
        """Numero totale di markers."""
        return len(self.markers)
