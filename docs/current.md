# Current work

The live handover: where things stand, what's next, and exactly what to read for it. Rewritten at the end of every session. Lasting decisions belong in `docs/decisions.md`; this file only points to them.

**Last updated:** 2026-10-02, end of session. Everything below is committed and pushed (the commit after `4d15293`).

## Where things stand
- **Slice 1b.4, external module bays:** the link test passed its FL check. The rest was built this session and **FL-checked once** (results and fixes: `decisions.md`, "Bay control outputs" > "FL check (2026-10-02) and fixes"):
  - **Passed:** control outputs (exact values, several at once, automation, holding, saved values).
  - **Measured:** the Peak Controller's delay with the probe (about 300 to 550 samples, 10–90% in about 170; ramped about 1,600, 10–90% in about 1,100), whatever the buffer length. No control lead by default.
  - **Changed after the check, not yet checked in FL:**
    - Automatic round trip and the panel's missing-block count fixed; a new "Receiver missed" line.
    - The probe fades the output out and in.
    - The bypass crossfade is a number field (0 to 50 ms, default 30).
    - Every tab keeps its scroll position.
    - **Lag > Measure redesigned:** it sends a known noise burst (output muted) and lists every delay at which it comes back, with its gain.
- **Automated tests pass in Debug and Release** (58 test cases), warning-free.

## Next
1. **The user's FL session** (below): the re-check of the changes, the merge experiment, then the regression stages.
2. Then: **a bay plugin's own latency, handled automatically** (before 1b.5), shaped by the merge experiment's result (`decisions.md`, "Bay control outputs" > "A bay plugin's latency: candidates"). After 1b.4: 1b.5 plugin swap (see the slice plan in `decisions.md`).

## FL session: re-check, merge experiment, regression
Before starting: restart FL (the Debug build is current). Setup as in the first check: Patcher with smart disable off, panel, receiver, FL plugin, transmitter, Control 1 to 4 through Peak Controllers.

1. **Automatic round trip** (buffer length 512, then 288).
   - With the wire into the receiver: **expect** "1 block works" and "Receiver missed" staying at 0.
   - Remove the wire. **Expect** "1 block missed, so 2" within a second, and the Panel tab's missing blocks or "Receiver missed" counting what happened. After the fallback, no more pops.
   - Put the wire back and switch the receiver to Transmitter and back. **Expect** 1 block again.
   - With no wire at all from the start (new project). **Expect** it falls back to 2 instead of hanging on "trying 1 block".
2. **Probe:** no pop at its start or end (a short fade instead).
3. **Bypass crossfade:** type values into the field (e.g. 30, 0, 50, 75 → 50, "abc" → back to the last value). **Expect** each takes effect when you leave the field or press Enter.
4. **Scroll:** scroll the Panel tab down, switch to Settings and back. **Expect** the same position (and likewise for Settings).
5. **Lag > Measure** (nothing needs to be playing; the output is muted for about a second).
   - A pass-through bay (no plugin, or a neutral one): **expect** "0 samples (an exact copy)" or one path at 0.
   - FL's linear-phase EQ 2 at its highest setting: **expect** one path at about +12,288 (what FL reports for it). Try a few settings, and compare with FL's own figure.
6. **The merge experiment.**
   - EQ 2 at a high setting in the bay; wire the receiver's output both into the EQ and straight into the transmitter's input.
   - Lag > Measure. **Either** one path at +L (Patcher compensates paths that merge: the "position marker" idea works), **or** two paths, at 0 and +L (it doesn't).
7. **Regression:** stages 1 and 4 of the bay link checklist below.

## Bay link FL checklist (for regression runs when bays change)
Before starting: the panel and relay junctions in the system VST3 folder point at the Debug builds, so FL picks up this build. If FL was open during the build, restart it. Note FL's sample rate and buffer length (Options > Audio settings). Terms changed after these were written: "Bay return" is now Bypass off, "Direct" is Bypass on, and "Missing bay return blocks" is now Bay dropouts (Safe was "Bypass").

**Record for every stage:**
- Panel tab: round trip, block sizes, headroom (lowest), missing blocks, return vs sent, lag.
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
   - Panel tab: "Linked as Panel 1", "receiver playing; transmitter returning".
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
  - Panel tab: 17 sliders (the EQ's Low as Param 1; Bay 1 Control 1 to 16 as Param 2 to 17), then the "Bay link (test build)" section:
    - Link, relays, round trip, and a line saying how Automatic chose it.
    - Bypass checkbox (with the crossfade length), missing blocks (and what dropouts play), receiver missed, value sets sent.
    - Headroom, block sizes, return vs sent.
    - Lag > Measure (a noise burst, every path it comes back on), Control timing > Probe, Reset counters.
  - Settings tab: "Bay dropouts" (Safe, Silence), "Bay round trip" (Automatic, 1, 2, 3 blocks), "Bay bypass crossfade" (a number field, 0 to 50 ms, default 30). These use new settings keys, so earlier choices don't carry over.
  - Each tab keeps its scroll position while the page is open.
  - New instances start with Bypass off; the panel's own sound only plays when the return is missing (Safe) or Bypass is on.
  - Unchanged: the fixed test routing, "Audio: running", the FL gain correction setting. The panel always reports the round trip as latency, even with no relays.
- **Relay:** as before, plus 16 mono control outputs ("Control 1" to "Control 16") and window lines: "Control outputs: from the panel / holding the last values / silent (receivers only)", and all 16 exact values.
- **Code to know:**
  - `source/link/` (`LinkApi.h` version 2 with value sets, `LinkHub.cpp`, `BayNames.h`)
  - `source/relay/` (`RelayLink`, with `receiveControls` and the held values; `RelayProcessor`, filling the control outputs; `RelayEditor`)
  - `source/engine/BayLink` (`BayControl`; `updateRoundTrip` for Automatic; `mix` for bypass and dropouts; `runTest` / `captureTest` for the probe and Lag > Measure; `findLag`)
  - `source/RelayHubFinder`
  - `source/PluginProcessor` (binding the bay controls, `createSessionParameters`, `applyLinkSettings`, saved state)
  - `source/PluginEditor` (`describeLink`, the link events), `source/PluginSettings`
  - `ui/src/lib/link.svelte.ts`, `ui/src/components/LinkStatus.svelte`, `ui/src/components/SettingsTab.svelte`
  - Tests: `tests/LinkHubTests.cpp`, `tests/LinkTests.cpp`, `tests/RelayTests.cpp`

## Open items
- **A bay plugin's own latency (L), automatically:** for bypass and dropout alignment, the crossfade, the latency told to FL, and later the graph's compensation. Candidates: `decisions.md`, "Bay control outputs" > "A bay plugin's latency: candidates".
- A control lead: not by default (it costs the same latency it cancels); maybe a setting later.
- **Pops (polish, later):** adding or removing relays, cutting the wire into the receiver or its output, a new transmitter's first audio. See `decisions.md`, "FL check (2026-10-02) and fixes".
- Lag > Measure on a schedule or on demand as the fallback for a bay plugin's latency, if the merge experiment rules out the position marker.
- Counting the round trip in FL's real blocks (at most 215 samples) rather than the announced buffer: needs an FL check that FL never exceeds it.
- Optional: a panel-side "check 1 block again" button for Automatic.
- Choosing and naming bay controls in the UI.
- The macOS finder (written down, not built). Bays inside the panel's routing graph (now one bay after the engine's output). Relay naming beyond "first panel found".

## Read for this step
In `docs/decisions.md`, by heading:
- **"Bay control outputs, automatic round trip, bypass"**, all of it.
- **"Module bays and relay nodes"** > **"What FL does"** (Peak Controller, outputs, smart disable).
- **"Bay link: the audio round-trip test"** > **"Timing"** and **"FL check"**.
- For the next step (a bay plugin's latency), also **"Module bays and relay nodes"** > **"Decided"** (bypass alignment) and **"Target production form"** > **"Architectural implications"**.
- **"Platforms and toolchain"**: macOS is required.

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

## Ending a session
Rewrite this file: status, next step, the reading list (exact headings in `decisions.md`), current test builds, open items. Move anything decided into `decisions.md` first.
