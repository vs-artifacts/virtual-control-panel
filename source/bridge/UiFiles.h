#pragma once

#include <juce_core/juce_core.h>

// One file of the built web UI, embedded in the binary
struct UiFile
{
    const char* data;
    int size;
    const char* mimeType;
};

// Looks up a request path in a fixed list of embedded files. Nothing is read from disk,
// so no request can reach any other file. Shared by the editor's web view and the future remote server.
std::optional<UiFile> findUiFile (juce::StringRef path);
