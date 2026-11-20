# Current work

The live handover: where things stand, what's next, and what to read for it. Rewritten at the end of every session. Lasting decisions and open items belong in `docs/decisions.md`; FL check steps in `docs/fl-checks.md`. This file only points to them.

**Last updated:** 2026-11-19, end of session (planning only; no code changed since 2026-10-15). Everything is committed and pushed.

## Where things stand
- **1b.5b is planned in three parts** (`decisions.md`, "1b.5b: decided with the user (2026-10-11)"): **A** the UI additions, **B** slot states, **C** following a hosted plugin's own latency changes.
- **Part A (three rounds) and part B are built** (2026-10-11 to 10-15). The user's FL check of round 3 and part B (2026-11-19) found bugs and asked for additions; all are planned in **"Next build: decided with the user (2026-11-19)"**, with the user's terms for unit states in **"Unit states: terms (user, 2026-11-19)"**.
- **Automated tests pass in Debug and Release** (95 test cases), warning-free.

## Next
1. **Build "Next build: decided with the user (2026-11-19)"**, in one build, then write its FL check in `fl-checks.md` and `docs/style/pixel-text.md`. The "agent's reading" under the terms section and the Standby default (bypass-type proposed) are open to the user's correction: mention them before building.
2. **Part C:** follow a hosted plugin's latency changes under the dip (Compactor makes it testable; its box's dry delay line must follow too). Needed before or with 1b.5c.

## Pixel fonts: state of play
- **How it works:**
  - `.p2` (m5x7 16 px) / `.p3` (Tiny5 8 px) set the font in `ui/src/app.css`; `.crisp` goes on text-only elements and applies the SVG alpha-threshold filter `#pixel-text` in `ui/src/App.svelte` (on at 75% coverage). Translucent colours inside `.crisp` vanish: fade with opacity.
  - `ui/src/lib/pixelText.svelte.ts` sets `--p2-stepped` / `--p3-stepped` so a font pixel is a whole number of screen pixels (stepping per the settings), and snaps every `.crisp` element (CSS `translate`, right and down only) so its text starts on a whole screen pixel; after text, class, layout, scroll, scaling or size changes, plus two settling frames. `span.crisp` is inline-block (inline boxes ignore `translate`).
  - Settings (page-only, `PluginSettings::pageOnlyKeys`): `pixelTextAntialiasing`, `fontFallback` (Barlow 10/8 px), `pixelTextSnap`, `pixelStepAtPercent`, `pixelStepAntialiasedPercent`.
- **Facts measured** (user's screenshots in `C:\dev\auxiliary materials\testing screenshots`: `ours.png`, `ours antialiased.png`, `ideal.png`): Windows' text engine (WebView2) smears strokes sideways only; vertical is exact. Coverage: stroke about 89%, beside one stroke 37%, a 1-pixel gap between two strokes 51 to 59%. Glyph positions were already right; only gaps filled. Font grids: m5x7 1024 units/em, 64 units per pixel at 16 px; Tiny5 128 units per pixel at 8 px; both have whole-pixel advances.
- **Confirmed (user, 2026-10-15):** perfect at 100%. **Found 2026-11-19:** missing glyphs (thin spaces, …, −) break alignment ("dB"); Smooth below 100% resamples the page image; "…" boxes' rounded clip edges cut the first column; 0.01 px snap tolerance too tight. Fixes planned (see the decisions section). **Font metrics** (from the files): m5x7 at 16 px: capitals 7 px, x-height 5, descender 2, advances 6 (k 5, M 8), ascent/descent 10.66/2 (hhea); Tiny5 at 8 px: capitals 5, x-height 4, descender 1, advances 5 (1: 4, M: 6), ascent/descent 7/2 (typo metrics). Read-only helper scripts for font tables and cursor bitmaps live in `%TEMP%` (`fontmetrics.js`, `fontcoverage.js`, `cursorpngs.js`; Node), recreate if gone.
- **How to check without FL:** dump a screenshot's pixels as text art with PowerShell's `System.Drawing.Bitmap` (`GetPixel`, brightness bands) to read stroke widths and coverage. The browser preview (`cd ui; npx vite preview --outDir ../Builds/ui --host 127.0.0.1 --port 4173 --strictPort`, a fake bridge per `CLAUDE.md`, `display.scale` from a `?scale=` query in the init script) shows layout and stepping, but its text engine doesn't smear like WebView2: FL screenshots are the ground truth. Crop with Playwright and enlarge with `image-rendering: pixelated` to see pixels.
- **If thresholding stays fragile:** the fallback discussed with the user is drawing pixel text ourselves (glyph patterns read once from a large canvas rendering, text drawn at whole pixels into canvases): exact everywhere, but manual ellipsis/wrapping. Not chosen yet.

## Read for the next step
In `docs/decisions.md`, by heading (all near the bottom):
- **"Unit states: terms (user, 2026-11-19)"**, **"Next build: decided with the user (2026-11-19)"**.
- Background: **"Part A round 3 and part B: decided with the user (2026-10-15)"**, **"Part A round 3 as built (2026-10-15)"**, **"Part B as built (2026-10-15)"**.
- **"Open items (kept current)"**, **"Working preferences (user)"**, **"Platforms and toolchain"** (macOS is required).

## Code to know
- Page: `ui/src/app.css` (tokens incl. `--warning`, `--error`), `ui/src/lib/hint.svelte.ts` (`use:hinted`, `forceHint`, `autoScroll`, `smartHintDelay`), `ui/src/lib/pixelText.svelte.ts`, `ui/src/lib/shortcuts.svelte.ts` (chords, recording, warnings), `ui/src/lib/settingMenu.ts`, `ui/src/lib/editor.svelte.ts` (settings, sections, slot state actions, `flipSettingsTab`), `ui/src/components/SettingsTab.svelte` (one snippet per section), `SlotBox.svelte` (the two-row header), `LinkStatus.svelte`.
- C++: `source/PluginSettings` (change listeners, `isChord`, page-only keys, `lastSettingsCollapsed`), `source/PluginEditor` (`describeSettings`, `changeListenerCallback`, `handleSlotState`), `source/PluginProcessor` (editor sections, `applyNewSettingsSections`, `applyEngineSettings`), `source/engine/SlotState.h`, `source/engine/Engine` (`SlotBox`: dry delay line, state level, run/reset handshake; `syncGraph`: OFF and cut-off boxes left out; saved state v3), `source/engine/BayLink`, `source/engine/PluginCatalog.h`.
- Tests: `tests/WebUiTests.cpp` (shortcuts, slot buttons), `tests/EditorTests.cpp` (sections), `tests/KeyForwarderTests.cpp` (chords, change messages), `tests/HostingTests.cpp` (generator MUTE/BYPASS, Compactor latency with BYPASS/OFF/cut-off).

## Ending a session
Rewrite this file: status, next step, the reading list (exact headings in `decisions.md`), code to know. Record decisions and open items in `decisions.md` first, and the FL check steps in `docs/fl-checks.md`.
