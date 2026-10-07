#pragma once

// The link hub's plain C interface: the only thing the panel and the relays must agree on.
// The hub lives in the relay's file. The panel finds that file already loaded in the host's process and looks up
// HS_LINK_GET_API_SYMBOL; relays call it directly. Treat everything that crosses here as untrusted: the hub checks
// every argument, and callers check versions and sizes.
//
// A channel is a named stream of stereo frames. One writer stamps each block with its position on the writer's
// timeline (a sample count); one reader asks for frames by position. Frames that haven't arrived, or are no longer
// there, are reported as such, never handed out stale. A writer that skips ahead leaves a gap (those frames never
// arrive); one that goes back, or starts a new session, starts afresh.
//
// A channel also carries value sets: up to HS_LINK_MAX_VALUES numbers stamped with the position from which they apply,
// until a later set. They outlast sessions; a new writer starts them afresh.
//
// A writer can also report its latency: how much later than the positions its source was playing its audio arrives
// (for a bay's transmitter, the FL plugin's own delay, read from the position marker).

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HS_LINK_ABI_VERSION 3u
#define HS_LINK_GET_API_SYMBOL "heartseeker_link_get_api"

// Channel names: printable ASCII, 1 to HS_LINK_MAX_NAME - 1 characters
#define HS_LINK_MAX_NAME 64

// Frames a channel keeps: positions older than the newest minus this are gone
#define HS_LINK_CAPACITY 65536

// Most frames one write or read may carry
#define HS_LINK_MAX_FRAMES 16384

// Positions run from 0 to below this
#define HS_LINK_MAX_POSITION ((int64_t) 1 << 47)

// Most values in one set, and how many sets a channel keeps
#define HS_LINK_MAX_VALUES 64
#define HS_LINK_VALUE_SETS 256

// The most latency a writer may report as measured (samples)
#define HS_LINK_MAX_LATENCY 32768

enum {
    HS_LINK_WRITER = 0, // one per channel
    HS_LINK_READER = 1, // one per channel; the hub publishes where it last read
    HS_LINK_OBSERVER = 2, // any number; info only, and never creates a channel
};

enum {
    HS_LINK_OK = 0,
    HS_LINK_NOT_YET = 1, // read: some of the frames (or no value set for that position) haven't been written yet
    HS_LINK_GONE = 2, // read: some of the frames (or the value set) are no longer there (overwritten, or the writer restarted)
    HS_LINK_BAD_ARGUMENT = -1,
    HS_LINK_ROLE_TAKEN = -2,
    HS_LINK_NOT_FOUND = -3, // observer: no such channel
    HS_LINK_FULL = -4, // no room for another channel
    HS_LINK_CLOSED = -5, // the handle's claim has been closed
};

// What a writer reports about its latency
enum {
    HS_LINK_LATENCY_UNKNOWN = 0, // nothing to measure it by (e.g. no marker arriving); samples 0
    HS_LINK_LATENCY_MEASURED = 1, // samples: 0 to HS_LINK_MAX_LATENCY
    HS_LINK_LATENCY_UNREADABLE = 2, // something arrives that can't be read; samples 0
    HS_LINK_LATENCY_OUT_OF_RANGE = 3, // samples: the reading, below 0 or above HS_LINK_MAX_LATENCY
    HS_LINK_LATENCY_NO_INPUT = 4, // the writer can't measure (e.g. its Marker input is off); samples 0
};

typedef struct hs_link_info
{
    uint32_t struct_size; // the caller sets this to sizeof (hs_link_info)
    uint32_t has_writer;
    uint32_t has_reader;
    uint32_t last_read_ok; // whether the reader's last read had every frame
    uint64_t session; // changes whenever the writer sets its details
    double sample_rate; // the writer's details, 0 until set
    int32_t max_block;
    int32_t round_trip;
    int64_t readable_start; // frames from here...
    int64_t readable_end; // ...to just before here can be read
    int64_t last_read_position; // where the reader's last read started
    int32_t last_read_length;
    uint32_t writer_claims; // times each role has been claimed, to tell when a new writer or reader has taken over
    uint32_t reader_claims;
    uint32_t reserved;
    uint64_t writes; // calls so far, to tell whether each side is running
    uint64_t reads;
    uint64_t value_sets; // value sets written so far
    uint64_t failed_reads; // reads that didn't get every frame
    int32_t latency_state; // the writer's latest latency report (HS_LINK_LATENCY_...), unknown since it claimed
    int32_t latency;
    uint64_t latency_reports; // reports so far, to tell a new one from the last
} hs_link_info;

typedef struct hs_link_api
{
    uint32_t struct_size;
    uint32_t abi_version;

    // Never on the audio thread: these may wait for each other. open returns a handle (>= 0) or an error.
    int32_t (*open) (const char* name, int32_t role);
    void (*close) (int32_t handle);

    // Writer only: its sample rate, largest block and round trip (samples, 0 if none). Starts a new session,
    // so everything written before can no longer be read.
    int32_t (*set_writer_details) (int32_t handle, double sample_rate, int32_t max_block, int32_t round_trip);

    // Any thread, including audio threads: no locks, no allocation. num_channels is 1 or 2; a mono write fills both
    // channels, a mono read gets the first. A read that fails leaves its frames silent.
    int32_t (*write) (int32_t handle, int64_t position, const float* const* channels, int32_t num_channels, int32_t num_frames);
    int32_t (*read) (int32_t handle, int64_t position, float* const* channels, int32_t num_channels, int32_t num_frames);
    int32_t (*get_info) (int32_t handle, hs_link_info* info);

    // Any thread, as write and read. Writer: values (finite, 0 to HS_LINK_MAX_VALUES of them) that apply from `position`
    // on; a set before the newest one starts the sets afresh. Reader or observer: the set in effect at `position` (the
    // newest written at or before it); copies up to max_values of its values and sets *num_values to how many it has.
    // A read that fails changes nothing but *num_values (0).
    int32_t (*write_values) (int32_t handle, int64_t position, const float* values, int32_t num_values);
    int32_t (*read_values) (int32_t handle, int64_t position, float* values, int32_t max_values, int32_t* num_values);

    // Writer only, any thread, as write: its latency (state HS_LINK_LATENCY_..., samples as that state allows). Report
    // after writing the audio it applies to: a reader that sees the report also sees that audio.
    int32_t (*set_latency) (int32_t handle, int32_t state, int32_t samples);
} hs_link_api;

// The one exported function: null unless this hub speaks the requested version
typedef const hs_link_api* (*hs_link_get_api_function) (uint32_t abi_version);

#ifdef __cplusplus
}
#endif
