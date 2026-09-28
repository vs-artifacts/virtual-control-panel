#include <EditorScale.h>
#include <HostMenus.h>
#include <PluginEditor.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

// One run through, because every processor loads all three plugins
TEST_CASE ("The editor reopens on its instance's tab, at its size", "[editor][processor]")
{
    // Opening an editor records its size as the one new instances use; this test mustn't change the user's
    juce::SharedResourcePointer<PluginSettings> settings;
    const auto previousScale = settings->getLastEditorScale();
    const auto previousResizing = settings->getResizing();
    settings->setResizing (EditorScale::Resizing::smooth);

    PluginProcessor processor;
    CHECK (processor.getEditorTab() == 0);

    processor.setEditorTab (2);
    processor.setEditorScale (1.5);
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);

    processor.setEditorTab (1);
    processor.setEditorScale (0.75);
    processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (processor.getEditorTab() == 2);
    CHECK_THAT (processor.getEditorScale().value_or (0.0), WithinAbs (1.5, 1e-9));

    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        CHECK (editor->getWidth() == juce::roundToInt (EditorScale::designWidth * 1.5));
        CHECK (editor->getHeight() == juce::roundToInt (EditorScale::designHeight * 1.5));
        CHECK_FALSE (editor->isResizable());

        auto& pluginEditor = dynamic_cast<PluginEditor&> (*editor);
        pluginEditor.setScale (0.1);
        CHECK (editor->getWidth() == juce::roundToInt (EditorScale::designWidth * EditorScale::minimum));
        CHECK_THAT (processor.getEditorScale().value_or (0.0), WithinAbs (EditorScale::minimum, 1e-9));

        processor.editorBeingDeleted (editor.get());
    }

    settings->setLastEditorScale (previousScale);
    settings->setResizing (previousResizing);
}

TEST_CASE ("The editor's scale", "[editor]")
{
    using EditorScale::constrain;
    using EditorScale::Resizing;

    SECTION ("smooth resizing only keeps within 50% and 500%")
    {
        CHECK_THAT (constrain (1.37, Resizing::smooth, 1.0), WithinAbs (1.37, 1e-9));
        CHECK_THAT (constrain (0.2, Resizing::smooth, 1.0), WithinAbs (0.5, 1e-9));
        CHECK_THAT (constrain (80.0, Resizing::smooth, 2.0), WithinAbs (5.0, 1e-9));
        CHECK_THAT (constrain (std::numeric_limits<double>::quiet_NaN(), Resizing::smooth, 1.0), WithinAbs (1.0, 1e-9));
    }

    SECTION ("sharp steps give a whole number of physical pixels per design pixel")
    {
        CHECK_THAT (constrain (1.37, Resizing::sharp, 1.0), WithinAbs (1.0, 1e-9));
        CHECK_THAT (constrain (1.6, Resizing::sharp, 1.0), WithinAbs (2.0, 1e-9));
        CHECK_THAT (constrain (0.2, Resizing::sharp, 1.0), WithinAbs (1.0, 1e-9));
        CHECK_THAT (constrain (80.0, Resizing::sharp, 1.0), WithinAbs (5.0, 1e-9));

        // On a 150% monitor: 2 physical pixels per design pixel at 133%
        CHECK_THAT (constrain (1.2, Resizing::sharp, 1.5), WithinAbs (2.0 / 1.5, 1e-9));
        CHECK_THAT (constrain (0.6, Resizing::sharp, 1.5), WithinAbs (1.0 / 1.5, 1e-9));

        // On a 200% monitor even 50% is sharp
        CHECK_THAT (constrain (0.5, Resizing::sharp, 2.0), WithinAbs (0.5, 1e-9));
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
