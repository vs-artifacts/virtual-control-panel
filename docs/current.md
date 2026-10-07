# Current work

The live handover: where things stand, what's next, and exactly what to read for it. Rewritten at the end of every session. Lasting decisions belong in `docs/decisions.md`; this file only points to them.

**Last updated:** 2026-10-07, end of session. Everything is committed and pushed (C2: `2a20055`, plus this handover).

## Where things stand
- **Slice 1b.4, the rest (planned 2026-10-06):** C (a bay plugin's own latency via the position marker), then A (regression fixes), then B (real blocks). The settings defaults file is postponed. Plan and reasons: `decisions.md`, "The rest of 1b.4: plan, and the position marker".
- **C1 and C2 are built and FL-checked (2026-10-07).** The panel reads the bay plugin's latency from the marker and plays and reports round trip + L, in time with the rest of the project (Lag reads 0 through linear-phase EQ 2). Results: `decisions.md`, "FL check of C2".
- **Still failing: missing blocks and Automatic falling back to 2 on latency changes** (cause unknown; the next build makes it visible first).
- **Automated tests pass in Debug and Release** (70 test cases), warning-free.

## Next
1. **Next session: build "the next build"** (`decisions.md`, "The next build (user, 2026-10-07)" and "Future refinement: dry dropouts"). The user has given the go; the one point to confirm in a line before building is "Leaving Mute, Bypass or Off is a transition, not a dropout". Then write its FL checklist here.
2. **Then A: the regression fixes,** then **B: real blocks** (both specified in "Plan (user, 2026-10-06)").
3. **Then the settings defaults file** (postponed), then **slice 1b.5, plugin swap.**

## The next build: implementation notes (agent, for the next session)
Where each part goes, so the session doesn't need to explore first. Spec: `decisions.md`, "The next build (user, 2026-10-07)".
1. **Event log and counters.**
   - Panel: a message-thread log in `BayLink`, like the key log in `PluginEditor`: the newest first, about 30 lines, shown in Diagnostics. Lines:
     - `prepare` (sample rate, block);
     - round trip changes, with why (setting, Automatic trying 1, Automatic falling back);
     - bay latency settling starts and adoptions;
     - Automatic's decisions;
     - missing blocks by cause.
   - Missing blocks by cause: the audio thread keeps counters (late = `HS_LINK_NOT_YET`, gone = `HS_LINK_GONE`, held back), and the 10 Hz timer turns their changes into lines. No strings on the audio thread.
   - Relay window: prepare count; transmitter's fallbacks to the receiver's position (`RelayLink::transmit`, the no-marker branch after a marker was in use).
2. **A window after any change of delay** for Automatic and the missing count. In `BayLink::process`, `roundTripRun` resets only when the round trip changes; reset it when `currentLinkDelay` changes too, and don't count missing blocks inside that window either.
3. **Silence-only dropouts with a reversed-copy fade-out.**
   - `BayLink::mix`: a missing block (or a held-back one: settling, the marker hold, out of range, unreadable) starts from the last crossfade length of *output* played, reversed, under a falling ramp, then silence; fading back in as now.
   - Keep a ring of the last output samples, sized in `prepare` for `maxCrossfadeMs`.
   - The `Dropouts` setting leaves Settings and `PluginSettings` (`bayDropouts` is then ignored); `BayLink` keeps `Dropouts::safe` for tests (see decisions).
   - Tests that relied on Safe for exactness keep selecting it explicitly; new tests cover the silence fade (no sample jumps above a small threshold at a dropout's start, silence after the fade).
4. **Four slot states.**
   - `BayLink::BypassMode` becomes the state: active, mute, bypass, off. The old modes map as off → active, synced → mute, dropLatency → off.
   - What each state does:
     - Mute: as today's synced, the bay keeps getting the panel's sound.
     - Bypass: synced; the bay gets silence and its silent stretch is tracked like a dropped bay's (`silentStart` / `silentEnd`); the tail cut is 1.
     - Off: as today's drop latency, always silence; the tail cut is 1.
   - `DropSends` and its setting (`bayDropSends`) go.
   - Saved as `baySlotState`. Read old projects through `bayBypassMode`, `bayBypass` and `bayOutput` (all \"on\" values → mute).
   - Page: three toggle buttons (Mute, Bypass, Off) in `LinkStatus.svelte`, each selecting its state, or Active when clicked again; the `bayBypass` event carries the state name.
5. **Leaving a state.** Dry until the bay's real sound is back (positions sent after the change) and the **tail cut release** (new setting, ms, default 0, `PluginSettings`, Settings tab) has passed, then a crossfade into the return (see the point to confirm).
6. **Tail cut output.**
   - Relay: a mono \"Tail cut\" output bus after the Marker output (`RelayProcessor` buses; the relay tests' bus counts and `relayChannels` in `LinkTests.cpp` change).
   - The panel sends it as value 17 of each value set (`sendValues`: `numControls + 1` values, the last 1 while the state is Bypass or Off). The receiver plays it for the whole block, like a control, and holds it like the controls.
   - A transmitter's tail cut output is silent.
7. **Docs:** the FL checklist for this build, here; the build's results in `decisions.md`.

## Bay link FL checklist (for regression runs when bays change)
Before starting: the panel and relay junctions in the system VST3 folder point at the Debug builds, so FL picks up this build. If FL was open during the build, restart it. Note FL's sample rate and buffer length (Options > Audio settings). Terms changed after these were written: "Bay return" is now Bypass Off, "Direct" is now "Bypass, synced", and "Missing bay return blocks" is now Bay dropouts (Safe was "Bypass"). Since C2, wire the marker (receiver's Marker output into the transmitter's Marker input) unless a stage says otherwise.

**Record for every stage:**
- Diagnostics: round trip, block sizes, headroom (lowest), missing blocks, return vs sent, lag.
- Each relay window: link line, missed blocks (with the last reason), headroom (receiver), block sizes, "Audio: running".
- Anything audible: clicks, gaps, stuck or repeated sound, timing shifts.
- Any crash or hang.

### 1. Link alone (no FL plugin)
1. New project. Patcher on a channel, with **Patcher's smart disable off**. Inside it: the panel (MIDI from "From FL Studio", audio to "To FL Studio"), relay A set to **Receiver** in its window, relay B set to **Transmitter**.
2. Wire the receiver's main output into the transmitter's main input, and its Marker output into the transmitter's Marker input (assign the Marker pins in each wrapper's Connections; target 1 is enough for the Marker output, with one pin added on the node). No wire into the receiver yet.
3. (Dropped: wiring the transmitter to "To FL Studio" makes no difference.)
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
With the marker wired (C2), Diagnostics shows the plugin's latency as Bay latency, Lag > Measure reads one path near 0, and the return is in time: see "FL check: C2". Without the marker wire:
1. Use one with lookahead or linear phase, set as neutral as possible.
2. Lag > Measure. **Expect:** one path at +L samples, the plugin's own latency, as FL reports it (FL's linear-phase EQ 2 at its highest: 12,288). Compare it with what FL shows for that plugin's delay.
3. Flip Direct and Bay return: the return is L samples late.
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
    - The "Bay link (test build)" section: link, relays, round trip and how Automatic chose it; **Bay latency** (in use, the marker's reading, settling, readings since reset), **Output delay** (and what the host was told); **Bypass** (Off / "Bypass, synced" / "Bypass, drop latency", with a line saying what plays); missing blocks, receiver missed, **held back**, value sets; headroom, block sizes (since the last reset), return vs sent; Lag > Measure, Control timing > Probe, Reset counters.
    - "Keys forwarded to the host": the last 12 keys, newest first (mouse held or not, capture windows, which window took the key).
    - "Windows around the editor": the window chain from the editor's own window to the top.
  - Settings tab: "Center a separate window when it first opens on" (as before), "Center a window attached inside FL Studio when it first opens" (Off, FL's workspace (default), the monitor where FL is open), "Resizing" (Smooth, Sharp; "Window resize: flip drag and alt+drag"), "Double-click on a control" (ms between the clicks, 1 to 2,000, default 500; "sets a value instead of resetting", a no-op placeholder), "Bay dropouts" (Safe, Silence), "Bay round trip" (Automatic, 1, 2, 3 blocks), "Bay bypass crossfade" (a number field, 0 to 50 ms, default 30; also blends jumps of the bay's delay), **"Bay latency settle time"** (0 to 1,000 ms, default 50), **"While a bay is bypassed with its latency dropped, send it"** (Silence (default), the panel's sound).
  - Corner grips: drag (opposite corner), Alt+drag (center; separate windows and, in FL, attached ones), Ctrl adds 25% steps; Shift does nothing yet. Smooth mode is a layer image while resizing, redrawn sharp 250 ms after it stops.
  - Saved with the instance and the project: the tab, whether Diagnostics is open, every scroll position, the size, the Bypass choice, control values, mutes.
  - New instances start with Bypass Off; the panel's own sound only plays when the return is missing or held back (Safe) or a bypass is on.
  - Unchanged: the fixed test routing, the FL gain correction setting. The panel always reports its delay as latency (round trip + bay latency, or nothing with latency dropped), even with no relays.
- **Relay:** 16 mono control outputs ("Control 1" to "Control 16"), then a stereo **"Marker" output** (at most about −108 dB); a stereo **"Marker" input** as its second input pin. Window lines: link, round trip, missed blocks, headroom, block sizes, **Marker** (receiver: sending / silent / switched off; transmitter: bay latency now (out of range: held back), range since reset, unreadable blocks, held back while silent, or "return stamped by the receiver's position"), audio running, input peak, "Control outputs: from the panel / holding the last values / silent (receivers only)", all 16 exact values, Reset counters, and **"Panels in the hub"** (each panel name with its panel, receiver and transmitter: none / stopped / running; * marks this relay's). Window 680 × 640.
- **Code to know:**
  - `source/link/` (`LinkApi.h` version 3: value sets, `set_latency` and the latency in `hs_link_info`; `LinkHub.cpp`, `BayNames.h`)
  - `source/relay/` (`RelayLink`: `receive` with its skip-ahead and miss counting, `transmit` (stamping by the marker, the hold, out of range and unreadable), `transmitByReceiver`, `sendReturn`, `reportLatency`, `writeMarker` / `readMarker` / `readMarkerInput`, `getPanels`; `RelayProcessor`: the buses, `markerInputBus`, `markerOutputBus`; `RelayEditor`: `describeMarker`, `describePanels`)
  - `source/engine/BayLink` (`process`: flowing, warm-up, headroom, settling and what's held back (`notTheBay`), the Automatic check, the dropped bay's silence, the bypass modes; `noteLatencyReport`, `updateBayLatency`, `getDelaySamples`; `mix` for bypass and dropouts; `blendJump`; `runTest` / `captureTest`; `findLag`)
  - `source/engine/Engine` (`MuteStage`, `setMuted`)
  - `source/PluginProcessor` (saved state: editor view, scroll areas), `source/PluginEditor` (`describeEditor`, `describeLink`, the page's events, `continueResize` with the chords, `reportHostDiagnostics`)
  - `source/KeyForwarder` (`postToHostOf`: route B and the key log), `source/EditorPlacement` (`findMovableWindow`: FL's attached `TPluginForm`, `tryToPlace`, `describeWindows`), `source/EditorScale.h` (`snap`), `source/bridge/ControlSession` (the catalog's `d`)
  - `ui/src/App.svelte` (layout), `ui/src/lib/layout.ts` (`columnFlow`, `scrollMemory`), `ui/src/lib/display.svelte.ts` (smooth and sharp rendering, the layer redraw), `ui/src/lib/editor.svelte.ts`, `ui/src/lib/params.svelte.ts` (`resetToDefault`), `ui/src/components/LinkStatus.svelte`, `ui/src/components/HostDiagnostics.svelte`, `ui/src/components/ResizeGrips.svelte`, `ui/src/components/PluginsTab.svelte`, `ui/src/components/Slider.svelte`
  - Tests: `tests/LinkHubTests.cpp`, `tests/LinkTests.cpp` (the simulated `Patcher` has a marker wire: `markerDelay`, `markerGain`, and records each output delay in `delays`; `latentBay`, `runWithTimers`, `countWrongByDelay`; the C2 tests from "With the marker wired, the panel plays a latent bay's return…" to "Bypass, drop latency…"), `tests/RelayTests.cpp` (the marker's encoding), `tests/WebUiTests.cpp`, `tests/EditorTests.cpp`, `tests/HostingTests.cpp`, `tests/ControlTests.cpp`, `tests/KeyForwarderTests.cpp` (also the settings file)

## Open items
- **The next build** (above), then **A and B** (`decisions.md`, "The rest of 1b.4: plan, and the position marker"). A: headroom from arriving blocks; ~~Safe fade-back hold~~ (Safe is parked; the silence fade-back may want the same hold); Automatic counting receiver misses without a flowing return; matching miss counts (skip-ahead plays the newest block); the "return silent while the sent audio isn't" line. B: real blocks with a lowest-tempo floor (empty by default; FL's block is exactly 30,000 / BPM at 48 kHz); its round trip jumps use the blend built in C2.
- **Dry dropouts ("Safe"): a future refinement** (`decisions.md`, "Future refinement: dry dropouts").
- **The four slot states for hosted plugins** come with slots (Off takes the plugin out of the engine's path).
- Optional carry-over: a sleep and wake test with a working bay (play, sleep the PC, wake it, read every relay's Panels section and the panel's Diagnostics before touching anything).
- **Unexplained link problems** from the C1 check (a re-added panel not relinking; a second panel's link carrying the first receiver's marker as audio after the PC had slept). Not reproduced; the Panels section will show it if it returns. Optional sleep and wake test.
- **FL's "Use time offset"** (wrapper option): a thread for tempo-synced effects in a bay, which may act off the grid by the receiver's lead. Not investigated yet.
- A control lead: not by default (it costs the same latency it cancels); maybe a setting later.
- **Pops (polish, later):** adding or removing relays, cutting wires inside the bay, switching relay modes or the round trip, a new transmitter's first audio. See `decisions.md`, "FL check (2026-10-02) and fixes".
- Counting the round trip in FL's real blocks: planned as B.
- Fixed buffer sizes in the Fruity Wrapper: a measured test after the position marker.
- Attached windows, first-open centering and the keys route: done (FL-checked 2026-10-05).
- Double-click "sets a value": a popup with a text entry (and possibly more); the setting exists as a placeholder.
- Shift+drag: free aspect ratio (each axis stepped with Ctrl), once the panel's layout can follow.
- **The settings defaults file (JSON): postponed** (user, 2026-10-06) until after 1b.4. Open question for its proposal: edited in the source only, or also a file beside the settings file (then it's untrusted input, validated like the settings file).
- Optional: a panel-side "check 1 block again" button for Automatic.
- Choosing and naming bay controls in the UI.
- The macOS finder (written down, not built). Bays inside the panel's routing graph (now one bay after the engine's output). Relay naming beyond "first panel found".
- The Plugins tab (with its Mute checkboxes) merges into the panel in a later slice.

## Read for this step
In `docs/decisions.md`, by heading (all under "The rest of 1b.4: plan, and the position marker"):
- **"C2: decided with the user (2026-10-07)"**, **"C2 as built"**, **"FL check of C2"**, **"The next build (user, 2026-10-07)"** and **"Future refinement: dry dropouts"**.
- Outside it: **"Bay link: the audio round-trip test"** > **"Timing"** (the receiver's lead, stamping), and **"Platforms and toolchain"** (macOS is required).

Then read the code named in "The next build: implementation notes" above: chiefly `source/engine/BayLink.h/.cpp`, `source/relay/RelayLink.*` and `RelayProcessor.*`, `source/PluginProcessor.cpp`, `source/PluginEditor.cpp`, `ui/src/components/LinkStatus.svelte`, `tests/LinkTests.cpp`.

Read the rest of `decisions.md` only for questions these don't cover.

## The user's preferences for this work
- Precision first: controlled parameters leave no room for calibration error, so no fiddly correction chains inside FL.
- Keep diagnostics that make FL's behaviour visible (silence on missing blocks, running indicators, exact level readouts) until the user drops them.
- FL-specific behaviour goes behind settings, not hard-coded. Tidying the settings is a later polishing step, not now.
- Relays stay light (an instrument package needs 2 per FL plugin); relays working without a panel is a wanted later utility.
- Small slices, each ending in an FL check by the user.
- Latency matters for live playing: prefer 1 block per bay; several bays in series at large buffers is the worry. The zero-latency segments idea is only worth it if the numbers are bad, and only if it reassembles into one interface.
- Nothing the user must re-measure and re-enter when slots move or plugins change (e.g. a bay plugin's latency must be found automatically).
- Naming: "bypass" is the explicit switch of what plays; "safe" and "silence" are what dropouts play. Slot states: Active, Mute, Bypass, Off (2026-10-07).
- Safety over continuity: a dropout must never be louder than the bay's sound would have been, so dropouts are silence (faded) for now.
- Reopening a saved project must restore the panel's UI exactly as it was left (tab, panels, scroll positions).
- Fade times: 30 ms is the floor for anything switched while playing (10 ms still pops on the bass-heavy test material).
- Testing uses a fresh blank project each time; save and reload only when a test calls for it.
- Smooth rendering means soft, unsnapped everything (text included): proportional positions matter more than crispness there. Sharp is the crisp option.
- Sessions get expensive when long: hand over through these docs at natural break points rather than carrying a huge context.

## Ending a session
Rewrite this file: status, next step, the reading list (exact headings in `decisions.md`), current test builds, open items. Move anything decided into `decisions.md` first.
