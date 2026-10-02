#pragma once

#include "engine/BayLink.h"

// Finds the link hub inside a relay the host has already loaded into this process. Never loads anything: it asks
// for the relay's file among those already loaded, looks up the hub's one exported function, and checks its version.
// From then on it keeps that file loaded, so the hub can't vanish while the panel uses it.
// Windows only so far; elsewhere it reports that linking isn't available yet.
class RelayHubFinder
{
public:
    RelayHubFinder() = default;
    ~RelayHubFinder();

    // Message thread. Once found, returns the same hub every time.
    BayLink::HubSearch find();

    // The relay's binary, as hosts load it
    static constexpr auto relayFileName = "heartseeker relay.vst3";

private:
    [[maybe_unused]] void* module = nullptr; // Windows: the relay's HMODULE, held loaded
    const hs_link_api* hub = nullptr;

    JUCE_DECLARE_NON_COPYABLE (RelayHubFinder)
};
