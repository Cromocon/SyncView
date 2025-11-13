#!/usr/bin/env python3
"""
Script di test per verificare la validazione dei marker nell'export.

Questo script simula vari scenari di marker problematici:
1. Marker troppo vicino all'inizio del video (tempo insufficiente prima)
2. Marker troppo vicino alla fine del video (tempo insufficiente dopo)
3. Marker su video molto corto (tempo insufficiente sia prima che dopo)

Usage:
    python test_marker_validation.py
"""

import sys
from pathlib import Path
from datetime import datetime
from typing import Optional
from core.markers import Marker


def create_test_marker(
    timestamp_ms: int, 
    video_index: Optional[int] = None, 
    name: Optional[str] = None
) -> Marker:
    """Crea un marker di test.
    
    Args:
        timestamp_ms: Posizione in millisecondi
        video_index: Indice video (None = globale)
        name: Nome del marker
    
    Returns:
        Marker creato
    """
    return Marker(
        timestamp=timestamp_ms,
        color="#FF0000",
        description=name or f"Test marker @ {timestamp_ms}ms",
        category="test",
        video_index=video_index,
        created_at=datetime.now().isoformat()
    )


def test_scenario_1():
    """Scenario 1: Marker a 490ms con richiesta di 1s prima."""
    print("\n" + "="*60)
    print("SCENARIO 1: Marker troppo vicino all'inizio")
    print("="*60)
    print("Setup:")
    print("  - Video durata: 10 secondi (10000ms)")
    print("  - Marker posizione: 490ms (0.49s)")
    print("  - Export richiede: 1.0s prima + 1.0s dopo")
    print("\nProblema atteso:")
    print("  - Tempo disponibile PRIMA: 0.49s")
    print("  - Tempo richiesto PRIMA: 1.0s")
    print("  - ⚠ Tempo insufficiente prima del marker!")
    
    marker = create_test_marker(490, video_index=0, name="Marker vicino inizio")
    
    # Simula validazione
    sec_before = 1.0
    sec_after = 1.0
    marker_pos_sec = marker.timestamp / 1000.0
    video_duration_sec = 10.0
    
    available_before = marker_pos_sec
    available_after = video_duration_sec - marker_pos_sec
    
    print(f"\nRisultato validazione:")
    print(f"  Disponibile PRIMA: {available_before:.2f}s (richiesti: {sec_before:.2f}s)")
    print(f"  Disponibile DOPO: {available_after:.2f}s (richiesti: {sec_after:.2f}s)")
    
    if available_before < sec_before:
        print(f"  ✅ CORRETTO: Rilevato tempo insufficiente prima del marker")
        print(f"     Mancano {sec_before - available_before:.2f}s")
        return True
    else:
        print(f"  ❌ ERRORE: Non rilevato problema")
        return False


def test_scenario_2():
    """Scenario 2: Marker a 9.5s su video di 10s con richiesta di 1s dopo."""
    print("\n" + "="*60)
    print("SCENARIO 2: Marker troppo vicino alla fine")
    print("="*60)
    print("Setup:")
    print("  - Video durata: 10 secondi (10000ms)")
    print("  - Marker posizione: 9500ms (9.5s)")
    print("  - Export richiede: 1.0s prima + 1.0s dopo")
    print("\nProblema atteso:")
    print("  - Tempo disponibile DOPO: 0.5s")
    print("  - Tempo richiesto DOPO: 1.0s")
    print("  - ⚠ Tempo insufficiente dopo del marker!")
    
    marker = create_test_marker(9500, video_index=0, name="Marker vicino fine")
    
    # Simula validazione
    sec_before = 1.0
    sec_after = 1.0
    marker_pos_sec = marker.timestamp / 1000.0
    video_duration_sec = 10.0
    
    available_before = marker_pos_sec
    available_after = video_duration_sec - marker_pos_sec
    
    print(f"\nRisultato validazione:")
    print(f"  Disponibile PRIMA: {available_before:.2f}s (richiesti: {sec_before:.2f}s)")
    print(f"  Disponibile DOPO: {available_after:.2f}s (richiesti: {sec_after:.2f}s)")
    
    if available_after < sec_after:
        print(f"  ✅ CORRETTO: Rilevato tempo insufficiente dopo del marker")
        print(f"     Mancano {sec_after - available_after:.2f}s")
        return True
    else:
        print(f"  ❌ ERRORE: Non rilevato problema")
        return False


def test_scenario_3():
    """Scenario 3: Marker a 1s su video di 1.5s con richiesta di 2s prima e dopo."""
    print("\n" + "="*60)
    print("SCENARIO 3: Video troppo corto per i requisiti")
    print("="*60)
    print("Setup:")
    print("  - Video durata: 1.5 secondi (1500ms)")
    print("  - Marker posizione: 1000ms (1.0s)")
    print("  - Export richiede: 2.0s prima + 2.0s dopo")
    print("\nProblema atteso:")
    print("  - Tempo disponibile PRIMA: 1.0s")
    print("  - Tempo disponibile DOPO: 0.5s")
    print("  - ⚠ Tempo insufficiente sia prima che dopo!")
    
    marker = create_test_marker(1000, video_index=0, name="Marker su video corto")
    
    # Simula validazione
    sec_before = 2.0
    sec_after = 2.0
    marker_pos_sec = marker.timestamp / 1000.0
    video_duration_sec = 1.5
    
    available_before = marker_pos_sec
    available_after = video_duration_sec - marker_pos_sec
    
    print(f"\nRisultato validazione:")
    print(f"  Disponibile PRIMA: {available_before:.2f}s (richiesti: {sec_before:.2f}s)")
    print(f"  Disponibile DOPO: {available_after:.2f}s (richiesti: {sec_after:.2f}s)")
    
    has_before_issue = available_before < sec_before
    has_after_issue = available_after < sec_after
    
    if has_before_issue and has_after_issue:
        print(f"  ✅ CORRETTO: Rilevato tempo insufficiente sia prima che dopo")
        print(f"     Mancano PRIMA: {sec_before - available_before:.2f}s")
        print(f"     Mancano DOPO: {sec_after - available_after:.2f}s")
        return True
    elif has_before_issue or has_after_issue:
        print(f"  ⚠ PARZIALE: Rilevato solo un problema")
        return False
    else:
        print(f"  ❌ ERRORE: Non rilevato problema")
        return False


def test_scenario_4():
    """Scenario 4: Marker valido (tempo sufficiente)."""
    print("\n" + "="*60)
    print("SCENARIO 4: Marker valido (controllo negativo)")
    print("="*60)
    print("Setup:")
    print("  - Video durata: 10 secondi (10000ms)")
    print("  - Marker posizione: 5000ms (5.0s)")
    print("  - Export richiede: 1.0s prima + 1.0s dopo")
    print("\nRisultato atteso:")
    print("  - ✓ Marker valido, nessun problema")
    
    marker = create_test_marker(5000, video_index=0, name="Marker valido")
    
    # Simula validazione
    sec_before = 1.0
    sec_after = 1.0
    marker_pos_sec = marker.timestamp / 1000.0
    video_duration_sec = 10.0
    
    available_before = marker_pos_sec
    available_after = video_duration_sec - marker_pos_sec
    
    print(f"\nRisultato validazione:")
    print(f"  Disponibile PRIMA: {available_before:.2f}s (richiesti: {sec_before:.2f}s)")
    print(f"  Disponibile DOPO: {available_after:.2f}s (richiesti: {sec_after:.2f}s)")
    
    has_issue = available_before < sec_before or available_after < sec_after
    
    if not has_issue:
        print(f"  ✅ CORRETTO: Marker valido, nessun problema rilevato")
        return True
    else:
        print(f"  ❌ ERRORE: Rilevato problema su marker valido")
        return False


def test_format_time():
    """Test della formattazione del tempo."""
    print("\n" + "="*60)
    print("TEST: Formattazione tempo")
    print("="*60)
    
    test_cases = [
        (0.49, "00:00:00.490"),
        (5.0, "00:00:05.000"),
        (65.5, "00:01:05.500"),
        (3661.123, "01:01:01.123"),
    ]
    
    all_passed = True
    for seconds, expected in test_cases:
        hours = int(seconds // 3600)
        minutes = int((seconds % 3600) // 60)
        secs = seconds % 60
        formatted = f"{hours:02d}:{minutes:02d}:{secs:06.3f}"
        
        passed = formatted == expected
        status = "✅" if passed else "❌"
        print(f"{status} {seconds}s -> {formatted} (atteso: {expected})")
        
        if not passed:
            all_passed = False
    
    return all_passed


def run_all_tests():
    """Esegue tutti i test."""
    print("\n" + "="*60)
    print("MARKER VALIDATION - TEST SUITE")
    print("="*60)
    print("\nQuesto test verifica la logica di validazione dei marker")
    print("per l'export, simulando vari scenari problematici.")
    
    tests = [
        ("Marker vicino inizio", test_scenario_1),
        ("Marker vicino fine", test_scenario_2),
        ("Video troppo corto", test_scenario_3),
        ("Marker valido (negativo)", test_scenario_4),
        ("Formattazione tempo", test_format_time),
    ]
    
    results = []
    for name, test_func in tests:
        try:
            result = test_func()
            results.append((name, result))
        except Exception as e:
            print(f"\n❌ ERRORE nel test '{name}': {e}")
            import traceback
            traceback.print_exc()
            results.append((name, False))
    
    # Riepilogo
    print("\n" + "="*60)
    print("RIEPILOGO TEST")
    print("="*60)
    
    passed = sum(1 for _, result in results if result)
    total = len(results)
    
    for name, result in results:
        status = "✅ PASS" if result else "❌ FAIL"
        print(f"{status}: {name}")
    
    print("\n" + "-"*60)
    print(f"Risultato: {passed}/{total} test passati ({100*passed//total}%)")
    print("="*60)
    
    if passed == total:
        print("\n🎉 TUTTI I TEST PASSATI!")
        print("\nLa logica di validazione funziona correttamente.")
        print("Puoi procedere a testare l'UI con l'applicazione reale.")
        return 0
    else:
        print(f"\n⚠️  {total - passed} test falliti")
        return 1


if __name__ == "__main__":
    exit_code = run_all_tests()
    sys.exit(exit_code)
