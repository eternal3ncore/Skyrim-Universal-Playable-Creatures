# Universal Playable Creatures 0.2.26

Universal Playable Creatures (UPC) is an SKSE plugin for Skyrim Special Edition / Anniversary Edition that makes creature races usable by the player and provides runtime support for creature-specific controls that Skyrim's normal player systems do not reliably handle.

## Feature overview

- Data-driven playable creature race catalogs for Skyrim and converted creature sets.
- Universal creature melee and power-attack handling using each race's own `BGSAttackData`.
- Attack-family profiles that classify compatible attack events without hardcoding individual races into the DLL.
- Creature spell-hand policy (`Left`, `Right`, or `Both`) configurable per race.
- Creature blocking, spell/shout fallback, crafting interaction, and traversal support where the native player route is unavailable.
- Third-person creature camera-node support.
- Configurable equipped/sheathed weapon visibility for creature races.
- RaceMenu safeguards for creature-player use.
- Event-driven runtime design; the normal control paths do not rely on background polling loops.

## Bug fixes and workarounds

Skyrim's player controls assume humanoid behavior graphs. Many native and converted creature races either do not expose the expected player action branches or expose attacks under creature-specific animation events. UPC can enable a combat workaround per race that discovers the active race's own attack data and dispatches compatible creature attack events while preserving native behavior where possible.

Attack-family JSON files provide the event-family rules used by that workaround. This keeps converter/game-specific naming outside the core DLL and allows classification fixes to be made without hardcoding individual creature races.

UPC also contains runtime workarounds for creature RaceMenu use, third-person camera placement, spell-hand restrictions, and weapon presentation where Skyrim's humanoid assumptions produce incorrect behavior.

## Race JSON catalogs

Race catalogs are located in:

```text
Data/SKSE/Plugins/UniversalPlayableCreatures/
```

Examples include `Skyrim.json`, `Oblivion.json`, `Morroblivion.json`, and `FalloutNV.json` when that converted set is installed.

Each race entry defines its UPC policy. Important fields include:

- `playable` — whether UPC applies Skyrim's playable flag to the race at startup.
- `spellHand` — `Left`, `Right`, or `Both`. Use `Both` when no restriction is known to be necessary.
- `useCombatWorkaround` — enables UPC's creature attack/control workaround for that race.
- `attackFamily` — names the attack-family profile used to classify the race's attack events.
- `hideEquippedWeapon` / `hideSheathedWeapon` — controls creature weapon-mesh visibility.

Keep fields explicit rather than relying on omitted values. Changes to the JSON catalogs require a full Skyrim restart because UPC loads its configuration at startup.

## Attack-family JSON

Attack-family profiles are located in:

```text
Data/SKSE/Plugins/UniversalPlayableCreatures/AttackFamilies/
```

Files use the `.attackfamilies.json` suffix. A race's `attackFamily` value must exactly match a profile `name` defined in one of these files.

Profiles describe converter/game-specific attack naming, including weapon-family markers, swimming events, power attacks, bashes, block/counter attacks, exclusions, and whether matched converted events need redispatch. UPC still uses the active race's own `BGSAttackData`; the family profile tells UPC how those discovered events should be interpreted.

Markers should be specific enough to identify the intended event family. For converted TES4-style events, full family prefixes are preferable to short ambiguous substrings when exclusions could overlap another valid attack name.

## Installation

Install the contents of the packaged `Data` folder into Skyrim's `Data` directory so the layout is:

```text
Data/SKSE/Plugins/UniversalPlayableCreatures.dll
Data/SKSE/Plugins/UniversalPlayableCreatures.json
Data/SKSE/Plugins/UniversalPlayableCreatures/*.json
Data/SKSE/Plugins/UniversalPlayableCreatures/AttackFamilies/*.attackfamilies.json
```

SKSE64 is required. Install the appropriate Address Library for the Skyrim runtime in use.

If an older standalone `UniversalCreatureControls.dll` is installed, remove or disable it before using the merged UPC plugin so the same creature controls are not registered twice.

The runtime log is written to:

```text
Documents/My Games/Skyrim Special Edition/SKSE/UniversalPlayableCreatures.log
```

## License

Source available — All Rights Reserved. See `LICENSE` for permitted uses.
