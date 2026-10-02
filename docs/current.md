# Current work

The live handover: where things stand, what's next, and exactly what to read for it. Rewritten at the end of every session. Lasting decisions belong in `docs/decisions.md`; this file only points to them.

**Last updated:** 2026-10-01, after commit `b52212e` (slices 1b.3–1b.4 experiments).

## Where things stand
- **Slice 1b.4, external module bays** (see the slice plan in `decisions.md`). Relay nodes on both sides of an FL plugin link it into the panel without Patcher wires, because FL forbids feedback loops.
- **Done:** every FL experiment the design depended on (parameter outputs, extra outputs, Peak Controller accuracy, FL's output gain, smart disable), and a relay test plugin (`source/relay/`, CMake target `HeartseekerRelay`), checked in FL. Its junction in the system VST3 folder exists.
- **Not built yet:** the link itself. No audio or values cross between the panel and relays.

## Next: the minimal link test (audio round trip)
Propose a plan (files touched, approach, rough size) and wait for the user's go.
- **Setup in FL:** panel synth → link → relay A (input mode, main output) → an FL plugin (e.g. Fruity Filter; later one with latency of its own) → relay B (output mode, input) → link → panel. A panel switch picks its output: direct, or the bay's return.
- **Build:**
  - The **link hub**: self-contained, in its own folder, compiled into the relay only. The panel gets just the plain C interface header; if it compiled the hub too, it would have its own copy and never meet the relays'.
  - The relay: input/output mode and a link client.
  - The panel: a link client that looks for the relay's loaded file about once a second, a direct/return switch, and the missing-block setting (silence / bypass).
  - Every window shows: connected, link name, round-trip delay, dropout count.
- **Measures:** whether the fixed delay stays steady in FL (Patcher's processing order, threads, varying block sizes); whether FL's latency compensation lines the return up; whether adding, removing and reloading in any order connects and disconnects cleanly (no crash, no stuck or stale audio).
- **After it:** control outputs (input-mode relays, one per parameter, into Peak Controllers).
- **Open items:** measuring a bay plugin's own latency (needed for bypass alignment); the optional crossfade into and out of bypass; the macOS way of finding an already-loaded plugin file (Windows: `GetModuleHandleEx`); where bays sit in the panel's routing graph beyond this test.

## Current test builds
- **Relay:** main input measured (its window shows the last block's peak to 7 decimals, to read a wire's exact level), all outputs silent, four control outputs declared (active, silent). Window: running indicator and input peak.
- **Panel:** "Audio: running / not running" on the Panel tab; FL gain correction setting on the Settings tab (no control outputs to apply it to yet). Still the fixed test routing (Serum 2 and Sylenth1 into the kHs EQ, Param 1 bound to the EQ's low gain).
- **Code to know:** `source/relay/` (`RelayProcessor`, `RelayEditor`, `RelayEntry.cpp`); `source/PluginProcessor` (`isBeingProcessed`); `source/engine/Engine` (the routing graph, built by `Engine::build`); `source/PluginSettings` (`flOutputCorrection`, `flGeneratorOutputGain`); `tests/RelayTests.cpp`; `CMakeLists.txt` (`RelayCode`, kept out of `SharedCode`).

## Read for this step
In `docs/decisions.md`, by heading:
- **"Module bays and relay nodes"**, all of it: the bay and relay design, the link hub, timing and Patcher's order, missing blocks, and what FL does (measured).
- **"Platforms and toolchain"**: macOS is required, so the link needs a macOS path.
- **"Slice 1a: hosting"** and **"Slice 1a.2: instrument + parallel generators"**: the engine graph, fixed test routing, stereo only, saved state per slot, message-thread rules for hosted plugins.
- **"Target production form"** and its **"Architectural implications"**: routing is data, sidechains and multiple ins/outs later; bays should fit that.
- **"Slice 1b: bridge" > "Security stance"**: the link is in-process only, but treat what crosses it as untrusted (version and size checks).
- **"Plan: thin vertical slice"**: where this step sits.
- For the control-output step after it: **"Slice 1b.2" > "Parameter model"** and **"Data flow and threads"**.

Read the rest of `decisions.md` only for questions these don't cover.

## The user's preferences for this work
- Precision first: controlled parameters leave no room for calibration error, so no fiddly correction chains inside FL.
- Keep diagnostics that make FL's behaviour visible (silence on missing blocks, running indicators, exact level readouts) until the user drops them.
- FL-specific behaviour goes behind settings, not hard-coded. Tidying the settings is a later polishing step, not now.
- Relays stay light (an instrument package needs 2 per FL plugin); relays working without a panel is a wanted later utility.
- Small slices, each ending in an FL check by the user.

## Ending a session
Rewrite this file: status, next step, the reading list (exact headings in `decisions.md`), current test builds, open items. Move anything decided into `decisions.md` first.
