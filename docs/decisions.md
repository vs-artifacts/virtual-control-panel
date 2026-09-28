# Decisions

Short log of settled decisions. Newest at the bottom. Update when something changes; don't re-debate settled items without new information.

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
   - **1b.3. Plugin swap:** a button per slot in the Plugins tab that swaps the plugin from a copy of FL's plugin menu (its categories and favourites, plus "More plugins…" with everything installed), read from FL's plugin database (`Documents\Image-Line\FL Studio\Presets\Plugin database`) each time the menu opens so it follows FL's changes, and styleable like the rest of our UI later. Needs: swapping while audio runs, safe unbinding of host controls, saving each slot's plugin and the bindings with the project, and a rudimentary "Bind…" menu per host control (the real solution is each parameter item in a slot's UI linking to the exposed-parameters list). Lets us test automation across swaps.
   - **1b.4. External module bays:** in/out pins on our plugin that the host routes through its own plugins and back, because FL's native plugins can't be hosted by us (see "1b.2 wrap-up"). Needs an FL experiment first: how FL handles a loop through a plugin (refused, or at least one block of delay); the alternative is linked instances sharing one engine.
   - **1b.5. Remote server:** Crow WebSocket server behind a remote-control toggle, own update thread, security tests, "Open in browser" button.
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

### Remote server rules (1b.5 and later LAN mode)
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

### Protocol (native bridge now, WebSocket in 1b.5)
- **`ControlSession`** (`source/bridge/`) doesn't know the transport. `catalog` (native function) starts or restarts a session: `[{ h, slot, plugin, name, host, v, t }]`. `params` (C++ to page, 30 Hz, only while the editor is shown): `[{ h, v, t }]` for changes since the last one. `set` (page to C++): `[{ h, v }]`, at most one batch per frame. `gesture`: `{ h, active }`.
- **Validation:** arrays only, at most 4096 entries (else the whole batch is dropped), integer handles in range, finite numbers clamped to 0–1; everything else is ignored. Nothing is accepted before the page has asked for the catalog.
- **Open gestures end** when the page restarts the session or the session ends (editor closed), so the host never stays stuck in a gesture.
- **The page ignores incoming values for a slider while dragging it** (replies still on their way would make it jitter); it always takes the display text.
- **Accepted:** JUCE parses page messages with its recursive JSON parser before we can check their depth. Only our own page can send, and a compromised page already has full control. WebSocket input is checked before parsing (1b.5).
- **For 1b.5:** the session is message-thread-only because `poll` fetches display text; the remote server's own update thread needs values and text split. Two sessions on one control (editor + remote) need shared gesture bookkeeping: JUCE asserts on overlapping gestures.

### Keys on focused controls
- Sliders, checkboxes and buttons don't count as text entry, so forwarded keys still reach the host while they have focus. Text fields and selects keep their keys.
- **"FL default"** (the user's name for how other plugins behave in FL; match it, maybe make it configurable later): keys pass through while the mouse is pressed anywhere in the plugin. A key held when a text field gains focus keeps sounding in the host until released, while new keys go into the field; once it's released, no keys pass while a text field has focus. Our editor currently passes no keys during a mouse press anywhere in it (first pass: "1b.2 wrap-up").

### Editor placement
- **First open only,** per instance (a flag in the instance's saved state), so windows stay where the user puts them.
- **Plugin-wide setting** (`editorPlacement`, combo box in the editor): off / the monitor the host opened it on (default; safe for attached windows) / the monitor under the mouse / the monitor with the host's main window. Centres in the monitor's work area; a window bigger than the monitor keeps its top-left corner on screen.
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
- Not testable yet: a hardware controller link (none at hand), and what happens to automation when a plugin is swapped (no swap UI: 1b.3).
- Findings, handled in the wrap-up below: the web view scaled to the monitor (200%) while the JUCE part followed FL (100%); right-click showed the browser's menu instead of the host's options; no keys pass while the mouse is pressed anywhere in our editor.

## 1b.2 wrap-up (2026-09-27)

### Tabs
- **Panel** (the web view only), **Plugins** (the temporary plugin list and "Open window" buttons; the swap buttons come in 1b.3), **Settings** (every setting, the title and version, "Inspect the UI", and in Debug builds the key log).
- Each instance reopens on the tab it was closed at (saved with the instance); new instances open on Panel.
- The whole Plugins tab is temporary: it becomes part of the web UI on the Panel tab later, in our own style.

### Scaling and resizing
- **The page follows the plugin's scale, not the monitor's.** The web view renders at the monitor's scale; the host decides the plugin window's (FL keeps plugins at 100% on a 200% monitor). C++ sends the page its scale in physical pixels per pixel (`display`), and the page zooms its root by that ÷ its own pixel ratio. Anything that measures pixels in the page (WebGL canvases later) must allow for that zoom.
- **The whole UI scales; nothing rearranges.** The editor's content is laid out at the design size (560 × 600) and scaled as a whole; the web view zooms to match. Fixed aspect ratio for now, until the full UI's aspect ratio is decided.
- **Resize from any corner.** JUCE handles in all four corners; the page draws its own bottom two, because the web view covers the JUCE ones there. Dragging a top or left corner also moves a floating window so the corner stays under the pointer (same "owned window" rule as placement); attached windows grow down and right. The host's own window frame may also resize the plugin; JUCE's VST3 wrapper then applies only the limits and aspect ratio, so those resizes are always smooth.
- **Limits:** 50% to 500% of the design size (500% should fill a 16k screen once the full panel is built and larger by default; windows stranded off screen can be recovered by any visible corner or FL's "bring windows back").
- **Resizing setting** (`resizing`): smooth, or sharp steps. Sharp keeps 1-pixel lines crisp, which needs a whole number of physical pixels per design pixel (monitor × host × ours), so the steps are 100%, 200%, … on a 100% monitor (133%, 200%, … on 150%). Other step options can be added later if needed.
- **Size memory:** saved per instance with the project, like the tab; new instances open at the last size used (`lastEditorScale`). "Reset size" returns to the design size.
- **Browser zoom is blocked** (Ctrl+wheel, Ctrl+plus/minus/0): it would fight the page's own scaling.

### Right-click menus
- **Our menu by default, listing the host's own options** for that parameter (FL's "Create automation clip", "Link to controller", …). JUCE fetches the host's menu for the parameter's host control (VST3 `IComponentHandler3`); C++ turns it into items the page draws in our style (`parameterMenu`), keeps each item's action, and runs it when the page reports a choice (`menuChoice`). Host-supplied text is shown through Svelte's escaping only; ids are checked against the current menu. It will carry our glass style later.
- **Setting "Use the host's UI for menus"** (`useHostMenus`, off by default): the host shows its own native menu instead.
- **Right-clicking anywhere else shows nothing** for now; the browser's own menu is always blocked.
- **Risk to check in FL:** if a host offers VST3 context menus but returns none for a parameter, JUCE dereferences a null menu (crash). Not guardable without changing JUCE.
- **Browser shortcuts blocked:** print, find, reload, save, view source, history navigation. Dev tools are already off in every non-Debug build (JUCE switches them off).

### Keys (first pass: diagnosis)
- **Releases always pass:** the page and C++ now pass on releases of forwarded keys even when the page never saw them go down (a note held since before the page had focus, FL default).
- **Experimental setting "Keep keyboard focus in the host"** (`keepKeyboardFocusInHost`, off by default): clicks in our editor no longer take keyboard focus (JUCE part), and clicks in the page outside text fields hand focus straight back to the host window. Hypothesis being tested: other plugins pass keys during a mouse press because clicking them never takes focus away from FL.
- **Key log** (Debug builds only; deleted once the fix is in): a thread-local message hook on the host's UI thread only (never system-wide) that records key and mouse-button messages, which window they went to, and which had focus and capture. It records **no key identities**, keeps its log in memory only (Settings tab, with Copy), and stops after two minutes or when the editor closes. Accepted with care because a key logger is exactly the kind of code that shouldn't linger.
- **Drags no longer end on blur** (focus moving away mid-drag would otherwise split one drag into many gestures); a new press ends any drag whose release never arrived.

### FL's own plugins can't be hosted
- It's the format, not their location: FL's own plugins (e.g. `Plugins\Fruity\Generators\FLEX\FLEX_x64.dll`) use FL's private plugin format, which only FL can host; Image-Line's kit is for writing plugins for FL, not hosting them. "FL Studio VSTi" (FL running as a VST2) can't be hosted either: JUCE hosts VST2 only with Steinberg's VST2 SDK, which is no longer licensed. Hence external module bays (1b.4).

### FL check (to do)
- **Scaling:** at 200% Windows scaling, the page and the JUCE part now match. Each corner resizes at a fixed ratio; top and left corners keep the corner under the pointer (detached windows); sharp steps; size remembered per instance and for new instances; "Reset size".
- **Tabs:** reopen on the last tab; new instances on Panel; switching tabs keeps the page loaded (no reload).
- **Right-click on the slider:** our menu lists FL's options and they work (e.g. "Create automation clip"); with "Use the host's UI for menus" on, FL's own menu appears. Right-click elsewhere: nothing.
- **Keys:** with the key log running (Settings → Start), follow the steps given in chat, first with "Keep keyboard focus in the host" off, then on; also compare with Serum 2 loaded directly in FL. Copy the log into the chat.
