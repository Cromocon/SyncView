"""
Sistema di caching dei frame per migliorare le performance di seek e playback.
Implementa una cache LRU (Least Recently Used) per i frame video.
"""

from collections import OrderedDict
from typing import Optional, Any
from pathlib import Path
import threading
from core.logger import logger


class LRUCache:
    """Cache LRU thread-safe per frame video.
    
    Mantiene una cache ordinata dei frame più recentemente accessi,
    eliminando automaticamente quelli meno usati quando la cache si riempie.
    """
    
    def __init__(self, capacity: int = 50):
        """
        Args:
            capacity: Numero massimo di frame da mantenere in cache
        """
        self.capacity = capacity
        self.cache: OrderedDict[str, Any] = OrderedDict()
        self.lock = threading.Lock()
        self.hits = 0
        self.misses = 0
    
    def get(self, key: str) -> Optional[Any]:
        """Recupera un elemento dalla cache.
        
        Args:
            key: Chiave dell'elemento
            
        Returns:
            L'elemento se presente, altrimenti None
        """
        with self.lock:
            if key in self.cache:
                # Sposta alla fine (più recente)
                self.cache.move_to_end(key)
                self.hits += 1
                return self.cache[key]
            else:
                self.misses += 1
                return None
    
    def put(self, key: str, value: Any) -> None:
        """Inserisce un elemento nella cache.
        
        Args:
            key: Chiave dell'elemento
            value: Valore da cachare
        """
        with self.lock:
            if key in self.cache:
                # Aggiorna e sposta alla fine
                self.cache.move_to_end(key)
            else:
                # Aggiungi nuovo elemento
                self.cache[key] = value
                # Se supera la capacità, rimuovi il meno recente
                if len(self.cache) > self.capacity:
                    oldest = next(iter(self.cache))
                    del self.cache[oldest]
    
    def clear(self) -> None:
        """Svuota la cache."""
        with self.lock:
            self.cache.clear()
            self.hits = 0
            self.misses = 0
    
    def get_stats(self) -> dict:
        """Ritorna statistiche sulla cache.
        
        Returns:
            Dict con hits, misses, size, hit_rate
        """
        with self.lock:
            total = self.hits + self.misses
            hit_rate = (self.hits / total * 100) if total > 0 else 0
            return {
                'hits': self.hits,
                'misses': self.misses,
                'size': len(self.cache),
                'capacity': self.capacity,
                'hit_rate': hit_rate
            }
    
    def resize(self, new_capacity: int) -> None:
        """Ridimensiona la capacità della cache.
        
        Args:
            new_capacity: Nuova capacità massima
        """
        with self.lock:
            self.capacity = new_capacity
            # Rimuovi elementi in eccesso
            while len(self.cache) > self.capacity:
                oldest = next(iter(self.cache))
                del self.cache[oldest]


class FrameCache:
    """Gestore della cache frame per un singolo video.
    
    Mantiene informazioni sulle posizioni visitate e predice
    le posizioni future durante il playback per ottimizzare il buffering.
    """
    
    def __init__(self, video_path: Path, cache_size: int = 50):
        """
        Args:
            video_path: Path del file video
            cache_size: Dimensione della cache LRU
        """
        self.video_path = video_path
        self.cache = LRUCache(capacity=cache_size)
        
        # Tracking delle posizioni
        self.last_position = 0
        self.position_history: list[int] = []
        self.max_history = 10
        
        # Predecoding
        self.is_playing = False
        self.playback_direction = 1  # 1 = avanti, -1 = indietro
        self.predecode_distance = 1000  # ms da pre-caricare
    
    def _make_key(self, position_ms: int) -> str:
        """Crea una chiave cache da una posizione.
        
        Args:
            position_ms: Posizione in millisecondi
            
        Returns:
            Chiave stringa per la cache
        """
        # Arrotonda alla posizione più vicina (ogni 100ms)
        rounded = (position_ms // 100) * 100
        return f"{self.video_path.name}:{rounded}"
    
    def mark_position_visited(self, position_ms: int) -> None:
        """Marca una posizione come visitata.
        
        Args:
            position_ms: Posizione in millisecondi
        """
        key = self._make_key(position_ms)
        self.cache.put(key, {'timestamp': position_ms, 'visited': True})
        
        # Aggiorna history
        self.position_history.append(position_ms)
        if len(self.position_history) > self.max_history:
            self.position_history.pop(0)
        
        # Determina direzione di playback
        if len(self.position_history) >= 2:
            delta = self.position_history[-1] - self.position_history[-2]
            if abs(delta) > 50:  # Ignora micro-movimenti
                self.playback_direction = 1 if delta > 0 else -1
        
        self.last_position = position_ms
    
    def is_position_cached(self, position_ms: int) -> bool:
        """Verifica se una posizione è in cache.
        
        Args:
            position_ms: Posizione in millisecondi
            
        Returns:
            True se la posizione è stata visitata recentemente
        """
        key = self._make_key(position_ms)
        return self.cache.get(key) is not None
    
    def get_predecode_positions(self, current_position: int, count: int = 5) -> list[int]:
        """Calcola le posizioni da pre-decodificare.
        
        Durante il playback, predice le prossime posizioni da caricare
        per rendere il seek più fluido.
        
        Args:
            current_position: Posizione corrente in ms
            count: Numero di posizioni da predire
            
        Returns:
            Lista di posizioni (in ms) da pre-caricare
        """
        positions = []
        step = self.predecode_distance // count
        
        for i in range(1, count + 1):
            future_pos = current_position + (step * i * self.playback_direction)
            if future_pos >= 0:  # Non andare sotto 0
                positions.append(future_pos)
        
        return positions
    
    def set_playing(self, playing: bool) -> None:
        """Imposta lo stato di playback.
        
        Args:
            playing: True se in riproduzione
        """
        self.is_playing = playing
    
    def clear(self) -> None:
        """Pulisce la cache."""
        self.cache.clear()
        self.position_history.clear()
        logger.log_user_action(
            f"Cache pulita per {self.video_path.name}",
            f"Stats: {self.get_stats()}"
        )
    
    def get_stats(self) -> dict:
        """Ritorna statistiche sulla cache.
        
        Returns:
            Dict con statistiche cache e playback
        """
        stats = self.cache.get_stats()
        stats['video'] = self.video_path.name
        stats['last_position'] = self.last_position
        stats['is_playing'] = self.is_playing
        stats['playback_direction'] = 'forward' if self.playback_direction > 0 else 'backward'
        return stats
    
    def optimize_for_seek(self, target_position: int, duration: int) -> list[int]:
        """Ottimizza la cache per un seek imminente.
        
        Suggerisce posizioni da pre-caricare intorno alla posizione target.
        
        Args:
            target_position: Posizione target del seek (ms)
            duration: Durata totale del video (ms)
            
        Returns:
            Lista di posizioni da pre-caricare
        """
        positions = []
        
        # Carica la posizione esatta
        positions.append(target_position)
        
        # Carica posizioni vicine (±2 secondi)
        for offset in [-2000, -1000, 1000, 2000]:
            pos = target_position + offset
            if 0 <= pos <= duration:
                positions.append(pos)
        
        return positions


class FrameCacheManager:
    """Gestore globale delle cache frame per tutti i video.
    
    Coordina le cache di tutti i video player e fornisce
    statistiche aggregate.
    """
    
    def __init__(self, cache_size_per_video: int = 50):
        """
        Args:
            cache_size_per_video: Dimensione cache per ogni video
        """
        self.caches: dict[int, FrameCache] = {}
        self.cache_size = cache_size_per_video
        self.enabled = True
    
    def create_cache(self, video_index: int, video_path: Path) -> FrameCache:
        """Crea una cache per un video.
        
        Args:
            video_index: Indice del video (0-3)
            video_path: Path del file video
            
        Returns:
            FrameCache creata
        """
        cache = FrameCache(video_path, self.cache_size)
        self.caches[video_index] = cache
        logger.log_user_action(
            f"Cache creata per Feed-{video_index + 1}",
            f"File: {video_path.name}, Size: {self.cache_size}"
        )
        return cache
    
    def get_cache(self, video_index: int) -> Optional[FrameCache]:
        """Recupera la cache di un video.
        
        Args:
            video_index: Indice del video
            
        Returns:
            FrameCache se esiste, altrimenti None
        """
        return self.caches.get(video_index)
    
    def remove_cache(self, video_index: int) -> None:
        """Rimuove la cache di un video.
        
        Args:
            video_index: Indice del video
        """
        if video_index in self.caches:
            self.caches[video_index].clear()
            del self.caches[video_index]
            logger.log_user_action(
                f"Cache rimossa per Feed-{video_index + 1}",
                "Cache deallocata"
            )
    
    def clear_all(self) -> None:
        """Pulisce tutte le cache."""
        for cache in self.caches.values():
            cache.clear()
        self.caches.clear()
        logger.log_user_action("Tutte le cache pulite", "Reset completo")
    
    def get_global_stats(self) -> dict:
        """Ritorna statistiche aggregate di tutte le cache.
        
        Returns:
            Dict con statistiche globali
        """
        total_hits = 0
        total_misses = 0
        total_size = 0
        
        for cache in self.caches.values():
            stats = cache.get_stats()
            total_hits += stats['hits']
            total_misses += stats['misses']
            total_size += stats['size']
        
        total = total_hits + total_misses
        hit_rate = (total_hits / total * 100) if total > 0 else 0
        
        return {
            'total_caches': len(self.caches),
            'total_hits': total_hits,
            'total_misses': total_misses,
            'total_cached_positions': total_size,
            'global_hit_rate': hit_rate,
            'enabled': self.enabled
        }
    
    def set_enabled(self, enabled: bool) -> None:
        """Abilita/disabilita il caching globale.
        
        Args:
            enabled: True per abilitare
        """
        self.enabled = enabled
        logger.log_user_action(
            "Frame caching",
            "ABILITATO" if enabled else "DISABILITATO"
        )
