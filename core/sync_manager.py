"""
Gestore avanzato della sincronizzazione video.
Gestisce offset, drift tolerance e sincronizzazione precisa.
"""

from core.logger import logger
# Import necessari per il type hinting (Dict per offset, List per players)
from typing import List, Dict, TYPE_CHECKING

# --- CORREZIONE PER L'ERRORE PYLANCE ---
# Questo blocco viene letto solo dall'analizzatore statico (Pylance)
# e non viene eseguito a runtime, evitando l'import circolare.
if TYPE_CHECKING:
    from ui.video_player import VideoPlayerWidget
# -------------------------------------


class SyncManager:
    """Gestisce la sincronizzazione avanzata tra i video."""

    def __init__(self, num_players: int = 4):
        """
        Inizializza il SyncManager.
        
        Args:
            num_players: Il numero di video player da gestire.
        """
        self.sync_enabled = True
        self.num_players = num_players
        self.video_offsets: Dict[int, int] = {i: 0 for i in range(num_players)}
        self.master_video_index = 0  # Indice del video master (default: primo)

    def set_sync_enabled(self, enabled):
        """Abilita o disabilita la sincronizzazione."""
        self.sync_enabled = enabled
        logger.log_user_action("Sincronizzazione", "ON" if enabled else "OFF")

    def is_sync_enabled(self) -> bool:
        """Verifica se la sincronizzazione è abilitata."""
        return self.sync_enabled

    def set_video_offset(self, video_index, offset_ms):
        """Imposta l'offset di un video specifico."""
        if 0 <= video_index < 4:
            old_offset = self.video_offsets[video_index]
            self.video_offsets[video_index] = int(offset_ms)
            logger.log_user_action(
                f"Offset Video {video_index + 1}",
                f"{old_offset}ms → {offset_ms}ms"
            )

    def get_video_offset(self, video_index):
        """Ottiene l'offset di un video specifico."""
        if 0 <= video_index < 4:
            return self.video_offsets.get(video_index, 0)

    def reset_all_offsets(self):
        """Resetta tutti gli offset a zero."""
        self.video_offsets = {i: 0 for i in range(self.num_players)}
        logger.log_user_action("Reset Offset", "Tutti gli offset azzerati")

    def calculate_sync_position(self, source_position, source_index, target_index):
        """
        Calcola la posizione sincronizzata per un video target.

        Args:
            source_position: Posizione del video sorgente in ms
            source_index: Indice del video sorgente
            target_index: Indice del video target

        Returns:
            Posizione sincronizzata per il video target in ms
        """
        # Calcola offset differenziale
        source_offset = self.get_video_offset(source_index)
        target_offset = self.get_video_offset(target_index)

        # Posizione sincronizzata = posizione sorgente - offset sorgente + offset target
        sync_position = source_position - source_offset + target_offset

        # Assicura che la posizione sia >= 0
        return max(0, sync_position)

    def set_master_video(self, video_index):
        """Imposta il video master per la sincronizzazione."""
        if 0 <= video_index < 4:
            old_master = self.master_video_index
            self.master_video_index = video_index
            # Logga solo se cambia effettivamente
            if old_master != video_index:
                 logger.log_user_action(
                     "Video Master",
                     f"Video {old_master + 1} → Video {video_index + 1}"
                 )

    def get_master_video_index(self):
        """Ottiene l'indice del video master."""
        return self.master_video_index

    # --- CORREZIONE PER L'ERRORE PYLANCE ---
    # Usiamo un "forward reference" (stringa) per il type hint
    # per evitare l'errore di import circolare a runtime.
    def sync_all_to_master(self, master_position: int, video_players: List['VideoPlayerWidget']):
        """
        Sincronizza tutti i video alla posizione del master.
        Questa funzione viene chiamata da `resync_all` (pulsante).

        Args:
            master_position: Posizione del video master in ms
            video_players: Lista dei video player
        """
        # Nota: Questa funzione deve funzionare anche se self.sync_enabled è False,
        # perché viene chiamata dal pulsante di resync manuale.

        logger.log_user_action(
            "Sync Manager: Inizio sync_all_to_master",
            f"Master Video {self.master_video_index + 1} @ {master_position}ms"
        )

        for i, player in enumerate(video_players):
            # Assicurati che il player esista e sia caricato
            if player and player.is_loaded:
                if i != self.master_video_index:
                    sync_pos = self.calculate_sync_position(
                        master_position,
                        self.master_video_index,
                        i
                    )
                    logger.log_user_action(f"Sync Manager: Calcolata pos per Video {i+1}", f"{sync_pos}ms")
                    # Imposta la posizione
                    player.seek_position(sync_pos, emit_signal=False)
                    # Metti in pausa subito dopo il seek
                    player.pause()
                # Assicurati che anche il master sia in pausa se gli altri lo sono
                elif i == self.master_video_index:
                     player.pause()


        logger.log_user_action(
            "Sync Manager: Fine sync_all_to_master",
            f"Tutti i video (eccetto master) spostati e messi in pausa."
        )
