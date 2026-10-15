# FL checks

The user's checks in FL Studio: the latest slice's, and the earlier ones kept for regression runs. Read only when preparing or running a check; `docs/current.md` points here. Results go into `docs/decisions.md`.

**Common setup:** close FL during the build, so the junctions in the system VST3 folder pick up the Debug builds (if FL was open, restart it). Fresh blank project, 48 kHz, ASIO4ALL, buffer 512. Bay tests run inside Patcher (smart disable off): panel, receiver relay, an FL plugin, transmitter relay, the marker wire (the receiver's Marker output, target 1, into the transmitter's Marker input), and for 1-block round trips a wire from the panel into the receiver. Diagnostics open.

## 1b.5b part A round 3 and part B: settings, shortcuts, slot states (built 2026-10-15)
**First:** close FL during the build; then Settings > Factory reset (confirm), so the new settings start at their defaults. Old projects open as new instances (engine state version 3).

**Part A round 3**
1. **Pixel text near 100%** (Ctrl+drag a corner to 101 to 110%, both renderings): no label loses its first pixel column ("k" keeps its stroke, "S" its left edge). Text now only ever moves right or down when it's lined up.
2. **Re-rendering:** switch Settings > Panel, open and close Diagnostics, make tiny size changes: the pixel text is crisp every time, without a hover or scroll to fix it.
3. **Two instances, both editors open:** change a Slot text setting (or any setting) in one: the other changes at once.
4. **Grip hint:** hover a corner grip: line 1 shows the scale and size (e.g. "125%   988 x 1588"), updating while you drag; the old text from line 2.
5. **Hint scrolling** (Settings > General > Hints): hover a long hint (the Resizing heading): it waits (smart delay: about 3 s at the design width), scrolls at 1 line per second, waits at the bottom, jumps back to the top and starts again. Alt+wheel stops it for that hint. Smart delay off: the 3 s / 1 line/s / 3 s fields apply; speed 0: no scrolling.
6. **Sections:** the title, resets and note stay at the top, with Expand all and Collapse all; below them General, Window, Slot text, Bays, Slot changes, Keyboard Shortcuts, each under a bar (pinned while its section scrolls by) that collapses it; right-click a bar resets the section. Collapse some, close and reopen the editor, switch tabs, save and reload the project: they stay. A new instance follows "Settings open": as set in last instance (the last collapse state from any instance), expanded, or collapsed.
7. **Ctrl+T:** flips to Settings and back to Panel, also while a number field has focus.
8. **Shortcut box** (Keyboard Shortcuts): click the box ("Press keys"), press Ctrl+K: it shows "Ctrl+K"; click elsewhere: back to Ctrl+T. Again, then ">": Ctrl+K flips the tabs, Ctrl+T doesn't. Backspace or Delete while listening: "none"; Esc cancels. Press a plain letter FL's typing keyboard uses (e.g. Q): ⚠ appears between ">" and the label (its hint: passed on to the host); the first click on ">" shows the warning in the hint pane in orange without moving anything on the line; a second click (or a double-click) applies; ⚠ stays; Q now flips the tabs and no longer plays a note in FL. On a different keyboard layout (e.g. German), the box shows the layout's letters.

**Part B: slot states** (use kHs Compactor, 489 samples of latency, and kHs Reverb from "+ slot")
9. **Header:** two rows: label, name, ×; then M, B, OFF (tiny, grey; the selected one orange) and a short MIX slider. Empty slots: all shown but disabled. Generators: the old Mute checkbox is gone.
10. **MUTE** on an FX slot: the dry sound plays (34 ms crossfade), no timing jump, the host's latency unchanged; the plugin keeps running (a reverb tail continues underneath and is there when you go back ON). On a generator: silence, held notes still there when back ON. M again: back ON.
11. **BYPASS:** like MUTE, but the plugin stops; back ON, no old reverb tail plays (it was reset). With Compactor: no timing jump either way, the host's latency unchanged.
12. **OFF:** the output dips (17 ms out, 100 ms in) and the slot leaves the graph: with Compactor, the host's latency drops by 489 samples (Diagnostics' "host told"); back ON, it returns, under the dip again. A generator OFF: silent.
13. **MIX:** dry/wet, linear; on a generator it's the level. Double-click: 100%. Its hint shows the percentage.
14. **End signal path when off** (right-click OFF > tick it; a line under OFF shows it's set): with OFF selected, every later slot in that bus is greyed with "Cut off: an earlier slot ends the signal path", silent, and its latency gone (put Compactor after it); their own buttons still work. Sub-buses into the same bus still play. Unticked, or back ON: the later slots return.
15. **Saved:** states, MIX and the OFF option survive saving and reloading the project.
16. **The bay** (Diagnostics' Slot buttons): works as before; the events log says "Slot state: on" instead of "active". Settings > Slot changes: "MUTE and BYPASS" fades (34 ms each) now apply to slots and the bay; the bay's own OFF fade stays under Bays.
17. **Regression:** 1b.5a's steps 2 to 8.

## 1b.5b part A, second build: pixel text and polish (built 2026-10-13)
Run after the part A steps below (they still apply, except where this changes them).

1. **Pixel text at 100%:** slot names, "6 of 2086 parameters", the bay's "Control 1": every stroke 1 pixel, the 1-pixel gaps inside letters (r, n, o, s) open, as in the "ideal" screenshot. Slot buttons ("Open", "Choose plugin…", bus chips) too.
2. **Scaling (Ctrl+drag a corner, both Smooth and Sharp):** at 110%, 125% and 140% the pixel text stays exactly 1 pixel per font pixel (so it's a little smaller than the rest); from 150% it's 2 pixels per font pixel (a little bigger); every size stays crisp with even strokes. Smooth softens it while dragging and sharpens when you stop.
3. **Settings > Slot text:** "% past each step" (default 50): 30 makes the step at 130%, 100 at 200%. "Snap pixel text" off: at in-between sizes some strokes blur or vanish (on again fixes it). "Anti-alias pixel text": soft edges, growing in steps of the "% of a pixel" field (default 25; try 10 and 50). Rendering fallback: Barlow; the other four grey out.
4. **Context menus:** a setting with no default of your own offers "Set as default" and "Reset to factory default (…)"; with one, "Reset to user default (…)" and "Reset to factory default (…)". Right-click a heading (e.g. Bay fades): "Reset to factory defaults" for the whole group, plus "Reset to user defaults" when any setting in it has one.
5. **Settings text:** option labels grey, values typed into fields white; the "While a window corner is dragged…" label wraps naturally (no forced break); fade labels plain ("Mute", no bold, no "(up to …)"); ranges, the control output gain and the double-click range are in the hints, written "(min to max)"; "Milliseconds; 0 is off." is the Bay fades and Slot changes headings' hint. The hint pane text is grey.
6. **Controls:** buttons, number fields, checkboxes and radios: dark (#1a1d21) with a grey border, Barlow; checked boxes and radios show an orange tick or dot; hover lightens the border. Slot buttons keep the slot font.
7. **Diagnostics:** one label column for every group, labels right-aligned; "receiver: …" and "transmitter: …"; "(announced 512)" on its own line under the round trip.

## 1b.5b part A: UI additions (built 2026-10-11)
**First:** Settings > Factory reset (confirm), so the new settings start at their defaults.

1. **Hint pane** (60 px at the bottom): hover any setting row, heading, button, slot header, ×, bus chip, slider, Diagnostics line and the corner grips: its explanation shows there; nothing hovered, nothing shown. A long hint (Settings > the "Resizing" heading, "Bay tail cut release"): **Alt+wheel** while still over it scrolls the hint a line at a time; releasing Alt afterwards doesn't open FL's menu bar. No browser tooltips anywhere.
2. **Settings rows:** labels without their parenthetical explanations (those are in the hints; units and ranges stay). Each checkbox, radio and number field sits level with the first line of its text. Clicking a row's text does nothing; only the control itself changes it. Right-click still works on the whole row.
3. **Resets:** "Reset all" and "Factory reset" (help cursor over both) each ask first. Make a default of your own (right-click a setting > Set as default), change it, then: Reset all > your own default; Factory reset > the factory value, your own default still offered on right-click. Right-click Reset all > Clear user defaults (asks; values stay). Right-click Factory reset > Hard reset to factory defaults (asks; everything at the factory's, no defaults of your own).
4. **Swap width and height** (beside Reset size): the window turns landscape (1,270 × 790 plus FL's border) and back; with Diagnostics open the 400 px stays on the right. Alt+click swaps around the window's centre (detached, attached inside FL's workspace, and in Patcher). With "flip drag and alt+drag" on, the two are swapped.
5. **Diagnostics:** Reset counters at the top; the groups in the new order with dividers; labels and units grey (#555555), the values white; explanations only in the hints. The names probe, key log and window list are gone. The bay's MUTE/BYPASS/OFF buttons, pressed: orange.
6. **Buses:** with BUS X taller than the other buses (add slots to it), the sideways scrollbar sits at the bottom of the whole bus area.
7. **Fonts:** stage bars and "Bay link" in Rubik Bold; setting headings and "Events" in Rubik; bus titles and body text in Barlow; slot text in m5x7, crisp pixels at 100% and 200% (uneven pixel widths between is expected). Settings > Slot text: "Anti-alias pixel text" softens it; "Rendering fallback" switches slot text to small Barlow (and greys out the first).
8. **Colours:** checkboxes, radios, sliders, the tab underline, pressed bus chips: orange (#ff6000). Drag a slot: the drop line and the ghost's border are pink-red (#ff0055). Menus: the page background with a thin orange border. Warnings still light orange.
9. **Regression:** 1b.5a's steps 2 to 8 (sound, collapsing, adding, routing, dragging, Param 1, removing).

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
