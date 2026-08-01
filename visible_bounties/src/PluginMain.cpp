#include "BountyLabels.h"
#include "Diagnostics.h"

// RE_Kenshi discovers this exact exported symbol when it loads the mod DLL.
__declspec(dllexport) void startPlugin()
{
    VisibleBounties::Log("startup version=1.0");
    VisibleBounties::Install();
}
