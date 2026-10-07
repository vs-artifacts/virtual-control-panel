#include "RelayHubFinder.h"

#if JUCE_WINDOWS
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

RelayHubFinder::~RelayHubFinder()
{
#if JUCE_WINDOWS
    if (module != nullptr)
        FreeLibrary (static_cast<HMODULE> (module));
#endif
}

BayLink::HubSearch RelayHubFinder::find()
{
    if (hub != nullptr)
        return { hub, {} };

#if JUCE_WINDOWS
    // Flags 0: finds the file only if it's already loaded, and holds it loaded until FreeLibrary
    HMODULE found = nullptr;

    if (GetModuleHandleExW (0, juce::String (relayFileName).toWideCharPointer(), &found) == 0 || found == nullptr)
        return { nullptr, "No relay loaded in this process" };

    const auto symbol = GetProcAddress (found, HS_LINK_GET_API_SYMBOL);
    const auto getApi = reinterpret_cast<hs_link_get_api_function> (reinterpret_cast<void*> (symbol));

    if (getApi == nullptr)
    {
        FreeLibrary (found);
        return { nullptr, "The loaded relay has no link (an older build)" };
    }

    const auto* api = getApi (HS_LINK_ABI_VERSION);
    const auto complete = api != nullptr && api->struct_size >= sizeof (hs_link_api) && api->abi_version == HS_LINK_ABI_VERSION
                          && api->open != nullptr && api->close != nullptr && api->set_writer_details != nullptr
                          && api->write != nullptr && api->read != nullptr && api->get_info != nullptr
                          && api->write_values != nullptr && api->read_values != nullptr && api->set_latency != nullptr;

    if (!complete)
    {
        FreeLibrary (found);
        return { nullptr, "The loaded relay's link is a different version from this panel's" };
    }

    module = found;
    hub = api;
    return { hub, {} };
#else
    // macOS: list the loaded images (_dyld_image_count, _dyld_get_image_name), take the relay's with
    // dlopen (path, RTLD_NOLOAD), then dlsym (docs/decisions.md, "Bay link")
    return { nullptr, "Linking to relays isn't available on this system yet" };
#endif
}
