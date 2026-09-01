# Universal Playable Creatures v0.2.12

Source-available SKSE/CommonLibSSE-NG plugin for Skyrim AE 1.6.1170.

Universal Playable Creatures (UPC) makes supported creature races practical as player races and integrates the Universal Creature Controls runtime into the same DLL. The merged runtime uses one SKSE entry point, one lifecycle listener, one input route, one menu route, and one owner for each feature.

The implementation is event-driven. It does not use save serialization, background polling, worker timers, recurring equipment scans, delayed input replay, or plugin-owned persistent combat state.

## Features

### Universal creature controls

Supported catalog creatures receive a shared runtime control layer built around their own race data and behavior events:

- Attack-data-driven normal attacks using compatible race-local `BGSAttackData`.
- Sneak input repurposed for creature power attacks, including directional power attacks where supported.
- Native/compatible blocking support.
- Native-first spellcasting with compatibility fallback only when Skyrim's hand `MagicCaster` does not own the cast.
- Fallback spell and shout execution synchronized to authored animation `HitFrame` events when available.
- Crafting/workbench interception for creature players.
- Traversal/collision fallback used by supported creatures.
- Action-scoped cleanup for attacks and pending spell/shout payloads when an action ends or is interrupted.

Werewolf, Vampire Lord, and Werebear retain their dedicated vanilla/vanilla-derived behavior systems. The established crafting-interception exception is preserved.

### Race catalogs and playability

UPC loads creature definitions from namespaced race catalogs in:

```text
Data\SKSE\Plugins\UniversalPlayableCreatures\
```

Each row independently defines whether the race is enabled for UPC controls and which spell hand policy applies. `playable` and `spellHand` are independent. A disabled row may still retain handedness metadata. `spellHand` accepts `Left`, `Right`, or `Both`. Invalid or unresolved rows are isolated and logged instead of preventing other catalogs from loading.

The packaged catalogs contain 122 Skyrim/Dawnguard/Dragonborn races, 223 Oblivion-converted races, and 68 Morroblivion races, for 413 records total.

### Creature spell-hand policy

UPC can restrict ordinary spells to the hand supported by the current creature behavior. The policy is read from the active race-catalog row. Original spell equip slots are retained only in DLL memory and restored when the restriction is inactive; nothing is serialized into the save.

An optional `PlayableHumanoidSpellHand` setting can independently apply `Both`, `Left`, or `Right` to ordinary playable humanoid races. `Both` is the default/no-op.

### Drawn-state preservation across equipment changes

UPC preserves the visible ready/drawn state when a supported creature changes hand equipment.

`TESEquipEvent` is the single equipment-activity notification source for both menu and real-time changes. Equip and unequip events defer one authoritative comparison of `GetEquippedObject(left/right)` against cached hand FormIDs. Events that do not actually change either hand are ignored automatically.

Inventory, Magic, and Favorites are safety gates only: equipment events received while one of those menus is open are deferred until menu close. Real-time equipment changes use the same settled-hand comparison immediately through a one-shot SKSE task.

When a real hand change occurs while the ActionEvent-derived ready state is active, UPC uses the validated sheathe -> `EndSheathe` -> deferred redraw -> `EndDraw` transaction. The equipment system does not use `IsWeaponDrawn()`, NiNode rebuilding, input/hotkey inference, MinHook, timers, polling, or manual hand-form classification to detect changes.

## Bug fixes

### Creature RaceMenu crash protection

UPC preserves the signature-guarded RaceMenu protections required for creature player races on AE 1.6.1170. Playable flags are applied only to catalog rows explicitly enabled with `playable: true`.

### Weapon visibility corrections

The integrated controls core is the sole owner of runtime weapon-clone visibility. It can hide unsupported equipped weapon visuals and sheathed weapon clones for creature players without running a second competing weapon-hide implementation.

Physical converted attacks retain their real `BGSAttackData` through the authored damage `HitFrame`; UPC does not clear physical attack data prematurely at `HitFrame`.

## Compatibility workarounds

### Third-person camera node

Many creature skeletons do not provide the player camera node expected by Skyrim. UPC injects `Camera3rd [Cam3]` when needed and resynchronizes it from race/3D/menu/POV events rather than from a polling loop.

### Unsupported spell-hand behavior

Some creature behavior graphs become stuck or conflict with blocking when a spell is equipped to an unsupported hand. The per-race spell-hand policy prevents those unsupported configurations while leaving ordinary spells otherwise unchanged.

### Converted-creature equipment readiness

Converted creature graphs can visibly remain ready/drawn while Skyrim's ordinary `IsWeaponDrawn()` query reports false. UPC therefore derives creature readiness from draw/sheathe ActionEvents and preserves that state across hand-equipment changes using the event-driven transaction described above.

### Spellcasting fallback

When a creature does not obtain native `MagicCaster` ownership for a spell, UPC falls back to a compatible creature animation and synchronizes the cast to that animation's exact `HitFrame` when possible. An immediate cast is reserved for the absolute bottom fallback where neither native ownership nor an accepted fallback animation exists.

## Configuration

Main configuration:

```text
Data\SKSE\Plugins\UniversalPlayableCreatures.json
```

Default settings:

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

Race catalogs are loaded non-recursively and in deterministic filename order from the UPC subfolder. JSONC-style comments are accepted. Human-readable metadata such as `name` and `editorID` may be included for maintenance and is ignored by runtime lookup.

## Installation layout

```text
Data\SKSE\Plugins\UniversalPlayableCreatures.dll
Data\SKSE\Plugins\UniversalPlayableCreatures.json
Data\SKSE\Plugins\UniversalPlayableCreatures\Skyrim.json
Data\SKSE\Plugins\UniversalPlayableCreatures\Oblivion.json
Data\SKSE\Plugins\UniversalPlayableCreatures\Morroblivion.json
```

A separately installed `UniversalCreatureControls.dll` should not be used alongside UPC because the controls core is already integrated into `UniversalPlayableCreatures.dll`.

Runtime log:

```text
Documents\My Games\Skyrim Special Edition\SKSE\UniversalPlayableCreatures.log
```

## License

Source available — All Rights Reserved. This project is not open source. See `LICENSE` for permitted uses.
