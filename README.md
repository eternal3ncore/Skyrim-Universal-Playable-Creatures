# Universal Playable Creatures v0.2.12

Universal Playable Creatures (UPC) is an SKSE plugin for Skyrim Anniversary Edition that enables supported creature races to be used by the player and supplies runtime control support for creature-specific behavior.

## Features

### Universal creature controls

UPC provides runtime control support for supported creature races, including:

- Creature-compatible normal and power attacks.
- Directional power attacks through the Sneak control where supported.
- Blocking support for compatible creature behavior graphs.
- Native-first creature spellcasting with animation fallback where required.
- Shout/power animation support with HitFrame synchronization where supported.
- Creature crafting/furniture interception.
- Runtime weapon visibility synchronization.
- Preservation of the player's ready/drawn state when changing hand equipment.

### Race catalogs and playability

Creature support is configured through JSON race catalogs. UPC includes separate catalogs for Skyrim/DLC, converted Oblivion creatures, and Morroblivion creatures.

Races are disabled for player selection by default. This allows users to explicitly enable only the creature races they want to use without rebuilding the DLL.

To enable a race, open the appropriate JSON catalog in:

`Data\SKSE\Plugins\UniversalPlayableCreatures\`

Find the race entry you want to enable. Each entry contains a `playable` setting such as:

```json
{
  "name": "Clannfear",
  "editorID": "TES4SummonClannfearRace",
  "race": "Oblivion.esm|00714D7C",
  "playable": false,
  "spellHand": "Right"
}
```

Change only:

```json
"playable": false
```

to:

```json
"playable": true
```

Save the JSON file and restart Skyrim so UPC reloads the catalog. No DLL rebuild is required.

The other fields should normally be left unchanged. In particular, `race` identifies the race form and `spellHand` defines the configured spell-hand policy for that race.

The included catalogs are:

- `Skyrim.json` — Skyrim and DLC creature races.
- `Oblivion.json` — converted Oblivion creature races.
- `Morroblivion.json` — converted Morroblivion creature races.

### Creature spell-hand policy

Race catalog entries can specify the supported creature spell hand independently of whether the race is playable. UPC uses this information to prevent unsupported spell-hand configurations while preserving known native creature behavior.

### Drawn-state preservation across equipment changes

UPC listens for player equipment activity and compares the player's actual equipped left- and right-hand objects after the change settles. When hand equipment changes while a supported creature is ready, UPC preserves that ready state through the equipment transition.

This system is event-driven and does not rely on continuous polling.

## Bug fixes

### Creature RaceMenu crash protection

UPC protects supported creature transformations from RaceMenu paths that are unsafe for creature races.

### Weapon visibility corrections

UPC synchronizes creature weapon visuals with equipment and ready-state changes where the normal humanoid weapon presentation does not behave correctly for creature skeletons.

## Compatibility workarounds

### Third-person camera node

UPC can provide the runtime third-person camera node support required by creature skeletons that do not contain the normal player camera node.

### Unsupported spell-hand behavior

Converted creature behavior graphs do not necessarily support both humanoid spell hands. UPC applies the configured race-specific hand policy rather than assuming every creature can use both hands.

### Converted-creature equipment readiness

Skyrim's normal `IsWeaponDrawn()` state is not reliable for every converted creature. UPC therefore tracks creature ready state from SKSE action events for systems that need to preserve the player's drawn/sheathed state.

### Spellcasting fallback

When a creature behavior graph does not take native ownership of an equipped spell cast, UPC can fall back to a compatible creature animation and synchronize the spell effect to its authored HitFrame where possible.

## Configuration

The main configuration file is:

`Data\SKSE\Plugins\UniversalPlayableCreatures.json`

Race catalogs are stored in:

`Data\SKSE\Plugins\UniversalPlayableCreatures\`

Race playability is controlled per race with the `playable` boolean in those catalog files. The distributed catalogs default races to `false`; set individual races to `true` to enable them.

## Installation layout

UPC runtime files are installed under Skyrim's `Data` directory. The SKSE plugin DLL and primary configuration belong in `Data\SKSE\Plugins`, with race catalogs in the `UniversalPlayableCreatures` subdirectory.

## License

See `LICENSE` for licensing information.
