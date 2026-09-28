#include "HostMenus.h"

namespace
{
    juce::var describeItems (const juce::PopupMenu& menu, std::vector<std::function<void()>>& actions, int depth, int& budget)
    {
        juce::Array<juce::var> items;

        for (juce::PopupMenu::MenuItemIterator iterator (menu); iterator.next() && budget > 0; --budget)
        {
            const auto& item = iterator.getItem();
            auto* entry = new juce::DynamicObject();

            if (item.isSeparator)
            {
                entry->setProperty ("separator", true);
            }
            else if (item.isSectionHeader)
            {
                entry->setProperty ("header", item.text);
            }
            else if (item.subMenu != nullptr)
            {
                entry->setProperty ("text", item.text);
                entry->setProperty ("enabled", item.isEnabled && depth < HostMenus::maxDepth);
                entry->setProperty ("items", depth < HostMenus::maxDepth ? describeItems (*item.subMenu, actions, depth + 1, budget) : juce::var (juce::Array<juce::var>()));
            }
            else
            {
                entry->setProperty ("id", static_cast<int> (actions.size()));
                entry->setProperty ("text", item.text);
                entry->setProperty ("enabled", item.isEnabled && item.action != nullptr);
                entry->setProperty ("checked", item.isTicked);
                actions.push_back (item.action);
            }

            items.add (entry);
        }

        return items;
    }

    juce::var result (bool native, const juce::var& items)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("native", native);
        object->setProperty ("items", items);
        return object;
    }
}

HostMenus::HostMenus (juce::AudioProcessorEditor& editorIn, ControlSession& sessionIn, PluginSettings& settingsIn)
    : editor (editorIn), session (sessionIn), settings (settingsIn)
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

    const auto noItems = juce::var (juce::Array<juce::var>());
    auto* target = session.findWriteTarget (handle);
    auto* context = editor.getHostContext();

    // No host (e.g. the standalone app), or one that offers no parameter menus
    if (target == nullptr || context == nullptr)
        return result (false, noItems);

    const auto menu = context->getContextMenuForParameter (target);

    if (menu == nullptr)
        return result (false, noItems);

    if (settings.getUseHostMenus())
    {
        menu->showNativeMenu (editor.getMouseXYRelative());
        return result (true, noItems);
    }

    return result (false, describe (menu->getEquivalentPopupMenu(), actions));
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
