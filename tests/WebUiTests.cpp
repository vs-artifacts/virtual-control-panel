#include "helpers/ParameterHost.h"
#include <WebUi.h>
#include <WebViewKeepAlive.h>
#include <bridge/UiFiles.h>
#include <catch2/catch_test_macros.hpp>

namespace
{
    juce::String contentsOf (const UiFile& file)
    {
        return juce::String::fromUTF8 (file.data, file.size);
    }

    constexpr auto bridgeStatus = "document.getElementById('bridge-status')?.textContent ?? ''";
    constexpr auto connected = "Bridge connected: test reply";

    // The web view only exists while it's on the desktop; far off screen keeps it out of the way
    struct OffscreenWebUi
    {
        OffscreenWebUi (KeyForwarder& keys, ControlSession& session, WebUi::Handlers handlers = {}) : webUi ("test reply", keys, session, std::move (handlers))
        {
            window.setBounds (-10000, -10000, 400, 300);
            webUi.setBounds (window.getLocalBounds());
            window.addAndMakeVisible (webUi);
            window.addToDesktop (juce::ComponentPeer::windowIsTemporary);
            window.setVisible (true);
        }

        void pumpUntil (const std::function<bool()>& done, int timeoutMs = 20000)
        {
            const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);

            while (!done() && juce::Time::getMillisecondCounter() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }

        juce::String evaluate (const juce::String& script)
        {
            // Shared, because a reply can still arrive after we've stopped waiting
            struct Reply
            {
                bool arrived = false;
                juce::String value;
            };

            const auto reply = std::make_shared<Reply>();

            webUi.evaluateJavascript (script, [reply] (juce::WebBrowserComponent::EvaluationResult result) {
                if (const auto* value = result.getResult())
                    reply->value = value->toString();

                reply->arrived = true;
            });

            pumpUntil ([&reply] { return reply->arrived; });
            return reply->value;
        }

        // Re-runs the script until it returns the expected text or time runs out; returns the last result
        juce::String waitFor (const juce::String& script, const juce::String& expected)
        {
            const auto deadline = juce::Time::getMillisecondCounter() + 20000;
            juce::String result;

            while (result != expected && juce::Time::getMillisecondCounter() < deadline)
                result = evaluate (script);

            return result;
        }

        juce::Component window;
        WebUi webUi;
    };
}

TEST_CASE ("The embedded UI files", "[webui]")
{
    SECTION ("the root is the page, with its security policy")
    {
        const auto root = findUiFile ("/");
        const auto index = findUiFile ("/index.html");
        REQUIRE (root.has_value());
        REQUIRE (index.has_value());
        CHECK (root->data == index->data);
        CHECK (juce::String (root->mimeType) == "text/html");
        CHECK (contentsOf (*root).contains ("Content-Security-Policy"));
    }

    SECTION ("every file the page refers to is embedded, with the right type")
    {
        const auto html = contentsOf (*findUiFile ("/"));
        int found = 0;

        for (auto reference = html.indexOf ("\"./"); reference >= 0; reference = html.indexOf (reference + 1, "\"./"))
        {
            const auto path = html.substring (reference + 2, html.indexOf (reference + 1, "\""));
            INFO (path);
            const auto file = findUiFile (path);
            REQUIRE (file.has_value());
            CHECK (juce::String (file->mimeType) == (path.endsWith (".js") ? "text/javascript" : "text/css"));
            ++found;
        }

        CHECK (found == 2);
    }

    SECTION ("anything else is not found, including attempts to reach other files")
    {
        for (const auto* path : { "", "index.html", "/assets/", "/assets/missing.js", "/../index.html", "/assets/../index.html", "/%2e%2e/index.html", "/index.html?x=1", "/INDEX.HTML", "\\index.html", "//index.html", "/index.html/" })
        {
            INFO (path);
            CHECK_FALSE (findUiFile (path).has_value());
        }
    }
}

TEST_CASE ("The web view only serves and loads our own page", "[webui]")
{
    SECTION ("unknown requests get an empty local reply, never the network")
    {
        const auto reply = WebUi::serve ("/../secrets.txt");
        CHECK (reply.data.empty());
        CHECK (reply.mimeType == "text/plain");

        const auto page = WebUi::serve ("/");
        CHECK (page.mimeType == "text/html");
        CHECK (page.data.size() == static_cast<size_t> (findUiFile ("/")->size));
    }

    SECTION ("navigation stays on our page")
    {
        const auto root = juce::WebBrowserComponent::getResourceProviderRoot();
        CHECK (WebUi::isAllowedNavigation (root));
        CHECK (WebUi::isAllowedNavigation (root + "index.html"));
        CHECK (WebUi::isAllowedNavigation ("about:blank"));

        for (const auto* url : { "", "https://example.com/", "http://127.0.0.1:8080/", "file:///C:/Windows/win.ini", "data:text/html,<script>alert(1)</script>", "javascript:alert(1)", "https://juce.backend.example.com/", "https://juce.backend@example.com/", "juce://juce.backend.example.com/", "about:blank#x" })
        {
            INFO (url);
            CHECK_FALSE (WebUi::isAllowedNavigation (url));
        }
    }
}

TEST_CASE ("The page loads in a real web view and reaches C++ through the bridge", "[webui][webview]")
{
    if (!WebUi::isSupported())
        SKIP ("No supported web view on this system");

    KeyForwarder keys ({}, [] (std::uint32_t, bool, bool) {}, [] { return KeyForwarder::Modifiers {}; });
    ControlSession noParameters ({});
    OffscreenWebUi page (keys, noParameters);
    CHECK (page.waitFor (bridgeStatus, connected) == connected);
}

TEST_CASE ("Keys pressed on the page reach the key forwarder", "[webui][webview]")
{
    if (!WebUi::isSupported())
        SKIP ("No supported web view on this system");

    std::vector<std::pair<std::uint32_t, bool>> posts;
    KeyForwarder keys ("KeyZ,Space", [&posts] (std::uint32_t scanCode, bool down, bool) { posts.emplace_back (scanCode, down); }, [] { return KeyForwarder::Modifiers {}; });
    ControlSession noParameters ({});
    OffscreenWebUi page (keys, noParameters);
    REQUIRE (page.waitFor (bridgeStatus, connected) == connected);
    CHECK (page.evaluate ("window.__JUCE__.initialisationData.forwardedKeys.join()") == "KeyZ,Space");

    page.evaluate ("for (const code of ['KeyZ', 'Escape']) for (const type of ['keydown', 'keyup']) window.dispatchEvent (new KeyboardEvent (type, { code }))");
    page.pumpUntil ([&posts] { return posts.size() >= 2; });

    CHECK (posts == std::vector<std::pair<std::uint32_t, bool>> { { 0x2C, true }, { 0x2C, false } });
}

TEST_CASE ("With the keep-alive running, the page reopens quickly", "[webui][webview]")
{
    if (!WebUi::isSupported())
        SKIP ("No supported web view on this system");

    KeyForwarder keys ({}, [] (std::uint32_t, bool, bool) {}, [] { return KeyForwarder::Modifiers {}; });
    ControlSession noParameters ({});
    WebViewKeepAlive keepAlive;

    {
        OffscreenWebUi first (keys, noParameters);
        REQUIRE (first.waitFor (bridgeStatus, connected) == connected);
        keepAlive.start();

        // Nothing reports when the hidden web view is ready; give it time to start
        first.pumpUntil ([] { return false; }, 1000);
    }

    const auto started = juce::Time::getMillisecondCounterHiRes();
    OffscreenWebUi second (keys, noParameters);
    REQUIRE (second.waitFor (bridgeStatus, connected) == connected);
    CHECK (juce::Time::getMillisecondCounterHiRes() - started < 2000.0);
}

TEST_CASE ("The page's slider controls a parameter both ways", "[webui][webview]")
{
    if (!WebUi::isSupported())
        SKIP ("No supported web view on this system");

    HostLog log;
    ParameterHost plugin;
    plugin.addListener (&log);
    auto& gain = plugin.addFloat ("gain");

    {
        KeyForwarder keys ({}, [] (std::uint32_t, bool, bool) {}, [] { return KeyForwarder::Modifiers {}; });
        ControlSession session ({ { "FX 1", "Test plugin", "Gain", "Param 1", &gain, &gain } });
        OffscreenWebUi page (keys, session);
        REQUIRE (page.waitFor ("document.querySelectorAll('input[type=range]').length", "1") == "1");
        CHECK (page.evaluate ("document.querySelector('.value-text').textContent") == gain.getCurrentValueAsText());

        // A drag on the page: press, move, release (anywhere)
        page.evaluate ("{ const slider = document.querySelector('input[type=range]');"
                       "  slider.dispatchEvent (new PointerEvent ('pointerdown', { bubbles: true }));"
                       "  slider.value = '0.75';"
                       "  slider.dispatchEvent (new Event ('input', { bubbles: true }));"
                       "  window.dispatchEvent (new PointerEvent ('pointerup')); }");
        page.pumpUntil ([&log] { return log.gestures.size() >= 2; });

        CHECK (juce::exactlyEqual (gain.getValue(), 0.75f));
        CHECK (log.gestures == std::vector<std::pair<int, bool>> { { 0, true }, { 0, false } });

        // A change from elsewhere (the plugin's own window, automation) shows on the page
        gain.setValueNotifyingHost (0.25f);
        CHECK (page.waitFor ("document.querySelector('input[type=range]').value", "0.25") == "0.25");
        CHECK (page.waitFor ("document.querySelector('.value-text').textContent", gain.getCurrentValueAsText()) == gain.getCurrentValueAsText());
    }

    plugin.removeListener (&log);
}

TEST_CASE ("The page matches the plugin's scale, whatever the web view's own", "[webui][webview]")
{
    if (!WebUi::isSupported())
        SKIP ("No supported web view on this system");

    KeyForwarder keys ({}, [] (std::uint32_t, bool, bool) {}, [] { return KeyForwarder::Modifiers {}; });
    ControlSession noParameters ({});
    OffscreenWebUi page (keys, noParameters);
    REQUIRE (page.waitFor (bridgeStatus, connected) == connected);

    const auto pixelRatio = page.evaluate ("window.devicePixelRatio").getDoubleValue();
    REQUIRE (pixelRatio > 0.0);

    const auto expectedZoom = juce::String (page.webUi.getDisplayScale() / pixelRatio, 4);
    CHECK (page.waitFor ("Number (document.documentElement.style.zoom).toFixed (4)", expectedZoom) == expectedZoom);
}

TEST_CASE ("Right-clicking a slider shows the host's options for its parameter", "[webui][webview]")
{
    if (!WebUi::isSupported())
        SKIP ("No supported web view on this system");

    ParameterHost plugin;
    auto& gain = plugin.addFloat ("gain");
    juce::var askedFor;
    juce::var chosen;

    WebUi::Handlers handlers;
    handlers.openParameterMenu = [&askedFor] (const juce::var& handle) {
        askedFor = handle;
        return juce::JSON::parse (R"({ "items": [{ "text": "Host menu", "enabled": true, "items": [{ "header": "Param 1" }, { "id": 0, "text": "Create automation clip", "enabled": true, "checked": false }] }] })");
    };
    handlers.events["menuChoice"] = [&chosen] (const juce::var& choice) { chosen = choice; };

    KeyForwarder keys ({}, [] (std::uint32_t, bool, bool) {}, [] { return KeyForwarder::Modifiers {}; });
    ControlSession session ({ { "FX 1", "Test plugin", "Gain", "Param 1", &gain, &gain } });
    OffscreenWebUi page (keys, session, std::move (handlers));
    REQUIRE (page.waitFor ("document.querySelectorAll('input[type=range]').length", "1") == "1");

    page.evaluate ("document.querySelector('input[type=range]').dispatchEvent (new MouseEvent ('contextmenu', { bubbles: true, cancelable: true, clientX: 40, clientY: 60 }))");
    CHECK (page.waitFor ("String (document.querySelector('.context-menu button')?.textContent.startsWith ('Host menu'))", "true") == "true");
    CHECK (static_cast<int> (askedFor) == 0);

    // The host's options are in a submenu, a separate list next to the first
    page.evaluate ("document.querySelector('.context-menu button').click()");
    const auto hostItem = "document.querySelectorAll('.context-menu .menu-list')[1]?.querySelector('button')";
    CHECK (page.waitFor (juce::String (hostItem) + "?.textContent.trim() ?? ''", "Create automation clip") == "Create automation clip");

    page.evaluate (juce::String (hostItem) + ".click()");
    page.pumpUntil ([&chosen] { return !chosen.isVoid(); });
    CHECK (static_cast<int> (chosen["id"]) == 0);
    CHECK (page.waitFor ("String (document.querySelector('.context-menu') === null)", "true") == "true");
}

TEST_CASE ("The page's corner grips resize the plugin", "[webui][webview]")
{
    if (!WebUi::isSupported())
        SKIP ("No supported web view on this system");

    juce::StringArray messages;

    WebUi::Handlers handlers;
    handlers.events["resize"] = [&messages] (const juce::var& drag) { messages.add (drag["corner"].toString() + " " + drag["phase"].toString()); };

    KeyForwarder keys ({}, [] (std::uint32_t, bool, bool) {}, [] { return KeyForwarder::Modifiers {}; });
    ControlSession noParameters ({});
    OffscreenWebUi page (keys, noParameters, std::move (handlers));
    REQUIRE (page.waitFor ("document.querySelectorAll('.grip').length", "4") == "4");

    page.evaluate ("{ const grip = document.querySelector('.grip.topLeft');"
                   "  grip.dispatchEvent (new PointerEvent ('pointerdown', { bubbles: true, cancelable: true, button: 0 }));"
                   "  grip.dispatchEvent (new PointerEvent ('pointerup', { bubbles: true })); }");
    page.pumpUntil ([&messages] { return messages.size() >= 2; });

    CHECK (messages == juce::StringArray { "topLeft start", "topLeft end" });
}
