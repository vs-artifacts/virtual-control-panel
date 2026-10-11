#include "UiFiles.h"
#include "UiBundle.h"

std::optional<UiFile> findUiFile (juce::StringRef path)
{
    struct Entry
    {
        const char* path;
        UiFile file;
    };

    // Must match the files Vite produces (see cmake-local/Ui.cmake)
    static const Entry entries[] {
        { "/", { UiBundle::index_html, UiBundle::index_htmlSize, "text/html" } },
        { "/index.html", { UiBundle::index_html, UiBundle::index_htmlSize, "text/html" } },
        { "/assets/index.js", { UiBundle::index_js, UiBundle::index_jsSize, "text/javascript" } },
        { "/assets/index.css", { UiBundle::index_css, UiBundle::index_cssSize, "text/css" } },
        { "/assets/m5x7.ttf", { UiBundle::m5x7_ttf, UiBundle::m5x7_ttfSize, "font/ttf" } },
    };

    for (const auto& entry : entries)
        if (path == juce::StringRef (entry.path))
            return entry.file;

    return std::nullopt;
}
