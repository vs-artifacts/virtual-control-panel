#include <EditorScale.h>
#include <HostMenus.h>
#include <PluginEditor.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

// One run through, because every processor loads all three plugins
TEST_CASE ("The editor reopens as its instance left it: tab, Diagnostics, scroll positions, collapsed sections, size", "[editor][processor]")
{
    // Opening an editor records its size as the one new instances use; this test mustn't change the user's
    juce::SharedResourcePointer<PluginSettings> settings;
    const auto previousScale = settings->getLastEditorScale();
    const auto previousLayout = settings->getLastEditorLayout();

    PluginProcessor processor;
    CHECK (processor.getEditorTab() == 0);

    processor.setEditorTab (1);
    processor.setEditorScale (1.5);
    processor.setEditorLayout ({ 1.25, 2.0 });
    processor.setDiagnosticsOpen (true);
    const auto buses = PluginProcessor::findEditorScrollArea ("panel.buses");
    const auto diagnostics = PluginProcessor::findEditorScrollArea ("diagnostics");
    const auto generators = PluginProcessor::findEditorSection ("generators");
    REQUIRE (buses.has_value());
    REQUIRE (diagnostics.has_value());
    REQUIRE (generators.has_value());
    CHECK_FALSE (PluginProcessor::findEditorScrollArea ("tab0.single").has_value());
    CHECK_FALSE (PluginProcessor::findEditorSection ("elsewhere").has_value());
    processor.setEditorScroll (*buses, 340);
    processor.setEditorScroll (*diagnostics, 75);
    processor.setSectionCollapsed (*generators, true);
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);

    processor.setEditorTab (0);
    processor.setEditorScale (0.75);
    processor.setEditorLayout ({ 1.0, 1.0 });
    processor.setDiagnosticsOpen (false);
    processor.setEditorScroll (*buses, 0);
    processor.setEditorScroll (*diagnostics, -5);
    processor.setSectionCollapsed (*generators, false);
    CHECK (processor.getEditorScroll (*diagnostics) == 0);
    processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (processor.getEditorTab() == 1);
    CHECK_THAT (processor.getEditorScale().value_or (0.0), WithinAbs (1.5, 1e-9));
    CHECK_THAT (processor.getEditorLayout().value_or (juce::Point<double>()).y, WithinAbs (2.0, 1e-9));
    CHECK (processor.isDiagnosticsOpen());
    CHECK (processor.getEditorScroll (*buses) == 340);
    CHECK (processor.getEditorScroll (*diagnostics) == 75);
    CHECK (processor.isSectionCollapsed (*generators));

    {
        // Diagnostics widens the window to the right of the layout area
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        CHECK (editor->getWidth() == juce::roundToInt ((EditorScale::designWidth * 1.25 + EditorScale::diagnosticsWidth) * 1.5));
        CHECK (editor->getHeight() == juce::roundToInt (EditorScale::designHeight * 2.0 * 1.5));
        CHECK_FALSE (editor->isResizable());

        auto& pluginEditor = dynamic_cast<PluginEditor&> (*editor);
        processor.setDiagnosticsOpen (false);

        // Width and height apart keep the scale; the layout stays within its limits
        pluginEditor.setLayout ({ 0.1, 9.0 });
        CHECK_THAT (pluginEditor.getScale(), WithinAbs (1.5, 1e-9));
        CHECK (editor->getWidth() == juce::roundToInt (EditorScale::designWidth * EditorScale::minimumLayout * 1.5));
        CHECK (editor->getHeight() == juce::roundToInt (EditorScale::designHeight * EditorScale::maximumLayout * 1.5));
        CHECK_THAT (processor.getEditorLayout().value_or (juce::Point<double>()).x, WithinAbs (EditorScale::minimumLayout, 1e-9));

        pluginEditor.setLayout ({ 1.0, 1.0 });
        pluginEditor.setScale (0.1);
        CHECK (editor->getWidth() == juce::roundToInt (EditorScale::designWidth * EditorScale::minimum));
        CHECK_THAT (processor.getEditorScale().value_or (0.0), WithinAbs (EditorScale::minimum, 1e-9));

        processor.editorBeingDeleted (editor.get());
    }

    settings->setLastEditorScale (previousScale);
    settings->setLastEditorLayout (previousLayout);
}

TEST_CASE ("The editor's scale", "[editor]")
{
    using EditorScale::constrain;
    using EditorScale::snap;

    SECTION ("only kept within 50% and 500%")
    {
        CHECK_THAT (constrain (1.37), WithinAbs (1.37, 1e-9));
        CHECK_THAT (constrain (0.2), WithinAbs (0.5, 1e-9));
        CHECK_THAT (constrain (80.0), WithinAbs (5.0, 1e-9));
        CHECK_THAT (constrain (std::numeric_limits<double>::quiet_NaN()), WithinAbs (1.0, 1e-9));
    }

    SECTION ("Ctrl snaps to the nearest 25% step, or back to the size Ctrl went down at if that's nearer")
    {
        // Virtual step at 137%: between the 125% and 150% steps
        CHECK_THAT (snap (1.37, 1.37), WithinAbs (1.37, 1e-9));
        CHECK_THAT (snap (1.42, 1.37), WithinAbs (1.37, 1e-9));
        CHECK_THAT (snap (1.45, 1.37), WithinAbs (1.5, 1e-9));
        CHECK_THAT (snap (1.62, 1.37), WithinAbs (1.5, 1e-9));
        CHECK_THAT (snap (1.64, 1.37), WithinAbs (1.75, 1e-9));
        CHECK_THAT (snap (1.33, 1.37), WithinAbs (1.37, 1e-9));
        CHECK_THAT (snap (1.3, 1.37), WithinAbs (1.25, 1e-9));

        // Steps stay within the limits
        CHECK_THAT (snap (0.3, 1.0), WithinAbs (0.5, 1e-9));
        CHECK_THAT (snap (9.0, 1.0), WithinAbs (5.0, 1e-9));
        CHECK_THAT (snap (9.0, 1.0, EditorScale::minimumLayout, EditorScale::maximumLayout), WithinAbs (4.0, 1e-9));
    }
}

TEST_CASE ("Host menus become items the page can draw", "[editor]")
{
    int chosen = -1;
    juce::PopupMenu submenu;
    submenu.addItem ("Deeper", [&chosen] { chosen = 3; });

    juce::PopupMenu menu;
    menu.addSectionHeader ("Param 1");
    menu.addItem ("Create automation clip", [&chosen] { chosen = 0; });
    menu.addSeparator();
    menu.addItem ("Link to controller", false, true, [&chosen] { chosen = 1; });
    menu.addSubMenu ("More", submenu);

    std::vector<std::function<void()>> actions;
    const auto items = HostMenus::describe (menu, actions);

    REQUIRE (items.size() == 5);
    CHECK (items[0]["header"] == "Param 1");
    CHECK (items[1]["text"] == "Create automation clip");
    CHECK (static_cast<int> (items[1]["id"]) == 0);
    CHECK (static_cast<bool> (items[1]["enabled"]));
    CHECK_FALSE (static_cast<bool> (items[1]["checked"]));
    CHECK (static_cast<bool> (items[2]["separator"]));
    CHECK_FALSE (static_cast<bool> (items[3]["enabled"]));
    CHECK (static_cast<bool> (items[3]["checked"]));
    CHECK (items[4]["text"] == "More");
    REQUIRE (items[4]["items"].size() == 1);
    CHECK (static_cast<int> (items[4]["items"][0]["id"]) == 2);

    REQUIRE (actions.size() == 3);
    actions[2]();
    CHECK (chosen == 3);
}

TEST_CASE ("FL Studio's section items become headings after a divider", "[editor]")
{
    juce::PopupMenu menu;
    menu.addItem ("-Remote control", [] {});
    menu.addItem ("Link to controller", [] {});
    menu.addItem ("-Automation", [] {});
    menu.addItem ("Create automation clip", [] {});

    std::vector<std::function<void()>> actions;
    const auto items = HostMenus::describe (menu, actions);

    REQUIRE (items.size() == 5);
    CHECK (items[0]["header"] == "Remote control");
    CHECK (items[1]["text"] == "Link to controller");
    CHECK (static_cast<bool> (items[2]["separator"]));
    CHECK (items[3]["header"] == "Automation");
    CHECK (items[4]["text"] == "Create automation clip");
    CHECK (actions.size() == 2);
}
