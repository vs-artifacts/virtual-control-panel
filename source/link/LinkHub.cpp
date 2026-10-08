#include "LinkHub.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>

namespace
{
    constexpr int maxChannels = 64;
    constexpr int64_t capacity = HS_LINK_CAPACITY;
    constexpr int64_t positionMask = capacity - 1;
    static_assert ((capacity & positionMask) == 0, "the capacity must be a power of two");
    static_assert (HS_LINK_MAX_FRAMES < capacity);

    // A handle: the role in bits 0-1, the channel in bits 2-7, the claim number above them (0 for observers)
    constexpr uint32_t roleMask = 3;
    constexpr int slotShift = 2;
    constexpr uint32_t slotMask = 63;
    constexpr int claimShift = 8;
    constexpr uint32_t maxClaim = (1u << 23) - 1;
    static_assert (maxChannels <= static_cast<int> (slotMask) + 1);

    // The reader's last read in one word, so it's always seen whole: position, length, whether it had every frame
    constexpr int lengthShift = 47;
    constexpr uint64_t positionBits = (uint64_t (1) << lengthShift) - 1;
    constexpr uint64_t okBit = uint64_t (1) << 63;
    static_assert (HS_LINK_MAX_FRAMES < (1 << 16));

    static_assert (std::atomic<float>::is_always_lock_free);
    static_assert (std::atomic<double>::is_always_lock_free);
    static_assert (std::atomic<int64_t>::is_always_lock_free);
    static_assert (std::atomic<uint64_t>::is_always_lock_free);

    constexpr uint64_t valueSetCount = HS_LINK_VALUE_SETS;

    // One value set. Odd sequence: being written; a reader that sees it change drops what it copied.
    struct ValueSet
    {
        std::atomic<uint32_t> sequence { 0 };
        std::atomic<uint32_t> epoch { 0 }; // sets from an earlier epoch are no longer readable
        std::atomic<int64_t> position { 0 };
        std::atomic<int32_t> count { 0 };
        std::array<std::atomic<float>, HS_LINK_MAX_VALUES> values {};
    };

    struct Channel
    {
        // Named once, under the hub's mutex, and never removed, so a handle can never point at freed memory
        char name[HS_LINK_MAX_NAME] {};
        std::unique_ptr<std::atomic<float>[]> frames; // two channels of `capacity` frames
        std::unique_ptr<ValueSet[]> valueSets; // valueSetCount of them, reused in turn
        std::atomic<bool> named { false };

        // 0: free; otherwise the claim number in its owner's handle
        std::atomic<uint32_t> writerClaim { 0 };
        std::atomic<uint32_t> readerClaim { 0 };
        std::atomic<uint32_t> writerClaims { 0 };
        std::atomic<uint32_t> readerClaims { 0 };

        std::atomic<uint64_t> session { 0 };
        std::atomic<double> sampleRate { 0.0 };
        std::atomic<int32_t> maxBlock { 0 };
        std::atomic<int32_t> roundTrip { 0 };
        std::atomic<bool> restartNext { true };

        // Odd while the writer restarts its run; readers drop anything they read meanwhile
        std::atomic<uint32_t> restarts { 0 };
        // The current run of writes: frames from runStart to just before written, except the latest gap
        std::atomic<int64_t> runStart { 0 };
        std::atomic<int64_t> written { 0 };
        // Frames the writer skipped: they never arrive. Only the latest gap is remembered.
        std::atomic<int64_t> gapStart { 0 };
        std::atomic<int64_t> gapEnd { 0 };
        // Where the block being written ends: frames older than this minus the capacity may be overwritten already
        std::atomic<int64_t> writing { 0 };

        std::atomic<uint64_t> lastRead { 0 };
        std::atomic<uint64_t> writes { 0 };
        std::atomic<uint64_t> reads { 0 };
        std::atomic<uint64_t> failedReads { 0 };

        std::atomic<uint64_t> valueWrites { 0 }; // sets written so far; the next goes in valueSets[valueWrites % count]
        std::atomic<uint32_t> valueEpoch { 1 };

        // The writer's latency report: the state in the upper half, the samples (as int32_t) in the lower
        std::atomic<uint64_t> latency { 0 };
        std::atomic<uint64_t> latencyReports { 0 };
    };

    bool isValidName (const char* name)
    {
        if (name == nullptr)
            return false;

        for (int i = 0; i < HS_LINK_MAX_NAME; ++i)
        {
            const auto ch = static_cast<unsigned char> (name[i]);

            if (ch == 0)
                return i > 0;

            if (ch < 0x20 || ch > 0x7e)
                return false;
        }

        return false;
    }

    // Checked by its bits: fast-math builds may assume NaN and infinity never happen
    bool isFinite (double value)
    {
        return (std::bit_cast<uint64_t> (value) & 0x7ff0000000000000ull) != 0x7ff0000000000000ull;
    }

    bool isFinite (float value)
    {
        return (std::bit_cast<uint32_t> (value) & 0x7f800000u) != 0x7f800000u;
    }

    bool isValidBlock (int64_t position, int32_t numFrames)
    {
        return numFrames >= 0 && numFrames <= HS_LINK_MAX_FRAMES && position >= 0 && position <= HS_LINK_MAX_POSITION - numFrames;
    }

    template <typename Pointer>
    bool isValidChannels (Pointer const* channels, int32_t numChannels)
    {
        return channels != nullptr && (numChannels == 1 || numChannels == 2) && channels[0] != nullptr && channels[numChannels - 1] != nullptr;
    }

    bool isValidLatency (int32_t state, int32_t samples)
    {
        switch (state)
        {
            case HS_LINK_LATENCY_MEASURED:
                return samples >= 0 && samples <= HS_LINK_MAX_LATENCY;
            case HS_LINK_LATENCY_OUT_OF_RANGE:
                return samples < 0 || samples > HS_LINK_MAX_LATENCY;
            case HS_LINK_LATENCY_UNKNOWN:
            case HS_LINK_LATENCY_UNREADABLE:
            case HS_LINK_LATENCY_NO_INPUT:
            case HS_LINK_LATENCY_HELD:
                return samples == 0;
            default:
                return false;
        }
    }

    uint64_t packLatency (int32_t state, int32_t samples)
    {
        return (static_cast<uint64_t> (static_cast<uint32_t> (state)) << 32) | static_cast<uint32_t> (samples);
    }

    class Hub
    {
    public:
        int32_t open (const char* name, int32_t role)
        {
            if (!isValidName (name) || role < HS_LINK_WRITER || role > HS_LINK_OBSERVER)
                return HS_LINK_BAD_ARGUMENT;

            const std::scoped_lock lock (mutex);
            auto slot = findNamed (name);

            if (slot < 0)
            {
                if (role == HS_LINK_OBSERVER)
                    return HS_LINK_NOT_FOUND;

                slot = createChannel (name);

                if (slot < 0)
                    return HS_LINK_FULL;
            }

            const auto slotBits = static_cast<uint32_t> (slot) << slotShift;

            if (role == HS_LINK_OBSERVER)
                return static_cast<int32_t> (slotBits | HS_LINK_OBSERVER);

            auto& channel = channels[static_cast<size_t> (slot)];
            auto& claimWord = claimFor (channel, role);

            if (claimWord.load (std::memory_order_relaxed) != 0)
                return HS_LINK_ROLE_TAKEN;

            lastClaim = lastClaim % maxClaim + 1;

            if (role == HS_LINK_WRITER)
            {
                channel.restartNext.store (true, std::memory_order_release);
                channel.valueEpoch.fetch_add (1, std::memory_order_acq_rel);
                channel.writerClaims.fetch_add (1, std::memory_order_relaxed);
                channel.latency.store (packLatency (HS_LINK_LATENCY_UNKNOWN, 0), std::memory_order_release);
            }
            else
            {
                channel.lastRead.store (0, std::memory_order_relaxed);
                channel.readerClaims.fetch_add (1, std::memory_order_relaxed);
            }

            claimWord.store (lastClaim, std::memory_order_release);
            return static_cast<int32_t> ((lastClaim << claimShift) | slotBits | static_cast<uint32_t> (role));
        }

        void close (int32_t handle)
        {
            const std::scoped_lock lock (mutex);
            Channel* channel = nullptr;
            int32_t role = 0;

            if (resolve (handle, channel, role) != HS_LINK_OK || role == HS_LINK_OBSERVER)
                return;

            if (role == HS_LINK_WRITER)
            {
                channel->restartNext.store (true, std::memory_order_release);
                channel->latency.store (packLatency (HS_LINK_LATENCY_UNKNOWN, 0), std::memory_order_release);
            }
            else
            {
                channel->lastRead.store (0, std::memory_order_relaxed);
            }

            claimFor (*channel, role).store (0, std::memory_order_release);
        }

        int32_t setWriterDetails (int32_t handle, double sampleRate, int32_t maxBlock, int32_t roundTrip)
        {
            const std::scoped_lock lock (mutex);
            Channel* channel = nullptr;
            int32_t role = 0;

            if (const auto result = resolve (handle, channel, role); result != HS_LINK_OK)
                return result;

            const auto rateIsValid = isFinite (sampleRate) && sampleRate > 0.0 && sampleRate <= 1.0e6;

            if (role != HS_LINK_WRITER || !rateIsValid || maxBlock < 1 || maxBlock > HS_LINK_MAX_FRAMES || roundTrip < 0 || roundTrip > capacity)
                return HS_LINK_BAD_ARGUMENT;

            channel->sampleRate.store (sampleRate, std::memory_order_relaxed);
            channel->maxBlock.store (maxBlock, std::memory_order_relaxed);
            channel->roundTrip.store (roundTrip, std::memory_order_relaxed);
            channel->restartNext.store (true, std::memory_order_release);
            channel->session.fetch_add (1, std::memory_order_acq_rel);
            return HS_LINK_OK;
        }

        int32_t write (int32_t handle, int64_t position, const float* const* source, int32_t numChannels, int32_t numFrames)
        {
            Channel* channel = nullptr;
            int32_t role = 0;

            if (const auto result = resolve (handle, channel, role); result != HS_LINK_OK)
                return result;

            if (role != HS_LINK_WRITER || !isValidBlock (position, numFrames) || !isValidChannels (source, numChannels))
                return HS_LINK_BAD_ARGUMENT;

            if (numFrames == 0)
                return HS_LINK_OK;

            auto& c = *channel;
            const auto end = position + numFrames;
            const auto previousEnd = c.written.load (std::memory_order_relaxed);
            const auto restart = c.restartNext.load (std::memory_order_acquire) || position < previousEnd;

            if (restart || position > previousEnd)
            {
                // The readable range changes shape: readers drop anything they read meanwhile
                const auto sequence = c.restarts.load (std::memory_order_relaxed);
                c.restarts.store (sequence + 1, std::memory_order_relaxed);
                std::atomic_thread_fence (std::memory_order_release);

                if (restart)
                {
                    // A new session, or going back over written positions: nothing before this is part of the run
                    c.restartNext.store (false, std::memory_order_relaxed);
                    c.runStart.store (position, std::memory_order_relaxed);
                    c.gapStart.store (0, std::memory_order_relaxed);
                    c.gapEnd.store (0, std::memory_order_relaxed);
                    c.written.store (position, std::memory_order_relaxed);
                }
                else
                {
                    // Skipping ahead leaves a gap, and what came before it stays readable. Only the latest gap is
                    // remembered, so nothing before the one it replaces stays readable.
                    if (const auto lastGapEnd = c.gapEnd.load (std::memory_order_relaxed); lastGapEnd > c.gapStart.load (std::memory_order_relaxed))
                        c.runStart.store (std::max (c.runStart.load (std::memory_order_relaxed), lastGapEnd), std::memory_order_relaxed);

                    c.gapStart.store (previousEnd, std::memory_order_relaxed);
                    c.gapEnd.store (position, std::memory_order_relaxed);
                }

                c.writing.store (end, std::memory_order_relaxed);
                copyIn (c, position, source, numChannels, numFrames);
                c.written.store (end, std::memory_order_release);
                c.restarts.store (sequence + 2, std::memory_order_release);
            }
            else
            {
                c.writing.store (end, std::memory_order_relaxed);
                std::atomic_thread_fence (std::memory_order_release);
                copyIn (c, position, source, numChannels, numFrames);
                c.written.store (end, std::memory_order_release);
            }

            c.writes.fetch_add (1, std::memory_order_relaxed);
            return HS_LINK_OK;
        }

        int32_t read (int32_t handle, int64_t position, float* const* destination, int32_t numChannels, int32_t numFrames)
        {
            Channel* channel = nullptr;
            int32_t role = 0;

            if (const auto result = resolve (handle, channel, role); result != HS_LINK_OK)
                return result;

            if (role != HS_LINK_READER || !isValidBlock (position, numFrames) || !isValidChannels (destination, numChannels))
                return HS_LINK_BAD_ARGUMENT;

            const auto result = numFrames == 0 ? HS_LINK_OK : readFrames (*channel, position, destination, numChannels, numFrames);

            if (result != HS_LINK_OK)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    std::fill_n (destination[ch], numFrames, 0.0f);

                channel->failedReads.fetch_add (1, std::memory_order_relaxed);
            }

            const auto packed = static_cast<uint64_t> (position) | (static_cast<uint64_t> (numFrames) << lengthShift) | (result == HS_LINK_OK ? okBit : 0);
            channel->lastRead.store (packed, std::memory_order_relaxed);
            channel->reads.fetch_add (1, std::memory_order_release);
            return result;
        }

        int32_t getInfo (int32_t handle, hs_link_info* info)
        {
            Channel* channel = nullptr;
            int32_t role = 0;

            if (const auto result = resolve (handle, channel, role); result != HS_LINK_OK)
                return result;

            if (info == nullptr || info->struct_size < sizeof (hs_link_info))
                return HS_LINK_BAD_ARGUMENT;

            const auto& c = *channel;
            const auto structSize = info->struct_size;
            *info = {};
            info->struct_size = structSize;

            // Before the frames' end: a report made after a write then never shows without that write
            info->latency_reports = c.latencyReports.load (std::memory_order_acquire);
            const auto latency = c.latency.load (std::memory_order_acquire);
            info->latency_state = static_cast<int32_t> (static_cast<uint32_t> (latency >> 32));
            info->latency = static_cast<int32_t> (static_cast<uint32_t> (latency));

            info->has_writer = c.writerClaim.load (std::memory_order_acquire) != 0 ? 1u : 0u;
            info->has_reader = c.readerClaim.load (std::memory_order_acquire) != 0 ? 1u : 0u;
            info->session = c.session.load (std::memory_order_acquire);
            info->sample_rate = c.sampleRate.load (std::memory_order_relaxed);
            info->max_block = c.maxBlock.load (std::memory_order_relaxed);
            info->round_trip = c.roundTrip.load (std::memory_order_relaxed);

            const auto end = c.written.load (std::memory_order_acquire);
            info->readable_end = end;

            // Waiting for a new session's first write: nothing from the old one may be read
            if (c.restartNext.load (std::memory_order_acquire))
                info->readable_start = end;
            else
                info->readable_start = std::min (end, std::max (c.runStart.load (std::memory_order_relaxed), end - capacity));

            info->reads = c.reads.load (std::memory_order_acquire);
            const auto lastRead = c.lastRead.load (std::memory_order_relaxed);
            info->last_read_position = static_cast<int64_t> (lastRead & positionBits);
            info->last_read_length = static_cast<int32_t> ((lastRead >> lengthShift) & 0xffff);
            info->last_read_ok = (lastRead & okBit) != 0 ? 1u : 0u;
            info->writes = c.writes.load (std::memory_order_relaxed);
            info->writer_claims = c.writerClaims.load (std::memory_order_relaxed);
            info->reader_claims = c.readerClaims.load (std::memory_order_relaxed);
            info->value_sets = c.valueWrites.load (std::memory_order_relaxed);
            info->failed_reads = c.failedReads.load (std::memory_order_relaxed);
            return HS_LINK_OK;
        }

        int32_t writeValues (int32_t handle, int64_t position, const float* values, int32_t numValues)
        {
            Channel* channel = nullptr;
            int32_t role = 0;

            if (const auto result = resolve (handle, channel, role); result != HS_LINK_OK)
                return result;

            if (role != HS_LINK_WRITER || !isValidBlock (position, 0) || numValues < 0 || numValues > HS_LINK_MAX_VALUES || (numValues > 0 && values == nullptr))
                return HS_LINK_BAD_ARGUMENT;

            for (int32_t i = 0; i < numValues; ++i)
                if (!isFinite (values[i]))
                    return HS_LINK_BAD_ARGUMENT;

            auto& c = *channel;
            auto epoch = c.valueEpoch.load (std::memory_order_acquire);
            const auto written = c.valueWrites.load (std::memory_order_relaxed);

            // Going back: the newer sets would hide this one, so they all go
            if (written > 0)
                if (const auto& newest = c.valueSets[(written - 1) % valueSetCount]; newest.epoch.load (std::memory_order_relaxed) == epoch && position < newest.position.load (std::memory_order_relaxed))
                    epoch = c.valueEpoch.fetch_add (1, std::memory_order_acq_rel) + 1;

            auto& set = c.valueSets[written % valueSetCount];
            const auto sequence = set.sequence.load (std::memory_order_relaxed);
            set.sequence.store (sequence + 1, std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_release);

            set.epoch.store (epoch, std::memory_order_relaxed);
            set.position.store (position, std::memory_order_relaxed);
            set.count.store (numValues, std::memory_order_relaxed);

            for (int32_t i = 0; i < numValues; ++i)
                set.values[static_cast<size_t> (i)].store (values[i], std::memory_order_relaxed);

            set.sequence.store (sequence + 2, std::memory_order_release);
            c.valueWrites.store (written + 1, std::memory_order_release);
            return HS_LINK_OK;
        }

        int32_t readValues (int32_t handle, int64_t position, float* values, int32_t maxValues, int32_t* numValues)
        {
            Channel* channel = nullptr;
            int32_t role = 0;

            if (const auto result = resolve (handle, channel, role); result != HS_LINK_OK)
                return result;

            if (role == HS_LINK_WRITER || !isValidBlock (position, 0) || maxValues < 0 || maxValues > HS_LINK_MAX_VALUES || (maxValues > 0 && values == nullptr) || numValues == nullptr)
                return HS_LINK_BAD_ARGUMENT;

            *numValues = 0;
            const auto& c = *channel;

            // A set the writer overwrites while we copy it: look again, from the newest
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                const auto epoch = c.valueEpoch.load (std::memory_order_acquire);
                const auto written = c.valueWrites.load (std::memory_order_acquire);
                const auto oldest = written > valueSetCount ? written - valueSetCount : 0;
                auto result = written > valueSetCount ? HS_LINK_GONE : HS_LINK_NOT_YET;
                auto changed = false;

                for (auto number = written; number > oldest; --number)
                {
                    const auto& set = c.valueSets[(number - 1) % valueSetCount];
                    const auto sequence = set.sequence.load (std::memory_order_acquire);

                    if ((sequence & 1u) != 0)
                    {
                        changed = true;
                        break;
                    }

                    const auto setEpoch = set.epoch.load (std::memory_order_relaxed);
                    const auto setPosition = set.position.load (std::memory_order_relaxed);
                    const auto setCount = std::clamp (set.count.load (std::memory_order_relaxed), 0, HS_LINK_MAX_VALUES);
                    const auto toCopy = std::min (setCount, maxValues);
                    std::array<float, HS_LINK_MAX_VALUES> copy {};

                    for (int32_t i = 0; i < toCopy; ++i)
                        copy[static_cast<size_t> (i)] = set.values[static_cast<size_t> (i)].load (std::memory_order_relaxed);

                    std::atomic_thread_fence (std::memory_order_acquire);

                    if (set.sequence.load (std::memory_order_relaxed) != sequence)
                    {
                        changed = true;
                        break;
                    }

                    // Epochs only grow in the order sets are written, so nothing older is readable either
                    if (setEpoch != epoch)
                    {
                        result = HS_LINK_NOT_YET;
                        break;
                    }

                    if (setPosition <= position)
                    {
                        std::copy_n (copy.begin(), toCopy, values);
                        *numValues = setCount;
                        return HS_LINK_OK;
                    }
                }

                if (!changed)
                    return result;
            }

            return HS_LINK_GONE;
        }

        int32_t setLatency (int32_t handle, int32_t state, int32_t samples)
        {
            Channel* channel = nullptr;
            int32_t role = 0;

            if (const auto result = resolve (handle, channel, role); result != HS_LINK_OK)
                return result;

            if (role != HS_LINK_WRITER || !isValidLatency (state, samples))
                return HS_LINK_BAD_ARGUMENT;

            channel->latency.store (packLatency (state, samples), std::memory_order_release);
            channel->latencyReports.fetch_add (1, std::memory_order_release);
            return HS_LINK_OK;
        }

    private:
        static std::atomic<uint32_t>& claimFor (Channel& channel, int32_t role)
        {
            return role == HS_LINK_WRITER ? channel.writerClaim : channel.readerClaim;
        }

        // The channel a handle refers to, if the handle is well formed and (for a writer or reader) still holds its claim
        int32_t resolve (int32_t handle, Channel*& channel, int32_t& role)
        {
            if (handle < 0)
                return HS_LINK_BAD_ARGUMENT;

            const auto bits = static_cast<uint32_t> (handle);
            const auto slot = (bits >> slotShift) & slotMask;
            const auto claim = bits >> claimShift;
            role = static_cast<int32_t> (bits & roleMask);

            if (role > HS_LINK_OBSERVER || slot >= static_cast<uint32_t> (maxChannels))
                return HS_LINK_BAD_ARGUMENT;

            auto& c = channels[slot];

            if (!c.named.load (std::memory_order_acquire))
                return HS_LINK_BAD_ARGUMENT;

            if (role == HS_LINK_OBSERVER)
            {
                if (claim != 0)
                    return HS_LINK_BAD_ARGUMENT;
            }
            else if (claim == 0)
            {
                return HS_LINK_BAD_ARGUMENT;
            }
            else if (claimFor (c, role).load (std::memory_order_acquire) != claim)
            {
                return HS_LINK_CLOSED;
            }

            channel = &c;
            return HS_LINK_OK;
        }

        int findNamed (const char* name) const
        {
            for (size_t slot = 0; slot < channels.size(); ++slot)
                if (channels[slot].named.load (std::memory_order_relaxed) && std::strncmp (channels[slot].name, name, HS_LINK_MAX_NAME) == 0)
                    return static_cast<int> (slot);

            return -1;
        }

        int createChannel (const char* name)
        {
            for (size_t slot = 0; slot < channels.size(); ++slot)
            {
                auto& c = channels[slot];

                if (c.named.load (std::memory_order_relaxed))
                    continue;

                c.frames.reset (new (std::nothrow) std::atomic<float>[2 * capacity]());
                c.valueSets.reset (new (std::nothrow) ValueSet[valueSetCount]);

                if (c.frames == nullptr || c.valueSets == nullptr)
                {
                    c.frames.reset();
                    c.valueSets.reset();
                    return -1;
                }

                // Checked by isValidName: shorter than the buffer, which is still all zeros
                std::memcpy (c.name, name, std::strlen (name));
                c.named.store (true, std::memory_order_release);
                return static_cast<int> (slot);
            }

            return -1;
        }

        static void copyIn (Channel& c, int64_t position, const float* const* source, int32_t numChannels, int32_t numFrames)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* from = source[std::min (ch, numChannels - 1)];
                auto* to = c.frames.get() + ch * capacity;

                for (int32_t i = 0; i < numFrames; ++i)
                    to[(position + i) & positionMask].store (from[i], std::memory_order_relaxed);
            }
        }

        static int32_t readFrames (const Channel& c, int64_t position, float* const* destination, int32_t numChannels, int32_t numFrames)
        {
            const auto sequence = c.restarts.load (std::memory_order_acquire);

            // Odd: the writer is changing the run. Restart pending: a new session or writer, so the old run is over.
            if ((sequence & 1u) != 0 || c.restartNext.load (std::memory_order_acquire))
                return HS_LINK_GONE;

            const auto end = c.written.load (std::memory_order_acquire);
            const auto start = std::max (c.runStart.load (std::memory_order_relaxed), end - capacity);

            if (position < start || (position < c.gapEnd.load (std::memory_order_relaxed) && position + numFrames > c.gapStart.load (std::memory_order_relaxed)))
                return HS_LINK_GONE;

            if (position + numFrames > end)
                return HS_LINK_NOT_YET;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto* from = c.frames.get() + ch * capacity;
                auto* to = destination[ch];

                for (int32_t i = 0; i < numFrames; ++i)
                    to[i] = from[(position + i) & positionMask].load (std::memory_order_relaxed);
            }

            // Anything the writer changed while we copied makes the copy worthless
            std::atomic_thread_fence (std::memory_order_acquire);

            if (c.restarts.load (std::memory_order_relaxed) != sequence || position < c.writing.load (std::memory_order_relaxed) - capacity)
                return HS_LINK_GONE;

            return HS_LINK_OK;
        }

        std::mutex mutex; // guards open, close and writer details; never taken by write, read or get_info
        std::array<Channel, maxChannels> channels;
        uint32_t lastClaim = 0;
    };

    Hub& hub()
    {
        static Hub instance;
        return instance;
    }

    int32_t apiOpen (const char* name, int32_t role) { return hub().open (name, role); }
    void apiClose (int32_t handle) { hub().close (handle); }
    int32_t apiSetWriterDetails (int32_t handle, double sampleRate, int32_t maxBlock, int32_t roundTrip) { return hub().setWriterDetails (handle, sampleRate, maxBlock, roundTrip); }
    int32_t apiWrite (int32_t handle, int64_t position, const float* const* channels, int32_t numChannels, int32_t numFrames) { return hub().write (handle, position, channels, numChannels, numFrames); }
    int32_t apiRead (int32_t handle, int64_t position, float* const* channels, int32_t numChannels, int32_t numFrames) { return hub().read (handle, position, channels, numChannels, numFrames); }
    int32_t apiGetInfo (int32_t handle, hs_link_info* info) { return hub().getInfo (handle, info); }
    int32_t apiWriteValues (int32_t handle, int64_t position, const float* values, int32_t numValues) { return hub().writeValues (handle, position, values, numValues); }
    int32_t apiReadValues (int32_t handle, int64_t position, float* values, int32_t maxValues, int32_t* numValues) { return hub().readValues (handle, position, values, maxValues, numValues); }
    int32_t apiSetLatency (int32_t handle, int32_t state, int32_t samples) { return hub().setLatency (handle, state, samples); }

    constexpr hs_link_api api {
        sizeof (hs_link_api),
        HS_LINK_ABI_VERSION,
        apiOpen,
        apiClose,
        apiSetWriterDetails,
        apiWrite,
        apiRead,
        apiGetInfo,
        apiWriteValues,
        apiReadValues,
        apiSetLatency,
    };
}

extern "C" const hs_link_api* heartseeker_link_get_api (uint32_t abi_version)
{
    if (abi_version != HS_LINK_ABI_VERSION)
        return nullptr;

    hub(); // created now, on the caller's (non-audio) thread, so no audio thread ever pays for it
    return &api;
}
