#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>
#include <link/LinkHub.h>
#include <thread>

namespace
{
    const hs_link_api& linkHub()
    {
        return *heartseeker_link_get_api (HS_LINK_ABI_VERSION);
    }

    hs_link_info infoFor (int32_t handle)
    {
        hs_link_info info {};
        info.struct_size = sizeof (info);
        linkHub().get_info (handle, &info);
        return info;
    }

    // Different at every position and channel, so any mix-up shows
    float frameAt (int64_t position, int channel)
    {
        return static_cast<float> ((position * 31 + channel * 17) % 20011) / 20011.0f - 0.5f;
    }

    int32_t writeFrames (int32_t writer, int64_t position, int numFrames)
    {
        std::vector<float> left (static_cast<size_t> (numFrames)), right (static_cast<size_t> (numFrames));

        for (int i = 0; i < numFrames; ++i)
        {
            left[static_cast<size_t> (i)] = frameAt (position + i, 0);
            right[static_cast<size_t> (i)] = frameAt (position + i, 1);
        }

        const float* channels[] { left.data(), right.data() };
        return linkHub().write (writer, position, channels, 2, numFrames);
    }

    struct ReadResult
    {
        int32_t result;
        bool exact; // every frame is what was written at its position
        bool silent;
    };

    ReadResult readFrames (int32_t reader, int64_t position, int numFrames)
    {
        std::vector<float> left (static_cast<size_t> (numFrames), 9.0f), right (static_cast<size_t> (numFrames), 9.0f);
        float* channels[] { left.data(), right.data() };
        ReadResult read { linkHub().read (reader, position, channels, 2, numFrames), true, true };

        for (int i = 0; i < numFrames; ++i)
        {
            const auto index = static_cast<size_t> (i);
            read.exact = read.exact && juce::exactlyEqual (left[index], frameAt (position + i, 0)) && juce::exactlyEqual (right[index], frameAt (position + i, 1));
            read.silent = read.silent && juce::exactlyEqual (left[index], 0.0f) && juce::exactlyEqual (right[index], 0.0f);
        }

        return read;
    }

    // Closes every claim at the end of a test. The hub lasts for the whole test run and never forgets a channel name,
    // so every test uses names of its own.
    struct Claims
    {
        ~Claims()
        {
            for (const auto handle : handles)
                linkHub().close (handle);
        }

        int32_t open (const char* name, int32_t role)
        {
            const auto handle = linkHub().open (name, role);

            if (handle >= 0)
                handles.push_back (handle);

            return handle;
        }

        std::vector<int32_t> handles;
    };
}

TEST_CASE ("The link hub only speaks its own version", "[link]")
{
    CHECK (heartseeker_link_get_api (0) == nullptr);
    CHECK (heartseeker_link_get_api (1) == nullptr); // no value sets
    CHECK (heartseeker_link_get_api (2) == nullptr); // no latency reports
    CHECK (heartseeker_link_get_api (HS_LINK_ABI_VERSION + 1) == nullptr);

    const auto* api = heartseeker_link_get_api (HS_LINK_ABI_VERSION);
    REQUIRE (api != nullptr);
    CHECK (api == heartseeker_link_get_api (HS_LINK_ABI_VERSION));
    CHECK (api->struct_size == sizeof (hs_link_api));
    CHECK (api->abi_version == HS_LINK_ABI_VERSION);
    CHECK (api->open != nullptr);
    CHECK (api->close != nullptr);
    CHECK (api->set_writer_details != nullptr);
    CHECK (api->write != nullptr);
    CHECK (api->read != nullptr);
    CHECK (api->get_info != nullptr);
    CHECK (api->write_values != nullptr);
    CHECK (api->read_values != nullptr);
    CHECK (api->set_latency != nullptr);
}

TEST_CASE ("A writer reports its latency through the hub", "[link]")
{
    const auto& hub = linkHub();
    Claims claims;
    const auto writer = claims.open ("test/latency", HS_LINK_WRITER);
    const auto reader = claims.open ("test/latency", HS_LINK_READER);
    const auto observer = hub.open ("test/latency", HS_LINK_OBSERVER);
    REQUIRE (writer >= 0);
    REQUIRE (reader >= 0);

    auto info = infoFor (reader);
    CHECK (info.latency_state == HS_LINK_LATENCY_UNKNOWN);
    CHECK (info.latency == 0);
    const auto reportsBefore = info.latency_reports;

    CHECK (hub.set_latency (writer, HS_LINK_LATENCY_MEASURED, 12288) == HS_LINK_OK);
    info = infoFor (observer);
    CHECK (info.latency_state == HS_LINK_LATENCY_MEASURED);
    CHECK (info.latency == 12288);
    CHECK (info.latency_reports == reportsBefore + 1);

    CHECK (hub.set_latency (writer, HS_LINK_LATENCY_OUT_OF_RANGE, -5) == HS_LINK_OK);
    info = infoFor (reader);
    CHECK (info.latency_state == HS_LINK_LATENCY_OUT_OF_RANGE);
    CHECK (info.latency == -5);
    CHECK (info.latency_reports == reportsBefore + 2);

    SECTION ("every state takes only its own samples, only from the writer; refused reports change nothing")
    {
        CHECK (hub.set_latency (writer, HS_LINK_LATENCY_MEASURED, 0) == HS_LINK_OK);
        CHECK (hub.set_latency (writer, HS_LINK_LATENCY_MEASURED, HS_LINK_MAX_LATENCY) == HS_LINK_OK);
        CHECK (hub.set_latency (writer, HS_LINK_LATENCY_OUT_OF_RANGE, HS_LINK_MAX_LATENCY + 1) == HS_LINK_OK);
        CHECK (hub.set_latency (writer, HS_LINK_LATENCY_UNREADABLE, 0) == HS_LINK_OK);
        CHECK (hub.set_latency (writer, HS_LINK_LATENCY_NO_INPUT, 0) == HS_LINK_OK);
        CHECK (hub.set_latency (writer, HS_LINK_LATENCY_HELD, 0) == HS_LINK_OK);
        CHECK (hub.set_latency (writer, HS_LINK_LATENCY_UNKNOWN, 0) == HS_LINK_OK);
        const auto reports = infoFor (reader).latency_reports;

        const std::pair<int32_t, int32_t> refused[] {
            { HS_LINK_LATENCY_MEASURED, -1 },
            { HS_LINK_LATENCY_MEASURED, HS_LINK_MAX_LATENCY + 1 },
            { HS_LINK_LATENCY_OUT_OF_RANGE, 0 },
            { HS_LINK_LATENCY_OUT_OF_RANGE, HS_LINK_MAX_LATENCY },
            { HS_LINK_LATENCY_UNKNOWN, 1 },
            { HS_LINK_LATENCY_UNREADABLE, 3 },
            { HS_LINK_LATENCY_NO_INPUT, -1 },
            { HS_LINK_LATENCY_HELD, 7 },
            { 6, 0 },
            { -1, 0 },
        };

        for (const auto& [state, samples] : refused)
        {
            INFO (state << ", " << samples);
            CHECK (hub.set_latency (writer, state, samples) == HS_LINK_BAD_ARGUMENT);
        }

        CHECK (hub.set_latency (reader, HS_LINK_LATENCY_MEASURED, 0) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.set_latency (observer, HS_LINK_LATENCY_MEASURED, 0) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.set_latency (-1, HS_LINK_LATENCY_MEASURED, 0) == HS_LINK_BAD_ARGUMENT);

        info = infoFor (reader);
        CHECK (info.latency_state == HS_LINK_LATENCY_UNKNOWN);
        CHECK (info.latency_reports == reports);
    }

    SECTION ("a writer that closes leaves it unknown, and a new writer starts from unknown")
    {
        hub.close (writer);
        CHECK (hub.set_latency (writer, HS_LINK_LATENCY_MEASURED, 1) == HS_LINK_CLOSED);
        CHECK (infoFor (reader).latency_state == HS_LINK_LATENCY_UNKNOWN);

        const auto next = claims.open ("test/latency", HS_LINK_WRITER);
        REQUIRE (next >= 0);
        CHECK (infoFor (reader).latency_state == HS_LINK_LATENCY_UNKNOWN);
        CHECK (hub.set_latency (next, HS_LINK_LATENCY_MEASURED, 576) == HS_LINK_OK);
        CHECK (infoFor (reader).latency == 576);
    }
}

TEST_CASE ("Link channels: names and roles", "[link]")
{
    const auto& hub = linkHub();
    Claims claims;

    SECTION ("names must be printable ASCII, 1 to 63 characters")
    {
        const std::string longest (HS_LINK_MAX_NAME - 1, 'n');
        const std::string tooLong (HS_LINK_MAX_NAME, 'n');

        CHECK (hub.open (nullptr, HS_LINK_WRITER) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.open ("", HS_LINK_WRITER) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.open (tooLong.c_str(), HS_LINK_WRITER) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.open ("test/tab\there", HS_LINK_WRITER) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.open ("test/caf\xc3\xa9", HS_LINK_WRITER) == HS_LINK_BAD_ARGUMENT);
        CHECK (claims.open (longest.c_str(), HS_LINK_WRITER) >= 0);
    }

    SECTION ("roles must be writer, reader or observer")
    {
        CHECK (hub.open ("test/roles", -1) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.open ("test/roles", 3) == HS_LINK_BAD_ARGUMENT);
    }

    SECTION ("one writer and one reader per channel, any number of observers")
    {
        const auto writer = claims.open ("test/one each", HS_LINK_WRITER);
        const auto reader = claims.open ("test/one each", HS_LINK_READER);
        REQUIRE (writer >= 0);
        REQUIRE (reader >= 0);
        CHECK (hub.open ("test/one each", HS_LINK_WRITER) == HS_LINK_ROLE_TAKEN);
        CHECK (hub.open ("test/one each", HS_LINK_READER) == HS_LINK_ROLE_TAKEN);
        CHECK (hub.open ("test/one each", HS_LINK_OBSERVER) >= 0);
        CHECK (hub.open ("test/one each", HS_LINK_OBSERVER) >= 0);

        const auto info = infoFor (hub.open ("test/one each", HS_LINK_OBSERVER));
        CHECK (info.has_writer == 1);
        CHECK (info.has_reader == 1);
    }

    SECTION ("observers never create a channel")
    {
        CHECK (hub.open ("test/nobody here", HS_LINK_OBSERVER) == HS_LINK_NOT_FOUND);
    }

    SECTION ("a closed claim can be taken again, and the old handle stops working")
    {
        const auto first = hub.open ("test/reclaim", HS_LINK_WRITER);
        REQUIRE (first >= 0);
        CHECK (writeFrames (first, 1000, 16) == HS_LINK_OK);
        hub.close (first);

        CHECK (writeFrames (first, 1016, 16) == HS_LINK_CLOSED);
        CHECK (infoFor (hub.open ("test/reclaim", HS_LINK_OBSERVER)).has_writer == 0);

        const auto second = claims.open ("test/reclaim", HS_LINK_WRITER);
        REQUIRE (second >= 0);
        CHECK (second != first);

        // Closing the old handle again doesn't touch the new claim
        hub.close (first);
        CHECK (writeFrames (second, 1016, 16) == HS_LINK_OK);
    }

    SECTION ("made-up handles are refused")
    {
        const auto writer = claims.open ("test/made up", HS_LINK_WRITER);
        REQUIRE (writer >= 0);
        const auto observer = hub.open ("test/made up", HS_LINK_OBSERVER);
        hs_link_info info {};
        info.struct_size = sizeof (info);

        for (const auto handle : { -1, -5, writer | 3, writer + (1 << 8), observer + (1 << 8), (63 << 2) | HS_LINK_WRITER | (1 << 8), 0x7fffffff })
        {
            INFO (handle);
            CHECK (hub.get_info (handle, &info) != HS_LINK_OK);
            CHECK (writeFrames (handle, 2000, 16) != HS_LINK_OK);
        }
    }
}

TEST_CASE ("Link channels carry exact audio by position", "[link]")
{
    const auto& hub = linkHub();
    Claims claims;
    const auto writer = claims.open ("test/audio", HS_LINK_WRITER);
    const auto reader = claims.open ("test/audio", HS_LINK_READER);
    REQUIRE (writer >= 0);
    REQUIRE (reader >= 0);
    REQUIRE (hub.set_writer_details (writer, 48000.0, 512, 1024) == HS_LINK_OK);

    SECTION ("a read gets exactly what was written at its position, across blocks")
    {
        REQUIRE (writeFrames (writer, 1000, 512) == HS_LINK_OK);
        REQUIRE (writeFrames (writer, 1512, 512) == HS_LINK_OK);

        CHECK (readFrames (reader, 1000, 512).exact);
        CHECK (readFrames (reader, 1200, 512).exact);
        CHECK (readFrames (reader, 1512, 512).exact);
        CHECK (readFrames (reader, 1000, 1024).exact);
    }

    SECTION ("frames not written yet are reported, and read as silence")
    {
        REQUIRE (writeFrames (writer, 1000, 512) == HS_LINK_OK);
        const auto read = readFrames (reader, 1400, 512);
        CHECK (read.result == HS_LINK_NOT_YET);
        CHECK (read.silent);
    }

    SECTION ("frames more than the capacity behind the newest are gone")
    {
        int64_t position = 100000;

        for (int block = 0; block < HS_LINK_CAPACITY / 512 + 4; ++block, position += 512)
            REQUIRE (writeFrames (writer, position, 512) == HS_LINK_OK);

        const auto info = infoFor (reader);
        CHECK (info.readable_end == position);
        CHECK (info.readable_start == position - HS_LINK_CAPACITY);

        const auto gone = readFrames (reader, 100000, 512);
        CHECK (gone.result == HS_LINK_GONE);
        CHECK (gone.silent);
        CHECK (readFrames (reader, info.readable_start - 1, 512).result == HS_LINK_GONE);
        CHECK (readFrames (reader, info.readable_start, 512).exact);
        CHECK (readFrames (reader, position - 512, 512).exact);
    }

    SECTION ("frames stay exact where the buffer wraps around")
    {
        const int64_t wrap = 5 * int64_t (HS_LINK_CAPACITY);

        for (int64_t position = wrap - 2048; position < wrap + 2048; position += 300)
            REQUIRE (writeFrames (writer, position, 300) == HS_LINK_OK);

        CHECK (readFrames (reader, wrap - 1000, 2000).exact);
        CHECK (readFrames (reader, wrap - 1, 2).exact);
    }

    SECTION ("skipping ahead leaves a gap that never arrives; earlier frames stay")
    {
        REQUIRE (writeFrames (writer, 1000, 512) == HS_LINK_OK);
        REQUIRE (writeFrames (writer, 5000, 512) == HS_LINK_OK);

        CHECK (readFrames (reader, 1000, 512).exact);
        CHECK (readFrames (reader, 1512, 512).result == HS_LINK_GONE);
        CHECK (readFrames (reader, 4900, 200).result == HS_LINK_GONE);
        CHECK (readFrames (reader, 5000, 512).exact);

        // Only the latest gap is remembered, so nothing before the one it replaces stays
        REQUIRE (writeFrames (writer, 9000, 512) == HS_LINK_OK);
        CHECK (readFrames (reader, 1000, 512).result == HS_LINK_GONE);
        CHECK (readFrames (reader, 5000, 512).exact);
        CHECK (readFrames (reader, 5512, 512).result == HS_LINK_GONE);
        CHECK (readFrames (reader, 9000, 512).exact);
    }

    SECTION ("going back over written positions starts afresh")
    {
        REQUIRE (writeFrames (writer, 5000, 512) == HS_LINK_OK);
        REQUIRE (writeFrames (writer, 4800, 512) == HS_LINK_OK);

        CHECK (readFrames (reader, 4800, 512).exact);
        CHECK (readFrames (reader, 5000, 512).result == HS_LINK_NOT_YET);
        CHECK (infoFor (reader).readable_start == 4800);
    }

    SECTION ("new writer details start a new session: earlier frames are gone, even if the next write follows on")
    {
        REQUIRE (writeFrames (writer, 1000, 512) == HS_LINK_OK);
        const auto before = infoFor (reader).session;

        REQUIRE (hub.set_writer_details (writer, 44100.0, 256, 512) == HS_LINK_OK);
        REQUIRE (writeFrames (writer, 1512, 512) == HS_LINK_OK);

        const auto info = infoFor (reader);
        CHECK (info.session == before + 1);
        CHECK (juce::exactlyEqual (info.sample_rate, 44100.0));
        CHECK (info.max_block == 256);
        CHECK (info.round_trip == 512);
        CHECK (readFrames (reader, 1000, 512).result == HS_LINK_GONE);
        CHECK (readFrames (reader, 1512, 512).exact);
    }

    SECTION ("a new writer starts afresh too")
    {
        REQUIRE (writeFrames (writer, 1000, 512) == HS_LINK_OK);
        linkHub().close (writer);

        const auto newWriter = claims.open ("test/audio", HS_LINK_WRITER);
        REQUIRE (newWriter >= 0);
        REQUIRE (writeFrames (newWriter, 1512, 512) == HS_LINK_OK);
        CHECK (readFrames (reader, 1000, 512).result == HS_LINK_GONE);
        CHECK (readFrames (reader, 1512, 512).exact);
    }

    SECTION ("the hub publishes where the reader last read, and whether it got every frame")
    {
        REQUIRE (writeFrames (writer, 1000, 512) == HS_LINK_OK);
        const auto readsBefore = infoFor (writer).reads;

        REQUIRE (readFrames (reader, 1100, 256).result == HS_LINK_OK);
        auto info = infoFor (writer);
        CHECK (info.reads == readsBefore + 1);
        CHECK (info.last_read_position == 1100);
        CHECK (info.last_read_length == 256);
        CHECK (info.last_read_ok == 1);

        REQUIRE (readFrames (reader, 1400, 256).result == HS_LINK_NOT_YET);
        info = infoFor (writer);
        CHECK (info.last_read_position == 1400);
        CHECK (info.last_read_ok == 0);

        const auto failedBefore = info.failed_reads;
        REQUIRE (readFrames (reader, 1600, 256).result == HS_LINK_NOT_YET);
        REQUIRE (readFrames (reader, 1000, 256).result == HS_LINK_OK);
        CHECK (infoFor (writer).failed_reads == failedBefore + 1);
    }

    SECTION ("a mono write fills both channels; a mono read gets the first")
    {
        std::vector<float> mono (64);

        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = static_cast<float> (i) * 0.01f;

        const float* source[] { mono.data() };
        REQUIRE (hub.write (writer, 3000, source, 1, 64) == HS_LINK_OK);

        std::vector<float> left (64), right (64), first (64);
        float* stereo[] { left.data(), right.data() };
        float* single[] { first.data() };
        REQUIRE (hub.read (reader, 3000, stereo, 2, 64) == HS_LINK_OK);
        REQUIRE (hub.read (reader, 3000, single, 1, 64) == HS_LINK_OK);
        CHECK (left == mono);
        CHECK (right == mono);
        CHECK (first == mono);
    }
}

TEST_CASE ("Link calls refuse bad arguments", "[link]")
{
    const auto& hub = linkHub();
    Claims claims;
    const auto writer = claims.open ("test/arguments", HS_LINK_WRITER);
    const auto reader = claims.open ("test/arguments", HS_LINK_READER);
    const auto observer = hub.open ("test/arguments", HS_LINK_OBSERVER);
    REQUIRE (writer >= 0);
    REQUIRE (reader >= 0);
    REQUIRE (observer >= 0);

    std::vector<float> frames (HS_LINK_MAX_FRAMES + 1);
    float* channels[] { frames.data(), frames.data() };
    const float* constChannels[] { frames.data(), frames.data() };
    const float* missingChannel[] { frames.data(), nullptr };

    SECTION ("writes")
    {
        CHECK (hub.write (writer, 0, constChannels, 2, HS_LINK_MAX_FRAMES) == HS_LINK_OK);
        CHECK (hub.write (writer, 0, constChannels, 2, HS_LINK_MAX_FRAMES + 1) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write (writer, 0, constChannels, 2, -1) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write (writer, -1, constChannels, 2, 16) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write (writer, HS_LINK_MAX_POSITION - 8, constChannels, 2, 16) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write (writer, 0, nullptr, 2, 16) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write (writer, 0, constChannels, 0, 16) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write (writer, 0, constChannels, 3, 16) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write (writer, 0, missingChannel, 2, 16) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write (reader, 0, constChannels, 2, 16) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write (observer, 0, constChannels, 2, 16) == HS_LINK_BAD_ARGUMENT);
    }

    SECTION ("reads")
    {
        CHECK (hub.read (reader, 0, channels, 2, HS_LINK_MAX_FRAMES + 1) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.read (reader, -1, channels, 2, 16) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.read (reader, 0, nullptr, 2, 16) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.read (reader, 0, channels, 3, 16) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.read (writer, 0, channels, 2, 16) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.read (observer, 0, channels, 2, 16) == HS_LINK_BAD_ARGUMENT);
    }

    SECTION ("writer details")
    {
        for (const auto rate : { 0.0, -48000.0, 2.0e6, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() })
        {
            INFO (rate);
            CHECK (hub.set_writer_details (writer, rate, 512, 1024) == HS_LINK_BAD_ARGUMENT);
        }

        CHECK (hub.set_writer_details (writer, 48000.0, 0, 1024) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.set_writer_details (writer, 48000.0, HS_LINK_MAX_FRAMES + 1, 1024) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.set_writer_details (writer, 48000.0, 512, -1) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.set_writer_details (writer, 48000.0, 512, HS_LINK_CAPACITY + 1) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.set_writer_details (reader, 48000.0, 512, 1024) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.set_writer_details (writer, 48000.0, HS_LINK_MAX_FRAMES, 3 * HS_LINK_MAX_FRAMES) == HS_LINK_OK);
        CHECK (hub.set_writer_details (writer, 48000.0, 512, 1024) == HS_LINK_OK);
    }

    SECTION ("info")
    {
        hs_link_info info {};
        info.struct_size = sizeof (info) - 1;
        CHECK (hub.get_info (observer, &info) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.get_info (observer, nullptr) == HS_LINK_BAD_ARGUMENT);
    }

    SECTION ("value sets")
    {
        std::array<float, HS_LINK_MAX_VALUES + 1> values {};
        int32_t count = 0;

        CHECK (hub.write_values (writer, 0, values.data(), HS_LINK_MAX_VALUES) == HS_LINK_OK);
        CHECK (hub.write_values (writer, 0, nullptr, 0) == HS_LINK_OK);
        CHECK (hub.write_values (writer, 0, values.data(), HS_LINK_MAX_VALUES + 1) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write_values (writer, 0, values.data(), -1) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write_values (writer, -1, values.data(), 1) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write_values (writer, HS_LINK_MAX_POSITION + 1, values.data(), 1) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write_values (writer, 0, nullptr, 1) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write_values (reader, 0, values.data(), 1) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.write_values (observer, 0, values.data(), 1) == HS_LINK_BAD_ARGUMENT);

        for (const auto bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() })
        {
            values[3] = bad;
            CHECK (hub.write_values (writer, 0, values.data(), 4) == HS_LINK_BAD_ARGUMENT);
        }

        CHECK (hub.read_values (reader, 0, values.data(), HS_LINK_MAX_VALUES + 1, &count) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.read_values (reader, 0, values.data(), -1, &count) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.read_values (reader, 0, nullptr, 1, &count) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.read_values (reader, 0, values.data(), 1, nullptr) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.read_values (reader, -1, values.data(), 1, &count) == HS_LINK_BAD_ARGUMENT);
        CHECK (hub.read_values (writer, 0, values.data(), 1, &count) == HS_LINK_BAD_ARGUMENT);
    }
}

TEST_CASE ("Link channels carry value sets by position", "[link]")
{
    const auto& hub = linkHub();
    Claims claims;
    const auto writer = claims.open ("test/values", HS_LINK_WRITER);
    const auto reader = claims.open ("test/values", HS_LINK_READER);
    const auto observer = hub.open ("test/values", HS_LINK_OBSERVER);
    REQUIRE (writer >= 0);
    REQUIRE (reader >= 0);
    REQUIRE (observer >= 0);

    // A set of `count` values: first, first + 0.125, ...
    const auto writeSet = [&hub] (int32_t handle, int64_t position, float first, int count = 3) {
        std::array<float, HS_LINK_MAX_VALUES> values {};

        for (int i = 0; i < count; ++i)
            values[static_cast<size_t> (i)] = first + static_cast<float> (i) * 0.125f;

        return hub.write_values (handle, position, values.data(), count);
    };

    struct ValuesRead
    {
        int32_t result;
        int32_t count;
        std::array<float, HS_LINK_MAX_VALUES> values;
    };

    const auto readSet = [&hub] (int32_t handle, int64_t position, int32_t maxValues = HS_LINK_MAX_VALUES) {
        ValuesRead read {};
        read.values.fill (9.0f);
        read.count = -1;
        read.result = hub.read_values (handle, position, read.values.data(), maxValues, &read.count);
        return read;
    };

    SECTION ("a read gets the set in effect at its position: the newest written at or before it")
    {
        const auto setsBefore = infoFor (reader).value_sets;
        REQUIRE (writeSet (writer, 1000, 0.25f) == HS_LINK_OK);
        REQUIRE (writeSet (writer, 2000, 0.5f) == HS_LINK_OK);
        CHECK (infoFor (reader).value_sets == setsBefore + 2);

        // Nothing yet: the values are left alone
        const auto early = readSet (reader, 999);
        CHECK (early.result == HS_LINK_NOT_YET);
        CHECK (early.count == 0);
        CHECK (juce::exactlyEqual (early.values[0], 9.0f));

        for (const auto& [position, first] : { std::pair { int64_t (1000), 0.25f }, std::pair { int64_t (1999), 0.25f }, std::pair { int64_t (2000), 0.5f }, std::pair { int64_t (900000), 0.5f } })
        {
            INFO (position);
            const auto read = readSet (reader, position);
            CHECK (read.result == HS_LINK_OK);
            CHECK (read.count == 3);
            CHECK (juce::exactlyEqual (read.values[0], first));
            CHECK (juce::exactlyEqual (read.values[2], first + 0.25f));
            CHECK (juce::exactlyEqual (read.values[3], 9.0f));
        }

        // Observers may read values too (more than one relay could serve a bay)
        CHECK (readSet (observer, 2000).result == HS_LINK_OK);
    }

    SECTION ("a reader asking for fewer values gets those; the count says how many the set has")
    {
        REQUIRE (writeSet (writer, 1000, 0.25f, 10) == HS_LINK_OK);
        const auto read = readSet (reader, 1000, 4);
        CHECK (read.result == HS_LINK_OK);
        CHECK (read.count == 10);
        CHECK (juce::exactlyEqual (read.values[3], 0.625f));
        CHECK (juce::exactlyEqual (read.values[4], 9.0f));
    }

    SECTION ("sets outlast new writer details, but the oldest go once the ring is full")
    {
        REQUIRE (writeSet (writer, 1000, 0.25f) == HS_LINK_OK);
        REQUIRE (hub.set_writer_details (writer, 48000.0, 256, 512) == HS_LINK_OK);
        CHECK (readSet (reader, 1000).result == HS_LINK_OK);

        for (int i = 0; i < HS_LINK_VALUE_SETS; ++i)
            REQUIRE (writeSet (writer, 2000 + i, 0.5f) == HS_LINK_OK);

        CHECK (readSet (reader, 1000).result == HS_LINK_GONE);
        CHECK (readSet (reader, 2000).result == HS_LINK_OK);
        CHECK (readSet (reader, 1999).result == HS_LINK_GONE);
    }

    SECTION ("going back starts the sets afresh")
    {
        REQUIRE (writeSet (writer, 5000, 0.25f) == HS_LINK_OK);
        REQUIRE (writeSet (writer, 4000, 0.5f) == HS_LINK_OK);
        CHECK (juce::exactlyEqual (readSet (reader, 6000).values[0], 0.5f));
        CHECK (juce::exactlyEqual (readSet (reader, 4500).values[0], 0.5f));
        CHECK (readSet (reader, 3999).result == HS_LINK_NOT_YET);
    }

    SECTION ("a new writer starts afresh, and the hub counts every claim")
    {
        const auto before = infoFor (observer);
        REQUIRE (writeSet (writer, 1000, 0.25f) == HS_LINK_OK);
        hub.close (writer);

        const auto newWriter = claims.open ("test/values", HS_LINK_WRITER);
        REQUIRE (newWriter >= 0);
        CHECK (readSet (reader, 2000).result == HS_LINK_NOT_YET);
        REQUIRE (writeSet (newWriter, 3000, 0.75f) == HS_LINK_OK);
        CHECK (juce::exactlyEqual (readSet (reader, 3000).values[0], 0.75f));

        hub.close (reader);
        const auto newReader = claims.open ("test/values", HS_LINK_READER);
        REQUIRE (newReader >= 0);

        const auto after = infoFor (observer);
        CHECK (after.writer_claims == before.writer_claims + 1);
        CHECK (after.reader_claims == before.reader_claims + 1);
    }
}

TEST_CASE ("Value sets are never torn while the writer runs on another thread", "[link]")
{
    // Every value in a set is the same number, different for each set: a read mixing two sets would show
    const auto& hub = linkHub();
    Claims claims;
    const auto writer = claims.open ("test/value threads", HS_LINK_WRITER);
    const auto reader = claims.open ("test/value threads", HS_LINK_READER);
    REQUIRE (writer >= 0);
    REQUIRE (reader >= 0);

    std::atomic<bool> stop { false };
    std::atomic<int64_t> newest { 0 };

    std::thread writing ([&] {
        juce::Random random (42);
        std::array<float, HS_LINK_MAX_VALUES> values {};
        int64_t position = 1000;

        while (!stop.load())
        {
            position += random.nextInt ({ 1, 513 });
            values.fill (static_cast<float> (position % 65521));
            hub.write_values (writer, position, values.data(), HS_LINK_MAX_VALUES);
            newest.store (position);
        }
    });

    juce::Random random (7);
    std::array<float, HS_LINK_MAX_VALUES> values {};
    int complete = 0, torn = 0;
    const auto deadline = juce::Time::getMillisecondCounter() + 400;

    while (juce::Time::getMillisecondCounter() < deadline)
    {
        const auto position = newest.load() - random.nextInt (HS_LINK_VALUE_SETS * 100);
        auto count = int32_t { 0 };

        if (position < 0 || hub.read_values (reader, position, values.data(), HS_LINK_MAX_VALUES, &count) != HS_LINK_OK)
            continue;

        ++complete;

        if (count != HS_LINK_MAX_VALUES || !std::all_of (values.begin(), values.end(), [first = values[0]] (float v) { return juce::exactlyEqual (v, first); }))
            ++torn;
    }

    stop.store (true);
    writing.join();

    CHECK (complete > 500);
    CHECK (torn == 0);
}

TEST_CASE ("Reads are never torn while the writer runs on another thread", "[link]")
{
    // Channel 0 holds the position, channel 1 the run it was written in. A read mixing two runs, or getting frames
    // the writer overwrote meanwhile, would show as a wrong position or a change of run inside one read.
    const auto& hub = linkHub();
    Claims claims;
    const auto writer = claims.open ("test/threads", HS_LINK_WRITER);
    const auto reader = claims.open ("test/threads", HS_LINK_READER);
    REQUIRE (writer >= 0);
    REQUIRE (reader >= 0);

    const auto positionValue = [] (int64_t position) { return static_cast<float> (position % 65521); };
    std::atomic<bool> stop { false };

    std::thread writing ([&] {
        juce::Random random (42);
        std::vector<float> positions (512), runs (512);
        int64_t position = 1000000;
        auto run = 0.0f;

        while (!stop.load())
        {
            // Now and then the run restarts, sometimes going back over positions already written
            if (random.nextInt (60) == 0)
            {
                position += random.nextInt ({ -3000, 3000 });
                run += 1.0f;
            }

            const auto numFrames = random.nextInt ({ 1, 513 });

            for (int i = 0; i < numFrames; ++i)
            {
                positions[static_cast<size_t> (i)] = positionValue (position + i);
                runs[static_cast<size_t> (i)] = run;
            }

            const float* channels[] { positions.data(), runs.data() };
            hub.write (writer, position, channels, 2, numFrames);
            position += numFrames;
        }
    });

    juce::Random random (7);
    std::vector<float> positions (512), runs (512);
    int complete = 0, torn = 0;
    const auto deadline = juce::Time::getMillisecondCounter() + 400;

    while (juce::Time::getMillisecondCounter() < deadline)
    {
        const auto info = infoFor (reader);
        const auto numFrames = random.nextInt ({ 1, 513 });
        const auto position = info.readable_end - random.nextInt (HS_LINK_CAPACITY + 2000) + 300;
        float* channels[] { positions.data(), runs.data() };

        if (position < 0 || hub.read (reader, position, channels, 2, numFrames) != HS_LINK_OK)
            continue;

        ++complete;

        for (int i = 0; i < numFrames; ++i)
        {
            const auto index = static_cast<size_t> (i);

            if (!juce::exactlyEqual (positions[index], positionValue (position + i)) || !juce::exactlyEqual (runs[index], runs[0]))
            {
                ++torn;
                break;
            }
        }
    }

    stop.store (true);
    writing.join();

    CHECK (complete > 1000);
    CHECK (torn == 0);
}
