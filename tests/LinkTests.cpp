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
    using Slot = BayLink::SlotState;
    using Mode = RelayLink::Mode;
    constexpr int relayChannels = 2 + RelayProcessor::numControlOutputs + 2 + 1; // main, controls, marker, tail cut
    constexpr int markerOutChannel = 2 + RelayProcessor::numControlOutputs;
    constexpr int tailCutChannel = markerOutChannel + 2;
    constexpr int markerInChannel = 2;

    // Every fade's length in the simulated Patcher, unless a test sets its own
    constexpr int testFadeMs = 30;

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

            // Safe dropouts, so a pass-through bay must give exactly the sent audio at every moment; tests of silence
            // choose it
            panel->setDropouts (Dropouts::safe);
            panel->setRealBlocks (realBlocks);

            for (const auto fade : BayLink::fades)
                panel->setFadeMs (fade, testFadeMs);

            panel->setLowestTempo (lowestTempo);
            panel->setRoundTripBlocks (roundTripBlocks);
            panel->prepare (rigRate, rigBlock);
            roundTrip = panel->getRoundTripSamples();
            latency = panel->getBayLatency();
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
                panel->updateBayLatency();
                panel->updateEventLog();
                roundTrip = panel->getRoundTripSamples();
                latency = panel->getBayLatency();

                if (const auto delay = panel->getDelaySamples(); delay != delays.back().second)
                    delays.emplace_back (played, delay);
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

            tailCut = relayBuffer.getSample (tailCutChannel, 0);

            if (!panelFirst)
                runPanel (numSamples);

            for (int i = 0; i < numSamples; ++i)
            {
                markerLeft.push_back (relayBuffer.getSample (markerOutChannel, i) * markerGain);
                markerRight.push_back (relayBuffer.getSample (markerOutChannel + 1, i) * markerGain);
            }

            juce::AudioBuffer<float> wire (2, numSamples);
            wire.copyFrom (0, 0, relayBuffer, 0, 0, numSamples);
            wire.copyFrom (1, 0, relayBuffer, 1, 0, numSamples);
            bay (wire);

            if (transmitter != nullptr)
            {
                relayBuffer.clear();
                relayBuffer.copyFrom (0, 0, wire, 0, 0, numSamples);
                relayBuffer.copyFrom (1, 0, wire, 1, 0, numSamples);

                // Patcher delays the marker's wire to line it up with the bay plugin's output
                if (markerDelay >= 0)
                {
                    const auto start = static_cast<juce::int64> (markerLeft.size()) - numSamples - markerDelay;

                    for (int i = 0; i < numSamples; ++i)
                    {
                        // As FL does when a bay plugin's latency grows: its compensation refills the wire with silence
                        if (markerSilence > 0)
                        {
                            --markerSilence;
                            continue;
                        }

                        if (const auto index = start + i; index >= 0)
                        {
                            relayBuffer.setSample (markerInChannel, i, markerLeft[static_cast<size_t> (index)]);
                            relayBuffer.setSample (markerInChannel + 1, i, markerRight[static_cast<size_t> (index)]);
                        }
                    }
                }

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
                    buffer.setSample (channel, i, source (played + i, channel));

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

        // Samples played from `from` on that aren't the sent audio exactly a round trip (plus the bay latency in use)
        // late; with silenceAllowed, silent samples (a missing return) don't count either
        int countWrong (juce::int64 from, bool silenceAllowed = false) const
        {
            auto wrong = 0;
            const auto delay = roundTrip + latency;

            for (auto sample = from; sample < played; ++sample)
            {
                const auto index = static_cast<size_t> (sample);
                const auto left = outLeft[index];
                const auto right = outRight[index];
                const auto exact = juce::exactlyEqual (left, sentSample (sample - delay, 0)) && juce::exactlyEqual (right, sentSample (sample - delay, 1));
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
        bool realBlocks = false; // the panel's round trip counts in announced blocks unless a test says otherwise
        double lowestTempo = 0.0;
        std::unique_ptr<RelayProcessor> receiver, transmitter;
        std::function<void (juce::AudioBuffer<float>&)> bay = [] (juce::AudioBuffer<float>&) {}; // FL's plugin
        juce::Random random { 1234 };
        juce::int64 played = 0;
        int roundTrip = 0;
        int latency = 0; // the bay latency the panel uses
        std::vector<std::pair<juce::int64, int>> delays { { 0, -1 } }; // from which sample each output delay applied
        std::vector<float> outLeft, outRight;
        float receiverPeak = 0.0f; // the loudest the receiver has played
        std::array<float, RelayProcessor::numControlOutputs> controls {}; // the receiver's control outputs, last block
        float tailCut = 0.0f; // the receiver's tail cut output, last block
        int unsteadyBlocks = 0; // control outputs that changed level within a block
        int markerDelay = -1; // samples; -1: no wire from the receiver's Marker output to the transmitter's Marker input
        int markerSilence = 0; // samples of silence still to come on the marker's wire
        float markerGain = 1.0f;
        std::function<float (juce::int64, int)> source = sentSample; // what the panel's engine plays
        std::vector<float> markerLeft, markerRight; // everything the receiver's Marker output has played
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
    // 1 block, but the receiver runs first: everything it plays arrives a block too late for the panel. The bay's sound
    // never arrives, so the sent audio plays (flagged), as before a bay's first return.
    Patcher patcher;
    patcher.addAll (1);
    patcher.panel->setDropouts (Dropouts::silence);
    patcher.run (30, Order::receiverFirst);
    patcher.panel->resetCounters();
    const auto from = patcher.played;
    patcher.run (100, Order::receiverFirst);

    const auto status = patcher.panel->getStatus();
    CHECK (status.missing == 100);
    CHECK (status.missingLate == 100);
    CHECK (status.awaitingBay);
    CHECK (patcher.countWrong (from) == 0);
}

TEST_CASE ("Dropouts are silence by default; the sent audio plays before the bay's sound first arrives, and in Mute", "[link]")
{
    CHECK (BayLink (inProcessHub).getDropouts() == Dropouts::silence);
    Patcher patcher;
    patcher.addPanel (2);
    patcher.panel->setDropouts (Dropouts::silence);

    SECTION ("no relays: the sent audio a round trip late, flagged")
    {
        patcher.run (20, Order::random);
        CHECK (patcher.countWrong (0) == 0);
        CHECK (patcher.panel->getStatus().awaitingBay);
    }

    SECTION ("muted, with or without relays")
    {
        patcher.panel->setSlotState (Slot::mute);
        patcher.run (10, Order::random);
        patcher.addReceiver();
        patcher.addTransmitter();
        patcher.bay = [] (juce::AudioBuffer<float>& audio) { audio.applyGain (0.5f); };
        patcher.run (20, Order::random);
        CHECK (patcher.countWrong (0) == 0);
        CHECK_FALSE (patcher.panel->getStatus().awaitingBay); // only Active waits
    }
}

TEST_CASE ("Relays and panels come and go in any order without stale audio", "[link]")
{
    // With safe dropouts, a pass-through bay must give exactly the sent audio a round trip late at every moment:
    // anything else is stale or misplaced audio
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
        // Without a crossfade, so a dropout is plain silence (no reversed fade) and the return comes back at once
        patcher.addAll (2);
        patcher.panel->setDropouts (Dropouts::silence);

        for (const auto fade : BayLink::fades)
            patcher.panel->setFadeMs (fade, 0);

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
    patcher.runVarying (300, Order::random, 215);
    const auto status = patcher.panel->getStatus();
    CHECK (status.receiverMisses > 0);
    CHECK (status.missing > 0);
}

TEST_CASE ("Mute crossfades, and a return coming back after a dropout fades in", "[link]")
{
    Patcher patcher;
    patcher.addAll (2);
    patcher.bay = [] (juce::AudioBuffer<float>& audio) { audio.applyGain (0.5f); };
    patcher.run (warmUpBlocks, Order::random);

    const auto fade = juce::roundToInt (testFadeMs * rigRate / 1000.0);

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

    SECTION ("mute on and off")
    {
        auto switchedAt = patcher.played;
        patcher.panel->setSlotState (Slot::mute);
        patcher.run (4, Order::random);
        CHECK (countOffRamp (switchedAt, 1.0f, 0.0f) == 0);
        CHECK (patcher.countWrong (switchedAt + fade) == 0); // then exactly the sent audio

        switchedAt = patcher.played;
        patcher.panel->setSlotState (Slot::active);
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

TEST_CASE ("A transmitter reads the bay plugin's latency from the receiver's position marker", "[link][marker]")
{
    struct Case
    {
        int latency;
        int roundTripBlocks;
        Order order;
        bool varying;
    };

    for (const auto& [latency, blocks, order, varying] : { Case { 0, 2, Order::panelFirst, false },
             Case { 3, 2, Order::panelFirst, false },
             Case { 576, 1, Order::panelFirst, true },
             Case { 12288, 2, Order::panelFirst, false },
             Case { 12480, 3, Order::random, true } })
    {
        INFO ("latency " << latency << ", round trip " << blocks << " blocks, varying blocks " << varying);
        Patcher patcher;
        patcher.addAll (blocks);
        patcher.markerDelay = latency;

        if (latency > 0)
            patcher.bay = delayingBay (latency);

        // Long enough for the marker to come through the bay plugin's latency
        const auto warmUp = warmUpBlocks + 2 * latency / rigBlock;
        varying ? patcher.runVarying (warmUp * 4, order) : patcher.run (warmUp, order);
        patcher.transmitter->getLink().resetCounters();
        varying ? patcher.runVarying (400, order) : patcher.run (200, order);

        const auto status = patcher.transmitter->getLink().getStatus();
        CHECK (status.marker == RelayLink::MarkerState::flowing);
        CHECK (status.bayLatencyNow == latency);
        CHECK (status.bayLatencyLowest == latency);
        CHECK (status.bayLatencyHighest == latency);
        CHECK (status.markerUnreadable == 0);
        CHECK (patcher.receiver->getLink().getStatus().marker == RelayLink::MarkerState::flowing);
    }

    SECTION ("without the wire, or with anything changing the marker on its way, there's no latency to read")
    {
        Patcher patcher;
        patcher.addAll (2);
        patcher.run (warmUpBlocks, Order::panelFirst);
        CHECK (patcher.transmitter->getLink().getStatus().marker == RelayLink::MarkerState::silent);

        patcher.markerDelay = 0;
        patcher.markerGain = 0.999f;
        patcher.run (warmUpBlocks, Order::panelFirst);
        patcher.transmitter->getLink().resetCounters();
        patcher.run (10, Order::panelFirst);

        const auto status = patcher.transmitter->getLink().getStatus();
        CHECK (status.marker == RelayLink::MarkerState::unreadable);
        CHECK (status.markerUnreadable == 10);
        CHECK_FALSE (status.bayLatencyNow.has_value());
        CHECK_FALSE (status.bayLatencyLowest.has_value());
    }
}

namespace
{
    // An ideal bay plugin with latency of its own, which may change at any moment: it plays its input exactly `latency`
    // samples late (wire the marker with the same delay, as Patcher's compensation does)
    std::function<void (juce::AudioBuffer<float>&)> latentBay (std::shared_ptr<int> latency)
    {
        auto input = std::make_shared<std::array<std::vector<float>, 2>>();

        return [input, latency] (juce::AudioBuffer<float>& audio) {
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                for (size_t channel = 0; channel < 2; ++channel)
                {
                    auto& stored = (*input)[channel];
                    stored.push_back (audio.getSample (static_cast<int> (channel), i));
                    const auto from = static_cast<juce::int64> (stored.size()) - 1 - *latency;
                    audio.setSample (static_cast<int> (channel), i, from >= 0 ? stored[static_cast<size_t> (from)] : 0.0f);
                }
            }
        };
    }

    // Blocks with the panel's and relays' timers in between, so a new bay latency can settle
    void runWithTimers (Patcher& patcher, int rounds, Order order, bool varying = false)
    {
        for (int round = 0; round < rounds; ++round)
        {
            varying ? patcher.runVarying (10, order) : patcher.run (10, order);
            patcher.maintain();
        }
    }

    constexpr int jumpFade = testFadeMs * static_cast<int> (rigRate) / 1000;

    // Samples from `from` on that aren't the sent audio at the output delay in use then (Patcher::delays), leaving out
    // the blend after each change of delay
    int countWrongByDelay (const Patcher& patcher, juce::int64 from)
    {
        auto wrong = 0;

        for (auto sample = from; sample < patcher.played; ++sample)
        {
            auto delay = -1;
            auto blending = false;

            for (const auto& [start, value] : patcher.delays)
            {
                if (start <= sample)
                {
                    delay = value;
                    blending = sample < start + jumpFade;
                }
            }

            const auto index = static_cast<size_t> (sample);

            if (delay >= 0 && !blending
                && (!juce::exactlyEqual (patcher.outLeft[index], sentSample (sample - delay, 0)) || !juce::exactlyEqual (patcher.outRight[index], sentSample (sample - delay, 1))))
                ++wrong;
        }

        return wrong;
    }
}

TEST_CASE ("With the marker wired, the panel plays a latent bay's return exactly a round trip plus its latency late", "[link][marker]")
{
    struct Case
    {
        int latency;
        int roundTripBlocks;
        Order order;
        bool varying;
    };

    for (const auto& [latency, blocks, order, varying] : { Case { 0, 2, Order::panelFirst, false },
             Case { 576, 1, Order::panelFirst, true },
             Case { 12288, 2, Order::random, false },
             Case { 12480, 3, Order::random, true } })
    {
        INFO ("latency " << latency << ", round trip " << blocks << " blocks, varying blocks " << varying);
        Patcher patcher;
        patcher.addAll (blocks);
        patcher.markerDelay = latency;
        patcher.bay = latentBay (std::make_shared<int> (latency));
        runWithTimers (patcher, varying ? 60 : 30, order, varying);

        REQUIRE (patcher.panel->getBayLatency() == latency);
        CHECK (patcher.panel->getDelaySamples() == patcher.roundTrip + latency);

        patcher.panel->resetCounters();
        const auto from = patcher.played;
        runWithTimers (patcher, 20, order, varying);

        const auto status = patcher.panel->getStatus();
        CHECK (patcher.countWrong (from) == 0);
        CHECK (status.missing == 0);
        CHECK (status.reading == BayLink::Reading::measured);
        CHECK (status.readingLowest == latency);
        CHECK (status.readingHighest == latency);
        CHECK_FALSE (status.settlingTo.has_value());
        CHECK (status.differenceLargest.value_or (-1.0f) == 0.0f); // the return is the sent audio, lined up exactly
        CHECK (patcher.transmitter->getLink().getStatus().stamp == RelayLink::Stamp::marker);
    }
}

TEST_CASE ("A new bay latency settles before it's used, and stale readings never place audio", "[link][marker]")
{
    Patcher patcher;
    patcher.addAll (2);
    patcher.panel->setSettleMs (50);
    auto latency = std::make_shared<int> (576);
    patcher.bay = latentBay (latency);
    patcher.markerDelay = 576;
    runWithTimers (patcher, 30, Order::random);
    REQUIRE (patcher.panel->getBayLatency() == 576);
    const auto from = patcher.played;

    // FL rearranging its compensation: stale marker audio for a block, then for three in a row
    for (const auto staleBlocks : { 1, 3 })
    {
        patcher.markerDelay = 25000;

        for (int block = 0; block < staleBlocks; ++block)
        {
            patcher.run (1, Order::random);
            patcher.maintain();
            CHECK (patcher.panel->getBayLatency() == 576);
        }

        patcher.markerDelay = 576;
        runWithTimers (patcher, 3, Order::random);
    }

    CHECK (patcher.panel->getBayLatency() == 576);
    CHECK (countWrongByDelay (patcher, from) == 0);

    SECTION ("a change shorter than the settle time is never used; the sent audio plays meanwhile")
    {
        patcher.panel->setSettleMs (1000);
        *latency = 192;
        patcher.markerDelay = 192;
        runWithTimers (patcher, 4, Order::random);

        auto status = patcher.panel->getStatus();
        CHECK (status.bayLatency == 576);
        CHECK (status.settlingTo == 192);
        CHECK (status.heldBack > 0);

        *latency = 576;
        patcher.markerDelay = 576;
        runWithTimers (patcher, 3, Order::random);
        status = patcher.panel->getStatus();
        CHECK (status.bayLatency == 576);
        CHECK_FALSE (status.settlingTo.has_value());
        CHECK (countWrongByDelay (patcher, from) == 0);
    }

    SECTION ("with silent dropouts, what's held back while it settles is silence")
    {
        patcher.panel->setDropouts (Dropouts::silence);
        patcher.panel->setSettleMs (1000);
        *latency = 192;
        patcher.markerDelay = 192;
        const auto changed = patcher.played;
        runWithTimers (patcher, 4, Order::random);

        const auto status = patcher.panel->getStatus();
        CHECK (status.settlingTo == 192);
        CHECK (patcher.countSilent (changed) > rigBlock);
        CHECK (status.events.joinIntoString ("\n").contains ("Bay latency: settling to 192 (in use: 576)"));
    }

    SECTION ("a lasting change is used once it has held, with one blended jump")
    {
        *latency = 12288;
        patcher.markerDelay = 12288;
        runWithTimers (patcher, 40, Order::random);

        CHECK (patcher.panel->getBayLatency() == 12288);
        CHECK (patcher.panel->getDelaySamples() == patcher.roundTrip + 12288);
        CHECK (countWrongByDelay (patcher, from) == 0);

        // The jump blends the old timeline into the new over the crossfade
        REQUIRE (patcher.delays.size() >= 2);
        const auto [jumpedAt, newDelay] = patcher.delays.back();
        const auto oldDelay = patcher.delays[patcher.delays.size() - 2].second;
        REQUIRE (newDelay == patcher.roundTrip + 12288);
        auto offRamp = 0;

        for (int i = 0; i < jumpFade; ++i)
        {
            const auto sample = jumpedAt + i;
            const auto oldSample = sentSample (sample - oldDelay, 0);
            const auto newSample = sentSample (sample - newDelay, 0);
            const auto oldShare = 1.0f - static_cast<float> (i + 1) / static_cast<float> (jumpFade);
            const auto expected = newSample + oldShare * (oldSample - newSample);

            if (std::abs (patcher.outLeft[static_cast<size_t> (sample)] - expected) > 1.0e-3f)
                ++offRamp;
        }

        CHECK (offRamp == 0);
    }
}

TEST_CASE ("A transmitter holds back what it can't place, and stamps by the receiver once the marker has gone", "[link][marker]")
{
    Patcher patcher;
    patcher.addAll (2);
    auto latency = std::make_shared<int> (576);
    patcher.bay = latentBay (latency);
    patcher.markerDelay = 576;
    runWithTimers (patcher, 30, Order::panelFirst);
    REQUIRE (patcher.panel->getBayLatency() == 576);
    auto& transmitter = patcher.transmitter->getLink();
    const auto from = patcher.played;

    SECTION ("the marker's wire cut: held back for a moment, then stamped by the receiver's position, which counts as 0")
    {
        patcher.markerDelay = -1;
        patcher.run (2, Order::panelFirst);
        auto status = transmitter.getStatus();
        CHECK (status.markerHeld);
        CHECK (status.stamp == RelayLink::Stamp::marker);
        CHECK (patcher.panel->getStatus().reading == BayLink::Reading::held);

        patcher.run ((RelayLink::markerHoldSamples + rigBlock) / rigBlock + 1, Order::panelFirst);
        status = transmitter.getStatus();
        CHECK_FALSE (status.markerHeld);
        CHECK (status.stamp == RelayLink::Stamp::receiver);
        CHECK (status.fallbacks == 1);
        CHECK (status.prepares == 1);

        runWithTimers (patcher, 5, Order::panelFirst);
        CHECK (patcher.panel->getBayLatency() == 0);
        CHECK (patcher.panel->getStatus().reading == BayLink::Reading::none);
    }

    SECTION ("a bay latency above the cap: never returned, and the sent audio plays")
    {
        *latency = HS_LINK_MAX_LATENCY + 1000;
        patcher.markerDelay = HS_LINK_MAX_LATENCY + 1000;
        runWithTimers (patcher, 5, Order::panelFirst);
        patcher.panel->getStatus(); // "returning" means: since the last status
        runWithTimers (patcher, 5, Order::panelFirst);

        const auto status = patcher.panel->getStatus();
        CHECK (status.reading == BayLink::Reading::outOfRange);
        CHECK (status.readingSamples == HS_LINK_MAX_LATENCY + 1000);
        CHECK_FALSE (status.transmitterReturning);
        CHECK (status.bayLatency == 576);
        CHECK (patcher.countWrong (from) == 0);
    }

    SECTION ("an unreadable marker: never returned, and the sent audio plays")
    {
        patcher.markerGain = 0.999f;
        runWithTimers (patcher, 2, Order::panelFirst);
        patcher.panel->getStatus();
        runWithTimers (patcher, 5, Order::panelFirst);

        const auto status = patcher.panel->getStatus();
        CHECK (status.reading == BayLink::Reading::unreadable);
        CHECK_FALSE (status.transmitterReturning);
        CHECK (status.bayLatency == 576);
        CHECK (patcher.countWrong (from) == 0);
    }
}

TEST_CASE ("Slot states: Off plays the engine's sound at once; Bypass and Off send the bay silence and cut its tails; Mute keeps it fed", "[link]")
{
    Patcher patcher;
    patcher.addAll (2);
    patcher.panel->setDropouts (Dropouts::silence);
    patcher.bay = [] (juce::AudioBuffer<float>& audio) { audio.applyGain (0.5f); };
    patcher.run (warmUpBlocks, Order::random);
    REQUIRE_FALSE (patcher.panel->getStatus().awaitingBay);
    CHECK (juce::exactlyEqual (patcher.tailCut, 0.0f));

    // Samples from `from` on that aren't exactly `gain` times the sent audio `delay` samples late
    const auto countNot = [&patcher] (juce::int64 from, int delay, float gain) {
        auto wrong = 0;

        for (auto sample = from; sample < patcher.played; ++sample)
        {
            const auto index = static_cast<size_t> (sample);

            if (!juce::exactlyEqual (patcher.outLeft[index], gain * sentSample (sample - delay, 0)) || !juce::exactlyEqual (patcher.outRight[index], gain * sentSample (sample - delay, 1)))
                ++wrong;
        }

        return wrong;
    };

    // Samples from `from` to `to` (or the end) that are silent where the sent audio isn't
    const auto countSilentGaps = [&patcher] (juce::int64 from, juce::int64 to = -1) {
        auto silent = 0;

        for (auto sample = from; sample < (to < 0 ? patcher.played : to); ++sample)
            if (std::abs (patcher.outLeft[static_cast<size_t> (sample)]) < 1.0e-6f && std::abs (sentSample (sample - patcher.roundTrip, 0)) > 0.01f)
                ++silent;

        return silent;
    };

    SECTION ("Off: the engine's sound at once, the bay gets silence and the tail cut; back to Active, never the silence it got")
    {
        patcher.panel->setSlotState (Slot::off);
        CHECK (patcher.panel->getDelaySamples() == 0);
        const auto dropped = patcher.played;
        patcher.run (8, Order::random);
        patcher.receiverPeak = 0.0f;
        patcher.run (16, Order::random);
        CHECK (juce::exactlyEqual (patcher.receiverPeak, 0.0f));
        CHECK (juce::exactlyEqual (patcher.tailCut, 1.0f));
        CHECK (countNot (dropped + jumpFade, 0, 1.0f) == 0);

        patcher.panel->setSlotState (Slot::active);
        CHECK (patcher.panel->getDelaySamples() == patcher.roundTrip);
        const auto back = patcher.played;
        patcher.run (30, Order::random);

        // The sent audio a round trip late until the bay's sound returns, then the bay's sound
        CHECK (countSilentGaps (back + jumpFade) == 0);
        CHECK (countNot (patcher.played - 5 * rigBlock, patcher.roundTrip, 0.5f) == 0);
        CHECK (juce::exactlyEqual (patcher.tailCut, 0.0f));
        CHECK_FALSE (patcher.panel->getStatus().awaitingBay);
    }

    SECTION ("Bypass: the sent audio at the same delay, the bay gets silence and the tail cut; leaving it waits out the release")
    {
        constexpr int releaseMs = 100;
        constexpr int release = releaseMs * static_cast<int> (rigRate) / 1000;
        patcher.panel->setTailCutReleaseMs (releaseMs);
        patcher.panel->setSlotState (Slot::bypass);
        CHECK (patcher.panel->getDelaySamples() == patcher.roundTrip);
        const auto bypassed = patcher.played;
        patcher.run (8, Order::random);
        patcher.receiverPeak = 0.0f;
        patcher.run (16, Order::random);
        CHECK (juce::exactlyEqual (patcher.receiverPeak, 0.0f));
        CHECK (juce::exactlyEqual (patcher.tailCut, 1.0f));
        CHECK (countNot (bypassed + jumpFade, patcher.roundTrip, 1.0f) == 0);

        // Back to Active: exactly the sent audio until the return passes the silence, the release and the send's fade-in,
        // then a crossfade into the bay's sound
        patcher.panel->setSlotState (Slot::active);
        const auto back = patcher.played;
        patcher.run (40, Order::random);
        CHECK (juce::exactlyEqual (patcher.tailCut, 0.0f));

        auto notSent = 0;

        for (auto sample = back; sample < back + patcher.roundTrip + release + jumpFade; ++sample)
            if (!juce::exactlyEqual (patcher.outLeft[static_cast<size_t> (sample)], sentSample (sample - patcher.roundTrip, 0)))
                ++notSent;

        CHECK (notSent == 0);
        CHECK (countSilentGaps (back) == 0);
        CHECK (countNot (patcher.played - 5 * rigBlock, patcher.roundTrip, 0.5f) == 0);
    }

    SECTION ("Mute: the sent audio at the same delay; the bay keeps getting the panel's sound, no tail cut")
    {
        patcher.panel->setSlotState (Slot::mute);
        const auto muted = patcher.played;
        patcher.run (4, Order::random);
        patcher.receiverPeak = 0.0f;
        patcher.run (16, Order::random);
        CHECK (patcher.receiverPeak > 0.0f);
        CHECK (juce::exactlyEqual (patcher.tailCut, 0.0f));
        CHECK (countNot (muted + jumpFade, patcher.roundTrip, 1.0f) == 0);

        patcher.panel->setSlotState (Slot::active);
        patcher.run (10, Order::random);
        CHECK (countNot (patcher.played - 5 * rigBlock, patcher.roundTrip, 0.5f) == 0);
    }

    SECTION ("leaving a state while the bay's sound can't come back: the sent audio plays on, flagged")
    {
        patcher.panel->setSlotState (Slot::bypass);
        patcher.run (10, Order::random);
        patcher.transmitter.reset();
        patcher.maintain();
        patcher.panel->setSlotState (Slot::active);
        const auto back = patcher.played;
        patcher.run (30, Order::random);
        patcher.maintain();

        CHECK (countNot (back, patcher.roundTrip, 1.0f) == 0);
        CHECK (patcher.panel->getStatus().awaitingBay);
        CHECK (patcher.panel->getStatus().events.joinIntoString ("\n").contains ("Waiting for the bay's sound"));

        // It comes back: a crossfade into it, and no longer flagged
        patcher.addTransmitter();
        patcher.run (30, Order::random);
        CHECK (countSilentGaps (back) == 0);
        CHECK (countNot (patcher.played - 5 * rigBlock, patcher.roundTrip, 0.5f) == 0);
        CHECK_FALSE (patcher.panel->getStatus().awaitingBay);
    }
}

TEST_CASE ("A silent dropout starts with a reversed copy of what played, fading out, then silence; the return fades back in", "[link]")
{
    Patcher patcher;
    patcher.addAll (2);
    patcher.panel->setDropouts (Dropouts::silence);

    // A bay that plays a smooth tone, so any jump in the output shows
    constexpr float toneLevel = 0.5f;
    constexpr double toneStep = juce::MathConstants<double>::twoPi * 440.0 / rigRate;
    auto phase = std::make_shared<double> (0.0);

    patcher.bay = [phase] (juce::AudioBuffer<float>& audio) {
        for (int i = 0; i < audio.getNumSamples(); ++i, *phase += toneStep)
        {
            const auto sample = toneLevel * static_cast<float> (std::sin (*phase));
            audio.setSample (0, i, sample);
            audio.setSample (1, i, sample);
        }
    };

    // Long enough for missing blocks to count (they don't just after the round trip is set)
    patcher.run (30, Order::random);
    const auto removed = patcher.played;
    patcher.transmitter.reset();
    patcher.maintain();
    patcher.run (20, Order::random);
    patcher.addTransmitter();
    patcher.run (20, Order::random);

    // No step between neighbouring samples beyond the tone's own (and a fade's)
    const auto largestToneStep = toneLevel * static_cast<float> (toneStep);
    auto largestStep = 0.0f;

    for (auto sample = removed; sample < patcher.played; ++sample)
        largestStep = std::max (largestStep, std::abs (patcher.outLeft[static_cast<size_t> (sample)] - patcher.outLeft[static_cast<size_t> (sample - 1)]));

    CHECK (largestStep < 1.1f * largestToneStep);

    // The silence: a block or more of it after the transmitter went
    const auto silentAt = [&patcher] (juce::int64 sample) { return juce::exactlyEqual (patcher.outLeft[static_cast<size_t> (sample)], 0.0f); };
    auto silentFrom = removed;

    while (silentFrom < patcher.played && !silentAt (silentFrom))
        ++silentFrom;

    auto silentTo = silentFrom;

    while (silentTo < patcher.played && silentAt (silentTo))
        ++silentTo;

    REQUIRE (silentTo - silentFrom >= rigBlock);

    // Before it, the fade: the samples before the dropout, reversed, under a falling ramp ending at the first silent one
    const auto fade = testFadeMs * static_cast<int> (rigRate) / 1000;
    const auto dropoutStart = silentFrom - (fade - 1);
    auto offMirror = 0;

    for (int k = 0; k < fade; ++k)
    {
        const auto gain = 1.0f - static_cast<float> (k + 1) / static_cast<float> (fade);
        const auto expected = gain * patcher.outLeft[static_cast<size_t> (dropoutStart - 1 - k)];

        if (std::abs (patcher.outLeft[static_cast<size_t> (dropoutStart + k)] - expected) > 1.0e-6f)
            ++offMirror;
    }

    CHECK (offMirror == 0);

    // And it's back
    CHECK (std::abs (patcher.outLeft.back()) + std::abs (patcher.outLeft[patcher.outLeft.size() - 100]) > 0.0f);
}

TEST_CASE ("Bypass fades the bay's send, cuts its tails only once it's silent, and brings it back on silence after the release", "[link]")
{
    Patcher patcher;
    patcher.addAll (2);
    patcher.source = [] (juce::int64, int) { return 0.5f; };

    // What the bay's plugin gets, sample by sample, with the tail cut in effect for its block
    auto input = std::make_shared<std::vector<std::pair<float, float>>>();
    patcher.bay = [&patcher, input] (juce::AudioBuffer<float>& audio) {
        for (int i = 0; i < audio.getNumSamples(); ++i)
            input->emplace_back (audio.getSample (0, i), patcher.tailCut);
    };

    patcher.run (warmUpBlocks, Order::panelFirst);
    constexpr int releaseMs = 20;
    constexpr int release = releaseMs * static_cast<int> (rigRate) / 1000;
    patcher.panel->setTailCutReleaseMs (releaseMs);
    const auto from = input->size();
    const auto playedFrom = patcher.played;
    patcher.panel->setSlotState (Slot::bypass);
    patcher.run (10, Order::panelFirst);
    patcher.panel->setSlotState (Slot::active);
    patcher.run (20, Order::panelFirst);

    // No step beyond the fade's own; the tail cut only while the bay gets silence
    auto largestStep = 0.0f;
    auto cutWhileSounding = 0;
    auto ramping = 0;

    for (auto i = from; i < input->size(); ++i)
    {
        const auto [sample, tailCut] = (*input)[i];
        largestStep = std::max (largestStep, std::abs (sample - (*input)[i - 1].first));

        if (tailCut > 0.0f && !juce::exactlyEqual (sample, 0.0f))
            ++cutWhileSounding;

        if (sample > 0.0f && sample < 0.5f)
            ++ramping;
    }

    CHECK (largestStep <= 0.5f / static_cast<float> (jumpFade) + 1.0e-6f);
    CHECK (cutWhileSounding == 0);
    CHECK (ramping >= 2 * (jumpFade - 2));
    CHECK (ramping <= 2 * jumpFade);

    // After the tail cut ends, silence for the release before the send fades in
    auto cutEnds = from;

    while (cutEnds < input->size() && !((*input)[cutEnds - 1].second > 0.0f && juce::exactlyEqual ((*input)[cutEnds].second, 0.0f)))
        ++cutEnds;

    auto sounds = cutEnds;

    while (sounds < input->size() && juce::exactlyEqual ((*input)[sounds].first, 0.0f))
        ++sounds;

    REQUIRE (sounds < input->size());
    CHECK (static_cast<int> (sounds - cutEnds) >= release);

    // Meanwhile the panel's output never moved
    auto moved = 0;

    for (auto sample = playedFrom; sample < patcher.played; ++sample)
        if (std::abs (patcher.outLeft[static_cast<size_t> (sample)] - 0.5f) > 1.0e-6f)
            ++moved;

    CHECK (moved == 0);
}

TEST_CASE ("A bay latency that grows while FL refills the marker's wire with silence is held back, not missing; Automatic stays at 1 block", "[link][marker]")
{
    Patcher patcher;
    patcher.addAll (BayLink::autoRoundTrip);
    auto latency = std::make_shared<int> (576);
    patcher.bay = latentBay (latency);
    patcher.markerDelay = 576;
    runWithTimers (patcher, 60, Order::panelFirst);
    REQUIRE (patcher.panel->getStatus().autoState == BayLink::AutoState::oneBlock);
    REQUIRE (patcher.panel->getBayLatency() == 576);

    patcher.panel->resetCounters();
    patcher.transmitter->getLink().resetCounters();
    *latency = 12480;
    patcher.markerDelay = 12480;
    patcher.markerSilence = 12480;
    runWithTimers (patcher, 20, Order::panelFirst);

    const auto status = patcher.panel->getStatus();
    INFO (status.events.joinIntoString ("\n"));
    CHECK (status.bayLatency == 12480);
    CHECK (status.missing == 0);
    CHECK (status.heldByTransmitter > 0);
    CHECK (status.autoState == BayLink::AutoState::oneBlock);
    CHECK (status.roundTripSamples == rigBlock);
    CHECK (patcher.transmitter->getLink().getStatus().fallbacks == 0);
}

TEST_CASE ("Missing blocks and the automatic round trip ignore a moment after any change of delay; the event log says why", "[link]")
{
    // 2 blocks with the receiver first works; 1 block then misses every block
    Patcher patcher;
    patcher.addAll (2);
    patcher.run (30, Order::receiverFirst);
    patcher.panel->resetCounters();
    patcher.panel->setRoundTripBlocks (1);
    patcher.maintain();

    // Inside the window: nothing counted
    patcher.run (10, Order::receiverFirst);
    CHECK (patcher.panel->getStatus().missing == 0);

    patcher.run (30, Order::receiverFirst);
    patcher.maintain();
    const auto status = patcher.panel->getStatus();
    CHECK (status.missing > 0);

    const auto log = status.events.joinIntoString ("\n");
    INFO (log);
    CHECK (log.contains ("Prepared: 48000 Hz, blocks of 512; round trip 1024 samples"));
    CHECK (log.contains ("Round trip: 1 block (512 samples), set in Settings"));
    CHECK (log.contains ("blocks missing (late"));
    CHECK (status.events.size() <= BayLink::maxEvents);
}

namespace
{
    // FL's real blocks at 140 BPM, 48 kHz: one sequencer tick (at most 215 samples) while it announces 512
    constexpr int flTick = 215;

    int unitFor (int largest)
    {
        return static_cast<int> (std::ceil (BayLink::unitHeadroom * largest));
    }

    void runFixedWithTimers (Patcher& patcher, int rounds, Order order, int blockSize)
    {
        for (int round = 0; round < rounds; ++round)
        {
            patcher.run (10, order, blockSize);
            patcher.maintain();
        }
    }
}

TEST_CASE ("Real blocks: the round trip counts the host's real blocks, and the return stays exact", "[link][blocks]")
{
    for (const auto& [blocks, order] : { std::pair { 1, Order::panelFirst }, std::pair { 2, Order::receiverFirst }, std::pair { 2, Order::random }, std::pair { 3, Order::random } })
    {
        INFO ("round trip " << blocks << " blocks, order " << static_cast<int> (order));
        Patcher patcher;
        patcher.realBlocks = true;
        patcher.addAll (blocks);

        // The announced block until the survey has seen real ones
        auto status = patcher.panel->getStatus();
        CHECK (status.realBlocks);
        CHECK (status.surveyingBlocks);
        CHECK (status.blockUnit == rigBlock);
        CHECK (patcher.roundTrip == blocks * rigBlock);

        for (int round = 0; round < 40; ++round)
        {
            patcher.runVarying (10, order, flTick);
            patcher.maintain();
        }

        // Random sizes may not reach the largest within the survey; a block that grows past 90% of the unit grows it
        status = patcher.panel->getStatus();
        INFO (status.events.joinIntoString ("\n"));
        REQUIRE (status.blockMax.has_value());
        CHECK_FALSE (status.surveyingBlocks);
        CHECK (status.blockUnit >= *status.blockMax);
        CHECK (status.blockUnit <= unitFor (*status.blockMax));
        CHECK (patcher.roundTrip == blocks * status.blockUnit);
        CHECK (status.events.joinIntoString ("\n").contains ("samples (largest real block"));

        patcher.panel->resetCounters();
        const auto from = patcher.played;
        patcher.runVarying (400, order, flTick);

        status = patcher.panel->getStatus();
        CHECK (patcher.countWrong (from) == 0);
        CHECK (status.missing == 0);
        CHECK (status.differenceLargest.value_or (-1.0f) == 0.0f);
    }
}

TEST_CASE ("Real blocks: the round trip grows ahead of need, never shrinks while playing, and is surveyed again on a recheck", "[link][blocks]")
{
    Patcher patcher;
    patcher.realBlocks = true;
    patcher.addAll (1);
    runFixedWithTimers (patcher, 10, Order::panelFirst, 100);
    REQUIRE (patcher.panel->getBlockUnit() == unitFor (100));
    const auto from = patcher.played;

    // Above 80% of the unit: grown to 125% of it before it's needed, so nothing goes missing
    runFixedWithTimers (patcher, 5, Order::panelFirst, 120);
    CHECK (patcher.panel->getBlockUnit() == unitFor (120));
    CHECK (patcher.roundTrip == unitFor (120));
    CHECK (patcher.panel->getStatus().missing == 0);

    // A sudden jump past the unit (a tempo dropping at once): grown as soon as the timer sees it
    runFixedWithTimers (patcher, 5, Order::panelFirst, flTick);
    CHECK (patcher.panel->getBlockUnit() == unitFor (flTick));

    // Smaller blocks again: no shrinking while playing
    runFixedWithTimers (patcher, 10, Order::panelFirst, 100);
    CHECK (patcher.panel->getBlockUnit() == unitFor (flTick));

    // Every sample is the sent audio at the delay in use (Safe dropouts cover what the jump missed), apart from the blends
    CHECK (countWrongByDelay (patcher, from) == 0);

    // A recheck surveys again, and may shrink; Reset counters doesn't
    patcher.panel->resetCounters();
    runFixedWithTimers (patcher, 10, Order::panelFirst, 100);
    CHECK (patcher.panel->getBlockUnit() == unitFor (flTick));

    patcher.panel->recheckBlockSize();
    runFixedWithTimers (patcher, 10, Order::panelFirst, 100);
    CHECK (patcher.panel->getBlockUnit() == unitFor (100));
    CHECK (patcher.roundTrip == unitFor (100));

    const auto log = patcher.panel->getStatus().events.joinIntoString ("\n");
    INFO (log);
    CHECK (log.contains ("(a real block of 120)"));
    CHECK (log.contains ("counted in blocks of " + juce::String (unitFor (120))));
}

TEST_CASE ("Real blocks: the lowest tempo is the starting size, a fixed setting counts too, and announced blocks ignore both", "[link][blocks]")
{
    // One tick at 120 BPM is 250 samples at 48 kHz, plus 25%
    Patcher patcher;
    patcher.realBlocks = true;
    patcher.lowestTempo = 120.0;
    patcher.addAll (2);
    CHECK (patcher.panel->getBlockUnit() == 313);
    CHECK (patcher.roundTrip == 2 * 313);

    // Smaller real blocks don't go below it
    runFixedWithTimers (patcher, 10, Order::random, flTick);
    CHECK (patcher.panel->getBlockUnit() == 313);
    CHECK (patcher.roundTrip == 2 * 313);

    // A lower tempo raises it at once, up to the announced block
    patcher.panel->setLowestTempo (60.0);
    patcher.maintain();
    CHECK (patcher.panel->getBlockUnit() == rigBlock);

    // Announced blocks
    patcher.panel->setRealBlocks (false);
    patcher.panel->setLowestTempo (120.0);
    patcher.maintain();
    CHECK (patcher.panel->getBlockUnit() == rigBlock);
    CHECK_FALSE (patcher.panel->getStatus().realBlocks);

    // Back to real blocks: surveyed afresh
    patcher.panel->setRealBlocks (true);
    patcher.panel->setLowestTempo (0.0);
    patcher.maintain();
    CHECK (patcher.panel->getStatus().surveyingBlocks);
    runFixedWithTimers (patcher, 10, Order::random, flTick);
    CHECK (patcher.panel->getBlockUnit() == unitFor (flTick));
    CHECK (patcher.roundTrip == 2 * unitFor (flTick));
}

TEST_CASE ("Real blocks: Automatic settles on 1 real block with the receiver after the panel", "[link][blocks]")
{
    Patcher patcher;
    patcher.realBlocks = true;
    patcher.addAll (BayLink::autoRoundTrip);

    for (int round = 0; round < 100; ++round)
    {
        patcher.runVarying (10, Order::panelFirst, flTick);
        patcher.maintain();
    }

    const auto status = patcher.panel->getStatus();
    INFO (status.events.joinIntoString ("\n"));
    CHECK (status.autoState == BayLink::AutoState::oneBlock);
    CHECK (status.roundTripSamples == status.blockUnit);
    CHECK (status.blockUnit < rigBlock);
    CHECK (status.missing == 0);
    CHECK (patcher.relayMisses (patcher.receiver.get()) == 0);
}

TEST_CASE ("Real blocks: while the transport is stopped the round trip may shrink, as the setting allows", "[link][blocks]")
{
    using Shrink = BayLink::StoppedShrink;
    using Transport = BayLink::Transport;

    Patcher patcher;
    patcher.realBlocks = true;
    patcher.addAll (1);
    runFixedWithTimers (patcher, 10, Order::panelFirst, flTick);
    REQUIRE (patcher.panel->getBlockUnit() == unitFor (flTick));

    // A faster tempo: smaller blocks
    const auto runFaster = [&patcher] { runFixedWithTimers (patcher, 30, Order::panelFirst, 100); };

    SECTION ("playing: never")
    {
        patcher.panel->setStoppedShrink (Shrink::always);
        patcher.panel->setTransport (Transport::playing);
        runFaster();
        CHECK (patcher.panel->getBlockUnit() == unitFor (flTick));
    }

    SECTION ("transport not reported: treated as playing")
    {
        patcher.panel->setStoppedShrink (Shrink::always);
        runFaster();
        CHECK (patcher.panel->getBlockUnit() == unitFor (flTick));
    }

    SECTION ("stopped, always")
    {
        patcher.panel->setStoppedShrink (Shrink::always);
        patcher.panel->setTransport (Transport::stopped, 140.0);
        runFaster();
        CHECK (patcher.panel->getBlockUnit() == unitFor (100));
        CHECK (patcher.roundTrip == unitFor (100));
        CHECK (patcher.panel->getStatus().events.joinIntoString ("\n").contains ("while stopped"));

        // One check per stop: none while nothing changes
        auto checks = 0;

        for (int round = 0; round < 20; ++round)
        {
            patcher.run (10, Order::panelFirst, 100);
            patcher.maintain();
            checks += patcher.panel->getStatus().surveyingBlocks ? 1 : 0;
        }

        CHECK (checks == 0);

        // A new tempo checks again, and so does stopping after playing
        patcher.panel->setTransport (Transport::stopped, 150.0);
        patcher.maintain();
        CHECK (patcher.panel->getStatus().surveyingBlocks);
        runFaster();
        CHECK_FALSE (patcher.panel->getStatus().surveyingBlocks);

        patcher.panel->setTransport (Transport::playing, 150.0);
        patcher.maintain();
        patcher.panel->setTransport (Transport::stopped, 150.0);
        patcher.maintain();
        CHECK (patcher.panel->getStatus().surveyingBlocks);
    }

    SECTION ("stopped, never")
    {
        patcher.panel->setStoppedShrink (Shrink::never);
        patcher.panel->setTransport (Transport::stopped);
        runFaster();
        CHECK (patcher.panel->getBlockUnit() == unitFor (flTick));
    }

    SECTION ("stopped, only when quiet: not while the panel plays, then once it has been quiet long enough")
    {
        patcher.panel->setStoppedShrink (Shrink::whenQuiet);
        patcher.panel->setQuiet (-80, 500);
        patcher.panel->setTransport (Transport::stopped);
        runFaster();
        CHECK (patcher.panel->getBlockUnit() == unitFor (flTick));
        CHECK_FALSE (patcher.panel->getStatus().outputQuiet);

        patcher.source = [] (juce::int64, int) { return 0.0f; };
        runFaster();
        runFaster();
        CHECK (patcher.panel->getStatus().outputQuiet);
        CHECK (patcher.panel->getBlockUnit() == unitFor (100));
    }
}

TEST_CASE ("Real blocks: a new lowest tempo rechecks, so the round trip can shrink while playing", "[link][blocks]")
{
    Patcher patcher;
    patcher.realBlocks = true;
    patcher.addAll (1);
    runFixedWithTimers (patcher, 10, Order::panelFirst, flTick);
    runFixedWithTimers (patcher, 10, Order::panelFirst, 100);
    REQUIRE (patcher.panel->getBlockUnit() == unitFor (flTick));

    // 300 BPM: a tick of 100 samples, plus 25%
    patcher.panel->setLowestTempo (300.0);
    runFixedWithTimers (patcher, 10, Order::panelFirst, 100);
    CHECK (patcher.panel->getBlockUnit() == 125);
    CHECK (patcher.roundTrip == 125);
}

TEST_CASE ("Real blocks: one big block long ago doesn't starve a 2-block round trip, however it's reached", "[link][blocks]")
{
    enum class Path {
        fromOne,
        fromThree,
        automaticFallback,
        shrinkAtTwo,
    };

    for (const auto path : { Path::fromOne, Path::fromThree, Path::automaticFallback, Path::shrinkAtTwo })
    {
        for (const auto order : { Order::panelFirst, Order::receiverFirst, Order::random })
        {
            // With the panel first, Automatic keeps 1 block
            if (path == Path::automaticFallback && order == Order::panelFirst)
                continue;

            INFO ("path " << static_cast<int> (path) << ", order " << static_cast<int> (order));
            Patcher patcher;
            patcher.realBlocks = true;
            patcher.addAll (path == Path::fromOne ? 1 : path == Path::fromThree       ? 3
                                                    : path == Path::automaticFallback ? BayLink::autoRoundTrip
                                                                                      : 2);

            // A full announced block, as FL may send around loading or starting; the panel's recheck then forgets it,
            // as its later checks do
            patcher.run (1, Order::panelFirst, rigBlock);
            runFixedWithTimers (patcher, 10, Order::panelFirst, flTick);
            REQUIRE (patcher.panel->getBlockUnit() == rigBlock);
            patcher.panel->recheckBlockSize();

            // At 2 blocks the shrink itself is the change; the other paths shrink first
            if (path != Path::shrinkAtTwo)
            {
                runFixedWithTimers (patcher, 10, Order::panelFirst, flTick);
                REQUIRE (patcher.panel->getBlockUnit() == unitFor (flTick));
            }

            if (path == Path::fromOne || path == Path::fromThree)
                patcher.panel->setRoundTripBlocks (2);

            // The change (or Automatic's fallback, or the shrink), then the window after it
            runFixedWithTimers (patcher, 30, order, flTick);
            REQUIRE (patcher.roundTrip == 2 * unitFor (flTick));

            patcher.panel->resetCounters();
            const auto receiverMisses = patcher.relayMisses (patcher.receiver.get());

            for (int round = 0; round < 20; ++round)
            {
                patcher.runVarying (10, order, flTick);
                patcher.maintain();
            }

            const auto status = patcher.panel->getStatus();
            INFO (status.events.joinIntoString ("\n"));
            CHECK (status.missing == 0);
            CHECK (patcher.relayMisses (patcher.receiver.get()) == receiverMisses);
            CHECK (patcher.roundTrip == 2 * unitFor (flTick));
        }
    }
}

TEST_CASE ("Retry 1 block: Automatic tries 1 block again; a fixed round trip ignores it", "[link][blocks]")
{
    const auto settle = [] (Patcher& patcher, Order order) {
        for (int round = 0; round < 30; ++round)
        {
            patcher.run (10, order);
            patcher.maintain();
        }
    };

    Patcher patcher;
    patcher.addAll (BayLink::autoRoundTrip);
    settle (patcher, Order::receiverFirst);
    REQUIRE (patcher.panel->getStatus().autoState == BayLink::AutoState::fellBack);

    // The wire added: retrying settles on 1 block without relinking the relays
    patcher.panel->retryOneBlock();
    patcher.maintain();
    CHECK (patcher.panel->getStatus().autoState == BayLink::AutoState::checking);
    CHECK (patcher.roundTrip == rigBlock);
    settle (patcher, Order::panelFirst);
    CHECK (patcher.panel->getStatus().autoState == BayLink::AutoState::oneBlock);
    CHECK (patcher.panel->getStatus().events.joinIntoString ("\n").contains ("Automatic: trying 1 block again (asked)"));

    patcher.panel->setRoundTripBlocks (3);
    patcher.panel->retryOneBlock();
    patcher.maintain();
    CHECK (patcher.panel->getStatus().autoState == BayLink::AutoState::off);
    CHECK (patcher.roundTrip == 3 * rigBlock);
}

TEST_CASE ("A change of the round trip blends over the round trip fade; Off and back over the Off fade", "[link]")
{
    constexpr int fiftyMs = static_cast<int> (rigRate) / 20;

    // The sample `after` samples past the latest change of delay is the sent audio at the new delay, exactly
    const auto exactAfter = [] (const Patcher& patcher, juce::int64 after) {
        const auto [start, delay] = patcher.delays.back();
        const auto sample = start + after;
        REQUIRE (sample < patcher.played);
        return juce::exactlyEqual (patcher.outLeft[static_cast<size_t> (sample)], sentSample (sample - delay, 0));
    };

    Patcher patcher;
    patcher.addAll (2);
    patcher.run (warmUpBlocks, Order::panelFirst);
    patcher.maintain();

    SECTION ("no round trip fade: the new timing at once")
    {
        patcher.panel->setFadeMs (BayLink::Fade::roundTrip, 0);
        patcher.panel->setRoundTripBlocks (3);
        patcher.maintain();
        patcher.run (warmUpBlocks, Order::panelFirst);
        CHECK (exactAfter (patcher, 0));
    }

    SECTION ("100 ms: still blending after 50 ms, done after 100")
    {
        patcher.panel->setFadeMs (BayLink::Fade::roundTrip, 100);
        patcher.panel->setRoundTripBlocks (3);
        patcher.maintain();
        patcher.run (30, Order::panelFirst);
        CHECK_FALSE (exactAfter (patcher, fiftyMs));
        CHECK (exactAfter (patcher, 2 * fiftyMs + 64));
    }

    SECTION ("Off uses the Off fade, not the round trip fade")
    {
        patcher.panel->setFadeMs (BayLink::Fade::roundTrip, 0);
        patcher.panel->setFadeMs (BayLink::Fade::off, 50);
        patcher.panel->setSlotState (Slot::off);
        patcher.maintain();
        patcher.run (30, Order::panelFirst);
        CHECK (patcher.delays.back().second == 0);
        CHECK_FALSE (exactAfter (patcher, fiftyMs / 2));
        CHECK (exactAfter (patcher, fiftyMs + 64));
    }
}

TEST_CASE ("A relay shows what the hub sees for every panel", "[link]")
{
    Patcher patcher;
    patcher.addAll (2);
    patcher.run (warmUpBlocks, Order::panelFirst);

    const auto find = [] (const std::vector<RelayLink::PanelView>& panels, int number) {
        const auto view = std::find_if (panels.begin(), panels.end(), [number] (const auto& p) { return p.number == number; });
        return view != panels.end() ? std::optional (*view) : std::nullopt;
    };

    auto& link = patcher.receiver->getLink();
    link.getPanels();
    patcher.run (2, Order::panelFirst);
    const auto running = find (link.getPanels(), 1);
    REQUIRE (running.has_value());
    CHECK (link.getLinkedPanel() == 1);
    CHECK ((running->panel && running->panelRunning && running->receiver && running->receiverRunning && running->transmitter && running->transmitterRunning));

    // Nothing has run since the last look; a second panel holds its own name, with no relays
    BayLink second (inProcessHub);
    second.prepare (rigRate, rigBlock);
    REQUIRE (second.getStatus().linkName == "Panel 2");
    const auto panels = link.getPanels();
    const auto stopped = find (panels, 1);
    const auto other = find (panels, 2);
    REQUIRE (stopped.has_value());
    REQUIRE (other.has_value());
    CHECK ((stopped->panel && !stopped->panelRunning && stopped->receiver && !stopped->receiverRunning));
    CHECK ((other->panel && !other->receiver && !other->transmitter));
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

TEST_CASE ("Each fade has its own length: Mute, Bypass, a held-back bay latency and a dropout", "[link][fades]")
{
    using Fade = BayLink::Fade;

    // From `from`, the first sample that isn't the sent audio at the delay in use (where a fade starts), and the first
    // exactly silent one after it (where a dropout's reversed fade has ended)
    const auto fadeOutLength = [] (const Patcher& patcher, juce::int64 from) {
        const auto delay = patcher.roundTrip + patcher.latency;
        auto start = from;

        while (start < patcher.played && juce::exactlyEqual (patcher.outLeft[static_cast<size_t> (start)], sentSample (start - delay, 0)))
            ++start;

        auto end = start;

        while (end < patcher.played && !juce::exactlyEqual (patcher.outLeft[static_cast<size_t> (end)], 0.0f))
            ++end;

        return static_cast<int> (end - start);
    };

    // From `from`, the first sample that isn't silent (where a fade in starts), and the first after it that is exactly the
    // sent audio at the delay in use (where it has ended: the bay passes its audio through)
    const auto fadeInLength = [] (const Patcher& patcher, juce::int64 from) {
        const auto delay = patcher.roundTrip + patcher.latency;
        auto start = from;

        while (start < patcher.played && juce::exactlyEqual (patcher.outLeft[static_cast<size_t> (start)], 0.0f))
            ++start;

        auto end = start;

        while (end < patcher.played && !juce::exactlyEqual (patcher.outLeft[static_cast<size_t> (end)], sentSample (end - delay, 0)))
            ++end;

        return static_cast<int> (end - start);
    };

    constexpr auto samplesPerMs = static_cast<int> (rigRate) / 1000;

    SECTION ("Mute and Bypass crossfade over their own lengths")
    {
        Patcher patcher;
        patcher.addAll (2);
        patcher.bay = [] (juce::AudioBuffer<float>& audio) { audio.applyGain (0.5f); };
        patcher.panel->setFadeMs (Fade::mute, 10);
        patcher.panel->setFadeMs (Fade::bypass, 40);
        patcher.run (warmUpBlocks, Order::panelFirst);

        auto switchedAt = patcher.played;
        patcher.panel->setSlotState (Slot::mute);
        patcher.run (8, Order::panelFirst);
        CHECK (patcher.countWrong (switchedAt + 10 * samplesPerMs) == 0);
        CHECK (patcher.countWrong (switchedAt + 5 * samplesPerMs) > 0);

        patcher.panel->setSlotState (Slot::active);
        patcher.run (20, Order::panelFirst);

        switchedAt = patcher.played;
        patcher.panel->setSlotState (Slot::bypass);
        patcher.run (8, Order::panelFirst);
        CHECK (patcher.countWrong (switchedAt + 40 * samplesPerMs) == 0);
        CHECK (patcher.countWrong (switchedAt + 20 * samplesPerMs) > 0);
    }

    SECTION ("a dropout fades out over the dropout fade")
    {
        Patcher patcher;
        patcher.addAll (2);
        patcher.panel->setDropouts (Dropouts::silence);
        patcher.panel->setFadeMs (Fade::dropoutOut, 30);
        patcher.panel->setFadeMs (Fade::bayLatencyOut, 10);
        patcher.run (warmUpBlocks, Order::panelFirst);

        const auto removed = patcher.played;
        patcher.transmitter.reset();
        patcher.maintain();
        patcher.run (10, Order::panelFirst);
        CHECK (std::abs (fadeOutLength (patcher, removed) - 30 * samplesPerMs) <= 1);
    }

    SECTION ("the return comes back over the dropout fade in, not its fade out")
    {
        Patcher patcher;
        patcher.addAll (2);
        patcher.panel->setDropouts (Dropouts::silence);
        patcher.panel->setFadeMs (Fade::dropoutOut, 10);
        patcher.panel->setFadeMs (Fade::dropoutIn, 40);
        patcher.run (warmUpBlocks, Order::panelFirst);

        patcher.transmitter.reset();
        patcher.maintain();
        patcher.run (10, Order::panelFirst);

        const auto back = patcher.played;
        patcher.addTransmitter();
        patcher.run (20, Order::panelFirst);
        CHECK (std::abs (fadeInLength (patcher, back) - 40 * samplesPerMs) <= 2);
    }

    SECTION ("a return held back while a new bay latency settles fades out over the bay latency fade")
    {
        Patcher patcher;
        patcher.addAll (2);
        auto latency = std::make_shared<int> (576);
        patcher.bay = latentBay (latency);
        patcher.markerDelay = 576;
        runWithTimers (patcher, 30, Order::panelFirst);
        REQUIRE (patcher.panel->getBayLatency() == 576);

        patcher.panel->setDropouts (Dropouts::silence);
        patcher.panel->setFadeMs (Fade::dropoutOut, 30);
        patcher.panel->setFadeMs (Fade::bayLatencyOut, 10);
        patcher.panel->setSettleMs (1000);
        const auto changed = patcher.played;
        *latency = 192;
        patcher.markerDelay = 192;
        runWithTimers (patcher, 4, Order::panelFirst);

        REQUIRE (patcher.panel->getStatus().settlingTo == 192);
        CHECK (std::abs (fadeOutLength (patcher, changed) - 10 * samplesPerMs) <= 1);
    }
}

TEST_CASE ("The processor's bay commands are parameters after the pool, with permanent IDs, acting once per switch on", "[processor][link]")
{
    PluginProcessor processor;
    const auto& parameters = processor.getParameters();
    REQUIRE (parameters.size() == HostControls::count + 1 + PluginProcessor::maxBays);

    const auto check = [&parameters] (int index, const juce::String& id, const juce::String& name) {
        const auto* parameter = dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameters[index]);
        REQUIRE (parameter != nullptr);
        CHECK (parameter->getParameterID() == id);
        CHECK (parameter->getName (64) == name);
        CHECK (parameter->isAutomatable());
        CHECK (parameter->isBoolean());
    };

    check (HostControls::count, "baysRecheckBlockSize", "Bays: recheck block size");

    for (int bay = 1; bay <= PluginProcessor::maxBays; ++bay)
        check (HostControls::count + bay, "bay" + juce::String (bay) + "RetryOneBlock", "Bay " + juce::String (bay) + ": retry 1-block round trip");

    auto& retry = processor.getRetryOneBlock (0);
    retry.setValue (1.0f);
    CHECK (retry.takeRequest());
    CHECK_FALSE (retry.takeRequest());

    // Staying on asks nothing more; off and on again asks again
    retry.setValue (1.0f);
    CHECK_FALSE (retry.takeRequest());
    retry.setValue (0.0f);
    retry.setValue (1.0f);

    // The link settings' timer takes the requests
    auto& recheck = processor.getRecheckBlockSize();
    recheck.setValue (1.0f);
    processor.applyLinkSettings();
    CHECK_FALSE (retry.takeRequest());
    CHECK_FALSE (recheck.takeRequest());
    CHECK (recheck.getCurrentValueAsText() == "On");
}

TEST_CASE ("The panel tells the host its delay as latency, and keeps its slot state and control values", "[processor][link]")
{
    juce::SharedResourcePointer<PluginSettings> settings;
    const auto previousBlocks = settings->getRoundTripBlocks();
    const auto previousFade = settings->getFadeMs (BayLink::Fade::bypass);
    const auto previousSettle = settings->getLatencySettleMs();
    const auto previousRelease = settings->getTailCutReleaseMs();

    {
        settings->setRoundTripBlocks (3);
        PluginProcessor processor;
        processor.prepareToPlay (rigRate, rigBlock);
        const auto engineLatency = processor.getEngine().getLatencySamples();
        CHECK (processor.getLatencySamples() == engineLatency + 3 * rigBlock);
        CHECK (processor.getBayLink().getDropouts() == Dropouts::silence);

        // A change from any instance's Settings tab
        settings->setRoundTripBlocks (1);
        settings->setFadeMs (BayLink::Fade::bypass, 50);
        settings->setLatencySettleMs (200);
        settings->setTailCutReleaseMs (40);
        processor.applyLinkSettings();
        CHECK (processor.getLatencySamples() == engineLatency + rigBlock);
        CHECK (processor.getBayLink().getFadeMs (BayLink::Fade::bypass) == 50);
        CHECK (processor.getBayLink().getSettleMs() == 200);
        CHECK (processor.getBayLink().getTailCutReleaseMs() == 40);

        // Off leaves only the engine's latency
        processor.getBayLink().setSlotState (Slot::off);
        processor.applyLinkSettings();
        CHECK (processor.getLatencySamples() == engineLatency);
        processor.getBayLink().setSlotState (Slot::active);
        processor.applyLinkSettings();
        CHECK (processor.getLatencySamples() == engineLatency + rigBlock);

        // Automatic starts by trying 1 block
        settings->setRoundTripBlocks (BayLink::autoRoundTrip);
        processor.applyLinkSettings();
        CHECK (processor.getLatencySamples() == engineLatency + rigBlock);
        CHECK (processor.getBayLink().getStatus().autoState == BayLink::AutoState::checking);

        // The bay's controls are always host controls 2 to 17
        auto& hostControl = *processor.getParameters()[PluginProcessor::firstBayControl + 2];
        hostControl.setValue (0.625f);
        CHECK (juce::exactlyEqual (processor.getBayLink().getControl (2).getValue(), 0.625f));
        CHECK (processor.getBayLink().getControl (2).getText (0.625f, 20) == "62.50%");

        processor.getBayLink().setSlotState (Slot::off);
        processor.getBayLink().getControl (15).setValue (0.1f);
        juce::MemoryBlock saved;
        processor.getStateInformation (saved);
        processor.getBayLink().setSlotState (Slot::active);
        processor.getBayLink().getControl (2).setValue (0.0f);
        processor.getBayLink().getControl (15).setValue (0.0f);
        processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        CHECK (processor.getBayLink().getSlotState() == Slot::off);
        CHECK (juce::exactlyEqual (processor.getBayLink().getControl (2).getValue(), 0.625f));
        CHECK (juce::exactlyEqual (processor.getBayLink().getControl (15).getValue(), 0.1f));
        CHECK (juce::exactlyEqual (hostControl.getValue(), 0.625f));

        // A state without this version (an older project) opens as a new instance would: nothing is taken from it
        const auto load = [&processor] (int version, const char* value) {
            juce::XmlElement state ("HeartseekerPanelState");
            state.setAttribute ("baySlotState", value);

            if (version > 0)
                state.setAttribute ("version", version);

            juce::MemoryBlock bytes;
            juce::AudioProcessor::copyXmlToBinary (state, bytes);
            processor.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
            return processor.getBayLink().getSlotState();
        };

        CHECK (load (1, "bypass") == Slot::bypass);
        CHECK (load (0, "mute") == Slot::bypass);
        CHECK (load (2, "mute") == Slot::bypass);
        CHECK (load (1, "sideways") == Slot::active);

        processor.releaseResources();
    }

    settings->setRoundTripBlocks (previousBlocks);
    settings->setFadeMs (BayLink::Fade::bypass, previousFade);
    settings->setLatencySettleMs (previousSettle);
    settings->setTailCutReleaseMs (previousRelease);
}
