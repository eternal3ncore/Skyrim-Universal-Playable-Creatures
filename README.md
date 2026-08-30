# Universal Playable Creatures v0.1.6

## v0.1.6 race-catalog update

`Skyrim.json` is rebuilt from the supplied full Skyrim/Dawnguard/Dragonborn RACE export. It contains 122 creature and creature-variant races. This also corrects the Armored Husky FormID and includes all four exported Husky race variants. `Oblivion.json` remains at 223 entries and `Morroblivion.json` at 68 entries.


Source-available SKSE/CommonLibSSE-NG plugin for Skyrim AE 1.6.1170.

Universal Playable Creatures combines four creature-player runtime features into one DLL. Each feature is independently optional in the main JSON and enabled by default.

## Included features

1. **Creature RaceMenu Crash Fix**
   - Preserves the signature-guarded RaceMenu crash safeguards from the confirmed Creature Race Menu Crash Fix lineage.
   - At DataLoaded the plugin scans only `Data\SKSE\Plugins\UniversalPlayableCreatures\` for `*.json` race-definition files.
   - Every race-definition JSON uses the existing `playableRaces` array with `PluginName.esm|FORMID` entries.

2. **3rd-Person Camera Node**
   - Injects `Camera3rd [Cam3]` into creature player 3D when needed.
   - Event-driven injection occurs on race changes, player 3D reload, RaceMenu transitions, and new/load game.
   - Third-person camera binding is retried only in response to the mapped Toggle POV input; no timer thread or polling loop is used.

3. **Creature Spell Hand Restriction**
   - Keeps ordinary spells as ordinary spells.
   - Restricts their equip side while the configured creature policy is active.
   - Default: Skyrim creatures = Left, TES4-converted creatures = Right. This stops the graph from getting stuck, as some creatures simple should not have a spell equipped in a certain hand.
   - Valid policies are `Left`, `Right`, and `Both`.
   - Original equip slots exist only in DLL memory and are restored when the restriction is inactive; nothing is serialized into the save.

4. **Hide Sheathed Weapons**
   - Hides native BipedAnim weapon clones after a creature player finishes sheathing and restores them when drawing begins.
   - On load, race switch, and player 3D reload, the plugin derives desired visibility from the engine's current `WEAPON_STATE` rather than a plugin-owned save boolean.
   - No SKSE serialization, save-baked state, timer, or polling loop is used.

## Configuration

Install the main config as:

```text
Data\SKSE\Plugins\UniversalPlayableCreatures.json
```

Default main configuration:

```json
{
  "EnableCreatureRaceMenuCrashFix": true,
  "EnableThirdPersonCameraNode": true,
  "EnableCreatureSpellHandRestriction": true,
  "EnableHideSheathedWeapons": true,

  "CameraNodeHeightZ": 121.0,

  "EnablePlayableHumanoidRacesForSpellHandRestriction": false,
  "EnableCreatureRacesForSpellHandRestriction": true,
  "SkyrimCreatureSpellHand": "Left",
  "ConvertedTES4CreatureSpellHand": "Right"
}
```

Playable race definitions belong only in:

```text
Data\SKSE\Plugins\UniversalPlayableCreatures\
```

The package includes:

```text
UniversalPlayableCreatures\Skyrim.json
UniversalPlayableCreatures\Oblivion.json
UniversalPlayableCreatures\Morroblivion.json
```

The DLL scans only that namespaced subfolder. It does not enumerate unrelated JSON files elsewhere in `SKSE\Plugins`.

Race-definition format:

```json
{
  "playableRaces": [
    "SomePlugin.esm|00123456"
  ]
}
```

Additional `*.json` files using the same format may be added to the same folder without recompiling the DLL.

The packaged race catalogs now contain the current exhaustive test definitions: 122 Skyrim/DLC creature races, 223 Oblivion-converted races, and 68 Morroblivion races. Ordinary vanilla playable humanoid races remain excluded.

Race-definition files may use readable JSONC-style `//` or `/* ... */` comments. v0.1.5 parses the `playableRaces` array structurally, so brackets inside comments such as `[EditorID]` no longer truncate the array.

Install:

```text
Data\SKSE\Plugins\UniversalPlayableCreatures.dll
Data\SKSE\Plugins\UniversalPlayableCreatures.json
Data\SKSE\Plugins\UniversalPlayableCreatures\Skyrim.json
Data\SKSE\Plugins\UniversalPlayableCreatures\Oblivion.json
Data\SKSE\Plugins\UniversalPlayableCreatures\Morroblivion.json
```
Expected log:

```text
Documents\My Games\Skyrim Special Edition\SKSE\UniversalPlayableCreatures.log
```

## License

Source available — All Rights Reserved. This project is not open source. See `LICENSE`.
