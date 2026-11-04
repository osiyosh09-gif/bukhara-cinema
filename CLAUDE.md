# BUKHARA CINEMA — notes for Claude

- Owner communicates in Uzbek (Cyrillic). Replies and user-facing docs: Uzbek Cyrillic.
- Stack: C++17, Qt 6 Widgets; video engine MLT + FFmpeg (LGPL-only builds, no x264/x265, no GPL MLT modules).
- Windows builds run only in GitHub Actions (MSYS2 UCRT64 + BtbN LGPL FFmpeg shared build + windeployqt + Inno Setup). Never use MSYS2's ffmpeg/mlt packages in the shipped app: they are GPL builds. A `v*` tag publishes a GitHub Release. Locally test on Linux with `QT_QPA_PLATFORM=offscreen`.
- Never present a non-working feature as working: unfinished menu actions use `MainWindow::addPlanned()` (disabled, phase tag). Keep `FeatureStatus.cpp` and `docs/STATUS.md` in sync.
- Every UI string goes through `tr()`; add translations in `tools/translations.py`, then run `update_translations` target and `python3 tools/translations.py`. Test `translations_complete` must pass.
- Never commit media files. Never delete existing working code without reason.
- Architecture: `src/core` (Project model, MediaProbe; no widgets) ← `src/app` (ProjectController owns all project mutations; widgets only call it).
- `tests/bc_screenshot <dir> [media...]` renders the main window per language for visual checks.
- Video engine: `src/engine` (MLT). Windows CI builds MLT 7.42 from source with LGPL parts only (core, avformat, sdl2) and ships it as `lib/mlt` + `share/mlt`; Linux CI uses Ubuntu's MLT for tests only. `BC_MLT_PREFIX` points tests at another MLT install. Own MLT services in `src/engine/EngineServices.cpp`: producer `bc_still` (photos and titles with alpha, rendered by Qt) and filter `bc_gain` (clip volume, fades); video tracks are composited onto track 0 with core `composite`.
