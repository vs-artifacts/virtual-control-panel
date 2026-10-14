# Decisions

Short log of settled decisions, their reasons and the measured facts behind them. Newest at the bottom. Update when something changes; don't re-debate settled items without new information. Where things stand and what's next: `docs/current.md` (the handover). FL check steps live in `docs/fl-checks.md`; only their results are recorded here.

## Platforms and toolchain
- **Windows now, macOS required.** Dev happens on Windows (VS Code + clangd + MSVC + Ninja). macOS support is mandatory: the user's target setup is a headless Mac mini. Don't pick Windows-only rendering tech.
- **JUCE 9** (submodule on master, 9.0.1+).
- **Formats:** Standalone, VST3, CLAP on Windows. Re-add `AU` to `FORMATS` before any macOS/CI build (the CI workflow expects AU on macOS).
- **Debug by default**; Release in `Builds-Release/` for performance testing.
- **GitHub Actions:** triggers off (manual only) while the repo is private, to save minutes.

## Performance targets
- UI frame rate: **30 fps hard floor, 60 fps goal.** "Smooth when a control is touched" is acceptable during style prototyping; optimise hard after the style is settled, but architect for performance from the start.
- **Keep the CPU for DSP.** Offload UI rendering to the GPU wherever possible. Audio and UI run on separate threads (JUCE audio thread vs message thread); keep all UI work off the audio thread.

## Product shape
- **A live-performance control panel and plugin host.** It hosts the user's licensed desktop plugins (iZotope, Kilohearts, FabFilter, Serum 2, Sylenth1, ...) and centralises/abstracts their controls and routing: audio, MIDI and parameters, with two-way parameter comms (live writes; live reads, pushed where the plugin reports changes, periodic resync otherwise).
- **One shared engine, two shells.** Engine = routing graph + plugin hosting + control protocol. Shells: a **plugin** (VST3 first; tested in FL Studio and FL's Patcher) now, a **standalone app** (headless Mac mini in the live rig) later. Keep the engine free of shell-specific code.
- Two-way control of *sibling* plugins in a DAW is impossible via plugin APIs; that's why target plugins are hosted *inside* our plugin.
- **"Brainlessly easy" to run live:** auto-start, restore last session, pick audio device by name, pre-flight status screen, dedicated travel router, tablet opens a saved web app. Mostly standalone-app/setup concerns; design for it.
- **Modularity / failure isolation:** level A now (UI/network failure never affects audio; UI reconnects and resyncs). Level B (watchdog + auto-restart + session restore) is a bonus, standalone only. Level C (plugins in separate processes) later; don't block it architecturally.
- **Hardware MIDI controllers** go straight into the host machine for note playing; the tablet is for control, not timing-critical notes.
- **Max/MSP is not in the core.** RNBO (exports C++ for JUCE) is a possible later route for the user's own DSP modules.
- **Configurability is a core UX value** (alongside security): where it's cheap, offer a setting instead of hard-coding a trade-off. Plugin-wide preferences go in `PluginSettings`; per-project state goes in the plugin's saved state.

## Target production form (user's brief, 2026-09-25)
In effect a pseudo-wrapper for a mini-DAW: a patcher-style routing matrix with the user's own UI design language and UX decisions on top. The blueprint docs will expand on this.
- **Hosting and routing:** an arbitrary (configurable) number of generators, routed to an arbitrary number of buses; each bus holds an arbitrary number of FX slots. Any slot (hosted plugin) can route or sidechain to any other slot on any bus: fully modular, grid-style routing.
- **The slot structure (user, 2026-10-08):**
  - **Generator stage** first: each generator chooses its own output bus or buses (a new generator: BUS 1). Generators that take input (sidechains) are possible but rare, so they're kept conceptually as the first stage.
  - **Buses** process their slots in sequence. A bus's input is whatever is routed to it, by default from the generator stage; its output goes to the master bus's end stage by default, but can be routed into other buses or slots. A new bus gets no generators and outputs to BUS X.0.
  - **Sub-buses** are a submix: a bus whose output feeds its parent bus's end stage by default (unless, or as well as, it's routed elsewhere). Example: generator 2 → bus 2, whose frequency splitter sends each band to a sub-bus, recombined in bus 2's end stage, then to master. Under the hood, possibly just buses (the splitter's slot routes its bands into buses 3, 4 and 5, which route to master); buses are an organizing and routing aid.
  - **Numbering:** slots are "b.n" (bus.slot). Each scope has a pre stage numbered 0 and a post stage numbered X: every bus has b.0 (pre-FX) and b.X (post-FX), and the whole FX grid has **BUS X** (the master) with slots X.0, X.1 … X.X. The 0 and X stages hold utilities (gain, pan, simple adjustments and effects not part of a patch's "flavor"). Unique bus types may come later, still numbered b.n underneath.
  - **Default routing** is sequential within a bus; "out of chain" routes (any slot to any other slot or generator, sidechains) come later.
  - **Summing** where routes meet: for now simply added; a toggleable, probably configurable option per bus or stage later.
  - **Slots can be dragged** to another position on any bus and re-route by the default sequential rule. **Each slot's plugin can be hot-swapped.**
  - **Each slot exposes** any number of its plugin's parameters as knobs, sliders or other inputs, and more can be added.
- **Exposed parameters:** each FX slot (in its default mode, at least) exposes a configurable set of parameters linked to its hosted plugin. Only the parameters the user chooses are exposed, in one consistent format and style matching the design theme.
- **Live performance UX:** stored parameter sets act as waypoints; transitions between them follow arbitrary automation curves or settings, interpolating groups of parameters together.
- **Design:** the glassmorphism/light style; by virtue of its design, the UI doubles as a stage visualiser.
- **Why:** no DAW offers the configurability the user's sound design and composition workflow needs, or the aesthetics wanted on stage.
- **Origin:** processing and cross-processing FX streams from parallel Serum 2 and Sylenth1 instances fed by one MIDI input.

### Architectural implications (agent's reading; revisit with the blueprint)
- **Routing is data.** The engine should build its graph from a saved, editable routing model (slots, buses, connections), not hard-coded wiring. `juce::AudioProcessorGraph` supports adding and removing nodes and connections while running.
- **Slots have permanent IDs** (agent, agreed 2026-10-08): swaps, drags, states, bindings and bays all key on them, so the structure comes before plugin swap. **Each slot is one permanent box in the graph with its plugin inside**: states, fades, swaps and latency bookkeeping happen inside the box, so most actions don't rewire the graph (only adding, removing or moving slots, and Off, do). **A bay is a kind of slot.** Once anything can route anywhere, loops are refused with a message.
- **Sidechains and multiple ins/outs:** the current stereo-only rule (aux buses disabled) must go; slots need addressable input/output buses.
- **A parameter-mapping layer per slot:** which hosted parameters are exposed, with display names, ranges and curves. Plugins expose hundreds (Serum 2: 541 automatable).
- **Waypoint interpolation lives in the engine, not the UI,** so transitions stay smooth regardless of UI frame rate or network, and keep running with the UI disconnected.
- **Scale:** many generators and FX on one audio thread won't last; plan for multi-core graph processing (and level C process isolation) later.
- **Visualiser:** the UI will likely want live audio-derived data (levels, spectra) streamed from the engine.

## UI architecture
- **Svelte 5 + Vite + TypeScript** web UI in `ui/` (monorepo). Shown inside the plugin editor via JUCE's `WebBrowserComponent` (WebView2 on Windows, WKWebView on macOS) and on tablets/displays via their browser. The built UI is embedded in every build.
- **One protocol, two transports** *(decided in 1b, replaces "own WebSocket protocol for the editor too")*. The plugin editor talks to the engine through JUCE's native web bridge (no network surface). Remote devices use a WebSocket server that only runs when remote control is switched on (always in the headless standalone, opt-in in the plugin). Same JSON messages on both. Security rules: "Slice 1b" below.
- **All glass visuals are rendered in WebGL2 with custom shaders**, not CSS. The panel scene is rendered to textures on a canvas; DOM only for things like text overlays where useful. WebGPU is a possible later upgrade; keep rendering code structured to allow it.
- Required shader capabilities (all available in WebGL2): explicit texture wrap modes (`CLAMP_TO_EDGE`) and/or manual UV clamping in shaders, render-to-texture passes with exact capture bounds, floating-point render targets (`RGBA16F` via `EXT_color_buffer_float`) so additive energy can exceed 1.0 before tone mapping, custom blend maths (additive, plus-lighter, dodge, etc).
- **Signature effect: "edge-clamped convolution blooming".** A thin bright border, when overlapped by a blurred additive element, blooms massively because the blur samples outside the captured bounds as the duplicated edge row (clamp, not transparent). Implement it deliberately (exact capture bounds + clamped sampling + HDR additive compositing), with controllable parameters (bloom gain, extent, per-edge), rather than depending on a browser artifact.
- The JUCE-native GlassToggleButton prototype (parent snapshot + `applyGaussianBlurEffect`) was a workflow test only; deleted in 1a (recoverable from commit `3d7b75c`).

## Workflow
- **Style tokens:** all visual constants live in one place, in the web UI (JSON/TS), with a debug tuning panel (sliders/colour pickers, copy/save). Vite hot reload gives live editing. (Replaces the earlier C++ JSON token plan.)
- **Specs:** split into small files under `docs/` (see index in `CLAUDE.md`), not one monolithic blueprint. Behaviour lives in each component's spec; global material rules in `docs/style/glass.md`.
- **Assets:** one SVG per component with named layer ids; states are usually parameters applied in code, not separate files. Separate state SVGs only when geometry actually morphs (matching path structure).
- **Iteration:** FL Studio / Patcher (via the VST3 junction) for in-DAW checks; web UI iterated in a browser with Vite hot reload where possible.
- **Platforms in practice:** build and test on Windows first; keep platform-specific code isolated; test the web UI in Safari early; smoke-test on a Mac at milestones.

## Working preferences (user)
- Precision first: controlled parameters leave no room for calibration error, so no fiddly correction chains inside FL.
- Keep diagnostics that make FL's behaviour visible (silence on missing blocks, running indicators, exact level readouts) until the user drops them.
- FL-specific behaviour goes behind settings, not hard-coded. Tidying the settings is a later polishing step.
- Relays stay light (an instrument package needs 2 per FL plugin); relays working without a panel is a wanted later utility.
- Small slices, each ending in an FL check by the user (checklists in `docs/fl-checks.md`). Testing uses a fresh blank project each time; save and reload only when a test calls for it.
- Latency matters for live playing: prefer 1 block per bay; several bays in series at large buffers is the worry. Zero-latency segments only if the numbers are bad, and only if they reassemble into one interface.
- Nothing the user must re-measure and re-enter when slots move or plugins change (e.g. a bay plugin's latency is found automatically).
- Naming: "bypass" is the explicit switch of what plays. Slot states: Active, Mute, Bypass, Off. Dropouts play silence ("safe", dry dropouts, is a future refinement).
- Safety over continuity: a dropout must never be louder than the bay's sound would have been, so dropouts are silence (faded).
- Reopening a saved project restores the panel's UI exactly as it was left (tab, sections, scroll positions).
- Fades: separate fade out and fade in where a cause has both (short out against the click, long in against a pulse). 10 ms still pops on bass-heavy material for crossfades between unrelated audio.
- Smooth rendering means soft, unsnapped everything (text included): proportional positions matter more than crispness. Sharp is the crisp option.
- Sessions get expensive when long: hand over through these docs at natural break points.

## Plan: thin vertical slice (each sub-slice needs the user's go, ideally its own chat session)
1. **1a. Host inside the plugin:** our VST3 loads one VST3 from a fixed path (test plugin: `C:\Program Files\Common Files\VST3\Kilohearts\kHs 3-Band EQ.vst3`), passes audio and MIDI through, can open its editor window, lists its parameters. C++ only, no scanning. **Built and FL-checked 2026-09-25** (audio, hosted editor, live values, state in projects and Fruity Wrapper presets). Patcher had no MIDI event input, which led to 1a.2.
   - **1a.2. Instrument + parallel generators:** switch to an instrument; Serum 2 and Sylenth1 in parallel on one MIDI input, summed into the kHs EQ, plus a MIDI pass-through lane. **Built 2026-09-25; FL-checked 2026-09-26.**
2. **1b. Bridge:** the plugin editor shows a minimal Svelte page; one plain slider controls one hosted parameter two-way, including pushed updates when the hosted plugin's own UI changes it; then a remote-control server. Sub-slices:
   - **1b.1. Page in the editor** via JUCE's native bridge. **Built and FL-checked 2026-09-26** (page shows, second instance fine, hosted plugins unaffected; its findings, slow reopen, white flash and no key pass-through, fixed and re-checked the same day).
   - **1b.2. One slider, two-way** (kHs 3-Band EQ low band gain) via the native bridge, **also exposed to the host** for automation and controllers (a core mechanic, so tested early). **Built and FL-checked 2026-09-27,** with its wrap-up (tabs, scaling and corner resizing, the host's right-click menu, keys while the mouse is held; "1b.2 wrap-up"). Not testable then: a hardware controller, and automation across plugin swaps (1b.5).
   - **1b.3. Parameter outputs:** can our plugin hand values to FL as *outputs* (Patcher's controls list, internal controller links)? Tested with two temporary parameters moved from the audio thread (a meter-category one, which JUCE marks read-only in VST3, and an ordinary automatable one). No MIDI CC out: the user found it doesn't drive FL plugins in Patcher. **FL check 2026-10-01: no parameter outputs** ("Module bays and relay nodes"); the test parameters are removed.
   - **1b.4. External module bays:** our way to use FL's own plugins, which we can't host ("1b.2 wrap-up"). FL forbids feedback loops, so relay nodes on both sides of the FL plugin talk to our plugin directly instead of over Patcher wires ("Module bays and relay nodes"). Placed before plugin swap to validate the idea, or to settle a full move away from FL's plugins. **Done, all FL-checked 2026-10-01 to 2026-10-08:** the audio round trip, control outputs and the automatic round trip, the bay plugin's own latency (the position marker, C1 and C2), slot states and silent dropouts, real blocks. Left: the Diagnostics line "return silent while the sent audio isn't" (kept for the alpha slot interface).
   - **1b.5. Slots and plugin swap** (core, non-negotiable: choosing the plugin in a slot). Split into sub-slices, each with its FL check (agreed 2026-10-08; details in "Slots: decided with the user (2026-10-08)"):
     - **a. Structure and graph:** the generator stage, buses (with sub-buses) and slots with permanent IDs, routed in sequence within each bus, saved with the project; add, remove and move slots (drag). **Built 2026-10-10**; the user's first look in FL was positive. Older projects open as a new instance (no backward compatibility before the first release). See "1b.5a as built".
     - **b. Slot states** inside each slot's box: Mute, Bypass, Off, and "End signal path when off"; the default slot controls (M, B, X, MIX).
     - **c. Plugin swap** from a flat list of installed plugins, with a rule for the slot's bindings.
     - **d. Exposed controls per slot,** host-control binding ("Bind…") and dynamic names ("SLOT 1.1: parameter").
     - **e. Bays as slots:** relays choose a bay number; per-bay control values; Bay 2 to 4's parameters work.
     - **f. A copy of FL's plugin menu** (its categories and favourites, plus "More plugins…"), read from FL's plugin database (`Documents\Image-Line\FL Studio\Presets\Plugin database`; Documents may be in OneDrive, so the path comes from Windows) each time it opens. Only folder and file names are read (the `.fst` files are an undocumented binary format: never parsed), matched against our own scan. Styleable like the rest of our UI later.
     - Lets us test automation across swaps.
   - **1b.6. Remote server:** Crow WebSocket server behind a remote-control toggle, own update thread, security tests, "Open in browser" button.
3. **1c. Shader knob:** the slider becomes a WebGL2 glass/light-scatter knob with a frame-time readout, running inside FL.
- Style work starts after 1c.

## Open items (kept current)
**Slots (1b.5):**
- **A hosted plugin changing its own latency** (e.g. linear phase in a slot) isn't followed yet: its box takes the latency when prepared. Needed before latent plugins go into slots.
- **Removing a slot asks nothing first** (× removes it with its plugin's settings). A confirmation or undo: the user's call.
- **Sub-bus layout and routing:** expect regular revision (layouts to try: buses scrolling right, "leaf" tabs, wrapped rows with strict or top-aligned rows).
- **Dynamic names (1b.5d):** safe to build ("Names probe"). The probe itself is archived ("Archived diagnostics").
- **"Tidy host controls"** (renumber the pool into slot order, with a warning): a definite later addition.
- **Double-click "sets a value":** a popup with a text entry (the setting exists as a placeholder).
- **Choosing and naming bay controls** in the UI.

**Page (1b.5b part A):**
- **Pixel fonts are still being tuned** (user, 2026-10-14: in parallel with part B): the 75% threshold, stepping and snapping await the user's FL check; Tiny5 (p3) is unchecked in practice. Fallback if thresholding stays fragile: drawing pixel text ourselves. State of play: `docs/current.md`.
- **Part A's full FL checklist** (`fl-checks.md`, both part A sections) hasn't been run yet.

**Bays (1b.4, 1b.5e):**
- **The Diagnostics line "return silent while the sent audio isn't"**: kept for the alpha slot interface.
- **Real blocks at 2 or 3 blocks:** each change of the unit makes the receiver line up again (a gap of about a block in the bay). If audible: a hub call that updates the details without a new session (hub version 4). See "B: real blocks".
- **Future refinements:** dry dropouts ("Safe"), the latency-change pop fade ("linear phase pop fade"), virtual lookahead automation (each under its own heading below).
- **Unexplained link problems** from the C1 check (a re-added panel not relinking; a second panel's link carrying the first receiver's marker after the PC slept). Not reproduced; an optional sleep and wake test (play, sleep, wake, read every relay's Panels section and Diagnostics before touching anything).
- **FL's "Use time offset"** (wrapper option): tempo-synced effects in a bay may act off the grid by the receiver's lead. Not investigated.
- A control lead: not by default (it costs the same latency it cancels); maybe a setting later.
- **Pops (polish):** adding or removing relays, cutting wires inside the bay, switching relay modes or the round trip, a new transmitter's first audio.
- Fixed buffer sizes in the Fruity Wrapper: a measured test.
- The macOS hub finder (written down, not built). Relay naming beyond "first panel found".
- Updating the settings' factory defaults for release: when the user is ready.

## Slice 1a: hosting (2026-09-25)
- **Web UI re-confirmed** after weighing JUCE-only: the Mac is headless, so the tablet is the real screen and a web UI is needed anyway; one Svelte app serves both. Fallback if WebView2 had failed inside FL: a plain JUCE editor in the DAW, the web UI still serving tablets over the same protocol.
- **Engine code lives in `source/engine/`** (`Engine` runs the routing graph; `HostedPlugin` is one slot's plugin). Plugins load on the message thread; swapping while audio runs needs a realtime-safe handover.
- **Stereo only:** our buses and the hosted plugin's main buses; its sidechain/aux buses are disabled.
- **Saved state:** every DAW's wrapper saves a plugin as an opaque blob it asks the plugin for, so we store each hosted plugin's state inside ours (XML, tagged with the plugin's identifier) and only restore it into the same plugin.
- **Threading:** hosted VST3s require `prepareToPlay` and get/set state on the message thread (JUCE asserts). VST3 hosts call ours there, so we forward directly.
- **VST3 parameter lists include MIDI CC slots** (16 channels x 130 controllers); the EQ reports 6 real + 2080 MIDI. `HostedPlugin::getControls()` returns only the real controls (flagged automatable), and the UI shows only those. Expand later for plugins that need longer lists.
- **Our own plugin exposes the same 2080 MIDI CC slots:** JUCE turns on MIDI CC emulation whenever MIDI input is enabled. VST3 has no MIDI CC events; hosts deliver CC, pitch bend and aftertouch as these parameters, so keep it on.
- **Patcher offered no MIDI event input pin** on our plugin as an effect, though our VST3 declares one: FL's effect/generator split. Solved by the instrument switch (1a.2).

## Slice 1a.2: instrument + parallel generators (2026-09-25)
- **Instrument only (`IS_SYNTH`), same plugin ID;** no FX variant for now. FL only sends piano-roll notes to generators, and hosting synths is core.
- **The engine is a `juce::AudioProcessorGraph`:** boxes (plugins) and wires (connections). It fans MIDI out, sums audio where wires meet, delay-matches parallel paths, and swaps in routing changes safely while audio runs. It processes plugins one after another on one audio thread. MIDI routing is explicit graph wiring.
- **The fixed test routing** (Serum 2 and Sylenth1 in parallel, summed into the kHs 3-Band EQ, plus a MIDI pass-through lane) was replaced by the structure in 1b.5a, where its plugins are the default layout.
- **Slots have roles:** generator slots only take instruments, FX slots only effects. Needed because one VST3 file can hold several plugins (`Serum2.vst3` holds both Serum 2 and Serum 2 FX).
- **Saved state is per slot,** so two copies of the same plugin each get their own settings back.
- **A failed load keeps its slot** with an error message; everything else still loads and plays.
- **Measured:** Serum 2 loads in ~0.5 s and exposes 541 of 2623 parameters as controls; Sylenth1 ~4 s, 246 of 2326; the EQ 6 of 2086. The "Boot performance" benchmark runs 10 samples (~90 s).

## Slice 1b: bridge (2026-09-26)

### Security stance
- **The threat that matters is remote code execution and lateral movement** onto the user's machine and network, not someone changing the sound or crashing the DAW. Assume attackers with LLM-assisted bug hunting. Unoptimised UI is acceptable; an unnecessary or unhardened attack surface is not (also in `CLAUDE.md`).
- **Why two transports:** a network server exposes code that runs *before* authentication to every local program and, through the user's browser, to any website. The native bridge has no network surface at all; its only trust boundary is what loads inside our web view. So the DAW plugin opens no socket unless remote control is on.
- **Code inside our own page has full control in both designs.** Hence: CSP, everything bundled (no CDNs), Svelte text escaping only (never `{@html}`), plugin-supplied strings (names, presets) treated as untrusted.
- **A program running as the user wins regardless** (it can read memory, inject code, fake any header). OS-level trust; not defensible by our design.
- **Output safety limiter** (a ceiling on the engine output): flagged for the architecture, since a sudden blast can come from a bug or bad morph as easily as an attacker. Not in 1b.

### Web view lockdown (1b.1)
- **Page served from memory** by exact path from a fixed list (`source/bridge/UiFiles`); unknown paths get an empty local reply, so the web view never goes to the network. No dev-server origin is allowed.
- **Navigation limited to our page** (plus `about:blank`, which JUCE shows while the editor is hidden); JUCE already blocks pop-ups. Dev tools are off in Release (JUCE).
- **CSP** injected into the built page only: `default-src 'none'; script-src 'self'; style-src 'self'; base-uri 'none'; form-action 'none'`. JUCE's bridge still works (verified by the `[webview]` test).
- **Never fall back to Internet Explorer:** JUCE silently falls back when WebView2 is missing, so the editor checks first and shows a message instead.
- **WebView2 data** lives in `%LOCALAPPDATA%\heartseeker panel\WebView2` (its default, next to the host's executable, isn't writable for plugins).
- **WebView2 SDK:** `cmake-local/WebView2Sdk.cmake` downloads it per build folder at a pinned version and checks its SHA-256 (cross-checked once against nuget.org's published SHA-512). The *runtime* is Microsoft's auto-updated one; old SDKs work with newer runtimes, so SDK updates are rare.
- **npm:** install scripts disabled (`ui/.npmrc`). The build runs `npm install` (not `npm ci`, which deletes `node_modules` first and fails on Windows while an editor holds a package file open), then `npm run check` (warnings fail the build) and `npm run build`. The output is embedded as `UiBundle` with fixed file names.

### Editor fixes after the first FL check (1b.1)
- **Instances are independent:** each has its own editor, web view and bridge to its own engine. They share only WebView2's browser process (one per data folder, across all processes), so anything stored in the browser itself would be shared: keep per-instance UI state in the plugin.
- **Slow reopen (up to ~7 s):** the browser process shuts down when the last web view closes, and a new web view on the same data folder waits for that exit. Fix: `WebViewKeepAlive`, one hidden, empty web view per process (no page, no bridge, refuses all navigation), started after the first editor's page loads and kept while any instance exists. Plugin-wide setting "Keep web UI warm" (default on) trades memory for instant reopen. The support check runs once per process.
- **Hidden editor** (e.g. Fruity Wrapper's settings tab): the page stays loaded instead of being swapped for a blank page; the web view still tells it it's hidden, so animations pause.
- **White flash:** JUCE paints its placeholder white; ours and WebView2's default background are `#0f1012`.
- **Key pass-through:** keys typed while the page has focus go to the web view's own process, so the host never sees them. The page sends allowed keys over the bridge (`KeyForwarder`), and C++ posts them to the editor's parent window, the same messages JUCE passes on for keys it doesn't use.
  - Default chords: letters, digits, `[ ] ; ' , . /`, Space and Ctrl+Space, matched by physical key position (`KeyboardEvent.code`). Configurable via the settings file (`forwardedKeys`); no UI yet.
  - **Modifiers are read from the physical keyboard**, never taken from the page, and nothing passes with Alt, Shift or Windows held. So a compromised page can't turn an allowed key into a host shortcut such as Ctrl+S.
  - Releases always pass for keys that went down; the page releases everything on blur, and the forwarder on destruction, so the host never keeps a stuck note. Text fields keep their keys.
  - Hosted plugins' own windows still swallow keys (they aren't FL's windows); a workaround would mean intercepting other plugins' window messages. Parked; showing hosted editors inside our editor may solve it later.
- **Plugin-wide settings** (`PluginSettings`): an XML file in `%APPDATA%\heartseeker panel\`, shared by all instances and projects. Values from it are validated like any other input.

### Remote server rules (1b.6 and later LAN mode)
- **Crow >= 1.3.4.** Five advisories published Sept 2026 (auth bypass via middleware on WebSocket upgrade, Windows crash from nested JSON in Crow's parser, fragmented messages bypassing size limits, and two in features we won't use); the three relevant ones are fixed in 1.3.4. A 2022 use-after-free allowed code execution (up to 1.0+4). Boost.Beast rejected: hand-written hardened netcode isn't maintainable here.
- **Use Crow minimally:** auth checks in `onaccept` (never middleware), two routes, no Crow JSON / static folder / sessions / multipart / compression, `CROW_ENFORCE_WS_SPEC`, limits on request size, message size and connections, `signal_clear()` (else it hijacks the host's shutdown signals), `TCP_NODELAY`.
- **Checks:** bind `127.0.0.1` until LAN mode; per-session 128-bit token from the OS's secure random source, compared in constant time, stripped from the address bar after load; exact `Origin` and `Host` match (DNS rebinding); HTTP read-only, commands only over the authenticated WebSocket.
- **Messages:** size and nesting depth checked before parsing (JUCE's JSON parser is recursive with no depth limit), known handles only, values clamped.
- **LAN mode:** explicit opt-in, pairing that gives each device its own revocable token, dedicated WPA3 router; TLS optional (iPads need a one-time trusted certificate profile; without TLS anyone on that Wi-Fi can read and inject).

### Protocol and threading (1b.2 onwards)
- **Catalog on connect:** each exposed parameter gets a numeric handle for that connection; the catalog entry names its owning slot (bus > slot > exposed parameters). Updates are batched: one `params` message per tick; `set` messages carry a batch too.
- **Slot labels:** people see 1-based labels; code never uses a position as an identity. Dynamic slots will need permanent slot IDs.
- **Direct writes:** hosted VST3 `setValue` is lock-free and safe from any thread (JUCE stores it for the next audio block and updates the plugin's editor on the message thread), so incoming values are set straight from whichever thread receives them; no queue. Outgoing changes are found by polling `getValue` each tick (catches plugin UI, automation, morphs). Re-check for AU hosting on macOS.
- **Display text only on the message thread:** hosted VST3 `getText` takes a `MessageManagerLock`. The editor path lives on the message thread anyway; the remote server gets its own update thread for values, with text sent from the message thread at a lower rate.
- **Estimate** for 50 changed parameters per tick at 60 Hz to 3 clients: ~0.5 Mbit/s per client, well under 1% of a core for us. The real morph cost is how each hosted plugin reacts to parameter changes. *(Measured in 1b.2, see below: sync is cheap, the hosted plugin's reaction is not.)*
- **Benchmarks:** Debug numbers are smoke checks only (unoptimised, extra checks, uneven slowdown); quote numbers from `Builds-Release`. Boot benchmarks were representative because hosted plugins run their own optimised code.

## Slice 1b.2: one slider, two-way, exposed to the host (2026-09-27)

### Parameter model
- **Three layers.** *Native:* all of a hosted plugin's parameters; internal only, never exposed wholesale (20 plugins could mean tens of thousands). *Exposed:* the ones the user activates per slot, like FL Patcher's activated parameters (`Engine::expose`); what our UI shows. *Host controls:* our own VST3 parameters, the ones the host sees.
- **Why a pool:** VST3 hosts (and JUCE) need a plugin's parameter list fixed at load; the only official way to change it asks the host to reload the plugin, with patchy support. Patcher is a native FL plugin with private access to FL, so it isn't bound by this and can't be copied. So our plugin has a fixed pool of generic host controls (`HostControls`), and exposing a parameter binds it to a free one. Activated means automatable, as in Patcher.
- **Pool:** 256 for now; IDs `param1` to `param256`, names "Param 1" to "Param 256". Growing is safe because IDs are permanent; **never shrink it** (projects' automation refers to the IDs). Only bound controls cost anything per tick; the real cost of size is clutter in the host's lists.
- **Names:** dynamic, "SLOT b.n: parameter" ("Slots: decided with the user (2026-10-08)"); what FL does with renames: "Names probe".
- **Binding:** an exposed parameter takes a free host control (1b.2: the EQ's low gain to Param 1) and keeps it; bindings never move once made. Bindings are sticky: after a plugin swap a control stays, shown as unbound ("was X"), and the user rebinds it; freed controls are never reused automatically, so an old automation clip can't silently drive something new. A later iteration improves automation-binding handling.
- **Later targets:** a host control may later drive a macro or a morph position, not only a hosted parameter.
- **Shell split:** host controls exist only in the plugin shell (`source/HostControls`); the engine knows only exposed parameters. The standalone has no host; the remote protocol uses its own catalog.

### Data flow and threads
- **Page to host control to hosted parameter.** The page writes through the host control (`setValueNotifyingHost`, with begin/end gestures around a drag), so FL sees the change (last tweaked, recording).
- **Host (automation, controllers) to host control to hosted parameter.** Hosts may set it from the audio thread; the control forwards to the hosted `setValue` (lock-free), target first, so a report running meanwhile never sends the old value.
- **Hosted plugin's own window to host and page.** A 30 Hz message-thread timer compares each bound hosted parameter with its control and reports differences to the host (`HostControls::reportTargetChanges`). The page's session polls the hosted parameters themselves at 30 Hz.
- **Display text** of a host control comes from the hosted plugin only on the message thread (hosted `getText` waits for it); elsewhere it falls back to a plain number.
- **State restore:** host controls take the restored values without notifying the host, before the host reads them back.
- **Not yet:** passing the hosted plugin's own gestures on to the host (so a recording from the plugin's own window groups its moves); rebinding while running (needs a realtime-safe handover).

### Protocol (native bridge now, WebSocket in 1b.6)
- **`ControlSession`** (`source/bridge/`) doesn't know the transport. `catalog` (native function) starts or restarts a session: `[{ h, slot, plugin, name, host, v, t }]`. `params` (C++ to page, 30 Hz, only while the editor is shown): `[{ h, v, t }]` for changes since the last one. `set` (page to C++): `[{ h, v }]`, at most one batch per frame. `gesture`: `{ h, active }`.
- **Validation:** arrays only, at most 4096 entries (else the whole batch is dropped), integer handles in range, finite numbers clamped to 0–1; everything else is ignored. Nothing is accepted before the page has asked for the catalog.
- **Open gestures end** when the page restarts the session or the session ends (editor closed), so the host never stays stuck in a gesture.
- **The page ignores incoming values for a slider while dragging it** (replies still on their way would make it jitter); it always takes the display text.
- **Accepted:** JUCE parses page messages with its recursive JSON parser before we can check their depth. Only our own page can send, and a compromised page already has full control. WebSocket input is checked before parsing (1b.6).
- **For 1b.6:** the session is message-thread-only because `poll` fetches display text; the remote server's own update thread needs values and text split. Two sessions on one control (editor + remote) need shared gesture bookkeeping: JUCE asserts on overlapping gestures.

### Keys on focused controls
- Sliders, checkboxes and buttons don't count as text entry, so forwarded keys still reach the host while they have focus. Text fields and selects keep their keys.
- **"FL default"** (the user's name for how other plugins behave in FL; match it, maybe make it configurable later): keys pass through while the mouse is pressed anywhere in the plugin. A key held when a text field gains focus keeps sounding in the host until released, while new keys go into the field; once it's released, no keys pass while a text field has focus. Keys now pass during a mouse press in FL ("1b.2 wrap-up", Keys); the text-field part is still to do.

### Editor placement
- **First open only,** per instance (a flag in the instance's saved state), so windows stay where the user puts them.
- **Plugin-wide setting** (`editorPlacement`, on the Settings tab): off / the monitor the host opened it on (default; safe for attached windows) / the monitor under the mouse / the monitor with the host's main window. Centres in the monitor's work area; a window bigger than the monitor keeps its top-left corner on screen.
- **Detection, not host settings:** FL's detach setting isn't read (undocumented, version-specific). Only an *owned* top-level window moves (a floating window, such as FL's detached plugin windows). An unowned top-level window is the host's main window with the editor attached inside it, or our standalone window; it stays put. Windows only; macOS later.
- **Windows attached inside FL** got their own movement and placement setting on 2026-10-05 ("Resizing chords, smooth rendering, keys and windows").

### Benchmarks (Release, `Builds-Release/Benchmarks "[sync]"`, Serum 2 with all 541 controls exposed)
| Measurement | Mean |
|---|---|
| Poll every control, none changed | 1.4 µs |
| Change 50, then poll into a `params` message (with display text) | 122 µs |
| Parse and apply a `set` message with 50 values | 56 µs |
| Render a 512-sample block holding a note | 102 µs |
| Same, with 50 parameters changing every block | 1.03 ms |
- **Sync is cheap:** 50 changes per tick at 30 Hz is ~0.4% of a core per client; polling unchanged controls is negligible.
- **The hosted plugin's reaction dominates:** 50 parameters changing every block make Serum 2 about 10x more expensive (~10% of the 10.7 ms block budget at 48 kHz, for one instance playing one voice). Morphs will need a budget: how often a morph updates (every block vs 30–60 Hz), how many parameters move, and which ones (some trigger heavy recalculation). A per-parameter breakdown is a later benchmark.

### FL check (2026-09-27): passed
- Slider both ways; Param 1 in the browser, last tweaked, automation playback and recording, Patcher nodes and internal controller links; centring on first open. Its findings (the web view scaled to the monitor at 200% while the JUCE part followed FL at 100%; the browser's right-click menu instead of the host's options; no keys while the mouse was pressed) were fixed in the wrap-up.

## 1b.2 wrap-up (2026-09-27/28; FL-checked)

### Whole UI in the web view
- **The editor is only the web view,** filling it; tabs, slots, settings, corner grips and menus are all page code. JUCE draws nothing but a placeholder colour (and a message if no web view is available). Reason: the JUCE content was scaled with a transform, which doesn't move the native web view, so it stayed at its first size.
- **Page messages** (`editorState`, and events such as `tab`, `setting`, `resetSize`, `openPlugin`, `resize`): C++ validates each (indexes in range, known setting names and values only); the page validates every reply. Before `editorState` replies the page shows no tab content, so it doesn't flash the wrong tab.
- **Hosted plugin editors** open in separate always-on-top windows (`HostedEditorWindows`), one per slot; opening again brings it to the front.
- **Tabs:** Panel and Settings since 1b.5a (the temporary Plugins tab merged into Panel). Each instance reopens on the tab it was closed at (saved with the instance); new instances open on Panel.
- **Dropped:** the "Inspect the UI" button (Melatonin inspects JUCE components, and there are none left) and the "keep keyboard focus in the host" experiment.

### Scaling and resizing
- **The page follows the plugin's scale, not the monitor's.** The web view renders at the monitor's scale; the host decides the plugin window's (FL keeps plugins at 100% on a 200% monitor). C++ sends the page its scale in physical pixels per page pixel (`display`: host scale × our UI scale), and the page scales its root by that ÷ its own pixel ratio. Anything that measures pixels in the page (WebGL canvases later) must allow for that.
- **The whole UI scales; nothing rearranges.** The page is laid out at the design size (since 1b.5a: 790 × 1,270, portrait) and scaled to fill the window. Shift+drag changes the layout area instead ("Fixes after that check").
- **Only our own corner grips resize** (all four, drawn by the page; C++ follows the pointer). The host's window frame doesn't: the editor isn't marked resizable, because the wrapper's own resizing ignores our steps and window moves. Chords and rendering: "Resizing chords, smooth rendering, keys and windows".
- **Limits:** 50% to 500% of the design size (500% should fill a 16k screen once the full panel is built and larger by default; windows stranded off screen can be recovered by any visible corner or FL's "bring windows back").
- **Size memory:** saved per instance with the project, like the tab; new instances open at the last size used (`lastEditorScale`). "Reset size" returns to the design size.
- **Browser zoom is blocked** (Ctrl+wheel, Ctrl+plus/minus/0): it would fight the page's own scaling.

### Right-click menus
- **Our menu only,** with the host's options for that parameter in a "Host menu" submenu (greyed out when the host offers none). Our own items join it later. JUCE fetches the host's menu for the parameter's host control (VST3 `IComponentHandler3`); C++ turns it into items the page draws (`parameterMenu`), keeps each item's action, and runs it when the page reports a choice (`menuChoice`). Host-supplied text is shown through Svelte's escaping only; ids are checked against the current menu.
- **FL's section items** ("-Remote control" and the like) become non-clickable headings, with a divider before each.
- **No option to use the host's native menu:** it can't be combined with our own items, and FL's was badly positioned.
- **Each submenu is its own panel** kept inside the page: it opens beside its item, flips to the other side if it doesn't fit, and moves up if it would run off the bottom; a list taller than the page scrolls. Menus close on a click elsewhere, Escape, the window losing focus, or a resize.
- **The browser's own menu is always blocked;** right-clicking shows only our own menus (parameters; settings since 2026-10-08).
- **Risk:** if a host offers VST3 context menus but returns none for a parameter, JUCE dereferences a null menu (crash). Not guardable without changing JUCE.
- **Browser shortcuts blocked:** print, find, reload, save, view source, history navigation. Dev tools are off in every non-Debug build (JUCE).

### Keys
- **Cause (key log):** FL is a Delphi (VCL) program. VCL doesn't handle a key message where it arrives: its message loop reflects it to the window that should handle it as `CN_KEYDOWN`/`CN_KEYUP` (`0xBC00` + the message). While our window holds the mouse capture (a button held in it), that path is skipped, so FL's typing keyboard never sees the keys we post.
- **Fix:** in FL only, while a mouse button is held, the forwarder sends FL the CN message itself. Since 2026-10-05 that's route B, up the window chain ("Resizing chords, smooth rendering, keys and windows"). Other hosts and other cases are unchanged.
- **Releases always pass:** releases of forwarded keys pass even when the page never saw them go down (a note held since before the page had focus, FL default).
- **Drags no longer end on blur** (focus moving away mid-drag would otherwise split one drag into many gestures); a new press ends any drag whose release never arrived.

### FL's own plugins can't be hosted
- It's the format, not their location: FL's own plugins (e.g. `Plugins\Fruity\Generators\FLEX\FLEX_x64.dll`) use FL's private plugin format, which only FL can host; Image-Line's kit is for writing plugins for FL, not hosting them. "FL Studio VSTi" (FL running as a VST2) can't be hosted either: JUCE hosts VST2 only with Steinberg's VST2 SDK, which is no longer licensed. Hence external module bays (1b.4).

### FL check (2026-09-27): passed
- Keys while the mouse is held (no stuck notes, also when the mouse is released first); scaling at 200% Windows scaling; the grips, attached windows growing down and right; size memory and Reset size; tabs; "Open window"; the host menu with headings and dividers, staying inside the window, its items working (e.g. "Create automation clip").

## Module bays and relay nodes (2026-10-01)
The design and FL's measured behaviour. Status, next step and test builds: `docs/current.md`.

### Decided
- **FL forbids feedback loops** of any kind, so our plugin can't send audio through an FL plugin and get it back over Patcher wires. Both sides of a bay are wireless instead: relay nodes before and after the FL plugin talk to our plugin directly.
- **FL findings (user):** MIDI CC out doesn't drive FL plugins in the user's Patcher setup. Audio into a Peak Controller does, linked to any knob: 0.1% precision, smoothing can be turned off.
- **No parameter outputs (1b.3 FL check, 2026-10-01).** An ordinary parameter moved by our plugin on the audio thread ("Test output") is offered in Patcher only as an input; a meter-category one ("Test meter", read-only in VST3) appears nowhere. Inside Patcher a plugin's parameters don't show in FL's browser; outside it, "Test output" did. A brief recording test didn't capture its moves. So values reach FL's plugins only as audio into Peak Controllers.
- **One relay node type** with two modes, *receiver* and *transmitter* (named 2026-10-02; first called input and output mode): "heartseeker relay", a separate small VST3 *effect* (`source/relay/`, CMake target `HeartseekerRelay`). VST3 fixes inputs, outputs and parameters at load, so it declares everything both modes need, all active (FL leaves outputs declared inactive switched off); unused pins stay silent. Kept light, since an instrument package needs 2 relays per FL plugin: a small native JUCE window, no web view, no engine. **The mode is chosen in the relay's own window** (saved with the relay); what it carries (link, parameters) is configured from the panel's UI. Used inside Patcher and on mixer inserts (a useful utility there too).
  - *Receiver mode* (on the bay's input side, before the FL plugin): outputs the audio our plugin sends, plus one control output per controlled parameter, each into its own Peak Controller linked to one knob. Audio outputs add no delay of their own.
  - *Transmitter mode* (after it): takes the FL plugin's audio and passes it back.
- **The node demultiplexes.** Rejected: one value signal plus a selector signal into a switch script, because simultaneous changes (e.g. 10 parameters during a morph) would also need time multiplexing. The link itself carries exact values; audio encoding is only for the last step into FL.
- **Link hub in the relay's file** (changed 2026-10-01; was in the panel's). All relays are the same file, so they share memory with no library at all, and relay-to-relay use without a panel comes free later (a utility the user wants if the link proves acoustically robust). The panel is a client: on the message thread, about once a second, it asks Windows whether the relay's file is loaded in FL's process, looks up one plain, versioned function, and holds a reference so the file can't unload while connected. No loading from disk, no network, no new files; a version mismatch or a bridged plugin (separate process) shows "not connected".
  - **The hub is self-contained** (its own folder, no panel or relay dependencies; plain named channels any party can write to or read from), so moving it into a shared library later is packaging, not a redesign. It must stay small: it ships in every relay.
  - Rejected: a shared library now (a third file to install, protect and sign), OS shared memory (a local attack surface; only if FL's bridging forces it), a relay mode of the panel (not light).
- **Timing:** audio crosses the link through lock-free ring buffers with a fixed delay counted in samples (at least 2 × the largest block, since FL can vary block sizes). The panel reports the round trip to FL as latency and never waits for a relay. *(Refined when built: "Bay link" below. The round trip is 1 to 3 blocks, set by the panel; audio carries its position, so the delay can't drift.)*
  - **Patcher's order** (FL's docs): modules at the same depth after "From FL Studio" run at the same time, on several threads; deeper columns run after. The panel and a receiver (no input wires) share the first column, so the relay reads what the panel wrote a block earlier; the transmitter runs after the bay's plugin, after the panel, so the panel picks the return up a block later. Expect a steady 2 blocks plus the bay plugin's own latency.
- **Missing return blocks play silence, faded** (since "The next build (user, 2026-10-07)"; first a choice of silence or the sent audio). Never a repeated or stale block. Dry dropouts are a future refinement ("Future refinement: dry dropouts").
- **Linking:** one link name per panel for now (e.g. "Panel 1", shown in every window); a relay connects to the first panel it finds and reconnects if it loads first. Choosing names comes later.
- **FL gain correction is a panel setting** (Settings tab, plugin-wide, on by default, effective in FL Studio only): ×1.283291 on the panel's own control outputs. The panel has none yet; it's a hardwired safety for when it does. Relays need no correction (see below).

### What FL does (measured 2026-10-01)
- **Extra outputs work once assigned.** Until then FL sums every output into the main one (Patcher's first pin, or the channel's mixer track outside Patcher), and added Patcher pins stay silent.
  - Assigning: wrapper settings > Processing > Connections lists the outputs by name, each with an on/off switch and a target number (plus "Auto map outputs"). In Patcher, target *n* is the node's pin *n* + 1: pin 1 is the main output, so target 1 is the first added pin.
  - An output declared inactive shows switched off until the user switches it on.
  - Mono and stereo outputs give the same Peak Controller reading; steady levels (DC) pass through Patcher intact. Never route control outputs to speakers.
- **The Peak Controller is exact:** steady levels give exact tenths (relay → Peak Controller), linearly, near-instantly at maximum decay speed; its Ramp switch smooths jumps.
- **FL scales a generator's added outputs by 0.7792467 (−2.167 dB), not an effect's.**
  - Measured exactly: 24-bit Edison recordings (`C:\dev\auxiliary materials`) of the panel's 100% probe hold 6536795 in every sample, so 6536795 / 8388608 = 0.7792467 ±0.0000001. A relay reading the panel's probe agreed (0.7792467); one relay reading another's 100% level read exactly 1.0000000.
  - It can't be corrected inside FL: no plugin there sets a gain precisely enough, and controlled parameters leave no room for error.
- **Smart disable stops FL calling a plugin at all.** Wires keep animating, but nothing downstream gets values, and no audio leaves Patcher. A note wakes a generator.
  - **Inside Patcher only Patcher's own smart disable counts** (set from Patcher's wrapper menu; Patcher has no Processing tab). With it on, our plugin stopped whatever its own setting said; with it off, a silent relay with its own "Allow smart disable" on kept running. So a Patcher holding relays needs smart disable off.
  - **On a mixer insert the relay's own setting applies,** judged by its input: it stopped whether its outputs were silent or not. So input-mode relays on inserts need their own smart disable off.
  - Reporting an endless tail made no observable difference; it was tried and removed.
  - A relay that isn't being called counts as a missing block (silence, never a repeated stale buffer), and the user is told.
  - **"Audio: running / not running"** (last audio block more than 500 ms ago, checked 4 times a second) shows on the panel's Panel tab and in the relay's window; it caught smart disable in every test. To be restyled as a graphic element later.

## Bay link: the audio round-trip test (2026-10-02; FL-checked)

### Decided with the user
- **Live rig:** if FL's plugins aren't replaced by the time the live rig is built, it runs FL on the Mac with the panel as a plugin. Image-Line selling its flagship plugins as standalone VSTs again would be ideal; otherwise a slow migration off FL (and off any plugin tied to a DAW).
- **Latency is paid per bay in series:** one round trip (1 to 3 blocks) each. FL plugins in a row share one bay; bays on parallel branches cost one round trip between them. At 48 kHz, 1 block is 2.7 ms at 128 samples, 5.3 ms at 256, 10.7 ms at 512. Several serial bays at a large buffer aren't usable live.
- **Fallbacks if the numbers are bad:**
  - *Wired receiver* (user's idea): any wire into a receiver's input puts it in Patcher's second column, after the panel, so the order is guaranteed (1 block works). The panel could then send the bay's audio and the control values over that wire, leaving only the transmitter's jump back wireless. A generator's added outputs are scaled by FL (×0.7792467), so that path needs the gain correction.
  - *Zero-latency segments* (agent's idea): split the panel's chain at each bay into wired panel instances (part 1 → FL plugin → part 2), which isn't a loop. Live use only, and only if the parts reassemble into one interface: the product's point is centralised control.
- **The dry sound is delayed like the bay's sound:** the latency told to the host doesn't change when a slot goes dry, so the sound never moves in time. Only Off drops the bay's latency (slot states: "The next build (user, 2026-10-07)").
- **macOS finder: stub only.** The way: list the loaded images (`_dyld_image_count`, `_dyld_get_image_name`), take the relay's with `dlopen (path, RTLD_NOLOAD)` (a reference, never a load), then `dlsym`.

### The hub (`source/link/`)
- **Plain C interface** (`LinkApi.h`; version 1 here, 2 adds value sets, 3 latency reports): open or close a role on a named channel, writer details, write, read, info. `heartseeker_link_get_api (version)` returns null for any other version; callers also check `struct_size` and every function pointer.
- **Channels:** up to 64, named (printable ASCII, 1 to 63 characters), never removed, so no handle can point at freed memory. Each holds 65,536 stereo frames (1.4 s at 48 kHz; 512 KB, allocated when the name is first used). Roles: one writer, one reader, any number of observers (info only; observers never create a channel). Handles carry a claim number: closed or made-up handles are refused.
- **Audio carries its position** (a sample count on the panel's timeline). A read gets exactly the frames written at those positions, or a failure (`NOT_YET`, `GONE`) and silence: never stale or mixed audio. A writer skipping ahead leaves a gap that never arrives (only the latest gap is remembered); going back, new writer details (a new session) or a new writer start afresh, and the old run is unreadable from that moment.
- **Lock-free on audio threads:** write, read and info take no locks and allocate nothing; open, close and details use a mutex, on message threads only. Frames are relaxed atomics; a sequence counter (odd while the run changes shape) and an overwrite horizon let a reader detect anything that changed while it copied.
- **Untrusted input:** every argument is checked (names, roles, handles, positions, blocks of up to 16,384 frames, channel counts; the sample rate by its bits, since fast-math builds can't test for NaN).
- **The reader's last read** (position, length, complete or not) is published as one word; the transmitter stamps the return with it.

### Timing
- **The panel sets the round trip** (Settings: 1, 2 or 3 blocks, or Automatic, the default since the control outputs) and tells the host it, plus the engine's latency. It reads the return at its own position minus the round trip, so the delay is fixed by design: FL's processing order can only cause missing blocks, never drift.
- **The receiver** plays from `max (block, round trip − block)` behind the panel's newest audio, chosen when it links or the panel's session changes. After a miss it plays silence for that block (never a repeat) and lines up again where either processing order is safe. So 2 blocks work in any order, with at most one silent block inside the bay after linking (the panel's return isn't affected: the stamps heal it); 3 never miss. Its block is the panel's unit (since the 2026-10-08 bug fix).
  - **1 block** works only if the receiver runs after the panel (a wire into it). The receiver then plays exactly the block the panel has just written (its lead is the current block's size, since FL's blocks vary), and skips ahead to it whenever it finds itself behind (e.g. a wire added after linking).
- **The transmitter** stamps the FL plugin's output with the position the receiver played in the same cycle (by the marker since C2), and never re-sends positions it already returned.
- **A panel's link timeline only moves forward,** past anything an earlier panel left in the channels. Its delay line keeps its own count, so the dry sound never jumps when the link does.
- **Missing blocks count only in a flowing return**; a stopped side (smart disable, a removed relay) shows in the status instead (refined: "FL check (2026-10-02) and fixes"). Relays count their own misses the same way.
- **Linking:** once prepared, the panel takes the first free "Panel n" (n up to 8). Relays scan Panel 1 to 8 four times a second and join the first panel whose role is free; a relay whose panel has gone drops the link and scans again. Channels: "Panel n/bay 1/send" and "Panel n/bay 1/return" (`BayNames.h`).
- **Finding the hub (Windows, `RelayHubFinder`):** `GetModuleHandleExW (0, L"heartseeker relay.vst3")` finds the file only if it's already loaded, and holds it; then `GetProcAddress` and the version checks. Held until the panel is destroyed, after its link has closed.

### Diagnostics and tests
- **Panel** (now the Diagnostics panel): link state, relays, round trip and the latency told to the host, missing blocks, headroom (returned audio to spare), block sizes, return vs sent (0 is identical), Lag > Measure, silenced samples (NaN or infinity), oversized blocks. **Relay window:** mode, link, round trip, missed blocks with the last reason, headroom, block sizes, running, input peak.
- **Tests** (`[link]`, `[relay]`, `[relayfile]`): the hub's rules and a two-thread stress test; a simulated Patcher (panel, receiver, FL plugin, transmitter) in every order with constant and varying blocks, exact sample for sample, never stale audio when pieces are added, removed or reloaded; the real built relay file hosted as FL hosts it.

### FL check (2026-10-02, 48 kHz, buffer length 288)
- **Passed:** the link alone (exact, lag 0); an FL plugin in the bay at 2 and 3 blocks, 0 missing blocks over minutes; adding, removing, reloading, saving and reopening, second relays and panels, smart disable, closing FL; FL's linear-phase EQ 2's latency measured exactly as FL reports it.
- **FL's blocks:** plugins are called with blocks of 1 to 215 samples whenever the buffer length is above 215 (at or below it, up to the buffer length), while FL announces the buffer length as the largest block. (Later: one sequencer tick, "FL check of the Diagnostics build".)
- **1 block failed at first:** the receiver lined up one *announced* block behind the panel, too far for FL's smaller, varying blocks. Fixed as in "Timing"; **re-check passed:** 1 block works with a wire into the receiver (also when added after linking) and fails without one.
- **The passive lag readout was confusing** (a held note repeats every waveform cycle, so many delays fit; the search stopped at +8,192): replaced by the active Lag > Measure ("FL check (2026-10-02) and fixes").
- **Pops:** switching between dry and the bay's sound popped faintly when the bay changes the sound; changing the round trip pops louder (the output jumps by a block), and 3 → 2 blocks misses 2 or 3 blocks while the receiver lines up again.
- **Latency changes** reach FL at once, playing or not. **Buffer length changes** (e.g. 512 → 2048) can miss up to 5 blocks, audibly; expected while the audio restarts.
- **Mixer inserts:** relays around an effect on a mixer insert return its audio to the panel inside Patcher, as long as they aren't downstream of that Patcher (FL silences what it sees as a loop). So relays can also act as sends that take audio out of a Patcher: a wanted utility. A bridged relay unlinks only itself.

## Bay control outputs and the automatic round trip (2026-10-02; FL-checked)

### Decided with the user
- **16 mono control outputs per relay** ("Control 1" to "Control 16"). If that's too few: more later, or several relays serving one bay.
- **The panel's bay controls are parameters like any exposed one:** sliders in the catalog (slot "Bay 1"), bound to Param 2 to 17, so FL can automate them and morphs can drive them later. Control *k* plays on the receiver's output *k*; choosing and naming them in the UI comes later. Their values are saved with the project.
- **Relays hold the last values** through missed blocks, without a link, and without a panel, and save them with their state, so knobs don't drop to 0 when a project opens before linking. Values are settings, not audio: the "never stale" rule doesn't apply to them. A transmitter's control outputs are silent.
- **Values cross the hub as value sets**, not as one channel per parameter (64 channels at 512 KB each would run out) and not as extra audio lanes (about 4 MB per bay for values that only change per block). Both give the same signal on the outputs.
- **A timing probe** (below), so the Peak Controller's delay is measured rather than guessed.
- **Round trip default: Automatic** (below). The user's concern: 2 blocks per serial bay at large buffers may make complex patches unplayable live.
- **A bay plugin's own latency (L) must be handled automatically,** before slots can be moved: no manual values to re-enter after every change. It also belongs in the latency told to FL (FL can't compensate a plugin hidden behind the link) and, later, in the graph's compensation of parallel branches. Solved by the position marker (C1, C2).

### Values in the hub (version 2)
- **Value sets:** up to 64 values stamped with the position from which they apply, until a later set. A channel keeps the last 256 (about 72 KB, allocated with the channel). A read gets the set in effect at a position: the newest written at or before it; NOT_YET before the first, GONE once overwritten. A read that fails changes nothing.
- Sets outlast sessions (new writer details), since the panel's timeline only moves forward; a new writer, or a set before the newest, starts them afresh (an epoch number). Each set has its own sequence counter, so a read that overlaps a write is dropped and retried. Values must be finite. Readers and observers may read values (several relays could serve one bay later).
- `hs_link_info` also counts each role's claims, so the panel sees when a relay relinks. The panel refuses an older hub ("a different version").

### Timing and resolution
- The panel writes a set when a value changes (and at the start of each link), stamped with that block's position. The receiver plays, for each block, the set in effect at the block's last sample: one steady level per block (what the Peak Controller reads exactly), in step with the audio, up to one block early and never late.
- Resolution: one value per panel block. If finer steps help, a set could carry a ramp later. Hosted plugins get parameter changes at block starts too (JUCE's VST3 hosting); finer automation there means splitting blocks in the engine, a later option.

### Automatic round trip
- **Neither side can tell FL's processing order directly:** without a shared cycle count, the panel-first and receiver-first orders look the same from positions alone. What shows is whether 1 block closes. So Automatic tries 1 block whenever a relay links (a new relay, or one switching modes), and falls back to 2 at the first missing block once the return has flowed for a few blocks at that round trip. A second of returned audio without a miss settles it on 1 block. The choice holds until the relays link again.
- A wire from the panel into the receiver puts the receiver in Patcher's next column, so 1 block works whatever the panel's own graph looks like. Each serial bay costs at least 1 block with one panel instance; parallel bays share it.
- **To try 1 block again** (e.g. after adding the wire): "Retry 1 block" (a button and an automatable command, 2026-10-08), or switch the receiver's mode to Transmitter and back. A latency change can pop once, at link time.

### Probe
- "Control timing > Probe" (Diagnostics): the panel sends a steady tone (fs/4, every sample the same size, level 0.25) instead of its sound, mutes its output, holds control 1 at 0 for 0.5 s, steps it to 1, and compares the return's level per sample: 0.25 s before the step against the last half of the second after it. It reports the delay (step to halfway) and the rise (10% to 90%) in samples, ms and blocks. Control 1 must drive a level knob in the bay (e.g. through a Peak Controller to Fruity Balance's volume). Afterwards control 1 returns to its value. The output fades out over 20 ms before the probe and back in after (it popped without).

### FL check (2026-10-02) and fixes
- **Passed:** exact values (to FL's 0.1%), several controls at once, automation on Param 2, holding without a panel and through save and reopen, silent control outputs in transmitter mode.
- **Probe (Peak Controller):** at every buffer length tried (64 to 2,048), the same numbers, so the delay is the Peak Controller's own, not FL's buffer:
  - Ramp off: 300 to 550 samples from the step to halfway (mostly near 310 or 510), 10–90% in 166 to 173.
  - Ramp on: 1,550 to 1,750 (mostly high 1,500s), 10–90% in 1,020 to 1,130.
  - The two clusters about 200 samples apart suggest it updates its knob once per FL internal block (at most 215). A guess, not measured.
  - **Control lead: not by default.** Values can't reach the receiver before the panel computes them, so cancelling about 400 samples (8 ms at 48 kHz) means delaying the bay's audio by the same amount: more latency per bay. A possible setting later.
- **Automatic round trip didn't work at first. Cause:** the panel counted a missing return block only if the transmitter had written in the last 2 blocks, after 5 blocks of warm-up. With FL's real blocks the panel's own return had room to spare at 1 block, so it only missed where the receiver had missed; but each receiver miss made the transmitter skip blocks, which reset the warm-up, so those misses were never counted and Automatic never fell back. **Fixed:**
  - "Stopped" is now measured in time (no new return for 0.1 s), and the warm-up in samples (the round trip plus 2 announced blocks), so a stop-and-start return still counts its gaps.
  - The hub counts the reader's failed reads; the panel shows them ("Receiver missed") and Automatic counts them as misses too.
  - Automatic keeps watching while at 1 block: any miss on either side falls back to 2 (e.g. the wire removed later). It ignores the first 0.25 s after a round trip or block size change, while the relays line up again.
- **Crossfade lengths** (dry ↔ the bay's sound): 20 ms clean, 10 ms a slight pop, none a bad one, 50 ms audible as a crossfade on overt effects. Now one fade per cause ("One fade per cause, one check per stop").
- **Pops left (polish, not now):** removing or adding relays, cutting the wire into the receiver or the receiver's output, and the first audio from a new transmitter instance can pop. A dropout can only start abruptly without a block of lookahead, and some come from FL's own rewiring (industry plugins pop there too). Removing them all would beat the norm; a later polish item.
- **The passive lag finder was unsuited to measuring a bay plugin's latency** (it needed audio playing, read held or tonal sound as "unclear", and couldn't separate the merge experiment's two paths). **Replaced by an active Lag > Measure** (user's go, same day): it fades the output out, sends a known noise burst (8,192 samples, level 0.25, the same every time) instead of the panel's sound, records the return from 1,024 samples before the burst to 32,768 after its end, then fades back in; nothing needs to be playing. It correlates the capture with the burst (FFT, `juce::dsp`) and reports every delay at which the burst comes back with at least 20% of the strongest path's strength (at most 4, strongest first; ringing within 16 samples of a path isn't a path), each with its gain (1: as loud as sent); an unchanged copy is flagged "exact", below a gain of 0.1 "didn't come back recognisably". Noise doesn't repeat, so nothing is "unclear". The probe and the measurement share the machinery (one at a time). The passive "return vs sent" readout stays, paused while a test signal is on its way.

## FL re-check, merge experiment, Diagnostics panel (2026-10-03/04)

### FL re-check (2026-10-03)
- **Passed:** the automatic round trip (with and without the wire into the receiver, the fallback, trying 1 block again, no wire from the start), the probe's fades, the crossfade field, the tabs' scroll positions.
- **Lag > Measure:** a pass-through bay 0 (an exact copy); EQ 2 at its neutral defaults +3 samples (gain 0.60; FL reports no latency for it, so that's its phase response smearing the burst, not a delay); linear-phase EQ 2 at its highest +12,288 (gain 1.00).

### The merge experiment: Patcher lines up paths that merge
The receiver was wired from the panel (the panel reports its 512-sample round trip as latency), buffer 512.
- **a)** Receiver → transmitter, and receiver → EQ 2 (12,288) → transmitter: one path at +12,288, gain 2.00 (the direct wire delayed to match; two identical copies add up).
- **b)** Receiver → transmitter, and EQ 2 (no input) → transmitter: +11,776 (exact). **c)** Maximus at 12,480, no input: +11,968. **d)** Maximus at 576, no input: +64. Each is the plugin's latency minus 512: Patcher counts the panel's reported latency upstream of the receiver, and a plugin with no input starts at 0.
- **e)** Receiver → EQ 2 (12,288) and → Maximus (12,480), both → a neutral EQ 2 → transmitter: FL shows 12,992 at the last EQ and the transmitter (512 for the panel + 12,480 for the longer branch); measured one path at +12,483 (12,480 + the neutral EQ 2's 3), gain 1.16.
- **So the position marker works:** both its paths start at the receiver, so a marker wired straight into the transmitter arrives exactly the bay plugin's latency late.

### Regression stages 1 and 4 (2026-10-03)
- Headroom 297 at 1 block (= 512 − 215, FL's real blocks). The known pops when a wire inside the bay is cut or reconnected, a relay switches modes, or the round trip changes. A lowest headroom of −4,416 during dropouts was an artefact (measured before a stalled return counted as stopped); fixed in A.
- **A cut wire from the receiver to the effect gives silence,** as designed: the return arrives on time, just silent, and the link can't tell this from an effect that's silent on purpose. Hence the planned "return silent while the sent audio isn't" line.
- **A cut wire from the effect to the transmitter:** with no input wire the transmitter moves to Patcher's first column, beside the receiver in no fixed order; when it runs first it can't return the receiver's block, so most return blocks go missing. The receiver also counted about twice the panel's "Receiver missed" (running first, it alternates a failed read with an uncounted skip ahead). Both addressed in A.

### Diagnostics panel (built 2026-10-04; FL-checked)
- **Two columns per tab:** the left column fills to the window's height, then the right one (CSS columns, filled in order; nothing is split between columns). Content longer than both columns is split evenly between them and scrolls (`columnFlow` in `ui/src/lib/layout.ts`).
- **Diagnostics:** a toggle at the right end of the tab row. Open, the Diagnostics panel (the bay link section, later the event log and the key and window diagnostics) shows beside the current tab; both sides scroll independently and stay usable. Since 1b.5a it widens the window by 400 px.
- **Scroll memory:** one position per tab per layout and one for Diagnostics, kept per instance and **saved with the project** (user: reopening a project restores the UI exactly as it was left). Always on, not tied to "Keep web UI warm". The page sends a position 250 ms after scrolling stops; on reopen it puts each back, waiting for late content unless the user scrolls first. No CSS scroll anchoring in the scrolled areas (it shifted restored positions). Whether Diagnostics is open is saved too.
- **Mute per generator:** a mute stage after each generator in the engine's graph fades its output out or in over 30 ms (10 ms still popped on bass-heavy material, user); the generator keeps running, so load and timing don't change. Saved with the project.
- **Page messages,** each checked in C++: `diagnostics {open}` (a boolean), `scroll {area, top}` (a known area name, a whole number from 0 to 1,000,000), `mutePlugin {slot, muted}` (the engine refuses anything but a generator).

### FL check of the Diagnostics build (2026-10-04)
- **Passed:** two columns, Diagnostics beside the tabs, scroll memory (switching, reopening, reloading the project), mute.
- **Keys while a mouse button is held failed again,** with our key code unchanged since it passed, after a WebView2 runtime update (154.0.4258.53) and Windows' KB5121794 (both 2026-10-03). Fixed 2026-10-05 ("Resizing chords, smooth rendering, keys and windows").
- **FL's real block size follows the tempo:** the largest block was 215 samples at 140 BPM (FL's default), 167 at 180 and 435 at 70 (48 kHz): one sequencer tick at 96 ticks per beat (214.3, 166.7, 428.6; the 70 BPM figure was 6 over, unexplained). Confirmed exactly in the C1 check (30,000 / BPM samples at 48 kHz). The round trip in real blocks must follow what FL delivers, not a constant. Diagnostics' block sizes are the smallest and largest since Reset counters (the label says so).
- **Fruity Wrapper's fixed buffer sizes: not now.** The link assumes each plugin is called once per FL cycle; with fixed buffers a wrapper calls its plugin 0, 1 or several times per cycle, so 1 block would miss, and their own latency would shift the relays' audio against its stamps. The position marker travels through the same wrappers as the audio, so fixed buffers are now worth a measured test.

### Resizing chords, smooth rendering, keys and windows (2026-10-04/05; FL-checked 2026-10-05)
- **Corner grips:** drag keeps the opposite corner in place; Alt+drag resizes around the window's center. "Window resize: flip drag and alt+drag" (`resizeFlipAlt`, off by default) swaps them. Pressing or releasing Alt mid-drag carries on from the window's current size and place, so nothing jumps. The page sends a move when Alt, Ctrl or Shift changes, and swallows a lone Alt during a drag so it can't open the host's menu bar.
- **Ctrl+drag steps by 25% of the design size** (50% to 500%). When Ctrl goes down the current size becomes a virtual step; the size then snaps to whichever is nearest, a 25% step or the virtual step. Releasing Ctrl or the mouse clears it. Combines with Alt. Kept as built (user, 2026-10-05). Shift+drag: "Fixes after that check".
- **Rendering setting** (`resizeRendering`):
  - *Smooth* (default for now): the page is laid out once at the design size and scaled like a picture (a CSS transform on the page's root), so everything keeps its exact proportions while resizing; edges soften between sizes.
  - *Sharp:* CSS zoom, laid out again at every size: crisp, but elements can shift by a pixel against each other.
  - **Smooth text** (user's goal: soft, unsnapped everything, text included). Under a transform Chromium snaps each text line's baseline to whole device pixels while boxes move proportionally; `text-rendering: geometricPrecision`, a tiny rotation and a 3D transform changed nothing. **`will-change: transform` on the scaled root fixes it:** the page is drawn once into a layer the browser stretches (largest text step 0.02 px instead of 0.6 to 0.85). The layer keeps its resolution, so 250 ms after the last size change it's dropped for one frame and redrawn (sharp at rest). It also turns off LCD text antialiasing. In FL: roughly as intended; to be tuned once the real fonts are in.
  - *Measuring in the browser preview:* its viewport stays fixed while the scale changes, so measure a probe element at a fixed position, and take full screenshots (a clipped one can redraw the page).
  - Later: WebGL canvases in Smooth mode will need their drawing resolution matched to the scale.
- **Keys while the mouse is held (route B):** the key log showed our thread does see the capture (the web view's `Chrome_WidgetWin_1`), but FL's plugin panel now refused the control route (`SendMessage` of `CN_KEYDOWN` returned 0) and ignored the posted key. Background: with a capture window FL didn't create, Delphi's message loop skips its own key handling (`IsKeyMsg` only sends control notifications to a capture window from FL's own module). A test build offered four routes; **B and D both played. Chosen: B:** `CN_KEYDOWN`/`CN_KEYUP` sent up the window chain (parent or owner) to the first window that takes it, else posted; used whenever a mouse button is physically down (`GetAsyncKeyState`, so swapped buttons count). It follows VCL's own dispatch, so FL's windows decide what the key does. **D stays the known-good alternative** (plain `WM_KEYDOWN`/`WM_KEYUP` sent at once to `GetAncestor (panel, GA_ROOTOWNER)`); both are in commit `4e2e1ba`. Never synthesize OS-level input (`SendInput`): it would go to the focused web view and loop back into the forwarder. The key log stays in Diagnostics (it names the window that took each key).
- **FL's window chain** (Diagnostics' window list):
  - Outside Patcher, detached: `JUCE_…` (the editor) → `TVST3Panel` → `TWrapperSynthPanel` → `TVectorSheet` (at 5, 26: below FL's title bar) → `TPluginForm` (top-level, owned).
  - Outside Patcher, attached: the same up to `TPluginForm`, which is then a **child** of `TWPControl` (FL's workspace, 3471 × 2046 at 369, 82 in the main window) inside `TFruityLoopsMainForm` (top-level).
  - Inside Patcher (attached or detached Patcher alike; Patcher's plugins can't be attached on their own): the same chain ending in `TWrapperPluginForm` (top-level, owned).
  - Moving the editor's window moves `TPluginForm`; resizing changes every window in the list except `TWPControl` and `TFruityLoopsMainForm` (user).
- **Attached windows (FL only):** the editor's ancestor `TPluginForm`, when it's a child window, moves within its parent (FL's workspace), always kept inside the workspace's client area (a window bigger than it keeps its top-left corner inside). So drag, Alt+drag and the flip setting work for attached windows too. Top-level windows (detached, Patcher) float as before; with no `TPluginForm` found, nothing moves.
- **Placement settings (user):** "Center a separate window when it first opens on" (`editorPlacement`) and **"Center a window attached inside FL Studio when it first opens"** (`attachedPlacement`): off / centered in FL's workspace (**default**) / centered on the monitor where FL is open (the monitor showing most of the workspace; the part of the workspace on it, then kept inside the workspace). Instances already placed (the per-instance flag) aren't centered again.
- **Double-click resets a parameter to its default** (user request): catalog entries carry each parameter's normalised default (`d`), and a double-click sets it as one gesture, so the host records it like any change. The browser's range slider jumps to a click on the track first (gone with the real knob, 1c).
- **Double-click settings** ("Double-click on a control"): speed 1 to 2,000 ms, default 500 (Windows' default; `doubleClickMs`), detected by the page itself (two clicks within that time and 6 page pixels), not by the browser's `dblclick`, which follows the system setting; "Double-click sets a value instead of resetting" (`doubleClickSets`, off), a placeholder for a text-entry popup.

## The rest of 1b.4: plan, and the position marker (2026-10-06)

### Plan (user, 2026-10-06)
- **Order:** **C** (a bay plugin's own latency, via the position marker: **C1** relay only, **C2** the panel uses it), **A** (the regression fixes), **B** (real blocks), each with its FL check. All done by 2026-10-08.
- **Why the marker (C):** a bay plugin's own latency L (linear-phase EQ 2: 12,288 samples; Maximus at its defaults: 576) is hidden from FL behind the link, and unknown to the panel. Without it: the processed sound is L late against the rest of the project (and the latency told to FL leaves it out); the dry sound, dropouts and crossfades use the sent audio L away from the return (time jumps, stutters, a doubled or phasey blend); later, the graph can't line up parallel branches. FL doesn't tell a plugin its upstream latency, Lag > Measure mutes the output, and comparing sent and returned audio fails on tonal sound and on effects that change it. The marker gives L exactly, every block, silently, following changes, for one wire per bay (no re-measuring when slots or plugins change).
- **Why C1 first:** its readout answers the open FL questions (does Patcher line up a node's *second input pin* with its first; does the marker arrive exactly) before the panel's half is built on them. In Patcher a "sidechain" is just another input pin; the merge experiment only showed paths lining up on *one* pin.
- **A: regression fixes** ("Regression stages 1 and 4"; done except the last item, user 2026-10-08): headroom only from blocks where new return audio arrived; Automatic counts the receiver's misses even when the return isn't flowing; panel and relay count misses the same way, and the receiver's skip-ahead plays the newest block instead of a silent one; a **"return silent while the sent audio isn't" line** in Diagnostics (user: keep the idea for the real UI too, e.g. silence shown on a slot), deferred to the alpha slot interface. A also planned a hold after Safe dropouts, moot since dropouts became silence.
- **B** was planned here and built as "B: real blocks".
- **Dropped:** the transmitter's wire to "To FL Studio" (user: relays behave the same with or without it; only Patcher's own smart disable stops them).
- **FL facts (user, 2026-10-06):** the Fruity Wrapper lists extra *inputs* under Connections like extra outputs. The wrapper also has **"Use time offset"** ("adjust time information reported to a plugin"); not documented further. Agent's reading, unverified: it shifts the song position a plugin is told, likely by latency compensation. A thread for later: the bay's audio reaches its FL plugin later than FL's song position (by the receiver's lead), so tempo-synced effects in a bay (Gross Beat, synced LFOs and delays) may act off the grid; this option may be the way to line them up.

### C1: the position marker, relay side (built 2026-10-06; FL-checked 2026-10-07)
- **Pins:** a stereo **"Marker" input** (the relay's second input pin) and a stereo **"Marker" output** after Control 16 (so the controls' target numbers don't move). Both start active. Extra pins may be mono, stereo or off; the marker needs stereo.
- **Receiver:** plays the position of every sample it plays on the Marker output; silent when it plays nothing (a miss, no link).
- **Encoding:** left: the position modulo 2^22 (87 s at 48 kHz) × 2^-40; right: its complement ((2^22 − 1 − position) × 2^-40). Every value is exact in a float and below 2^-18 (about −108 dB), so a marker that ends up in audio stays inaudible (made quiet after the FL check, user; first 2^-24, up to 0.25). Control outputs keep their levels (the levels are the values). A block reads as a marker only if every sample is an exact marker value, its complement matches, and the positions run on without a gap; compared by bits (fast-math builds), so negatives, NaN and infinity are turned away. Any gain, mixing or damage makes it unreadable, never a wrong position.
- **Transmitter:** reads its Marker input every block (also without a link): off, silent, unreadable (counted) or reading. While linked, with the receiver having played this cycle, **bay latency = where the receiver's latest read ended − where the marker's block ends** (modulo 2^22). The marker's wire from the receiver also guarantees that the receiver runs before the transmitter.
- **Readout (relay window):** the receiver's Marker output state; the transmitter's bay latency (samples, ms), its range since reset and unreadable blocks, or why there's none.
- **"Panels in the hub" section** (relay window, a diagnostic for link problems): every panel name the hub knows, each with its panel, receiver and transmitter as none / stopped / running (running: its count moved since the last look, 4 times a second), and a * on this relay's panel. A removed panel that still holds its name would show as "panel stopped" instead of "none".
- **Tests:** the encoding (round trips, the wrap, every kind of damage refused); a simulated Patcher with bay plugins of 0 to 12,480 samples in every order reads exactly that latency.

### FL check of C1 (2026-10-06/07, 48 kHz, ASIO4ALL, buffer 512, all inside Patcher)
- **Passed: Patcher lines up a node's second input pin like its first.** Linear-phase EQ 2 at its highest: 12,288, range 12,288, 0 unreadable. Maximus: 192 (FL: 192 for the plugin, 704 in total there: the panel's reported 512 + 192). EQ 2 at its neutral defaults: 0 (FL reports none). The pins show in Connections; a damaged marker (Fruity Balance) reads unreadable and never a latency. Buffer lengths and tempo changes: same latency, 0 unreadable.
- **Targets (user):** extra pins must be assigned in Connections (by hand or "Auto map"), and target numbers only go up to the number of pins added to the node in Patcher. The Marker output can simply take target 1, so only one pin needs adding for it.
- **FL shows each node's total latency** (704 at the transmitter; 512 at "To FL Studio", or 704 once the transmitter is wired there too). A possible fallback for finding L, but more wiring; only if the marker fails (user).
- **Changing the EQ's latency while playing:** the reading follows, but the range picks up huge one-off readings (usually 25,000 to 28,000, once nearly 2 million) until Reset counters; 1 unreadable block each time linear phase is switched on at its 384 and 768 settings, not at higher ones. Agent's reading: real readings while FL rearranges its compensation. **So C2 must not trust a new reading at once** (the settle rule).
- **FL's real blocks are exactly one tick at 96 per beat:** at 48 kHz, 500 at 60 BPM, 375 at 80, 250 at 120 (= 30,000 / BPM), also while the transport is stopped. B's lowest-tempo floor can be computed exactly.
- **Link problems seen, not reproduced:** a re-added panel didn't relink to existing relays; after the PC had slept, a second panel's link carried the first receiver's marker as audio with no wires to it, and a third relay as receiver wouldn't link (pins assigned; FL had just been updated). In a fresh project the next day everything linked as designed. Open: an optional sleep and wake test.

### C2: the panel uses the bay latency (decided with the user and built 2026-10-07; FL-checked)
- **Hub version 3:** one new writer call, `set_latency (handle, state, samples)` (lock-free, any thread), and `hs_link_info` gains the state, the samples and a report count. States: unknown (no marker arriving: stamped by the receiver's position, L counts as 0), measured, unreadable, out of range (below 0 or above the cap), no input (the Marker pin is off), and held (added in "Fixes after that check"). The hub checks every field and resets the state when a writer claims or closes. Not value sets: the stamps already put the audio at the right positions, so the panel only needs one current number.
- **The transmitter stamps by the marker** (the position of the audio its FL plugin is giving back, rebuilt in full from the receiver's last read), so the stamp stays right through a receiver lining up again. It returns a block only if the reading is in range *and* matches the previous block's reading, so a one-off stale reading never stamps audio (a real change costs one block). Positions already returned are never sent again. Unreadable or out of range (above the cap too: held back, user): not returned.
  - **A marker that goes silent after it was read is held** (those blocks aren't returned: a receiver's dropout arriving L late, or FL refilling the wire), then the transmitter falls back to the receiver's position (L = 0). First 0.1 s; up to the latency cap since "Fixes after that check".
- **The settle rule runs in the panel** (relays stay light): each new report that differs from the value in use starts a settle count; a different value or an invalid reading starts it again. **Settle time: a setting, 0 to 1,000 ms** (`bayLatencySettleMs`; default 50 at first, 0 since 1b.5a). While settling, the return is held back; those blocks count neither as missing nor for Automatic. The new value is adopted on the message thread with the latency report, as round trip changes are; that timer runs 10 times a second. Tempo changes don't change L, so tempo automation never triggers it.
- **Audio stamped by the receiver isn't trusted once an L above 0 is in use:** the panel holds back return positions written before the transmitter switched to the marker, so a newly wired marker never plays mislabeled audio.
- **Cap: 32,768 samples** (`HS_LINK_MAX_LATENCY`; the hub keeps 65,536 frames). The history holds the largest round trip + the cap + a block (allocated in `prepare`).
- **The panel** reads the return, and the delayed dry sound, at its position minus (round trip + L), and reports engine + round trip + L to FL. Any change of the output's delay blends the old and new timelines (now over its cause's fade); a stutter or skip of the difference remains.
- **Two bypasses per bay** ("synced" and "drop latency", with a plugin-wide choice of what a dropped bay is sent) were built here and replaced the same day by the four slot states ("The next build (user, 2026-10-07)"). Hosted plugins get the same states with slots; there, dropping takes the plugin out of the engine's path, and only lowers the total if it's on the slowest branch.
- **Diagnostics:** Bay latency (in use; the transmitter's reading; settling to what, for how long; readings since reset), Output delay (and what the host was told), Held back. The relay window's Marker line says when the return is stamped by the receiver's position or held back.

### FL check of C2 (2026-10-07, 48 kHz, ASIO4ALL, buffer 512)
- **Passed:** pass-through bay (0, exact); linear-phase EQ 2 at its highest (bay latency 12,288, output delay and host told 12,800 at 1 block, **Lag 0**); in time with the rest of the project whatever the linear phase setting; no huge readings used; Maximus 192; marker unwired, rewired and damaged; settle times 0 and 500; drop latency (one blended jump forward, host told 0, FL re-shifts the project seamlessly).
- **Failed: missing blocks and Automatic falling back on latency changes,** with nothing audible: explained by the event log in the next build (the marker hold was too short).
- **FL's totals double-count the bay latency** (panel 704 = 512 + Maximus 192; the receiver, wired from the panel, shows 704; Maximus 896; EQ 2: 25,088 at the transmitter). FL adds the panel's reported delay down the wire into the receiver. Bookkeeping for a path that never reaches the mix (the audio is in time); **our own figures are the ones to trust.**
- **"Safe" isn't safe** (user): the sent audio can be much louder than the bay's output (a bay taming a hot signal), and a dropout plays it. Hence silence-only dropouts.
- **Tails:** a reverb in a dropped bay fed the panel's sound keeps its tail; silence should also cut tails. Hence the tail cut.

### The next build (user, 2026-10-07)
Decided, confirmed and built the same day; FL-checked, then refined in "Fixes after that check".
- **Four slot states (the bay for now), saved with the project** (`BayLink::SlotState`, `baySlotState`): **Active** (the bay's sound), **Mute** (dry, synced; the bay keeps getting the panel's sound), **Bypass** (dry, synced; the bay gets silence and its tails are cut), **Off** (dry, latency dropped; the bay gets silence and its tails are cut; switching in or out jumps by the bay's whole delay, blended, and FL re-shifts the project). Three controls, Mute, Bypass and Off, each a toggle that acts like a radio button: selecting one sets that state, deselecting it goes back to Active. **Bypass and Mute stay plainly dry** (no gain matching). Replaces the two bypasses and the "what a dropped bay is sent" setting.
- **Mute confirmed** (user): it only stops the slot's new processing being heard; the bay keeps getting the panel's sound, stays synced, and its tails carry on. It does not make the output silent.
- **Cutting tails: a control output.** The receiver's mono **"Tail cut" output**, after the Marker output, plays value 17 of every value set: 1 while the slot is Bypass or Off, 0 otherwise, for the user to link (Peak Controller) to whatever clears that plugin (its bypass, a clear button, decay). Held like the controls (through misses and without a panel, saved with the relay); a transmitter's is silent. Waiting for quiet is out: a long tail would block the slot while it plays out.
- **Tail cut release** (`bayTailCutReleaseMs`, 0 to 1,000 ms): extra time after Bypass or Off before the bay's sound counts again, since a plugin cleared through a Peak Controller reacts some ms late (the probe measured 300 to 550 samples). Its meaning changed in "Fixes after that check"; default 8 since 1b.5a.
- **Dropouts are silence only** (user): never the sent audio. A missing or held-back block (a bay latency settling, audio stamped by the receiver's position, the transmitter's marker hold, out of range, unreadable) starts with **the last fade-out length of the output played, reversed, under a falling ramp** (it continues from the last sample without a jump and needs no lookahead), then silence; the output's level then fades back in. The ring holds twice the longest fade, so the fade never reads what it has overwritten. A return that can't play counts as silence in crossfades; a blend after a change of delay scales the old timeline by its level and takes a missing old return as silence. The Safe/Silence setting is gone.
- **Safe stays in the code, out of the UI** (`Dropouts::safe`): tests use it to check exactness (a pass-through bay must give exactly the sent audio), and it's where "Future refinement: dry dropouts" starts.
- **Leaving Mute, Bypass or Off is a transition, not a dropout** (confirmed with the user): the sent audio plays on until the bay's sound is back (after Bypass or Off, plus the tail cut release), then crossfades into it. If it never comes back (e.g. no transmitter), the sent audio carries on, **flagged on the slot**. **The same before a bay's first return** (a new panel, or a project opening before the relays link): Active plays the sent audio until the bay's sound first arrives, flagged, so a panel without relays still plays. (`awaiting` in `BayLink::process`: cleared by the first playable return block past the latest silent stretch plus the tail cut release.)
- **After any change of delay** (the round trip, the block size or the bay latency in use; `delayRun`), neither the missing count nor Automatic counts for 0.25 s.
- **Event log** (Diagnostics, "Events", newest first, 30 lines, time to the ms): prepares; round trip changes and why; Automatic's decisions; bay latency settling and adopted; the marker reading's state changes; slot state changes; waiting for the bay's sound; and, 10 times a second, new blocks missing by cause (late: not returned yet; gone: no longer there; no return), missed by the receiver, and held back. A return whose transmitter has gone isn't "flowing", so it shows in the status, not as missing blocks.
- **Relay window:** "Prepared by the host: N times" and (transmitters) "fallbacks to the receiver's position", both since Reset counters; the controls line shows the tail cut value.
- **Page:** Mute, Bypass and Off toggle buttons on a "Slot" line in Diagnostics, with a line saying what plays (orange while waiting for the bay's sound).

### FL check of the next build (2026-10-07, 48 kHz, ASIO4ALL, buffer 512)
Screenshots: `C:\dev\auxiliary materials\testing screenshots` (1 to 12).
- **Passed:** latency changes sound fine (mostly no click; a gap about as long as the latency, the EQ refilling its own delay line; sometimes a faint click); dropouts fade to silence without a click; a marker cut and relink; a new panel plays its own sound until the bay's arrives, flagged; Mute; Bypass's tail cut; Off; leaving a state with the bay gone; save and reopen.
- **What the event log showed** (the C2 check's failure):
  - **The transmitter's 0.1 s marker hold was too short.** When a bay plugin's latency grows, FL's compensation plays silence on the marker's wire for up to the new latency. Fallbacks to the receiver's position happened only at 6,336 samples and above (132 ms), never at 3,264 (68 ms). Each fallback made two delay jumps (192 → 0 → 12,480) and a run of blocks "stamped by the receiver's position".
  - **Blocks the transmitter held back counted as missing,** both during that hold and in the one unreadable marker block each change makes (FL changing the wire's delay). Automatic then fell back; also on cutting the marker wire.
  - **FL doesn't re-prepare the relays on a latency change** (their prepare count stayed 0).
- **A hidden pop on leaving Bypass or Off while audio plays,** heard only through a reverb in the bay (a new tail after the cut). Cause: the bay's send switched between silence and the panel's sound in one step, a click at the bay's input; the panel's output was still dry then, so only the reverb's response was heard. The plugin's own clear also pops if it switches while audio flows.
- **Automatic otherwise:** the user can live with setting 1 block by hand if it stays fragile.

### Fixes after that check (built 2026-10-07; FL-checked 2026-10-08)
- **Marker hold up to the latency cap** (`RelayLink::markerHoldSamples` = 32,768, plus a block): FL refilling the wire is held, not a fallback. A deliberately cut wire holds the return for about 0.7 s before falling back.
- **A new reading state, "held"** (`HS_LINK_LATENCY_HELD`, hub still version 3; user chose a separate state over reusing "unreadable"): the transmitter reports it while it holds.
- **The panel tells held-back blocks from missing ones:** while the transmitter's latest reading is unreadable, out of range or held, a missing return block counts as "held back by the transmitter", not as missing, and Automatic ignores it. Any change in the transmitter's reading (state or value) restarts the 0.25 s window.
- **The send fades** (over the state's fade):
  - Entering Bypass or Off, the send stays full for one fade length (the output's own crossfade to dry, so the output never dips), then fades out; the tail cut goes to 1 only on the first block that's silent.
  - Leaving, the tail cut goes to 0 at once, the send stays silent for the **tail cut release** (its new meaning: time for the bay's plugin to come back before audio reaches it), then fades in.
  - The panel plays the dry sound until the return has passed every faded or silent position, then crossfades. So the plugin is only ever switched on silence, and nothing steps at its input.
- **Shift+drag sets width and height apart** (`PluginEditor`, `EditorScale`): the editor's size is the scale times a layout area (shares of the design size per axis, 0.5 to 4). Plain drag changes the scale (the page scales as before, keeping the window's current aspect); Shift+drag changes the layout at the same scale (the page shows more or less). Shift toggles mid-drag like Alt; with Ctrl, plain drag snaps the scale and Shift+drag snaps each axis to 25% steps of the design size, each with its own virtual step. Reset size returns both to the design. Saved with the instance (`editorLayoutWidth`, `editorLayoutHeight`) and as the default for new instances.

### Future refinement: dry dropouts ("Safe")
Parked by the user (2026-10-07) to get to functional machinery first; to come back to.
- **What it was:** in a dropout (a return block missing), play the sent audio (dry) instead of silence, so only the bay is skipped and the music carries on. Fading back into the return is free; fading out of a dropout needs lookahead.
- **Why it isn't safe:** the sent audio can be far louder than the bay's output (a bay taming a hot signal), and a dropout plays it at full level.
- **Gain matching** (discussed): measure the bay's output level against the sent audio over about half a second (only while both are clearly above silence, so quiet passages and tails don't skew it), and play the dry sound at that ratio, capped at 1. **Limit:** it matches the average level only. A compressor or limiter in the bay lowers the peaks far more than the average, so matched dry audio brings the peaks back; truly safe would need a limiter on dropout audio too. Filters and distortion change the sound itself, which no level match reproduces.
- **Where it plays:** dropouts, and the held-back stretches (settling, the marker hold, a dropped bay's silent stretch).
- **Cost when set aside:** level measuring and its tests (about 150 lines), one setting; the fades are needed for silence anyway.

### FL recheck of the fixes (2026-10-08)
- **Passed:** latency changes (Automatic stays at 1 block), the marker wire cut, the hidden pop (Bypass and Off with a reverb in the bay), longer crossfades, Shift+drag.
- **Tail cut release:** 0 is fine; no pop from the Peak Controller. Kept as a setting (user agreed): it costs nothing, and a Peak Controller with its ramp on reacts in about 1,600 samples (33 ms at 48 kHz), longer than a 30 ms fade. (Default 8 since 1b.5a.)
- **The fades are enough** (user): they catch the pops and smooth the bay's sound coming in from 0, so even low-frequency content fades in and out cleanly.
- **Control values do save** (what looked like a regression was an FL feature the user wasn't using deliberately). Hosted plugins' states are saved too: each slot's plugin keeps its full state in the project, restored by slot, never into a different plugin.
- **Pops on latency changes come from the bay's plugin** (the EQ pops on an ordinary mixer insert too): "Future refinement: latency-change pop fade".
- **Windows' input language flyout while resizing:** releasing Shift while Alt is held (and no other modifier) shows Windows 11's input language flyout, likely with stacked resizing chords. **Fixed:** while a corner is dragged, whenever Shift is held together with Alt or Ctrl, the panel types one meaningless key (Windows' unassigned virtual key 0xE8, the trick AutoHotkey uses against menu activation); Windows only switches language if no other key came between the modifiers. Checked 100 times a second and once more when the drag ends. Setting: "While a window corner is dragged, stop Shift with Alt or Ctrl from switching Windows' input language" (`resizeBlocksLanguageSwitch`, on). Windows only; macOS has no such shortcut.

### B: real blocks (built and FL-checked 2026-10-08)
- **Why:** FL's real blocks are one sequencer tick at most (215 samples at 140 BPM, 48 kHz), never more than the buffer length, also while the transport is stopped (user). Counting the round trip in them instead of the announced buffer length cuts each bay's latency.
- **Setting "Bay round trip counted in":** *real blocks* (default, user: revert if needed) or *announced blocks*. Plugin-wide: `bayRealBlocks`. **"Lowest tempo"** (BPM, empty by default: `bayLowestTempo`, 0 is none, up to 999). The floor is only a starting size; real blocks still rule, so a wrong guess (e.g. another project PPQ) only grows or costs a little latency.
- **The unit** (`BayLink::blockUnit`, the block the round trip counts in; the message thread's 10-per-second check, `updateBlockUnit`):
  - Announced blocks: the announced block.
  - Real blocks, after a prepare or a recheck: the lowest tempo's tick (sample rate × 60 / (96 × BPM), plus 25%, at most the announced block), or the announced block with no floor. Blocks are watched for 0.1 s (a *survey*), then the unit is 125% of the largest block seen (never below the floor, at most the announced block). Only a survey can shrink it.
  - **Growing ahead of need:** a block above **90%** of the unit grows it to 125% of the largest seen. Not 80%: then each new largest block grew the unit again (0.8 × 1.25 = 1), so a slowly falling tempo would make many tiny jumps. A sudden jump past the unit misses blocks until the check sees it (at most 0.1 s). Tempo automation is unaffected; a falling tempo costs a few blended 1 to 2 ms jumps (gradual) or one jump and maybe one missed block (instant drop).
  - Raising the lowest tempo's floor applies at once. Switching to real blocks surveys afresh; switching to announced blocks goes straight to the announced block.
  - Each change logs "Round trip block: N samples (why)"; the round trip line says "N × unit; real blocks (announced M)". Automatic's check carries on at the new size.
- **The hub's writer details carry the unit** as the largest block, so receivers line up by it (since the bug fix below, only by it).
- **Known cost:** a change of the unit sets new writer details, which starts a new session. At 1 block the receiver carries on without a gap (it plays the newest block anyway). At 2 or 3 blocks it lines up again, so the bay gets a gap of about one block, like any round trip change. This happens at startup (the survey) and when blocks grow. If it's audible through reverbs at 2 or 3 blocks, the fix is a hub call that updates the details without a new session (hub version 4).
- **At 512 and 140 BPM (48 kHz):** FL's largest block is 215, so 1 block is 269 samples (5.6 ms) instead of 512 (10.7 ms).

### Additions to B (user, 2026-10-08; built and FL-checked the same day)
- **The limits of smoothing a round trip change** (user):
  1. *The time shift* (growing repeats a few ms, shrinking skips a few ms): blended over its own fade ("Round trip change", below).
  2. *The gap inside the bay at 2 or 3 blocks:* **an orange warning** under "counted in" when real blocks are on, the lowest tempo is empty, and the round trip in use is more than 1 block (Automatic's fallback included): tempo drops can pop inside the bay; set a lowest tempo.
  3. *A sudden tempo drop missing blocks before the unit grows:* parked ("Future refinement: virtual lookahead automation").
- **The transport:** the processor passes the host's playhead state ("is playing", and the tempo) to the bay every block. Not reported counts as playing. Diagnostics shows "Transport: playing / stopped / not reported", the host's tempo, and "output quiet".
- **Shrinking while stopped:** "Real blocks: shrink the round trip while the transport is stopped" (`bayStoppedShrink`): Never, Only when the panel's output is quiet too (default), Always (user: a failsafe; a player will also have live BPM controls). "Quiet": the output's peak below a level for a time, both settings for this testing phase: `bayQuietLevelDb` (−120 to 0, default −80) and `bayQuietMs` (0 to 5,000, default 500). A survey started while stopped doesn't shrink if playing has started (or the output got loud) by the time it ends; it may still grow. How often it checks: "One fade per cause, one check per stop".
- **A new lowest tempo rechecks** (a survey), so the round trip can shrink at once, while playing too.
- **Buttons:** **"Retry 1 block"** on the Automatic line (enabled only in Automatic; it doesn't recheck the block size: concerns kept apart, user) and **"Recheck real block size"** (enabled with real blocks). **Reset counters only resets the counters** (user).
- **Automatable commands** (`TriggerParameter`: on/off, acts each time it's switched on, an automation pulse or a controller button; the message thread's 10-per-second check takes the request). After the Param 1–256 pool, with permanent IDs (JUCE hashes parameter IDs, so the pool's IDs don't move):
  - `baysRecheckBlockSize`, "Bays: recheck block size": one for all bays, since the block size is the host's, shared by every bay in a panel.
  - `bay1RetryOneBlock` to `bay4RetryOneBlock`, "Bay N: retry 1-block round trip": one per bay, since each bay's relays can run in their own order. Planned for 4 bays (user); bays 2 to 4 do nothing until there are more bays.

### One fade per cause, one check per stop (user, 2026-10-08; built and FL-checked the same day)
The user's tuning in FL had the old two crossfades ("bay bypass" and "bay delay change") doing different jobs at different lengths (tempo changes 6 ms, linear phase about 20, Off about 25, Bypass about 12). User: ironing out every pop will likely take separate parts per situation, since there are non-linear interactions in each; this is the start.
- **One fade per cause** (`BayLink::Fade`), in a "Bay fades" section in Settings. Each fade picks its length when it starts and keeps it to the end, so overlapping causes never mix lengths. Current lengths, keys and the fade out/in splits: "1b.5a: decided with the user (2026-10-09/10)".

  | Fade | Covers |
  |---|---|
  | Round trip change | the timing jump when the round trip changes (tempo, Automatic, Settings) |
  | Bay latency change | the fade-out and fade-in around a return held back for the bay latency (settling, the transmitter holding, stamped by the receiver), and the timing jump when a new latency is adopted |
  | Mute | dry ↔ the bay's sound, entering and leaving Mute |
  | Bypass | dry ↔ the bay's sound, and the bay's send, entering and leaving Bypass |
  | Off | the timing jump, the bay's send and the fade-in, entering and leaving Off |
  | Dropouts | the fade-out and fade-in around a missing return, and the crossfade into the bay's first sound |

  - State fades are 0 to 500 ms, the round trip change 0 to 100 ms.
  - Leaving a state, the crossfade back into the bay's sound uses the state left.
  - The bay's first sound is only crossfaded when a panel starts (a new instance, a project opening, FL preparing it) or relays first arrive. After a dropout, the bay's sound fades in from silence instead.
  - A timing jump where both the round trip and the bay latency changed uses the longer of the two.
- **Shrinking while stopped: one check per stop** (user: fewer pointless checks). Once shrinking is allowed (stopped, and quiet if so set), one check runs. Another runs only when the host's tempo changes, the transport plays and stops again, or a check is asked for. A check that ended without being allowed to shrink is tried again.

### Bug: 1 → 2 real blocks missed nearly every block (fixed and FL-checked 2026-10-08)
- **Seen:** with real blocks, switching from a fixed 1 block to 2 (also 3 → 2, Automatic's fallback to 2, a shrink at a fixed 2) often made the panel miss nearly every block ("late"); 1 → 3 and 2 → 3 didn't.
- **Cause:** the receiver lined up by `max (the panel's unit, its own largestBlock)`, and `largestBlock` was its largest real block since its prepare, never forgotten. One block bigger than the panel's unit (e.g. a full 512-sample block, as FL may send around loading or starting) stuck at 512 while the panel's unit was rechecked down to 269. At 2 blocks (538 > 512) the receiver's lead was 512, leaving the return 26 samples of room; at 1 block it follows the newest block anyway; at 3 blocks 295 samples of room exceed FL's 215-sample ticks.
- **Fix (user's choice):** the receiver lines up by the panel's unit (`info.max_block`: real or announced blocks), never less than the block being processed; the relay's `largestBlock` is gone. The lead is 1 unit at 2 blocks, 2 at 3. No hub version change.
- **Regression test** (`[blocks]`, "one big block long ago doesn't starve a 2-block round trip, however it's reached"): 1 → 2, 3 → 2, Automatic's fallback and a shrink at a fixed 2, in every order. Before the fix up to 180 of 200 blocks missing; after, 0.

### Future refinement: latency-change pop fade ("linear phase pop fade")
Noted for a later quality-of-life pass (user, 2026-10-08). Reference it by either name.
- **The problem:** a bay plugin that changes its own latency (linear phase on or off, its amount) pops at that moment: its output jumps, or it refills its delay line with silence and then starts abruptly. It happens inside the bay, before the panel knows. Our fades already hide the panel's own side (the ghost fade, held-back silence, the faded return). But a plugin after it in the bay, such as a reverb, rings the pop out loudly.
- **The idea: an automatic Bypass-like cycle on every bay latency change,** reusing the send fades, the tail cut and the "waiting for the bay's sound" transition. When the transmitter's reading changes (held, unreadable or a new value), the panel fades the bay's send out; the tail cut goes to 1 once the send is silent (clearing the reverb through the user's Peak Controller wiring) and holds for the release *and* the new bay latency, so the latent plugin's delayed output (the abrupt restart included) has flushed through before the reverb runs again. Then the send fades back in, and the panel holds its output (silence, or dry as when leaving Bypass: to decide) until the return has passed the faded positions, then crossfades in.
- **Limits:** it can't stop the first pop reaching the reverb (the change is only known after the plugin has switched), only clear the ringing afterwards; it clears the music's own reverb tail too; the gap grows to about fade + cut hold + new latency + fade; it only helps when the tail cut is linked to the plugins that ring.
- **A cheaper part on its own:** after a new bay latency is adopted, keep the return silent until it carries sound again, then fade it in. This hides the abrupt restart when nothing after the plugin rings, and does nothing for a reverb.
- **Size:** a refit of existing machinery, but with timing subtleties (the cut's length depends on the latency being adopted). A setting: on a bay latency change, *hold* (today) or *cut tails*.

### Future refinement: virtual lookahead automation
Parked by the user (2026-10-08), low priority. Reference it by that name.
- **The problem:** with real blocks, a sudden tempo drop makes FL's blocks outgrow the round trip before the panel notices (up to its 0.1 s check). Those blocks are missed (faded silence), and Automatic can fall back to 2 blocks.
- **The idea (user):** a copy of the project's tempo automation clip, moved slightly earlier, drives a panel parameter ("upcoming tempo"). The panel grows the round trip before the real tempo drops.
- **Reading the project's automation directly would be better, but plugins can't:** VST3 and CLAP only hand a plugin parameter changes for the block being processed, and the playhead reports the current tempo, not future ones. FL's Python scripting API might reach automation clips; not checked.

### Settings defaults file and Set as default (user, 2026-10-08; built and FL-checked the same day)
- **Defaults in the source only for now** (user). Updating the factory defaults for release is handled then.
- **`source/SettingsDefaults.json`** lists each plugin-wide setting by its key (as in the settings file and the page's messages): its type (flag, whole, decimal or choice), factory default, limits (min and max, or the choices) and, for numbers, a unit for the menu.
  - CMake (`cmake-local/SettingsDefaults.cmake`) turns it into `SettingsDefaults.h` when the build is configured. Editing the JSON reconfigures; a malformed entry, or a default outside its limits, stops the build.
  - `PluginSettings` and `BayLink` take their defaults and limits from it; so does the page (`editor.svelte.ts` imports the same file), so the limits are never written twice. The page uses the same keys as the settings file.
  - Not in the file: the forwarded keys (no page; a list edited in the settings file) and the remembered editor size.
- **The user's own defaults** (user's choice A): kept in the settings file as `default.<key>`, beside the values. A setting's default is the user's own where there is one, else the factory's. Every value and user default is checked (the file is untrusted): one that doesn't fit is ignored and the default used; decimals are checked by their bits (fast-math).
- **Right-click any setting** (an in-page menu, like the parameter menu; it will also work on a tablet):
  - **Set as default** takes the right-clicked radio option, the box's state, or a text field's typed value (committed as its change handler would). It sets the value too. A default equal to the factory's forgets the user's own.
  - **Reset to default (X)**: forgets the setting's value, so it follows its default.
  - **Reset to factory default (Y)**, where the user has a default of their own.
- **"Reset all settings to defaults"** (top of the Settings tab; asks first): every setting in the file back to its default (the user's own where set). The remembered size and the forwarded keys stay.

### Names probe (test build, 2026-10-08; FL-checked)
- **Host controls can be renamed** (`HostControls::setName`), and the host is told: JUCE turns it into VST3's `kParamTitlesChanged`, a CLAP rescan of names, an AU parameter list change. Plugin-supplied parts are untrusted: control characters go, and names stop at 100 characters.
- **The probe:** Param 1 is called "SLOT 1.1: kHs 3-Band EQ - Low" (since 1b.5a it follows its slot); Diagnostics' "Names probe" button toggles the plugin's name ("SLOT 1.1: Low"). To be removed with 1b.5d.
- **What JUCE allows** (checked in its source): renames in both directions (a hosted plugin's renames reach us as `parameterInfoChanged`, so names like Snap Heap's can pass through). Not possible without editing JUCE: hiding unused controls (JUCE never sets VST3's hidden flag, and reads "automatable" only once) or changing their number (most hosts don't support it). So FL always sees all 256, with real names on bound ones.
- **What FL does with a renamed parameter** (FL check, 2026-10-08):
  - Renamed at once: the plugin parameter browser.
  - Renamed on reopening: the remote control settings window of a controller link, the automation clip's list of target links (its properties window), "last tweaked" menus, and the Patcher node's input pin parameter list.
  - **Never renamed: the automation clip's own name**, but the clip keeps driving the parameter (after a rename and after reloading). The user: a clip's name is just a label the user can edit, and one clip can drive many links at once, each with its own mapping, so this is robust.
  - So dynamic names are safe to build (1b.5d).
- The same check asked for separate fade out and fade in lengths: done in 1b.5a. Round trip changes keep one length: they crossfade the old timing into the new rather than fading out and back in.

### Slots: decided with the user (2026-10-08)
- **Order:** the structure first, then states, swap, controls and names, bays as slots, FL's menu (the plan's 1b.5 a to f).
- **Slot states** (the same meaning for every kind of slot; the mechanics differ):
  - **Mute** (hot bypass): the slot's contribution is silenced, but it keeps processing in the background for instant resumption. An FX slot passes the dry sound; a generator is silent.
  - **Bypass** (warm bypass): its processing is bypassed, but it holds its place in latency calculations; effectively resets its tails.
  - **Off** (cold bypass): removed from the processing grid entirely (the latency reported changes).
  - **"End signal path when off"**: a right-click option on the Off control. Off then also kills the slot's output downstream, so it's no longer a kind of bypass.
  - Slot state management will be reworked slightly later; this works for now.
- **Default controls on every slot:** the three non-active state buttons, small and single-letter: **M**, **B**, **X** (host names "SLOT 1.1: MUTE", "SLOT 1.1: BYPASS", "SLOT 1.1: DISABLED"; default controls are uppercase). A **dry/wet mix**, a short slider until there are knobs: "SLOT 1.1: MIX".
- **Host control names:** "SLOT b.n: parameter" for slots, "GEN n: parameter" for generators. Two settings: **"show plugin name"** ("SLOT 1.1: Serum 2 - cutoff frequency") and **"instead of SLOT b.n"** ("Serum 2 (2) - cutoff frequency").
  - **Instance numbers** per plugin, by the order instances were added, with no gaps (later ones move down when one goes); moving a slot never changes them. A single instance has no number.
  - **Unbound entries: "---".**
- **Bindings never move once made** (option a): a new control takes the next free entry anywhere in the pool, and its name says where it belongs. Shifting entries to insert controls would break automation, controller links and Patcher wires, which follow the parameter's ID.
  - **"Tidy host controls" is a definite later addition** (option b): it renumbers into slot order and warns that existing automation and links will then point at other controls.
  - Fixed blocks per slot (option c) wouldn't work anyway: slot order and count aren't fixed.
  - FL doesn't rename automation clips, but (user) keeps them pointing at their parameter; confirmed by the names probe.
- **Drag:** a rudimentary drag that isn't clunky (pointer tracking with a drop line, not the browser's drag-and-drop, which may clash with FL's file dropping). If it can't be made un-awkward cheaply, buttons for now.
- **Loading** a plugin freezes the UI while it loads (VST3s load on the message thread; Sylenth1 takes about 4 s): fine for now.

### 1b.5a: decided with the user (2026-10-09/10)
- **No backward compatibility before the first release** (standing rule), except the host-control IDs. Each saved state carries a version; an older or unknown one opens as a new instance would. The bay's old slot-state formats and the old-project importer go; setting keys get consistent names (older values are simply ignored).
- **Fade splits** (user's finding: a short fade out kills the click when a bay plugin switches its latency, but a short fade in makes a pulse when the delayed audio comes back; a long fade in eases it in). The bay latency change and dropouts each get a **fade out** (the reversed ghost) and a **fade in** (the level coming back). **The timing jump on adopting a new bay latency uses the bay latency fade in:** while the bay plays, the old timeline is already silent, so the jump is a second fade-in ending with the first; while dry plays (Mute, Bypass, waiting for the bay's sound), it crossfades two stretches of audio up to the whole latency change apart, which wants the longer fade. The crossfade into the bay's first sound uses the dropout fade in.
- **New factory defaults and keys** (ms): round trip change 3 (`bayRoundTripFadeMs`); bay latency change out 17, in 100 (`bayLatencyFadeOutMs`, `bayLatencyFadeInMs`); Mute, Bypass, Off 34 each (`bayMuteFadeMs`, `bayBypassFadeMs`, `bayOffFadeMs`); dropouts out 17, in 100 (`bayDropoutFadeOutMs`, `bayDropoutFadeInMs`); bay latency settle 0; tail cut release 8.
- **Structure changes dip the engine's output** (adding, removing, moving slots or buses, a generator's buses): it fades out, the change is applied once silent, then it fades back in. Its own machinery (it can wait for its fade out, unlike a dropout), so its own split: `slotChangeFadeOutMs` 17, `slotChangeFadeInMs` 100.
- **New slots:** a menu of Empty, kHs 3-Band EQ, kHs Nonlinear Filter (FX); Serum 2 (generators). The default layout keeps Sylenth1 as GEN 2. Plugins only ever load from this known list, never from a path in a project (project files are untrusted).
- **b.0 and b.X** are fixed, empty placeholders (no menu), filled with utilities later. A bus outputs to X.0; a sub-bus into its parent's b.X.
- **Drag straight away** (no buttons first).
- **Dev layout:**
  - Design size 790 × 1,270, portrait (user, 2026-10-10: FL adds 10 px of window border, making it 800 × 1,280; first built as 1,280 × 800 by mistake). Background #0f1012. Slot contents in `assets/fonts/m5x7.ttf` at 16 px.
  - **Font licences: settled.** The user provided the OFL licence files in `assets/fonts/` and confirmed the permissions.
  - **One "Panel" tab** (Panel and Plugins merged; the exposed parameters live inside their slots), then Settings; the tab bar stays at the top.
  - Stages from the top: a **Header** (panel-specific, not plugin-wide; 200 px; for now the old Panel tab's non-control contents), the **Generator stage** (a row of slots that scrolls right on its own), the **Buses** (columns of slots, b.0 at the top, scrolling right on their own). **BUS X is pinned** at the right. Sub-buses sit right after their parent (an open design question: several layouts to try later, so expect sub-bus routing and presentation to be revised often).
  - Slots 180 px wide; their header (name and, later, the default controls) 40 px tall; they grow downwards as controls are added.
  - Each stage's bar is about 30 px, spans the window, collapses its stage, and pins at the top, stacking while scrolling.
  - **The bay** shows as a box after X.X in the master column, not draggable, its 16 controls in 4 columns of 4, each a slot's width.
  - **A hint pane** pinned to the bottom, 40 px, a no-op for now.
  - **Diagnostics widens the window by 400 px** to the right (the hint pane keeps the main width), rather than a separate window (no second web view or bridge to lock down).
  - Collapsed stages and sideways scroll positions are saved with the project.

### 1b.5a as built (built 2026-10-10; the user's first look in FL was positive)
User, pre-alpha: "the interface looks great, and it works smoothly". The step-by-step check: `docs/fl-checks.md`.
- **The model** (`source/engine/Structure`): generators (each with the buses it feeds), buses in display order (a sub-bus right after its parent; one level only, so no loops are possible), the master, and the next ID. IDs are never reused. Positions give the names: GEN n, BUS n (sub-buses numbered like any bus), b.n, X.n. Edits are refused (nothing changes) when they don't fit; limits: 32 generators, 32 buses, 32 slots per bus. Saved as XML with a version; reading checks everything (unique IDs, plugin keys of letters and digits only, a sub-bus right after its parent, generators' buses existing) and refuses the whole structure otherwise.
- **The plugin list** (`source/engine/PluginCatalog.h`): Serum 2 and Sylenth1 (generators; Sylenth1 not in the menu), kHs 3-Band EQ and kHs Nonlinear Filter (effects), by key, with Windows paths (macOS paths are guesses). A project only names keys; a key not in the list (or of the wrong role) gives a slot that says "Not in the plugin list" and loads nothing.
- **The graph** (`Engine`): every generator and slot is a box (`Engine::SlotBox`) with its plugin inside; each bus (the master too) has two empty boxes for b.0 and b.X. A generator's box takes MIDI and has the generator Mute (as before). Generators feed b.0 of their buses; slots run in sequence to b.X; b.X feeds X.0, or for a sub-bus its parent's b.X; X.X feeds the output. Any change rewires every connection from the model (cheap), then the graph rebuilds once. JUCE keeps a removed box alive until the audio thread has stopped using it.
- **The dip:** a change of what's heard (adding, removing or moving a slot, removing a bus, a generator's buses) while audio runs fades the engine's output out (`slotChangeFadeOutMs`), applies the change once silent (or after the fade plus 250 ms, whatever happens), then fades in (`slotChangeFadeInMs`). Adding an empty bus or reordering generators changes nothing heard: no dip. Audio not running (no block for 200 ms): at once. The model changes at once; only the graph waits. A new slot's plugin loads before the dip.
- **Host controls:** the bay's controls are always Param 2 to 17; exposed parameters keep the host control they were bound to (Param 1: the default EQ's Low), saved with the project. Removing a slot unbinds its controls first (`HostControls::unbind` waits for any host thread still setting it: a counter, realtime-safe), closes its plugin's window and drops its parameters from the page. Param 1's name follows its slot ("SLOT X.1: …" after a move), "Param 1" when unbound (the names probe still toggles the plugin's name).
- **Saved state:** the processor's state has `version` 1, the engine's 2, the structure's 1; anything else opens as a new instance (default layout). Restoring reuses plugins already loaded, in order, where the saved structure has the same plugin, so a project opening doesn't load Serum 2 and Sylenth1 twice. The bay's old slot-state formats are gone.
- **Fades:** `BayLink::Fade` has eight lengths (round trip, bay latency out and in, Mute, Bypass, Off, dropout out and in). A dropout or held-back return fades out over its cause's fade out (the reversed ghost); the level comes back over the same cause's fade in. The timing jump to a new bay latency uses the bay latency fade in. Keys renamed (`bayRoundTripFadeMs`, `bayLatencyFadeOutMs`/`InMs`, `bayDropoutFadeOutMs`/`InMs`); old keys are ignored.
- **The page:** tabs Panel and Settings. Panel: Header (title, bridge, audio), Generator stage (a row, sideways scroll), Buses (columns, sideways scroll; BUS X pinned at the right; the bay box below it, right-aligned, 4 × 4 controls), each under a sticky 30 px bar that collapses it; a 40 px hint pane at the bottom; Diagnostics widens the window by 400 px. Slots: 180 px, a 40 px header (label, name, ×; the drag handle), then status, Open, Mute (generators), the generator's bus chips, and the exposed controls as compact sliders; an empty slot shows "Choose plugin…" (the plugin's slot takes its place). "+ slot" (Empty or a plugin), "+ generator", "+ bus", "+ sub", bus ×. Drag: pointer tracking from a slot's header, a drop line that takes no room, a ghost under the pointer, sideways rows scroll near their edges, Esc cancels; generators move only among generators. Slot contents use `assets/fonts/m5x7.ttf` at 16 px (embedded with the page; the page's security policy allows its own fonts). Background #0f1012.
- **Fixed on the way:** the Settings tab's two-column layout briefly measured itself shorter, which pulled its scroll position to the top on any change (now kept).
- **Not done (noted):** a hosted plugin changing its own latency isn't followed yet (the box takes it at prepare); removing a slot asks nothing first.
- **Tests (92 test cases, Debug and Release):** the structure and its saved data, the engine's wiring, the dip, missing or unlisted plugins, Param 1 following its slot, the fade in after a dropout, and the page's slot controls.

### 1b.5b: decided with the user (2026-10-11)
Three parts, each with its own FL check: **A** the UI additions, **B** slot states, **C** following a hosted plugin's own latency changes (needed before or with 1b.5c).

**A: UI**
- **Hint pane** (60 px, scrolls; about 2.5 lines): shows the hint of whatever the pointer is over. Every former `title` tooltip moves there (the resize grips' too: their tooltips never showed). **Alt+wheel scrolls it** while the pointer stays on the hinted element (a hint only shows during mouseover), at most a line per step; the Alt release after it is swallowed so it can't open the host's menu bar.
- **Settings:** an option's explanation (the text that was in parentheses) is its row's hint; units, ranges and scope tags ("FL only", "Windows only") stay in the label. Where a semicolon split a label, a line break. A setting's own explanation (the paragraphs under it) is its heading's hint. Warnings that depend on the state stay on the page.
- **Option rows:** the control lines up with the first line of its text; only the control itself reacts to clicks. Hints and the right-click menu still cover the whole row.
- **Resets:** **Reset all** (every setting to its default: the user's own where set) and **Factory reset** (every setting to the factory default; the user's own defaults are kept). Both ask first. Right-click Reset all: **Clear user defaults** (the current values stay). Right-click Factory reset: **Hard reset to factory defaults** (clears every setting and every user default).
- **Controls hiding an option behind a right-click** (the resets, not the settings' ordinary menus) show the `help` cursor; a custom cursor with a menu mark may come later.
- **Swap width and height** (beside Reset size): swaps the main area's width and height at the same scale; Diagnostics' 400 px stays out of it. A plain click keeps the top-left corner; with the centre modifier (whatever the corner grips use for "around the centre", so the flip setting counts, and nothing hard-codes Alt) it swaps around the centre.
- **Diagnostics:** explanations go into hints; each line shows its values, short numbers and units included. The primary value (a number, or a word that is the line's value) is white; all other text #555555. Order, in groups with dividers: Reset counters, Link, Relays, Transport | Round trip (with Automatic), Headroom, Block sizes, Output delay, Bay latency | Missing blocks, Receiver missed, Held back | Slot (until slots get their own controls), Return vs sent, Lag, Controls, Control timing | Events.
- **Archived:** the names probe, the key log and the window list leave Diagnostics and the code ("Archived diagnostics", below).
- **Buses:** the sideways scrollbar sits at the bottom of the whole bus area, BUS X included.
- **Fonts:** h1 Rubik Bold (the stage bars, Diagnostics' "Bay link", the header's title: the plugin has no other title); h2 Rubik (setting headings, "Events"); h3 Barlow (bus titles, for now); p Barlow Regular; emphasis Barlow SemiBold, only on a line with plain text; **p2** m5x7 at 16 px (slot text); **p3** Tiny5 at 8 px (small slot text: the slot header's state buttons, MIX, the slot number).
- **Pixel fonts:** no anti-aliasing for p2 and p3 by default (an alpha threshold filter: exact at 100% and 200%, uneven pixel widths in between). Settings: **pixel text anti-aliasing** (off) and a **rendering fallback** (Barlow for all text, p2 at 10 px and p3 at 8 px; anti-aliasing then always on).
- **Colours:** controls and their states #ff6000 (the browser's blue, our #8fa2ff), drag #ff0055; hover adjustments stay. The bay's toggles too. Warnings keep #ffb36b. Menus: the page background with a hairline accent border.
- **Font files:** only the page's fonts are kept, in `ui/fonts/` with their licences (the page embeds its own copies; `assets/` is embedded into the plugin binary as well). The rest of the families moved to `C:\dev\auxiliary materials\fonts\`.

**B: slot states**
- **States ON, MUTE, BYPASS, OFF.** Uppercase is the convention for our own controls from now on (slot default controls, header controls, bus-level controls later). "Active" is renamed ON everywhere (the bay included), so it isn't confused with a control's selected state.
- **Buttons:** text only, tiny: "M", "B", "OFF" (labels only; hints and code use the full names). The selected one is in the accent colour. No ON button: deselecting returns to ON. On empty slots they're shown but disabled.
- **BYPASS keeps the slot's latency** (dry delayed by it). **MUTE**: the plugin keeps processing, its output is dry. **OFF**: out of the processing grid, its latency no longer counts.
- **"End signal path when off"** (right-click on OFF, per slot): a kill switch. Everything downstream fed only through that point is dead: not processed and not connected, so it costs no CPU and no latency. A box with another live input carries on. Dead slots show greyed with a note; their own buttons stay usable.
- **Fades:** MUTE and BYPASS share one set with the bays. OFF on a hosted slot is a dip using the Slot changes pair; the bay keeps its own OFF fade (option A).
- **Test plugins:** kHs Compactor (489 samples of latency) and kHs Reverb (tails) join the slot menu.
- **Host controls:** page only for now. Later (1b.5d) a slot's state could be one stepped host control (OFF at 0, BYPASS at 0.25 … ON at 1, more robust in hosts than whole numbers).

### Archived diagnostics (2026-10-11)
Removed from Diagnostics and the code in 1b.5b part A; recoverable from the git tag **`archive/host-diagnostics`** (commit `699ff02`, the last with them). To bring one back, take its pieces from that commit.
- **Names probe:** a Diagnostics button that toggled the plugin's name in Param 1's host name (`PluginProcessor::toggleNamesProbe`, `applyNamesProbe`, the page's `namesProbe` message and `probeName` in the editor state). Param 1 keeps the full form, "SLOT 1.1: kHs 3-Band EQ - Low" (`PluginProcessor::nameParam1`), until 1b.5d's dynamic names. Findings: "Names probe".
- **Keys forwarded to the host:** the last 12 forwarded keys, each with the mouse state, the capture windows and which window took it (`KeyForwarder::postToHostOf`'s log callback, `PluginEditor::logKey`). Used to find FL's key route ("Resizing chords, smooth rendering, keys and windows").
- **Windows around the editor:** the window chain from the editor up, with class, kind, size and position (`EditorPlacement::describeWindows`). Its findings are recorded under "FL's window chain".
- Both lists reached the page as the `hostDiagnostics` event (`PluginEditor::reportHostDiagnostics`, the page's `HostDiagnostics.svelte`).

### 1b.5b part A as built (2026-10-11)
- **Hints:** `ui/src/lib/hint.svelte.ts`: `use:hinted={text}` on any element; the innermost hinted element under the pointer wins; a Diagnostics line hints for its label and value together. Page menu items can carry hints too.
- **Pixel text:** the `.p2` and `.p3` classes set the font; `.crisp` (on text-only elements, since translucent pixels vanish) applies an SVG alpha-threshold filter (`#pixel-text` in `App.svelte`). Settings `pixelTextAntialiasing` and `fontFallback` toggle classes on the page's root; style tokens (`--accent`, `--drag`, `--dim`, the p2/p3 fonts) are in `app.css`.
- **Resets** (`PluginSettings`): `resetAll` (to defaults), `factoryResetAll` (factory values written, user defaults kept), `clearUserDefaults` (each current value written first, so it stays), `hardReset`. The page's message: `resetSettings {all: "defaults" | "factory" | "clearUserDefaults" | "hard"}` or `{name}`.
- **Swap** (`PluginEditor::swapSize`, message `swapSize`): the layout shares become `(y × 1270 / 790, x × 790 / 1270)`, so swapping twice is exact unless a layout limit clamps it; the centre anchor comes from `wantsCenterAnchor` (the corner grips' rule). The page names the modifier in one place (`centreModifier`).
- **Fonts** live in `ui/fonts/` (Rubik variable, Barlow Regular and SemiBold, m5x7, Tiny5); `assets/` keeps only the template image. A test checks every font the page's styles name is served.
- **Hint pane** text starts at the top for each new hint. The editor's title (version and build type) is a plain line at the top of Settings; the Header stage has no title.

### Part A: pixel text and polish after the first FL look (user, 2026-10-13)
- **Why the pixel text looked bold:** Windows' text engine smears strokes sideways (vertical is exact). Measured on the user's screenshot: a stroke about 89% covered, a pixel beside one stroke 37%, a 1-pixel gap between two strokes 51 to 59%. The old threshold (50%) filled those gaps. **Now 75%** (`#pixel-text`, `tableValues="0 0 0 1"`); at 100% it matches the user's "ideal" (the same font in another app).
- **Stepped pixel sizes** (`ui/src/lib/pixelText.svelte.ts`): each font pixel is a whole number of screen pixels (one below 150%, two from 150%, …); anti-aliased, steps of a share of a screen pixel. Settings (page only): `pixelStepAtPercent` (where the next size starts, 1 to 100, default 50), `pixelStepAntialiasedPercent` (5 to 100, default 25). Between steps pixel text is a little smaller or larger than the rest of the UI (user's choice, to keep it crisp).
- **Snapping** (`pixelTextSnap`, on): every `.crisp` element is moved by a fraction of a pixel (CSS `translate`; spans become inline-block, since inline boxes ignore it) so its text starts on a whole screen pixel; again after text, layout, scrolling or size changes (one pass per frame). Without it, a stroke between two pixels falls below the threshold and vanishes.
- **Page-only settings** go through one checked path in C++ (`PluginSettings::pageOnlyKeys`, `getPageOnly`, `setPageOnly`), stored like any other setting.
- **Menus:** a setting offers "Reset to user default (…)" only when it has one; "Reset to factory default (…)" always. Right-clicking a group's heading resets every setting in it ("Reset to user defaults" when any has one, "Reset to factory defaults").
- **Settings text:** option labels mid-gray (`--mid-gray`, #555555, a global token, also the hint pane's text); no forced line breaks in labels; ranges and other values (the control output gain, the double-click range) in the hints as "(min to max)"; no emphasis on option labels.
- **Controls:** buttons, number fields, checkboxes and radios share one style (`app.css`, zero specificity so components can override): background #1a1d21 (`--control-background`), mid-gray border, Barlow; checked: an orange tick or dot. Slot buttons keep the slot font (p2) inside the new style.
- **Diagnostics:** one label column for every group (a CSS subgrid), labels right-aligned; "receiver: …", "transmitter: …"; "(announced N)" on its own line.
