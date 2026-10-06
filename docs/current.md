# Current work

The live handover: where things stand, what's next, and exactly what to read for it. Rewritten at the end of every session. Lasting decisions belong in `docs/decisions.md`; this file only points to them.

**Last updated:** 2026-10-05, end of session. The previous work is committed and pushed (`b03af29`). This session's build (below) is **not committed yet**; ask the user whether to commit it after the FL check.

## Where things stand
- **Slice 1b.4, external module bays:** control outputs, automatic round trip, bypass and dropouts, probe and Lag > Measure all **passed the FL re-check** (2026-10-03). **The merge experiment showed Patcher lines up paths that merge, so the position marker works.** Regression findings and likely causes: `decisions.md`, "FL re-check, merge experiment, Diagnostics panel" > "Regression stages 1 and 4".
- **Built 2026-10-05, awaiting the user's FL check** (`decisions.md`, "Keys route test, attached windows, smooth text, double-click reset"):
  1. **Keys route test:** Diagnostics > "Key route while a mouse button is held", routes A to D, each key logged with its route and result.
  2. **Smooth text:** the scaled page is a layer image while resizing (text moves in proportion; measured in the browser preview), redrawn sharp 250 ms after resizing stops.
  3. **Attached windows in FL** move within FL's workspace: drag, Alt+drag and the flip setting work for them; a new setting centers them on first open (default: FL's workspace).
  4. **Double-click a slider** resets it to its default, as one gesture.
- **Automated tests pass in Debug and Release** (63 test cases), warning-free.

## Next
1. **The user's FL check** of this build (checklist below). Then: the winning key route becomes the only one (the choice and its UI go); the Smooth text result decides whether the redraw delay or the layer approach needs tuning.
2. **The settings defaults file** (soon, user 2026-10-05; see Open items).
3. Then, each with the user's go: the regression findings' candidate fixes; counting the round trip in FL's real blocks (following what FL delivers). Then **a bay plugin's own latency with the position marker** (before 1b.5). After 1b.4: 1b.5 plugin swap.

## FL check: this build
Before starting: close FL during builds; the panel junction points at the Debug build. Use a fresh project. The plugin's Settings tab should show two placement groups ("Center a separate window…" and "Center a window attached inside FL Studio…").

### 1. Keys while a mouse button is held
For each route A, B, C, D (Diagnostics > "Key route while a mouse button is held"):
1. Hold the left mouse button on a slider, type a few notes (e.g. Z, X, C), release the keys, then the mouse. Repeat with the mouse held on a tab, on a corner grip, and on empty page space.
2. **Record:** whether FL's typing keyboard plays each time, and the key log lines (route, which window took or refused the key, return values).
3. Also check per route: no stuck notes (release the mouse before the key, and the key before the mouse); keys still play with the mouse up.
4. Try at least the winning route detached, attached, and inside Patcher.
- Expect A to fail (as before). Report which of B, C, D play; if several do, say which feels right (e.g. no side effects such as FL shortcuts firing).

### 2. Smooth text
1. Settings > Resizing: Smooth. Drag a corner slowly and watch the text: it should move in step with everything else (no whole-pixel jumps), softly while moving, and sharpen about a quarter second after you stop.
2. Grow the window a lot in one drag (e.g. 100% to 300%): it may look blurry until you pause, then sharp.
3. Compare with Sharp mode. Note anything odd: flicker or a visible jump when it sharpens, and slider thumbs or text that look wrong.

### 3. Attached windows (FL)
1. Attach the panel's window (not inside Patcher). Drag each corner: the opposite corner stays put. Alt+drag: the center stays put. Turn on "flip drag and alt+drag": the two swap.
2. Drag towards FL's workspace edges: the window stays inside the workspace, and doesn't jump.
3. First-open centering: add a **new** panel instance for each setting ("Centered in FL's workspace", "Centered on the monitor where FL is open", Off) with its window attached. Expect: centered in the workspace; centered on the monitor that shows most of FL's workspace; left where FL puts it. Instances that were already opened before keep their place (by design).
4. Detached windows and Patcher: as before (regression).

### 4. Double-click reset
1. Double-click a slider's thumb: it returns to its default (the EQ's Low Gain to 0 dB; bay controls to 0). Double-clicking the track first jumps to the click (the browser slider's known limit), then resets.
2. With an automation clip recording on Param 1: the reset is recorded like a normal move.

### 5. Regression
Resizing chords (Ctrl steps, Ctrl+Alt), scroll memory, mute, Diagnostics, tab memory.

## Bay link FL checklist (for regression runs when bays change)
Before starting: the panel and relay junctions in the system VST3 folder point at the Debug builds, so FL picks up this build. If FL was open during the build, restart it. Note FL's sample rate and buffer length (Options > Audio settings). Terms changed after these were written: "Bay return" is now Bypass off, "Direct" is Bypass on, and "Missing bay return blocks" is now Bay dropouts (Safe was "Bypass").

**Record for every stage:**
- Diagnostics: round trip, block sizes, headroom (lowest), missing blocks, return vs sent, lag.
- Each relay window: link line, missed blocks (with the last reason), headroom (receiver), block sizes, "Audio: running".
- Anything audible: clicks, gaps, stuck or repeated sound, timing shifts.
- Any crash or hang.

### 1. Link alone (no FL plugin)
1. New project. Patcher on a channel, with **Patcher's smart disable off**. Inside it: the panel (MIDI from "From FL Studio", audio to "To FL Studio"), relay A set to **Receiver** in its window, relay B set to **Transmitter**.
2. Wire the receiver's main output into the transmitter's main input. No wire into the receiver yet.
3. If the transmitter's window shows "Audio: not running", wire its output to "To FL Studio" too (it outputs silence).
4. **Expect:**
   - Receiver: "Linked to Panel 1, panel running".
   - Transmitter: "Linked to Panel 1, receiver playing".
   - Diagnostics: "Linked as Panel 1", "receiver playing; transmitter returning".
5. Play notes and set the panel's Output to **Bay return**. **Expect:** the same sound as Direct, "Return vs sent: now identical, largest identical", missing blocks 0. Press Lag > Measure: "0 samples (an exact copy)".
6. Flip Direct and Bay return while playing. **Expect:** no audible difference, no timing jump.

### 2. An FL plugin in the bay
1. Put Fruity Filter (or any FL effect) between the receiver's output and the transmitter's input.
2. **Expect:**
   - The effect is heard in Bay return only, and its knobs work.
   - Missing blocks stay 0.
   - "Return vs sent" is no longer identical (expected).
   - Lag > Measure shows one path near 0 (a filter's phase shift can show as a few samples), not an exact copy.

### 3. An FL plugin with latency of its own
1. Use one with lookahead or linear phase, set as neutral as possible.
2. Lag > Measure. **Expect:** one path at +L samples, the plugin's own latency, as FL reports it (FL's linear-phase EQ 2 at its highest: 12,288). Compare it with what FL shows for that plugin's delay.
3. Flip Direct and Bay return: the return is L samples late. Bypass alignment for this is still open.
4. Note whether anything else in the project shifts in time when the plugin is added or removed (FL's latency compensation).

### 4. Round trip and processing order
1. Settings tab, **2 blocks**, no wire into the receiver. Play for a few minutes, with some CPU load (other plugins). **Expect:** panel missing blocks 0; receiver missed blocks at most 1, right after linking.
2. **3 blocks.** **Expect:** 0 everywhere.
3. **1 block, no wire into the receiver**, dropouts set to Silence. **Expect:** missing blocks climb steadily and Bay return is silent (FL ran the receiver first in the 2026-10-02 check).
4. **1 block with a wire** from the panel's output into the receiver's input (the receiver ignores what arrives there; this is the "wired receiver" fallback). **Expect:** missing blocks stop rising within a block or two, also when the wire is added after linking (the receiver may show one "skipped ahead to the panel's newest block").
5. After each change, note whether FL picks up the new latency right away or only after a restart of playback or the project.

### 5. Block sizes
1. Change FL's buffer length (e.g. 128, 256, 512, 1024) while the project is open. **Expect:** the round trip follows (e.g. 2 × 256), the relays relink by themselves, and missing blocks stay 0 at 2 and 3 blocks.
2. Note the block sizes the windows report. If the smallest differs from the largest, FL varies its blocks.

### 6. Adding, removing and reloading
1. Bay return, silence mode:
   - Remove the transmitter while playing. **Expect:** silence, then "transmitter linked, not returning" or "no transmitter". Add a new relay, set it to Transmitter, wire it. **Expect:** the return resumes, identical again (pass-through bay).
   - The same with the receiver.
2. Remove the panel. **Expect:** both relays show "Not linked: waiting for a panel". Add a panel again: the relays relink within a second.
3. Load the relays first, then the panel.
4. Save, close and reopen the project. **Expect:** the relays keep their modes, the panel keeps its Bypass switch and control values, and everything relinks.
5. A second relay set to Receiver. **Expect:** "Not linked: every panel already has a receiver".
6. Switch a relay's mode while playing. **Expect:** it drops the link and takes the other role if that's free.
7. A second panel on another channel. **Expect:** it's "Panel 2"; the relays stay on Panel 1.
8. Patcher's smart disable on, then off. **Expect:** "not running" while stopped; missing blocks don't pile up; everything resumes.
9. Close FL while playing. **Expect:** no crash.

### 7. Dropouts setting
1. Settings tab, "Bay dropouts: Safe" (the default). Remove the transmitter while playing. **Expect:** the sound carries on unprocessed, with no timing jump; adding it back fades into the return.
2. "Silence": removing it gives silence.

### Optional
- **Relays on mixer inserts** instead of inside Patcher (receiver's smart disable off).
- **FL's "Make bridged" on a relay.** Expect "not linked" for that relay only.

## Current test builds
- **Panel:**
  - Window 1,120 × 600 at 100%; each tab in two columns (one beside Diagnostics).
  - Panel tab: 17 sliders (the EQ's Low as Param 1; Bay 1 Control 1 to 16 as Param 2 to 17), the bridge line and "Audio: running". Double-click a slider to reset it to its default.
  - Plugins tab: as before, plus a Mute checkbox on each generator row (30 ms fade; saved with the project).
  - Diagnostics (toggle at the right end of the tab row):
    - The "Bay link (test build)" section: link, relays, round trip and how Automatic chose it; Bypass (with the crossfade length), missing blocks, receiver missed, value sets; headroom, block sizes (since the last reset), return vs sent; Lag > Measure, Control timing > Probe, Reset counters.
    - "Keys forwarded to the host": "Key route while a mouse button is held (FL only)" (A to D, default A, not saved), then the last 12 keys, newest first (mouse held or not, capture windows, route and its result).
    - "Windows around the editor": the window chain from the editor's own window to the top.
  - Settings tab: "Center a separate window when it first opens on" (as before), "Center a window attached inside FL Studio when it first opens" (Off, FL's workspace (default), the monitor where FL is open), "Resizing" (Smooth, Sharp; "Window resize: flip drag and alt+drag"), "Bay dropouts" (Safe, Silence), "Bay round trip" (Automatic, 1, 2, 3 blocks), "Bay bypass crossfade" (a number field, 0 to 50 ms, default 30).
  - Corner grips: drag (opposite corner), Alt+drag (center; separate windows and, in FL, attached ones), Ctrl adds 25% steps; Shift does nothing yet. Smooth mode is a layer image while resizing, redrawn sharp 250 ms after it stops.
  - Saved with the instance and the project: the tab, whether Diagnostics is open, every scroll position, the size, Bypass, control values, mutes.
  - New instances start with Bypass off; the panel's own sound only plays when the return is missing (Safe) or Bypass is on.
  - Unchanged: the fixed test routing, the FL gain correction setting. The panel always reports the round trip as latency, even with no relays.
- **Relay:** unchanged: 16 mono control outputs ("Control 1" to "Control 16") and window lines: "Control outputs: from the panel / holding the last values / silent (receivers only)", and all 16 exact values.
- **Code to know:**
  - `source/link/` (`LinkApi.h` version 2 with value sets, `LinkHub.cpp`, `BayNames.h`)
  - `source/relay/` (`RelayLink`: `receive` with its skip-ahead and miss counting, `transmit` with its stamping; `RelayProcessor`; `RelayEditor`)
  - `source/engine/BayLink` (`process`: flowing, warm-up, headroom and the Automatic check; `mix` for bypass and dropouts; `runTest` / `captureTest`; `findLag`)
  - `source/engine/Engine` (`MuteStage`, `setMuted`)
  - `source/PluginProcessor` (saved state: editor view, scroll areas), `source/PluginEditor` (`describeEditor`, `describeLink`, the page's events, `continueResize` with the chords, `reportHostDiagnostics`)
  - `source/KeyForwarder` (`postToHostOf`: routes A to D and the key log), `source/EditorPlacement` (`findMovableWindow`: FL's attached `TPluginForm`, `tryToPlace`, `describeWindows`), `source/EditorScale.h` (`snap`), `source/bridge/ControlSession` (the catalog's `d`)
  - `ui/src/App.svelte` (layout), `ui/src/lib/layout.ts` (`columnFlow`, `scrollMemory`), `ui/src/lib/display.svelte.ts` (smooth and sharp rendering, the layer redraw), `ui/src/lib/editor.svelte.ts`, `ui/src/lib/params.svelte.ts` (`resetToDefault`), `ui/src/components/LinkStatus.svelte`, `ui/src/components/HostDiagnostics.svelte`, `ui/src/components/ResizeGrips.svelte`, `ui/src/components/PluginsTab.svelte`, `ui/src/components/Slider.svelte`
  - Tests: `tests/LinkHubTests.cpp`, `tests/LinkTests.cpp`, `tests/RelayTests.cpp`, `tests/WebUiTests.cpp`, `tests/EditorTests.cpp`, `tests/HostingTests.cpp`, `tests/ControlTests.cpp`, `tests/KeyForwarderTests.cpp` (also the settings file)

## Open items
- **The regression findings' candidate fixes** (`decisions.md`, "Regression stages 1 and 4"): Safe dropouts with hysteresis; Automatic counting receiver misses without a flowing return; matching receiver miss counts (and maybe playing the newest block on skip-ahead); headroom only from arriving blocks; optionally a "return silent while the sent audio isn't" line.
- **A bay plugin's own latency (L), automatically, with the position marker:** for bypass and dropout alignment, the crossfade, the latency told to FL, and later the graph's compensation. To check in that slice: Patcher compensating a sidechain input, and whether the transmitter's wire to "To FL Studio" makes Patcher delay the panel's own output.
- A control lead: not by default (it costs the same latency it cancels); maybe a setting later.
- **Pops (polish, later):** adding or removing relays, cutting wires inside the bay, switching relay modes or the round trip, a new transmitter's first audio. See `decisions.md`, "FL check (2026-10-02) and fixes".
- Counting the round trip in FL's real blocks rather than the announced buffer: the largest real block follows the tempo (about one tick), so it must follow what FL delivers.
- Fixed buffer sizes in the Fruity Wrapper: a measured test after the position marker.
- Attached windows, first-open centering and the keys route: built, awaiting the FL check (see Next).
- Shift+drag: free aspect ratio (each axis stepped with Ctrl), once the panel's layout can follow.
- **A settings defaults file (JSON): to be implemented soon** (user, 2026-10-05), right after this step's FL check. Open question for its proposal: edited in the source only, or also a file beside the settings file (then it's untrusted input, validated like the settings file).
- Optional: a panel-side "check 1 block again" button for Automatic.
- Choosing and naming bay controls in the UI.
- The macOS finder (written down, not built). Bays inside the panel's routing graph (now one bay after the engine's output). Relay naming beyond "first panel found".
- The Plugins tab (with its Mute checkboxes) merges into the panel in a later slice.

## Read for this step
In `docs/decisions.md`, by heading:
- **"FL re-check, merge experiment, Diagnostics panel"** > **"Keys route test, attached windows, smooth text, double-click reset"** (what this build does), **"FL check of the resizing build (2026-10-05) and the next session's plan"** (why), and **"Resizing chords, smooth rendering, key and window diagnostics"**.
- **"1b.2 wrap-up"** > **"Keys"** (FL's Delphi key route) and **"Scaling and resizing"**.
- For the later bay work: **"Bay control outputs, automatic round trip, bypass"**, **"Bay link: the audio round-trip test"** > **"Timing"**, and the rest of "FL re-check, merge experiment, Diagnostics panel".
- **"Platforms and toolchain"**: macOS is required (keep the Windows window and key code isolated).

Read the rest of `decisions.md` only for questions these don't cover.

## The user's preferences for this work
- Precision first: controlled parameters leave no room for calibration error, so no fiddly correction chains inside FL.
- Keep diagnostics that make FL's behaviour visible (silence on missing blocks, running indicators, exact level readouts) until the user drops them.
- FL-specific behaviour goes behind settings, not hard-coded. Tidying the settings is a later polishing step, not now.
- Relays stay light (an instrument package needs 2 per FL plugin); relays working without a panel is a wanted later utility.
- Small slices, each ending in an FL check by the user.
- Latency matters for live playing: prefer 1 block per bay; several bays in series at large buffers is the worry. The zero-latency segments idea is only worth it if the numbers are bad, and only if it reassembles into one interface.
- Nothing the user must re-measure and re-enter when slots move or plugins change (e.g. a bay plugin's latency must be found automatically).
- Naming: "bypass" is the explicit switch of what plays; "safe" and "silence" are what dropouts play.
- Reopening a saved project must restore the panel's UI exactly as it was left (tab, panels, scroll positions).
- Fade times: 30 ms is the floor for anything switched while playing (10 ms still pops on the bass-heavy test material).
- Testing uses a fresh blank project each time; save and reload only when a test calls for it.
- Smooth rendering means soft, unsnapped everything (text included): proportional positions matter more than crispness there. Sharp is the crisp option.
- Sessions get expensive when long: hand over through these docs at natural break points rather than carrying a huge context.

## Ending a session
Rewrite this file: status, next step, the reading list (exact headings in `decisions.md`), current test builds, open items. Move anything decided into `decisions.md` first.
