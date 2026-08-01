#include <Debug.h>

#include "Diagnostics.h"

namespace
{
    const char* const kLogPrefix = "ShowEffectiveStats: ";
}

namespace ShowEffectiveStats
{
    void Log(const std::string& message)
    {
        DebugLog(std::string(kLogPrefix) + message);
    }
}
