"""
Advanced Marker Database Layer for SyncView.
Versione 1.0 - Sistema di storage avanzato con:
- SQLite per gestione efficiente di grandi quantità di marker
- Salvataggi incrementali (solo marker modificati)
- Sistema di migrazione versioned
- Backward compatibility con JSON
"""

import sqlite3
from pathlib import Path
from typing import List, Optional, Dict, Any, Tuple
from dataclasses import asdict
from datetime import datetime
import json
from contextlib import contextmanager

from core.markers import Marker
from core.logger import logger


# Versione corrente del database schema
DB_VERSION = 1

# Schema SQL per ogni versione
DB_SCHEMAS = {
    1: """
        CREATE TABLE IF NOT EXISTS metadata (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
        
        CREATE TABLE IF NOT EXISTS markers (
            id TEXT PRIMARY KEY,
            timestamp INTEGER NOT NULL,
            color TEXT NOT NULL,
            description TEXT DEFAULT '',
            category TEXT DEFAULT 'default',
            video_index INTEGER,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            is_deleted INTEGER DEFAULT 0,
            UNIQUE(timestamp, video_index, created_at)
        );
        
        CREATE INDEX IF NOT EXISTS idx_timestamp ON markers(timestamp);
        CREATE INDEX IF NOT EXISTS idx_category ON markers(category);
        CREATE INDEX IF NOT EXISTS idx_video_index ON markers(video_index);
        CREATE INDEX IF NOT EXISTS idx_deleted ON markers(is_deleted);
    """
}


class MarkerDatabase:
    """Gestisce il database SQLite per marker storage."""
    
    def __init__(self, db_path: Path):
        """
        Inizializza il database.
        
        Args:
            db_path: Path del file database SQLite
        """
        self.db_path = db_path
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self._ensure_schema()
    
    @contextmanager
    def _get_connection(self):
        """Context manager per connessioni database."""
        conn = sqlite3.connect(str(self.db_path))
        conn.row_factory = sqlite3.Row  # Accesso per nome colonna
        try:
            yield conn
            conn.commit()
        except Exception as e:
            conn.rollback()
            raise e
        finally:
            conn.close()
    
    def _ensure_schema(self) -> None:
        """Crea o aggiorna lo schema del database."""
        with self._get_connection() as conn:
            cursor = conn.cursor()
            
            # Controlla versione corrente
            cursor.execute("""
                SELECT name FROM sqlite_master 
                WHERE type='table' AND name='metadata'
            """)
            
            if not cursor.fetchone():
                # Database nuovo - crea schema versione corrente
                cursor.executescript(DB_SCHEMAS[DB_VERSION])
                cursor.execute(
                    "INSERT INTO metadata (key, value) VALUES (?, ?)",
                    ('db_version', str(DB_VERSION))
                )
                cursor.execute(
                    "INSERT INTO metadata (key, value) VALUES (?, ?)",
                    ('created_at', datetime.now().isoformat())
                )
                logger.log_user_action(
                    "Database marker creato",
                    f"Versione {DB_VERSION}, Path: {self.db_path}"
                )
            else:
                # Database esistente - verifica versione
                cursor.execute(
                    "SELECT value FROM metadata WHERE key = 'db_version'"
                )
                row = cursor.fetchone()
                current_version = int(row[0]) if row else 0
                
                if current_version < DB_VERSION:
                    # Esegui migrazioni
                    self._migrate_database(conn, current_version, DB_VERSION)
    
    def _migrate_database(self, conn: sqlite3.Connection, 
                         from_version: int, to_version: int) -> None:
        """
        Esegue migrazioni del database da una versione all'altra.
        
        Args:
            conn: Connessione al database
            from_version: Versione di partenza
            to_version: Versione target
        """
        logger.log_user_action(
            "Migrazione database marker",
            f"Da versione {from_version} a {to_version}"
        )
        
        # In futuro, qui andranno le migrazioni specifiche per versione
        # Esempio:
        # if from_version < 2 and to_version >= 2:
        #     conn.executescript(MIGRATION_1_TO_2)
        
        # Aggiorna versione
        cursor = conn.cursor()
        cursor.execute(
            "UPDATE metadata SET value = ? WHERE key = 'db_version'",
            (str(to_version),)
        )
        
        logger.log_user_action(
            "Migrazione completata",
            f"Database aggiornato a versione {to_version}"
        )
    
    def save_marker(self, marker: Marker) -> bool:
        """
        Salva un singolo marker (insert or update).
        
        Args:
            marker: Marker da salvare
            
        Returns:
            True se salvato con successo
        """
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                
                now = datetime.now().isoformat()
                
                cursor.execute("""
                    INSERT INTO markers 
                    (id, timestamp, color, description, category, video_index, 
                     created_at, updated_at, is_deleted)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0)
                    ON CONFLICT(id) DO UPDATE SET
                        timestamp = excluded.timestamp,
                        color = excluded.color,
                        description = excluded.description,
                        category = excluded.category,
                        video_index = excluded.video_index,
                        updated_at = excluded.updated_at
                """, (
                    marker.id,
                    marker.timestamp,
                    marker.color,
                    marker.description,
                    marker.category,
                    marker.video_index,
                    marker.created_at,
                    now
                ))
                
            return True
            
        except Exception as e:
            logger.log_error("Errore salvataggio marker", e)
            return False
    
    def save_markers_batch(self, markers: List[Marker]) -> bool:
        """
        Salva multipli marker in batch (più efficiente).
        
        Args:
            markers: Lista di marker da salvare
            
        Returns:
            True se salvati con successo
        """
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                
                now = datetime.now().isoformat()
                
                data = [
                    (m.id, m.timestamp, m.color, m.description, 
                     m.category, m.video_index, m.created_at, now)
                    for m in markers
                ]
                
                cursor.executemany("""
                    INSERT INTO markers 
                    (id, timestamp, color, description, category, video_index, 
                     created_at, updated_at, is_deleted)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0)
                    ON CONFLICT(id) DO UPDATE SET
                        timestamp = excluded.timestamp,
                        color = excluded.color,
                        description = excluded.description,
                        category = excluded.category,
                        video_index = excluded.video_index,
                        updated_at = excluded.updated_at
                """, data)
                
            logger.log_user_action(
                "Batch save marker",
                f"{len(markers)} marker salvati"
            )
            return True
            
        except Exception as e:
            logger.log_error("Errore batch save marker", e)
            return False
    
    def delete_marker(self, marker_id: str) -> bool:
        """
        Marca un marker come eliminato (soft delete).
        
        Args:
            marker_id: ID del marker da eliminare
            
        Returns:
            True se eliminato con successo
        """
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute(
                    "UPDATE markers SET is_deleted = 1, updated_at = ? WHERE id = ?",
                    (datetime.now().isoformat(), marker_id)
                )
            return True
            
        except Exception as e:
            logger.log_error("Errore eliminazione marker", e)
            return False
    
    def load_all_markers(self, include_deleted: bool = False) -> List[Marker]:
        """
        Carica tutti i marker dal database.
        
        Args:
            include_deleted: Se True include anche marker eliminati
            
        Returns:
            Lista di marker
        """
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                
                query = "SELECT * FROM markers"
                if not include_deleted:
                    query += " WHERE is_deleted = 0"
                query += " ORDER BY timestamp"
                
                cursor.execute(query)
                rows = cursor.fetchall()
                
                markers = []
                for row in rows:
                    marker = Marker(
                        id=row['id'],
                        timestamp=row['timestamp'],
                        color=row['color'],
                        description=row['description'],
                        category=row['category'],
                        video_index=row['video_index'],
                        created_at=row['created_at']
                    )
                    markers.append(marker)
                
                return markers
                
        except Exception as e:
            logger.log_error("Errore caricamento marker", e)
            return []
    
    def clear_all_markers(self) -> bool:
        """
        Elimina tutti i marker (hard delete).
        
        Returns:
            True se eliminati con successo
        """
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute("DELETE FROM markers")
            logger.log_user_action("Database marker pulito", "Tutti i marker eliminati")
            return True
            
        except Exception as e:
            logger.log_error("Errore pulizia marker", e)
            return False
    
    def get_marker_count(self, include_deleted: bool = False) -> int:
        """
        Ottiene il numero di marker nel database.
        
        Args:
            include_deleted: Se True include anche marker eliminati
            
        Returns:
            Numero di marker
        """
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                
                query = "SELECT COUNT(*) FROM markers"
                if not include_deleted:
                    query += " WHERE is_deleted = 0"
                
                cursor.execute(query)
                return cursor.fetchone()[0]
                
        except Exception as e:
            logger.log_error("Errore conteggio marker", e)
            return 0
    
    def export_to_json(self, json_path: Path) -> bool:
        """
        Esporta tutti i marker in formato JSON (backward compatibility).
        
        Args:
            json_path: Path del file JSON
            
        Returns:
            True se esportato con successo
        """
        try:
            markers = self.load_all_markers()
            
            data = {
                'version': '3.0',
                'created_at': datetime.now().isoformat(),
                'markers': [asdict(m) for m in markers]
            }
            
            with open(json_path, 'w', encoding='utf-8') as f:
                json.dump(data, f, indent=2, ensure_ascii=False)
            
            logger.log_user_action(
                "Export JSON marker",
                f"{len(markers)} marker esportati in {json_path}"
            )
            return True
            
        except Exception as e:
            logger.log_error("Errore export JSON", e)
            return False
    
    def import_from_json(self, json_path: Path) -> bool:
        """
        Importa marker da file JSON (backward compatibility).
        
        Args:
            json_path: Path del file JSON
            
        Returns:
            True se importato con successo
        """
        try:
            with open(json_path, 'r', encoding='utf-8') as f:
                data = json.load(f)
            
            markers = []
            for marker_data in data.get('markers', []):
                # Rimuovi 'label' se presente (compatibilità vecchie versioni)
                if 'label' in marker_data:
                    del marker_data['label']
                marker = Marker(**marker_data)
                markers.append(marker)
            
            self.save_markers_batch(markers)
            
            logger.log_user_action(
                "Import JSON marker",
                f"{len(markers)} marker importati da {json_path}"
            )
            return True
            
        except Exception as e:
            logger.log_error("Errore import JSON", e)
            return False
    
    def vacuum(self) -> None:
        """Ottimizza il database rimuovendo spazio non utilizzato."""
        try:
            with self._get_connection() as conn:
                conn.execute("VACUUM")
            logger.log_user_action("Database vacuum", "Spazio ottimizzato")
        except Exception as e:
            logger.log_error("Errore vacuum database", e)
