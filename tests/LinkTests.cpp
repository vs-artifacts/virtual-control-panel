#include <PluginProcessor.h>
#include <RelayHubFinder.h>
#include <catch2/catch_test_macros.hpp>
#include <engine/BayLink.h>
#include <link/LinkHub.h>
#include <relay/RelayProcessor.h>

namespace
{
    constexpr double rigRate = 48000.0;
    constexpr int rigBlock = 512;
    using Output = BayLink::Output;
    using Mode = RelayLink::Mode;

    BayLink::HubSearch inProcessHub()
    {
        return { heartseeker_link_get_api (HS_LINK_ABI_VERSION), {} };
    }

    // What the panel's engine plays: different at every sample and channel, so any shift or mix-up shows
    float sentSample (juce::int64 sample, int channel)
    {
        return sample < 0 ? 0.0f : static_cast<float> ((sample * 31 + channel * 17) % 20011) / 20011.0f - 0.5f;
    }

    enum class Order {
        panelFirst, // as with a Patcher wire from the panel into the receiver
        receiverFirst,
        random, // FL's first column runs on several threads, in no fixed order
    };

    // One Patcher: the panel and a receiver in the first column (in either order), the bay's plugin after the receiver,
    // the transmitter after that. Every piece can come and go.
    struct Patcher
    {
        Patcher() = default;

        ~Patcher()
        {
            // Relays first, as their timers would notice the panel going anyway
            receiver.reset();
            transmitter.reset();
            panel.reset();
        }

        void addPanel (int roundTripBlocks, Output output = Output::bayReturn)
        {
            panel = std::make_unique<BayLink> (inProcessHub);
            panel->setRoundTripBlocks (roundTripBlocks);
            panel->prepare (rigRate, rigBlock);
            panel->setOutput (output);
            roundTrip = panel->getRoundTripSamples();
        }

        static std::unique_ptr<RelayProcessor> makeRelay (Mode mode)
        {
            auto relay = std::make_unique<RelayProcessor>();
            relay->setMode (mode);
            relay->prepareToPlay (rigRate, rigBlock);
            relay->maintainLink();
            return relay;
        }

        void addReceiver() { receiver = makeRelay (Mode::receiver); }
        void addTransmitter() { transmitter = makeRelay (Mode::transmitter); }

        void addAll (int roundTripBlocks, Output output = Output::bayReturn)
        {
            addPanel (roundTripBlocks, output);
            addReceiver();
            addTransmitter();
        }

        // What the relays' and the panel's timers do
        void maintain()
        {
            if (panel != nullptr)
                panel->maintain();

            for (auto* relay : { receiver.get(), transmitter.get() })
                if (relay != nullptr)
                    relay->maintainLink();
        }

        void cycle (int numSamples, Order order)
        {
            const auto panelFirst = order == Order::panelFirst || (order == Order::random && random.nextBool());

            if (panelFirst)
                runPanel (numSamples);

            // The receiver's output, through the bay's plugin, into the transmitter
            juce::AudioBuffer<float> relayBuffer (2 * (1 + RelayProcessor::numControlOutputs), numSamples);
            relayBuffer.clear();
            juce::MidiBuffer midi;

            if (receiver != nullptr)
                receiver->processBlock (relayBuffer, midi);

            receiverPeak = std::max ({ receiverPeak, relayBuffer.getMagnitude (0, 0, numSamples), relayBuffer.getMagnitude (1, 0, numSamples) });

            if (!panelFirst)
                runPanel (numSamples);

            juce::AudioBuffer<float> wire (2, numSamples);
            wire.copyFrom (0, 0, relayBuffer, 0, 0, numSamples);
            wire.copyFrom (1, 0, relayBuffer, 1, 0, numSamples);
            bay (wire);

            if (transmitter != nullptr)
            {
                relayBuffer.clear();
                relayBuffer.copyFrom (0, 0, wire, 0, 0, numSamples);
                relayBuffer.copyFrom (1, 0, wire, 1, 0, numSamples);
                transmitter->processBlock (relayBuffer, midi);
            }
        }

        void run (int numBlocks, Order order, int blockSize = rigBlock)
        {
            for (int block = 0; block < numBlocks; ++block)
                cycle (blockSize, order);
        }

        // Varying block sizes, as FL can use
        void runVarying (int numBlocks, Order order)
        {
            for (int block = 0; block < numBlocks; ++block)
                cycle (random.nextInt ({ 1, rigBlock + 1 }), order);
        }

        void runPanel (int numSamples)
        {
            juce::AudioBuffer<float> buffer (2, numSamples);

            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < numSamples; ++i)
                    buffer.setSample (channel, i, sentSample (played + i, channel));

            if (panel != nullptr)
                panel->process (buffer);
            else
                buffer.clear();

            for (int i = 0; i < numSamples; ++i)
            {
                outLeft.push_back (buffer.getSample (0, i));
                outRight.push_back (buffer.getSample (1, i));
            }

            played += numSamples;
        }

        // Samples played from `from` on that aren't the sent audio exactly a round trip late; with silenceAllowed, silent
        // samples (a missing return) don't count either
        int countWrong (juce::int64 from, bool silenceAllowed = false) const
        {
            auto wrong = 0;

            for (auto sample = from; sample < played; ++sample)
            {
                const auto index = static_cast<size_t> (sample);
                const auto left = outLeft[index];
                const auto right = outRight[index];
                const auto exact = juce::exactlyEqual (left, sentSample (sample - roundTrip, 0)) && juce::exactlyEqual (right, sentSample (sample - roundTrip, 1));
                const auto silent = juce::exactlyEqual (left, 0.0f) && juce::exactlyEqual (right, 0.0f);

                if (!exact && !(silenceAllowed && silent))
                    ++wrong;
            }

            return wrong;
        }

        int countSilent (juce::int64 from) const
        {
            auto silent = 0;

            for (auto sample = from; sample < played; ++sample)
                if (juce::exactlyEqual (outLeft[static_cast<size_t> (sample)], 0.0f) && juce::exactlyEqual (outRight[static_cast<size_t> (sample)], 0.0f))
                    ++silent;

            return silent;
        }

        juce::uint64 relayMisses (RelayProcessor* relay) const
        {
            return relay != nullptr ? relay->getLink().getStatus().missed : 0;
        }

        std::unique_ptr<BayLink> panel;
        std::unique_ptr<RelayProcessor> receiver, transmitter;
        std::function<void (juce::AudioBuffer<float>&)> bay = [] (juce::AudioBuffer<float>&) {}; // FL's plugin
        juce::Random random { 1234 };
        juce::int64 played = 0;
        int roundTrip = 0;
        std::vector<float> outLeft, outRight;
        float receiverPeak = 0.0f; // the loudest the receiver has played
    };

    // Warm-up: the relays line up and the round trip fills
    constexpr int warmUpBlocks = 12;
}

TEST_CASE ("A bay returns the panel's audio exactly, a fixed round trip later, in any processing order", "[link]")
{
    struct Case
    {
        int roundTripBlocks;
        Order order;
        juce::uint64 mostReceiverMisses;
    };

    // 1 block needs the receiver to run after the panel; 2 may cost the bay one block of silence once, after linking
    // (the receiver lines up again, and the panel's return isn't affected); 3 never misses
    for (const auto& [blocks, order, mostReceiverMisses] : { Case { 1, Order::panelFirst, 0 },
             Case { 2, Order::panelFirst, 0 },
             Case { 2, Order::receiverFirst, 0 },
             Case { 2, Order::random, 1 },
             Case { 3, Order::panelFirst, 0 },
             Case { 3, Order::receiverFirst, 0 },
             Case { 3, Order::random, 0 } })
    {
        INFO ("round trip " << blocks << " blocks, order " << static_cast<int> (order));
        Patcher patcher;
        patcher.addAll (blocks);

        REQUIRE (patcher.panel->getStatus().linkName == "Panel 1");
        REQUIRE (patcher.receiver->getLink().getStatus().state == RelayLink::State::linked);
        REQUIRE (patcher.transmitter->getLink().getStatus().state == RelayLink::State::linked);
        CHECK (patcher.roundTrip == blocks * rigBlock);

        patcher.run (warmUpBlocks, order);
        patcher.panel->resetCounters();
        const auto from = patcher.played;
        patcher.run (200, order);

        const auto status = patcher.panel->getStatus();
        CHECK (patcher.countWrong (from) == 0);
        CHECK (status.missing == 0);
        CHECK (status.receiverLinked);
        CHECK (status.transmitterLinked);
        CHECK (status.differenceLargest.value_or (-1.0f) == 0.0f);
        CHECK (status.headroomLowest.value_or (-1) >= 0);
        CHECK (patcher.relayMisses (patcher.receiver.get()) <= mostReceiverMisses);
        CHECK (patcher.relayMisses (patcher.transmitter.get()) == 0);
    }
}

TEST_CASE ("With varying block sizes the return stays exact", "[link]")
{
    // As FL does: blocks of any size up to the largest it announced. 1 block needs the receiver after the panel.
    for (const auto& [blocks, order] : { std::pair { 1, Order::panelFirst }, std::pair { 2, Order::random }, std::pair { 3, Order::random } })
    {
        INFO ("round trip " << blocks << " blocks");
        Patcher patcher;
        patcher.addAll (blocks);
        patcher.runVarying (warmUpBlocks * 4, order);
        patcher.panel->resetCounters();
        const auto from = patcher.played;
        patcher.runVarying (400, order);

        const auto status = patcher.panel->getStatus();
        CHECK (patcher.countWrong (from) == 0);
        CHECK (status.missing == 0);
        CHECK (status.blockMin.value_or (0) >= 1);
        CHECK (status.blockMax.value_or (0) <= rigBlock);
    }
}

TEST_CASE ("With 1 block, a receiver that starts running after the panel catches up", "[link]")
{
    // As when a wire into the receiver is added after linking: the return was late, then it fits
    Patcher patcher;
    patcher.addAll (1);
    patcher.runVarying (warmUpBlocks * 4, Order::receiverFirst);
    patcher.runVarying (warmUpBlocks, Order::panelFirst);
    patcher.panel->resetCounters();
    const auto from = patcher.played;
    patcher.runVarying (400, Order::panelFirst);

    CHECK (patcher.countWrong (from) == 0);
    CHECK (patcher.panel->getStatus().missing == 0);
}

TEST_CASE ("A return that can't make the round trip shows as missing blocks", "[link]")
{
    // 1 block, but the receiver runs first: everything it plays arrives a block too late for the panel
    Patcher patcher;
    patcher.addAll (1);
    patcher.run (warmUpBlocks, Order::receiverFirst);
    patcher.panel->resetCounters();
    const auto from = patcher.played;
    patcher.run (100, Order::receiverFirst);

    CHECK (patcher.panel->getStatus().missing == 100);
    CHECK (patcher.countSilent (from) == 100 * rigBlock);
}

TEST_CASE ("Missing return blocks play silence or the sent audio; the direct sound is always a round trip late", "[link]")
{
    Patcher patcher;
    patcher.addPanel (2);

    SECTION ("no relays, silence")
    {
        patcher.run (20, Order::random);
        CHECK (patcher.countSilent (0) == 20 * rigBlock);
    }

    SECTION ("no relays, bypass")
    {
        patcher.panel->setMissingBlocks (BayLink::MissingBlocks::bypass);
        patcher.run (20, Order::random);
        CHECK (patcher.countWrong (0) == 0);
    }

    SECTION ("direct, with or without relays")
    {
        patcher.panel->setOutput (Output::direct);
        patcher.run (10, Order::random);
        patcher.addReceiver();
        patcher.addTransmitter();
        patcher.bay = [] (juce::AudioBuffer<float>& audio) { audio.applyGain (0.5f); };
        patcher.run (20, Order::random);
        CHECK (patcher.countWrong (0) == 0);
    }
}

TEST_CASE ("Relays and panels come and go in any order without stale audio", "[link]")
{
    // In bypass, a pass-through bay must give exactly the sent audio a round trip late at every moment: anything
    // else is stale or misplaced audio
    Patcher patcher;
    const auto bypassEverywhere = [&patcher] { patcher.panel->setMissingBlocks (BayLink::MissingBlocks::bypass); };

    SECTION ("relays first, then the panel")
    {
        patcher.addReceiver();
        patcher.addTransmitter();
        CHECK (patcher.receiver->getLink().getStatus().state == RelayLink::State::waitingForPanel);

        patcher.addPanel (2);
        bypassEverywhere();
        patcher.maintain();
        CHECK (patcher.receiver->getLink().getStatus().linkName == "Panel 1");
        CHECK (patcher.transmitter->getLink().getStatus().linkName == "Panel 1");

        patcher.run (100, Order::random);
        CHECK (patcher.countWrong (0) == 0);
    }

    SECTION ("the receiver goes and comes back")
    {
        patcher.addAll (2);
        bypassEverywhere();
        patcher.run (30, Order::random);
        patcher.receiver.reset();
        patcher.maintain();
        patcher.run (5, Order::random);
        patcher.panel->getStatus(); // "returning" means: since the last status
        patcher.run (25, Order::random);
        CHECK_FALSE (patcher.panel->getStatus().transmitterReturning);

        patcher.addReceiver();
        patcher.run (30, Order::random);
        CHECK (patcher.countWrong (0) == 0);
        CHECK (patcher.panel->getStatus().differenceLargest.value_or (-1.0f) == 0.0f);
    }

    SECTION ("the transmitter goes and comes back")
    {
        patcher.addAll (2);
        bypassEverywhere();
        patcher.run (30, Order::random);
        patcher.transmitter.reset();
        patcher.maintain();
        patcher.run (30, Order::random);
        patcher.addTransmitter();
        patcher.run (30, Order::random);
        CHECK (patcher.countWrong (0) == 0);
    }

    SECTION ("the panel goes and a new one comes")
    {
        patcher.addAll (2);
        patcher.run (30, Order::random);
        patcher.panel.reset();
        patcher.maintain();
        CHECK (patcher.receiver->getLink().getStatus().state == RelayLink::State::waitingForPanel);

        // Without a panel the receiver plays silence
        patcher.receiverPeak = 0.0f;
        patcher.run (10, Order::random);
        CHECK (juce::exactlyEqual (patcher.receiverPeak, 0.0f));

        patcher.addPanel (2);
        bypassEverywhere();
        const auto from = patcher.played;
        patcher.maintain();
        patcher.run (100, Order::random);

        // A new panel has nothing to play for its first round trip but silence
        CHECK (patcher.countSilent (from) >= patcher.roundTrip);
        CHECK (patcher.countWrong (from + patcher.roundTrip) == 0);
        CHECK (patcher.panel->getStatus().differenceLargest.value_or (-1.0f) == 0.0f);
    }

    SECTION ("in silence mode a missing return is silence, never old audio")
    {
        patcher.addAll (2);
        patcher.run (30, Order::random);
        patcher.receiver.reset();
        patcher.maintain();
        patcher.run (30, Order::random);
        patcher.addReceiver();
        patcher.run (30, Order::random);
        CHECK (patcher.countWrong (rigBlock * 2, true) == 0);
    }

    SECTION ("a second receiver or transmitter waits; a second panel gets its own link name")
    {
        patcher.addAll (2);
        const auto extraReceiver = Patcher::makeRelay (Mode::receiver);
        const auto extraTransmitter = Patcher::makeRelay (Mode::transmitter);
        CHECK (extraReceiver->getLink().getStatus().state == RelayLink::State::roleTaken);
        CHECK (extraTransmitter->getLink().getStatus().state == RelayLink::State::roleTaken);

        BayLink secondPanel (inProcessHub);
        secondPanel.prepare (rigRate, rigBlock);
        CHECK (secondPanel.getStatus().linkName == "Panel 2");

        extraReceiver->maintainLink();
        CHECK (extraReceiver->getLink().getStatus().linkName == "Panel 2");
    }

    SECTION ("switching a relay's mode drops its link and finds the other role")
    {
        patcher.addAll (2);
        patcher.transmitter.reset();
        patcher.receiver->setMode (Mode::transmitter);
        CHECK (patcher.receiver->getLink().getStatus().state == RelayLink::State::waitingForPanel);
        patcher.receiver->maintainLink();
        CHECK (patcher.receiver->getLink().getStatus().state == RelayLink::State::linked);
        CHECK (patcher.panel->getStatus().transmitterLinked);
        CHECK_FALSE (patcher.panel->getStatus().receiverLinked);
    }
}

TEST_CASE ("The difference readout and the lag finder see what the bay did", "[link]")
{
    Patcher patcher;
    patcher.addAll (2);

    const auto measureLag = [&patcher] {
        patcher.panel->measureLag();
        CHECK (patcher.panel->getStatus().lag.state == BayLink::Lag::State::measuring);
        patcher.run (8, Order::random);
        return patcher.panel->getStatus().lag;
    };

    SECTION ("a pass-through bay: identical, no lag")
    {
        patcher.run (warmUpBlocks, Order::random);
        const auto lag = measureLag();
        CHECK (lag.state == BayLink::Lag::State::found);
        CHECK (lag.exact);
        CHECK (lag.samples == 0);
        CHECK (patcher.panel->getStatus().differenceLargest.value_or (-1.0f) == 0.0f);
    }

    SECTION ("bays with latency of their own: 37 samples, and FL's linear-phase EQ 2 at its highest (12,288)")
    {
        for (const auto latency : { 37, 12288 })
        {
            INFO (latency);
            Patcher delayed;
            delayed.addAll (2);
            auto delayLine = std::make_shared<std::vector<float>> (static_cast<size_t> (2 * latency), 0.0f);
            auto next = std::make_shared<size_t> (0);

            delayed.bay = [delayLine, next, latency] (juce::AudioBuffer<float>& audio) {
                for (int i = 0; i < audio.getNumSamples(); ++i, *next = (*next + 1) % static_cast<size_t> (latency))
                {
                    for (int channel = 0; channel < 2; ++channel)
                    {
                        auto& stored = (*delayLine)[static_cast<size_t> (channel * latency) + *next];
                        const auto incoming = audio.getSample (channel, i);
                        audio.setSample (channel, i, stored);
                        stored = incoming;
                    }
                }
            };

            delayed.run (warmUpBlocks + latency / rigBlock, Order::random);
            delayed.panel->measureLag();
            delayed.run (8, Order::random);
            const auto lag = delayed.panel->getStatus().lag;
            CHECK (lag.state == BayLink::Lag::State::found);
            CHECK (lag.exact);
            CHECK (lag.samples == latency);
        }
    }

    SECTION ("a bay that changes the level: not exact, but the shape matches")
    {
        patcher.bay = [] (juce::AudioBuffer<float>& audio) { audio.applyGain (0.5f); };
        patcher.run (warmUpBlocks, Order::random);
        const auto lag = measureLag();
        CHECK (lag.state == BayLink::Lag::State::found);
        CHECK_FALSE (lag.exact);
        CHECK (lag.samples == 0);
        CHECK (lag.similarity > 0.999);
    }

    SECTION ("a silent bay")
    {
        patcher.bay = [] (juce::AudioBuffer<float>& audio) { audio.clear(); };
        patcher.run (warmUpBlocks, Order::random);
        CHECK (measureLag().state == BayLink::Lag::State::silent);
    }

    SECTION ("returned samples that aren't numbers are played as silence")
    {
        patcher.bay = [] (juce::AudioBuffer<float>& audio) { audio.setSample (0, 3, std::numeric_limits<float>::quiet_NaN()); };
        patcher.run (warmUpBlocks, Order::random);

        const auto status = patcher.panel->getStatus();
        CHECK (status.replaced > 0);

        for (const auto sample : patcher.outLeft)
            CHECK (std::isfinite (sample));
    }
}

TEST_CASE ("The lag finder", "[link]")
{
    constexpr int window = BayLink::lagWindow;
    constexpr int latest = 300;
    constexpr int earliest = 100;
    juce::Random random (99);
    std::vector<float> direct (static_cast<size_t> (window + latest + earliest));

    for (auto& sample : direct)
        sample = random.nextFloat() - 0.5f;

    // The return at `lag` samples late: the direct sound that many samples earlier
    const auto returnedAt = [&] (int lag, float gain) {
        std::vector<float> returned (static_cast<size_t> (window));

        for (int i = 0; i < window; ++i)
            returned[static_cast<size_t> (i)] = gain * direct[static_cast<size_t> (i + latest - lag)];

        return returned;
    };

    for (const auto lag : { 0, 1, 37, latest, -1, -earliest })
    {
        INFO (lag);
        const auto exact = BayLink::findLag (returnedAt (lag, 1.0f).data(), direct.data(), window, earliest, latest);
        CHECK (exact.state == BayLink::Lag::State::found);
        CHECK (exact.exact);
        CHECK (exact.samples == lag);

        const auto quieter = BayLink::findLag (returnedAt (lag, 0.25f).data(), direct.data(), window, earliest, latest);
        CHECK (quieter.state == BayLink::Lag::State::found);
        CHECK_FALSE (quieter.exact);
        CHECK_FALSE (quieter.ambiguous);
        CHECK (quieter.samples == lag);
        CHECK (quieter.similarity > 0.999);
    }

    SECTION ("a sound that repeats (a held note) fits many delays: the match is flagged as unclear")
    {
        constexpr int period = 50;
        std::vector<float> held (direct.size());

        for (size_t i = 0; i < held.size(); ++i)
            held[i] = std::sin (juce::MathConstants<float>::twoPi * static_cast<float> (i % period) / period);

        std::vector<float> returned (static_cast<size_t> (window));

        for (int i = 0; i < window; ++i)
            returned[static_cast<size_t> (i)] = 0.5f * held[static_cast<size_t> (i + latest - 37)];

        const auto found = BayLink::findLag (returned.data(), held.data(), window, earliest, latest);
        CHECK (found.state == BayLink::Lag::State::found);
        CHECK (found.ambiguous);
        CHECK ((found.samples - 37) % period == 0);
        CHECK ((found.alternative - 37) % period == 0);
    }

    const std::vector<float> silence (static_cast<size_t> (window), 0.0f);
    CHECK (BayLink::findLag (silence.data(), direct.data(), window, earliest, latest).state == BayLink::Lag::State::silent);

    std::vector<float> unrelated (static_cast<size_t> (window));

    for (auto& sample : unrelated)
        sample = random.nextFloat() - 0.5f;

    CHECK (BayLink::findLag (unrelated.data(), direct.data(), window, earliest, latest).state == BayLink::Lag::State::noMatch);
}

namespace
{
    // A host gives a VST3 its saved state as XML holding the plugin's own bytes (see juce_VST3PluginFormatImpl.h)
    void setHostedRelayMode (juce::AudioPluginInstance& relay, const char* mode)
    {
        juce::XmlElement relayState (RelayProcessor::stateTag);
        relayState.setAttribute ("mode", mode);
        juce::MemoryBlock relayBytes;
        juce::AudioProcessor::copyXmlToBinary (relayState, relayBytes);

        juce::XmlElement hostState ("VST3PluginState");
        hostState.createNewChildElement ("IComponent")->addTextElement (relayBytes.toBase64Encoding());
        juce::MemoryBlock hostBytes;
        juce::AudioProcessor::copyXmlToBinary (hostState, hostBytes);
        relay.setStateInformation (hostBytes.getData(), static_cast<int> (hostBytes.getSize()));
    }

    void pumpMessagesUntil (const std::function<bool()>& done, int timeoutMs = 5000)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);

        while (!done() && juce::Time::getMillisecondCounter() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }
}

TEST_CASE ("The panel finds the hub in the real relay file once a host has loaded it", "[link][relayfile]")
{
    // The relay's binary sits inside its bundle: heartseeker relay.vst3/Contents/x86_64-win/heartseeker relay.vst3
    const juce::File binary (HEARTSEEKER_RELAY_BINARY);
    const auto bundle = binary.getParentDirectory().getParentDirectory().getParentDirectory();

    if (!binary.existsAsFile())
        SKIP ("Not built: " << binary.getFullPathName());

    RelayHubFinder finder;
    const auto beforeLoading = finder.find();
    CHECK (beforeLoading.hub == nullptr);
    CHECK (beforeLoading.problem.isNotEmpty());

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);
    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3.findAllTypesForFile (descriptions, bundle.getFullPathName());
    REQUIRE (descriptions.size() == 1);

    juce::String error;
    auto receiver = formats.createPluginInstance (*descriptions[0], rigRate, rigBlock, error);
    auto transmitter = formats.createPluginInstance (*descriptions[0], rigRate, rigBlock, error);
    INFO (error);
    REQUIRE (receiver != nullptr);
    REQUIRE (transmitter != nullptr);
    setHostedRelayMode (*transmitter, "transmitter");

    const auto found = finder.find();
    REQUIRE (found.hub != nullptr);
    CHECK (found.problem.isEmpty());
    CHECK (found.hub != heartseeker_link_get_api (HS_LINK_ABI_VERSION)); // the file's own hub, not this program's

    {
        BayLink panel ([&finder] { return finder.find(); });
        panel.setRoundTripBlocks (2);
        panel.prepare (rigRate, rigBlock);
        panel.setOutput (Output::bayReturn);
        CHECK (panel.getStatus().linkName == "Panel 1");

        receiver->prepareToPlay (rigRate, rigBlock);
        transmitter->prepareToPlay (rigRate, rigBlock);

        // The relays link on their own timers, as in a host
        pumpMessagesUntil ([&panel] {
            const auto status = panel.getStatus();
            return status.receiverLinked && status.transmitterLinked;
        });

        const auto linked = panel.getStatus();
        REQUIRE (linked.receiverLinked);
        REQUIRE (linked.transmitterLinked);

        juce::int64 played = 0;
        auto wrong = 0;
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> relayBuffer (receiver->getTotalNumOutputChannels(), rigBlock);

        for (int block = 0; block < 60; ++block)
        {
            juce::AudioBuffer<float> panelBuffer (2, rigBlock);

            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < rigBlock; ++i)
                    panelBuffer.setSample (channel, i, sentSample (played + i, channel));

            panel.process (panelBuffer);
            relayBuffer.clear();
            receiver->processBlock (relayBuffer, midi);
            transmitter->processBlock (relayBuffer, midi);

            if (block >= warmUpBlocks)
                for (int i = 0; i < rigBlock; ++i)
                    if (!juce::exactlyEqual (panelBuffer.getSample (0, i), sentSample (played + i - 2 * rigBlock, 0)))
                        ++wrong;

            played += rigBlock;
        }

        CHECK (wrong == 0);
        CHECK (panel.getStatus().missing == 0);

        receiver->releaseResources();
        transmitter->releaseResources();
    }

    receiver.reset();
    transmitter.reset();
}

TEST_CASE ("The panel tells the host the round trip as latency, and keeps its output choice", "[processor][link]")
{
    juce::SharedResourcePointer<PluginSettings> settings;
    const auto previousBlocks = settings->getRoundTripBlocks();
    const auto previousMissing = settings->getMissingReturnBlocks();

    {
        settings->setRoundTripBlocks (3);
        PluginProcessor processor;
        processor.prepareToPlay (rigRate, rigBlock);
        const auto engineLatency = processor.getEngine().getLatencySamples();
        CHECK (processor.getLatencySamples() == engineLatency + 3 * rigBlock);

        // A change from any instance's Settings tab
        settings->setRoundTripBlocks (1);
        settings->setMissingReturnBlocks (BayLink::MissingBlocks::bypass);
        processor.applyLinkSettings();
        CHECK (processor.getLatencySamples() == engineLatency + rigBlock);
        CHECK (processor.getBayLink().getMissingBlocks() == BayLink::MissingBlocks::bypass);

        processor.getBayLink().setOutput (Output::bayReturn);
        juce::MemoryBlock saved;
        processor.getStateInformation (saved);
        processor.getBayLink().setOutput (Output::direct);
        processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        CHECK (processor.getBayLink().getOutput() == Output::bayReturn);

        processor.releaseResources();
    }

    settings->setRoundTripBlocks (previousBlocks);
    settings->setMissingReturnBlocks (previousMissing);
}
