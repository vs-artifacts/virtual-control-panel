# Decisions

Short log of settled decisions. Newest at the bottom. Update when something changes; don't re-debate settled items without new information. Where things stand and what's next: `docs/current.md`.

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
- **Exposed parameters:** each FX slot (in its default mode, at least) exposes a configurable set of parameters linked to its hosted plugin. Only the parameters the user chooses are exposed, in one consistent format and style matching the design theme.
- **Live performance UX:** stored parameter sets act as waypoints; transitions between them follow arbitrary automation curves or settings, interpolating groups of parameters together.
- **Design:** the glassmorphism/light style; by virtue of its design, the UI doubles as a stage visualiser.
- **Why:** no DAW offers the configurability the user's sound design and composition workflow needs, or the aesthetics wanted on stage.
- **Origin:** processing and cross-processing FX streams from parallel Serum 2 and Sylenth1 instances fed by one MIDI input.

### Architectural implications (agent's reading; revisit with the blueprint)
- **Routing is data.** The engine should build its graph from a saved, editable routing model (slots, buses, connections), not hard-coded wiring. `juce::AudioProcessorGraph` supports adding and removing nodes and connections while running.
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

## Rendering / blur (historical)
- The JUCE-native GlassToggleButton prototype (parent snapshot + `applyGaussianBlurEffect`) was a workflow test only. Deleted in 1a (recoverable from commit `3d7b75c`).

## Workflow
- **Style tokens:** all visual constants live in one place, in the web UI (JSON/TS), with a debug tuning panel (sliders/colour pickers, copy/save). Vite hot reload gives live editing. (Replaces the earlier C++ JSON token plan.)
- **Specs:** split into small files under `docs/` (see index in `CLAUDE.md`), not one monolithic blueprint. Behaviour lives in each component's spec; global material rules in `docs/style/glass.md`.
- **Assets:** one SVG per component with named layer ids; states are usually parameters applied in code, not separate files. Separate state SVGs only when geometry actually morphs (matching path structure).
- **Iteration:** FL Studio / Patcher (via the VST3 junction) for in-DAW checks; web UI iterated in a browser with Vite hot reload where possible.
- **Platforms in practice:** build and test on Windows first; keep platform-specific code isolated; test the web UI in Safari early; smoke-test on a Mac at milestones.

## Plan: thin vertical slice (each sub-slice needs the user's go, ideally its own chat session)
1. **1a. Host inside the plugin:** our VST3 loads one VST3 from a fixed path (test plugin: `C:\Program Files\Common Files\VST3\Kilohearts\kHs 3-Band EQ.vst3`), passes audio and MIDI through, can open its editor window, lists its parameters. C++ only, no scanning. **Built and FL-verified 2026-09-25** (audio, hosted editor, live values, state in projects and Fruity Wrapper presets). Patcher had no MIDI event input, which led to 1a.2.
   - **1a.2. Instrument + parallel generators:** switch to an instrument; Serum 2 and Sylenth1 in parallel on one MIDI input, summed into the kHs EQ, plus a MIDI pass-through lane. **Built 2026-09-25; FL-verified 2026-09-26.**
2. **1b. Bridge:** the plugin editor shows a minimal Svelte page; one plain slider controls one hosted parameter two-way, including pushed updates when the hosted plugin's own UI changes it; then a remote-control server. Sub-slices:
   - **1b.1. Page in the editor** via JUCE's native bridge. **Built 2026-09-26; FL go/no-go passed** (page shows, second instance fine, hosted plugins unaffected). Its findings (slow reopen, white flash, no key pass-through) are fixed; **FL re-check passed 2026-09-26**, except that keys don't pass while a mouse button is held on the page (carried into 1b.2).
   - **1b.2. One slider, two-way** (kHs 3-Band EQ low band gain) via the native bridge, **also exposed to the host** for automation and controllers (added to this slice: a core mechanic, so test it early). **Built 2026-09-27; FL check passed 2026-09-27** (slider both ways, Param 1 in the browser, last tweaked, automation playback and recording, Patcher nodes and internal controller links; a hardware controller and plugin swaps weren't testable). Centring on first open built and passed. Its findings are being wrapped up in the same slice (see "1b.2 wrap-up" below): tabs, scaling and corner resizing, the host's right-click menu, and key pass-through while the mouse is down (first pass: diagnosis). **Wrap-up built 2026-09-27; awaiting the FL check.**
   - **1b.3. Parameter outputs:** can our plugin hand values to FL as *outputs* (Patcher's controls list, internal controller links)? Two temporary test parameters appended after the pool (a meter-category one, which JUCE marks read-only in VST3, and an ordinary automatable one), both moved by an internal slow wave from the audio thread (VST3's output-parameter route; Param 1's plugin-driven changes go through the message-thread edit route and appear only as an input). No MIDI CC out: the user found it doesn't drive FL plugins in Patcher. **FL check 2026-10-01: no parameter outputs** (details in "Module bays and relay nodes"); the test parameters are removed.
   - **1b.4. External module bays:** our way to use FL's own plugins, which we can't host (see "1b.2 wrap-up"). FL forbids feedback loops, so relay nodes on both sides of the FL plugin talk to our plugin directly instead of over Patcher wires (see "Module bays and relay nodes" below). Placed before plugin swap to validate the idea, or to settle a full move away from FL's plugins. **FL experiments done 2026-10-01** (outputs, Peak Controller, gain, smart disable; relay test plugin built and checked). **Link test (audio round trip) built and FL-checked 2026-10-02:** everything passed except the 1-block round trip and confusing lag readouts, both fixed the same day and **passed the FL re-check**. **Control outputs built 2026-10-02** (with the automatic round trip, the bypass switch and crossfade, safe dropouts, and the timing probe; see "Bay control outputs" below); awaiting the FL check. Then: a bay plugin's own latency. Status and next step: `docs/current.md`.
   - **1b.5. Plugin swap** (core, non-negotiable: choosing the plugin in a slot): a button per slot in the Plugins tab that swaps the plugin from a copy of FL's plugin menu (its categories and favourites, plus "More plugins…" with everything installed), read from FL's plugin database (`Documents\Image-Line\FL Studio\Presets\Plugin database`) each time the menu opens so it follows FL's changes, and styleable like the rest of our UI later. Needs: swapping while audio runs, safe unbinding of host controls, saving each slot's plugin and the bindings with the project, and a rudimentary "Bind…" menu per host control (the real solution is each parameter item in a slot's UI linking to the exposed-parameters list). Lets us test automation across swaps.
   - **1b.6. Remote server:** Crow WebSocket server behind a remote-control toggle, own update thread, security tests, "Open in browser" button.
3. **1c. Shader knob:** the slider becomes a WebGL2 glass/light-scatter knob with a frame-time readout, running inside FL.
- Style work starts after 1c.

## Slice 1a: hosting (2026-09-25)
- **Web UI re-confirmed** after weighing JUCE-only: the Mac is headless, so the tablet is the real screen and a web UI is needed anyway; one Svelte app serves both. 1b/1c double as go/no-go tests for WebView2 inside FL. Fallback: plain JUCE editor in the DAW, web UI still serving tablets over the same protocol.
- **Effect now, instrument soon.** FL only sends piano-roll notes to generators, and hosting synths is core. *(Done in 1a.2, earlier than planned.)*
- **Engine code lives in `source/engine/`.** *(1a.2: `Engine` runs a routing graph; `HostedPlugin` is one slot in it.)* Plugins load once on the message thread when our processor is created. No hot-swap yet; swapping while audio runs needs a realtime-safe handover later.
- **Stereo only:** our buses and the hosted plugin's main buses; its sidechain/aux buses are disabled.
- **MIDI:** ours goes into the hosted plugin and its MIDI output becomes ours. Plugins with no MIDI in or out (like the EQ) leave MIDI untouched, so it flows past them. *(Superseded in 1a.2: MIDI routing is explicit graph wiring.)*
- **Saved state:** every DAW's wrapper saves a plugin as an opaque blob it asks the plugin for, so we store the hosted plugin's state inside ours (XML, tagged with the plugin's identifier) and only restore it into the same plugin.
- **Threading:** hosted VST3s require `prepareToPlay` and get/set state on the message thread (JUCE asserts). VST3 hosts call ours there, so we forward directly.
- **VST3 parameter lists include MIDI CC slots** (16 channels x 130 controllers); the EQ reports 6 real + 2080 MIDI. `HostedPlugin::getControls()` returns only the real controls (flagged automatable), and the UI shows only those. Expand later for plugins that need longer lists.
- **Our own plugin exposes the same 2080 MIDI CC slots:** JUCE turns on MIDI CC emulation whenever MIDI input is enabled. VST3 has no MIDI CC events; hosts deliver CC, pitch bend and aftertouch as these parameters, so keep it on.
- **Patcher offers no MIDI event input pin** on our plugin (as an effect), though our VST3 declares a proper event input bus and MIDI out works. Likely FL's effect/generator split again; the 1a.2 instrument switch should fix it. *(1a.2 FL-verified 2026-09-26.)*
- Test plugin path: `hostedPluginPath` in `source/PluginProcessor.h`. Loading it adds ~0.2 s to processor construction. *(1a.2: replaced by `serum2Path`, `sylenth1Path`, `eqPath`.)*

## Slice 1a.2: instrument + parallel generators (2026-09-25)
- **Instrument only (`IS_SYNTH`), same plugin ID;** no FX variant for now. FL lists it under Generators after a rescan. Old test projects with it on a mixer insert are expendable.
- **The engine is a `juce::AudioProcessorGraph`:** boxes (plugins) and wires (connections). It fans MIDI out, sums audio where wires meet, delay-matches parallel paths, and swaps in routing changes safely while audio runs. It processes plugins one after another on one audio thread.
- **Fixed test routing:** MIDI in -> Serum 2 and Sylenth1 in parallel -> summed into the kHs 3-Band EQ -> audio out; MIDI in -> MIDI out (pass-through lane; bypass controls later). Set by `Engine::build (generators, fxChain)`, with the FX chain wired in series.
- **Slots have roles:** generator slots only take instruments, FX slots only effects. Needed because one VST3 file can hold several plugins (`Serum2.vst3` holds both Serum 2 and Serum 2 FX).
- **Saved state is per slot** (slot index plus plugin identifier), so two copies of the same plugin each get their own settings back.
- **A failed load keeps its slot** with an error message; everything else still loads and plays.
- **Measured:** Serum 2 loads in ~0.5 s and exposes 541 of 2623 parameters as controls; Sylenth1 ~4 s, 246 of 2326; the EQ 6 of 2086. The "Boot performance" benchmark now runs 10 samples instead of 100 (~90 s).

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
- **Names are generic for now.** Dynamic labels are needed later (arbitrary buses and slots); VST3 can tell hosts that names changed, but FL's handling is untested.
- **Binding:** automatic, exposed parameter *i* to host control *i* (1b.2: the EQ's low gain to Param 1). Bindings are sticky: after a plugin swap a control stays, shown as unbound ("was X"), and the user rebinds it; freed controls are never reused automatically, so an old automation clip can't silently drive something new. A later iteration improves automation-binding handling for another QoL issue.
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
- Slider both ways; Param 1 in the browser, last tweaked, automation playback and recording, Patcher nodes and internal controller links; centring on first open.
- Not testable yet: a hardware controller link (none at hand), and what happens to automation when a plugin is swapped (no swap UI: 1b.5).
- Findings, handled in the wrap-up below: the web view scaled to the monitor (200%) while the JUCE part followed FL (100%); right-click showed the browser's menu instead of the host's options; no keys pass while the mouse is pressed anywhere in our editor.

## 1b.2 wrap-up (2026-09-27; second round 2026-09-28)

### Whole UI in the web view (second round)
- **The editor is only the web view,** filling it; tabs, plugin list, settings, corner grips and menus are all page code. JUCE draws nothing but a placeholder colour (and a message if no web view is available). Reason: the JUCE content was scaled with a transform, which doesn't move the native web view, so it stayed at its first size.
- **Page messages:** `editorState` (native function: title, tab, settings, plugin rows); events `tab {index}`, `setting {name, value}`, `resetSize`, `openPlugin {slot}`, `resize {corner, phase}`. C++ validates each (tab index in range, known setting names and values only, slot in range); the page validates every reply. Before `editorState` replies the page shows no tab content, so it doesn't flash the wrong tab.
- **Hosted plugin editors** open in separate always-on-top windows (`HostedEditorWindows`), one per slot; opening again brings it to the front.
- **Dropped:** the "Inspect the UI" button (Melatonin inspects JUCE components, and there are none left to inspect), the key log, and the "keep keyboard focus in the host" experiment (both solved by the keys fix below; the log is recoverable from commit `fb37620`).

### Tabs
- **Panel** (sliders), **Plugins** (the temporary plugin list and "Open window" buttons; the swap buttons come in 1b.5), **Settings** (every setting, the title and version).
- Each instance reopens on the tab it was closed at (saved with the instance); new instances open on Panel.
- The whole Plugins tab is temporary: it becomes part of the panel later, in our own style.

### Scaling and resizing
- **The page follows the plugin's scale, not the monitor's.** The web view renders at the monitor's scale; the host decides the plugin window's (FL keeps plugins at 100% on a 200% monitor). C++ sends the page its scale in physical pixels per page pixel (`display`: host scale × our UI scale), and the page zooms its root by that ÷ its own pixel ratio. Anything that measures pixels in the page (WebGL canvases later) must allow for that zoom.
- **The whole UI scales; nothing rearranges.** The page is always laid out at the design size (560 × 600) and zoomed to fill the window. Fixed aspect ratio for now, until the full UI's aspect ratio is decided.
- **Only our own corner grips resize** (all four, drawn by the page; C++ follows the pointer). The host's window frame no longer resizes the plugin: the editor isn't marked resizable, because the wrapper's own resizing ignores our sharp steps and window moves.
- **"Resize from" setting** (`resizeAnchor`): the opposite corner stays put (default), or the window's center does. Hosts grow windows from their top-left corner, so for either anchor we move the window to compensate; only floating windows can move (same "owned window" rule as placement), attached ones always grow down and right.
- **Limits:** 50% to 500% of the design size (500% should fill a 16k screen once the full panel is built and larger by default; windows stranded off screen can be recovered by any visible corner or FL's "bring windows back").
- **Resizing setting** (`resizing`): smooth, or sharp steps. Sharp keeps 1-pixel lines crisp, which needs a whole number of physical pixels per design pixel (monitor × host × ours), so the steps are 100%, 200%, … on a 100% monitor (133%, 200%, … on 150%).
- **Size memory:** saved per instance with the project, like the tab; new instances open at the last size used (`lastEditorScale`). "Reset size" returns to the design size.
- **Browser zoom is blocked** (Ctrl+wheel, Ctrl+plus/minus/0): it would fight the page's own scaling.

### Right-click menus
- **Our menu only,** with the host's options for that parameter in a "Host menu" submenu (greyed out when the host offers none). Our own items join it later. JUCE fetches the host's menu for the parameter's host control (VST3 `IComponentHandler3`); C++ turns it into items the page draws (`parameterMenu`), keeps each item's action, and runs it when the page reports a choice (`menuChoice`). Host-supplied text is shown through Svelte's escaping only; ids are checked against the current menu.
- **FL's section items** ("-Remote control" and the like) become non-clickable headings, with a divider before each.
- **The "Use the host's UI for menus" setting is gone:** a native host menu can't be combined with our own items, and FL's positioned it badly.
- **Each submenu is its own panel** kept inside the page: it opens beside its item, flips to the other side if it doesn't fit, and moves up if it would run off the bottom; a list taller than the page scrolls. Menus close on a click elsewhere, Escape, the window losing focus, or a resize.
- **Right-clicking anywhere else shows nothing** for now; the browser's own menu is always blocked.
- **Risk to check in FL:** if a host offers VST3 context menus but returns none for a parameter, JUCE dereferences a null menu (crash). Not guardable without changing JUCE.
- **Browser shortcuts blocked:** print, find, reload, save, view source, history navigation. Dev tools are already off in every non-Debug build (JUCE switches them off).

### Keys
- **Cause found (key log):** FL is a Delphi (VCL) program. VCL doesn't handle a key message where it arrives: its message loop reflects it to the window that should handle it as `CN_KEYDOWN`/`CN_KEYUP` (`0xBC00` + the message). While our window holds the mouse capture (a button held in it), that path is skipped, so FL's typing keyboard never sees the keys we post.
- **Fix:** in FL only, when some window has the capture, the forwarder sends the host window the CN message itself (`SendMessage`), falling back to the plain key message if it isn't handled. Other hosts and other cases are unchanged.
- **Releases always pass:** releases of forwarded keys pass even when the page never saw them go down (a note held since before the page had focus, FL default).
- **Drags no longer end on blur** (focus moving away mid-drag would otherwise split one drag into many gestures); a new press ends any drag whose release never arrived.

### FL's own plugins can't be hosted
- It's the format, not their location: FL's own plugins (e.g. `Plugins\Fruity\Generators\FLEX\FLEX_x64.dll`) use FL's private plugin format, which only FL can host; Image-Line's kit is for writing plugins for FL, not hosting them. "FL Studio VSTi" (FL running as a VST2) can't be hosted either: JUCE hosts VST2 only with Steinberg's VST2 SDK, which is no longer licensed. Hence external module bays (1b.4).

### FL check (2026-09-27): passed, minor bugs and tweaks left for the next session
- **Next (now slice 1b.3):** a small test of parameters as outputs (FL's Patcher "controls" list): a meter-category parameter (JUCE marks those read-only in VST3) that moves by itself, plus MIDI CC out. Param 1 already shows that a plugin-driven ordinary parameter appears only as an input.
- **Keys:** FL's typing keyboard plays while the mouse is held on the slider, on a grip, on a tab and on empty page space; releases arrive (no stuck notes), including when the mouse is released first.
- **Scaling:** at 200% Windows scaling the page fills the window at every size. All four grips resize at a fixed ratio; with "Opposite corner" the opposite corner stays put, with "Window center" the center does (detached windows); attached windows grow down and right. Sharp steps; size remembered per instance and for new instances; "Reset size". FL's window frame no longer resizes the plugin.
- **Tabs:** reopen on the last tab (no flash of Panel first); new instances on Panel; all three tabs work; "Open window" on the Plugins tab opens the hosted plugin's editor.
- **Right-click on the slider:** our menu shows "Host menu"; its submenu lists FL's options with headings and dividers, stays inside the window at any position, and its items work (e.g. "Create automation clip"). The menu closes on a click elsewhere, Escape, and clicking into FL.

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
- **Missing return blocks:** a panel setting chooses *silence* (diagnostic; the user wants it kept for testing) or *bypass*: the panel plays the matching block it sent, so only the bay is skipped for that block instead of the whole chain dropping out.
  - Bypass must line up with where the processed signal would have been: easy for the link's own delay, but a bay plugin with latency of its own (lookahead, linear phase) shifts the return further. Measuring that latency is open.
  - Switching between processed and unprocessed audio can click when the effect changes the sound a lot; a short crossfade would need one block of lookahead (one more block of latency), so it's an option, not a default.
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
  - A relay that isn't being called counts as a missing block (silence or bypass, never a repeated stale buffer), and the user is told.
  - **"Audio: running / not running"** (last audio block more than 500 ms ago, checked 4 times a second) shows on the panel's Panel tab and in the relay's window; it caught smart disable in every test. To be restyled as a graphic element later.

## Bay link: the audio round-trip test (2026-10-02)
Built for 1b.4; FL check results at the end of this section (passed, with the re-check of two fixes). Status and next step: `docs/current.md`.

### Decided with the user
- **Names:** *receiver mode* (the bay's input side, before the FL plugin) and *transmitter mode* (after it). The mode is set in the relay's own window and saved with the relay.
- **Live rig:** if FL's plugins aren't replaced by the time the live rig is built, it runs FL on the Mac with the panel as a plugin. Image-Line selling its flagship plugins as standalone VSTs again would be ideal; otherwise a slow migration off FL (and off any plugin tied to a DAW).
- **Latency is paid per bay in series:** one round trip (1 to 3 blocks) each. FL plugins in a row share one bay; bays on parallel branches cost one round trip between them. At 48 kHz, 1 block is 2.7 ms at 128 samples, 5.3 ms at 256, 10.7 ms at 512. Several serial bays at a large buffer aren't usable live; the FL check shows what's achievable.
- **Fallbacks if the numbers are bad:**
  - *Wired receiver* (user's idea): any wire into a receiver's input puts it in Patcher's second column, after the panel, so the order is guaranteed (1 block works). The panel could then send the bay's audio and the control values over that wire, leaving only the transmitter's jump back wireless. A generator's added outputs are scaled by FL (×0.7792467), so that path needs the gain correction.
  - *Zero-latency segments* (agent's idea): split the panel's chain at each bay into wired panel instances (part 1 → FL plugin → part 2), which isn't a loop. Live use only, and only if the parts reassemble into one interface: the product's point is centralised control.
- **Direct output is delayed by the round trip** (for now; to be re-evaluated): the latency told to the host is the same in both switch positions, switching doesn't move the sound in time, and bypass uses the same delayed copy.
- **Extras built:** the "return vs sent" difference readout, the lag finder, a test that loads the real relay file.
- **macOS finder: stub only.** The way: list the loaded images (`_dyld_image_count`, `_dyld_get_image_name`), take the relay's with `dlopen (path, RTLD_NOLOAD)` (a reference, never a load), then `dlsym`.

### The hub (`source/link/`)
- **Plain C interface** (`LinkApi.h`, version 1): open or close a role on a named channel, writer details, write, read, info. `heartseeker_link_get_api (version)` returns null for any other version; callers also check `struct_size` and every function pointer.
- **Channels:** up to 64, named (printable ASCII, 1 to 63 characters), never removed, so no handle can point at freed memory. Each holds 65,536 stereo frames (1.4 s at 48 kHz; 512 KB, allocated when the name is first used). Roles: one writer, one reader, any number of observers (info only; observers never create a channel). Handles carry a claim number: closed or made-up handles are refused.
- **Audio carries its position** (a sample count on the panel's timeline). A read gets exactly the frames written at those positions, or a failure (`NOT_YET`, `GONE`) and silence: never stale or mixed audio. A writer skipping ahead leaves a gap that never arrives (only the latest gap is remembered); going back, new writer details (a new session) or a new writer start afresh, and the old run is unreadable from that moment.
- **Lock-free on audio threads:** write, read and info take no locks and allocate nothing; open, close and details use a mutex, on message threads only. Frames are relaxed atomics; a sequence counter (odd while the run changes shape) and an overwrite horizon let a reader detect anything that changed while it copied.
- **Untrusted input:** every argument is checked (names, roles, handles, positions, blocks of up to 16,384 frames, channel counts; the sample rate by its bits, since fast-math builds can't test for NaN).
- **The reader's last read** (position, length, complete or not) is published as one word; the transmitter stamps the return with it.

### Timing
- **The panel sets the round trip** (Settings: 1, 2 or 3 blocks of the host's largest block; default 2) and tells the host it, plus the engine's latency. It reads the return at its own position minus the round trip, so the delay is fixed by design: FL's processing order can only cause missing blocks, never drift.
- **The receiver** plays from `max (block, round trip − block)` behind the panel's newest audio, chosen when it links or the panel's session changes. After a miss it plays silence for that block (never a repeat) and lines up again where either processing order is safe. So 2 blocks work in any order, with at most one silent block inside the bay after linking (the panel's return isn't affected: the stamps heal it); 3 never miss.
  - **1 block** works only if the receiver runs after the panel (a wire into it). The receiver then plays exactly the block the panel has just written (its lead is the current block's size, since FL's blocks vary), and skips ahead to it whenever it finds itself behind (e.g. a wire added after linking). *(Fixed after the FL check: it first used the announced largest block, too far behind with FL's smaller, varying blocks.)*
- **The transmitter** stamps the FL plugin's output with the position the receiver played in the same cycle, and never re-sends positions it already returned.
- **A panel's link timeline only moves forward,** past anything an earlier panel left in the channels. Its delay line keeps its own count, so the direct sound never jumps when the link does.
- **Missing blocks count only in a flowing return** (the transmitter wrote within the panel's last 2 blocks), after a warm-up of 5 blocks; a stopped side (smart disable, a removed relay) shows in the status instead. Relays count their own misses the same way.
- **Linking:** once prepared, the panel takes the first free "Panel n" (n up to 8). Relays scan Panel 1 to 8 four times a second and join the first panel whose role is free; a relay whose panel has gone drops the link and scans again. Channels: "Panel n/bay 1/send" and "Panel n/bay 1/return" (`BayNames.h`).
- **Finding the hub (Windows, `RelayHubFinder`):** `GetModuleHandleExW (0, L"heartseeker relay.vst3")` finds the file only if it's already loaded, and holds it; then `GetProcAddress` and the version checks. Held until the panel is destroyed, after its link has closed.

### Diagnostics
- **Panel tab:** link state (or why not), relays (linked; playing or returning), round trip (samples, ms, blocks) and the latency told to the host, the output switch, missing blocks, headroom (samples of returned audio to spare: now, lowest), block sizes, return vs sent (largest difference now and since reset; 0 is identical), lag (on demand: exact match, or closest shape within −1,024 to +32,768 samples, flagged "unclear" when another delay fits within 1 percentage point, as a held note's repeating waveform does; a near match never shows as 100%), silenced samples (NaN or infinity) and oversized blocks, reset. **Settings tab:** missing blocks (silence or bypass), round trip.
- **Relay window:** mode, link state, round trip, missed blocks (with the last reason), headroom (receivers), block sizes, running, input peak, reset.

### Tests (`[link]`, `[relay]`, `[relayfile]`)
- Hub rules, bad arguments, and a two-thread stress test (no torn reads).
- A simulated Patcher (panel, receiver, FL plugin, transmitter) in both orders and random ones, with constant and varying block sizes: the return is exact, sample for sample, a fixed round trip late. Adding, removing and reloading pieces in any order never plays stale or misplaced audio (checked in bypass, where every sample must be exact).
- The real built relay file, hosted as FL hosts it: the panel finds its hub, and an exact round trip runs through two hosted relays.
- Bugs the tests caught, fixed: a new panel session let a receiver play one block of the previous session's audio; a receiver lining up again could make the hub drop return audio the panel still needed.

### Open
- Measuring a bay plugin's own latency for bypass alignment (the lag finder measures it by hand for now); the macOS finder; bays inside the panel's routing graph; FL's plugin bridging (a separate process shows "not linked"). *(The bypass crossfade was built with the control outputs.)*

### FL check (2026-10-02, 48 kHz, buffer length 288)
- **Passed:** the link alone (exact, identical, lag 0); an FL plugin in the bay at 2 and 3 blocks; 0 missing blocks over minutes at 2 and 3 blocks (the receiver missed none); adding, removing, reloading, saving and reopening, second relays and panels, smart disable, closing FL; bypass on missing blocks; FL's linear-phase EQ 2's latency measured exactly as FL reports it (up to 6,144 samples).
- **FL's blocks:** plugins are called with blocks of 1 to 215 samples whenever the buffer length is above 215 (at or below it, up to the buffer length), while FL announces the buffer length as the largest block. The round trip is in announced blocks, so it has spare room (headroom: just under one buffer at 2 blocks; the receiver about two buffers at 3).
- **Failed, fixed:** 1 block, with or without a wire (or any plugin) into the receiver: missing blocks kept rising (silence, or crackle when the wire was added after linking). Cause: the receiver lined up one *announced* block behind the panel, too far for FL's smaller, varying blocks. Fixed as in "Timing"; tests now cover 1 block with varying blocks and a wire added later. **FL re-check passed 2026-10-02:** 1 block works with a wire into the receiver (also when added after linking) and still fails without one.
- **Confusing, fixed:** lag readouts of about +1,400, +2,900 and +7,300 (99.86%) with a filter in the bay, and 6,417 "100% similar" for the EQ's highest linear-phase setting (FL: 12,288). Causes: a held note repeats every cycle of its waveform, so many delays fit almost equally; and the search stopped at +8,192. Now it searches to +32,768 and says when the answer is unclear. **FL re-check passed** (12,288 measured; held notes show "unclear").
- **Pops:** switching Direct and Bay return gives a faint pop when the bay changes the sound (the open crossfade); changing the round trip a louder one (the output jumps in time by a block), and 3 → 2 blocks misses 2 or 3 blocks while the receiver lines up again. Silence and bypass switching is clean. Expected for now.
- **Latency changes** reach FL at once, playing or not.
- **Buffer length changes** (e.g. 512 → 2048) can miss up to 5 blocks, audibly; expected while the audio restarts.
- **Mixer inserts:** relays around an effect on a mixer insert return its audio to the panel inside Patcher, as long as they aren't downstream of that Patcher (FL silences what it sees as a loop). So relays can also act as sends that take audio out of a Patcher: a wanted utility. A bridged relay unlinks only itself.

## Bay control outputs, automatic round trip, bypass (2026-10-02)
The rest of 1b.4, built after the link test. Status, next step and the FL checklist: `docs/current.md`.

### Decided with the user
- **16 mono control outputs per relay** ("Control 1" to "Control 16"). If that's too few: more later, or several relays serving one bay.
- **The panel's bay controls are parameters like any exposed one:** sliders in the catalog (slot "Bay 1"), bound to host controls after the exposed parameters (now Param 2 to 17), so FL can automate them and morphs can drive them later. Control *k* plays on the receiver's output *k*; choosing and naming them in the UI comes later. Their values are saved with the project.
- **Relays hold the last values** through missed blocks, without a link, and without a panel, and save them with their state, so knobs don't drop to 0 when a project opens before linking. Values are settings, not audio: the "never stale" rule doesn't apply to them. A transmitter's control outputs are silent.
- **Values cross the hub as value sets**, not as one channel per parameter (64 channels at 512 KB each would run out) and not as extra audio lanes (about 4 MB per bay for values that only change per block). Both give the same signal on the outputs.
- **A timing probe** (see below), so the Peak Controller's delay is measured rather than guessed.
- **Renames:** the panel setting for missing return blocks is now **dropouts: Safe / Silence**, Safe the default (it plays the sent audio, so only the bay is skipped). The output switch is now **Bypass** (on: the sent audio, off: the return): in a reorderable graph it's the bypass of a bay-fed slot. Projects saved with the old switch load as before ("direct" is bypassed).
- **Round trip default: Automatic** (below). The user's concern: 2 blocks per serial bay at large buffers may make complex patches unplayable live.
- **A bay plugin's own latency (L) must be handled soon, and automatically,** before slots can be moved: no manual values to re-enter after every change. It also belongs in the latency told to FL (FL can't compensate a plugin hidden behind the link) and, later, in the graph's own compensation of parallel branches.

### Values in the hub (version 2)
- **Value sets:** up to 64 values stamped with the position from which they apply, until a later set. A channel keeps the last 256 (about 72 KB, allocated with the channel). A read gets the set in effect at a position: the newest written at or before it; NOT_YET before the first, GONE once overwritten. A read that fails changes nothing.
- Sets outlast sessions (new writer details), since the panel's timeline only moves forward; a new writer, or a set before the newest, starts them afresh (an epoch number). Each set has its own sequence counter, so a read that overlaps a write is dropped and retried. Values must be finite. Readers and observers may read values (several relays could serve one bay later).
- `hs_link_info` also counts each role's claims, so the panel sees when a relay relinks. The panel refuses a version-1 hub ("a different version").

### Timing and resolution
- The panel writes a set when a value changes (and at the start of each link), stamped with that block's position. The receiver plays, for each block, the set in effect at the block's last sample: one steady level per block (what the Peak Controller reads exactly), in step with the audio, up to one block early and never late.
- Resolution: one value per panel block (FL: blocks of up to 215 samples, about 4.5 ms at 48 kHz). Whether the Peak Controller and FL's link to the knob can use more, and how quickly they follow, is unknown: the probe measures the delay and the rise time. *(Corrected: the agent first said the Peak Controller only sees a block's peak; that was a guess.)* If finer steps help, a set could carry a ramp later.
- Hosted plugins get parameter changes at block starts too (JUCE's VST3 hosting); finer automation there means splitting blocks in the engine, a later option.
- A "control lead" (sending values a block early to cancel a steady Peak Controller delay) waits for the probe's numbers.

### Automatic round trip
- **Neither side can tell FL's processing order directly:** without a shared cycle count, the panel-first and receiver-first orders look the same from positions alone. What shows is whether 1 block closes. So Automatic tries 1 block whenever a relay links (a new relay, or one switching modes), and falls back to 2 at the first missing block once the return has flowed for a few blocks at that round trip. A second of returned audio without a miss settles it on 1 block. The choice holds until the relays link again.
- A wire from the panel into the receiver puts the receiver in Patcher's next column, so 1 block works whatever the panel's own graph looks like. Each serial bay costs at least 1 block with one panel instance; parallel bays share it.
- To try 1 block again after adding the wire: switch the receiver's mode to Transmitter and back (a panel-side "check again" button would be cheap if wanted). A latency change can pop once, at link time.
- **Idea, needs an FL check:** FL calls plugins with blocks of at most 215 samples even at larger buffer lengths. If that always holds, the round trip could count real blocks rather than announced ones (215 instead of 512 at a 512 buffer).

### Bypass crossfade and dropouts
- **The bypass switch crossfades** (a plugin-wide setting: off, 10, 20 or 50 ms, default 20). Both signals exist at the same position, so this costs no latency. Without the bay plugin's latency L the blend can sound phasey; it waits for L.
- **Dropouts in safe mode** cut to the sent audio at once (fading out of a dropout would need a block of lookahead), and **fade back into the return** over the same crossfade, which is free. Silence mode switches at once both ways, so dropouts stay plain to hear.

### Probe
- "Control timing > Probe" on the Panel tab: the panel sends a steady tone (fs/4, every sample the same size, level 0.25) instead of its sound, mutes its output, holds control 1 at 0 for 0.5 s, steps it to 1, and compares the return's level per sample: 0.25 s before the step against the last half of the second after it. It reports the delay (step to halfway) and the rise (10% to 90%) in samples, ms and blocks. Control 1 must drive a level knob in the bay (e.g. through a Peak Controller to Fruity Balance's volume). Afterwards control 1 returns to its value.

### A bay plugin's latency: candidates (open)
- **Position marker (needs an FL experiment):** the receiver sends a coded position on an extra output, wired straight into a sidechain input on the transmitter, beside the FL plugin. If Patcher compensates latency between paths that merge, the marker arrives L late, and the transmitter knows exactly which position its audio belongs to, every block, following any change of L. Costs a wire per bay and an input on the relay. The experiment needs no new code: the receiver wired into both a linear-phase EQ 2 and the transmitter's input; Lag > Measure showing one path at +L means Patcher compensates (two paths, at 0 and +L, mean it doesn't).
- **Fallback:** the active Lag > Measure, run when needed (or on a schedule), plus the passive comparison in the background.

### Tests
- Hub: value sets by position, fewer values than a set holds, the ring running out, going back, a new writer, claim counts, bad arguments, and a two-thread test (no torn sets).
- Simulated Patcher: values exact at the right positions in every order (a level knob in the bay shows the change at exactly the position it was made), steady within every block with FL-like varying blocks, held without a panel and through a saved state; Automatic settles on 1 block when the receiver runs after the panel, falls back to 2 otherwise, and tries 1 again when the receiver switches modes; the bypass crossfade and the fade back from a dropout follow the expected ramp; the probe measures a knob reacting at once (0) and a block late (512).
- The real relay file puts the panel's exact value on its control output.

### FL check (2026-10-02) and fixes
- **Passed:** exact values (to FL's 0.1%), several controls at once, automation on Param 2, holding without a panel and through save and reopen, silent control outputs in transmitter mode.
- **Probe (Peak Controller):** at every buffer length tried (64 to 2,048), the same numbers, so the delay is the Peak Controller's own, not FL's buffer:
  - Ramp off: 300 to 550 samples from the step to halfway (mostly near 310 or 510), 10–90% in 166 to 173.
  - Ramp on: 1,550 to 1,750 (mostly high 1,500s), 10–90% in 1,020 to 1,130.
  - The two clusters about 200 samples apart suggest it updates its knob once per FL internal block (at most 215). A guess, not measured.
  - **Control lead: not by default.** Values can't reach the receiver before the panel computes them, so cancelling about 400 samples (8 ms at 48 kHz) means delaying the bay's audio by the same amount: more latency per bay. A possible setting later.
  - The probe cut the output abruptly at its start and end (pops). **Fixed:** the output fades out over 20 ms before the probe starts, and back in after.
- **Automatic round trip didn't work. Cause:** the panel counted a missing return block only if the transmitter had written in the last 2 blocks, after 5 blocks of warm-up. With FL's blocks (announced 512, actually up to 215), the panel's own return had room to spare at 1 block in either order, so it only missed where the receiver had missed; but each receiver miss made the transmitter skip blocks, which reset the warm-up, so those misses were never counted. The panel showed 0 while the relays counted misses, Automatic never fell back, and a check could wait forever. **Fixed:**
  - "Stopped" is now measured in time (no new return for 0.1 s), and the warm-up in samples (the round trip plus 2 announced blocks), so a stop-and-start return still counts its gaps.
  - The hub counts the reader's failed reads; the panel shows them ("Receiver missed") and Automatic counts them as misses too.
  - Automatic keeps watching while at 1 block: any miss on either side falls back to 2 (e.g. the wire removed later). It ignores the first 0.25 s after a round trip or block size change, while the relays line up again.
- **Bypass crossfade:** 20 ms clean, 10 ms a slight pop, off a bad one, 50 ms audible as a crossfade on overt effects. **Now a number field, 0 to 50 ms, default 30** (scrubbing comes later as an input primitive).
- **Pops left (polish, not now):** removing or adding relays, cutting the wire into the receiver or the receiver's output, and the first audio from a new transmitter instance can pop. A dropout can only start abruptly without a block of lookahead, and some come from FL's own rewiring (industry plugins pop there too). Removing them all would beat the norm; a later polish item.
- **Lag finder: unsuited to measuring a bay plugin's latency.** It needed audio already playing, finished in a fraction of a second, read held or tonal sound as "unclear" (12,288 vs ±6,144 for EQ 2), and couldn't separate the two paths of the merge experiment. **Replaced (with the user's go, same day):**
  - **Lag > Measure is now active.** It fades the output out, sends a known noise burst (8,192 samples, level 0.25, the same every time) instead of the panel's sound, records the return from 1,024 samples before the burst to 32,768 after its end, then fades back in. Nothing needs to be playing.
  - It correlates the capture with the burst (through the FFT, `juce::dsp`), and reports every delay at which the burst comes back with at least 20% of the strongest path's strength, strongest first (at most 4; ringing within 16 samples of a path isn't a path), each with its gain (1: as loud as sent). An unchanged copy is flagged "exact". Below a gain of 0.1 it says "didn't come back recognisably"; too little returned audio says so.
  - Noise doesn't repeat, so there's no "unclear" any more. A linear-phase EQ's path lands on its latency. The merge experiment reads directly: **one path at +L** means Patcher compensates paths that merge; **two paths (0 and +L)** mean it doesn't.
  - The probe and the measurement share the machinery (one at a time; starting one cancels the other). The passive "return vs sent" readout stays, and pauses while a test signal is on its way.
