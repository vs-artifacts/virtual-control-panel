#include "HostMenus.h"

namespace
{
    juce::var makeSeparator()
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty ("separator", true);
        return entry;
    }

    juce::var makeHeader (const juce::String& text)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty ("header", text);
        return entry;
    }

    bool isSeparator (const juce::var& entry)
    {
        return static_cast<bool> (entry["separator"]);
    }

    juce::var describeItems (const juce::PopupMenu& menu, std::vector<std::function<void()>>& actions, int depth, int& budget)
    {
        juce::Array<juce::var> items;

        for (juce::PopupMenu::MenuItemIterator iterator (menu); iterator.next() && budget > 0; --budget)
        {
            const auto& item = iterator.getItem();

            if (item.isSeparator)
            {
                items.add (makeSeparator());
            }
            else if (item.isSectionHeader)
            {
                items.add (makeHeader (item.text));
            }
            else if (item.subMenu != nullptr)
            {
                auto* entry = new juce::DynamicObject();
                entry->setProperty ("text", item.text);
                entry->setProperty ("enabled", item.isEnabled && depth < HostMenus::maxDepth);
                entry->setProperty ("items", depth < HostMenus::maxDepth ? describeItems (*item.subMenu, actions, depth + 1, budget) : juce::var (juce::Array<juce::var>()));
                items.add (entry);
            }
            else if (item.text.startsWithChar ('-') && item.text.length() > 1)
            {
                // FL Studio marks its menu's sections with items like "-Remote control"
                if (!items.isEmpty() && !isSeparator (items.getLast()))
                    items.add (makeSeparator());

                items.add (makeHeader (item.text.substring (1).trim()));
            }
            else
            {
                auto* entry = new juce::DynamicObject();
                entry->setProperty ("id", static_cast<int> (actions.size()));
                entry->setProperty ("text", item.text);
                entry->setProperty ("enabled", item.isEnabled && item.action != nullptr);
                entry->setProperty ("checked", item.isTicked);
                actions.push_back (item.action);
                items.add (entry);
            }
        }

        return items;
    }
}

HostMenus::HostMenus (juce::AudioProcessorEditor& editorIn, ControlSession& sessionIn)
    : editor (editorIn), session (sessionIn)
{
}

juce::var HostMenus::describe (const juce::PopupMenu& menu, std::vector<std::function<void()>>& actionsOut)
{
    auto budget = maxItems;
    return describeItems (menu, actionsOut, 0, budget);
}

juce::var HostMenus::open (const juce::var& handle)
{
    close();

    auto hostItems = juce::var (juce::Array<juce::var>());
    auto* target = session.findWriteTarget (handle);

    // No host context in the standalone app; some hosts offer no parameter menus
    if (auto* context = editor.getHostContext(); target != nullptr && context != nullptr)
        if (const auto menu = context->getContextMenuForParameter (target))
            hostItems = describe (menu->getEquivalentPopupMenu(), actions);

    auto* hostMenu = new juce::DynamicObject();
    hostMenu->setProperty ("text", "Host menu");
    hostMenu->setProperty ("enabled", hostItems.size() > 0);
    hostMenu->setProperty ("items", hostItems);

    auto* result = new juce::DynamicObject();
    result->setProperty ("items", juce::Array<juce::var> { hostMenu });
    return result;
}

void HostMenus::choose (const juce::var& choice)
{
    const auto& id = choice["id"];

    if (!id.isInt() || !juce::isPositiveAndBelow (static_cast<int> (id), static_cast<int> (actions.size())))
        return;

    // The item may open a dialog or change anything, so the menu is forgotten first
    const auto action = actions[static_cast<size_t> (static_cast<int> (id))];
    close();

    if (action != nullptr)
        action();
}

void HostMenus::close()
{
    actions.clear();
}
