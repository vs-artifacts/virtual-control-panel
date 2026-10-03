#include <PluginProcessor.h>
#include <RelayHubFinder.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <engine/BayLink.h>
#include <link/LinkHub.h>
#include <relay/RelayProcessor.h>

namespace
{
    constexpr double rigRate = 48000.0;
    constexpr int rigBlock = 512;
    using Dropouts = BayLink::Dropouts;
    using Mode = RelayLink::Mode;
    constexpr int relayChannels = 2 + RelayProcessor::numControlOutputs;

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

        void addPanel (int roundTripBlocks)
        {
            panel = std::make_unique<BayLink> (inProcessHub);
            panel->setRoundTripBlocks (roundTripBlocks);
            panel->prepare (rigRate, rigBlock);
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

        void addAll (int roundTripBlocks)
        {
            addPanel (roundTripBlocks);
            addReceiver();
            addTransmitter();
        }

        // What the relays' and the panel's timers do
        void maintain()
        {
            if (panel != nullptr)
            {
                panel->maintain();
                panel->updateRoundTrip();
                roundTrip = panel->getRoundTripSamples();
            }

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
            juce::AudioBuffer<float> relayBuffer (relayChannels, numSamples);
            relayBuffer.clear();
            juce::MidiBuffer midi;

            if (receiver != nullptr)
                receiver->processBlock (relayBuffer, midi);

            receiverPeak = std::max ({ receiverPeak, relayBuffer.getMagnitude (0, 0, numSamples), relayBuffer.getMagnitude (1, 0, numSamples) });

            // Each control output holds one level for the whole block, as a Peak Controller needs
            for (int control = 0; control < RelayProcessor::numControlOutputs; ++control)
            {
                const auto* samples = relayBuffer.getReadPointer (2 + control);
                controls[static_cast<size_t> (control)] = samples[0];

                if (!std::all_of (samples, samples + numSamples, [first = samples[0]] (float s) { return juce::exactlyEqual (s, first); }))
                    ++unsteadyBlocks;
            }

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

        // Varying block sizes, as FL can use (it announces its buffer length, then calls with blocks of up to 215)
        void runVarying (int numBlocks, Order order, int largest = rigBlock)
        {
            for (int block = 0; block < numBlocks; ++block)
                cycle (random.nextInt ({ 1, largest + 1 }), order);
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
        std::array<float, RelayProcessor::numControlOutputs> controls {}; // the receiver's control outputs, last block
        int unsteadyBlocks = 0; // control outputs that changed level within a block
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
    patcher.panel->setDropouts (Dropouts::silence);
    patcher.run (warmUpBlocks, Order::receiverFirst);
    patcher.panel->resetCounters();
    const auto from = patcher.played;
    patcher.run (100, Order::receiverFirst);

    CHECK (patcher.panel->getStatus().missing == 100);
    CHECK (patcher.countSilent (from) == 100 * rigBlock);
}

TEST_CASE ("Dropouts play the sent audio or silence; bypassed, the sent audio is always a round trip late", "[link]")
{
    Patcher patcher;
    patcher.addPanel (2);

    SECTION ("no relays, silence")
    {
        patcher.panel->setDropouts (Dropouts::silence);
        patcher.run (20, Order::random);
        CHECK (patcher.countSilent (0) == 20 * rigBlock);
    }

    SECTION ("no relays, safe (the default)")
    {
        CHECK (patcher.panel->getDropouts() == Dropouts::safe);
        patcher.run (20, Order::random);
        CHECK (patcher.countWrong (0) == 0);
    }

    SECTION ("bypassed, with or without relays")
    {
        patcher.panel->setBypass (true);
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
    // In safe mode (the default), a pass-through bay must give exactly the sent audio a round trip late at every
    // moment: anything else is stale or misplaced audio
    Patcher patcher;

    SECTION ("relays first, then the panel")
    {
        patcher.addReceiver();
        patcher.addTransmitter();
        CHECK (patcher.receiver->getLink().getStatus().state == RelayLink::State::waitingForPanel);

        patcher.addPanel (2);
        patcher.maintain();
        CHECK (patcher.receiver->getLink().getStatus().linkName == "Panel 1");
        CHECK (patcher.transmitter->getLink().getStatus().linkName == "Panel 1");

        patcher.run (100, Order::random);
        CHECK (patcher.countWrong (0) == 0);
    }

    SECTION ("the receiver goes and comes back")
    {
        patcher.addAll (2);
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
        patcher.panel->setDropouts (Dropouts::silence);
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

namespace
{
    // FL's linear-phase EQ 2 and the like, as far as delay goes: a bay plugin with latency of its own
    std::function<void (juce::AudioBuffer<float>&)> delayingBay (int latency, bool alsoUndelayed = false)
    {
        auto delayLine = std::make_shared<std::vector<float>> (static_cast<size_t> (2 * latency), 0.0f);
        auto next = std::make_shared<size_t> (0);

        return [delayLine, next, latency, alsoUndelayed] (juce::AudioBuffer<float>& audio) {
            for (int i = 0; i < audio.getNumSamples(); ++i, *next = (*next + 1) % static_cast<size_t> (latency))
            {
                for (int channel = 0; channel < 2; ++channel)
                {
                    auto& stored = (*delayLine)[static_cast<size_t> (channel * latency) + *next];
                    const auto incoming = audio.getSample (channel, i);
                    audio.setSample (channel, i, stored + (alsoUndelayed ? incoming : 0.0f));
                    stored = incoming;
                }
            }
        };
    }

    // Long enough for a measurement: the fade out, the whole capture, and the round trip
    constexpr int lagMeasureBlocks = (BayLink::lagEarliest + BayLink::lagBurst + BayLink::lagLatest) / rigBlock + 12;
}

TEST_CASE ("Lag > Measure finds every delay at which its burst comes back", "[link]")
{
    using Catch::Matchers::WithinAbs;
    using State = BayLink::Lag::State;

    const auto measure = [] (Patcher& patcher) {
        patcher.run (warmUpBlocks, Order::random);
        patcher.panel->measureLag();
        CHECK (patcher.panel->getStatus().lag.state == State::measuring);
        patcher.run (lagMeasureBlocks, Order::random);
        return patcher.panel->getStatus().lag;
    };

    SECTION ("a pass-through bay: an exact copy, no delay; nothing needs to be playing")
    {
        Patcher patcher;
        patcher.addAll (2);
        const auto lag = measure (patcher);
        CHECK (lag.state == State::found);
        CHECK (lag.exact);
        REQUIRE (lag.paths.size() == 1);
        CHECK (lag.paths[0].samples == 0);
        CHECK_THAT (lag.paths[0].gain, WithinAbs (1.0, 1e-6));
        CHECK (patcher.panel->getStatus().differenceLargest.value_or (-1.0f) == 0.0f);
    }

    SECTION ("bays with latency of their own: 37 samples, and FL's linear-phase EQ 2 at its highest (12,288)")
    {
        for (const auto latency : { 37, 12288 })
        {
            INFO (latency);
            Patcher patcher;
            patcher.addAll (2);
            patcher.bay = delayingBay (latency);
            const auto lag = measure (patcher);
            CHECK (lag.state == State::found);
            CHECK (lag.exact);
            REQUIRE (lag.paths.size() == 1);
            CHECK (lag.paths[0].samples == latency);
        }
    }

    SECTION ("two paths that merge (the Patcher experiment, uncompensated): both delays, each at full strength")
    {
        Patcher patcher;
        patcher.addAll (2);
        patcher.bay = delayingBay (12288, true);
        const auto lag = measure (patcher);
        CHECK (lag.state == State::found);
        REQUIRE (lag.paths.size() == 2);
        CHECK (std::min (lag.paths[0].samples, lag.paths[1].samples) == 0);
        CHECK (std::max (lag.paths[0].samples, lag.paths[1].samples) == 12288);
        // Whatever the panel sent before its burst is still in the delayed path, so not quite exact
        CHECK_THAT (lag.paths[0].gain, WithinAbs (1.0, 0.02));
        CHECK_THAT (lag.paths[1].gain, WithinAbs (1.0, 0.02));
    }

    SECTION ("a bay that changes the level: not exact, at the gain it applied")
    {
        Patcher patcher;
        patcher.addAll (2);
        patcher.bay = [] (juce::AudioBuffer<float>& audio) { audio.applyGain (0.5f); };
        const auto lag = measure (patcher);
        CHECK (lag.state == State::found);
        CHECK_FALSE (lag.exact);
        REQUIRE (lag.paths.size() == 1);
        CHECK (lag.paths[0].samples == 0);
        CHECK_THAT (lag.paths[0].gain, WithinAbs (0.5, 1e-6));
    }

    SECTION ("a silent bay, and no relays at all")
    {
        Patcher silent;
        silent.addAll (2);
        silent.bay = [] (juce::AudioBuffer<float>& audio) { audio.clear(); };
        CHECK (measure (silent).state == State::noMatch);

        Patcher alone;
        alone.addPanel (2);
        CHECK (measure (alone).state == State::noReturn);
    }

    SECTION ("the output is muted while it measures")
    {
        Patcher patcher;
        patcher.addAll (2);
        patcher.run (warmUpBlocks, Order::random);
        patcher.panel->measureLag();
        const auto from = patcher.played + juce::roundToInt (BayLink::muteFadeSeconds * rigRate);
        patcher.run (lagMeasureBlocks, Order::random);
        CHECK (patcher.countSilent (from) >= BayLink::lagEarliest + BayLink::lagBurst + BayLink::lagLatest);
    }
}

TEST_CASE ("Returned samples that aren't numbers are played as silence", "[link]")
{
    Patcher patcher;
    patcher.addAll (2);
    patcher.bay = [] (juce::AudioBuffer<float>& audio) { audio.setSample (0, 3, std::numeric_limits<float>::quiet_NaN()); };
    patcher.run (warmUpBlocks, Order::random);

    const auto status = patcher.panel->getStatus();
    CHECK (status.replaced > 0);

    for (const auto sample : patcher.outLeft)
        CHECK (std::isfinite (sample));
}

TEST_CASE ("The lag finder", "[link]")
{
    using Catch::Matchers::WithinAbs;
    constexpr int earliest = 100;
    constexpr int latest = 3000;
    const auto burst = BayLink::makeLagBurst();
    const auto length = static_cast<int> (burst.size());

    // What comes back: the burst at each delay and gain, from `earliest` samples before it was sent
    const auto capture = [&] (std::initializer_list<std::pair<int, float>> paths) {
        std::vector<float> captured (static_cast<size_t> (earliest + length + latest), 0.0f);

        for (const auto& [delay, gain] : paths)
            for (int i = 0; i < length; ++i)
                captured[static_cast<size_t> (earliest + delay + i)] += gain * burst[static_cast<size_t> (i)];

        return captured;
    };

    for (const auto delay : { 0, 1, 37, latest, -1, -earliest })
    {
        INFO (delay);
        const auto exact = BayLink::findLag (capture ({ { delay, 1.0f } }).data(), burst.data(), length, earliest, latest);
        CHECK (exact.state == BayLink::Lag::State::found);
        CHECK (exact.exact);
        REQUIRE (exact.paths.size() == 1);
        CHECK (exact.paths[0].samples == delay);

        const auto inverted = BayLink::findLag (capture ({ { delay, -0.25f } }).data(), burst.data(), length, earliest, latest);
        CHECK_FALSE (inverted.exact);
        REQUIRE (inverted.paths.size() == 1);
        CHECK (inverted.paths[0].samples == delay);
        CHECK_THAT (inverted.paths[0].gain, WithinAbs (-0.25, 1e-5));
    }

    SECTION ("paths are listed strongest first; weak echoes and a filter's ringing next to a path aren't paths")
    {
        const auto found = BayLink::findLag (capture ({ { 500, 0.4f }, { 20, 1.0f }, { 23, 0.3f }, { 2000, 0.05f } }).data(), burst.data(), length, earliest, latest);
        REQUIRE (found.paths.size() == 2);
        CHECK (found.paths[0].samples == 20);
        CHECK (found.paths[1].samples == 500);
        CHECK_THAT (found.paths[1].gain, WithinAbs (0.4, 0.02));
    }

    SECTION ("silence, and unrelated sound, don't match")
    {
        CHECK (BayLink::findLag (capture ({}).data(), burst.data(), length, earliest, latest).state == BayLink::Lag::State::noMatch);

        juce::Random random (99);
        std::vector<float> unrelated (static_cast<size_t> (earliest + length + latest));

        for (auto& sample : unrelated)
            sample = (2.0f * random.nextFloat() - 1.0f) * BayLink::lagBurstLevel;

        CHECK (BayLink::findLag (unrelated.data(), burst.data(), length, earliest, latest).state == BayLink::Lag::State::noMatch);
    }
}

TEST_CASE ("Control values reach the receiver's outputs exactly, in step with the audio", "[link]")
{
    for (const auto& [blocks, order] : { std::pair { 1, Order::panelFirst }, std::pair { 2, Order::receiverFirst }, std::pair { 2, Order::random }, std::pair { 3, Order::random } })
    {
        INFO ("round trip " << blocks << " blocks, order " << static_cast<int> (order));
        Patcher patcher;
        patcher.addAll (blocks);

        // FL's plugin: a level knob linked (through a Peak Controller) to control 1, reacting at once
        patcher.bay = [&patcher] (juce::AudioBuffer<float>& audio) { audio.applyGain (patcher.controls[0]); };

        patcher.panel->getControl (0).setValue (0.25f);
        patcher.panel->getControl (15).setValue (0.875f);
        patcher.run (warmUpBlocks, order);
        CHECK (juce::exactlyEqual (patcher.controls[0], 0.25f));
        CHECK (juce::exactlyEqual (patcher.controls[15], 0.875f));
        CHECK (juce::exactlyEqual (patcher.controls[1], 0.0f));

        // A change applies from the block it's made in: the return has the sent audio at the new level from exactly there
        const auto changedAt = patcher.played;
        patcher.panel->getControl (0).setValue (0.5f);
        patcher.run (20, order);

        auto wrong = 0;

        for (auto sample = changedAt - 4 * rigBlock; sample < patcher.played; ++sample)
        {
            const auto source = sample - patcher.roundTrip;
            const auto gain = source < changedAt ? 0.25f : 0.5f;

            if (!juce::exactlyEqual (patcher.outLeft[static_cast<size_t> (sample)], gain * sentSample (source, 0)))
                ++wrong;
        }

        CHECK (wrong == 0);
        CHECK (patcher.unsteadyBlocks == 0);
        CHECK (patcher.panel->getStatus().valueSets == 2); // only changes are sent
        CHECK (patcher.receiver->getLink().getStatus().controlsLive);
    }
}

TEST_CASE ("With varying block sizes, control outputs stay steady within each block and end exact", "[link]")
{
    Patcher patcher;
    patcher.addAll (2);
    juce::Random values (5);

    for (int round = 0; round < 50; ++round)
    {
        for (int control = 0; control < BayLink::numControls; ++control)
            patcher.panel->getControl (control).setValue (values.nextFloat());

        patcher.runVarying (8, Order::random);
    }

    // Long enough for the last change to arrive
    patcher.run (4, Order::random);

    for (int control = 0; control < BayLink::numControls; ++control)
        CHECK (juce::exactlyEqual (patcher.controls[static_cast<size_t> (control)], patcher.panel->getControl (control).getValue()));

    CHECK (patcher.unsteadyBlocks == 0);
}

TEST_CASE ("A receiver holds its control values without a panel, and keeps them with its state", "[link][relay]")
{
    Patcher patcher;
    patcher.addAll (2);
    patcher.panel->getControl (4).setValue (0.75f);
    patcher.run (warmUpBlocks, Order::random);
    REQUIRE (juce::exactlyEqual (patcher.controls[4], 0.75f));

    patcher.panel.reset();
    patcher.maintain();
    patcher.run (10, Order::random);
    CHECK (juce::exactlyEqual (patcher.controls[4], 0.75f));
    CHECK_FALSE (patcher.receiver->getLink().getStatus().controlsLive);

    // As when a project opens: the knobs stay where they were before any panel links
    juce::MemoryBlock saved;
    patcher.receiver->getStateInformation (saved);
    RelayProcessor reopened;
    reopened.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    reopened.prepareToPlay (rigRate, rigBlock);
    juce::AudioBuffer<float> buffer (relayChannels, rigBlock);
    buffer.clear();
    juce::MidiBuffer midi;
    reopened.processBlock (buffer, midi);
    CHECK (juce::exactlyEqual (buffer.getSample (2 + 4, 0), 0.75f));
    reopened.releaseResources();

    // A transmitter's control outputs are silent
    patcher.receiver->setMode (Mode::transmitter);
    patcher.run (2, Order::random);
    CHECK (juce::exactlyEqual (patcher.controls[4], 0.0f));
}

TEST_CASE ("The automatic round trip uses 1 block when the receiver runs after the panel, and 2 otherwise", "[link]")
{
    // The panel's timer checks twice a second; a second of returned audio without a miss settles on 1 block
    const auto settle = [] (Patcher& patcher, Order order) {
        for (int round = 0; round < 30; ++round)
        {
            patcher.run (10, order);
            patcher.maintain();
        }
    };

    Patcher patcher;
    patcher.addAll (BayLink::autoRoundTrip);
    CHECK (patcher.roundTrip == rigBlock);
    CHECK (patcher.panel->getStatus().autoState == BayLink::AutoState::checking);

    SECTION ("the receiver runs after the panel (a wire into it): 1 block")
    {
        settle (patcher, Order::panelFirst);
        const auto status = patcher.panel->getStatus();
        CHECK (status.autoState == BayLink::AutoState::oneBlock);
        CHECK (status.roundTripSamples == rigBlock);
        CHECK (status.missing == 0);
        CHECK (status.receiverMisses == 0);

        // Still watching: the wire removed later falls back too
        settle (patcher, Order::receiverFirst);
        CHECK (patcher.panel->getStatus().autoState == BayLink::AutoState::fellBack);
        CHECK (patcher.panel->getRoundTripSamples() == 2 * rigBlock);
    }

    SECTION ("the receiver runs first: 2 blocks, until it relinks running after the panel")
    {
        settle (patcher, Order::receiverFirst);
        auto status = patcher.panel->getStatus();
        CHECK (status.autoState == BayLink::AutoState::fellBack);
        CHECK (status.roundTripSamples == 2 * rigBlock);

        patcher.panel->resetCounters();
        patcher.run (100, Order::receiverFirst);
        CHECK (patcher.panel->getStatus().missing == 0);

        // A wire alone changes nothing until the relays link again
        settle (patcher, Order::panelFirst);
        CHECK (patcher.panel->getStatus().autoState == BayLink::AutoState::fellBack);

        // Switching the receiver's mode and back relinks it: 1 block is tried again, and works now
        patcher.receiver->setMode (Mode::transmitter);
        patcher.receiver->setMode (Mode::receiver);
        patcher.maintain();
        settle (patcher, Order::panelFirst);
        status = patcher.panel->getStatus();
        CHECK (status.autoState == BayLink::AutoState::oneBlock);
        CHECK (status.roundTripSamples == rigBlock);

        patcher.panel->resetCounters();
        const auto from = patcher.played;
        patcher.run (100, Order::panelFirst);
        CHECK (patcher.panel->getStatus().missing == 0);
        CHECK (patcher.countWrong (from) == 0);
    }

    SECTION ("a fixed round trip turns the check off")
    {
        patcher.panel->setRoundTripBlocks (3);
        CHECK (patcher.panel->getStatus().autoState == BayLink::AutoState::off);
        CHECK (patcher.panel->getRoundTripSamples() == 3 * rigBlock);
    }
}

TEST_CASE ("With FL's blocks, the automatic round trip sees the receiver's misses too", "[link]")
{
    // FL announces its buffer length (here 512) but calls with blocks of 1 to 215. At 1 block the panel's own return
    // then has room to spare whatever the order, but a receiver running first still misses: that must count.
    constexpr int flLargestBlock = 215;

    const auto settle = [] (Patcher& patcher, Order order) {
        for (int round = 0; round < 60; ++round)
        {
            patcher.runVarying (10, order, flLargestBlock);
            patcher.maintain();
        }
    };

    Patcher patcher;
    patcher.addAll (BayLink::autoRoundTrip);

    SECTION ("in no fixed order: falls back to 2 blocks")
    {
        settle (patcher, Order::random);
        const auto status = patcher.panel->getStatus();
        CHECK (status.receiverMisses > 0);
        CHECK (status.autoState == BayLink::AutoState::fellBack);
        CHECK (status.roundTripSamples == 2 * rigBlock);

        patcher.panel->resetCounters();
        const auto from = patcher.played;
        settle (patcher, Order::random);
        CHECK (patcher.panel->getStatus().missing == 0);
        CHECK (patcher.countWrong (from) == 0);
    }

    SECTION ("running after the panel: 1 block, with nothing missed anywhere")
    {
        settle (patcher, Order::panelFirst);
        const auto status = patcher.panel->getStatus();
        CHECK (status.autoState == BayLink::AutoState::oneBlock);
        CHECK (status.receiverMisses == 0);
        CHECK (status.missing == 0);
        CHECK (patcher.relayMisses (patcher.receiver.get()) == 0);
    }
}

TEST_CASE ("Missing blocks count while the return stops and starts", "[link]")
{
    // A receiver that misses now and then makes the transmitter skip blocks: the panel must still count what's missing
    Patcher patcher;
    patcher.addAll (1);
    patcher.panel->setDropouts (Dropouts::silence);
    patcher.runVarying (100, Order::random, 215);
    const auto status = patcher.panel->getStatus();
    CHECK (status.receiverMisses > 0);
    CHECK (status.missing > 0);
}

TEST_CASE ("The bypass switch crossfades, and a return coming back after a dropout fades in", "[link]")
{
    Patcher patcher;
    patcher.addAll (2);
    patcher.bay = [] (juce::AudioBuffer<float>& audio) { audio.applyGain (0.5f); };
    patcher.run (warmUpBlocks, Order::random);

    const auto fade = juce::roundToInt (BayLink::defaultCrossfadeMs * rigRate / 1000.0);

    // How much of a played sample is the return (0.5 × sent) rather than the sent audio; NaN where they're too close to tell
    const auto returnShare = [&patcher] (juce::int64 sample) {
        const auto sent = sentSample (sample - patcher.roundTrip, 0);
        const auto played = patcher.outLeft[static_cast<size_t> (sample)];
        return std::abs (sent) < 0.05f ? std::numeric_limits<float>::quiet_NaN() : (sent - played) / (0.5f * sent);
    };

    // The share should move by 1/fade per sample from `start`, from `from` to `to`
    const auto countOffRamp = [&] (juce::int64 start, float from, float to) {
        auto off = 0;

        for (int i = 0; i < fade; ++i)
        {
            const auto share = returnShare (start + i);
            const auto expected = from + (to - from) * static_cast<float> (i + 1) / static_cast<float> (fade);

            if (!std::isnan (share) && std::abs (share - expected) > 0.002f)
                ++off;
        }

        return off;
    };

    SECTION ("bypass on and off")
    {
        auto switchedAt = patcher.played;
        patcher.panel->setBypass (true);
        patcher.run (4, Order::random);
        CHECK (countOffRamp (switchedAt, 1.0f, 0.0f) == 0);
        CHECK (patcher.countWrong (switchedAt + fade) == 0); // then exactly the sent audio

        switchedAt = patcher.played;
        patcher.panel->setBypass (false);
        patcher.run (4, Order::random);
        CHECK (countOffRamp (switchedAt, 0.0f, 1.0f) == 0);

        // Then exactly the return
        auto notReturn = 0;

        for (auto sample = switchedAt + fade; sample < patcher.played; ++sample)
            if (!juce::exactlyEqual (patcher.outLeft[static_cast<size_t> (sample)], 0.5f * sentSample (sample - patcher.roundTrip, 0)))
                ++notReturn;

        CHECK (notReturn == 0);
    }

    SECTION ("safe mode: a dropout cuts to the sent audio at once, and the return fades back in")
    {
        patcher.transmitter.reset();
        patcher.maintain();
        patcher.run (10, Order::random);
        const auto dropped = patcher.played;
        patcher.addTransmitter();
        patcher.run (10, Order::random);

        // The first sample that isn't purely the sent audio starts the fade
        auto resumed = dropped;

        while (resumed < patcher.played && juce::exactlyEqual (patcher.outLeft[static_cast<size_t> (resumed)], sentSample (resumed - patcher.roundTrip, 0)))
            ++resumed;

        REQUIRE (resumed < patcher.played - fade);
        CHECK (countOffRamp (resumed, 0.0f, 1.0f) == 0);
    }
}

TEST_CASE ("The probe times a step on control 1 through the bay", "[link]")
{
    for (const auto lagBlocks : { 0, 1 })
    {
        INFO (lagBlocks << " blocks of lag in the bay's knob");
        Patcher patcher;
        patcher.addAll (2);
        patcher.panel->getControl (0).setValue (0.3f);

        // A level knob linked to control 1, reacting at once or a block late (as a Peak Controller might)
        auto previous = std::make_shared<float> (0.0f);

        patcher.bay = [&patcher, previous, lagBlocks] (juce::AudioBuffer<float>& audio) {
            const auto now = patcher.controls[0];
            audio.applyGain (lagBlocks == 0 ? now : *previous);
            *previous = now;
        };

        patcher.run (warmUpBlocks, Order::random);
        patcher.panel->startProbe();
        CHECK (patcher.panel->getStatus().probe.state == BayLink::Probe::State::running);

        const auto from = patcher.played;
        patcher.run (200, Order::random);
        const auto probe = patcher.panel->getStatus().probe;

        CHECK (probe.state == BayLink::Probe::State::found);
        CHECK (probe.delay == lagBlocks * rigBlock);
        CHECK (probe.rise == 0);
        CHECK (juce::exactlyEqual (probe.before, 0.0f));
        CHECK (juce::exactlyEqual (probe.after, BayLink::probeToneLevel));

        // Muted while it ran, after a short fade (the tone never reaches the output); afterwards control 1 is back
        // where it was
        const auto fadeOut = juce::roundToInt (BayLink::muteFadeSeconds * rigRate);
        const auto probeLength = juce::roundToInt ((BayLink::probeSettleSeconds + BayLink::probeAfterSeconds) * rigRate);
        auto loud = 0;

        for (auto sample = from + fadeOut; sample < from + fadeOut + probeLength; ++sample)
            if (!juce::exactlyEqual (patcher.outLeft[static_cast<size_t> (sample)], 0.0f))
                ++loud;

        CHECK (loud == 0);
        CHECK (std::abs (patcher.outLeft[static_cast<size_t> (from)]) > 0.0f); // fading, not cut
        CHECK (juce::exactlyEqual (patcher.controls[0], 0.3f));
    }
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
        panel.getControl (2).setValue (0.375f);
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
        auto wrongControls = 0;
        juce::MidiBuffer midi;
        REQUIRE (receiver->getTotalNumOutputChannels() == relayChannels);
        juce::AudioBuffer<float> relayBuffer (relayChannels, rigBlock);

        for (int block = 0; block < 60; ++block)
        {
            juce::AudioBuffer<float> panelBuffer (2, rigBlock);

            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < rigBlock; ++i)
                    panelBuffer.setSample (channel, i, sentSample (played + i, channel));

            panel.process (panelBuffer);
            relayBuffer.clear();
            receiver->processBlock (relayBuffer, midi);

            // The receiver's control output 3 holds the panel's exact value
            if (block >= warmUpBlocks)
                for (int i = 0; i < rigBlock; ++i)
                    if (!juce::exactlyEqual (relayBuffer.getSample (2 + 2, i), 0.375f) || !juce::exactlyEqual (relayBuffer.getSample (2 + 3, i), 0.0f))
                        ++wrongControls;

            transmitter->processBlock (relayBuffer, midi);

            if (block >= warmUpBlocks)
                for (int i = 0; i < rigBlock; ++i)
                    if (!juce::exactlyEqual (panelBuffer.getSample (0, i), sentSample (played + i - 2 * rigBlock, 0)))
                        ++wrong;

            played += rigBlock;
        }

        CHECK (wrong == 0);
        CHECK (wrongControls == 0);
        CHECK (panel.getStatus().missing == 0);

        receiver->releaseResources();
        transmitter->releaseResources();
    }

    receiver.reset();
    transmitter.reset();
}

TEST_CASE ("The panel tells the host the round trip as latency, and keeps its bypass switch and control values", "[processor][link]")
{
    juce::SharedResourcePointer<PluginSettings> settings;
    const auto previousBlocks = settings->getRoundTripBlocks();
    const auto previousDropouts = settings->getBayDropouts();
    const auto previousCrossfade = settings->getBypassCrossfadeMs();

    {
        settings->setRoundTripBlocks (3);
        PluginProcessor processor;
        processor.prepareToPlay (rigRate, rigBlock);
        const auto engineLatency = processor.getEngine().getLatencySamples();
        CHECK (processor.getLatencySamples() == engineLatency + 3 * rigBlock);

        // A change from any instance's Settings tab
        settings->setRoundTripBlocks (1);
        settings->setBayDropouts (Dropouts::silence);
        settings->setBypassCrossfadeMs (50);
        processor.applyLinkSettings();
        CHECK (processor.getLatencySamples() == engineLatency + rigBlock);
        CHECK (processor.getBayLink().getDropouts() == Dropouts::silence);
        CHECK (processor.getBayLink().getCrossfadeMs() == 50);

        // Automatic starts by trying 1 block
        settings->setRoundTripBlocks (BayLink::autoRoundTrip);
        processor.applyLinkSettings();
        CHECK (processor.getLatencySamples() == engineLatency + rigBlock);
        CHECK (processor.getBayLink().getStatus().autoState == BayLink::AutoState::checking);

        // The bay's controls follow the exposed parameters in the host's pool
        const auto firstBayControl = static_cast<int> (processor.getEngine().getExposedParameters().size());
        auto& hostControl = *processor.getParameters()[firstBayControl + 2];
        hostControl.setValue (0.625f);
        CHECK (juce::exactlyEqual (processor.getBayLink().getControl (2).getValue(), 0.625f));
        CHECK (processor.getBayLink().getControl (2).getText (0.625f, 20) == "62.50%");

        processor.getBayLink().setBypass (true);
        processor.getBayLink().getControl (15).setValue (0.1f);
        juce::MemoryBlock saved;
        processor.getStateInformation (saved);
        processor.getBayLink().setBypass (false);
        processor.getBayLink().getControl (2).setValue (0.0f);
        processor.getBayLink().getControl (15).setValue (0.0f);
        processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        CHECK (processor.getBayLink().isBypassed());
        CHECK (juce::exactlyEqual (processor.getBayLink().getControl (2).getValue(), 0.625f));
        CHECK (juce::exactlyEqual (processor.getBayLink().getControl (15).getValue(), 0.1f));
        CHECK (juce::exactlyEqual (hostControl.getValue(), 0.625f));

        // Projects saved before the bypass switch had an output switch: "direct" is bypassed
        for (const auto& [output, bypassed] : { std::pair { "direct", true }, std::pair { "return", false } })
        {
            juce::XmlElement old ("HeartseekerPanelState");
            old.setAttribute ("bayOutput", output);
            juce::MemoryBlock oldBytes;
            juce::AudioProcessor::copyXmlToBinary (old, oldBytes);
            processor.getBayLink().setBypass (!bypassed);
            processor.setStateInformation (oldBytes.getData(), static_cast<int> (oldBytes.getSize()));
            CHECK (processor.getBayLink().isBypassed() == bypassed);
        }

        processor.releaseResources();
    }

    settings->setRoundTripBlocks (previousBlocks);
    settings->setBayDropouts (previousDropouts);
    settings->setBypassCrossfadeMs (previousCrossfade);
}
