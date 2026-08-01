#include <core/Functions.h>
#include <kenshi/Character.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/gui/ForgottenGUI.h>
#include <kenshi/gui/ScreenLabel.h>

#include <map>
#include <set>
#include <sstream>
#include <string>

#include "BountyLabels.h"
#include "Diagnostics.h"

namespace
{
    typedef void (__cdecl *WorldUpdateFn)(GameWorld*, float);
    WorldUpdateFn g_originalWorldUpdate = 0;
    typedef void (__cdecl *ScreenLabelUpdateFn)(ScreenLabel*);
    ScreenLabelUpdateFn g_originalScreenLabelUpdate = 0;

    // ScreenLabel owns its MyGUI widget and naturally observes camera/UI
    // scaling. Its world position is refreshed from each character's head.
    std::map<Character*, ScreenLabel*> g_labels;

    const MyGUI::Colour kBountyColour(1.0f, 0.86f, 0.23f, 1.0f);
    const int kVerticalGapPixels = 10;
    const float kHeadLiftWorldUnits = 0.5f;

    bool IsBountyLabel(ScreenLabel* label)
    {
        for (std::map<Character*, ScreenLabel*>::const_iterator it = g_labels.begin();
             it != g_labels.end(); ++it)
        {
            if (it->second == label)
            {
                return true;
            }
        }
        return false;
    }

    std::string FormatBounty(int value)
    {
        std::ostringstream digits;
        digits << value;
        const std::string raw = digits.str();
        std::string formatted;
        for (size_t index = 0; index < raw.size(); ++index)
        {
            if (index && (raw.size() - index) % 3 == 0)
            {
                formatted += ',';
            }
            formatted += raw[index];
        }
        return "[" + formatted + "c]";
    }

    Ogre::Vector3 GetOverheadPosition(Character* character)
    {
        // Bip01 Head is the skull's attachment point. HeadNub is its endpoint
        // near the crown, so it produces the same kind of overhead anchor as
        // a nameplate without guessing from the character's feet.
        Ogre::Vector3 position = character->getBoneWorldPosition("Bip01 HeadNub");
        // This small world-space lift is pronounced up close but nearly
        // imperceptible at distance, complementing the constant pixel gap.
        position.y += kHeadLiftWorldUnits;
        return position;
    }

    void DestroyLabel(ScreenLabel* label)
    {
        // The game owns ScreenLabel lifetimes. Never delete a label directly.
        if (label && gui)
        {
            gui->destroy(label);
        }
    }

    void RefreshLabels(GameWorld* world)
    {
        if (!world || !gui)
        {
            return;
        }
        if (!gui->created)
        {
            // A GUI restart destroys the game-owned labels. Forget their old
            // addresses so newly created UI cannot receive stale pointers.
            g_labels.clear();
            return;
        }

        std::set<Character*> visibleBountyTargets;
        const ogre_unordered_set<Character*>::type& characters =
            world->getCharacterUpdateList();
        for (ogre_unordered_set<Character*>::type::const_iterator it = characters.begin();
             it != characters.end(); ++it)
        {
            Character* const character = *it;
            // Player characters keep Kenshi's existing overhead name display;
            // this feature is intentionally limited to NPC bounty targets.
            if (!character || character->isPlayerCharacter() ||
                character->inSomething == IN_PRISON)
            {
                continue;
            }

            // Distant NPCs can remain in the update list after Kenshi has
            // unloaded their physical model. Their interpolated skeleton is
            // deliberately coarse, which makes overhead labels jump. Match
            // the game's near-model range.
            if (!character->isPhysical())
            {
                continue;
            }

            const int bounty = character->crimes.getTotalBounty();
            if (bounty <= 0)
            {
                continue;
            }

            visibleBountyTargets.insert(character);
            ScreenLabel*& label = g_labels[character];
            if (!label)
            {
                label = gui->createScreenLabel(FormatBounty(bounty), kBountyColour,
                                               ScreenLabel::LS_SMALL,
                                               ScreenLabel::RS_STOPPED);
                if (label)
                {
                    label->setPosition(GetOverheadPosition(character));
                }
            }
            else
            {
                label->setCaption(FormatBounty(bounty));
                label->setPosition(GetOverheadPosition(character));
            }
        }

        for (std::map<Character*, ScreenLabel*>::iterator it = g_labels.begin();
             it != g_labels.end(); )
        {
            if (visibleBountyTargets.find(it->first) == visibleBountyTargets.end())
            {
                DestroyLabel(it->second);
                g_labels.erase(it++);
            }
            else
            {
                ++it;
            }
        }
    }

    void __cdecl WorldUpdateDetour(GameWorld* world, float frameTime)
    {
        g_originalWorldUpdate(world, frameTime);
        RefreshLabels(world);
    }

    void __cdecl ScreenLabelUpdateDetour(ScreenLabel* label)
    {
        g_originalScreenLabelUpdate(label);

        // ScreenLabel projects its world position at the text widget's left
        // edge. The widget rectangle is taller than its rendered glyphs, so
        // centre the visual text using half that height before adding the
        // screen-space gap. This stays stable as the camera zooms.
        if (IsBountyLabel(label) && label->textWidget)
        {
            label->textWidget->setPosition(
                label->textWidget->getLeft() - label->textWidget->getWidth() / 2,
                label->textWidget->getTop() - label->textWidget->getHeight() / 2 -
                    kVerticalGapPixels);
        }
    }

}

namespace VisibleBounties
{
    bool Install()
    {
        if (g_originalWorldUpdate)
        {
            return true;
        }

        // mainLoop_GPUSensitiveStuff is virtual. Its ordinary C++ member
        // pointer resolves to this DLL's dispatch thunk; KenshiLib correctly
        // rejects that address. The generated _NV_ method names Kenshi's real
        // non-virtual implementation, which is the hook target we need.
        const intptr_t target = KenshiLib::GetRealAddress(
            &GameWorld::_NV_mainLoop_GPUSensitiveStuff);
        if (!target || KenshiLib::AddHook(target,
                                          reinterpret_cast<void*>(&WorldUpdateDetour),
                                          &g_originalWorldUpdate) != KenshiLib::SUCCESS ||
            !g_originalWorldUpdate)
        {
            g_originalWorldUpdate = 0;
            Log("world update hook=failed feature=disabled");
            return false;
        }

        Log("world update hook=installed labels=ScreenLabel bounty=BountyManager::getTotalBounty");

        const intptr_t labelUpdateTarget = KenshiLib::GetRealAddress(&ScreenLabel::_NV_update);
        if (!labelUpdateTarget || KenshiLib::AddHook(labelUpdateTarget,
                                                     reinterpret_cast<void*>(&ScreenLabelUpdateDetour),
                                                     &g_originalScreenLabelUpdate) != KenshiLib::SUCCESS ||
            !g_originalScreenLabelUpdate)
        {
            g_originalScreenLabelUpdate = 0;
            Log("screen label alignment hook=failed using=vanilla-left-anchor");
        }
        else
        {
            Log("screen label alignment hook=installed anchor=bottom-center vertical_gap_pixels=10");
        }

        Log("range mode=rendered labels=physical-npcs-only");
        return true;
    }
}
