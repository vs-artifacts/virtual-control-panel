# Current work

The live handover: where things stand, what's next, and what to read for it. Rewritten at the end of every session. Lasting decisions and open items belong in `docs/decisions.md`; FL check steps in `docs/fl-checks.md`. This file only points to them.

**Last updated:** 2026-10-15, end of session. Two local commits, **not pushed** (`0ba1356` part A round 3, `03f72d6` part B), plus the docs; push when the user says so.

## Where things stand
- **1b.5b is planned in three parts** (`decisions.md`, "1b.5b: decided with the user (2026-10-11)"): **A** the UI additions, **B** slot states, **C** following a hosted plugin's own latency changes.
- **Part A: three rounds built.** Rounds 1 and 2 (2026-10-11/13) were looked at in FL (no full checklist yet); the user's notes on round 2 led to round 3 (2026-10-15): right-only pixel snapping and extra re-snaps, settings sent to every open instance at once, the grip hint's scale and size line, automatic hint scrolling (smart delay), collapsible Settings sections with "Settings open" and Expand/Collapse all, keyboard shortcuts (layout-aware, ⚠ warnings with a confirm, Ctrl+T flips to Settings and back).
- **Part B built (2026-10-15)** in the same session: ON/MUTE/BYPASS/OFF and MIX inside each slot's box, "End signal path when off", the two-row slot header, the bay's "active" renamed "on", shared MUTE/BYPASS fades, kHs Compactor and kHs Reverb in the plugin list, engine state version 3.
- **Automated tests pass in Debug and Release** (95 test cases), warning-free. Visual check in the browser preview (fake bridge): slot headers and the Settings sections look right.

## Next
1. **The user's FL check:** `fl-checks.md`, "1b.5b part A round 3 and part B" (first section), then the two older part A sections if time allows. Fix what it finds.
2. **Part C:** follow a hosted plugin's latency changes under the dip (Compactor makes it testable; its box's dry delay line must follow too). Needed before or with 1b.5c.
3. Pixel fonts: see "Pixel fonts: state of play" below if the check still shows problems.

## Pixel fonts: state of play
- **How it works:**
  - `.p2` (m5x7 16 px) / `.p3` (Tiny5 8 px) set the font in `ui/src/app.css`; `.crisp` goes on text-only elements and applies the SVG alpha-threshold filter `#pixel-text` in `ui/src/App.svelte` (on at 75% coverage). Translucent colours inside `.crisp` vanish: fade with opacity.
  - `ui/src/lib/pixelText.svelte.ts` sets `--p2-stepped` / `--p3-stepped` so a font pixel is a whole number of screen pixels (stepping per the settings), and snaps every `.crisp` element (CSS `translate`, right and down only) so its text starts on a whole screen pixel; after text, class, layout, scroll, scaling or size changes, plus two settling frames. `span.crisp` is inline-block (inline boxes ignore `translate`).
  - Settings (page-only, `PluginSettings::pageOnlyKeys`): `pixelTextAntialiasing`, `fontFallback` (Barlow 10/8 px), `pixelTextSnap`, `pixelStepAtPercent`, `pixelStepAntialiasedPercent`.
- **Facts measured** (user's screenshots in `C:\dev\auxiliary materials\testing screenshots`: `ours.png`, `ours antialiased.png`, `ideal.png`): Windows' text engine (WebView2) smears strokes sideways only; vertical is exact. Coverage: stroke about 89%, beside one stroke 37%, a 1-pixel gap between two strokes 51 to 59%. Glyph positions were already right; only gaps filled. Font grids: m5x7 1024 units/em, 64 units per pixel at 16 px; Tiny5 128 units per pixel at 8 px; both have whole-pixel advances.
- **Confirmed (user, 2026-10-15):** perfect at 100%. **Not yet confirmed:** the round 3 fixes just above 100% (first pixel column) and the re-rendering; Tiny5 at 8 px in the new slot header.
- **How to check without FL:** dump a screenshot's pixels as text art with PowerShell's `System.Drawing.Bitmap` (`GetPixel`, brightness bands) to read stroke widths and coverage. The browser preview (`cd ui; npx vite preview --outDir ../Builds/ui --host 127.0.0.1 --port 4173 --strictPort`, a fake bridge per `CLAUDE.md`, `display.scale` from a `?scale=` query in the init script) shows layout and stepping, but its text engine doesn't smear like WebView2: FL screenshots are the ground truth. Crop with Playwright and enlarge with `image-rendering: pixelated` to see pixels.
- **If thresholding stays fragile:** the fallback discussed with the user is drawing pixel text ourselves (glyph patterns read once from a large canvas rendering, text drawn at whole pixels into canvases): exact everywhere, but manual ellipsis/wrapping. Not chosen yet.

## Read for the next step
In `docs/decisions.md`, by heading (all near the bottom):
- **"Part A round 3 and part B: decided with the user (2026-10-15)"**, **"Part A round 3 as built (2026-10-15)"**, **"Part B as built (2026-10-15)"**.
- For part C: **"1b.5b: decided with the user (2026-10-11)"**, **"1b.5a as built"** (the dip), **"C2: the panel uses the bay latency"** (how the bay follows its plugin's latency).
- **"Open items (kept current)"**, **"Working preferences (user)"**, **"Platforms and toolchain"** (macOS is required).

## Code to know
- Page: `ui/src/app.css` (tokens incl. `--warning`, `--error`), `ui/src/lib/hint.svelte.ts` (`use:hinted`, `forceHint`, `autoScroll`, `smartHintDelay`), `ui/src/lib/pixelText.svelte.ts`, `ui/src/lib/shortcuts.svelte.ts` (chords, recording, warnings), `ui/src/lib/settingMenu.ts`, `ui/src/lib/editor.svelte.ts` (settings, sections, slot state actions, `flipSettingsTab`), `ui/src/components/SettingsTab.svelte` (one snippet per section), `SlotBox.svelte` (the two-row header), `LinkStatus.svelte`.
- C++: `source/PluginSettings` (change listeners, `isChord`, page-only keys, `lastSettingsCollapsed`), `source/PluginEditor` (`describeSettings`, `changeListenerCallback`, `handleSlotState`), `source/PluginProcessor` (editor sections, `applyNewSettingsSections`, `applyEngineSettings`), `source/engine/SlotState.h`, `source/engine/Engine` (`SlotBox`: dry delay line, state level, run/reset handshake; `syncGraph`: OFF and cut-off boxes left out; saved state v3), `source/engine/BayLink`, `source/engine/PluginCatalog.h`.
- Tests: `tests/WebUiTests.cpp` (shortcuts, slot buttons), `tests/EditorTests.cpp` (sections), `tests/KeyForwarderTests.cpp` (chords, change messages), `tests/HostingTests.cpp` (generator MUTE/BYPASS, Compactor latency with BYPASS/OFF/cut-off).

## Ending a session
Rewrite this file: status, next step, the reading list (exact headings in `decisions.md`), code to know. Record decisions and open items in `decisions.md` first, and the FL check steps in `docs/fl-checks.md`.
