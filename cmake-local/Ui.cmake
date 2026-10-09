# Builds the web UI in ui/ with npm and embeds the result as the UiBundle binary data target.
# Vite writes fixed file names (see ui/vite.config.ts); UI_BUNDLE_FILES must list exactly what it produces.

find_program(NPM_EXECUTABLE NAMES npm.cmd npm REQUIRED)

set(UI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ui")
set(UI_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/ui")
set(UI_BUNDLE_FILES
    "${UI_BUILD_DIR}/index.html"
    "${UI_BUILD_DIR}/assets/index.js"
    "${UI_BUILD_DIR}/assets/index.css")

file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS "${UI_SOURCE_DIR}/src/*")
list(APPEND UI_SOURCES
    "${UI_SOURCE_DIR}/index.html"
    "${UI_SOURCE_DIR}/package.json"
    "${UI_SOURCE_DIR}/svelte.config.js"
    "${UI_SOURCE_DIR}/tsconfig.json"
    "${UI_SOURCE_DIR}/tsconfig.app.json"
    "${UI_SOURCE_DIR}/tsconfig.node.json"
    "${UI_SOURCE_DIR}/vite.config.ts"
    "${SETTINGS_DEFAULTS_JSON}")

# Once per build folder, and again when the dependencies change. `npm install` rather than `npm ci`:
# ci always deletes node_modules first, which fails on Windows while an editor has a package file open.
set(UI_INSTALL_STAMP "${CMAKE_CURRENT_BINARY_DIR}/ui-npm-install.stamp")

add_custom_command(
    OUTPUT "${UI_INSTALL_STAMP}"
    COMMAND "${NPM_EXECUTABLE}" install --no-audit --no-fund
    COMMAND "${CMAKE_COMMAND}" -E touch "${UI_INSTALL_STAMP}"
    WORKING_DIRECTORY "${UI_SOURCE_DIR}"
    DEPENDS "${UI_SOURCE_DIR}/package.json" "${UI_SOURCE_DIR}/package-lock.json" "${UI_SOURCE_DIR}/.npmrc"
    COMMENT "Installing web UI packages"
    VERBATIM)

add_custom_command(
    OUTPUT ${UI_BUNDLE_FILES}
    COMMAND "${NPM_EXECUTABLE}" run check
    COMMAND "${NPM_EXECUTABLE}" run build -- --outDir "${UI_BUILD_DIR}" --emptyOutDir
    WORKING_DIRECTORY "${UI_SOURCE_DIR}"
    DEPENDS ${UI_SOURCES} "${UI_INSTALL_STAMP}"
    COMMENT "Building web UI"
    VERBATIM)

juce_add_binary_data(UiBundle
    HEADER_NAME UiBundle.h
    NAMESPACE UiBundle
    SOURCES ${UI_BUNDLE_FILES})

set_target_properties(UiBundle PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
