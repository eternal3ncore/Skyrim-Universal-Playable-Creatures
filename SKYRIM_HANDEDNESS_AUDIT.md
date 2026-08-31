# Skyrim Creature Spell-Hand Audit

Date: 2026-08-31

Source corpus: user-supplied `All Behaviors(1).zip`, containing 660 behavior files from Skyrim/Dawnguard/Dragonborn behavior data.

## Decision

UPC v0.2.0 uses **Left** as the default spell hand for the 122 packaged Skyrim/Dawnguard/Dragonborn creature races. Explicit runtime-confirmed or stronger behavior-routing evidence may override that default per race.

This is an educated default, not a claim that every Skyrim creature has a native left-hand MagicCaster lifecycle. UPC's hand restriction and UCC's fallback spell backend remain separate concerns.

## Static evidence from the supplied corpus

A first-pass marker audit found no behavior file with `BeginCastRight` without `BeginCastLeft`. The following files exposed only `BeginCastLeft` among those two markers:

- `atronachstormbehavior.hkx`
- `benthiclurkerbehavior.hkx`
- `chaurusbehavior.hkx`
- `chaurusflyerbehavior.hkx`
- `chickenbehavior.hkx`
- `draugrbehavior.hkx`
- `dwarvenspiderbehavior.hkx`
- `frostbitespiderbehavior.hkx`
- `harebehavior.hkx`
- `mudcrabbehavior.hkx`
- `steambehavior.hkx`

Files exposing both `BeginCastLeft` and `BeginCastRight` included:

- `0_master.hkx`
- `0_master (1).hkx`
- `atronachflamebehavior.hkx`
- `dragon_priest.hkx`
- `falmerbehavior.hkx`
- `havgravenbehavior.hkx`
- `hmdaedra.hkx`
- `magicbehavior.hkx`
- `magicbehavior (1).hkx`
- `magicmountedbehavior.hkx`
- `magicmountedbehavior (1).hkx`
- `sprigganbehavior.hkx`
- `vampirelord.hkx`
- `wispbehavior.hkx`

The presence of both bridge symbols is **not** classified as `Both`; prior Oblivion research established that generic left/right bridge machinery alone does not determine the authored/effective action lane. Runtime evidence and Actor Action/IDLE routing outrank marker presence.

## Runtime policy

Current precedence:

1. Runtime-confirmed per-race result.
2. Strong behavior/action-routing detection.
3. Packaged Skyrim default: Left.

The default can be corrected per race without recompiling UPC by changing that race's `spellHand` value in the namespaced race catalog.
