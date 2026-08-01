#include <Debug.h>

#include "Diagnostics.h"

namespace
{
    const char* const kLogPrefix = "VisibleBounties: ";
}

namespace VisibleBounties
{
    void Log(const std::string& message)
    {
        DebugLog(std::string(kLogPrefix) + message);
    }
}
