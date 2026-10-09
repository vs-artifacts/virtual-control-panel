#include "helpers/ParameterHost.h"
#include <PluginProcessor.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace
{
    constexpr int testBlockSize = 512;
    constexpr double testSampleRate = 48000.0;

    bool isInstalled (const char* path)
    {
        return juce::File (path).exists();
    }

    juce::MidiBuffer noteOn()
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
        return midi;
    }

    // Sends one note-on, renders about half a second, and returns the loudest sample on the main output (channels 0 and 1)
    template <typename Render>
    float loudestAfterNote (Render&& render, int numChannels = 2)
    {
        auto loudest = 0.0f;

        for (int block = 0; block < 48; ++block)
        {
            juce::AudioBuffer<float> buffer (numChannels, testBlockSize);
            buffer.clear();
            auto midi = block == 0 ? noteOn() : juce::MidiBuffer();
            render (buffer, midi);
            loudest = std::max ({ loudest, buffer.getMagnitude (0, 0, testBlockSize), buffer.getMagnitude (1, 0, testBlockSize) });
        }

        return loudest;
    }

    void checkSynthPlaysANote (const char* path)
    {
        if (!isInstalled (path))
            SKIP ("Not installed: " << path);

        Engine engine;
        engine.build ({ juce::File (path) }, {});
        REQUIRE (engine.getPlugins()[0]->getInstance() != nullptr);

        engine.prepare (testSampleRate, testBlockSize, false);
        CHECK (loudestAfterNote ([&] (auto& buffer, auto& midi) { engine.process (buffer, midi, nullptr); }) > 0.001f);
        engine.release();
    }
}

TEST_CASE ("An empty engine outputs silence and passes MIDI through", "[engine]")
{
    Engine engine;
    engine.build ({}, {});
    engine.prepare (testSampleRate, testBlockSize, false);

    juce::AudioBuffer<float> buffer (2, testBlockSize);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill (buffer.getWritePointer (channel), 0.5f, testBlockSize);

    auto midi = noteOn();
    engine.process (buffer, midi, nullptr);

    CHECK (juce::exactlyEqual (buffer.getMagnitude (0, testBlockSize), 0.0f));
    REQUIRE (midi.getNumEvents() == 1);
    CHECK ((*midi.begin()).getMessage().isNoteOn());
}

TEST_CASE ("A missing plugin leaves an empty slot with a message", "[engine]")
{
    Engine engine;
    engine.build ({ juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("no-such-plugin.vst3") }, {});

    REQUIRE (engine.getPlugins().size() == 1);
    const auto& slot = *engine.getPlugins()[0];
    CHECK (slot.getInstance() == nullptr);
    CHECK (slot.getLoadError().isNotEmpty());
    CHECK (slot.getName() == "no-such-plugin");
    CHECK (engine.getSlotLabel (0) == "Generator 1");
    CHECK_FALSE (engine.expose (0, "0"));
}

TEST_CASE ("Generator slots only accept instruments", "[engine][vst3]")
{
    if (!isInstalled (eqPath))
        SKIP ("Not installed: " << eqPath);

    Engine engine;
    engine.build ({ juce::File (eqPath) }, {});

    const auto& slot = *engine.getPlugins()[0];
    CHECK (slot.getInstance() == nullptr);
    CHECK (slot.getLoadError().contains ("No instrument"));
}

TEST_CASE ("Hosting the test VST3 as FX", "[engine][vst3]")
{
    if (!isInstalled (eqPath))
        SKIP ("Not installed: " << eqPath);

    Engine engine;
    engine.build ({}, { juce::File (eqPath) });
    auto& eq = *engine.getPlugins()[0];
    REQUIRE (eq.getInstance() != nullptr);

    SECTION ("lists its real controls, without the MIDI CC slots")
    {
        const auto& controls = eq.getControls();
        REQUIRE (!controls.empty());
        CHECK (controls.size() < static_cast<size_t> (eq.getInstance()->getParameters().size()));

        for (const auto* param : controls)
        {
            CHECK (param->isAutomatable());
            CHECK (param->getName (64).isNotEmpty());
        }
    }

    SECTION ("restores saved state only into the same plugin")
    {
        auto state = eq.createStateXml();
        REQUIRE (state != nullptr);
        CHECK (eq.restoreStateFromXml (*state));

        state->setAttribute ("id", "VST3-Some Other Plugin-0-0");
        CHECK_FALSE (eq.restoreStateFromXml (*state));
    }

    SECTION ("exposes its controls by their plugin-given IDs")
    {
        CHECK (engine.getSlotLabel (0) == "FX 1");
        CHECK_FALSE (engine.expose (0, "no such ID"));
        CHECK_FALSE (engine.expose (1, eqLowGainID));
        CHECK (engine.getExposedParameters().empty());

        REQUIRE (engine.expose (0, eqLowGainID));
        REQUIRE (engine.getExposedParameters().size() == 1);
        CHECK (engine.getExposedParameters()[0].slot == 0);
        CHECK (engine.getExposedParameters()[0].parameter->getName (64) == "Low");
    }
}

TEST_CASE ("Plugin settings survive a save and reload, slot by slot", "[engine][vst3]")
{
    if (!isInstalled (eqPath))
        SKIP ("Not installed: " << eqPath);

    // Two copies of the same plugin, so each must get its own settings back
    const std::vector<juce::File> twoEqs { juce::File (eqPath), juce::File (eqPath) };

    std::unique_ptr<juce::XmlElement> saved;
    float changedValue = 0.0f;
    float untouchedValue = 0.0f;

    {
        Engine original;
        original.build ({}, twoEqs);
        auto* param = original.getPlugins()[1]->getControls()[0];
        param->setValueNotifyingHost (param->getValue() < 0.5f ? 0.8f : 0.2f);

        // Parameter changes reach a plugin's audio side during processing
        original.prepare (testSampleRate, testBlockSize, false);
        juce::AudioBuffer<float> buffer (2, testBlockSize);
        buffer.clear();
        juce::MidiBuffer midi;
        original.process (buffer, midi, nullptr);
        original.release();

        changedValue = param->getValue();
        untouchedValue = original.getPlugins()[0]->getControls()[0]->getValue();
        saved = original.createStateXml();
    }

    Engine restored;
    restored.build ({}, twoEqs);
    const auto* changed = restored.getPlugins()[1]->getControls()[0];
    const auto* untouched = restored.getPlugins()[0]->getControls()[0];
    REQUIRE_FALSE (juce::exactlyEqual (changed->getValue(), changedValue));

    restored.restoreStateFromXml (*saved);
    CHECK_THAT (changed->getValue(), Catch::Matchers::WithinAbs (changedValue, 0.001));
    CHECK_THAT (untouched->getValue(), Catch::Matchers::WithinAbs (untouchedValue, 0.001));
}

TEST_CASE ("Serum 2 plays a note from MIDI", "[engine][synths]")
{
    checkSynthPlaysANote (serum2Path);
}

TEST_CASE ("Sylenth1 plays a note from MIDI", "[engine][synths]")
{
    checkSynthPlaysANote (sylenth1Path);
}

TEST_CASE ("Muting a generator fades it out while it keeps running", "[engine][synths]")
{
    if (!isInstalled (sylenth1Path))
        SKIP ("Not installed: " << sylenth1Path);

    // No FX after it, so the output is exactly what the mute stage passes
    Engine engine;
    engine.build ({ juce::File (sylenth1Path) }, {});
    REQUIRE (engine.getPlugins()[0]->getInstance() != nullptr);

    CHECK (engine.canMute (0));
    CHECK_FALSE (engine.canMute (1)); // no such slot
    CHECK_FALSE (engine.setMuted (1, true));
    CHECK_FALSE (engine.isMuted (1));

    engine.prepare (testSampleRate, testBlockSize, false);
    juce::AudioBuffer<float> buffer (2, testBlockSize);

    const auto renderBlock = [&] (juce::MidiBuffer midi = {}) {
        buffer.clear();
        engine.process (buffer, midi, nullptr);
        return buffer.getMagnitude (0, testBlockSize);
    };

    renderBlock (noteOn());

    for (int block = 0; block < 8; ++block)
        renderBlock();

    REQUIRE (renderBlock() > 0.001f);

    REQUIRE (engine.setMuted (0, true));
    CHECK (engine.isMuted (0));

    // A fade, not a cut: the first muted block still starts at almost full level
    renderBlock();
    CHECK (buffer.getMagnitude (0, 0, 16) > 0.0f);

    const auto fadeBlocks = static_cast<int> (std::ceil (Engine::muteFadeSeconds * testSampleRate / testBlockSize));

    for (int block = 1; block < fadeBlocks; ++block)
        renderBlock();

    CHECK (juce::exactlyEqual (renderBlock(), 0.0f));

    // Still running underneath: the held note is there again as soon as it's unmuted
    REQUIRE (engine.setMuted (0, false));

    for (int block = 0; block < fadeBlocks; ++block)
        renderBlock();

    CHECK (renderBlock() > 0.001f);
    engine.release();

    // Saved with the slot, and restored (after the audio: restoring a plugin's state may stop its notes)
    REQUIRE (engine.setMuted (0, true));
    const auto saved = engine.createStateXml();
    REQUIRE (engine.setMuted (0, false));
    engine.restoreStateFromXml (*saved);
    CHECK (engine.isMuted (0));
}

TEST_CASE ("The fixed setup: both synths summed into the EQ", "[processor][synths]")
{
    for (auto* path : { serum2Path, sylenth1Path, eqPath })
        if (!isInstalled (path))
            SKIP ("Not installed: " << path);

    PluginProcessor processor;
    auto& engine = processor.getEngine();
    const auto& plugins = engine.getPlugins();
    REQUIRE (plugins.size() == 3);

    for (const auto& plugin : plugins)
    {
        INFO (plugin->getName() << ": " << plugin->getLoadError());
        REQUIRE (plugin->getInstance() != nullptr);
    }

    const auto& serum = *plugins[0];
    const auto& sylenth = *plugins[1];
    const auto& eq = *plugins[2];
    CHECK (serum.getName() == "Serum 2");
    CHECK (engine.isConnected (serum, eq));
    CHECK (engine.isConnected (sylenth, eq));
    CHECK_FALSE (engine.isConnected (serum, sylenth));
    CHECK (engine.canMute (0));
    CHECK (engine.canMute (1));
    CHECK_FALSE (engine.canMute (2)); // FX are never muted

    processor.prepareToPlay (testSampleRate, testBlockSize);
    const auto numChannels = processor.getTotalNumOutputChannels();
    const auto render = [&] (auto& buffer, auto& midi) { processor.processBlock (buffer, midi); };

    SECTION ("a note comes out of the EQ")
    {
        CHECK (loudestAfterNote (render, numChannels) > 0.001f);
    }

    SECTION ("MIDI also passes straight through")
    {
        juce::AudioBuffer<float> buffer (numChannels, testBlockSize);
        buffer.clear();
        auto midi = noteOn();
        processor.processBlock (buffer, midi);
        CHECK (midi.getNumEvents() == 1);
    }

    SECTION ("its saved state and garbage state both load without breaking playback")
    {
        juce::MemoryBlock saved;
        processor.getStateInformation (saved);
        processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        processor.setStateInformation ("garbage", 7);
        CHECK (loudestAfterNote (render, numChannels) > 0.001f);
    }

    processor.releaseResources();
}

// One run through, because every processor loads all three plugins
TEST_CASE ("The EQ's low band gain is exposed to the host as Param 1", "[processor][vst3]")
{
    if (!isInstalled (eqPath))
        SKIP ("Not installed: " << eqPath);

    using Catch::Matchers::WithinAbs;

    PluginProcessor processor;
    CHECK (processor.getParameters().size() == HostControls::count + 1 + PluginProcessor::maxBays);

    const auto& exposed = processor.getEngine().getExposedParameters();
    REQUIRE (exposed.size() == 1);
    auto& eqLow = *exposed[0].parameter;
    auto& param1 = *processor.getParameters()[0];
    CHECK (eqLow.getName (64) == "Low");
    CHECK (param1.getName (64) == "SLOT 1.1: kHs 3-Band EQ - Low");
    CHECK (processor.getParameters()[1]->getName (64) == "Param 2");

    // The names probe: the host is told each time Param 1's name changes
    {
        HostLog log;
        processor.addListener (&log);
        processor.toggleNamesProbe();
        CHECK (param1.getName (64) == "SLOT 1.1: Low");
        processor.toggleNamesProbe();
        CHECK (param1.getName (64) == "SLOT 1.1: kHs 3-Band EQ - Low");
        processor.removeListener (&log);
        CHECK (log.infoChanges == 2);
    }
    CHECK (juce::exactlyEqual (param1.getValue(), eqLow.getValue()));
    CHECK (param1.getCurrentValueAsText() == eqLow.getCurrentValueAsText());

    // The host's changes (automation, controllers) reach the EQ
    param1.setValue (0.8f);
    CHECK_THAT (eqLow.getValue(), WithinAbs (0.8, 1e-6));

    // Changes in the EQ's own window reach the host
    eqLow.setValue (0.3f);
    processor.getHostControls().reportTargetChanges();
    CHECK_THAT (param1.getValue(), WithinAbs (0.3, 1e-6));

    // The page's changes go through Param 1, so the host sees them
    {
        ControlSession session (processor.createSessionParameters());
        const auto catalog = session.catalog();
        REQUIRE (catalog.size() == 1 + BayLink::numControls);
        CHECK (catalog[0]["slot"] == "FX 1");
        CHECK (catalog[0]["plugin"] == processor.getEngine().getPlugins()[2]->getName());
        CHECK (catalog[0]["name"] == "Low");
        CHECK (catalog[0]["host"] == "SLOT 1.1: kHs 3-Band EQ - Low");
        CHECK (catalog[1]["slot"] == "Bay 1");
        CHECK (catalog[1]["name"] == "Control 1");
        CHECK (catalog[1]["host"] == "Param 2");

        session.applySet (juce::JSON::parse (R"([{ "h": 0, "v": 0.25 }])"));
        CHECK_THAT (param1.getValue(), WithinAbs (0.25, 1e-6));
        CHECK_THAT (eqLow.getValue(), WithinAbs (0.25, 1e-6));
    }

    // Restored state reaches Param 1 before the host reads it back
    processor.prepareToPlay (testSampleRate, testBlockSize);
    juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), testBlockSize);
    juce::MidiBuffer midi;
    const auto processOneBlock = [&] {
        buffer.clear();
        processor.processBlock (buffer, midi);
    };

    param1.setValue (0.7f);
    processOneBlock();
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);

    param1.setValue (0.2f);
    processOneBlock();
    processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK_THAT (eqLow.getValue(), WithinAbs (0.7, 0.001));
    CHECK_THAT (param1.getValue(), WithinAbs (eqLow.getValue(), 1e-6));

    processor.releaseResources();
}

TEST_CASE ("The processor knows when the host is running it", "[processor]")
{
    PluginProcessor processor;
    CHECK_FALSE (processor.isBeingProcessed());

    processor.prepareToPlay (testSampleRate, testBlockSize);
    juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), testBlockSize);
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
    CHECK (processor.isBeingProcessed());

    // As when FL's smart disable stops calling it
    juce::Thread::sleep (600);
    CHECK_FALSE (processor.isBeingProcessed());

    processor.releaseResources();
}
