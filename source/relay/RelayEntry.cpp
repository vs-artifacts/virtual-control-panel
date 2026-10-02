#include "RelayProcessor.h"

// Only in the relay plugin target: the Tests target also links the panel, which has its own
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RelayProcessor();
}
