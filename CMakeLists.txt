cmake_minimum_required(VERSION 3.25)

# This tells cmake we have goodies in the /cmake folder
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Our own modules (cmake/ is the upstream Pamplejuce submodule)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake-local")
include (PamplejuceVersion)

# Modern concise way to add dependencies to your project
include (CPM)

# Configures universal binaries and decides which version of macOS to support
include(PamplejuceMacOS)

# Couple tweaks that IMO should be JUCE defaults
include(JUCEDefaults)

# Change me!
# This is the internal name of the project and the name of JUCE's shared code target
# Note: This cannot have spaces (it may be 2024, but you can't have it all!)
# Worry not, JUCE's PRODUCT_NAME can have spaces (and is what DAWs display)
set(PROJECT_NAME "HeartseekerPanel")

# Worry not, JUCE's PRODUCT_NAME can have spaces (and is what DAWs will display)
# You can also just have it be the same thing as PROJECT_NAME
# You may want to append the major version on the end of this (and PROJECT_NAME) ala:
#   set(PROJECT_NAME "MyPlugin_v${MAJOR_VERSION}")
# Doing so enables major versions to show up in IDEs and DAWs as separate plugins
# allowing you to change parameters and behavior without breaking existing user projects
set(PRODUCT_NAME "heartseeker panel")

# Change me! Used for the MacOS bundle name and Installers
set(COMPANY_NAME "heartseeker")

# Change me! Used for the MacOS bundle identifier (and signing)
set(BUNDLE_ID "com.heartseeker.heartseekerpanel")

# Change me! Set the plugin formats you want built
# Valid choices: AAX Unity VST VST3 AU AUv3 Standalone
set(FORMATS Standalone VST3)

# Force JUCE to work with sccache
# instruct MSVC to embed debug info (/Z7) instead of emitting a .pdb
set(CMAKE_POLICY_DEFAULT_CMP0141        NEW      CACHE STRING "" FORCE)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded CACHE STRING "" FORCE)

# For simplicity, the name of the CMake project is also the name of the target
project(${PROJECT_NAME} VERSION ${CURRENT_VERSION})

# Log build environment info (after project() so compiler info is available)
include(PamplejuceLog)

# Defines the pamplejuce_* functions we call below (also fetches Catch2 at include time)
include(PamplejuceFunctions)

# JUCE is setup as a submodule in the /JUCE folder
# Locally, you must run `git submodule update --init --recursive` once
# and later `git submodule update --remote --merge` to keep it up to date
# On Github Actions, this is done as a part of actions/checkout
add_subdirectory(JUCE)

# Add CLAP format
add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)

# Add any other modules you want modules here, before the juce_add_plugin call
# juce_add_module(modules/my_module)

# This adds the melatonin inspector module
add_subdirectory (modules/melatonin_inspector)

if (APPLE)
    set(COPY_PLUGIN_AFTER_BUILD TRUE)
else ()
    set(COPY_PLUGIN_AFTER_BUILD FALSE)
endif ()

# Windows only: the WebView2 SDK that JUCE's web browser component links against
include(WebView2Sdk)

# See `docs/CMake API.md` in the JUCE repo for all config options
juce_add_plugin("${PROJECT_NAME}"
    # Icons for the standalone app
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png"

    # Change me!
    COMPANY_NAME "${COMPANY_NAME}"
    BUNDLE_ID "${BUNDLE_ID}"

    # On MacOS, plugin is copied to /Users/yourname/Library/Audio/Plug-Ins/
    # Off on Windows: the system VST3 folder requires admin rights
    COPY_PLUGIN_AFTER_BUILD ${COPY_PLUGIN_AFTER_BUILD}

    # Change me!
    # A four-character plugin id
    # First character MUST be uppercase for AU formats
    PLUGIN_MANUFACTURER_CODE Hrtk

    # Change me!
    # A unique four-character plugin id
    # Note: this must have at least one upper-case character
    PLUGIN_CODE Hsp1
    FORMATS "${FORMATS}"

    # Instrument: FL only sends piano-roll notes to generators, and we host generators
    IS_SYNTH TRUE

    # Receive MIDI from the DAW and send MIDI back out (pass-through lane)
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE

    # The editor shows the web UI: WebView2 on Windows (linked statically), WebKit on Linux
    NEEDS_WEBVIEW2 TRUE
    NEEDS_WEB_BROWSER TRUE

    # The name of your final executable
    # This is how it's listed in the DAW
    # This can be different from PROJECT_NAME and can have spaces!
    # You might want to use v${MAJOR_VERSION} here once you go to v2...
    PRODUCT_NAME "${PRODUCT_NAME}")

# This lets us use our code in both the JUCE targets and our Test target
# Without running into ODR violations
add_library(SharedCode INTERFACE)

clap_juce_extensions_plugin(TARGET "${PROJECT_NAME}"
    CLAP_ID "${BUNDLE_ID}"
    CLAP_FEATURES instrument synthesizer)

# Enable fast math, C++23 and a few other target defaults
pamplejuce_shared_code_defaults()


# Manually list all .h and .cpp files for the plugin
# If you are like me, you'll use globs for your sanity.
# Just ensure you employ CONFIGURE_DEPENDS so the build system picks up changes
# If you want to appease the CMake gods and avoid globs, manually add files like so:
# set(SourceFiles Source/PluginEditor.h Source/PluginProcessor.h Source/PluginEditor.cpp Source/PluginProcessor.cpp)
file(GLOB_RECURSE SourceFiles CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h")
# The relay is its own plugin (below); the link hub lives only in the relay, the panel uses just its headers
list(FILTER SourceFiles EXCLUDE REGEX "/source/(relay|link)/")
target_sources(SharedCode INTERFACE ${SourceFiles})

# Adds a BinaryData target for embedding assets into the binary
pamplejuce_add_assets()

# Builds the web UI with npm and embeds it as the UiBundle target
include(Ui)

# MacOS only: Cleans up folder and target organization on Xcode.
pamplejuce_xcode_prettify()

# This is where you can set preprocessor definitions for JUCE and your plugin
target_compile_definitions(SharedCode
    INTERFACE

    # The editor's web UI; on Windows it uses WebView2 (never the old IE engine), linked statically
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0     # If you set this to 1, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
    JUCE_VST3_CAN_REPLACE_VST2=0

    # Lets the engine load and run VST3 plugins
    JUCE_PLUGINHOST_VST3=1

    # Uncomment if you are paying for a an Indie/Pro license or releasing under GPLv3
    # JUCE_DISPLAY_SPLASH_SCREEN=0

    # lets the app known if we're Debug or Release
    CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    VERSION="${CURRENT_VERSION}"

    # JucePlugin_Name is for some reason doesn't use the nicer PRODUCT_NAME
    PRODUCT_NAME_WITHOUT_VERSION="HeartseekerPanel"
)

# Link to any other modules you added (with juce_add_module) here!
# Usually JUCE modules must have PRIVATE visibility
# See https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md#juce_add_module
# However, with Pamplejuce, you'll link modules to SharedCode with INTERFACE visibility
# This allows the JUCE plugin targets and the Tests target to link against it
target_link_libraries(SharedCode
    INTERFACE
    Assets
    UiBundle
    melatonin_inspector
    juce_audio_utils
    juce_audio_processors
    juce_dsp
    juce_gui_basics
    juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# juce_add_plugin links WebView2 into the plugin only; Tests and Benchmarks need it too
if (WIN32)
    target_link_libraries(SharedCode INTERFACE juce::juce_webview2)
endif ()

# Link the JUCE plugin targets our SharedCode target
target_link_libraries("${PROJECT_NAME}" PRIVATE SharedCode)

# The relay node: a separate, small VST3 effect (no web view, no engine) that sits in FL around FL's own plugins
juce_add_plugin(HeartseekerRelay
    COMPANY_NAME "${COMPANY_NAME}"
    BUNDLE_ID "com.heartseeker.heartseekerrelay"
    COPY_PLUGIN_AFTER_BUILD ${COPY_PLUGIN_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Hrtk
    PLUGIN_CODE Hsr1
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx
    PRODUCT_NAME "heartseeker relay")

# The link hub: self-contained, compiled into every relay (the panel finds it there at run time, see RelayHubFinder)
add_library(LinkHub INTERFACE)
target_sources(LinkHub INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/source/link/LinkHub.cpp")

# Like SharedCode: the relay's code, for both its plugin target and the Tests target
add_library(RelayCode INTERFACE)
pamplejuce_shared_code_defaults(TARGET RelayCode)
file(GLOB_RECURSE RelaySourceFiles CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/relay/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/source/relay/*.h")
# The plugin entry point; Tests already has the panel's
list(FILTER RelaySourceFiles EXCLUDE REGEX "RelayEntry\\.cpp$")
target_sources(RelayCode INTERFACE ${RelaySourceFiles})
target_link_libraries(RelayCode
    INTERFACE
    LinkHub
    juce_audio_processors
    juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

target_sources(HeartseekerRelay PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/source/relay/RelayEntry.cpp")
target_compile_definitions(HeartseekerRelay PRIVATE JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_WEB_BROWSER=0)
target_link_libraries(HeartseekerRelay PRIVATE RelayCode)

# IPP support, comment out to disable
include(PamplejuceIPP)

# Everything related to the tests target
pamplejuce_add_tests()
target_link_libraries(Tests PRIVATE RelayCode)

# The built relay file, for the test that loads it as a host would and finds its hub the way the panel does
add_dependencies(Tests HeartseekerRelay_VST3)
target_compile_definitions(Tests PRIVATE HEARTSEEKER_RELAY_BINARY="$<TARGET_FILE:HeartseekerRelay_VST3>")

# A separate target for Benchmarks (keeps the Tests target fast)
pamplejuce_add_benchmarks()

# Output some config for CI (like our PRODUCT_NAME)
include(GitHubENV)
