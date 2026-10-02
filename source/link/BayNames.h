#pragma once

#include <string>

// How the panel and the relays name their channels in the hub: "Panel 1/bay 1/send" and "Panel 1/bay 1/return".
// Plain C++ with no other dependencies, so both sides can share it.
namespace BayNames
{
    // A panel takes the first free link name; a relay joins the first panel it finds
    inline constexpr int maxPanels = 8;

    inline std::string linkName (int panel)
    {
        return "Panel " + std::to_string (panel);
    }

    // Panel to receiver
    inline std::string send (int panel, int bay)
    {
        return linkName (panel) + "/bay " + std::to_string (bay) + "/send";
    }

    // Transmitter to panel
    inline std::string back (int panel, int bay)
    {
        return linkName (panel) + "/bay " + std::to_string (bay) + "/return";
    }
}
