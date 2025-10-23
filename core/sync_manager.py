"""
Gestore avanzato della sincronizzazione video.
Gestisce offset, drift tolerance e sincronizzazione precisa.
"""

from core.logger import logger
# Import necessari per il type hinting
from typing import List, TYPE_CHECKING

# --- CORREZIONE PER L'ERRORE PYLANCE ---
# Questo blocco viene letto solo dall'analizzatore statico (Pylance)
# e non viene eseguito a runtime, evitando l'import circolare.
if TYPE_CHECKING:
    from ui.video_player import VideoPlayerWidget
# -------------------------------------


class SyncManager:
    """Gestisce la sincronizzazione avanzata tra i video."""

    def __init__(self):
        self.sync_enabled = True
        self.video_offsets = [0, 0, 0, 0]  # Offset in millisecondi per ogni video
        self.drift_tolerance = 100  # Tolleranza drift in millisecondi
        self.master_video_index = 0  # Indice del video master (default: primo)

    def set_sync_enabled(self, enabled):
        """Abilita o disabilita la sincronizzazione."""
        self.sync_enabled = enabled
        logger.log_user_action("Sincronizzazione", "ON" if enabled else "OFF")

    def is_sync_enabled(self):
        """Verifica se la sincronizzazione è abilitata."""
        return self.sync_enabled

    def set_video_offset(self, video_index, offset_ms):
        """Imposta l'offset di un video specifico."""
        if 0 <= video_index < 4:
            old_offset = self.video_offsets[video_index]
            self.video_offsets[video_index] = offset_ms
            logger.log_user_action(
                f"Offset Video {video_index + 1}",
                f"{old_offset}ms → {offset_ms}ms"
            )

    def get_video_offset(self, video_index):
        """Ottiene l'offset di un video specifico."""
        if 0 <= video_index < 4:
            return self.video_offsets[video_index]
        return 0

    def reset_all_offsets(self):
        """Resetta tutti gli offset a zero."""
        self.video_offsets = [0, 0, 0, 0]
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

    def check_drift(self, positions):
        """
        Verifica se c'è drift eccessivo tra i video.

        Args:
            positions: Lista delle posizioni attuali di tutti i video in ms

        Returns:
            True se c'è drift oltre la tolleranza
        """
        if not self.sync_enabled or len(positions) < 2:
            return False

        # Filtra posizioni valide (video caricati)
        valid_positions = [p for p in positions if p is not None and p >= 0]

        if len(valid_positions) < 2:
            return False

        # Calcola drift massimo
        min_pos = min(valid_positions)
        max_pos = max(valid_positions)
        drift = max_pos - min_pos

        if drift > self.drift_tolerance:
            logger.log_user_action(
                "Drift Rilevato",
                f"{drift}ms (tolleranza: {self.drift_tolerance}ms)"
            )
            return True

        return False

    def set_drift_tolerance(self, tolerance_ms):
        """Imposta la tolleranza del drift."""
        old_tolerance = self.drift_tolerance
        self.drift_tolerance = max(0, tolerance_ms)
        logger.log_user_action(
            "Tolleranza Drift",
            f"{old_tolerance}ms → {self.drift_tolerance}ms"
        )

    def get_drift_tolerance(self):
        """Ottiene la tolleranza del drift."""
        return self.drift_tolerance

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


    def get_sync_info(self):
        """Ritorna informazioni sullo stato della sincronizzazione."""
        return {
            "enabled": self.sync_enabled,
            "offsets": self.video_offsets.copy(),
            "drift_tolerance": self.drift_tolerance,
            "master_video": self.master_video_index
        }

