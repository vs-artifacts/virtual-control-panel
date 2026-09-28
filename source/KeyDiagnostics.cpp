#include "KeyDiagnostics.h"

#if JUCE_DEBUG

    #if JUCE_WINDOWS
        #define NOMINMAX
        #define WIN32_LEAN_AND_MEAN
        #include <windows.h>
    #endif

namespace
{
    // At most one log runs per process; message thread only, like the hook
    KeyDiagnostics* active = nullptr;

    #if JUCE_WINDOWS
    LRESULT CALLBACK onThreadMessage (int code, WPARAM removal, LPARAM message)
    {
        if (code == HC_ACTION && removal == PM_REMOVE && active != nullptr)
            active->recordWindowMessage (reinterpret_cast<const MSG*> (message));

        return CallNextHookEx (nullptr, code, removal, message);
    }

    juce::String describeWindow (HWND window, HWND editorWindow)
    {
        if (window == nullptr)
            return "none";

        wchar_t className[64] {};
        GetClassNameW (window, className, 64);
        const auto name = juce::String (className);

        if (editorWindow != nullptr && (window == editorWindow || IsChild (editorWindow, window)))
            return "ours [" + name + "]";

        if (editorWindow != nullptr && window == GetParent (editorWindow))
            return "host, around ours [" + name + "]";

        return "host [" + name + "]";
    }
    #endif
}

KeyDiagnostics::KeyDiagnostics (juce::Component& editorIn) : editor (editorIn)
{
}

KeyDiagnostics::~KeyDiagnostics()
{
    stop();
}

void KeyDiagnostics::start()
{
    if (running)
        return;

    if (active != nullptr)
        active->stop();

    #if JUCE_WINDOWS
    // This thread only (the host's UI thread), never the whole system
    hook = SetWindowsHookExW (WH_GETMESSAGE, onThreadMessage, nullptr, GetCurrentThreadId());
    #endif

    active = this;
    running = true;
    startTime = juce::Time::getMillisecondCounterHiRes();
    startTimer (runSeconds * 1000);
    add (hook != nullptr ? "--- started" : "--- started without the message hook (not available here)");
}

void KeyDiagnostics::stop()
{
    if (!running)
        return;

    #if JUCE_WINDOWS
    if (hook != nullptr)
        UnhookWindowsHookEx (static_cast<HHOOK> (hook));
    #endif

    hook = nullptr;
    active = nullptr;
    running = false;
    stopTimer();
    add ("--- stopped");
}

void KeyDiagnostics::note (const juce::String& event)
{
    if (running)
        add (event);
}

void KeyDiagnostics::clear()
{
    lines.clear();
    ++version;
}

void KeyDiagnostics::recordWindowMessage (const void* message)
{
    #if JUCE_WINDOWS
    const auto& msg = *static_cast<const MSG*> (message);

    const auto* kind = [&msg]() -> const char* {
        switch (msg.message)
        {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                return (msg.lParam & 0x40000000) != 0 ? "key down (repeat)" : "key down";
            case WM_KEYUP:
            case WM_SYSKEYUP:
                return "key up";
            case WM_CHAR:
            case WM_SYSCHAR:
                return "character";
            case WM_LBUTTONDOWN:
                return "left button down";
            case WM_LBUTTONUP:
                return "left button up";
            case WM_RBUTTONDOWN:
                return "right button down";
            case WM_RBUTTONUP:
                return "right button up";
            default:
                return nullptr;
        }
    }();

    if (kind == nullptr)
        return;

    auto* peer = editor.getPeer();
    const auto editorWindow = peer != nullptr ? static_cast<HWND> (peer->getNativeHandle()) : nullptr;
    const auto leftHeld = (GetAsyncKeyState (VK_LBUTTON) & 0x8000) != 0;

    add (juce::String (kind) + " to " + describeWindow (msg.hwnd, editorWindow)
         + "; focus " + describeWindow (GetFocus(), editorWindow)
         + "; capture " + describeWindow (GetCapture(), editorWindow)
         + (leftHeld ? "; left button held" : ""));
    #else
    juce::ignoreUnused (message);
    #endif
}

void KeyDiagnostics::add (const juce::String& line)
{
    const auto seconds = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;
    lines.add (juce::String (seconds, 3) + "s  " + line);

    if (lines.size() > maxLines)
        lines.remove (0);

    ++version;
}

#endif
