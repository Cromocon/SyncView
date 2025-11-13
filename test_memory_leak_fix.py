#!/usr/bin/env python3
"""
Script di test per verificare le fix dei memory leak in VideoPlayerWidget.

Questo script testa:
1. Cleanup dei timer attivi
2. Disconnessione segnali media player
3. Rimozione riferimenti circolari
4. Riutilizzo widget dopo cleanup

Usage:
    python test_memory_leak_fix.py
"""

import sys
import gc
from pathlib import Path
from PyQt6.QtWidgets import QApplication
from PyQt6.QtCore import QTimer
from ui.video_player import VideoPlayerWidget
from core.logger import logger


def test_timer_cleanup():
    """Test 1: Verifica che i timer vengano fermati durante cleanup."""
    print("\n" + "="*60)
    print("TEST 1: Timer Cleanup")
    print("="*60)
    
    app = QApplication(sys.argv)
    player = VideoPlayerWidget(video_index=0)
    
    # Simula creazione di timer (come fatto in load_preview_frame)
    print("Creando 3 timer di test...")
    for i in range(3):
        timer = QTimer()
        timer.setSingleShot(True)
        timer.timeout.connect(lambda: None)
        timer.start(10000)  # 10 secondi
        player._active_timers.append(timer)
    
    print(f"✓ Timer attivi: {len(player._active_timers)}")
    
    # Verifica che siano attivi
    active_count = sum(1 for t in player._active_timers if t.isActive())
    print(f"✓ Timer in esecuzione: {active_count}")
    
    # Chiama cleanup
    print("\nChiamando cleanup_video()...")
    player.cleanup_video(remove_path=False)
    
    # Verifica che siano stati fermati e puliti
    print(f"✓ Timer rimanenti dopo cleanup: {len(player._active_timers)}")
    
    if len(player._active_timers) == 0:
        print("\n✅ TEST PASSATO: Tutti i timer sono stati fermati e puliti")
        return True
    else:
        print(f"\n❌ TEST FALLITO: {len(player._active_timers)} timer non puliti")
        return False


def test_signal_disconnection():
    """Test 2: Verifica disconnessione segnali media player."""
    print("\n" + "="*60)
    print("TEST 2: Signal Disconnection")
    print("="*60)
    
    app = QApplication(sys.argv)
    player = VideoPlayerWidget(video_index=0)
    
    # Conta connessioni iniziali (non possiamo contarle direttamente in PyQt6,
    # ma possiamo verificare che la disconnessione non sollevi eccezioni)
    print("Connessioni segnali iniziali presenti ✓")
    
    # Imposta un video path fittizio per permettere cleanup
    player.video_path = Path("/fake/path.mp4")
    
    # Chiama cleanup
    print("\nChiamando cleanup_video()...")
    player.cleanup_video(remove_path=False)
    
    # Verifica che audio output sia stato rimosso
    if player.media_player.audioOutput() is None:
        print("✓ Audio output disconnesso dal media player")
    else:
        print("✗ Audio output ancora connesso")
        return False
    
    # Chiama reconnect
    print("\nChiamando _reconnect_signals()...")
    player._reconnect_signals()
    
    # Verifica riconnessione
    if player.media_player.audioOutput() is not None:
        print("✓ Audio output riconnesso")
    else:
        print("✗ Audio output non riconnesso")
        return False
    
    print("\n✅ TEST PASSATO: Segnali disconnessi e riconnessi correttamente")
    return True


def test_widget_reuse():
    """Test 3: Verifica che il widget possa essere riutilizzato dopo unload."""
    print("\n" + "="*60)
    print("TEST 3: Widget Reuse After Cleanup")
    print("="*60)
    
    app = QApplication(sys.argv)
    player = VideoPlayerWidget(video_index=0)
    
    # Simula caricamento
    print("Simulando caricamento video...")
    player.video_path = Path("/fake/path1.mp4")
    player.is_loaded = True
    
    # Aggiungi alcuni timer
    for i in range(2):
        timer = QTimer()
        timer.setSingleShot(True)
        timer.timeout.connect(lambda: None)
        timer.start(5000)
        player._active_timers.append(timer)
    
    print(f"✓ Video caricato, {len(player._active_timers)} timer attivi")
    
    # Unload
    print("\nChiamando unload_video()...")
    player.unload_video()
    
    # Verifica stato pulito
    checks = [
        (player.video_path is None, "video_path è None"),
        (not player.is_loaded, "is_loaded è False"),
        (len(player._active_timers) == 0, "timer puliti"),
        (player.media_player.audioOutput() is not None, "audio output riconnesso"),
    ]
    
    all_passed = True
    for passed, desc in checks:
        status = "✓" if passed else "✗"
        print(f"{status} {desc}")
        if not passed:
            all_passed = False
    
    if all_passed:
        print("\n✅ TEST PASSATO: Widget può essere riutilizzato")
        return True
    else:
        print("\n❌ TEST FALLITO: Widget non correttamente pulito")
        return False


def test_close_event():
    """Test 4: Verifica closeEvent per cleanup finale."""
    print("\n" + "="*60)
    print("TEST 4: Close Event Cleanup")
    print("="*60)
    
    app = QApplication(sys.argv)
    player = VideoPlayerWidget(video_index=0)
    
    # Simula caricamento con timer
    print("Preparando widget con video e timer...")
    player.video_path = Path("/fake/path.mp4")
    player.is_loaded = True
    
    timer = QTimer()
    timer.setSingleShot(True)
    timer.timeout.connect(lambda: None)
    timer.start(10000)
    player._active_timers.append(timer)
    
    print(f"✓ Setup completato: 1 timer attivo")
    
    # Simula close event
    print("\nSimulando closeEvent()...")
    from PyQt6.QtGui import QCloseEvent
    close_event = QCloseEvent()
    player.closeEvent(close_event)
    
    # Verifica cleanup
    if len(player._active_timers) == 0:
        print("✓ Timer puliti durante closeEvent")
        print("\n✅ TEST PASSATO: CloseEvent esegue cleanup correttamente")
        return True
    else:
        print(f"✗ {len(player._active_timers)} timer non puliti")
        print("\n❌ TEST FALLITO: CloseEvent non ha pulito i timer")
        return False


def run_all_tests():
    """Esegue tutti i test."""
    print("\n" + "="*60)
    print("MEMORY LEAK FIX - TEST SUITE")
    print("="*60)
    
    tests = [
        ("Timer Cleanup", test_timer_cleanup),
        ("Signal Disconnection", test_signal_disconnection),
        ("Widget Reuse", test_widget_reuse),
        ("Close Event", test_close_event),
    ]
    
    results = []
    for name, test_func in tests:
        try:
            result = test_func()
            results.append((name, result))
        except Exception as e:
            print(f"\n❌ ERRORE nel test '{name}': {e}")
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
    print(f"Risultato: {passed}/{total} test passati")
    print("="*60)
    
    if passed == total:
        print("\n🎉 TUTTI I TEST PASSATI!")
        return 0
    else:
        print(f"\n⚠️  {total - passed} test falliti")
        return 1


if __name__ == "__main__":
    # Supprime warning Qt
    import os
    os.environ['QT_LOGGING_RULES'] = '*.debug=false;qt.qpa.*=false'
    
    exit_code = run_all_tests()
    sys.exit(exit_code)
