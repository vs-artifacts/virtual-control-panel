# Current work

The live handover: where things stand, what's next, and what to read for it. Rewritten at the end of every session. Lasting decisions and open items belong in `docs/decisions.md`; FL check steps in `docs/fl-checks.md`. This file only points to them.

**Last updated:** 2026-10-14, end of session. Everything is committed and pushed, including the tag `archive/host-diagnostics` (the last commit with the archived diagnostics).

## Where things stand
- **1b.5b is planned in three parts, each with its own FL check** (`decisions.md`, "1b.5b: decided with the user (2026-10-11)"): **A** the UI additions, **B** slot states, **C** following a hosted plugin's own latency changes.
- **Part A is built in two rounds** and was looked at in FL by the user (no full checklist run yet):
  - Round 1 (2026-10-11): hint pane (Alt+wheel scrolls it), Settings labels/hints, controls level with their first line and only the control clickable, Reset all / Factory reset (+ right-click Clear user defaults / Hard reset), Swap width and height, Diagnostics reordered (values white, the rest mid-gray, explanations in hints), names probe / key log / window list archived, bus scrollbar at the bottom of the whole bus area, fonts (Rubik, Barlow, m5x7, Tiny5), orange accent, #ff0055 for drag; fonts moved to `ui/fonts/`.
  - Round 2 (2026-10-13), from the user's first FL look: pixel text threshold 75%, stepped pixel sizes and snapping (three settings), "Reset to user/factory default" wording and group resets on headings, mid-gray option labels and hints, one style for buttons/fields/checkboxes/radios (#1a1d21, mid-gray border), Diagnostics in one right-aligned label column.
- **Automated tests pass in Debug and Release** (92 test cases), warning-free.

## Next (the user's plan: both in parallel)
1. **Part B, slot states:** propose briefly if anything is unclear, otherwise build as decided ("1b.5b: decided with the user (2026-10-11)", part B):
   - ON, MUTE, BYPASS, OFF inside `Engine::SlotBox`: a dry delay line of the plugin's latency (MUTE, BYPASS, MIX); BYPASS keeps the latency, stops calling the plugin, then resets it on the message thread; OFF disconnects the box under the existing dip (Slot changes fade pair); MIX linear dry/wet (a generator's dry is silence, so MIX acts as its level).
   - "End signal path when off" (right-click on OFF, per slot): a kill switch; everything downstream fed only through it is dead (not processed, not connected), greyed with a note, its own buttons still usable.
   - Slot header: two 20 px rows (label, name, × / "M", "B", "OFF" text-only buttons in p3, selected in the accent, plus a short MIX slider); no ON button; shown but disabled on empty slots. Uppercase is the convention for our own controls.
   - Rename the bay's "active" state to "on" everywhere (C++ `BayLink::SlotState`, messages, page). MUTE and BYPASS fades shared by bays and slots (rename the `bay…` keys); the bay keeps its own OFF fade.
   - kHs Compactor (489 samples latency) and kHs Reverb into `PluginCatalog.h` (tests can use Compactor for latency; skip if missing).
   - Engine saved-state version 3; page-only controls for now (host binding in 1b.5d).
   - Then its FL check (write the checklist in `fl-checks.md`).
2. **Part A's FL check, and further pixel-font work** (see "Pixel fonts: state of play" below): the user will report findings from `fl-checks.md`, "1b.5b part A, second build", then "1b.5b part A".
3. **Part C** after part B: follow a hosted plugin's latency changes under the dip (Compactor makes it testable). Needed before or with 1b.5c.

## Pixel fonts: state of play
- **How it works:**
  - `.p2` (m5x7 16 px) / `.p3` (Tiny5 8 px) set the font in `ui/src/app.css`; `.crisp` goes on text-only elements and applies the SVG alpha-threshold filter `#pixel-text` in `ui/src/App.svelte` (on at 75% coverage). Translucent colours inside `.crisp` vanish: fade with opacity.
  - `ui/src/lib/pixelText.svelte.ts` sets `--p2-stepped` / `--p3-stepped` so a font pixel is a whole number of screen pixels (stepping per the settings), and snaps every `.crisp` element (CSS `translate`) so its text starts on a whole screen pixel; one pass per frame after text, layout, scroll or size changes. `span.crisp` is inline-block (inline boxes ignore `translate`).
  - Settings (page-only, `PluginSettings::pageOnlyKeys`): `pixelTextAntialiasing`, `fontFallback` (Barlow 10/8 px), `pixelTextSnap`, `pixelStepAtPercent`, `pixelStepAntialiasedPercent`.
- **Facts measured** (user's screenshots in `C:\dev\auxiliary materials\testing screenshots`: `ours.png`, `ours antialiased.png`, `ideal.png`): Windows' text engine (WebView2) smears strokes sideways only; vertical is exact. Coverage: stroke about 89%, beside one stroke 37%, a 1-pixel gap between two strokes 51 to 59%. Glyph positions were already right; only gaps filled. Font grids: m5x7 1024 units/em, 64 units per pixel at 16 px; Tiny5 128 units per pixel at 8 px; both have whole-pixel advances.
- **Not yet confirmed in FL:** the 75% threshold at 100% (should match `ideal.png`), stepping and snapping at 110/125/150%, Smooth vs Sharp rendering. Tiny5 at 8 px is unchecked in practice (p3 has no users until part B's header buttons).
- **How to check without FL:** dump a screenshot's pixels as text art with PowerShell's `System.Drawing.Bitmap` (`GetPixel`, brightness bands) to read stroke widths and coverage. The browser preview (`cd ui; npx vite preview --outDir ../Builds/ui --host 127.0.0.1 --port 4173 --strictPort`, a fake bridge per `CLAUDE.md`, `display.scale` from a `?scale=` query in the init script) shows layout and stepping, but its text engine doesn't smear like WebView2: FL screenshots are the ground truth. Crop with Playwright and enlarge with `image-rendering: pixelated` to see pixels.
- **If thresholding stays fragile:** the fallback discussed with the user is drawing pixel text ourselves (glyph patterns read once from a large canvas rendering, text drawn at whole pixels into canvases): exact everywhere, but manual ellipsis/wrapping. Not chosen yet.

## Read for the next step
In `docs/decisions.md`, by heading (all near the bottom):
- **"1b.5b: decided with the user (2026-10-11)"**, **"Archived diagnostics"**, **"1b.5b part A as built"**, **"Part A: pixel text and polish after the first FL look (user, 2026-10-13)"**.
- For part B: **"Slots: decided with the user (2026-10-08)"**, **"1b.5a as built"**, **"The next build (user, 2026-10-07)"** (the bay's states: the model), **"One fade per cause, one check per stop"**.
- **"Open items (kept current)"**, **"Working preferences (user)"**, **"Platforms and toolchain"** (macOS is required).

## Code to know
- Page: `ui/src/app.css` (tokens: `--accent`, `--drag`, `--mid-gray`, `--control-background`, p2/p3; shared control styles), `ui/src/lib/hint.svelte.ts` (`use:hinted`), `ui/src/lib/pixelText.svelte.ts`, `ui/src/lib/settingMenu.ts` (setting and group menus), `ui/src/components/SettingsTab.svelte`, `LinkStatus.svelte` (lines as parts: dim labels, `{ v }` values; one subgrid label column), `SlotBox.svelte` (the header where M, B, OFF and MIX go), `ui/src/lib/menu.svelte.ts` (page menus; items can carry hints).
- C++: `source/PluginSettings` (reset kinds, page-only settings), `source/PluginEditor` (`swapSize`, `wantsCenterAnchor`, `describeStructure`, `handleMutePlugin`), `source/engine/Engine` (`SlotBox`, the dip: `graphChanged`, `applyWaitingChanges`, `syncGraph`; saved state), `source/engine/BayLink` (`SlotState`, fades), `source/engine/PluginCatalog.h`.
- Tests: `tests/WebUiTests.cpp`, `tests/EditorTests.cpp` (swap), `tests/KeyForwarderTests.cpp` (settings), `tests/HostingTests.cpp`, `tests/StructureTests.cpp`.

## Ending a session
Rewrite this file: status, next step, the reading list (exact headings in `decisions.md`), code to know. Record decisions and open items in `decisions.md` first, and the FL check steps in `docs/fl-checks.md`.
