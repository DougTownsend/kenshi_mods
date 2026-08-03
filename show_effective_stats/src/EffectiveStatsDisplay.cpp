#include <core/Functions.h>
#include <kenshi/CharStats.h>
#include <kenshi/Character.h>
#include <kenshi/gui/DataPanelLine.h>
#include <kenshi/gui/DatapanelGUI.h>

// KenshiLib exposes a callable stub for updateStats, but the generated header
// preserves the game's private access marker. This narrow include-time access
// adjustment is only needed to take that method's address for the hook.
#define private public
#include <kenshi/gui/CharacterStatsWindow.h>
#undef private

#include <iomanip>
#include <sstream>
#include <string>

#include "Diagnostics.h"
#include "EffectiveStatsDisplay.h"

namespace
{
    typedef void (__cdecl *UpdateStatsFn)(CharacterStatsWindow*);
    UpdateStatsFn g_originalUpdateStats = 0;
    typedef void (__cdecl *UpdateWindowFn)(CharacterStatsWindow*);
    UpdateWindowFn g_originalUpdateWindow = 0;
    typedef void (__cdecl *SetupStatsFn)(CharacterStatsWindow*);
    SetupStatsFn g_originalSetupStats = 0;

    const float kStatsWindowWidthScale = 1.24f;
    const int kValueColumnWidth = 92;
    const int kValueColumnRightPadding = 8;
    const int kNameColumnRightPadding = 8;
    MyGUI::Widget* FindWidgetBySuffix(MyGUI::Widget* widget, const std::string& suffix);

    int ScaleWidth(int value)
    {
        return static_cast<int>(value * kStatsWindowWidthScale + 0.5f);
    }

    void ExpandSkillPanels(CharacterStatsWindow* window, MyGUI::Widget* root)
    {
        DatapanelGUI* const panels[] =
        {
            window->skills1Datapanel,
            window->skills2Datapanel,
            window->skills3Datapanel,
            window->skills4Datapanel
        };
        if (!panels[0] || !panels[1] || !panels[2] || !panels[3])
        {
            return;
        }

        MyGUI::Widget* const firstPanel = panels[0]->getWidget();
        MyGUI::Widget* const skillsContainer = firstPanel ? firstPanel->getParent() : 0;
        if (!skillsContainer)
        {
            return;
        }

        // Kenshi normally clips the right-side derived-stat placeholder. The
        // wider root exposes it, so reclaim that unused area for the four
        // actual skill panels instead.
        const int outerRightPadding = 12;
        skillsContainer->setSize(root->getWidth() - skillsContainer->getLeft() - outerRightPadding,
                                 skillsContainer->getHeight());

        const MyGUI::IntCoord firstCoord = firstPanel->getCoord();
        const int innerLeft = firstCoord.left;
        const int panelGap = 12;
        const int panelWidth =
            (skillsContainer->getWidth() - innerLeft - outerRightPadding - panelGap * 3) / 4;

        for (int index = 0; index != 4; ++index)
        {
            panels[index]->setPosition(innerLeft + index * (panelWidth + panelGap), firstCoord.top);
            panels[index]->resize(panelWidth, firstCoord.height);
        }

    }

    void MoveDerivedStatsOutsideWindow(CharacterStatsWindow* window)
    {
        if (!window)
        {
            return;
        }

        MyGUI::Widget* const root = window->getWidget();
        if (!root)
        {
            return;
        }

        const int outsideLeft = root->getWidth() + 8;
        MyGUI::Widget* const derivedTitle = FindWidgetBySuffix(root, "_lbDerivedStats");
        if (derivedTitle)
        {
            derivedTitle->setPosition(outsideLeft, derivedTitle->getTop());
        }

        MyGUI::Widget* const statistics = window->statsDatapanel ?
            window->statsDatapanel->getWidget() : FindWidgetBySuffix(root, "_StatisticsPanel");
        // StatisticsPanel's immediate parent is the otherwise hidden derived
        // stats wrapper. Do not climb further: its next ancestor is the shared
        // Client container, which owns the entire contents of the window.
        MyGUI::Widget* const derivedWrapper = statistics ? statistics->getParent() : 0;
        if (derivedWrapper)
        {
            derivedWrapper->setPosition(outsideLeft, derivedWrapper->getTop());
        }
    }

    void ExpandDescriptionPanel(CharacterStatsWindow* window, MyGUI::Widget* root)
    {
        MyGUI::Widget* const description1 = FindWidgetBySuffix(root, "_Description1Panel");
        MyGUI::Widget* const description2 = FindWidgetBySuffix(root, "_Description2Panel");
        MyGUI::Widget* const container = description1 ? description1->getParent() : 0;
        if (!description2 || !container)
        {
            return;
        }

        const MyGUI::IntCoord leftCoord = description1->getCoord();
        const MyGUI::IntCoord rightCoord = description2->getCoord();
        const int rightPadding = 12;
        container->setSize(root->getWidth() - container->getLeft() - rightPadding,
                           container->getHeight());

        // Description1 uses stretch alignment in Kenshi's layout. Keep the
        // original left panel width so expanding Description2 cannot overlap it.
        description1->setCoord(leftCoord.left, leftCoord.top,
                               leftCoord.width, leftCoord.height);

        const int rightWidth = container->getWidth() - rightCoord.left - 19;
        description2->setCoord(rightCoord.left, rightCoord.top,
                               rightWidth, rightCoord.height);
        if (window->description2Datapanel)
        {
            window->description2Datapanel->resize(rightWidth, rightCoord.height);
        }

        MyGUI::Widget* const descriptionTitle = FindWidgetBySuffix(root, "_lbDescription");
        if (descriptionTitle)
        {
            descriptionTitle->setSize(container->getWidth(), descriptionTitle->getHeight());
        }
    }

    void RightAlignValueColumn(DatapanelGUI* panel, DataPanelLine* line)
    {
        MyGUI::Widget* const panelWidget = panel ? panel->getWidget() : 0;
        if (!panelWidget || !line || !line->w2)
        {
            return;
        }

        const int lineHeight = line->w2->getHeight();
        line->resize(panelWidget->getWidth(), lineHeight);
        const MyGUI::IntCoord current = line->w2->getCoord();
        const int valueLeft = panelWidget->getWidth() -
            kValueColumnWidth - kValueColumnRightPadding;
        const int hoverLeft = line->w1 ? line->w1->getLeft() : current.left;

        // Kenshi attaches the stat-description hover handler to w2, not w1.
        // Give w2 a transparent hit area covering both captions while keeping
        // its effective-stat text in the existing right-hand value column.
        line->w2->setCoord(hoverLeft, current.top,
                           panelWidget->getWidth() - hoverLeft - kValueColumnRightPadding,
                           current.height);
        line->w2->setTextAlign(MyGUI::Align::Right);

        if (line->w1)
        {
            const MyGUI::IntCoord name = line->w1->getCoord();
            const int nameWidth = valueLeft - name.left - kNameColumnRightPadding;
            if (nameWidth > name.width)
            {
                line->w1->setCoord(name.left, name.top, nameWidth, name.height);
            }
        }
    }

    MyGUI::Widget* FindWidgetBySuffix(MyGUI::Widget* widget, const std::string& suffix)
    {
        if (!widget)
        {
            return 0;
        }
        const std::string& name = widget->getName();
        if (name.size() >= suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            return widget;
        }
        const size_t childCount = widget->getChildCount();
        for (size_t index = 0; index < childCount; ++index)
        {
            MyGUI::Widget* const found = FindWidgetBySuffix(widget->getChildAt(index), suffix);
            if (found)
            {
                return found;
            }
        }
        return 0;
    }

    void WidenStatsWindow(CharacterStatsWindow* window)
    {
        if (!window)
        {
            return;
        }

        MyGUI::Widget* const root = window->getWidget();
        if (!root)
        {
            return;
        }

        // Kenshi restores the vanilla geometry whenever it rebuilds a reused
        // window instance. Therefore this must run for every setup call, not
        // only for roots we have not seen before.
        const MyGUI::IntCoord coord = root->getCoord();
        root->setCoord(coord.left, coord.top, ScaleWidth(coord.width), coord.height);
        ExpandSkillPanels(window, root);
        ExpandDescriptionPanel(window, root);
    }

    std::string FormatEffectiveValue(float value)
    {
        // This formats a value already calculated by Kenshi; it does not
        // calculate, round into, or otherwise derive another stat value.
        std::ostringstream output;
        output << std::fixed << std::setprecision(1) << value;
        return output.str();
    }

    bool FindDisplayedStat(
        const std::string& panelKey,
        DataPanelLine* line,
        StatsEnumerated* result)
    {
        // Never dereference static game data by a fixed RVA: that crashed on
        // the supported 1.0.65 executable. Both the row caption and this name
        // are supplied by the game, so this remains valid with localisation.
        const std::string candidates[] = { panelKey, line->keyValue, line->s1 };
        struct LabelMapping
        {
            const char* label;
            StatsEnumerated stat;
        };
        const LabelMapping labelMappings[] =
        {
            { "Dodge", STAT_DODGE },
            { "Melee attack", STAT_MELEE_ATTACK },
            { "Melee defence", STAT_MELEE_DEFENCE },
            { "Precision shooting", STAT_FRIENDLY_FIRE },
            { "Armour smith", STAT_SMITHING_ARMOUR },
            { "Crossbow smith", STAT_SMITHING_BOW },
            { "Engineer", STAT_ENGINEERING },
            { "Field medic", STAT_MEDIC },
            { "Weapon smith", STAT_SMITHING_WEAPON }
        };
        for (int index = 0; index != 3; ++index)
        {
            for (int mapping = 0;
                 mapping != sizeof(labelMappings) / sizeof(labelMappings[0]);
                 ++mapping)
            {
                if (candidates[index] == labelMappings[mapping].label)
                {
                    *result = labelMappings[mapping].stat;
                    return true;
                }
            }
            for (int value = static_cast<int>(STAT_STRENGTH);
                 value < static_cast<int>(STAT_END);
                 ++value)
            {
                const StatsEnumerated stat = static_cast<StatsEnumerated>(value);
                if (candidates[index] == CharStats::getStatName(stat))
                {
                    *result = stat;
                    return true;
                }
            }
        }
        return false;
    }

    float GetEffectiveValue(CharStats* stats, StatsEnumerated displayedStat)
    {
        // The generic stat getter provides the directly stored modified value.
        // Kenshi applies the situational combat effects (equipment,
        // encumbrance, injuries, and combat mode) in these dedicated getters,
        // so use them for the rows they own. No bonus or penalty is calculated
        // here: every returned value is supplied by Kenshi itself.
        switch (displayedStat)
        {
        case STAT_MELEE_ATTACK:
            return stats->getMeleeAttack();
        case STAT_MELEE_DEFENCE:
            return stats->getMeleeDefence(false);
        case STAT_MARTIALARTS:
            return stats->getMeleeAttack_unarmed(true);
        case STAT_DODGE:
            return stats->getDodge(true);
        default:
            return stats->getStat(displayedStat, false);
        }
    }

    void AppendEffectiveValues(CharStats* stats, DatapanelGUI* panel)
    {
        if (!stats || !panel)
        {
            return;
        }

        // Use the stat-screen's own stat id map rather than guessing its
        // captions. It also means translated labels remain supported.
        for (auto category = panel->content.begin(); category != panel->content.end(); ++category)
        {
            for (auto entry = category->second.begin(); entry != category->second.end(); ++entry)
            {
                DataPanelLine* const line = entry->second;
                if (!line || !line->w2)
                {
                    continue;
                }

                StatsEnumerated displayedStat = STAT_NONE;
                if (!FindDisplayedStat(entry->first, line, &displayedStat))
                {
                    continue;
                }

                // false explicitly asks Kenshi for its effective, modified
                // value. The existing caption is vanilla's unmodified value.
                const float effectiveValue = GetEffectiveValue(stats, displayedStat);
                const std::string caption = line->s2 +
                    " (" + FormatEffectiveValue(effectiveValue) + ")";
                line->w2->setCaptionWithReplacing(caption);
                RightAlignValueColumn(panel, line);
            }
        }

    }

    void RefreshEffectiveValues(CharacterStatsWindow* window)
    {
        if (!window || !window->character)
        {
            return;
        }

        CharStats* const stats = window->character->getStats();
        AppendEffectiveValues(stats, window->attributesDatapanel);
        AppendEffectiveValues(stats, window->skills1Datapanel);
        AppendEffectiveValues(stats, window->skills2Datapanel);
        AppendEffectiveValues(stats, window->skills3Datapanel);
        AppendEffectiveValues(stats, window->skills4Datapanel);
        AppendEffectiveValues(stats, window->statsDatapanel);
        MoveDerivedStatsOutsideWindow(window);
    }

    void __cdecl UpdateStatsDetour(CharacterStatsWindow* window)
    {
        g_originalUpdateStats(window);
        RefreshEffectiveValues(window);
    }

    void __cdecl UpdateWindowDetour(CharacterStatsWindow* window)
    {
        g_originalUpdateWindow(window);
        RefreshEffectiveValues(window);
    }

    void __cdecl SetupStatsDetour(CharacterStatsWindow* window)
    {
        g_originalSetupStats(window);
        WidenStatsWindow(window);
        MoveDerivedStatsOutsideWindow(window);
    }
}

namespace ShowEffectiveStats
{
    bool Install()
    {
        if (g_originalUpdateStats)
        {
            return true;
        }

        const intptr_t target = KenshiLib::GetRealAddress(&CharacterStatsWindow::updateStats);
        if (!target || KenshiLib::AddHook(target,
                                         reinterpret_cast<void*>(&UpdateStatsDetour),
                                         &g_originalUpdateStats) != KenshiLib::SUCCESS ||
            !g_originalUpdateStats)
        {
            g_originalUpdateStats = 0;
            Log("stat screen hook=failed feature=disabled");
            return false;
        }

        Log("stat screen hook=installed target=CharacterStatsWindow::updateStats source=CharStats::getStat(false)");

        // `update` is virtual, so its C++ member pointer is a dispatch thunk
        // in this DLL rather than an address in kenshi_x64.exe. KenshiLib also
        // exposes the game's non-virtual entry point for the same method.
        const intptr_t updateTarget = KenshiLib::GetRealAddress(&CharacterStatsWindow::_NV_update);
        if (!updateTarget || KenshiLib::AddHook(updateTarget,
                                                reinterpret_cast<void*>(&UpdateWindowDetour),
                                                &g_originalUpdateWindow) != KenshiLib::SUCCESS ||
            !g_originalUpdateWindow)
        {
            g_originalUpdateWindow = 0;
            Log("stat screen live-update hook=failed feature=disabled");
        }
        else
        {
            Log("stat screen live-update hook=installed target=CharacterStatsWindow::_NV_update");
        }

        const intptr_t setupTarget = KenshiLib::GetRealAddress(&CharacterStatsWindow::setupStats);
        if (!setupTarget || KenshiLib::AddHook(setupTarget,
                                              reinterpret_cast<void*>(&SetupStatsDetour),
                                              &g_originalSetupStats) != KenshiLib::SUCCESS ||
            !g_originalSetupStats)
        {
            g_originalSetupStats = 0;
            Log("stat screen resize hook=failed feature=disabled");
            return true;
        }

        Log("stat screen resize hook=installed width_scale=1.24");
        return true;
    }
}
