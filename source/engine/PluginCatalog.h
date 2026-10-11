#pragma once

#include <juce_core/juce_core.h>

// Stand-ins until plugin scanning: the plugins a slot may hold, by key. Plugins only ever load from this list, never
// from a path in a project (project files are untrusted).
#if JUCE_WINDOWS
inline constexpr auto serum2Path = R"(C:\Program Files\Common Files\VST3\Serum2.vst3)";
inline constexpr auto sylenth1Path = R"(C:\Program Files\Common Files\VST3\Sylenth1.vst3)";
inline constexpr auto eqPath = R"(C:\Program Files\Common Files\VST3\Kilohearts\kHs 3-Band EQ.vst3)";
inline constexpr auto nonlinearFilterPath = R"(C:\Program Files\Common Files\VST3\Kilohearts\kHs Nonlinear Filter.vst3)";
#else
inline constexpr auto serum2Path = "/Library/Audio/Plug-Ins/VST3/Serum2.vst3";
inline constexpr auto sylenth1Path = "/Library/Audio/Plug-Ins/VST3/Sylenth1.vst3";
inline constexpr auto eqPath = "/Library/Audio/Plug-Ins/VST3/Kilohearts/kHs 3-Band EQ.vst3";
inline constexpr auto nonlinearFilterPath = "/Library/Audio/Plug-Ins/VST3/Kilohearts/kHs Nonlinear Filter.vst3";
#endif

struct PluginCatalog
{
    struct Entry
    {
        juce::String key, name;
        juce::File file;
        bool generator = false;
        bool offered = true; // in the menu for new slots
    };

    std::vector<Entry> entries;

    const Entry* find (const juce::String& key) const
    {
        const auto entry = std::find_if (entries.begin(), entries.end(), [&key] (const Entry& e) { return e.key == key; });
        return entry != entries.end() ? &*entry : nullptr;
    }

    static PluginCatalog standard()
    {
        return { {
            { "serum2", "Serum 2", juce::File (serum2Path), true, true },
            { "sylenth1", "Sylenth1", juce::File (sylenth1Path), true, false },
            { "khsEq", "kHs 3-Band EQ", juce::File (eqPath), false, true },
            { "khsNonlinearFilter", "kHs Nonlinear Filter", juce::File (nonlinearFilterPath), false, true },
        } };
    }
};
