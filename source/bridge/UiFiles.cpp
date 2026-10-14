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
        { "/assets/Tiny5-Regular.ttf", { UiBundle::Tiny5Regular_ttf, UiBundle::Tiny5Regular_ttfSize, "font/ttf" } },
        { "/assets/Barlow-Regular.ttf", { UiBundle::BarlowRegular_ttf, UiBundle::BarlowRegular_ttfSize, "font/ttf" } },
        { "/assets/Barlow-SemiBold.ttf", { UiBundle::BarlowSemiBold_ttf, UiBundle::BarlowSemiBold_ttfSize, "font/ttf" } },
        { "/assets/Rubik-VariableFont_wght.ttf", { UiBundle::RubikVariableFont_wght_ttf, UiBundle::RubikVariableFont_wght_ttfSize, "font/ttf" } },
    };

    for (const auto& entry : entries)
        if (path == juce::StringRef (entry.path))
            return entry.file;

    return std::nullopt;
}
