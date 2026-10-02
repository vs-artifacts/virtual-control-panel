# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## About This Project

This project is derived from the [Pamplejuce](https://github.com/sudara/pamplejuce) template — a JUCE audio plugin template using CMake, C++23, and modern CI/CD. It builds cross-platform (macOS, Windows, Linux) with support for multiple plugin formats (VST3, AU, AUv3, CLAP, Standalone).

The template provides the build system, CI/CD, and project structure. The plugin-specific logic lives in `source/`.

**What we're building:** a live-performance plugin host (VST3 instrument now, standalone app later). It is essentially a patcher (any number of generators, buses and FX slots; any-to-any routing including sidechains) with the user's own glassmorphism UI on top. It exposes only chosen parameters of each hosted plugin and morphs between stored parameter sets. Full brief and its architectural implications: "Target production form" in `docs/decisions.md`.

## Working Agreement (read first)

- **Never make code or file changes without explicit confirmation from the user.** For discussion questions, answer and propose a plan (files touched, approach, rough size), then wait for a go-ahead.
- **Ask clarifying questions whenever anything is ambiguous.** A cheap extra turn beats a wrong large implementation.
- Work in small vertical slices. Each slice ends with: warning-free build, passing tests, and a visual check (see Visual Verification).
- One chat session per feature. Keep this file and `docs/` current so a fresh session can pick up without the chat history: record decisions in `docs/decisions.md`, keep the index below accurate.
- The user has professional technical writing/editing experience in software and knows the concepts, but has very limited hands-on dev experience in any language (including Svelte and C++). Explain code and concepts plainly when relevant.

## Security (networked surfaces)

- **Be scrupulous, even a little paranoid, about anything networked.** The risk that matters is remote code execution and lateral movement onto the user's machine and network (assume attackers with LLM-assisted bug hunting), not someone changing the sound or crashing the DAW. Unoptimised UI code is acceptable; an unnecessary or unhardened attack surface is not.
- **No network exposure by default.** The plugin editor talks to the engine through JUCE's native web bridge; the WebSocket server runs only when remote control is explicitly switched on.
- Keep code reachable before authentication to a minimum, use third-party network libraries minimally and pinned to patched versions, and validate every incoming message (size and nesting depth before parsing).
- Raise security concerns proactively, even when not asked. Concrete rules are recorded in `docs/decisions.md`.

## Project Docs (source of truth)

Read only the files relevant to the task at hand.

- `docs/decisions.md` - Decisions made so far and why (platforms, performance targets, rendering approach, workflow). **Read this at the start of every session.**
- `docs/architecture.md` - *(not yet written)* Signal flow, modules, parameter table (ID, range, default, visual mapping).
- `docs/style/glass.md` - *(not yet written)* Glass material system: layering, blur modes, blend modes, light scatter model.
- `docs/style/tokens.md` - *(not yet written)* Named visual constants; may be superseded by the live-reloading JSON token file.
- `docs/components/<name>.md` - *(not yet written)* One file per UI component: geometry source, states, value-to-visual behaviour.

SVG assets from the user go in `assets/` (auto-embedded as BinaryData). SVGs are geometry only (named layer ids, text outlined, no filters); all styling is done in code. JUCE's SVG support is partial.

## Build Commands

Windows + VS Code (CMake Tools, Ninja, MSVC from Visual Studio 18). Project lives at `C:\dev\virtual-control-panel`. Build dir is `Builds/`, Debug by default.

In VS Code, select the "Visual Studio Community 18 - amd64" kit in CMake Tools so Ninja can find `cl.exe`. From a plain terminal, load the dev shell first:

```powershell
# Load MSVC + cmake + ninja into the shell (once per terminal)
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location C:\dev\virtual-control-panel

# Configure (run once, or after CMakeLists.txt changes)
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build Builds

# Run tests (from project root)
ctest --test-dir Builds --verbose --output-on-failure

# Or run tests directly
./Builds/Tests

# Run a single test by name
./Builds/Tests "[test name]"

# Run benchmarks
./Builds/Benchmarks
```

Build outputs land in `Builds/HeartseekerPanel_artefacts/<Config>/` (`Standalone/heartseeker panel.exe`, `VST3/heartseeker panel.vst3`, `CLAP/heartseeker panel.clap`). The relay plugin builds to `Builds/HeartseekerRelay_artefacts/<Config>/VST3/heartseeker relay.vst3` (VST3 only). `COPY_PLUGIN_AFTER_BUILD` is off on Windows (the system VST3 folder needs admin rights). Instead, `C:\Program Files\Common Files\VST3\heartseeker panel.vst3` is a directory junction to the Debug VST3 artefact, so rebuilds are picked up by DAWs automatically (FL Studio only scans VST3s in the standard folders; custom search paths are treated as VST2). Close the DAW before rebuilding, or the linker can't overwrite the loaded binary. The relay needs the same kind of junction, created once from an elevated terminal: `New-Item -ItemType Junction -Path "C:\Program Files\Common Files\VST3\heartseeker relay.vst3" -Target "C:\dev\virtual-control-panel\Builds\HeartseekerRelay_artefacts\Debug\VST3\heartseeker relay.vst3"`.

For performance testing or real use in a DAW, build Release in a separate dir: `cmake -B Builds-Release -G Ninja -DCMAKE_BUILD_TYPE=Release; cmake --build Builds-Release`. Parameter sync and morph cost: `./Builds-Release/Benchmarks "[sync]"` (needs Serum 2).

Intel IPP is optional and not installed locally; the "INTEL IPP NOT LOADED" configure message is expected.

The build also builds the web UI, so Node.js and npm must be on the PATH. The first build in a build dir runs `npm install` in `ui/` (install scripts are disabled in `ui/.npmrc`); every build that touches `ui/` runs `npm run check` (type check, warnings fail) and `npm run build` into `Builds/ui/`. On Windows, configure downloads the pinned WebView2 SDK into the build dir (`cmake-local/WebView2Sdk.cmake`). To work on the page alone: `cd ui; npm run dev` (it shows "Bridge unavailable" outside the plugin).

Test runs print `Thrift: ... TPipe::open ... errno = 2` lines: that's the hosted Kilohearts test plugin looking for its background service. Harmless.

Hosting tests load Serum 2, Sylenth1 and kHs 3-Band EQ from `C:\Program Files\Common Files\VST3` and skip if they're missing. A full test run takes ~20 s; the "Boot performance" benchmark ~90 s.

On macOS for universal binary: `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`

## Project Structure

- `source/` - Plugin shell code (PluginProcessor, PluginEditor: the web view filling the window, the page's handlers, and resizing from the page's corner grips, `HostedEditorWindows`: hosted plugins' own editors in separate windows, `EditorScale`: design size, limits and sharp steps, `WebUi`: the editor's locked-down web view and its native bridge, `WebViewKeepAlive`, `KeyForwarder`: typing keys from the page to the host (with FL's Delphi key path), `PluginSettings`: plugin-wide preferences file, `HostControls`: the fixed pool of parameters the host sees, bound to exposed parameters, `HostMenus`: the right-click menu for a parameter, with the host's options as a submenu, `EditorPlacement`: centres the editor's window on first open)
- `source/engine/` - Shell-independent engine code: `Engine` (the routing graph that runs everything, plus which hosted parameters are exposed) and `HostedPlugin` (one plugin slot in it)
- `source/bridge/` - Shell-independent web UI plumbing: `UiFiles` (the embedded page files, looked up by exact path), `ControlSession` (one page's view of the exposed parameters: catalog, updates, validated sets and gestures)
- `source/relay/` - The relay node, a separate small plugin (CMake target `HeartseekerRelay`, VST3 effect): `RelayProcessor`, `RelayEditor` (plain JUCE window, no web view). Kept out of `SharedCode`; its own `RelayCode` library feeds both the relay target and Tests, except `RelayEntry.cpp` (the plugin entry point, relay target only). No `JucePlugin_` macros in relay code: Tests is compiled with the panel's.
- `tests/` - Catch2 test files (`tests/helpers/ParameterHost.h`: a bare processor with parameters, plus a log of what a host would hear)
- `benchmarks/` - Catch2 benchmark files (each `.cpp` is included from `Catch2Main.cpp`)
- `docs/` - Project specs and decisions (see Project Docs above)
- `ui/` - Svelte 5 + Vite + TypeScript web UI (WebGL2 shaders later). Built by CMake and embedded as the `UiBundle` target
- `cmake/` - Pamplejuce CMake modules (Tests.cmake, Benchmarks.cmake, Assets.cmake, etc.); a git submodule, never edit
- `cmake-local/` - Our own CMake modules (WebView2 SDK, web UI build)
- `modules/` - Git submodules: clap-juce-extensions, melatonin_inspector
- `JUCE/` - JUCE framework (git submodule, JUCE 9.0.1+ on master). Never edit files in `JUCE/` or `modules/`; they are marked read-only in VS Code.
- `assets/` - Binary resources (auto-included via juce_add_binary_data)
- `packaging/` - Installer resources and scripts

## Architecture

**SharedCode Library**: The `SharedCode` INTERFACE library links plugin source code to both the main plugin target and the Tests target, avoiding ODR violations.

**CMake Modules** (`cmake/` is the upstream Pamplejuce submodule; never edit it):
- `PamplejuceVersion.cmake` - Reads VERSION file, optional auto-bump patch level
- `PamplejuceFunctions.cmake` - One include that defines the `pamplejuce_*` functions below (and turns off their legacy include-time behavior)
- `PamplejuceIPP.cmake` - Intel IPP integration (optional)

Our own modules live in `cmake-local/`:
- `WebView2Sdk.cmake` - Windows: downloads and checksum-verifies the WebView2 SDK that JUCE links statically
- `Ui.cmake` - Builds `ui/` with npm and embeds the result as `UiBundle` (its file list must match what Vite produces)

Target setup happens via explicit function calls in `CMakeLists.txt`, not at include time:
- `pamplejuce_add_assets()` - Includes all files in assets/ as binary data (from `Assets.cmake`)
- `pamplejuce_add_tests()` - Configures the Catch2 test target (from `Tests.cmake`)
- `pamplejuce_add_benchmarks()` - Configures the Catch2 benchmark target (from `Benchmarks.cmake`)
- `pamplejuce_shared_code_defaults()` - C++23, fast math (from `SharedCodeDefaults.cmake`)
- `pamplejuce_xcode_prettify()` - Xcode folder/scheme cleanup (from `XcodePrettify.cmake`)

Note: after `include(PamplejuceFunctions)`, a plain `include(Tests)` (or Assets, etc.) is a no-op - call the function instead.

**Test Discovery**: Uses `catch_discover_tests()` with `PRE_TEST` discovery mode for Xcode compatibility.

## Key Configuration

Edit `CMakeLists.txt` to customize:
- `PROJECT_NAME` - Internal name (no spaces)
- `PRODUCT_NAME` - Display name in DAWs (can have spaces)
- `COMPANY_NAME` - Used for bundle name
- `BUNDLE_ID` - macOS bundle identifier
- `FORMATS` - Plugin formats to build (currently `Standalone VST3`; CLAP is added separately via `clap_juce_extensions_plugin`)
- `IS_SYNTH` - on (instrument: FL only sends notes to generators)
- `NEEDS_MIDI_INPUT` / `NEEDS_MIDI_OUTPUT` - on; `JUCE_PLUGINHOST_VST3=1` enables VST3 hosting
- `PLUGIN_MANUFACTURER_CODE` / `PLUGIN_CODE` - 4-character plugin IDs

Version is read from the `VERSION` file in project root.

## Code Quality

Always resolve any compile warnings encountered during builds. Warnings should be treated as errors and fixed before considering a task complete.

Note: clangd reads `Builds/compile_commands.json` (configured in `.clangd`), so diagnostics in `source/` and `tests/` should be accurate. JUCE module internals (files compiled via JUCE's unity `.cpp` wrappers) may still show false positives; ignore those unless the actual build fails.

## Visual Verification

The agent can view PNGs. To check UI changes without the user, render the editor to an image via `runWithinPluginEditor` in `tests/helpers/test_helpers.h`: call `editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f)`, write it with `juce::PNGImageFormat` into `Builds/`, run `./Builds/Tests "[snapshot]"`, then view the PNG. Delete temporary snapshot tests and images afterwards. The whole UI (tabs included) is in the web view, so snapshots show little now. Opening an editor records its size in the user's real settings file (`lastEditorScale`), so tests that change the size must restore it.

Snapshots can't capture the web view (it's a separate native window; it shows as a white box). To see the page, serve the built copy on loopback with `cd ui; npx vite preview --outDir ../Builds/ui --host 127.0.0.1 --port 4173 --strictPort`, open it in VS Code's browser, and stop the server afterwards. Outside the plugin the page shows "Bridge unavailable"; to see it with data, fake the bridge with a Playwright init script that defines `window.__JUCE__` (`initialisationData.__juce__functions` plus a `postMessage` that answers `__juce__invoke` via `backend.emitByBackend('__juce__complete', …)`). That browser doesn't raise `contextmenu` from real right-clicks, and Playwright's hit testing is off under the page's CSS zoom: dispatch such events from script. The real bridge is covered by the `[webview]` tests, which load the page in an off-screen web view.

## Includes

JUCE modules include common standard library headers (`<vector>`, `<algorithm>`, `<string>`, `<memory>`, etc.) so you don't need to add those explicitly in JUCE code. Adding them is harmless but redundant.

## Threading Model

JUCE plugins have two main threads:

- **Audio thread**: Runs `processBlock` — must be realtime-safe (see below). Never block, allocate, or lock.
- **Message thread**: Runs UI callbacks, parameter listeners, and timer callbacks. Owns the `MessageManager`.

To communicate between them:
- **Simple values**: Use `std::atomic` or JUCE's `AudioParameterFloat`/`AudioParameterBool` (which are atomic under the hood)
- **Larger data**: Use a lock-free queue (e.g. `moodycamel::ReaderWriterQueue`) to pass data from message → audio thread
- **Audio → UI updates**: Use `juce::AsyncUpdater` or `juce::Timer` on the message thread to poll state — never call UI code from the audio thread

## Realtime Safety

For anything in the audio thread / hot DSP path (e.g. `processBlock`):
- Allocate in constructors or `prepareToPlay`, not while rendering audio
- Avoid dynamic allocations and container growth (`std::vector::push_back`, map insertion, string building)
- Prefer fixed-size storage (`std::array`, preallocated buffers, fixed-capacity queues)
- Keep operations deterministic and lock-free where possible

## Adding Dependencies

**JUCE Modules** live in `modules/` as git submodules. Add with `git submodule add`, then `add_subdirectory` and link to `SharedCode` in `CMakeLists.txt`. Some useful ones:

- [melatonin_inspector](https://github.com/sudara/melatonin_inspector) — runtime component debugger (already included)
- [melatonin_blur](https://github.com/sudara/melatonin_blur) — fast cross-platform blurs for C++ UI (shadows, glows, frosted glass)
- [melatonin_perfetto](https://github.com/sudara/melatonin_perfetto) — performance tracing with Perfetto, great for profiling `processBlock` and paint calls
- [gin](https://github.com/FigBug/gin) — large collection of utilities (DSP, UI components, LookAndFeel, etc.)

**Non-JUCE C++ libraries** should be added via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) which is already configured. CPM downloads and caches dependencies at configure time — no submodule needed:

```cmake
CPMAddPackage("gh:nlohmann/json@3.11.3")
target_link_libraries(SharedCode INTERFACE nlohmann_json::nlohmann_json)
```

Some useful CPM libraries:
- [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing/serialization
- [cameron314/readerwriterqueue](https://github.com/cameron314/readerwriterqueue) — lock-free single-producer/single-consumer queue, ideal for audio↔message thread communication

## Code Style

Uses `.clang-format` with Allman-style braces, 4-space indentation, no column limit.
