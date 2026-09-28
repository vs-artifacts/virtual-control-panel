#include "HostedEditorWindows.h"

class HostedEditorWindow final : public juce::DocumentWindow
{
public:
    HostedEditorWindow (juce::AudioProcessorEditor* editor, std::function<void()> onCloseIn)
        : DocumentWindow (editor->getAudioProcessor()->getName(), juce::Colours::black, DocumentWindow::closeButton),
          onClose (std::move (onCloseIn))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);

        // Otherwise it disappears behind the DAW as soon as the DAW is clicked
        setAlwaysOnTop (true);
    }

    void closeButtonPressed() override { onClose(); }

private:
    std::function<void()> onClose;
};

HostedEditorWindows::HostedEditorWindows (juce::Component& centreAround) : owner (centreAround)
{
}

HostedEditorWindows::~HostedEditorWindows() = default;

bool HostedEditorWindows::open (const HostedPlugin& plugin)
{
    if (const auto existing = windows.find (&plugin); existing != windows.end())
    {
        existing->second->toFront (true);
        return true;
    }

    auto* instance = plugin.getInstance();
    auto* editor = instance != nullptr && instance->hasEditor() ? instance->createEditorAndMakeActive() : nullptr;

    if (editor == nullptr)
        return false;

    auto window = std::make_unique<HostedEditorWindow> (editor, [this, key = &plugin] {
        // Deferred, because a window can't be deleted from inside its own callback
        juce::MessageManager::callAsync ([weakThis = juce::WeakReference<HostedEditorWindows> (this), key] {
            if (weakThis != nullptr)
                weakThis->windows.erase (key);
        });
    });

    window->centreAroundComponent (&owner, window->getWidth(), window->getHeight());
    window->setVisible (true);
    windows[&plugin] = std::move (window);
    return true;
}
