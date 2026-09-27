# JUCE links WebView2 statically on Windows and needs Microsoft's WebView2 SDK package at build time.
# It's downloaded once per build folder at a pinned version and checked against a recorded checksum,
# instead of being installed per machine. To update: change both values below (SHA-256 of the .nupkg).
if (NOT WIN32)
    return()
endif ()

set(WEBVIEW2_VERSION 1.0.3485.44)
set(WEBVIEW2_SHA256 BC09150B179246AC90189649B13BE8E6B11B3AC200E817E18DF106E1F3CF489E)

# One folder per version: JUCE's finder silently takes the first matching package folder it sees
set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${CMAKE_BINARY_DIR}/webview2-${WEBVIEW2_VERSION}")
set(_webview2_package_dir "${JUCE_WEBVIEW2_PACKAGE_LOCATION}/Microsoft.Web.WebView2")

if (NOT EXISTS "${_webview2_package_dir}/build/native/include/WebView2.h")
    set(_webview2_nupkg "${JUCE_WEBVIEW2_PACKAGE_LOCATION}/package.nupkg")

    file(DOWNLOAD "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}" "${_webview2_nupkg}"
        EXPECTED_HASH SHA256=${WEBVIEW2_SHA256}
        TLS_VERIFY ON
        STATUS _webview2_status)

    list(GET _webview2_status 0 _webview2_status_code)
    if (NOT _webview2_status_code EQUAL 0)
        message(FATAL_ERROR "WebView2 SDK download failed: ${_webview2_status}")
    endif ()

    file(ARCHIVE_EXTRACT INPUT "${_webview2_nupkg}" DESTINATION "${_webview2_package_dir}")
    file(REMOVE "${_webview2_nupkg}")
endif ()

# JUCE's finder caches the folder it found; forget it so a version change takes effect
unset(WebView2_root_dir CACHE)
