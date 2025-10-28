# Future improvements for SyncView

This document collects suggested optimizations and improvements for SyncView, grouped by area and prioritized with quick wins and longer-term projects. Use it as a roadmap for future development.

---

## Executive summary

Short-term (quick wins):
- Async video loading to avoid UI freezes
- Keyboard shortcuts for common actions
- Clear progress feedback for long operations (exports)
- Lazy imports to speed startup

Medium-term:
- Frame caching and playback optimizations
- Hardware-accelerated export (NVENC/QuickSync)
- Use SQLite for marker persistence when markers grow large
- Optimize timeline rendering (viewport culling, partial repaint)

Long-term:
- Plugin system for codecs/format handlers
- Full profiling and testing suite
- Resumable/robust export pipeline

---

## 1. Performance optimizations

1.1 Video loading & playback
- Lazy loading: only decode frames when required.
- Asynchronous loading: perform heavy I/O and probing in worker threads to keep the UI responsive.
- Frame caching (LRU) for recently accessed frames to improve seek/preview performance.
- Predecode next frames during playback to smooth playback.
- Consider memory-mapped I/O for large files if applicable.

Implementation notes
- Use QThread or Python threads (careful with GIL) or QThreadPool + QRunnable for worker tasks.
- Example pattern: enqueue file probe + preloading tasks on a worker pool and emit signals back to the UI.

1.2 Timeline & markers
- Only repaint the visible area of the timeline; avoid full repaint on scroll or small updates.
- Use spatial indexing (sorted list, bisect, or interval tree) to query markers in O(log n) for visible range.
- Debounce rapid updates during drag operations (e.g. 16-50ms throttle).

1.3 Export system
- Enable parallel exports (bounded thread- or process-pool) to use multiple CPU cores.
- Add optional hardware-accelerated encoders (FFmpeg NVENC, QSV) when present.
- Use ‘fast’ presets for quick previews and allow higher-quality presets for final renders.
- Implement an export queue with resume capability and per-clip retries on transient failures.

1.4 Memory & resource management
- Explicitly close moviepy/ffmpeg handles and release resources after export.
- Use small scoped objects and del + gc.collect() for deterministic clean-up when necessary.

Example cleanup snippet
```python
def safe_close(clip):
    try:
        clip.close()
    except Exception:
        pass
    try:
        del clip
    except Exception:
        pass
    import gc
    gc.collect()
```

---

## 2. UI / UX improvements

2.1 Responsiveness & rendering
- Virtualize long lists (only render visible items).
- Use progressive rendering: display core UI quickly, load heavy widgets later.
- Use QPropertyAnimation for smooth UI state transitions rather than manual repaint bursts.

2.2 Feedback & accessibility
- Improve progress indicators for exports, loads, and heavy tasks.
- Add accessible color contrast checks and alternative themes (high contrast).
- Add keyboard shortcuts and a help overview.

2.3 UX polish
- Skeleton or placeholder states for heavy widgets while loading.
- Clearer modal state handling for long-running operations (disable only necessary controls).

---

## 3. Architecture & maintainability

3.1 Data layer
- Use SQLite (or tiny DB) for marker storage instead of a single JSON blob when marker count grows.
- Implement incremental saves (only persist changed markers) to reduce I/O.
- Introduce a versioned migration strategy for saved marker files.

3.2 Code structure
- Centralize managers (MarkerManager, ExportManager) as singletons or carefully injected dependencies.
- Implement an event-bus to decouple components (publish/subscribe) and simplify interactions.
- Add a plugin API for feature extensions (export formats, custom annotation types).

3.3 Tests & CI
- Add unit tests for parsing, marker logic, and core algorithms.
- Add integration tests for a sample export flow (fast/low-res previews).
- Add CI linting and pre-commit checks (flake8/ruff, black/isort in a Python project).

---

## 4. Stability & robustness

4.1 Error handling
- Implement structured retries for transient failures (e.g. ffmpeg process fails to start).
- Graceful degradation: if audio processing fails, export video-only (already implemented but test more cases).
- Add consistent error reporting and optional user-facing error dialogs with actionable messages.

4.2 Logging & observability
- Use async logging (QueueHandler) when heavy logging might block the main thread.
- Add optional telemetry for performance metrics (local only, opt-in) to identify hotspots in the field.

---

## 5. Storage & persistence

- Consider SQLite for markers with indexing by timestamp and video_index for quick lookups.
- Introduce compact on-disk formats for session state (gzip JSON, or binary blob) with checksums.
- Implement incremental backups and automatic recovery points for long editing sessions.

---

## 6. Export pipeline improvements

- Add an export manager which schedules jobs, limits concurrency, and tracks progress per job.
- Add GPU-accelerated encoding if available, falling back to software encoding otherwise.
- Add streaming exports or chunked exports (write partial files and stitch) to enable resume.

---

## 7. Quick wins & prioritization (recommended roadmap)

High priority (2-4 days):
- Async load + QThreadPool worker pattern
- Keyboard shortcuts and a shortcut help dialog
- Better export progress UI and error handling
- Lazy imports for heavy modules (moviepy)

Medium priority (1-2 weeks):
- Frame cache and predecode pipeline
- Export manager with concurrency and resume
- Timeline render optimization and marker indexing

Longer-term (2+ weeks):
- Hardware acceleration integration
- SQLite persistence and migrations
- Plugin architecture and CI test coverage

---

## 8. Developer tooling & profiling

- Add a lightweight PerformanceMonitor utility using `time.perf_counter()` to annotate slow paths.
- Use `cProfile` and `snakeviz` for hotspots and interactive profiling.

Example profiling command
```bash
python -m cProfile -o profile.prof main.py
snakeviz profile.prof
```

---

## 9. Security & safety notes

- Avoid exfiltrating logs or user files by accident. Do not upload anything by default.
- If adding telemetry, make it opt-in and anonymized.

---

## 10. Next steps

- Pick 1-2 high priority items (async loading, shortcuts, export progress) and implement them iteratively.
- Add tests for each change and measure improvement with before/after metrics.

---

*File generated by the development assistant as a roadmap for future improvements.*
