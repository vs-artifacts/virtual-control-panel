# Current work

The live handover: where things stand, what's next, and exactly what to read for it. Rewritten at the end of every session. Lasting decisions belong in `docs/decisions.md`; this file only points to them.

**Last updated:** 2026-10-08, end of session. Everything below is built, FL-checked by the user (all passed), committed and pushed.

## Where things stand
- **Slice 1b.4 is done:** C, A (its Diagnostics line stays until the alpha slot interface), B (real blocks) and its additions, all FL-checked. Plan: `decisions.md`, "The rest of 1b.4: plan, and the position marker"; results: "FL check of real blocks, its additions and the fades".
- **Built and FL-checked 2026-10-08:**
  - **The bug fix:** with real blocks, reaching 2 blocks (from 1 or 3, Automatic's fallback, or a shrink at a fixed 2) missed nearly every block. The receiver now lines up by the panel's unit only. `decisions.md`, "Bug: 1 → 2 real blocks misses nearly every block".
  - **The settings defaults file:** `source/SettingsDefaults.json` holds every setting's factory default and limits (C++ and the page both build from it); right-click any setting for Set as default, Reset to default and (where you have your own) Reset to factory default; "Reset all settings to defaults" at the top of the Settings tab. `decisions.md`, "Settings defaults file and Set as default".
  - **The names probe:** Param 1 is named "SLOT 1.1: kHs 3-Band EQ - Low"; a Diagnostics button toggles the plugin's name. **Result:** FL renames it everywhere (at once in the browser, on reopening elsewhere) except automation clips' own names, which are just labels; clips keep driving the parameter. `decisions.md`, "Names probe" and "FL check of the bug fix, the settings defaults and the names probe".
- **Slots planned with the user** (the next slice, 1b.5, in six sub-slices): the structure (expanded in "Target production form"), slot states, default slot controls, host control names, bindings. `decisions.md`, "Slots: decided with the user (2026-10-08)" and the plan's 1b.5.
- **Automated tests pass in Debug and Release** (88 test cases), warning-free.

## Next
**Slots, sub-slice 1b.5a: the structure and graph** (propose first: files, approach, rough size; then wait for the user's go):
- The generator stage, buses (with sub-buses) and slots with permanent IDs, routed in sequence within each bus, saved with the project (old projects open as today's routing); add, remove and move slots (a rudimentary drag if cheap, else buttons). Each slot is one permanent box in the graph with its plugin inside. The bay stays after the master output until 1b.5e.
- **Rolled in (user):** the bay latency fade splits into a **fade out** and a **fade in** length (shorter out against the click, longer in against a pulse on the return). Round trip changes are one crossfade, so nothing to split there. Propose which length the timing jump uses on adopting a new bay latency, and the settings keys (add both to `source/SettingsDefaults.json`; keep `bayLatencyFadeMs` for one so old settings carry over). `decisions.md`, "FL check of the bug fix, the settings defaults and the names probe".
- Each sub-slice ends with its FL check; write the checklist into this file.

## FL check of the bug fix, settings defaults and names probe (passed 2026-10-08; kept for regression runs)
Close FL during the build (done), so the junctions pick up the Debug builds (the relay changed too). Fresh blank project, 48 kHz, ASIO4ALL, buffer 512. Patcher: panel, receiver, an FL plugin (Fruity Filter or a reverb), transmitter, marker wire, a wire from the panel into the receiver. Diagnostics open. Real blocks on (the default).

1. **The bug (1 → 2):** play; Bay round trip 1 block, let it settle, then 2 blocks. **Expect:** Missing blocks stay 0 after the change (the Events show no run of "blocks missing (late ...)"). Then:
   - 3 → 2 blocks: the same.
   - Automatic without the wire into the receiver (it falls back to 2): 0 after the fallback.
   - At a fixed 2 blocks: **Recheck real block size** (and, while stopped, raise the tempo so it shrinks): 0 after each.
2. **Settings, unchanged:** the Settings tab shows your values as before (nothing reset). The Bay round trip radios still change the round trip (their key was renamed).
3. **Set as default and reset** (right-click):
   - A checkbox, a radio option (right-click one that isn't selected: Set as default selects it), and a number field with a typed value you haven't committed (Set as default commits it).
   - The menu then offers "Reset to factory default (…)". Change the value, then **Reset to default**: back to your default. **Reset to factory default**: the factory's value.
   - Close and reopen the editor, and open a second instance: values and defaults hold.
   - **Reset all settings to defaults:** asks first; every setting back to its default (yours where set); the window size stays.
4. **Names probe** (Diagnostics, "Names probe"):
   - FL's parameter browser, Patcher's control list and Link to controller show Param 1 as "SLOT 1.1: kHs 3-Band EQ - Low". Make an automation clip for it; note its name.
   - Press **Show or hide the plugin's name.** **Record:** which places change to "SLOT 1.1: Low" at once (browser, Patcher, Link to controller, the clip's name, "last tweaked"), and which keep the old name.
   - Play the clip: does it still move the EQ's Low (and the Panel tab's slider)?
   - Save and reopen: Param 1 opens as "SLOT 1.1: kHs 3-Band EQ - Low" (the toggle isn't saved); does the clip still drive it?
5. **Regression:** the bay's sound, Mute/Bypass/Off, a marker cut, save and reopen.

## Real blocks FL check (passed 2026-10-08; kept for regression runs)
Close FL during the build (done), so the junctions pick up the Debug builds. Fresh blank project, 48 kHz, ASIO4ALL, buffer 512. Patcher: panel, receiver, an FL plugin (Fruity Filter, then a reverb), transmitter, marker wire, a wire from the panel into the receiver. Diagnostics open.

1. **Real blocks, Automatic:** play for a few seconds. **Expect:** the Round trip line goes from "1 × 512; real blocks: watching them" to "1 × N; real blocks (announced 512)" with N about 1.25 × FL's largest block (269 at 140 BPM; 30,000 / BPM × 1.25 at 48 kHz); the Events show "Round trip block: N samples (largest real block ...)"; Automatic says "1 block works"; Missing blocks 0; the output delay and "host told" drop accordingly. Lag > Measure through a pass-through bay: 0.
2. **Tempo changes:** lower the tempo while playing (e.g. 140 → 100 → 60). **Expect:** the unit grows (one "a real block of ..." line per step, crossfaded, maybe one missed block on a big sudden drop); raising the tempo while playing doesn't shrink it; **Recheck real block size** does (after a moment); Reset counters doesn't.
3. **Lowest tempo** set to 60 in Settings: the unit jumps to 512 at once (60 BPM's tick is 500 samples); 120: about 313; 300 at 140 BPM: back down to what's seen (a new lowest tempo rechecks); empty: rechecks too.
4. **2 and 3 blocks** (fixed in Settings), with and without the wire into the receiver: Missing blocks 0 after the start; the orange warning under "counted in" while the lowest tempo is empty (and gone with one set, or at 1 block); note anything audible when the unit changes (each change makes the receiver line up again: at 2 or 3 blocks the bay gets a gap of about a block; listen through the reverb).
5. **Announced blocks** in Settings: back to 512 × blocks, as before; Recheck real block size is greyed out.
6. **Stopped:** Diagnostics' Transport line follows FL's transport and tempo (inside Patcher too: "stopped at 140 BPM"; "output quiet" once silent for the set time). Raise the tempo while stopped: with the default rule, the round trip shrinks once the panel has been quiet 0.5 s (a "..., while stopped" event); playing notes while stopped holds it off; Always shrinks even while you play; Never doesn't. **One check per stop:** after it, the Round trip line shouldn't flicker to "watching them" again until the tempo changes, you play and stop, or you press Recheck. Try the quiet level and time.
7. **Retry 1 block:** at Automatic, without the wire into the receiver (it falls back to 2), add the wire, press Retry 1 block: "1 block works". Automate "Bay 1: retry 1-block round trip" (a pulse in an automation clip, or a controller button) and "Bays: recheck block size": each acts once per switch on (Events: "(asked)"). Bay 2 to 4's parameters exist and do nothing.
8. **Bay fades** (Settings, defaults are your values: round trip change 6, bay latency change 20, Mute 12, Bypass 12, Off 25, dropouts 30): tempo changes (round trip change); linear phase on and off (bay latency change); Mute, Bypass and Off in and out (each its own); removing the transmitter (dropouts). Diagnostics' Slot line shows the next crossfade's length. Changing one shouldn't change how the others sound.
9. **Alt+Shift while resizing:** drag a corner grip, hold Alt, tap Shift, release; also Alt then Shift then Shift released within one drag, and Ctrl+Shift. **Expect:** no language flyout; resizing works as before. Untick the new setting (under the FL gain line): the flyout comes back. Typing into FL afterwards still works.
10. **Regression:** the bay's sound, Mute/Bypass/Off, a marker cut, save and reopen.

## Previous FL checklist (the next build; kept for regression runs)
Before starting: close FL during the build (done), then the junctions pick up the Debug builds. Fresh blank project; 48 kHz, ASIO4ALL, buffer 512 as before. Inside Patcher (smart disable off): panel, receiver, an FL plugin, transmitter, with the marker wire (receiver's Marker output, target 1, into the transmitter's Marker input). Open Diagnostics; the new **Events** list is at the bottom of the Bay link section.

**Record for every stage:** the Events lines (a screenshot is enough), Missing blocks (late, gone), Receiver missed, Held back; each relay window's new "Prepared by the host" line (and fallbacks on the transmitter); anything audible.

1. **Latency changes (the old failure).** Bay plugin: linear-phase EQ 2. Round trip Automatic. Play, then turn linear phase on at its lowest, off, on at higher settings, as in the C2 check. **Expect:** no audible gap or click. **Read:** do missing blocks still appear, and if so, which cause and when (right after "Bay latency: now ...", or "Prepared" lines, i.e. FL re-preparing the panel)? Does Automatic still fall back ("Automatic: 1 block missed (panel N, receiver M)")? Do the relays' prepare counts rise on a latency change? Does the transmitter count fallbacks?
2. **Dropouts are silence, faded.** Remove the transmitter while playing. **Expect:** the sound fades out quickly (a short reversed "ghost", one crossfade length) to silence, no click; Missing blocks don't count it (the status shows the transmitter gone). Add it back: the bay's sound fades in. Also try cutting the marker wire (held back about 0.7 s, then stamped by the receiver).
3. **A new panel plays before the bay returns.** New panel without relays: its sound plays, and the Slot line says (in orange) it's waiting for the bay's sound. Add the relays: it crossfades into the bay's sound, and the orange line goes.
4. **Mute:** the panel's sound plays at the same delay; a reverb in the bay keeps its tail (bay fed); unmute: crossfade straight into the bay's sound, no gap.
5. **Bypass:** the panel's sound plays at the same delay; the receiver's **Tail cut** output is 1 (the relay window's controls line says "tail cut 1"). Link Tail cut (a mono output after Marker; assign it in Connections) through a Peak Controller to whatever clears the bay plugin (its bypass, a clear button, decay). Leave Bypass: the panel's sound plays on until the bay's new sound arrives, then crossfades; no silent gap, no old tail.
6. **Off:** the panel's sound at once (FL re-shifts the project, as in C2), bay silent, tail cut 1. Back to Active: as in 5, plus one blended jump of delay.
7. **Leaving a state with the bay gone:** Bypass, remove the transmitter, back to Active: the panel's sound carries on, flagged orange; add the transmitter: crossfade into the bay's sound.
8. **Save and reopen** in each state: the state comes back (an older project's Bypass opens as Mute). The relay keeps its Tail cut value.

## Bay link FL checklist (for regression runs when bays change)
Before starting: the panel and relay junctions in the system VST3 folder point at the Debug builds, so FL picks up this build. If FL was open during the build, restart it. Note FL's sample rate and buffer length (Options > Audio settings). Terms changed after these were written: "Bay return" is now the slot's Active state, "Direct" (later "Bypass, synced") is now Mute, "Bypass, drop latency" is now Off, and dropouts are always silence (faded); "Missing bay return blocks" is Missing blocks. With no bay sound yet (or after leaving a state, until it's back), Active plays the panel's sound, flagged orange. Since C2, wire the marker (receiver's Marker output into the transmitter's Marker input) unless a stage says otherwise.

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
3. **1 block, no wire into the receiver.** **Expect:** missing blocks climb steadily, and since the bay's sound never makes it in time, the panel's own sound plays, flagged "waiting for the bay's sound" (FL ran the receiver first in the 2026-10-02 check).
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

### 7. Dropouts
1. Remove the transmitter while playing. **Expect:** a short faded "ghost" (about 30 ms), then silence; adding it back fades the bay's sound in.

### Optional
- **Relays on mixer inserts** instead of inside Patcher (receiver's smart disable off).
- **FL's "Make bridged" on a relay.** Expect "not linked" for that relay only.

## Current test builds
- **Panel:**
  - Window 1,120 × 600 at 100% by default; Shift+drag sets width and height apart (the page keeps its scale and shows more or less); each tab in two columns (one beside Diagnostics).
  - Panel tab: 17 sliders (the EQ's Low as Param 1, named "SLOT 1.1: kHs 3-Band EQ - Low" for the names probe; Bay 1 Control 1 to 16 as Param 2 to 17), the bridge line and "Audio: running". Double-click a slider to reset it to its default.
  - Plugins tab: as before, plus a Mute checkbox on each generator row (30 ms fade; saved with the project).
  - Diagnostics (toggle at the right end of the tab row):
    - The "Bay link (test build)" section: link, relays, round trip and how Automatic chose it; **Bay latency** (in use, the marker's reading, settling, readings since reset), **Output delay** (and what the host was told); **Slot**: Mute, Bypass and Off toggle buttons (pressing the selected one again: Active), with a line saying what plays (orange while Active waits for the bay's sound); missing blocks (late, gone), receiver missed, held back (settling; stamped by the receiver's position), value sets; headroom, block sizes (since the last reset), return vs sent; Lag > Measure, Control timing > Probe, Reset counters; **Events** (newest first, 30 lines).
    - **"Names probe (test build)"**: what the host is told Param 1 is called, and "Show or hide the plugin's name".
    - "Keys forwarded to the host": the last 12 keys, newest first (mouse held or not, capture windows, which window took the key).
    - "Windows around the editor": the window chain from the editor's own window to the top.
  - Settings tab: "Center a separate window when it first opens on" (as before), "Center a window attached inside FL Studio when it first opens" (Off, FL's workspace (default), the monitor where FL is open), "Resizing" (Smooth, Sharp; "Window resize: flip drag and alt+drag"), "Double-click on a control" (ms between the clicks, 1 to 2,000, default 500; "sets a value instead of resetting", a no-op placeholder), "Bay round trip" (Automatic, 1, 2, 3 blocks), **"Bay round trip counted in"** (Real blocks (default), Announced blocks; **Lowest tempo**, BPM, empty: none), **"Bay fades"** (six ms fields, one per cause; see the FL check, stage 8), "Bay latency settle time" (0 to 1,000 ms, default 50), **"Bay tail cut release"** (0 to 1,000 ms, default 0).
  - Settings tab, top: **"Reset all settings to defaults"** (asks first), and a hint: right-click any setting to make its value your default, or to reset it (**Set as default**, **Reset to default (X)**, and **Reset to factory default (Y)** where you have your own). Then **"While a window corner is dragged, stop Shift with Alt or Ctrl from switching Windows' input language"** (on by default), under the web UI and FL gain lines.
  - Diagnostics' Round trip line: "N × unit; real blocks (announced M)" ("watching them" during the 0.1 s survey) or "announced blocks". A **Transport** line: playing / stopped / not reported, the host's tempo, and "output quiet".
  - Settings, Bay round trip: **Retry 1 block** (Automatic only) and **Recheck real block size** (real blocks only); "counted in" with the lowest tempo and the orange tempo-drop warning; **"Real blocks: shrink the round trip while the transport is stopped"** (Never, Only when quiet (default), Always; quiet below −80 dBFS for 500 ms, both settable; one check per stop).
  - Host parameters after Param 1–256: "Bays: recheck block size", "Bay 1: retry 1-block round trip" to "Bay 4: …" (on/off; each acts when switched on; bays 2 to 4 do nothing yet).
  - Corner grips: drag (opposite corner), Alt+drag (center; separate windows and, in FL, attached ones), Ctrl adds 25% steps, Shift sets width and height apart (Ctrl then steps each axis); Alt and Shift can change mid-drag. Smooth mode is a layer image while resizing, redrawn sharp 250 ms after it stops. Settings has lines around the window group (with Size and Reset size) and the bay latency group.
  - Saved with the instance and the project: the tab, whether Diagnostics is open, every scroll position, the size, the slot state, control values, mutes.
  - New instances start Active, playing their own sound until the bay's sound first arrives (flagged).
  - Unchanged: the fixed test routing, the FL gain correction setting. The panel always reports its delay as latency (round trip + bay latency, or nothing when Off), even with no relays.
- **Relay:** 16 mono control outputs ("Control 1" to "Control 16"), then a stereo **"Marker" output** (at most about −108 dB), then a mono **"Tail cut" output** (1 while the panel's slot is Bypass or Off); a stereo **"Marker" input** as its second input pin. Window lines: link, round trip, missed blocks, headroom, block sizes, **Marker** (receiver: sending / silent / switched off; transmitter: bay latency now (out of range: held back), range since reset, unreadable blocks, held back while silent, or "return stamped by the receiver's position"), audio running, **"Prepared by the host: N times"** (and, transmitters, fallbacks to the receiver's position), input peak, "Control outputs: from the panel / holding the last values; tail cut N" (or silent for transmitters), all 16 exact values, Reset counters, and **"Panels in the hub"** (each panel name with its panel, receiver and transmitter: none / stopped / running; * marks this relay's). Window 680 × 666.
- **Code to know:**
  - `source/link/` (`LinkApi.h` version 3: value sets, `set_latency` and the latency in `hs_link_info`; `LinkHub.cpp`, `BayNames.h`)
  - `source/relay/` (`RelayLink`: `receive` with its skip-ahead and miss counting (it lines up by the panel's unit, never less than the block being processed), `receiveControls` (the tail cut is value 17), `transmit` (stamping by the marker, the hold, out of range and unreadable, the fallback count), `transmitByReceiver`, `sendReturn`, `reportLatency`, `writeMarker` / `readMarker` / `readMarkerInput`, `getPanels`; `RelayProcessor`: the buses, `markerInputBus`, `markerOutputBus`, `tailCutOutputBus`; `RelayEditor`: `describeMarker`, `describePanels`)
  - `source/engine/BayLink` (`updateBlockUnit`, `startSurvey`, `mayShrinkNow`, `floorUnit`, `unitFor`, `blockUnit`, `unitLargest`: the real-block unit; `recheckBlockSize`, `retryOneBlock`; `setTransport`, `quietSamples`, `stoppedChecked`; `Fade`, `fadeMs`, `stepFor`, `fadeFor`, and the fade each ramp chose: `sendFade`, `mixFade`, `levelFade`, `jumpStep`; `process`: the slot state, `awaiting` (waiting for the bay's sound), `delayRun` (the window after a change of delay), flowing, warm-up, headroom, settling and what's held back (`notTheBay`: the silent stretch plus release, receiver-stamped), the Automatic check, missing blocks by cause; `mix`, `playDropout` (the reversed fade), `fadeIn`, `blendJump`; `noteLatencyReport`, `updateBayLatency`, `getDelaySamples`; `logEvent`, `updateEventLog`; `sendValues` (with the tail cut); `runTest` / `captureTest`; `findLag`)
  - `source/engine/TriggerParameter.h` (the automatable commands); `source/PluginProcessor` (`recheckBlockSize`, `retryOneBlock` parameters, taken in `applyLinkSettings`; the transport in `processBlock`)
  - `source/engine/Engine` (`MuteStage`, `setMuted`)
  - **Settings:** `source/SettingsDefaults.json` (every setting's factory default and limits), `cmake-local/SettingsDefaults.cmake` and `SettingsDefaults.h.in` (the generated `Builds/generated/SettingsDefaults.h`), `source/PluginSettings` (`read`, `stored`, `fromVar`; `setUserDefault`, `getUserDefaults`, `reset`, `resetAll`), `BayLink::fadeSetting`; `PluginEditor::handleSettingDefault`, `handleResetSettings`; the page: `ui/src/lib/editor.svelte.ts` (`definitionOf`, `fits`, `defaultOf`, `setAsDefault`, `resetSettings`), `ui/src/lib/settingMenu.ts` (`openSettingMenu`, `typedNumber`), `ui/src/lib/menu.svelte.ts` (`openPageMenu`: menus the page makes itself)
  - **Names:** `HostControls::setName` (cleaned, capped, the host told); `PluginProcessor::toggleNamesProbe`, `applyNamesProbe` (the probe)
  - `source/PluginProcessor` (saved state: editor view, scroll areas), `source/PluginEditor` (`describeEditor`, `describeLink`, the page's events, `continueResize` with the chords, `guardLanguageSwitch`, `reportHostDiagnostics`)
  - `source/KeyForwarder` (`postToHostOf`: route B and the key log; `interruptModifierChord`), `source/EditorPlacement` (`findMovableWindow`: FL's attached `TPluginForm`, `tryToPlace`, `describeWindows`), `source/EditorScale.h` (`snap`), `source/bridge/ControlSession` (the catalog's `d`)
  - `ui/src/App.svelte` (layout), `ui/src/lib/layout.ts` (`columnFlow`, `scrollMemory`), `ui/src/lib/display.svelte.ts` (smooth and sharp rendering, the layer redraw), `ui/src/lib/editor.svelte.ts`, `ui/src/lib/params.svelte.ts` (`resetToDefault`), `ui/src/components/LinkStatus.svelte`, `ui/src/components/HostDiagnostics.svelte`, `ui/src/components/ResizeGrips.svelte`, `ui/src/components/PluginsTab.svelte`, `ui/src/components/Slider.svelte`
  - Tests: `tests/LinkHubTests.cpp`, `tests/LinkTests.cpp` (the simulated `Patcher` has a marker wire: `markerDelay`, `markerGain`, and records each output delay in `delays`; `latentBay`, `runWithTimers`, `countWrongByDelay`; the C2 tests from "With the marker wired, the panel plays a latent bay's return…", then "Slot states…", "A silent dropout starts with a reversed copy…" and "Missing blocks and the automatic round trip ignore a moment…", and the real-block tests tagged `[blocks]` (`runFixedWithTimers`, `flTick`; "one big block long ago doesn't starve a 2-block round trip" is the bug's regression test); the `Patcher` defaults to Safe dropouts and announced blocks (`realBlocks`, `lowestTempo`), and captures the receiver's `tailCut`), `tests/RelayTests.cpp` (the marker's encoding), `tests/WebUiTests.cpp`, `tests/EditorTests.cpp`, `tests/HostingTests.cpp`, `tests/ControlTests.cpp`, `tests/KeyForwarderTests.cpp` (also the settings file)

## Open items
- **Slots, 1b.5a** (above), with the bay latency fade split.
- **Dynamic names (1b.5d):** safe to build (the probe's results). Remove the probe with 1b.5d.
- **"Tidy host controls"** (renumber the pool into slot order, with a warning): a definite later addition.
- **The Diagnostics line "return silent while the sent audio isn't"** (from A): kept for the alpha slot interface (silence shown on a slot).
- **Real blocks at 2 or 3 blocks:** each change of the unit makes the receiver line up again (a gap of about a block in the bay). If audible, a hub call that updates the details without a new session (hub version 4). See `decisions.md`, "B: real blocks".
- **Dry dropouts ("Safe"): a future refinement** (`decisions.md`, "Future refinement: dry dropouts").
- **Latency-change pop fade ("linear phase pop fade"): a future refinement** (`decisions.md`, "Future refinement: latency-change pop fade").
- **Virtual lookahead automation: a future refinement** (`decisions.md`, "Future refinement: virtual lookahead automation"): an earlier copy of the tempo automation drives a panel parameter, so real blocks grow before a tempo drop.
- **The four slot states for hosted plugins** come with slots (1b.5b): Mute (hot), Bypass (warm), Off (cold), and "End signal path when off".
- Optional carry-over: a sleep and wake test with a working bay (play, sleep the PC, wake it, read every relay's Panels section and the panel's Diagnostics before touching anything).
- **Unexplained link problems** from the C1 check (a re-added panel not relinking; a second panel's link carrying the first receiver's marker as audio after the PC had slept). Not reproduced; the Panels section will show it if it returns. Optional sleep and wake test.
- **FL's "Use time offset"** (wrapper option): a thread for tempo-synced effects in a bay, which may act off the grid by the receiver's lead. Not investigated yet.
- A control lead: not by default (it costs the same latency it cancels); maybe a setting later.
- **Pops (polish, later):** adding or removing relays, cutting wires inside the bay, switching relay modes or the round trip, a new transmitter's first audio. See `decisions.md`, "FL check (2026-10-02) and fixes".
- Fixed buffer sizes in the Fruity Wrapper: a measured test after the position marker.
- Attached windows, first-open centering and the keys route: done (FL-checked 2026-10-05).
- Double-click "sets a value": a popup with a text entry (and possibly more); the setting exists as a placeholder.
- ~~Shift+drag: free aspect ratio~~ (built 2026-10-07).
- ~~The settings defaults file (JSON)~~: built 2026-10-08 (source only, with your own defaults in the settings file). Updating the factory defaults for release: when you're ready.
- ~~Optional: a panel-side "check 1 block again" button for Automatic.~~ Built 2026-10-08 (Retry 1 block).
- Choosing and naming bay controls in the UI.
- The macOS finder (written down, not built). Bays inside the panel's routing graph (1b.5e; now one bay after the engine's output). Relay naming beyond "first panel found".
- The Plugins tab (with its Mute checkboxes) merges into the panel in a later slice.

## Read for this step
In `docs/decisions.md`, by heading:
- For slots (1b.5a, the structure): **"Target production form"** (expanded 2026-10-08 with the slot structure) and its **"Architectural implications"**, the slice plan's **1b.5** near the top, **"Slots: decided with the user (2026-10-08)"** and **"FL check of the bug fix, the settings defaults and the names probe"** (the fade split; both at the bottom), **"Settings defaults file and Set as default"** (how a new setting is added), **"One fade per cause, one check per stop"** (the fades), **"Slice 1a.2: instrument + parallel generators"** (today's graph and saved state per slot), **"The next build (user, 2026-10-07)"** (the bay's four slot states), and **"Platforms and toolchain"** (macOS is required).

The code for slots: `source/engine/Engine` (`build`, the graph, `MuteStage`, saved state), `source/engine/HostedPlugin`, `source/HostControls`, `source/PluginProcessor` (the fixed test routing, binding, saved state), `ui/src/components/PluginsTab.svelte`. For the fade split: `BayLink::process` (`holdFade`, the jump), `mix`, `playDropout`, `fadeIn`, `BayLink::fadeSetting`, `source/SettingsDefaults.json`, `ui/src/lib/editor.svelte.ts` (`fadeSettings`).

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
