# Universal Playable Creatures v0.2.20

SKSE plugin for Skyrim Special Edition/Anniversary Edition that makes configured creature races practical to use as the player while preserving native behavior wherever possible. Developed and tested on runtime 1.6.1170 and built with CommonLibSSE-NG. Universal Creature Controls is integrated into the same DLL.

## Features

- Opt-in playable creature races through editable JSON catalogs.
- RaceMenu crash protection for configured creature races.
- Automatic third-person creature camera-node support.
- Race-specific spell-hand restrictions.
- Creature combat compatibility using each race's real `BGSAttackData` and animation events.
- Normal attacks, power attacks, blocking, spell/shout fallback synchronization, and authentic HitFrame timing where supported.
- Equipment-change graph recovery while the creature is drawn/ready.
- Creature crafting/furniture compatibility handling.
- Per-race equipped and sheathed weapon visibility controls.
- Data-driven attack-family profiles for converted creature sets.
- Event-driven runtime: no background polling, save serialization, or persistent plugin-owned combat state.

## Compatibility workarounds

UPC supplies targeted runtime workarounds for creature systems that Skyrim normally assumes belong to NPCs rather than the player. These include creature RaceMenu registration, missing third-person camera nodes, broken equipment graph state after changing weapons or spells, converted-creature attack dispatch, spell release synchronization, crafting interception, and unsuitable weapon/sheath visuals.

Compatibility combat is enabled per race with `useCombatWorkaround`. Native Skyrim creatures can remain on their native combat path, while converted races can opt into UPC's compatibility bridge. Werewolf, Vampire Lord, and Werebear retain their dedicated vanilla behavior systems and are excluded from ordinary UPC combat/traversal intervention.

## Examples

- **Oblivion/Morroblivion creatures:** use their supplied converter-specific attack-family profiles to classify their existing attack events.
- **Skyrim creatures:** can be made playable while retaining native combat where no workaround is required.
- **Custom creature races:** can be added through a race catalog and assigned a custom `.attackfamilies.json` profile without hard-coding the race into the DLL.

## Configuration

Race catalogs are located in:

```text
Data\SKSE\Plugins\UniversalPlayableCreatures\
```

Shipped creature races are **disabled by default**. Enable only the races you want by changing their catalog entry:

```json
{
  "name": "Minotaur",
  "race": "Oblivion.esm|XXXXXXXX",
  "playable": true,
  "spellHand": "Right",
  "useCombatWorkaround": true,
  "attackFamily": "OblivionConverted",
  "hideEquippedWeapon": false,
  "hideSheathedWeapon": true
}
```

`playable: false` leaves the race unavailable to the player. `spellHand` accepts `Left`, `Right`, or `Both`. Weapon visibility and combat-workaround behavior are independently configurable per race.

Attack-family definitions are stored in:

```text
Data\SKSE\Plugins\UniversalPlayableCreatures\AttackFamilies\
```

Included profiles cover Skyrim, Oblivion conversions, Morroblivion conversions, and Fallout: New Vegas-style converted attack naming. A Fallout: New Vegas race catalog is not currently shipped.

## Installation

Requirements:

- Skyrim Special Edition/Anniversary Edition
- SKSE64 appropriate for your Skyrim runtime
- Address Library

The current release is developed and runtime-tested on Skyrim 1.6.1170. Other runtimes have not yet been validated and should not be assumed supported solely because UPC is built with CommonLibSSE-NG.

Install the compiled runtime files so the layout is:

```text
Data\SKSE\Plugins\UniversalPlayableCreatures.dll
Data\SKSE\Plugins\UniversalPlayableCreatures.json
Data\SKSE\Plugins\UniversalPlayableCreatures\Skyrim.json
Data\SKSE\Plugins\UniversalPlayableCreatures\Oblivion.json
Data\SKSE\Plugins\UniversalPlayableCreatures\Morroblivion.json
Data\SKSE\Plugins\UniversalPlayableCreatures\AttackFamilies\*.attackfamilies.json
```

Then edit the desired race catalog entries to `"playable": true` and launch Skyrim through SKSE.

If an older standalone `UniversalCreatureControls.dll` is installed, remove or disable it. Its functionality is already integrated into Universal Playable Creatures.

Runtime log:

```text
Documents\My Games\Skyrim Special Edition\SKSE\UniversalPlayableCreatures.log
```

## License

Source available — All Rights Reserved. This project is not open source. See `LICENSE` for permitted uses.
