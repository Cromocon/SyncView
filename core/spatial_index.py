"""
Spatial indexing per markers sulla timeline.
Ottimizza le query per trovare markers in un range visibile usando bisect.
"""

import bisect
from typing import List, Tuple, Optional
from core.markers import Marker


class MarkerSpatialIndex:
    """Indice spaziale per query O(log n) sui markers.
    
    Mantiene i markers ordinati per timestamp e permette query
    efficienti su range temporali (es. markers visibili nell'area corrente).
    """
    
    def __init__(self):
        """Inizializza l'indice vuoto."""
        self._markers: List[Marker] = []
        self._timestamps: List[int] = []
        self._dirty = True
    
    def update(self, markers: List[Marker]) -> None:
        """Aggiorna l'indice con una nuova lista di markers.
        
        Args:
            markers: Lista di markers da indicizzare
        """
        self._markers = sorted(markers, key=lambda m: m.timestamp)
        self._timestamps = [m.timestamp for m in self._markers]
        self._dirty = False
    
    def query_range(self, start_ms: int, end_ms: int) -> List[Marker]:
        """Query efficiente per trovare markers in un range temporale.
        
        Usa bisect per trovare gli indici in O(log n) invece di O(n).
        
        Args:
            start_ms: Inizio del range (millisecondi)
            end_ms: Fine del range (millisecondi)
            
        Returns:
            Lista di markers nel range [start_ms, end_ms]
        """
        if not self._markers:
            return []
        
        # Trova primo marker >= start_ms
        left_idx = bisect.bisect_left(self._timestamps, start_ms)
        
        # Trova primo marker > end_ms
        right_idx = bisect.bisect_right(self._timestamps, end_ms)
        
        # Ritorna slice [left_idx:right_idx]
        return self._markers[left_idx:right_idx]
    
    def find_nearest(self, timestamp_ms: int, max_distance_ms: int = 1000) -> Optional[Marker]:
        """Trova il marker più vicino a un timestamp.
        
        Args:
            timestamp_ms: Timestamp target
            max_distance_ms: Distanza massima in ms
            
        Returns:
            Marker più vicino o None
        """
        if not self._markers:
            return None
        
        # Trova posizione di inserimento
        idx = bisect.bisect_left(self._timestamps, timestamp_ms)
        
        # Controlla marker a sinistra e destra
        candidates = []
        
        # Marker a sinistra
        if idx > 0:
            left_marker = self._markers[idx - 1]
            distance = abs(left_marker.timestamp - timestamp_ms)
            if distance <= max_distance_ms:
                candidates.append((distance, left_marker))
        
        # Marker a destra
        if idx < len(self._markers):
            right_marker = self._markers[idx]
            distance = abs(right_marker.timestamp - timestamp_ms)
            if distance <= max_distance_ms:
                candidates.append((distance, right_marker))
        
        if not candidates:
            return None
        
        # Ritorna il più vicino
        return min(candidates, key=lambda x: x[0])[1]
    
    def find_prev(self, timestamp_ms: int) -> Optional[Marker]:
        """Trova il marker precedente a un timestamp.
        
        Args:
            timestamp_ms: Timestamp di riferimento
            
        Returns:
            Marker precedente o None
        """
        if not self._markers:
            return None
        
        idx = bisect.bisect_left(self._timestamps, timestamp_ms)
        
        if idx > 0:
            return self._markers[idx - 1]
        return None
    
    def find_next(self, timestamp_ms: int) -> Optional[Marker]:
        """Trova il marker successivo a un timestamp.
        
        Args:
            timestamp_ms: Timestamp di riferimento
            
        Returns:
            Marker successivo o None
        """
        if not self._markers:
            return None
        
        idx = bisect.bisect_right(self._timestamps, timestamp_ms)
        
        if idx < len(self._markers):
            return self._markers[idx]
        return None
    
    def get_all(self) -> List[Marker]:
        """Ritorna tutti i markers ordinati.
        
        Returns:
            Lista di tutti i markers
        """
        return self._markers.copy()
    
    def count(self) -> int:
        """Ritorna il numero di markers nell'indice.
        
        Returns:
            Numero di markers
        """
        return len(self._markers)
    
    def is_empty(self) -> bool:
        """Verifica se l'indice è vuoto.
        
        Returns:
            True se non ci sono markers
        """
        return len(self._markers) == 0


class ViewportCalculator:
    """Calcola il viewport visibile della timeline per ottimizzare il rendering.
    
    Determina quali markers sono visibili nell'area corrente e
    fornisce informazioni per il culling del rendering.
    """
    
    def __init__(self):
        """Inizializza il calculator."""
        self.width = 0
        self.duration_ms = 0
        self.margin_ms = 0  # Margine aggiuntivo per pre-rendering
    
    def update_dimensions(self, width: int, duration_ms: int, margin_percent: float = 0.1):
        """Aggiorna le dimensioni del viewport.
        
        Args:
            width: Larghezza del widget in pixel
            duration_ms: Durata totale della timeline
            margin_percent: Percentuale di margine per pre-rendering (default 10%)
        """
        self.width = width
        self.duration_ms = duration_ms
        # Margine per pre-renderizzare markers appena fuori viewport
        self.margin_ms = int(duration_ms * margin_percent)
    
    def get_visible_range(self, scroll_offset: int = 0) -> Tuple[int, int]:
        """Calcola il range temporale visibile.
        
        Args:
            scroll_offset: Offset di scroll (per timeline scrollabili)
            
        Returns:
            Tuple (start_ms, end_ms) del range visibile con margine
        """
        if self.duration_ms == 0:
            return (0, 0)
        
        # Per ora, timeline non è scrollabile, quindi range = [0, duration]
        # Con margine
        start_ms = max(0, -self.margin_ms)
        end_ms = min(self.duration_ms, self.duration_ms + self.margin_ms)
        
        return (start_ms, end_ms)
    
    def is_position_visible(self, timestamp_ms: int, scroll_offset: int = 0) -> bool:
        """Verifica se una posizione temporale è visibile.
        
        Args:
            timestamp_ms: Posizione da verificare
            scroll_offset: Offset di scroll
            
        Returns:
            True se la posizione è visibile
        """
        start_ms, end_ms = self.get_visible_range(scroll_offset)
        return start_ms <= timestamp_ms <= end_ms
    
    def timestamp_to_x(self, timestamp_ms: int) -> int:
        """Converte timestamp in coordinata X pixel.
        
        Args:
            timestamp_ms: Timestamp in millisecondi
            
        Returns:
            Coordinata X in pixel
        """
        if self.duration_ms == 0:
            return 10
        
        ratio = timestamp_ms / self.duration_ms
        # Mappa sul range [10, width - 10]
        return int(10 + (self.width - 20) * ratio)
    
    def x_to_timestamp(self, x: int) -> int:
        """Converte coordinata X in timestamp.
        
        Args:
            x: Coordinata X in pixel
            
        Returns:
            Timestamp in millisecondi
        """
        if self.width - 20 == 0:
            return 0
        
        ratio = (x - 10) / (self.width - 20)
        ratio = max(0.0, min(1.0, ratio))  # Clamp 0-1
        return int(self.duration_ms * ratio)
