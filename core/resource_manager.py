"""
Resource Manager per SyncView.
Gestisce cleanup deterministico di risorse (moviepy, ffmpeg, QMediaPlayer).
"""

import gc
import weakref
from typing import Optional, List, Any
from pathlib import Path
from core.logger import logger

# Import moviepy solo se disponibile
try:
    from moviepy.video.io.VideoFileClip import VideoFileClip
    MOVIEPY_AVAILABLE = True
except ImportError:
    MOVIEPY_AVAILABLE = False
    VideoFileClip = None


class ResourceHandle:
    """
    Context manager per gestione sicura di risorse video.
    
    Garantisce che le risorse siano sempre chiuse, anche in caso di eccezioni.
    """
    
    def __init__(self, resource: Any, resource_type: str, resource_id: str = ""):
        """
        Args:
            resource: Risorsa da gestire (VideoFileClip, QMediaPlayer, etc)
            resource_type: Tipo risorsa ('moviepy', 'qmedia', 'ffmpeg')
            resource_id: Identificativo per logging (es. filename)
        """
        self.resource = resource
        self.resource_type = resource_type
        self.resource_id = resource_id
        self._closed = False
        
    def __enter__(self):
        """Entra nel context manager."""
        return self.resource
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Esce dal context manager e garantisce cleanup."""
        self.close()
        return False  # Non sopprime eccezioni
    
    def close(self) -> None:
        """Chiude la risorsa in modo sicuro."""
        if self._closed or self.resource is None:
            return
        
        try:
            if self.resource_type == 'moviepy':
                # MoviePy VideoFileClip
                if hasattr(self.resource, 'close'):
                    self.resource.close()
                if hasattr(self.resource, 'reader') and self.resource.reader:
                    self.resource.reader.close()
                logger.log_user_action("Resource cleanup", f"MoviePy clip chiuso: {self.resource_id}")
                
            elif self.resource_type == 'qmedia':
                # QMediaPlayer
                if hasattr(self.resource, 'stop'):
                    self.resource.stop()
                if hasattr(self.resource, 'setSource'):
                    from PyQt6.QtCore import QUrl
                    self.resource.setSource(QUrl())
                logger.log_user_action("Resource cleanup", f"QMediaPlayer stopped: {self.resource_id}")
                
            self._closed = True
            
        except Exception as e:
            logger.log_error(f"Errore durante chiusura risorsa {self.resource_type} ({self.resource_id})", e)
        finally:
            # Rimuovi riferimento forte
            self.resource = None


class ResourcePool:
    """
    Pool di risorse con tracking e cleanup automatico.
    
    Tiene traccia di tutte le risorse attive e garantisce cleanup
    anche se non esplicitamente richiesto.
    """
    
    def __init__(self, name: str = "default"):
        """
        Args:
            name: Nome del pool per logging
        """
        self.name = name
        self._handles: List[weakref.ref] = []  # Weak references per evitare memory leaks
        
    def register(self, handle: ResourceHandle) -> ResourceHandle:
        """
        Registra un handle nel pool.
        
        Args:
            handle: ResourceHandle da registrare
            
        Returns:
            Lo stesso handle (per chaining)
        """
        # Usa weak reference per non tenere in vita l'oggetto
        self._handles.append(weakref.ref(handle))
        return handle
    
    def create_handle(self, resource: Any, resource_type: str, resource_id: str = "") -> ResourceHandle:
        """
        Crea e registra un nuovo handle.
        
        Args:
            resource: Risorsa da gestire
            resource_type: Tipo risorsa
            resource_id: ID per logging
            
        Returns:
            ResourceHandle registrato
        """
        handle = ResourceHandle(resource, resource_type, resource_id)
        return self.register(handle)
    
    def cleanup_all(self, force_gc: bool = True) -> int:
        """
        Chiude tutte le risorse ancora attive nel pool.
        
        Args:
            force_gc: Se True, forza garbage collection dopo cleanup
            
        Returns:
            Numero di risorse chiuse
        """
        closed_count = 0
        
        # Cleanup handles ancora vivi
        for weak_ref in self._handles:
            handle = weak_ref()  # Ottieni riferimento forte temporaneo
            if handle and not handle._closed:
                handle.close()
                closed_count += 1
        
        # Pulisci lista (rimuovi dead references)
        alive_handles = []
        for ref in self._handles:
            handle = ref()
            if handle is not None and not handle._closed:
                alive_handles.append(ref)
        self._handles = alive_handles
        
        if closed_count > 0:
            logger.log_user_action("Resource cleanup", f"Pool '{self.name}': {closed_count} risorse chiuse")
        
        # Garbage collection deterministico
        if force_gc:
            collected = gc.collect()
            logger.log_user_action("GC", f"{collected} oggetti raccolti dopo cleanup pool '{self.name}'")
        
        return closed_count
    
    def get_active_count(self) -> int:
        """
        Conta risorse ancora attive.
        
        Returns:
            Numero di risorse non ancora chiuse
        """
        # Pulisci dead references
        self._handles = [ref for ref in self._handles if ref() is not None]
        
        active = 0
        for ref in self._handles:
            handle = ref()
            if handle and not handle._closed:
                active += 1
        return active


class ScopedResource:
    """
    Context manager semplificato per oggetti Python standard.
    
    Garantisce che 'del' venga chiamato e GC sia eseguito.
    Utile per oggetti grandi come numpy arrays, PIL images, etc.
    """
    
    def __init__(self, obj: Any, name: str = ""):
        """
        Args:
            obj: Oggetto da gestire
            name: Nome per logging
        """
        self.obj = obj
        self.name = name
        
    def __enter__(self):
        """Entra nel context."""
        return self.obj
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Esce e forza cleanup."""
        if self.obj is not None:
            # Explicit delete
            del self.obj
            self.obj = None
            
            # Force GC
            collected = gc.collect()
            if self.name:
                logger.log_user_action("Resource cleanup", f"ScopedResource '{self.name}' deleted, GC: {collected} objects")
        
        return False


# Global pool per tracking generale
_global_pool = ResourcePool("global")


def get_global_pool() -> ResourcePool:
    """
    Ottiene il pool globale di risorse.
    
    Returns:
        Il pool globale
    """
    return _global_pool


def cleanup_global_resources(force_gc: bool = True) -> int:
    """
    Cleanup di tutte le risorse nel pool globale.
    
    Args:
        force_gc: Se True, forza garbage collection
        
    Returns:
        Numero di risorse chiuse
    """
    return _global_pool.cleanup_all(force_gc=force_gc)
