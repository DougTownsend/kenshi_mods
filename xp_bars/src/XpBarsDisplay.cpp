#include <core/Functions.h>
#include <kenshi/CharStats.h>
#include <kenshi/Character.h>
#include <kenshi/gui/ForgottenGUI.h>

#define private public
#define protected public
#include <kenshi/gui/DatapanelGUI.h>
#include <kenshi/gui/DataPanelLine.h>
#include <kenshi/InputHandler.h>
#include <kenshi/gui/CharacterStatsWindow.h>
#undef private
#undef protected

#include <mygui/MyGUI.h>

#include <cmath>
#include <algorithm>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "XpBarsDisplay.h"

namespace
{
    typedef void (__cdecl *SetupStatsFn)(CharacterStatsWindow*);
    typedef void (__cdecl *UpdateWindowFn)(CharacterStatsWindow*);
    typedef void (__cdecl *AutoChangeFn)(CharacterStatsWindow*, const hand&);
    typedef void (__cdecl *SelectedObjectsChangedFn)(ForgottenGUI*);
    typedef void (__cdecl *ForgottenGuiUpdateFn)(ForgottenGUI*);
    typedef void (__cdecl *CloseAllWindowsFn)(ForgottenGUI*);
    typedef void (__cdecl *KeyDownFn)(InputHandler*, OIS::KeyCode);
    SetupStatsFn g_originalSetupStats = 0;
    UpdateWindowFn g_originalUpdateWindow = 0;
    AutoChangeFn g_originalAutoChange = 0;
    SelectedObjectsChangedFn g_originalSelectedObjectsChanged = 0;
    ForgottenGuiUpdateFn g_originalForgottenGuiUpdate = 0;
    CloseAllWindowsFn g_originalCloseAllWindows = 0;
    KeyDownFn g_originalKeyDown = 0;

    struct TrackedStat
    {
        StatsEnumerated id;
        std::string name;
        std::string group;
        int column;
    };

    // Character pointers are stable while a character is loaded.  Keeping the
    // selection keyed this way makes selections independent when focus changes.
    typedef std::set<int> TrackedSet;
    std::map<Character*, TrackedSet> g_trackedByCharacter;
    std::map<MyGUI::Widget*, int> g_statBySelector;
    std::vector<TrackedStat> g_availableStats;

    CharacterStatsWindow* g_statsWindow = 0;
    MyGUI::Button* g_openButton = 0;
    MyGUI::Widget* g_buttonRoot = 0;
    MyGUI::Window* g_xpWindow = 0;
    MyGUI::Window* g_trackedStatsWindow = 0;
    MyGUI::Widget* g_selectorPanel = 0;
    MyGUI::Widget* g_xpRowsPanel = 0;
    Character* g_selectorCharacter = 0;
    bool g_trackedStatsVisible = false;
    Character* g_selectedCharacter = 0;
    int g_globalRefreshFrames = 0;
    MyGUI::IntPoint g_trackedButtonDragMouse;
    MyGUI::IntPoint g_trackedButtonDragWindow;
    bool g_trackedButtonDragging = false;
    bool g_trackedButtonDragged = false;
    bool g_keepXpWindowOpen = false;
    bool g_xpWindowDimmedForPause = false;
    std::vector<MyGUI::Widget*> g_xpRows;
    std::vector<MyGUI::Widget*> g_selectorRows;

    const int kWindowLeft = 40;
    const int kWindowTop = 60;
    const int kWindowWidth = 630;
    const int kHeaderHeight = 0;
    const int kRowHeight = 30;
    const int kSelectorWindowChromeHeight = 54;
    const int kPanelBorder = 10;
    const int kSelectorColumns = 5;
    const int kSelectorRowHeight = 24;
    const int kContentPadding = 22;
    const int kGridPadding = 14;
    const int kXpTopPadding = 8;
    // Inner tan margin below the last XP row.
    const int kXpBottomPadding = 4;
    const int kMinimumSelectorColumnWidth = 160;
    const int kApproximateMediumCharacterWidth = 10;
    int g_selectorColumnWidths[kSelectorColumns] = { 0, 0, 0, 0, 0 };
    std::string g_sectionLabelSkin = "Kenshi_EditBoxPaintedText";
    std::string g_sectionLabelFont = "Kenshi_PaintedTextFont_Small";
    MyGUI::Colour g_sectionLabelColour(0.286275f, 0.149020f, 0.125490f);


    Character* SelectedCharacter()
    {
        return g_selectedCharacter ? g_selectedCharacter :
            (g_statsWindow ? g_statsWindow->character : 0);
    }

    void DestroyRows(std::vector<MyGUI::Widget*>& rows)
    {
        for (std::vector<MyGUI::Widget*>::iterator it = rows.begin(); it != rows.end(); ++it)
        {
            if (*it)
            {
                MyGUI::Gui::getInstance().destroyWidget(*it);
            }
        }
        rows.clear();
    }

    void CloseXpWindows()
    {
        if (g_xpWindow)
        {
            g_xpWindow->setVisible(false);
        }
        if (g_trackedStatsWindow)
        {
            g_trackedStatsWindow->setVisible(false);
        }
        g_trackedStatsVisible = false;
    }

    void CloseXpWindowsForEscape()
    {
        if (!g_keepXpWindowOpen && g_xpWindow)
        {
            g_xpWindow->setVisible(false);
        }
        if (g_trackedStatsWindow)
        {
            g_trackedStatsWindow->setVisible(false);
        }
        g_trackedStatsVisible = false;
    }

    void OnKeepOpenClicked(MyGUI::Widget* sender)
    {
        if (g_trackedButtonDragged)
        {
            g_trackedButtonDragged = false;
            return;
        }
        g_keepXpWindowOpen = !g_keepXpWindowOpen;
        MyGUI::Button* const button = sender->castType<MyGUI::Button>(false);
        if (button)
        {
            button->setStateSelected(g_keepXpWindowOpen);
        }
    }

    void OnWindowKeyPressed(MyGUI::Widget*, MyGUI::KeyCode key, MyGUI::Char)
    {
        if (key == MyGUI::KeyCode::Escape)
        {
            CloseXpWindowsForEscape();
        }
    }

    void OnTrackedButtonPressed(MyGUI::Widget*, int, int, MyGUI::MouseButton)
    {
        if (!g_xpWindow)
        {
            return;
        }
        g_trackedButtonDragMouse = MyGUI::InputManager::getInstance().getMousePosition();
        g_trackedButtonDragWindow = g_xpWindow->getPosition();
        g_trackedButtonDragging = true;
        g_trackedButtonDragged = false;
    }

    void OnTrackedButtonDragged(MyGUI::Widget*, int, int, MyGUI::MouseButton)
    {
        if (!g_trackedButtonDragging || !g_xpWindow)
        {
            return;
        }
        const MyGUI::IntPoint mouse = MyGUI::InputManager::getInstance().getMousePosition();
        const int deltaX = mouse.left - g_trackedButtonDragMouse.left;
        const int deltaY = mouse.top - g_trackedButtonDragMouse.top;
        if (deltaX || deltaY)
        {
            g_trackedButtonDragged = true;
            g_xpWindow->setPosition(g_trackedButtonDragWindow.left + deltaX,
                                    g_trackedButtonDragWindow.top + deltaY);
        }
    }

    void OnTrackedButtonReleased(MyGUI::Widget*, int, int, MyGUI::MouseButton)
    {
        g_trackedButtonDragging = false;
    }

    void __cdecl KeyDownDetour(InputHandler* input, OIS::KeyCode key)
    {
        g_originalKeyDown(input, key);
        if (key == OIS::KC_ESCAPE)
        {
            // InputHandler receives Escape before any particular GUI widget is
            // focused, matching Kenshi's global window-close behaviour.
            CloseXpWindowsForEscape();
        }
    }

    void __cdecl CloseAllWindowsDetour(ForgottenGUI* gui)
    {
        g_originalCloseAllWindows(gui);
        // Use Kenshi's own close-all path so Escape behaves identically even
        // while a window title bar has MyGUI keyboard focus.
        CloseXpWindowsForEscape();
    }

    void UpdateSelectorColumnWidths()
    {
        // The game localises stat names, so each column is sized from the
        // longest actual caption it will display rather than a fixed width.
        for (int column = 0; column < kSelectorColumns; ++column)
        {
            size_t longestName = 0;
            for (std::vector<TrackedStat>::const_iterator it = g_availableStats.begin();
                 it != g_availableStats.end(); ++it)
            {
                if (it->column == column && it->name.size() > longestName)
                {
                    longestName = it->name.size();
                }
            }
            const int requiredWidth = kGridPadding * 2 + 32 +
                static_cast<int>(longestName) * kApproximateMediumCharacterWidth;
            g_selectorColumnWidths[column] = requiredWidth > kMinimumSelectorColumnWidth ?
                requiredWidth : kMinimumSelectorColumnWidth;
        }
    }

    void BuildAvailableStats()
    {
        if (!g_availableStats.empty())
        {
            return;
        }

        struct StatLayout
        {
            StatsEnumerated id;
            const char* group;
            int column;
        };
        // Mirrors Kenshi_StatsWindow: attributes at left, then the four skill
        // columns. This explicit public enum mapping avoids reading private
        // game static containers, which KenshiLib does not export.
        const StatLayout layout[] =
        {
            { STAT_STRENGTH, "Attributes", 0 }, { STAT_TOUGHNESS, "Attributes", 0 },
            { STAT_DEXTERITY, "Attributes", 0 }, { STAT_PERCEPTION, "Attributes", 0 },
            { STAT_KATANAS, "-Weapons-", 1 }, { STAT_SABRES, "-Weapons-", 1 },
            { STAT_HACKERS, "-Weapons-", 1 }, { STAT_HEAVYWEAPONS, "-Weapons-", 1 },
            { STAT_BLUNT, "-Weapons-", 1 }, { STAT_POLEARMS, "-Weapons-", 1 },
            { STAT_MELEE_ATTACK, "-Combat-", 2 }, { STAT_MELEE_DEFENCE, "-Combat-", 2 },
            { STAT_DODGE, "-Combat-", 2 }, { STAT_MARTIALARTS, "-Combat-", 2 },
            { STAT_TURRETS, "-Ranged-", 2 }, { STAT_CROSSBOWS, "-Ranged-", 2 },
            { STAT_FRIENDLY_FIRE, "-Ranged-", 2 },
            { STAT_STEALTH, "-Thievery-", 3 }, { STAT_LOCKPICKING, "-Thievery-", 3 },
            { STAT_THIEVING, "-Thievery-", 3 }, { STAT_ASSASSINATION, "-Thievery-", 3 },
            { STAT_ATHLETICS, "-Athletics-", 3 }, { STAT_SWIMMING, "-Athletics-", 3 },
            { STAT_MEDIC, "-Sciences-", 4 }, { STAT_ENGINEERING, "-Sciences-", 4 },
            { STAT_ROBOTICS, "-Sciences-", 4 }, { STAT_SCIENCE, "-Sciences-", 4 },
            { STAT_SMITHING_WEAPON, "-Trades-", 4 }, { STAT_SMITHING_ARMOUR, "-Trades-", 4 },
            { STAT_SMITHING_BOW, "-Trades-", 4 }, { STAT_LABOURING, "-Trades-", 4 },
            { STAT_FARMING, "-Trades-", 4 }, { STAT_COOKING, "-Trades-", 4 }
        };
        for (size_t index = 0; index < sizeof(layout) / sizeof(layout[0]); ++index)
        {
            const std::string name = CharStats::getStatName(layout[index].id);
            if (name.empty())
            {
                continue;
            }
            TrackedStat entry;
            entry.id = layout[index].id;
            entry.name = name;
            entry.group = layout[index].group;
            entry.column = layout[index].column;
            g_availableStats.push_back(entry);
        }

        UpdateSelectorColumnWidths();
    }

    bool FindVanillaDisplayedStat(const std::string& text, StatsEnumerated* result)
    {
        struct LabelMapping { const char* label; StatsEnumerated stat; };
        const LabelMapping specialLabels[] =
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
        for (size_t index = 0; index < sizeof(specialLabels) / sizeof(specialLabels[0]); ++index)
        {
            if (text == specialLabels[index].label)
            {
                *result = specialLabels[index].stat;
                return true;
            }
        }
        for (int value = static_cast<int>(STAT_STRENGTH);
             value < static_cast<int>(STAT_END); ++value)
        {
            const StatsEnumerated stat = static_cast<StatsEnumerated>(value);
            if (text == CharStats::getStatName(stat))
            {
                *result = stat;
                return true;
            }
        }
        return false;
    }

    void ReadVanillaStatPresentation(CharacterStatsWindow* window)
    {
        if (!window)
        {
            return;
        }
        DatapanelGUI* const panels[] =
        {
            window->attributesDatapanel, window->skills1Datapanel,
            window->skills2Datapanel, window->skills3Datapanel, window->skills4Datapanel
        };
        for (size_t panelIndex = 0; panelIndex < sizeof(panels) / sizeof(panels[0]); ++panelIndex)
        {
            DatapanelGUI* const panel = panels[panelIndex];
            if (!panel)
            {
                continue;
            }
            for (auto category = panel->content.begin(); category != panel->content.end(); ++category)
            {
                for (auto entry = category->second.begin(); entry != category->second.end(); ++entry)
                {
                    DataPanelLine* const line = entry->second;
                    if (!line)
                    {
                        continue;
                    }
                    if (line->w1 && line->s1.size() > 2 && line->s1[0] == '-')
                    {
                        g_sectionLabelSkin = line->skinW1;
                        g_sectionLabelFont = line->w1->getFontName();
                        g_sectionLabelColour = line->w1->getTextColour();
                    }
                    StatsEnumerated stat = STAT_NONE;
                    if (!FindVanillaDisplayedStat(line->s1, &stat))
                    {
                        continue;
                    }
                    for (std::vector<TrackedStat>::iterator available = g_availableStats.begin();
                         available != g_availableStats.end(); ++available)
                    {
                        if (available->id == stat)
                        {
                            available->name = line->s1;
                            break;
                        }
                    }
                }
            }
        }
        UpdateSelectorColumnWidths();
    }

    int SelectorColumnLeft(int column)
    {
        int left = 0;
        for (int index = 0; index < column; ++index)
        {
            left += g_selectorColumnWidths[index];
        }
        return left;
    }

    std::string FormatLevel(float level)
    {
        std::ostringstream output;
        output << static_cast<int>(std::floor(level));
        return output.str();
    }

    std::string FormatProgressPercent(float level)
    {
        std::ostringstream output;
        output << std::fixed << std::setprecision(1)
               << (level - std::floor(level)) * 100.0f << "%";
        return output.str();
    }

    void RefreshXpRows()
    {
        if (!g_xpWindow)
        {
            return;
        }

        DestroyRows(g_xpRows);
        const Character* const character = SelectedCharacter();
        if (!character)
        {
            return;
        }
        CharStats* const stats = const_cast<Character*>(character)->getStats();
        if (!stats)
        {
            return;
        }

        const TrackedSet& tracked = g_trackedByCharacter[const_cast<Character*>(character)];
        size_t longestNameLength = 0;
        for (std::vector<TrackedStat>::const_iterator it = g_availableStats.begin();
             it != g_availableStats.end(); ++it)
        {
            if (tracked.find(static_cast<int>(it->id)) != tracked.end() &&
                it->name.size() > longestNameLength)
            {
                longestNameLength = it->name.size();
            }
        }
        int nameColumnWidth = static_cast<int>(longestNameLength) * 10 + 16;
        if (nameColumnWidth < 110) nameColumnWidth = 110;
        if (nameColumnWidth > 230) nameColumnWidth = 230;
        const int valueLeft = kContentPadding + nameColumnWidth + 8;
        const int percentLeft = valueLeft + 46;
        const int progressLeft = percentLeft + 62;
        const int progressWidth = g_xpRowsPanel->getWidth() - progressLeft - kContentPadding;
        int row = 0;
        for (std::vector<TrackedStat>::const_iterator it = g_availableStats.begin();
             it != g_availableStats.end(); ++it)
        {
            if (tracked.find(static_cast<int>(it->id)) == tracked.end())
            {
                continue;
            }

            MyGUI::TextBox* const name = g_xpRowsPanel->createWidget<MyGUI::TextBox>(
                "Kenshi_TextboxStandardText", MyGUI::IntCoord(kContentPadding,
                    kXpTopPadding + row * kRowHeight, nameColumnWidth, kRowHeight),
                MyGUI::Align::Default, "");
            name->setCaption(it->name);

            const float rawLevel = stats->getStat(it->id, true);
            MyGUI::TextBox* const level = g_xpRowsPanel->createWidget<MyGUI::TextBox>(
                "Kenshi_TextboxStandardText", MyGUI::IntCoord(valueLeft,
                    kXpTopPadding + row * kRowHeight, 40, kRowHeight),
                MyGUI::Align::Default, "");
            level->setCaption(FormatLevel(rawLevel));

            MyGUI::TextBox* const percent = g_xpRowsPanel->createWidget<MyGUI::TextBox>(
                "Kenshi_TextboxStandardText", MyGUI::IntCoord(percentLeft,
                    kXpTopPadding + row * kRowHeight, 58, kRowHeight),
                MyGUI::Align::Default, "");
            percent->setCaption(FormatProgressPercent(rawLevel));

            // Kenshi stores a stat level as a float.  Its fractional component
            // is its progress toward the next displayed whole level.
            MyGUI::ProgressBar* const progress = g_xpRowsPanel->createWidget<MyGUI::ProgressBar>(
                "Kenshi_ProgressBarFill", MyGUI::IntCoord(progressLeft,
                    kXpTopPadding + row * kRowHeight + 4, progressWidth, 16),
                MyGUI::Align::Default, "");
            progress->setProgressRange(1000);
            progress->setProgressPosition(static_cast<size_t>(
                (rawLevel - std::floor(rawLevel)) * 1000.0f + 0.5f));

            g_xpRows.push_back(name);
            g_xpRows.push_back(level);
            g_xpRows.push_back(percent);
            g_xpRows.push_back(progress);
            ++row;
        }

        // Preserve the coordinate chosen by the player.  The Kenshi window
        // skin owns the header and its 3px lower frame; read its actual client
        // height instead of estimating chrome or imposing a minimum height.
        const MyGUI::IntCoord coord = g_xpWindow->getCoord();
        const int panelHeight = row * kRowHeight + kXpTopPadding + kXpBottomPadding;
        MyGUI::Widget* const client = g_xpWindow->getClientWidget();
        const int chromeHeight = coord.height - client->getHeight();
        g_xpWindow->setCoord(coord.left, coord.top, coord.width,
                             chromeHeight + panelHeight);
        g_xpRowsPanel->setCoord(kPanelBorder, kHeaderHeight, coord.width - kPanelBorder * 2,
                                panelHeight);
        g_xpRowsPanel->setVisible(row != 0);
    }

    void ShowTrackedStats(MyGUI::Widget* sender);

    void OnTrackedButtonClicked(MyGUI::Widget*)
    {
        if (g_trackedButtonDragged)
        {
            g_trackedButtonDragged = false;
            return;
        }
        if (g_trackedStatsVisible)
        {
            g_trackedStatsWindow->setVisible(false);
            g_trackedStatsVisible = false;
            return;
        }
        ShowTrackedStats(0);
    }

    void OnWindowButtonPressed(MyGUI::Window* sender, const std::string&)
    {
        // MyGUI only reports title-bar presses; hide either of our windows on
        // its close button rather than relying on a layout-specific default.
        if (sender == g_xpWindow || sender == g_trackedStatsWindow)
        {
            sender->setVisible(false);
            if (sender == g_trackedStatsWindow)
            {
                g_trackedStatsVisible = false;
            }
        }
    }

    void OnOpenClicked(MyGUI::Widget*)
    {
        if (g_xpWindow && g_xpWindow->getVisible())
        {
            CloseXpWindows();
            return;
        }
        if (!g_xpWindow)
        {
            g_xpWindow = MyGUI::Gui::getInstance().createWidget<MyGUI::Window>(
                "Kenshi_WindowCXP", MyGUI::IntCoord(kWindowLeft, kWindowTop, kWindowWidth, 90),
                MyGUI::Align::Default, "Window", "XpBars_Window");
            g_xpWindow->setCaption("XP BARS");
            MyGUI::TextBox* const caption = g_xpWindow->getCaptionWidget();
            caption->setTextAlign(MyGUI::Align::Left);
            caption->setCoord(16, caption->getTop(), 105, caption->getHeight());
            g_xpWindow->eventWindowButtonPressed +=
                MyGUI::newDelegate(&OnWindowButtonPressed);
            g_xpWindow->eventKeyButtonPressed += MyGUI::newDelegate(&OnWindowKeyPressed);
            // The standard text-box skin has 60px of non-stretching vertical
            // border art.  A one-row XP panel is shorter than that, which made
            // its top and bottom slices overlap.  The flat tan variant has
            // shallow 8px/9px caps and can stretch to this panel's real size.
            g_xpRowsPanel = g_xpWindow->createWidget<MyGUI::Widget>(
                "Kenshi_GenericTextBoxFlatSkin", MyGUI::IntCoord(kPanelBorder, kHeaderHeight,
                    kWindowWidth - kPanelBorder * 2, kContentPadding),
                MyGUI::Align::Default, "XpBars_RowsPanel");
            // The title strip is a sibling of the client area.  Parent the
            // button there so it is not clipped by the client-area bounds.
            MyGUI::Widget* const titleBar = caption->getParent();
            // The caption widget otherwise consumes mouse events over the
            // title strip.  This transparent strip makes all free title-bar
            // space draggable while leaving the native close controls alone.
            MyGUI::Widget* const dragStrip = titleBar->createWidget<MyGUI::Widget>(
                "PanelEmpty", MyGUI::IntCoord(0, 0, titleBar->getWidth(), titleBar->getHeight()),
                MyGUI::Align::Default, "XpBars_TitleDragStrip");
            dragStrip->eventMouseButtonPressed += MyGUI::newDelegate(&OnTrackedButtonPressed);
            dragStrip->eventMouseDrag += MyGUI::newDelegate(&OnTrackedButtonDragged);
            dragStrip->eventMouseButtonReleased += MyGUI::newDelegate(&OnTrackedButtonReleased);

            // Do not size title-bar controls in screen pixels.  Kenshi has
            // already created its pin/O control using the active UI scale, so
            // use that live control as the layout unit for ours as well.
            // This also keeps their vertical alignment identical to the
            // native O and close buttons at every resolution.
            MyGUI::Button* pinControl = 0;
            const size_t titleChildCount = titleBar->getChildCount();
            for (size_t index = 0; index < titleChildCount; ++index)
            {
                MyGUI::Button* const child = titleBar->getChildAt(index)->castType<MyGUI::Button>(false);
                if (child && (!pinControl || child->getLeft() < pinControl->getLeft()))
                {
                    pinControl = child;
                }
            }
            const int nativeControlHeight = pinControl ? pinControl->getHeight() :
                titleBar->getHeight() - 6;
            // Leave breathing room above and below the custom controls.  The
            // 13/16 proportion makes a standard 32px O control produce the
            // TickButton1 skin's native 26px height, so its left indicator is
            // rendered at its intended (circular) aspect ratio.
            const int controlHeight = (nativeControlHeight * 13 + 8) / 16;
            const int controlTop = (pinControl ? pinControl->getTop() : 0) +
                (nativeControlHeight - controlHeight) / 2;
            const int scaleGap = pinControl ? (pinControl->getWidth() + 2) / 4 :
                controlHeight / 3;
            MyGUI::Button* const tracked = titleBar->createWidget<MyGUI::Button>(
                "Kenshi_Button1", MyGUI::IntCoord(0, controlTop, 1, controlHeight),
                MyGUI::Align::Default, "XpBars_TrackedStats");
            tracked->setCaption("Tracked Stats");
            MyGUI::Button* const keepOpen = titleBar->createWidget<MyGUI::Button>(
                "Kenshi_TickButton1", MyGUI::IntCoord(0, controlTop, 1, controlHeight),
                MyGUI::Align::Default, "XpBars_KeepOpen");
            keepOpen->setCaption("Keep Open");

            // These widths come only from the captions actually rendered by
            // Kenshi's current font.  Keep Open additionally reserves its
            // skin's 20px indicator band.  The remaining values are just the
            // same small horizontal padding on either side of a label.
            const int labelPadding = 6;
            const int trackedWidth = tracked->getTextSize().width + labelPadding * 2;
            const int keepOpenWidth = 20 + keepOpen->getTextSize().width + labelPadding * 2;
            const int betweenButtons = labelPadding;
            const int keepOpenRight = (pinControl ? pinControl->getLeft() : titleBar->getWidth()) - scaleGap;
            const int keepOpenLeft = keepOpenRight - keepOpenWidth;
            const int trackedLeft = keepOpenLeft - betweenButtons - trackedWidth;
            tracked->setCoord(trackedLeft, controlTop, trackedWidth, controlHeight);
            keepOpen->setCoord(keepOpenLeft, controlTop, keepOpenWidth, controlHeight);
            dragStrip->setSize(trackedLeft, titleBar->getHeight());
            tracked->eventMouseButtonClick += MyGUI::newDelegate(&OnTrackedButtonClicked);
            keepOpen->setStateSelected(g_keepXpWindowOpen);
            keepOpen->eventMouseButtonClick += MyGUI::newDelegate(&OnKeepOpenClicked);
        }
        g_xpWindow->setVisible(true);
        MyGUI::LayerManager::getInstance().upLayerItem(g_xpWindow);
        RefreshXpRows();
    }

    void OnSelectorClicked(MyGUI::Widget* sender)
    {
        Character* const character = SelectedCharacter();
        std::map<MyGUI::Widget*, int>::const_iterator found = g_statBySelector.find(sender);
        if (!character || found == g_statBySelector.end())
        {
            return;
        }

        TrackedSet& tracked = g_trackedByCharacter[character];
        if (tracked.find(found->second) == tracked.end())
        {
            tracked.insert(found->second);
        }
        else
        {
            tracked.erase(found->second);
        }
        MyGUI::Button* const checkbox = sender->castType<MyGUI::Button>(false);
        if (checkbox)
        {
            checkbox->setStateSelected(tracked.find(found->second) != tracked.end());
        }
        RefreshXpRows();
    }

    void OnDeselectAllClicked(MyGUI::Widget*)
    {
        Character* const character = SelectedCharacter();
        if (!character)
        {
            return;
        }
        g_trackedByCharacter[character].clear();
        ShowTrackedStats(0);
        RefreshXpRows();
    }

    void ShowTrackedStats(MyGUI::Widget*)
    {
        if (!g_trackedStatsWindow)
        {
            int rowCounts[kSelectorColumns] = { 0, 0, 0, 0, 0 };
            std::string previousGroups[kSelectorColumns];
            for (size_t index = 0; index < g_availableStats.size(); ++index)
            {
                const TrackedStat& entry = g_availableStats[index];
                if (previousGroups[entry.column] != entry.group)
                {
                    rowCounts[entry.column] += 1;
                    previousGroups[entry.column] = entry.group;
                }
                rowCounts[entry.column] += 1;
            }
            int rowCount = rowCounts[0];
            for (int column = 1; column < kSelectorColumns; ++column)
            {
                if (rowCounts[column] > rowCount)
                {
                    rowCount = rowCounts[column];
                }
            }
            int selectorWidth = kPanelBorder * 2;
            for (int column = 0; column < kSelectorColumns; ++column)
            {
                selectorWidth += g_selectorColumnWidths[column];
            }
            const int selectorHeight = kSelectorWindowChromeHeight + 34 +
                rowCount * kSelectorRowHeight + kGridPadding * 2 + kPanelBorder;
            g_trackedStatsWindow = MyGUI::Gui::getInstance().createWidget<MyGUI::Window>(
                "Kenshi_WindowCXP", MyGUI::IntCoord(kPanelBorder, kPanelBorder,
                    selectorWidth, selectorHeight),
                MyGUI::Align::Default, "Window", "XpBars_TrackedStatsWindow");
            g_trackedStatsWindow->setCaption("TRACKED STATS");
            g_trackedStatsWindow->eventWindowButtonPressed +=
                MyGUI::newDelegate(&OnWindowButtonPressed);
            g_trackedStatsWindow->eventKeyButtonPressed +=
                MyGUI::newDelegate(&OnWindowKeyPressed);
            g_selectorPanel = g_trackedStatsWindow->createWidget<MyGUI::Widget>(
                "Kenshi_GenericTextBoxSkin", MyGUI::IntCoord(kPanelBorder, 34,
                    selectorWidth - kPanelBorder * 2,
                    rowCount * kSelectorRowHeight + kGridPadding * 2),
                MyGUI::Align::Default, "XpBars_TrackedStatsGrid");
            MyGUI::Button* const deselect = g_trackedStatsWindow->createWidget<MyGUI::Button>(
                "Kenshi_Button1", MyGUI::IntCoord(selectorWidth - 175, 6, 155, 24),
                MyGUI::Align::Default, "XpBars_DeselectAll");
            deselect->setCaption("Deselect All");
            deselect->eventMouseButtonClick += MyGUI::newDelegate(&OnDeselectAllClicked);
        }
        g_trackedStatsWindow->setVisible(true);
        MyGUI::LayerManager::getInstance().upLayerItem(g_trackedStatsWindow);
        g_trackedStatsVisible = true;
        DestroyRows(g_selectorRows);
        g_statBySelector.clear();

        Character* const character = SelectedCharacter();
        const TrackedSet empty;
        const TrackedSet& tracked = character ? g_trackedByCharacter[character] : empty;
        g_selectorCharacter = character;
        int nextTop[kSelectorColumns] =
        {
            kGridPadding, kGridPadding, kGridPadding, kGridPadding, kGridPadding
        };
        std::string previousGroups[kSelectorColumns];
        for (size_t index = 0; index < g_availableStats.size(); ++index)
        {
            const TrackedStat& entry = g_availableStats[index];
            const int column = entry.column;
            if (previousGroups[column] != entry.group)
            {
                MyGUI::TextBox* const heading = g_selectorPanel->createWidget<MyGUI::TextBox>(
                    "Kenshi_TextboxPaintedText", MyGUI::IntCoord(SelectorColumnLeft(column) +
                        kGridPadding, nextTop[column], g_selectorColumnWidths[column] - kGridPadding * 2, 22),
                    MyGUI::Align::Default, "");
                heading->setCaption(entry.group);
                heading->setTextAlign(MyGUI::Align::HCenter);
                heading->setFontName(g_sectionLabelFont);
                heading->setTextColour(g_sectionLabelColour);
                nextTop[column] += 22;
                previousGroups[column] = entry.group;
            }
            const int left = SelectorColumnLeft(column) + kGridPadding;
            const int top = nextTop[column];
            MyGUI::Button* const checkbox = g_selectorPanel->createWidget<MyGUI::Button>(
                "Kenshi_TickBoxSkin", MyGUI::IntCoord(left, top, 28, kSelectorRowHeight),
                MyGUI::Align::Default, "");
            checkbox->setStateSelected(
                tracked.find(static_cast<int>(g_availableStats[index].id)) != tracked.end());
            checkbox->eventMouseButtonClick += MyGUI::newDelegate(&OnSelectorClicked);
            MyGUI::TextBox* const label = g_selectorPanel->createWidget<MyGUI::TextBox>(
                "Kenshi_TextboxStandardText", MyGUI::IntCoord(left + 32, top + 2,
                    g_selectorColumnWidths[column] - kGridPadding * 2 - 32, kSelectorRowHeight - 2),
                MyGUI::Align::Default, "");
            label->setCaption(g_availableStats[index].name);
            g_statBySelector[checkbox] = static_cast<int>(g_availableStats[index].id);
            g_selectorRows.push_back(checkbox);
            g_selectorRows.push_back(label);
            nextTop[column] += kSelectorRowHeight;
        }
    }

    void AddOpenButton(CharacterStatsWindow* window)
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
        if (g_openButton && g_buttonRoot == root)
        {
            return;
        }

        // A reopened Stats screen can receive a new layout root.  Its old
        // child widgets are destroyed by MyGUI with that root, so do not carry
        // the old pointer into the replacement layout.
        g_openButton = 0;
        g_buttonRoot = root;
        int left = 10;
        int top = 235;
        MyGUI::Widget* const attributes = window->attributesDatapanel ?
            window->attributesDatapanel->getWidget() : 0;
        MyGUI::Widget* const attributesFrame = attributes ? attributes->getParent() : 0;
        if (attributesFrame)
        {
            left = attributesFrame->getLeft();
            top = attributesFrame->getTop() + attributesFrame->getHeight() + 8;
        }
        g_openButton = root->createWidget<MyGUI::Button>(
            "Kenshi_Button1", MyGUI::IntCoord(left, top, 120, 24),
            MyGUI::Align::Default, "XpBars_OpenButton");
        g_openButton->setCaption("XP Bars");
        g_openButton->eventMouseButtonClick += MyGUI::newDelegate(&OnOpenClicked);
    }

    void __cdecl SetupStatsDetour(CharacterStatsWindow* window)
    {
        g_originalSetupStats(window);
        g_statsWindow = window;
        BuildAvailableStats();
        ReadVanillaStatPresentation(window);
        AddOpenButton(window);
    }

    void __cdecl UpdateWindowDetour(CharacterStatsWindow* window)
    {
        g_originalUpdateWindow(window);
        g_statsWindow = window;
        BuildAvailableStats();
        ReadVanillaStatPresentation(window);
        AddOpenButton(window);
        if (g_xpWindow)
        {
            RefreshXpRows();
        }
    }

    void __cdecl AutoChangeSelectedObjectDetour(CharacterStatsWindow* window, const hand& object)
    {
        g_originalAutoChange(window, object);
        g_statsWindow = window;
        if (g_xpWindow)
        {
            RefreshXpRows();
        }
        if (g_trackedStatsVisible && g_selectorCharacter != SelectedCharacter())
        {
            ShowTrackedStats(0);
        }
    }

    void __cdecl SelectedObjectsChangedDetour(ForgottenGUI* gui)
    {
        g_originalSelectedObjectsChanged(gui);
        // This notification fires for portrait/squad focus changes even when
        // the character Stats window is closed.  It is the authoritative
        // source for the character currently selected by the player.
        g_selectedCharacter = gui->getSelectedPlayerCharacter().getCharacter();
        if (g_xpWindow)
        {
            RefreshXpRows();
        }
        if (g_trackedStatsVisible && g_selectorCharacter != g_selectedCharacter)
        {
            ShowTrackedStats(0);
        }
    }

    void __cdecl ForgottenGuiUpdateDetour(ForgottenGUI* gui)
    {
        g_originalForgottenGuiUpdate(gui);
        // Mod-created root windows are outside Kenshi's pause-menu overlay.
        // Put a kept-open XP window behind that overlay instead of faking the
        // effect with alpha; it then dims and stacks like native UI.
        if (g_xpWindow)
        {
            const bool shouldDim = gui->isPaused();
            if (shouldDim != g_xpWindowDimmedForPause)
            {
                MyGUI::LayerManager::getInstance().detachFromLayer(g_xpWindow);
                MyGUI::LayerManager::getInstance().attachToLayerNode(
                    shouldDim ? "Back" : "Window", g_xpWindow);
                g_xpWindow->setAlpha(1.0f);
                if (!shouldDim)
                {
                    MyGUI::LayerManager::getInstance().upLayerItem(g_xpWindow);
                }
                g_xpWindowDimmedForPause = shouldDim;
            }
        }
        // ForgottenGUI updates continuously in-game, unlike the Character
        // Stats window. Refresh from here so visible XP values continue to
        // change while the character screen is closed.
        if (g_xpWindow && (++g_globalRefreshFrames % 10) == 0)
        {
            RefreshXpRows();
        }
    }
}

namespace XpBars
{
    bool Install()
    {
        if (g_originalSetupStats)
        {
            return true;
        }
        const intptr_t setupTarget = KenshiLib::GetRealAddress(&CharacterStatsWindow::setupStats);
        if (!setupTarget || KenshiLib::AddHook(setupTarget,
                                               reinterpret_cast<void*>(&SetupStatsDetour),
                                               &g_originalSetupStats) != KenshiLib::SUCCESS ||
            !g_originalSetupStats)
        {
            g_originalSetupStats = 0;
            return false;
        }

        const intptr_t updateTarget = KenshiLib::GetRealAddress(&CharacterStatsWindow::_NV_update);
        if (!updateTarget || KenshiLib::AddHook(updateTarget,
                                               reinterpret_cast<void*>(&UpdateWindowDetour),
                                               &g_originalUpdateWindow) != KenshiLib::SUCCESS ||
            !g_originalUpdateWindow)
        {
            g_originalUpdateWindow = 0;
        }

        const intptr_t autoChangeTarget =
            KenshiLib::GetRealAddress(&CharacterStatsWindow::_NV_autoChangeSelectedObject);
        if (!autoChangeTarget || KenshiLib::AddHook(autoChangeTarget,
                                                    reinterpret_cast<void*>(&AutoChangeSelectedObjectDetour),
                                                    &g_originalAutoChange) != KenshiLib::SUCCESS ||
            !g_originalAutoChange)
        {
            g_originalAutoChange = 0;
        }

        const intptr_t selectedObjectsTarget =
            KenshiLib::GetRealAddress(&ForgottenGUI::selectedObjectsChanged);
        if (!selectedObjectsTarget || KenshiLib::AddHook(selectedObjectsTarget,
                                                         reinterpret_cast<void*>(&SelectedObjectsChangedDetour),
                                                         &g_originalSelectedObjectsChanged) != KenshiLib::SUCCESS ||
            !g_originalSelectedObjectsChanged)
        {
            g_originalSelectedObjectsChanged = 0;
        }

        const intptr_t guiUpdateTarget = KenshiLib::GetRealAddress(&ForgottenGUI::update);
        if (!guiUpdateTarget || KenshiLib::AddHook(guiUpdateTarget,
                                                   reinterpret_cast<void*>(&ForgottenGuiUpdateDetour),
                                                   &g_originalForgottenGuiUpdate) != KenshiLib::SUCCESS ||
            !g_originalForgottenGuiUpdate)
        {
            g_originalForgottenGuiUpdate = 0;
        }

        const intptr_t closeAllWindowsTarget =
            KenshiLib::GetRealAddress(&ForgottenGUI::closeAllWindows);
        if (!closeAllWindowsTarget || KenshiLib::AddHook(closeAllWindowsTarget,
                                                         reinterpret_cast<void*>(&CloseAllWindowsDetour),
                                                         &g_originalCloseAllWindows) != KenshiLib::SUCCESS ||
            !g_originalCloseAllWindows)
        {
            g_originalCloseAllWindows = 0;
        }

        const intptr_t keyDownTarget = KenshiLib::GetRealAddress(&InputHandler::keyDownEvent);
        if (!keyDownTarget || KenshiLib::AddHook(keyDownTarget,
                                                 reinterpret_cast<void*>(&KeyDownDetour),
                                                 &g_originalKeyDown) != KenshiLib::SUCCESS ||
            !g_originalKeyDown)
        {
            g_originalKeyDown = 0;
        }
        return true;
    }
}
