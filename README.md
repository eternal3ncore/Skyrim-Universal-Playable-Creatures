# Universal Playable Creatures v0.2.15 Test Candidate

## v0.2.3 native-first spell ownership

Fire-and-forget/fixed-duration creature spells now give Skyrim's native hand `MagicCaster` the first opportunity to own the cast. UCC records the input DOWN edge without starting a competing cast. On input UP, if the native hand caster still owns the exact equipped spell, UCC leaves release timing completely to Skyrim and the creature behavior graph. This preserves native authored spell release/HitFrame timing for creatures such as converted Scamps.

Only when native ownership is absent does UCC enter its compatibility fallback. An accepted fallback animation queues the spell for that animation's exact `HitFrame`; only the absolute animationless bottom fallback casts immediately. No timers, polling, synthetic delay, or save state were added.



## v0.2.1 merged-runtime cleanup

This candidate repairs the v0.2.0 merge regression in which the internal UCC core
attempted to register a second SKSE messaging callback from the same DLL. UPC now owns
one SKSE lifecycle listener, one input sink, and one menu sink, forwarding relevant
events into the internal UCC core. The shared base JSON is parsed once. Race catalogs
also provide the authoritative ordinary-creature availability gate: `playable=true`
enables UPC/UCC runtime controls, while `spellHand` remains independent metadata.

The packaged race catalogs include human-readable `name` and `editorID` fields and are
fully enabled for this test package. These metadata fields do not alter runtime lookup.

This is not yet a stable release; validate it in-game before promotion.

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
  "PlayableHumanoidSpellHand": "Both"
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
      "spellHand": "Left",
      "hideEquippedWeapon": true,
      "hideSheathedWeapon": true
    }
  ]
}
```

`playable: false` leaves the race unavailable in RaceMenu and disables ordinary UCC controls for that catalog race while retaining independent metadata such as `spellHand`. This is the release-safe default. `spellHand` accepts `Left`, `Right`, or `Both`. If a creature has no catalog row or no valid `spellHand`, UPC leaves its spell equip side unrestricted rather than applying an origin-based guess.

JSONC-style comments are accepted. Invalid or unresolved rows are isolated and logged instead of preventing the remaining files from loading.

The established catalog inventories are 122 Skyrim/Dawnguard/Dragonborn creature and variant races, 223 Oblivion-converted races, and 68 Morroblivion races, for 413 cataloged race records total. Ordinary vanilla playable humanoid races do not require catalog rows for the optional `PlayableHumanoidSpellHand` setting.

## Data-driven attack-family profiles

Converted attack naming is not hard-coded into the DLL. A race with `useCombatWorkaround: true` may select a named profile with `attackFamily`, for example `"attackFamily": "OblivionConverted"`. Attack-family definitions live in the dedicated directory `Data/SKSE/Plugins/UniversalPlayableCreatures/AttackFamilies/`; each profile file must end in `.attackfamilies.json`.

UPC parses attack-family files once during the SKSE `kDataLoaded` catalog pass, before race catalogs are resolved. The parsed profiles and per-race assignments remain in memory for the rest of that Skyrim process. Attack-button input and save-game loads do not read or parse JSON from disk. Editing a profile while Skyrim is running therefore requires restarting Skyrim before the change is applied; UPC intentionally has no config watcher, polling loop, or per-attack file I/O.

The selected profile supplies converter-specific event prefixes, weapon-family markers, swimming prefixes, power/bash/block/counter markers, explicit attack exclusions, and whether matching attack events require compatibility redispatch. UPC still discovers the active race's real `BGSAttackData` at runtime; JSON only describes how those existing event names should be classified. Missing or unknown `attackFamily` does not fail the race: UPC falls back to generic race-local `BGSAttackData` discovery without converter-specific naming assumptions.

`excludedAttackMarkers` is a generic exclusion list. Any profile-matched attack whose event name contains one of these lowercased markers is retained as a special/non-primary record and cannot enter UPC's ordinary normal or power attack rotation. This is intended for converter families that coexist in a race's attack map but represent actions UPC must never select as melee—for example Fallout: New Vegas firearm, grenade, mine, launcher, or heavy-weapon animations.

The distributed profiles are:

- `Oblivion.attackfamilies.json` → `OblivionConverted`
- `Morroblivion.attackfamilies.json` → `MorroblivionConverted`
- `FalloutNV.attackfamilies.json` → `FalloutNVConverted`

The FNV profile allows `h2h`, `1hm`, and `2hm` as unarmed/one-handed-melee/two-handed-melee families. It excludes `1hp`, `2hr`, `2ha`, `2hh`, `2hl`, `1gt`, and `1md`, preventing pistol, rifle, automatic/heavy, launcher, grenade, and mine animation families from entering ordinary creature melee selection.

The distributed race catalogs currently use these defaults:

- Skyrim / Dawnguard / Dragonborn: `useCombatWorkaround: false`; no attack-family profile is required.
- Oblivion conversions: `useCombatWorkaround: true` and `attackFamily: "OblivionConverted"`.
- Morroblivion conversions: `useCombatWorkaround: true` and `attackFamily: "MorroblivionConverted"`.
- FNV races should use `attackFamily: "FalloutNVConverted"` when an FNV race catalog is supplied. No FNV race catalog is shipped until its race records/FormIDs are provided and verified.

Race rows are kept in case-insensitive alphabetical order by human-readable `name` where practical, with `editorID` used to make duplicate display names deterministic. Fields are written in a stable readable order so custom catalog authors can copy and edit rows directly.

Example race row:

```json
{
  "name": "Minotaur",
  "editorID": "TES4MinotaurRace",
  "race": "Oblivion.esm|XXXXXXXX",
  "playable": true,
  "spellHand": "Right",
  "useCombatWorkaround": true,
  "attackFamily": "OblivionConverted",
  "hideEquippedWeapon": false,
  "hideSheathedWeapon": true
}
```

Example profile skeleton:

```json
{
  "attackFamilies": [
    {
      "name": "MyConvertedFamily",
      "eventPrefixes": ["attackStart_MyConverter_"],
      "redispatchMatchedEvents": false,
      "weaponFamilies": {
        "oneHand": ["onehand"],
        "twoHand": ["twohand"],
        "unarmed": ["handtohand"],
        "bow": ["attackbow"],
        "staff": ["attackstaff"]
      },
      "swimmingPrefixes": [],
      "powerMarkers": ["power"],
      "bashMarkers": ["bash"],
      "blockAttackMarkers": ["blockattack"],
      "counterAttackMarkers": ["counterattack"],
      "excludedAttackMarkers": ["not-a-primary-attack"]
    }
  ]
}
```

## Runtime feature notes

### RaceMenu protection

The RaceMenu safeguards resolve both hook targets through Address Library (`REL::ID(26988)` and `REL::ID(26993)`) instead of fixed SkyrimSE.exe RVAs, while retaining byte-signature validation before either hook is installed. On AE 1.6.1170 these IDs resolve to the previously validated RVAs `0x432550` and `0x432D50`. Playable flags are applied only to catalog rows explicitly set to `playable: true`.

### Camera node

For creature players, UPC injects `Camera3rd [Cam3]` only when needed. Race changes, player 3D rebuilds, RaceMenu transitions, and mapped POV input provide event-driven synchronization; no camera polling loop is used.

### Hand equipment while drawn

For active UPC creatures, `TESEquipEvent` is only an equipment-activity notification. UPC defers one authoritative `GetEquippedObject(left/right)` comparison against its runtime hand cache; Inventory, Magic, and Favorites simply postpone that settled comparison until menu close. When a real hand change occurs while the ActionEvent-derived creature ready state is drawn, UPC runs the established sheathe -> `EndSheathe` -> deferred redraw graph reset. v0.2.19 and later snapshot the complete authoritative post-change state of both hands as the transaction target, so actor-wide sheathe side effects cannot permanently discard the newly selected weapon or spell. Internal equipment events generated by that owned transaction are consumed rather than recursively starting another reset. No polling, timers, save serialization, `IsWeaponDrawn()` decision logic, or NiNode equipment detection is used.

### Spell hand restriction

UPC temporarily changes the equip slot of known ordinary spells while a restriction is active and restores the original slots when it is not. Nothing is serialized into the save. Creature policy comes from the current race's catalog row; humanoid policy comes only from `PlayableHumanoidSpellHand`.

Concentration spell behavior remains a known limitation of creature casting and should not be assumed equivalent to normal humanoid concentration casting.

### Universal creature controls

The merged controls core preserves the established data/event-driven model: race-local `BGSAttackData` is discovered on race activation, physical attacks use authored attack events and authentic HitFrames, and action-scoped payloads are cleared on termination/interruption. Converted TES4 attack-family compatibility remains isolated from unrelated native attack families.

Werewolf, Vampire Lord, and Werebear retain their dedicated vanilla behavior systems and are excluded from ordinary UPC creature combat/traversal intervention; the established crafting interception exception is preserved.

### Weapon visibility

Weapon visibility is configured per race in the race catalogs. `hideEquippedWeapon: true` hides the equipped weapon mesh in both drawn and sheathed states while leaving the item mechanically equipped. `hideSheathedWeapon: true` hides the mesh only while the weapon is sheathed. The two old global weapon-visibility switches have been removed from `UniversalPlayableCreatures.json`.

The race-catalog values are authoritative; UPC no longer scans attack families to infer whether a weapon should be visible. Missing or invalid weapon-visibility fields safely default to `false`. The distributed catalogs mark creatures that do not normally use equipped weapons as hidden in both states, while weapon-capable races remain visible by default. Known broken sheath placement can be handled independently; the Oblivion Minotaur defaults to `hideEquippedWeapon: false` and `hideSheathedWeapon: true`.

Only the controls core owns runtime weapon-clone culling in the merged DLL. The older independent UPC sheathed-weapon implementation is not simultaneously active, preventing two subsystems from competing to hide/restore the same `BipedAnim` clones.

## Install layout

```text
Data\SKSE\Plugins\UniversalPlayableCreatures.dll
Data\SKSE\Plugins\UniversalPlayableCreatures.json
Data\SKSE\Plugins\UniversalPlayableCreatures\Skyrim.json
Data\SKSE\Plugins\UniversalPlayableCreatures\Oblivion.json
Data\SKSE\Plugins\UniversalPlayableCreatures\Morroblivion.json
Data\SKSE\Plugins\UniversalPlayableCreatures\AttackFamilies\Oblivion.attackfamilies.json
Data\SKSE\Plugins\UniversalPlayableCreatures\AttackFamilies\Morroblivion.attackfamilies.json
Data\SKSE\Plugins\UniversalPlayableCreatures\AttackFamilies\FalloutNV.attackfamilies.json
Data\SKSE\Plugins\UniversalPlayableCreatures\AttackFamilies\Skyrim.attackfamilies.json
```

Expected log:

```text
Documents\My Games\Skyrim Special Edition\SKSE\UniversalPlayableCreatures.log
```

For the merged build, remove/disable a separately installed `UniversalCreatureControls.dll` so the same controls are not registered twice.

## Development status

v0.2.20 is the current test candidate. v0.2.19 runtime testing validated the revised hand-equipment graph-reset target preservation across repeated spell changes while a weapon remained equipped, and also validated crafting occupancy cleanup on Crafting Menu close. v0.2.20 leaves those runtime paths unchanged and only cleans catalog handling for source plugins that are not loaded.

## License

Source available — All Rights Reserved. This project is not open source. See `LICENSE` for permitted uses.


### v0.2.9 hand-equip redraw timing
For a supported creature that was already drawn/ready before a hand-equipment change, UPC preserves the v0.2.8 event-driven sheathe transaction but defers the restore-draw request by one SKSE game task after `EndSheathe`. This path is not used when the creature was initially sheathed.


### v0.2.11 unified hand-change observation
Hand-equipment preservation now uses the already-registered player `NiNodeUpdateEvent` as its sole change notification. UPC compares the actor's actual left/right equipped forms against a two-FormID runtime cache. Inventory, Magic, and Favorites only suppress this comparison while open; the first player node update after close uses the same path as an outside-menu equip. If a real hand change is observed while the ActionEvent-derived creature ready state is drawn, UPC reuses the proven sheathe -> `EndSheathe` -> one deferred redraw transaction. No hotkey inference, menu snapshot task, polling, timer, serialization, MinHook, or equipment detour is used.


### v0.2.12 TESEquipEvent hand-change observation
Hand-equipment preservation now uses `TESEquipEvent` from `ScriptEventSourceHolder` as the sole equipment-activity notification. Both equip and unequip events are accepted. The event's `baseObject` is diagnostic only; UPC defers one authoritative `GetEquippedObject(left/right)` comparison against the cached hand FormIDs and acts only when a hand actually changed. Inventory, Magic, and Favorites do not create a separate detector: while one is open the event sets a pending flag, and menu close queues the same one-shot settled-hand comparison. Outside menus the equip event queues that comparison immediately. The ActionEvent-derived creature ready latch is the only ready-state authority for this feature; `IsWeaponDrawn()` is not used to decide whether a hand change needs preservation. If a changed hand was ready, the validated force-sheathe -> `EndSheathe` -> deferred redraw -> `EndDraw` transaction remains unchanged. `NiNodeUpdateEvent` remains only for weapon-visual resynchronization and no longer participates in equipment detection. The first test build logs every raw `TESEquipEvent` before filtering.


### Per-race combat workaround

Race catalogs support `useCombatWorkaround`. This boolean is the authoritative per-race switch for UPC's compatibility combat/casting bridge. `true` opts the race into universal compatibility controls; `false` leaves its combat/casting on Skyrim's native path while retaining independent UPC features such as playability, camera, spell-hand metadata, weapon visibility, and crafting interception. Missing or invalid values default to `false`. The shipped Skyrim/Dawnguard/Dragonborn catalog uses `false`; shipped converted Oblivion and Morroblivion catalogs use `true`. Users and third-party catalog authors may override the value per race regardless of origin.

### v0.2.19 hand-equipment reset behavior
Hand changes made while a supported creature is ready still use the event-driven sheathe -> EndSheathe -> redraw graph reset. The reset now preserves the complete authoritative post-change state of both hands as its transaction target, so actor-wide sheathe side effects cannot permanently discard a newly equipped weapon or spell.


### v0.2.20 unloaded source-plugin catalog handling
Race rows whose source plugin is not currently loaded are now skipped as a normal condition. UPC reports at most one informational line per unloaded plugin per catalog instead of one unresolved-race warning for every row. If the source plugin is loaded but a listed race still cannot be resolved, the existing warning remains, preserving diagnostics for genuinely bad or stale catalog entries. Plugin-loaded status is cached during each one-time catalog pass; no runtime polling is added.
