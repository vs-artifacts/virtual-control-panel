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

## UI architecture
- **Svelte 5 + Vite + TypeScript** web UI in `ui/` (monorepo). Shown inside the plugin editor via JUCE's `WebBrowserComponent` (WebView2 on Windows, WKWebView on macOS) and on tablets/displays via their browser. Release builds embed the built UI.
- **Own WebSocket protocol from day one** for both the local editor and remote devices (one code path). Security: bind to `127.0.0.1` only until LAN mode exists; require a per-session random token on connect; check the `Origin` header (prevents other web pages on the same machine from connecting); serve only files from the UI bundle (no path traversal). LAN mode later: explicit opt-in, pairing code, consider TLS.
- **All glass visuals are rendered in WebGL2 with custom shaders**, not CSS. The panel scene is rendered to textures on a canvas; DOM only for things like text overlays where useful. WebGPU is a possible later upgrade; keep rendering code structured to allow it.
- Required shader capabilities (all available in WebGL2): explicit texture wrap modes (`CLAMP_TO_EDGE`) and/or manual UV clamping in shaders, render-to-texture passes with exact capture bounds, floating-point render targets (`RGBA16F` via `EXT_color_buffer_float`) so additive energy can exceed 1.0 before tone mapping, custom blend maths (additive, plus-lighter, dodge, etc).
- **Signature effect: "edge-clamped convolution blooming".** A thin bright border, when overlapped by a blurred additive element, blooms massively because the blur samples outside the captured bounds as the duplicated edge row (clamp, not transparent). Implement it deliberately (exact capture bounds + clamped sampling + HDR additive compositing), with controllable parameters (bloom gain, extent, per-edge), rather than depending on a browser artifact.

## Rendering / blur (historical)
- The JUCE-native `source/GlassToggleButton` prototype (parent snapshot + `applyGaussianBlurEffect`) was a workflow test only. It will be removed once the web UI exists; don't build on it.

## Workflow
- **Style tokens:** all visual constants live in one place, in the web UI (JSON/TS), with a debug tuning panel (sliders/colour pickers, copy/save). Vite hot reload gives live editing. (Replaces the earlier C++ JSON token plan.)
- **Specs:** split into small files under `docs/` (see index in `CLAUDE.md`), not one monolithic blueprint. Behaviour lives in each component's spec; global material rules in `docs/style/glass.md`.
- **Assets:** one SVG per component with named layer ids; states are usually parameters applied in code, not separate files. Separate state SVGs only when geometry actually morphs (matching path structure).
- **Iteration:** FL Studio / Patcher (via the VST3 junction) for in-DAW checks; web UI iterated in a browser with Vite hot reload where possible.
- **Platforms in practice:** build and test on Windows first; keep platform-specific code isolated; test the web UI in Safari early; smoke-test on a Mac at milestones.

## Plan: thin vertical slice (each sub-slice needs the user's go, ideally its own chat session)
1. **1a. Host inside the plugin:** our VST3 loads one VST3 from a fixed path (test plugin: `C:\Program Files\Common Files\VST3\Kilohearts\kHs 3-Band EQ.vst3`), passes audio and MIDI through, can open its editor window, lists its parameters. C++ only, no scanning.
2. **1b. Bridge:** embedded HTTP + WebSocket server (C++ library via CPM, to be chosen), plugin editor shows a minimal Svelte page; one plain slider controls one hosted parameter two-way, including pushed updates when the hosted plugin's own UI changes it.
3. **1c. Shader knob:** the slider becomes a WebGL2 glass/light-scatter knob with a frame-time readout, running inside FL.
- Style work starts after 1c.
