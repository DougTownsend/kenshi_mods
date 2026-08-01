# Visible Bounties

Visible Bounties displays a gold bounty label above every loaded NPC with a
positive bounty. Labels use the compact format `[3,000c]`, update when the
bounty changes, and are removed when the NPC is no longer loaded. Controlled
player characters are excluded. A label also disappears while its NPC is
imprisoned.

At long range, Kenshi updates NPC skeleton positions less frequently. The
label follows those updates, so it may visibly jump or jitter until the NPC is
closer.

## Implementation

This is an [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi) native
plugin built with KenshiLib.

- It hooks `GameWorld::_NV_mainLoop_GPUSensitiveStuff` to inspect Kenshi's
  loaded-character update list and read each NPC's total bounty through
  `BountyManager::getTotalBounty`.
- Each active bounty uses Kenshi's `ScreenLabel` UI, positioned from the
  humanoid skeleton's `Bip01 HeadNub` bone.
- It hooks `ScreenLabel::_NV_update` after Kenshi projects the label to the
  screen, then centers the label horizontally and applies a stable
  screen-space gap above the character.
- Labels are limited to NPCs whose physical models are active. Kenshi can
  still use coarse skeleton updates for distant physical models, which causes
  the documented jitter.
- Prisoners are identified from Kenshi's `Character::inSomething` state and
  do not receive a bounty label while in a prison cage.
- The plugin is read-only: it does not change bounty values, NPC data, or save
  files.
