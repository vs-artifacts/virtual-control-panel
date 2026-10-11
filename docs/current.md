# Current work

The live handover: where things stand, what's next, and what to read for it. Rewritten at the end of every session. Lasting decisions and open items belong in `docs/decisions.md`; FL check steps in `docs/fl-checks.md`. This file only points to them.

**Last updated:** 2026-10-10, end of session. Everything is committed and pushed.

## Where things stand
- **1b.4 (external module bays) is done** and FL-checked.
- **1b.5a (structure and graph) is built:** the generator stage, buses with sub-buses, BUS X, slots as permanent boxes with permanent IDs, saved with the project; add, remove and drag slots; the dip around structure changes; the fade splits (bay latency and dropouts: fade out and fade in) with new factory defaults; the dev layout (Panel and Settings tabs, portrait design size 790 × 1,270). The user's first look in FL: "the interface looks great, and it works smoothly" (pre-alpha). The full checklist (`docs/fl-checks.md`, "1b.5a") wasn't reported step by step; use it as the regression run.
- **Automated tests pass in Debug and Release** (92 test cases), warning-free.
- **Standing rule since 1b.5a:** no backward compatibility before the first release, except the host-control IDs (`CLAUDE.md`).

## Next: 1b.5b, slot states
**Propose first** (files, approach, rough size), ask what's unclear, then wait for the user's go.

Scope (from the plan's 1b.5 and "Slots: decided with the user (2026-10-08)"):
- **Mute** (hot): the slot's contribution is silenced while it keeps processing; an effect passes the dry sound, a generator goes silent.
- **Bypass** (warm): processing bypassed, but the slot keeps its place in the latency (dry delayed by the plugin's latency); effectively resets tails.
- **Off** (cold): out of the processing grid; the reported latency changes.
- **"End signal path when off"**: a right-click option on Off; Off then also silences the slot's output downstream.
- **Default controls** in each slot's 40 px header: **M**, **B**, **X** (single letters, toggles acting like radio buttons, as the bay's Mute/Bypass/Off do) and a **MIX** (dry/wet) short slider. Host names "SLOT 1.1: MUTE", "SLOT 1.1: BYPASS", "SLOT 1.1: DISABLED", "SLOT 1.1: MIX".

Agent's notes for the proposal (not decided):
- The states belong inside `Engine::SlotBox` (where the generator Mute already lives, which M replaces). Off may rewire the graph (the dip machinery exists); Bypass needs a delay line of the plugin's latency.
- The bay's four states (`BayLink::SlotState`, "The next build (user, 2026-10-07)") are the model; check whether the same names and toggle behaviour carry over.
- Questions to settle with the user: whether the default controls get host controls now (and in which pool entries: bindings never move once made, so they take the next free ones) or with 1b.5d; the fade lengths (shared settings like the bay's Mute/Bypass/Off fades, or per slot); what Bypass means for a generator; whether states are saved per slot (yes, presumably, with the structure).
- The open item "a hosted plugin changing its own latency isn't followed" matters for Bypass and Off; consider fixing it in this sub-slice.

## Read for the next step
In `docs/decisions.md`, by heading:
- **"Slots: decided with the user (2026-10-08)"**, **"1b.5a: decided with the user (2026-10-09/10)"** and **"1b.5a as built"** (all at the bottom).
- **"The next build (user, 2026-10-07)"** (the bay's slot states: the model) and **"One fade per cause, one check per stop"** (fades).
- **"Open items (kept current)"**, **"Working preferences (user)"**, **"Platforms and toolchain"** (macOS is required).

Read the rest only for questions these don't cover.

## Code to know for 1b.5b
- `source/engine/Engine` (`SlotBox`: a slot's box with its plugin and the generator Mute; `graphChanged` / `applyWaitingChanges` / `syncGraph`: the dip and rewiring; `releaseSlot`, `expose`; saved state: `createStateXml` / `restoreStateFromXml`, version 2), `source/engine/Structure` (the model and its saved form), `source/engine/HostedPlugin`, `source/engine/PluginCatalog.h`.
- `source/engine/BayLink` (`SlotState`, `fadeFor`, how the bay's states fade and cut tails), `source/SettingsDefaults.json` (how a setting is added: see "Settings defaults file and Set as default").
- `source/PluginProcessor` (`bindExposedParameters`, `firstBayControl`, `onSlotRemoving`, `onStructureChanged`), `source/HostControls` (`bind`, `unbind`, `setName`), `source/PluginEditor` (`describeStructure`, the structure handlers, `handleMutePlugin`).
- The page: `ui/src/components/SlotBox.svelte` (the header where M, B, X and MIX go), `ui/src/components/PanelTab.svelte`, `ui/src/components/BusColumn.svelte`, `ui/src/lib/editor.svelte.ts` (`readStructure`, `mutePlugin`), `ui/src/components/LinkStatus.svelte` (the bay's Mute/Bypass/Off buttons, for the toggle behaviour).
- Tests: `tests/StructureTests.cpp`, `tests/HostingTests.cpp` ("Muting a generator…" includes the dip), `tests/WebUiTests.cpp` (`oneSlotState`: a page with one generator and one slot).

## Ending a session
Rewrite this file: status, next step, the reading list (exact headings in `decisions.md`), code to know. Record decisions and open items in `decisions.md` first, and the FL check steps in `docs/fl-checks.md`.
