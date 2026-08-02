# XP Bars

XP Bars is an RE_Kenshi plugin that adds a compact, movable window for tracking
character experience progress. It uses Kenshi's own UI skins and the game's
localised stat names, so it fits alongside the vanilla character interface.

## What it does

- Adds an **XP Bars** button below the Attributes panel in the character Stats
  window.
- Displays any selected attributes and skills with their current level,
  percentage progress to the next level, and an XP bar.
- Refreshes displayed levels and progress continuously, including while the
  character Stats window is closed.
- Provides a **Tracked Stats** window with every available stat arranged in the
  same groups and order as Kenshi's character panel.
- Keeps tracked selections separate for each loaded character for the current
  game session.

## Using the mod

1. Open a character's Stats window and select **XP Bars**.
2. Select **Tracked Stats** in the XP Bars title bar.
3. Check the attributes or skills to show for the currently focused character.
   Select **Deselect All** to clear that character's tracked list.

The main XP Bars button toggles the XP Bars window. The **Keep Open** toggle
keeps only that window visible when `Esc` closes the rest of Kenshi's windows.
`Esc` always closes Tracked Stats. Both windows otherwise follow normal Kenshi
window stacking and pause-menu dimming behavior.

Tracked selections are intentionally session-only: they are not written to the
save file and are cleared when the game session ends.

## Implementation notes

The plugin hooks Kenshi's character-Stats setup/update paths, selected-character
changes, the global GUI update, and the game's close-all-windows input path.
Those hooks let it insert the launch button into the vanilla Stats screen,
refresh values as the focused character changes, and mirror the game's Escape
behavior without replacing the existing UI.

The XP and selector windows are built with MyGUI widgets and native Kenshi
skins. Stat names and category placement are read from the character Stats
panel, rather than maintained as a separate hard-coded list. The XP progress
bar uses the fractional portion of Kenshi's underlying stat value; its label is
shown as a percentage with one decimal place.

The title-bar controls are laid out from Kenshi's live native title controls
and the measured rendered label text. This keeps the controls aligned across
UI scales and prevents localised captions from receiving arbitrary fixed
widths.
