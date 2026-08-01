# Show Effective Stats

Show Effective Stats is an RE_Kenshi plugin for Kenshi that adds the live,
effective stat value to the Character Stats screen.

Every supported row keeps Kenshi's normal displayed value and adds the value
currently used by the game in parentheses:

```text
<base value> (<effective value>)
```

For example, a stat shown by Kenshi as `50` may appear as `50 (42.5)` while it
is reduced by the character's current condition or equipment.

## How it works

The mod does not calculate any bonuses or penalties. It asks Kenshi directly
for each stat's effective value through the game's `CharStats::getStat` API.
This means equipment, injuries, hunger, racial effects, encumbrance, and other
game modifiers remain entirely Kenshi's responsibility.

The Stats window and its child panels are widened so the extra value has room
beside long stat names. The unused derived-stat panel is kept outside the
visible window, matching the vanilla screen.

## Requirements

- Kenshi x64 with RE_Kenshi 0.3.4 / KenshiLib 0.4.0.
- Kenshi 1.0.65 or 1.0.68 x64.

## Use

Install and enable `ShowEffectiveStats.mod`, then open any character's Stats
screen. The mod changes only the UI; it does not modify saves or gameplay
data.

If troubleshooting is needed, open the Stats screen and inspect
`RE_Kenshi_log.txt` for the `ShowEffectiveStats` hook-install message.

## Building

Use `tools/build.ps1` to produce a `Release|x64` DLL, `tools/verify.ps1` to
verify it, and `tools/deploy.ps1` for a Windows Kenshi installation. The
project requires the MSVC 2010 (`v100`) toolset and matching KenshiLib
dependencies.
