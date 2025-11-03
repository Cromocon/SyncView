"""
Debounce utility per ottimizzare update rapidi.
Throttle/debounce per evitare troppi repaint durante drag o scroll.
"""

from PyQt6.QtCore import QTimer, QObject, pyqtSignal
from typing import Callable, Any, Optional
import time


class Debouncer(QObject):
    """Debouncer per ritardare l'esecuzione di una funzione fino a quando
    non passano N millisecondi senza nuove chiamate.
    
    Utile per eventi che si ripetono rapidamente (es. mouse move durante drag).
    """
    
    # Segnale emesso quando la funzione viene eseguita
    executed = pyqtSignal()
    
    def __init__(self, delay_ms: int = 50, parent: Optional[QObject] = None):
        """
        Args:
            delay_ms: Delay in millisecondi prima dell'esecuzione
            parent: Parent QObject
        """
        super().__init__(parent)
        self.delay_ms = delay_ms
        self.timer = QTimer(self)
        self.timer.setSingleShot(True)
        self.timer.timeout.connect(self._on_timeout)
        self.callback: Optional[Callable] = None
        self.args: tuple = ()
        self.kwargs: dict = {}
    
    def call(self, callback: Callable, *args: Any, **kwargs: Any) -> None:
        """Schedula una chiamata debounced.
        
        Se chiamato più volte rapidamente, solo l'ultima chiamata
        verrà eseguita dopo il delay.
        
        Args:
            callback: Funzione da chiamare
            *args: Argomenti posizionali
            **kwargs: Argomenti keyword
        """
        self.callback = callback
        self.args = args
        self.kwargs = kwargs
        
        # Resetta timer (cancella chiamata precedente)
        self.timer.stop()
        self.timer.start(self.delay_ms)
    
    def _on_timeout(self) -> None:
        """Esegue il callback quando il timer scade."""
        if self.callback:
            self.callback(*self.args, **self.kwargs)
            self.executed.emit()
    
    def cancel(self) -> None:
        """Cancella una chiamata pendente."""
        self.timer.stop()
        self.callback = None
    
    def is_pending(self) -> bool:
        """Verifica se c'è una chiamata in attesa.
        
        Returns:
            True se il timer è attivo
        """
        return self.timer.isActive()


class Throttler(QObject):
    """Throttler per limitare la frequenza di esecuzione di una funzione.
    
    Garantisce che la funzione non venga chiamata più spesso di
    una volta ogni N millisecondi, eseguendo sempre la prima e l'ultima chiamata.
    
    Utile per update di UI durante scroll o drag continui.
    """
    
    # Segnale emesso quando la funzione viene eseguita
    executed = pyqtSignal()
    
    def __init__(self, interval_ms: int = 16, parent: Optional[QObject] = None):
        """
        Args:
            interval_ms: Intervallo minimo tra chiamate (default 16ms = ~60fps)
            parent: Parent QObject
        """
        super().__init__(parent)
        self.interval_ms = interval_ms
        self.last_call_time = 0.0
        self.pending_call: Optional[tuple] = None
        self.timer = QTimer(self)
        self.timer.setSingleShot(True)
        self.timer.timeout.connect(self._execute_pending)
    
    def call(self, callback: Callable, *args: Any, **kwargs: Any) -> None:
        """Esegue una chiamata throttled.
        
        - Se è passato abbastanza tempo dall'ultima chiamata, esegue subito
        - Altrimenti, schedula l'esecuzione dopo l'intervallo minimo
        - Se già c'è una chiamata pendente, la sostituisce (trailing edge)
        
        Args:
            callback: Funzione da chiamare
            *args: Argomenti posizionali
            **kwargs: Argomenti keyword
        """
        current_time = time.perf_counter()
        time_since_last = (current_time - self.last_call_time) * 1000  # in ms
        
        if time_since_last >= self.interval_ms:
            # Abbastanza tempo è passato, esegui subito (leading edge)
            self._execute(callback, args, kwargs)
        else:
            # Troppo presto, schedula per dopo
            self.pending_call = (callback, args, kwargs)
            
            if not self.timer.isActive():
                # Calcola quanto aspettare
                remaining_ms = int(self.interval_ms - time_since_last)
                self.timer.start(max(1, remaining_ms))
    
    def _execute(self, callback: Callable, args: tuple, kwargs: dict) -> None:
        """Esegue il callback e aggiorna il timestamp."""
        callback(*args, **kwargs)
        self.last_call_time = time.perf_counter()
        self.executed.emit()
    
    def _execute_pending(self) -> None:
        """Esegue la chiamata pendente (trailing edge)."""
        if self.pending_call:
            callback, args, kwargs = self.pending_call
            self.pending_call = None
            self._execute(callback, args, kwargs)
    
    def cancel(self) -> None:
        """Cancella qualsiasi chiamata pendente."""
        self.timer.stop()
        self.pending_call = None
    
    def is_pending(self) -> bool:
        """Verifica se c'è una chiamata in attesa.
        
        Returns:
            True se c'è una chiamata pendente
        """
        return self.pending_call is not None or self.timer.isActive()


class UpdateScheduler:
    """Scheduler intelligente per update della UI.
    
    Combina debouncing e throttling per ottimizzare gli update:
    - Durante eventi rapidi (drag): throttle a ~60fps
    - Dopo eventi: debounce per cleanup finale
    """
    
    def __init__(self, throttle_ms: int = 16, debounce_ms: int = 50):
        """
        Args:
            throttle_ms: Intervallo throttle (default 16ms = 60fps)
            debounce_ms: Delay debounce (default 50ms)
        """
        self.throttler = Throttler(throttle_ms)
        self.debouncer = Debouncer(debounce_ms)
        self.active_mode = 'idle'  # 'idle', 'dragging', 'debouncing'
    
    def schedule_update(self, callback: Callable, mode: str = 'normal') -> None:
        """Schedula un update con la strategia appropriata.
        
        Args:
            callback: Funzione da eseguire
            mode: 'fast' (throttle), 'normal' (debounce), 'immediate' (esegui subito)
        """
        if mode == 'immediate':
            callback()
            self.active_mode = 'idle'
        elif mode == 'fast':
            # Usa throttle per update rapidi
            self.active_mode = 'dragging'
            self.throttler.call(callback)
        elif mode == 'normal':
            # Usa debounce per update finali
            self.active_mode = 'debouncing'
            self.debouncer.call(callback)
    
    def cancel_all(self) -> None:
        """Cancella tutti gli update pendenti."""
        self.throttler.cancel()
        self.debouncer.cancel()
        self.active_mode = 'idle'
    
    def is_busy(self) -> bool:
        """Verifica se ci sono update in corso.
        
        Returns:
            True se ci sono update pendenti
        """
        return self.throttler.is_pending() or self.debouncer.is_pending()
