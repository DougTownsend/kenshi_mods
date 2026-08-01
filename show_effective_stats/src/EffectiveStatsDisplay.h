#pragma once

namespace ShowEffectiveStats
{
    // Installs the single read-only stat-panel hook. Safe to call once from
    // startPlugin; returns false without touching the game if installation fails.
    bool Install();
}
