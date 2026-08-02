#include "XpBarsDisplay.h"

// RE_Kenshi discovers this exact exported symbol when it loads the mod DLL.
__declspec(dllexport) void startPlugin()
{
    XpBars::Install();
}
