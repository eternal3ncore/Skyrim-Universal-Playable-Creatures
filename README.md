# Universal Playable Creatures v0.2.13

Universal Playable Creatures (UPC) is an SKSE plugin for Skyrim Anniversary Edition that enables supported creature races to be used by the player and provides runtime controls and compatibility support for creature-specific behavior.

## Features

### Universal creature controls

UPC provides runtime control support for supported creature races, including:

- Creature-compatible normal and power attacks.
- Directional power attacks through the Sneak control where supported.
- Blocking support for compatible creature behavior graphs.
- Native-first creature spellcasting with animation fallback where required.
- Shout and power animation support with HitFrame synchronization where supported.
- Creature crafting and furniture interception.
- Per-race weapon visibility handling.
- Preservation of the player's ready state when hand equipment changes.

### Race catalogs and playability

Creature support is configured through separate JSON catalogs for Skyrim/DLC, converted Oblivion creatures, and Morroblivion creatures.

Races are disabled for player selection by default. To enable a race, open the appropriate catalog in:

`Data\SKSE\Plugins\UniversalPlayableCreatures\`

and change its `playable` value from `false` to `true`:

```json
{
  "name": "Clannfear",
  "editorID": "TES4SummonClannfearRace",
  "race": "Oblivion.esm|00714D7C",
  "playable": true,
  "spellHand": "Right",
  "hideEquippedWeapon": false,
  "hideSheathedWeapon": false
}
```

Save the file and restart Skyrim.

The included catalogs are:

- `Skyrim.json` — Skyrim and DLC creature races.
- `Oblivion.json` — converted Oblivion creature races.
- `Morroblivion.json` — converted Morroblivion creature races.

### Creature spell-hand policy

Each catalog entry can define a `spellHand` policy independently of whether the race is playable. UPC uses this to prevent unsupported spell-hand configurations while preserving known native creature behavior.

### Per-race weapon visibility

Weapon presentation is configured directly in each race entry rather than inferred from attack capability:

- `hideEquippedWeapon: false`, `hideSheathedWeapon: false` — weapon remains visible normally.
- `hideEquippedWeapon: false`, `hideSheathedWeapon: true` — weapon is visible while drawn and hidden while sheathed.
- `hideEquippedWeapon: true` — weapon is hidden regardless of ready state.

If either visibility field is absent or invalid, it defaults to `false`.

### Equipment-change ready-state preservation

UPC listens for player equipment activity and compares the player's actual equipped left- and right-hand objects after the change settles. If the hands changed while a supported creature was ready, UPC preserves that ready state through the equipment transition.

This system is event-driven and does not use continuous polling.

## Bug fixes

### Creature RaceMenu crash protection

UPC protects supported creature transformations from RaceMenu paths that are unsafe for creature races.

### Weapon visibility corrections

UPC can hide inappropriate equipped weapon meshes and broken sheathed weapon placements on a per-race basis. Weapon visibility is synchronized with equipment and ready-state events rather than determined from attack-family scanning.

## Compatibility workarounds

### Third-person camera node

UPC can provide runtime third-person camera-node support for creature skeletons that do not contain the normal player camera node.

### Unsupported spell-hand behavior

Converted creature behavior graphs do not necessarily support both humanoid spell hands. UPC applies the configured race-specific hand policy rather than assuming every creature can use both hands.

### Converted-creature equipment readiness

Skyrim's normal `IsWeaponDrawn()` state is not reliable for every converted creature. UPC therefore derives creature ready state from SKSE action events for systems that need to distinguish drawn and sheathed states.

### Spellcasting fallback

When a creature behavior graph does not take native ownership of an equipped spell cast, UPC can fall back to a compatible creature animation and synchronize the spell effect to its authored HitFrame where possible.

## Configuration

The main configuration file is:

`Data\SKSE\Plugins\UniversalPlayableCreatures.json`

Race catalogs are stored in:

`Data\SKSE\Plugins\UniversalPlayableCreatures\`

The main configuration controls global UPC behavior. Race-specific playability, spell-hand policy, and weapon visibility are controlled by each race's catalog entry.

## Installation layout

UPC runtime files are installed under Skyrim's `Data` directory. The SKSE plugin DLL and primary configuration belong in `Data\SKSE\Plugins`, with race catalogs in the `UniversalPlayableCreatures` subdirectory.

## License

See `LICENSE` for licensing information.
