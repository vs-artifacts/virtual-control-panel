# Current work

The live handover: where things stand, what's next, and exactly what to read for it. Rewritten at the end of every session. Lasting decisions belong in `docs/decisions.md`; this file only points to them.

**Last updated:** 2026-10-02, end of session, after commit `29f7ab0` (the bay link test, FL-checked; this file's update follows it).

## Where things stand
- **Slice 1b.4, external module bays:** the minimal link test (audio round trip) is **done and passed the user's FL check**, including the re-check of its two fixes (1-block round trip with a wired receiver; lag readouts). Results: `decisions.md`, "Bay link" > "FL check".
- **Built:**
  - **Link hub** in the relay's file.
  - **Relays** with receiver and transmitter modes.
  - **Panel side** after the engine: send, return, delayed direct sound, silence or bypass, round-trip setting.
  - **Diagnostics** in every window.
- **Automated tests pass in Debug and Release** (47 test cases). They include a simulated Patcher in every processing order, FL-like varying blocks, and a test that hosts the real relay file.

## Next: control outputs (the rest of 1b.4)
Propose a plan (files touched, approach, rough size), ask about anything open, and wait for the user's go.
- **Goal:** a receiver relay outputs one control signal per controlled parameter, each on its own control output into an FL Peak Controller linked to one knob of the bay's FL plugin. The panel drives the values (later: morphs).
- **Already known (read before planning):**
  - Values reach FL plugins only as audio into Peak Controllers. MIDI CC and parameter outputs don't work.
  - The Peak Controller is exact: steady levels give exact tenths, linearly, near-instantly at maximum decay speed; its Ramp switch smooths jumps.
  - Relays are effects, so FL doesn't scale their outputs. Only a generator's (the panel's) added outputs are scaled by 0.7792467; the FL gain correction setting exists for those.
  - The relay already declares 4 stereo control outputs, active and silent. Outputs must be assigned in FL's wrapper (Processing > Connections) before Patcher pins carry them.
  - The link carries exact values; turning them into audio is only the last step, in the relay ("The node demultiplexes").
  - Smart disable: a Patcher holding relays needs its own smart disable off; receivers on mixer inserts need theirs off.
- **Open questions for the plan:**
  - How values cross the hub: a channel per parameter, or one value channel.
  - Their timing relative to the bay's audio: same stamps?
  - Which parameters a receiver carries, and where that's chosen (the panel's UI, by decision).
  - Smoothing.
  - How many control outputs a relay declares (fixed at load).
  - What the user checks in FL.
- **Decisions still pending from the link test** (raise them; they may shape the plan):
  - The round trip's default (2 blocks now).
  - Whether to adopt the wired receiver (1 block, or control values on the wire).
  - How to align bypass with a bay plugin's own latency.
  - The bypass crossfade.
- **After 1b.4:** 1b.5 plugin swap (see the slice plan in `decisions.md`).

## Bay link FL checklist (for regression runs when bays change)
Before starting: the panel and relay junctions in the system VST3 folder point at the Debug builds, so FL picks up this build. If FL was open during the build, restart it. Note FL's sample rate and buffer length (Options > Audio settings).

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
5. Play notes and set the panel's Output to **Bay return**. **Expect:** the same sound as Direct, "Return vs sent: now identical, largest identical", missing blocks 0. Press Lag > Measure: "exact match at 0 samples".
6. Flip Direct and Bay return while playing. **Expect:** no audible difference, no timing jump.

### 2. An FL plugin in the bay
1. Put Fruity Filter (or any FL effect) between the receiver's output and the transmitter's input.
2. **Expect:**
   - The effect is heard in Bay return only, and its knobs work.
   - Missing blocks stay 0.
   - "Return vs sent" is no longer identical (expected).
   - Lag measures near 0 with short, percussive notes (a filter's phase shift can show as a few samples); held notes show "unclear".

### 3. An FL plugin with latency of its own
1. Use one with lookahead or linear phase, set as neutral as possible.
2. Lag > Measure. **Expect:** a match at +L samples, the plugin's own latency, as FL reports it (FL's linear-phase EQ 2 at its highest: 12,288). Compare it with what FL shows for that plugin's delay.
3. Flip Direct and Bay return: the return is L samples late. Bypass alignment for this is still open.
4. Note whether anything else in the project shifts in time when the plugin is added or removed (FL's latency compensation).

### 4. Round trip and processing order
1. Settings tab, **2 blocks** (default), no wire into the receiver. Play for a few minutes, with some CPU load (other plugins). **Expect:** panel missing blocks 0; receiver missed blocks at most 1, right after linking.
2. **3 blocks.** **Expect:** 0 everywhere.
3. **1 block, no wire into the receiver.** **Expect:** missing blocks climb steadily and Bay return is silent (FL ran the receiver first in the 2026-10-02 check).
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
4. Save, close and reopen the project. **Expect:** the relays keep their modes, the panel keeps its Output choice, and everything relinks.
5. A second relay set to Receiver. **Expect:** "Not linked: every panel already has a receiver".
6. Switch a relay's mode while playing. **Expect:** it drops the link and takes the other role if that's free.
7. A second panel on another channel. **Expect:** it's "Panel 2"; the relays stay on Panel 1.
8. Patcher's smart disable on, then off. **Expect:** "not running" while stopped; missing blocks don't pile up; everything resumes.
9. Close FL while playing. **Expect:** no crash.

### 7. Missing-block setting
1. Settings tab, "Missing bay return blocks: Bypass". Remove the transmitter while playing. **Expect:** the sound carries on unprocessed, with no timing jump.
2. Back to "Silence": removing it gives silence.

### Optional
- **Relays on mixer inserts** instead of inside Patcher (receiver's smart disable off).
- **FL's "Make bridged" on a relay.** Expect "not linked" for that relay only.

## Current test builds
- **Panel:**
  - Panel tab: "Bay link (test build)" section, with the Output switch (Direct / Bay return), Lag > Measure and Reset counters.
  - Settings tab: "Missing bay return blocks", "Bay round trip".
  - Unchanged: the fixed test routing, "Audio: running", the FL gain correction setting.
  - The panel always reports the round trip as latency, and its direct sound is delayed by it, even with no relays.
- **Relay:** window with Receiver / Transmitter, link status, round trip, missed blocks, headroom, block sizes, running, input peak, reset. Four control outputs, still silent (the next step fills them).
- **Code to know:**
  - `source/link/` (`LinkApi.h`, `LinkHub.cpp`, `BayNames.h`)
  - `source/relay/` (`RelayLink`, `RelayProcessor`, `RelayEditor`)
  - `source/engine/BayLink`
  - `source/RelayHubFinder`
  - `source/PluginProcessor` (`applyLinkSettings`, latency)
  - `source/PluginEditor` (`describeLink`, the link events)
  - `ui/src/lib/link.svelte.ts`, `ui/src/components/LinkStatus.svelte`
  - Tests: `tests/LinkHubTests.cpp`, `tests/LinkTests.cpp`, `tests/RelayTests.cpp`

## Open items
- A bay plugin's own latency (for bypass alignment).
- The bypass crossfade.
- The macOS finder (written down, not built).
- Bays inside the panel's routing graph (now one bay after the engine's output).
- Relay naming beyond "first panel found".

## Read for this step
In `docs/decisions.md`, by heading:
- **"Module bays and relay nodes"**, all of it: the relay design (control outputs, "The node demultiplexes", FL gain correction) and what FL does (outputs, Peak Controller, gain, smart disable).
- **"Bay link: the audio round-trip test"**: the hub's interface and rules (values will cross it too), timing, the FL check's findings (FL's blocks of 1 to 215 samples).
- **"Slice 1b.2: one slider, two-way, exposed to the host"** > **"Parameter model"** and **"Data flow and threads"**: exposed parameters, host controls, which thread writes values.
- **"Target production form"** > **"Architectural implications"**: morphs live in the engine; routing is data.
- **"Platforms and toolchain"**: macOS is required.

Read the rest of `decisions.md` only for questions these don't cover.

## The user's preferences for this work
- Precision first: controlled parameters leave no room for calibration error, so no fiddly correction chains inside FL.
- Keep diagnostics that make FL's behaviour visible (silence on missing blocks, running indicators, exact level readouts) until the user drops them.
- FL-specific behaviour goes behind settings, not hard-coded. Tidying the settings is a later polishing step, not now.
- Relays stay light (an instrument package needs 2 per FL plugin); relays working without a panel is a wanted later utility.
- Small slices, each ending in an FL check by the user.
- Latency matters for live playing: the zero-latency segments idea is only worth it if the numbers are bad, and only if it reassembles into one interface.

## Ending a session
Rewrite this file: status, next step, the reading list (exact headings in `decisions.md`), current test builds, open items. Move anything decided into `decisions.md` first.
