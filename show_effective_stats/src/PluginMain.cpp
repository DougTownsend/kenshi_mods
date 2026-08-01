#include "Diagnostics.h"
#include "EffectiveStatsDisplay.h"

// RE_Kenshi discovers this exact exported symbol when it loads the mod DLL.
__declspec(dllexport) void startPlugin()
{
    ShowEffectiveStats::Log("startup version=1.0");
    ShowEffectiveStats::Install();
}
