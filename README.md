# Universal Playable Creatures v0.2.0

Source-available SKSE/CommonLibSSE-NG plugin for Skyrim AE 1.6.1170.

Universal Playable Creatures now includes the Universal Creature Controls runtime core in the same DLL. The merge deliberately keeps one SKSE entry point, one logger, one base configuration, and one owner for each runtime feature.

## Merged architecture

`UniversalPlayableCreatures.dll` provides:

- Creature RaceMenu crash protection and runtime playable-race registration.
- 3rd-person creature camera-node support.
- Per-race creature spell-hand restriction plus an optional global humanoid spell-hand preference.
- Universal creature combat controls: attack-data-driven normal attacks, Sneak power attacks, blocking, spell/shout fallback synchronization, crafting interception, and traversal/collision fallback.
- Creature weapon-visibility handling, including the established unsupported-weapon and sheathed-weapon paths.

The controls core is internal to this DLL. A separate `UniversalCreatureControls.dll` is not part of the merged runtime.

The implementation remains event-driven. It does not add save serialization, background polling, worker timers, delayed input replay, or persistent plugin-owned combat state.

## Configuration

Install the main config as:

```text
Data\SKSE\Plugins\UniversalPlayableCreatures.json
```

Default configuration:

```json
{
  "EnableCreatureRaceMenuCrashFix": true,
  "EnableThirdPersonCameraNode": true,
  "CameraNodeHeightZ": 121.0,

  "EnableCreatureSpellHandRestriction": true,
  "PlayableHumanoidSpellHand": "Both",

  "EnableHideUnsupportedEquippedWeapons": true,
  "EnableHideSheathedWeapons": true
}
```

`PlayableHumanoidSpellHand` accepts `Both`, `Left`, or `Right`. `Both` is the default and imposes no restriction on ordinary playable humanoid races. Creature handedness is not guessed from origin; it is read from each creature's race-catalog row.

`EnableCreatureSpellHandRestriction` is the master switch for runtime spell-hand orientation. There is intentionally no separate `EnableCreatureRacesForSpellHandRestriction` setting.

## Race catalogs

Race definitions belong only in:

```text
Data\SKSE\Plugins\UniversalPlayableCreatures\
```

The DLL scans `*.json` files in that folder non-recursively and in deterministic filename order. Catalog files use an object-based schema so playability and runtime handedness are independent:

```json
{
  "schemaVersion": 2,
  "format": "UniversalPlayableCreaturesRaceCatalog",
  "races": [
    {
      "race": "SomePlugin.esm|00123456",
      "playable": false,
      "spellHand": "Left"
    }
  ]
}
```

`playable: false` leaves the race unavailable in RaceMenu while retaining its runtime metadata. This is the release-safe default. `spellHand` accepts `Left`, `Right`, or `Both`. If a creature has no catalog row or no valid `spellHand`, UPC leaves its spell equip side unrestricted rather than applying an origin-based guess.

JSONC-style comments are accepted. Invalid or unresolved rows are isolated and logged instead of preventing the remaining files from loading.

The established catalog inventories are 122 Skyrim/Dawnguard/Dragonborn creature and variant races, 223 Oblivion-converted races, and 68 Morroblivion races, for 413 cataloged race records total. Ordinary vanilla playable humanoid races do not require catalog rows for the optional `PlayableHumanoidSpellHand` setting.

## Runtime feature notes

### RaceMenu protection

The AE 1.6.1170 RaceMenu safeguards retain their signature validation before installing the protected hooks. Playable flags are applied only to catalog rows explicitly set to `playable: true`.

### Camera node

For creature players, UPC injects `Camera3rd [Cam3]` only when needed. Race changes, player 3D rebuilds, RaceMenu transitions, and mapped POV input provide event-driven synchronization; no camera polling loop is used.

### Spell hand restriction

UPC temporarily changes the equip slot of known ordinary spells while a restriction is active and restores the original slots when it is not. Nothing is serialized into the save. Creature policy comes from the current race's catalog row; humanoid policy comes only from `PlayableHumanoidSpellHand`.

Concentration spell behavior remains a known limitation of creature casting and should not be assumed equivalent to normal humanoid concentration casting.

### Universal creature controls

The merged controls core preserves the established data/event-driven model: race-local `BGSAttackData` is discovered on race activation, physical attacks use authored attack events and authentic HitFrames, and action-scoped payloads are cleared on termination/interruption. Converted TES4 attack-family compatibility remains isolated from unrelated native attack families.

Werewolf, Vampire Lord, and Werebear retain their dedicated vanilla behavior systems and are excluded from ordinary UPC creature combat/traversal intervention; the established crafting interception exception is preserved.

### Weapon visibility

Only the controls core owns runtime weapon-clone culling in the merged DLL. The older independent UPC sheathed-weapon implementation is not simultaneously active, preventing two subsystems from competing to hide/restore the same `BipedAnim` clones.

## Install layout

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

For the merged build, remove/disable a separately installed `UniversalCreatureControls.dll` so the same controls are not registered twice.

## Development status

v0.2.0 is the first merged UPC/UCC candidate. A successful compile and export check proves binary integration; it does not replace in-game validation. Preserve v0.1.6 UPC and the established UCC baseline for rollback until the merged candidate is runtime-tested.

## License

Source available — All Rights Reserved. This project is not open source. See `LICENSE` for permitted uses.
