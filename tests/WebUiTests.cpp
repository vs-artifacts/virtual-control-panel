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
        explicit OffscreenWebUi (KeyForwarder& keys) : webUi ("test reply", keys)
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
    OffscreenWebUi page (keys);
    CHECK (page.waitFor (bridgeStatus, connected) == connected);
}

TEST_CASE ("Keys pressed on the page reach the key forwarder", "[webui][webview]")
{
    if (!WebUi::isSupported())
        SKIP ("No supported web view on this system");

    std::vector<std::pair<std::uint32_t, bool>> posts;
    KeyForwarder keys ("KeyZ,Space", [&posts] (std::uint32_t scanCode, bool down, bool) { posts.emplace_back (scanCode, down); }, [] { return KeyForwarder::Modifiers {}; });
    OffscreenWebUi page (keys);
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
    WebViewKeepAlive keepAlive;

    {
        OffscreenWebUi first (keys);
        REQUIRE (first.waitFor (bridgeStatus, connected) == connected);
        keepAlive.start();

        // Nothing reports when the hidden web view is ready; give it time to start
        first.pumpUntil ([] { return false; }, 1000);
    }

    const auto started = juce::Time::getMillisecondCounterHiRes();
    OffscreenWebUi second (keys);
    REQUIRE (second.waitFor (bridgeStatus, connected) == connected);
    CHECK (juce::Time::getMillisecondCounterHiRes() - started < 2000.0);
}
