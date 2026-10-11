# FL checks

The user's checks in FL Studio: the latest slice's, and the earlier ones kept for regression runs. Read only when preparing or running a check; `docs/current.md` points here. Results go into `docs/decisions.md`.

**Common setup:** close FL during the build, so the junctions in the system VST3 folder pick up the Debug builds (if FL was open, restart it). Fresh blank project, 48 kHz, ASIO4ALL, buffer 512. Bay tests run inside Patcher (smart disable off): panel, receiver relay, an FL plugin, transmitter relay, the marker wire (the receiver's Marker output, target 1, into the transmitter's Marker input), and for 1-block round trips a wire from the panel into the receiver. Diagnostics open.

## 1b.5a: structure and graph (built 2026-10-10)
**Status:** the user's first look in FL (2026-10-10): "the interface looks great, and it works smoothly" (pre-alpha). The steps below weren't reported one by one; use them as the regression run for the structure.

**First:** Settings tab, "Reset all settings to defaults" (and, where you made your own default, right-click > Reset to factory default), so the new factory defaults apply.

1. **Layout:** the window opens at 790 × 1,270 (portrait, at 100%; 800 × 1,280 with FL's border), background #0f1012 (if not, Settings > Reset size). Tabs: Panel, Settings. Panel: Header, Generator stage (GEN 1 Serum 2, GEN 2 Sylenth1, both feeding BUS 1), Buses (BUS 1: 1.0, 1.1 kHs 3-Band EQ with its Low slider, 1.X; BUS X pinned at the right; the bay box below it, 4 × 4 controls), a 40 px hint pane at the bottom. Slot contents in m5x7 at 16 px.
2. **Sound:** both synths through the EQ; a generator's Mute (30 ms fade); Param 1 (the EQ's Low) both ways from FL's browser, automation and the slider in slot 1.1.
3. **Collapsing and scrolling:** each bar collapses its section; bars pin at the top, stacked; the bus and generator rows scroll sideways on their own, BUS X stays. Reopen the editor and the project: collapsed sections, scroll positions and the tab come back.
4. **Adding:** "+ slot" > kHs Nonlinear Filter; "+ slot" > Empty, then "Choose plugin…"; "+ generator" > Serum 2 (feeds BUS 1); "+ bus"; "+ sub" (the sub-bus "→ 1.X" sits after its parent; the others renumber). While playing, each change of what's heard dips the sound (Settings > "Slot changes": out 17, in 100 ms); an empty bus doesn't dip.
5. **Routing:** a generator's bus chips (only into a sub-bus with a filter: heard filtered, joined with BUS 1 after its slots; no chips: silent). The EQ moved into BUS X affects everything.
6. **Dragging** by a slot's header: within a bus, across buses, into and out of BUS X, generators among generators (no dip). Drop line, edge scrolling, Esc cancels, a drop outside a column does nothing.
7. **Param 1 follows the EQ:** after a move, FL calls it "SLOT X.1: kHs 3-Band EQ - Low" (or wherever it is) and automation still drives it. The EQ removed (×): "Param 1", drives nothing, no crash; its window closes. The bay's controls stay Param 2 to 17.
8. **Removing** a slot, a generator, a sub-bus, a bus with a sub-bus while playing: a dip, no crash, no click.
9. **Save and reopen:** the same structure, plugin settings, mutes, Param 1's binding; nothing loads twice. A project from before 1b.5a opens as a new instance (expected).
10. **Diagnostics** widens the window by 400 px and back; corner resizing works with it open.
11. **Bay fades** (defaults: round trip 3; bay latency out 17, in 100; Mute, Bypass, Off 34; dropouts out 17, in 100; settle 0; tail cut release 8): linear phase on and off in the bay (the click caught by the fade out, the return eased in without a pulse); removing and re-adding the transmitter.
12. **Regression:** the bay's sound, Mute/Bypass/Off, a marker cut, real blocks at 1 and 2 blocks.

## Real blocks, settings defaults, names (passed 2026-10-08)
1. **1 → 2 blocks:** play at 1 block, let it settle, then 2: Missing blocks stay 0. Also 3 → 2, Automatic's fallback to 2 (no wire into the receiver), and Recheck real block size (or a shrink while stopped) at a fixed 2.
2. **Real blocks, Automatic:** the Round trip line goes from "1 × 512; real blocks: watching them" to "1 × N; real blocks (announced 512)", N about 1.25 × FL's largest block (269 at 140 BPM); "1 block works"; Missing blocks 0; Lag > Measure through a pass-through bay: 0.
3. **Tempo changes while playing** (140 → 100 → 60): the unit grows (one event line per step); raising the tempo doesn't shrink it; Recheck real block size does; Reset counters doesn't.
4. **Lowest tempo** 60: the unit jumps to 512; 120: about 313; 300: back to what's seen; empty: rechecks.
5. **2 and 3 blocks**, with and without the wire into the receiver: Missing blocks 0 after the start; the orange tempo-drop warning while the lowest tempo is empty.
6. **Announced blocks:** 512 × blocks; Recheck greyed out.
7. **Stopped:** the Transport line follows FL; the round trip shrinks once quiet 0.5 s (default rule); playing notes holds it off; Always and Never; one check per stop.
8. **Retry 1 block** (Automatic, after a fallback, with the wire added): "1 block works". Automating "Bay 1: retry 1-block round trip" and "Bays: recheck block size" acts once per switch on.
9. **Alt+Shift while resizing:** no Windows language flyout (with the setting on); typing into FL still works.
10. **Set as default and reset** (right-click a checkbox, a radio option, a typed number): Reset to default, Reset to factory default, Reset all (asks first); values hold across editors and instances.
11. **Names:** FL renames a host control at once in its browser and on reopening elsewhere; automation clips keep their own names but still drive the parameter (see `decisions.md`, "Names probe").

## Bay slot states and dropouts (passed 2026-10-07/08)
1. **Latency changes:** linear-phase EQ 2 in the bay, linear phase on and off at several settings: no audible gap or click; no missing blocks (held back by the transmitter instead); Automatic stays at 1 block.
2. **Dropouts:** remove the transmitter while playing: a short reversed ghost fades to silence, no click; add it back: the bay's sound fades in. Cut the marker wire: held back about 0.7 s, then stamped by the receiver.
3. **A new panel plays before the bay returns**, flagged orange; adding the relays crossfades into the bay's sound.
4. **Mute:** the panel's sound at the same delay; a reverb in the bay keeps its tail; unmute crossfades straight back.
5. **Bypass:** the panel's sound at the same delay; Tail cut output 1 (linked through a Peak Controller to clear the bay plugin); leaving it: the panel's sound until the bay's new sound arrives, then a crossfade; no gap, no old tail, no hidden pop through a reverb.
6. **Off:** the panel's sound at once (FL re-shifts the project), bay silent, tail cut 1; back to Active as in 5, plus one blended jump of delay.
7. **Leaving a state with the bay gone:** the panel's sound carries on, flagged; adding the transmitter crossfades in.
8. **Save and reopen** in each state: the state comes back; the relay keeps its Tail cut value.

## Bay link basics (for regression runs when bays change)
**Record for every stage:** Diagnostics (round trip, block sizes, headroom, missing blocks, return vs sent, lag), each relay window (link, missed blocks, headroom, block sizes, "Audio: running"), anything audible, any crash or hang.

1. **Link alone (no FL plugin):** panel, receiver, transmitter in Patcher, the receiver's main and Marker outputs into the transmitter. **Expect:** receiver "Linked to Panel 1, panel running"; transmitter "Linked to Panel 1, receiver playing"; Diagnostics "Linked as Panel 1", "receiver playing; transmitter returning". Playing: the same sound as Mute, "Return vs sent: now identical", missing blocks 0, Lag > Measure "0 samples (an exact copy)". Switching Mute and Active: no audible difference, no timing jump.
2. **An FL plugin in the bay** (Fruity Filter): heard in Active only, its knobs work, missing blocks 0, Lag one path near 0.
3. **A plugin with latency** (lookahead or linear phase): with the marker wired, Diagnostics shows it as Bay latency and the return is in time; without the marker, Lag shows one path at its latency (linear-phase EQ 2 at its highest: 12,288).
4. **Round trip and order:** 2 blocks without the wire into the receiver, a few minutes under CPU load: missing 0, receiver missed at most 1 after linking; 3 blocks: 0; 1 block without the wire: missing climbs and the panel's own sound plays, flagged; 1 block with the wire: missing stops within a block or two.
5. **Block sizes:** change FL's buffer length (128 to 1024) while open: the round trip follows, relays relink, missing 0 at 2 and 3 blocks.
6. **Adding, removing, reloading:** remove and re-add the transmitter, then the receiver (silence, then the return resumes); remove the panel (relays wait, relink within a second when it's back); relays first, then the panel; save and reopen (modes, state and values kept, everything relinks); a second receiver ("every panel already has a receiver"); switching a relay's mode while playing; a second panel ("Panel 2", relays stay on Panel 1); Patcher's smart disable on and off; closing FL while playing (no crash).
7. **Optional:** relays on mixer inserts instead of inside Patcher (receiver's smart disable off); FL's "Make bridged" on a relay ("not linked" for that relay only).
