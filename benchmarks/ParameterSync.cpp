// Included from Catch2Main.cpp. Quote numbers from a Release build only.

namespace
{
    constexpr size_t morphedPerTick = 50;
    constexpr int syncBlockSize = 512;
    constexpr double syncSampleRate = 48000.0;

    // A slow morph: each parameter moves in small steps around where it started
    struct Morph
    {
        explicit Morph (const std::vector<juce::AudioProcessorParameter*>& controls)
        {
            // Spread across the whole list, as a morph between two presets would be
            for (size_t i = 0; i < morphedPerTick; ++i)
            {
                auto* parameter = controls[i * controls.size() / morphedPerTick];
                parameters.push_back (parameter);
                startValues.push_back (parameter->getValue());
            }
        }

        void step()
        {
            const auto offset = 0.05f * static_cast<float> (steps++ % 20 - 10) / 10.0f;

            for (size_t i = 0; i < parameters.size(); ++i)
                parameters[i]->setValue (juce::jlimit (0.0f, 1.0f, startValues[i] + offset));
        }

        std::vector<juce::AudioProcessorParameter*> parameters;
        std::vector<float> startValues;
        int steps = 0;
    };

    juce::String createSetMessage (const std::vector<juce::AudioProcessorParameter*>& controls, float value)
    {
        juce::Array<juce::var> batch;

        for (size_t i = 0; i < morphedPerTick; ++i)
        {
            auto* entry = new juce::DynamicObject();
            entry->setProperty ("h", static_cast<int> (i * controls.size() / morphedPerTick));
            entry->setProperty ("v", static_cast<double> (value));
            batch.add (entry);
        }

        return juce::JSON::toString (batch, true);
    }
}

TEST_CASE ("Parameter sync and morph cost (Serum 2)", "[sync]")
{
    if (!juce::File (serum2Path).exists())
        SKIP ("Not installed: " << serum2Path);

    Engine engine;
    engine.build (Structure::simple ({ "serum2" }, {}));
    REQUIRE (engine.getPlugins()[0]->getInstance() != nullptr);

    const auto& controls = engine.getPlugins()[0]->getControls();
    REQUIRE (controls.size() > morphedPerTick);

    // Every control exposed, so each poll checks all of them
    std::vector<ControlSession::Parameter> exposed;

    for (auto* control : controls)
        exposed.push_back ({ "Generator 1", "Serum 2", control->getName (ControlSession::maxTextLength), {}, control, control });

    ControlSession session (exposed);
    session.catalog();
    Morph morph (controls);

    BENCHMARK ("Poll every control, none changed")
    {
        return session.poll();
    };

    BENCHMARK ("Change 50, then poll every control into a params message")
    {
        morph.step();
        return juce::JSON::toString (session.poll(), true).length();
    };

    const juce::String setMessages[] { createSetMessage (controls, 0.45f), createSetMessage (controls, 0.55f) };
    int setMessageIndex = 0;

    BENCHMARK ("Parse and apply a set message with 50 values")
    {
        session.applySet (juce::JSON::parse (setMessages[setMessageIndex++ % 2]));
    };

    engine.prepare (syncSampleRate, syncBlockSize, false);
    juce::AudioBuffer<float> buffer (2, syncBlockSize);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
    engine.process (buffer, midi, nullptr);

    const auto render = [&] {
        buffer.clear();
        midi.clear();
        engine.process (buffer, midi, nullptr);
        return buffer.getSample (0, 0);
    };

    BENCHMARK ("Render a block (512 samples), holding a note")
    {
        return render();
    };

    BENCHMARK ("Render a block (512 samples), holding a note, 50 parameters changing")
    {
        morph.step();
        return render();
    };

    engine.release();
}
