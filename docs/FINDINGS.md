# ootmenu findings

Everything verified while building ootmenu, written so the next person doesn't have to rediscover it. Each solved
problem lists the symptom, the cause, the fix, how it was found, and the approaches that **don't** work.

Applies to **Ocarina of Time (Switch 2), program `0400486024DEC000`, update v131072, main build
`E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348`**, running in **Lichii** (an Eden/yuzu fork, Windows). Other versions
have different addresses.

Most of this was found with ootmenu, the Python overlay sloperinaGZ replaced; its files (`game.py`, `patch.py`,
`gamedata.py`, `guestmem.py`, the GDB tools) are named below where they're how something was found. They're
archived outside the project (`ootmenu-python-overlay-2026-11-09.tar.gz`); the native code is in `src/ootgz`.

Conventions: `main+0x…` is an offset into the game's main module. Field hashes are MurmurHash3_32 (seed 0) of the
field name (`gamedata.murmur3`). "The patch" is ootmenu's code patch (`patch.py`, source in `tools/hook_asm.py`).

## Contents

1. [Memory and save data](#1-memory-and-save-data)
2. [Game files](#2-game-files)
3. [The code patch](#3-the-code-patch)
4. [Game functions used](#4-game-functions-used)
5. [Solved problems](#5-solved-problems)
6. [Things that do not work](#6-things-that-do-not-work)
7. [How to investigate](#7-how-to-investigate)
8. [Open problems](#8-open-problems)
9. [Leads: pausing, debug leftovers, scene managers](#9-leads-pausing-debug-leftovers-scene-managers)

---

## 1. Memory and save data

### Finding the game
- Guest (Switch) addresses map linearly into Lichii's process: `host = guestbase + guest`. `game.Game` finds
  `guestbase` and the main module base (`game.M`, changes every launch) and checks them against a known pointer
  (`CHECK_OFF`/`CHECK_VAL_OFF` in `game.py`).
- Main's code ends at `main+0x3c461d0`; rodata starts at `main+0x3c47000`. The gap is mapped executable and
  zero-filled (used for the patch, see §3).

### GameData (the live save fields)
- All saved state lives in GameData fields, keyed by the hash of the field name. The full list (~15,000 fields with
  types and defaults) ships in the game: `romfs/GameData/GameDataList.Product.110.byml.zs`. Exported to
  `data/gamedata_fields.json`. About 4,600 names were recovered by hashing every string in the game's scripts and data;
  ~3,600 more fields have readable titles from the onif save editor (`data/all_fields.json`, mapped by diffing exports).
  2,568 more names come from the game's memory (`data/memory_names.json`, see [Names from memory](#names-from-memory)).
- GameData keeps a **type list** of `(type vtable, count, objects, hash table)` entries, one per field type. The
  hash table maps field hash → object index. Objects of one type are a fixed size apart:
  - Int / Float: 0x28 bytes, value at `+0x1C`, **copy at `+0x20`** (write both).
  - Enum: 0x38 bytes, value (MurmurHash3 of the choice name) at `+0x1C`, copy at `+0x20`.
  - Bool: 0x18 bytes, value byte at `+0x16`, copy at `+0x17`.
  - String64: 0x108 apart (`game.STR_STRIDE`); ootmenu reaches a few by index (`game.STR_IDX`): `sword` 0x17 =
    `PlayerStatus.EquipWeapon`, `shield` 0x16 = `EquipShield`, `tunic` 0x10, `boots` 0x12, `ocarina` 0x15 =
    `EquipOcarina`, `map` 0x1F = current map.
- Live access: `game.Game` — `get_field/set_field` (Int by hash), `get_bool/set_bool`, `get_typed/set_typed`
  (`"enum"`, `"float"`), `get_str/set_str` (String64 by `STR_IDX` key).

### Field types that matter
- **Struct** fields group others (`PlayerStatus`, `ActionGuide_IsEnd`, `DungeonInfo.<Dungeon>`); members are
  separate fields named `Struct.Member`.
- **BoolExp** fields are formulas the game evaluates (e.g. "quest at or past stage X"); they can't be written.
- **Enum** values are hashes of choice names; the choices are in `data/gamedata_fields.json` (`"v"`).

### Saved or not, and what resets a field
- Every field object starts with the same header: `+0x00` its type's vtable, `+0x08` reset bits (u32), `+0x0C` a
  byte that changes during play (state, not a setting), `+0x10` the **save file index** (s32): -1 = not saved,
  0 = the save slot, 1 to 3 = other save files (`IsLastSelectedMQMode` is 3). Read from all 12,947 Bool, Int, UInt,
  Float, Enum, Vector3 and String64 fields; the counts below were the same in two emulator sessions two days apart.
  Struct, BoolExp, the arrays and the 64-bit types weren't read.
- **Bit 0 of the reset bits looks like "cleared on an area change"**: it's on the gossip stone fairies
  (`BlewOcarinaByGossipStone.GOSSIP_*`, one fairy per visit), battle states (`DekuScrubBrothers_IsBattle`) and
  picked bomb flowers. Inferred from which fields have it, not tested. Bit 3 is on nearly everything, saved or not;
  bits 1, 2, 4, 5 and 8 aren't understood.

Bools by setting:

| Reset bits | Save | Bools | Examples |
|---|---|---|---|
| 0x08 | 0 | 9338 | most progress: `IsGet.*`, `HavePlayedEvent.*` |
| 0x08 | -1 | 444 | `ActionGuide_IsRequestOcarina` |
| 0x18 | 0 | 341 | (unnamed; Ints with 0x18 are `DefeatedEnemyNum.*`) |
| 0x09 | 0 | 288 | `DreamWorld_ArriveFromKakarikoVillage`, `DungeonWarp_Canceled` |
| 0x09 | -1 | 185 | `DekuScrubBrothers_IsBattle` |
| 0x01 | -1 | 128 | `Cow_IsGetMilkFromCow` |
| 0x21 | -1 | 114 | `Npc_DekuTreeChild_GlassTruth` |
| 0x120 | 0 | 102 | (unnamed) |
| 0x19 | -1 | 100 | (unnamed) |
| 0x108 | 0 | 52 | `FairySpiritAppear_HrlField_Note` |
| 0x01 | 0 | 46 | `BlewOcarinaByGossipStone.GOSSIP_*` |
| 0x00 | -1 | 35 | `Sequence_IsAlreadyResetForLoadSave`, `DebugParam.*` |
| other | | 119 | |

- **Saved to the slot and without bit 0 = progress**: 9,869 Bools, 1,119 Ints, 87 UInts, 83 Enums, 76 Floats,
  29 String64s and 12 Vector3s. Checked at a gossip stone: `HavePlayedEvent.GossipStone_D_T` and
  `HaveTalked_Special.GossipStone_Talk_Event3` are saved and kept, `GossipStone_NaviTalk_Temporary` isn't saved.
  The play time counters `0xe573f564` and `0xe53eb6d1` (UInt, +1 a second) are saved too.
- In `data/gamedata_fields.json`: `"r"` (reset bits) and `"s"` (save file index) for those fields. The save fields
  reference shows them and has a **progress only** filter.
- **Found by:** reading every object of the seven type sections from outside the game, grouping them by these
  values and checking the groups against their names.

### Names from memory
- `data/memory_names.json` (`{hash: [name, type, grade]}`) adds 2,568 names. They're merged into
  `data/gamedata_fields.json` (`"ns"` says where each came from; the reference shows it on hover), and
  `tools/re64.py` reads the file after `rom_names.json`. With the ~4,600 from the game's files, 7,161 of the 14,997
  fields are named.
- **How they were found:** every identifier-like string in the running game's heap and in main (about 5 million)
  hashed against the field hashes, plus the game's own patterns filled in with names seen in memory
  (`IsGet.<item>`, `PouchData.<item>.StockNum`, `HavePlayedEvent.<event>`). Mostly `HavePlayedEvent.*` (about
  2,100), then `PouchData.*`, `HavePlayedGetEvent.*` and `IsGet.*`.
- **Grades:** *sure* = a whole string in main, or in a logic graph (AINB) or BYML file within 0x400 bytes of a
  `GameData*` key (for `HavePlayedEvent.X`, a key ending in `EventName`); *likely* = a whole string in a logic graph
  or BYML file, or a name made of words (8+ characters, two words) anywhere else. 98 are sure and 2,470 likely.
  Short or random-looking matches (*unsure*) were left out.
- **Chance matches:** with about 5 million strings against 15 thousand hashes, around 17 collisions are expected.
  Three names in `rom_names.json` were collisions where memory had the real name, and `gamedata_fields.json` now has
  the real ones: `0x4147bf54` `yuzN` is `MiniGame_Diving_ShilverScale_First`, `0xa385357f` `WMb:` is
  `HavePlayedEvent.Npc_Hylia_040_Child_D_N`, `0x54b613cf` `XN1wJ` is `LakeHylia_OverlapSharkAppearArea`. These
  still look like collisions, with no real name found yet: `zSA` (0x82daaab7), `lddC` (0x826db2f9), `6fwL`
  (0xbc8e1794), `iDr4` (0x6310157c), `zsbm` (0x7594791b), `KQigA` (0xe4075522), `f3xXn` (0x833e9356), `kz5L`
  (0xb42e40db), `mooona` (0xf0aad6fa).

### Save files
- `%APPDATA%\eden\nand\user\save\0000000000000000\<user id>\0400486024DEC000\slot_NN\progress.sav`: from offset
  0x20, `(hash u32, value u32)` pairs; for String64 fields the value is the file offset of a 64-byte text slot. No
  checksum. `caption.sav` holds the location and the save date (also hash/value pairs). The game reads the captions
  once, at boot, and keeps them in its CaptionMgr (§9.11); `slot_06`..`slot_11` are the Master Quest slots.
- Saving writes back what Link actually holds (e.g. `PlayerStatus.Equip*`).

### Useful name patterns
- `PouchData.<item>.StockNum` — count owned. `IsGet.<item>` — "ever acquired" flag. An item needs both to show on
  the item screen. The L/R quick menus only open with two or more items acquired.
- `PlayerStatus.EquipWeapon/EquipShield/EquipArmor/EquipBoot/EquipOcarina` (String64): what Link wears. **These
  follow pause-menu equips live** (not only on save).
- `HavePlayedEvent.<event>`, `HaveTalked_Special.<npc>_<event>`: event history. `Step_<quest>`: quest stage (Enum;
  setting the stage works after a loading zone; setting "played event" flags alone does nothing).
- `ActionGuide_IsEnd.<guide>`: tutorial screens seen. **The D-pad only exists once `ActionGuide_IsEnd.Navi` is set**
  (normally on entering the Deku Tree).
- `DungeonInfo.<Dungeon>.IsGetMap / IsGetCompass / IsGetBossKey` (Bool). Dungeon names: `InsideTheDekuTree`,
  `DodongoCavern`, `InsideJabuJabuBelly`, `ForestTemple`, `FireTemple`, `WaterTemple`, `ShadowTemple`,
  `SpiritTemple`, `BottomOfTheWell`, `IceCavern`, `GerudoTrainingGround`, `InsideGanonCastle` (same as the map name).
- **Item buttons**: four Enum fields with unrecovered names — `0x6bee602d`, `0x76f85e6d`, `0xa0029837`,
  `0xbc32ce24`. Their choices are the assignable items (`Empty`, `DekuStick`, `SlingShot`, `Bow`, `HeavyBoot`,
  bottles, masks, …). Found by searching Enum fields whose choice list holds `SlingShot`; confirmed live.
- `SimpleIntForMessage`, `…2`, `…3`, `SimpleHourForMessage`, `SimpleHourForMessageAs24Hour`, `SimpleMinForMessage`:
  message-formatting scratch Ints, never saved. **The game writes them** (e.g. its save-date text at
  `main+0x33cc94c` puts year/month/day/hour/minute there), see §5.3.

### Magic
- The real current magic lives in the magic meter object (holds the hashes of `Current magic` / `Magic capacity` at
  +0x38/+0x3C, value at +0x44). Every frame the game writes `clamp(value, 0, capacity)` into the save field, so
  writing only the field is undone (`game.set_magic` writes both). Capacity 0 = no meter; 240 normal, 480 double.

## 2. Game files

- The game ships as a `.nup` package (layout from Hookshot's `nx2/nup.py`); `tools/nupfs.py` lists/extracts it.
- Most romfs files are zstd with dictionaries from `Pack/ZsDic.pack.zs`; BYML files read with `tools/byml.py`.
- Event scripts `Event/EventFlow/*.bfevfl` (format v0.4) decode with the `evfl` Python package;
  `tools/evdump.py` lists what each flow sets and checks. **This is the fastest way to learn how the game itself
  does something** (e.g. which node and parameters a cutscene uses to give an item).
- **Dialogue and flags in event scripts** (1.1.1; read for the flag log, `tools/flaglog.py`): 914 `.bfevfl` files in
  `Event/EventFlow/`, 653 of them with a flowchart. A spoken line is an `EventTalk` action with
  `MessageId: EventFlowMsg/<file>:<label>` (10,158 of them); its speaker is the action's actor, and
  `Npc_EventStarter` is the NPC Link talked to. Flags are set by `GameDataActor` actions with a `GameDataName`:
  mostly `EventTriggerSetGameDataBool` (3,058), then `…SetGameDataInt`, `…AddOrSubGameDataInt`, `…CopyGameDataInt`
  and others, 1,902 different fields in all. Quest stages are moved by challenge steps instead:
  `ChallengeSystemActor.EventExecuteProgressChallengeStep` (287) with `ChallengeName: X, StepName: Y` sets
  `Step_X` to `Y`. 257 of them name a story menu quest and one of its stages (`SageOfWater` / `Step04`, set by
  `SageOfWater` › `IceCavern_WaterSerenade`); the rest are minigames and the like. There are also 39
  `EventExecuteRollbackChallengeStep`. Only 21 of the field writes are `HavePlayedEvent.<event>`, so the game sets
  the rest itself when an event plays. Of the 2,261 `HavePlayedEvent` names, 860 are a script or entry point name, and 883 more start with a script's name
  (`Npc_Kokiri_Mido_Adult_C1_N`).
- **Messages:** `Mals/<lang>.Product.110.sarc.zs` (`EUen`, `USen`, ...), a SARC of MSBT files (`EventFlowMsg/`,
  `LayoutMsg/`, `ChallengeMsg/`, ...). The text is **UTF-8** (header byte 0x0C = 0), not the usual UTF-16. A
  control tag is `0x0E`, u16 group, u16 type, u16 size, then its parameters. Seen: (0,4) a page break, (0,2)/(0,3)
  size and colour, (2,n) a game value (a u16 length, then the GameData name, e.g. `SimpleShop_Price`), (4,0) an
  emotion (`Angry`), (5,n) pauses, (201,n) item-name forms. Link's name is plain text.
- **Pause screen layout:** `romfs/UI/Pouch/PouchTable/PouchTable.game__ui__PouchTable.bgyml` (BYML) lists every
  slot of the item, equipment and collection screens with its pouch items: `ItemArray` (item screen, `Pa_ItemIcon_00`
  onward: sticks, slingshot, boomerang, nuts, bombs, bombchus, hammer, hookshot/longshot, bow, fire/ice/light arrows,
  beans, Din's/Farore's/Nayru's, 5 bottles, scrub seed, one trade-item slot, amiibo), `EquipmentArray` (swords,
  shields, tunics, Stone of Agony, Lens of Truth, masks, boots), `CollectArray` (Skulltula tokens, Gerudo card, magic,
  double defense, gauntlets, scale, wallet and bags), `OcarinaArray`, `StoneArray`, `MedalArray`, `NoDisplayArray`
  (deku seeds, heart pieces). Slots holding an upgrade chain list it highest first. ootmenu's items list follows
  `ItemArray`.
- **The amiibo masks**: `Armor_Child_Mask_Amiibo0` (the Fairy-Kid Mask, Link's amiibo) and `…Amiibo1` (the
  Princess Mask, Zelda's), names from `Mals/*/ActorMsg/PouchContent.msbt`. Same actor and pouch setup as the
  shop masks (`Armor_Mask_Base`, `MaskDefault`), but their own `EquipmentArray` slot (`Pa_HeadEquip_03`, both in
  it, `SpecialDeal: amiibo`) beside the shop masks' (`Pa_HeadEquip_02`), and no choice of the item buttons' Enum
  (its `Amiibo` is the amiibo item, `Pouch_Amiibo`). Held as the others are: `PouchData.<name>.StockNum` and
  `IsGet.<name>`. The menu's **amiibo mask** sets those two, one mask at a time.
- `tools/re64.py` does static analysis on a dump of main (`main.bin` = raw image from main's base, `main.base` = its
  load address in hex): string/function cross-references (`xref`), disassembly with field names (`dis`, `func`).

## 3. The code patch

Lichii applies IPS/IPS32 patches from
`%APPDATA%\eden\load\0400486024DEC000\ootmenu\exefs\E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348.ips` **at game start
only** (it translates and caches code; a running game never sees a new patch). Offsets in the file are main
offsets + 0x100 (module header). The menu installs/updates the file on start (`patch.install`) and checks the
running game's memory to see whether the current version is active (`patch.active`); it shows "patch updated,
please restart the game" until it is.

### Layout
- Code cave: `main+0x3c46400`..`0x3c47000` (Lichii's own fixes use `0x3c46200`–`0x3c46278`). Marker
  `OOTMENU\0` + version at `0x3c46400`, hook code at `0x3c46440`, item names (32 bytes each) after it.
- Hook site: every frame `main+0x3b500a4` locks GameData, applies its command queue (`main+0xb99980`) and tail-calls
  the unlock at `main+0x3b500dc`. The patch redirects that tail call to the hook, which unlocks, then handles one
  request. So requests run **on the game's thread, once per frame, while the game is running** (not while paused by
  the emulator).

### Request protocol (`patch.py`)
The menu writes a request into the mailbox Int `SimpleIntForMessage3` (`0xE670579A`); the hook reads it with the
game's GetInt, and if it's tagged, clears it with SetInt and acts:

```
value = 0x0CA7 << 16 | remove << 9 | IsEquipDirect << 8 | item      item = 1..len(ITEMS)
value = 0x0CA7 << 16 | 0x7F                                         call request
```

- **Give** (`patch.request_item`): AddPouch exactly as the `OneShotAddOrRemovePouchContent` event node does:
  `main+0x110d570(*[main+0x4c7e0d0], &name, 0, 1, 0, 0, 0, IsEquipDirect, [sp]=0)`, then its notify
  `main+0x110d0b0(*[main+0x4e6b718], &name, ok, 0)`. `name` is `{char* top; s32 size = 0x80; char buf[]}`.
- **Remove** (`remove=True`): `main+0x33a4fa0(pouch manager, &name, 1, 0, 0, 0)`, what the same node does for a
  negative count (at `main+0x28b5140`).
- **Call** (`patch.call_function(game, fn, x0, x1)`): the game calls `main+fn(x0, x1)` once. Function and arguments
  are passed in the other message scratch Ints (written immediately before the request).
- `patch.ITEMS` is the name table baked into the patch; **its order is the item numbering**, so changing it means a
  new patch version and a game restart.
- **Timing:** there is one request slot, and a request only runs while the game runs (the menu pauses the
  emulator). So every multi-step job goes through one worker, `patch.submit(key, fn, ...)`, which runs jobs in
  order and keeps only the newest pending job per key (scrolling through wallet sizes runs just the last one).
  `patch.wait_handled` waits as long as the game is paused and only gives up after the game has run ~4 s without
  taking the request. Before this, overlapping jobs interleaved on the slot (e.g. two wallet changes removing each
  other's items) and steps were abandoned after 10 s with the menu open.
- After each item request the hook also sets the `SeqOneShotResetPlayerStatus` flags (harmless, but they don't
  redraw anything, see §6).

### Changing the patch
1. Edit `tools/hook_asm.py` (or `patch.ITEMS`), bump `patch.VERSION`.
2. `python -B tools/hook_asm.py` assembles it (needs `keystone-engine`, `capstone`) and prints the bytes; put them in
   `patch.HOOK_BYTES` and check it prints `matches patch.py: True`. Use `-B` and delete `__pycache__/patch*.pyc`
   between steps: a stale cached `patch` module once produced bytes for the old item table.
3. Restart the menu (installs the file), then restart the game.

## 4. Game functions used

| Address | What | Signature / notes |
|---|---|---|
| `main+0x163d20` | GameData GetInt | `(mgr, &out, hash)`; `mgr = *[main+0x4c7e180]` |
| `main+0x163b70` | GameData SetInt | `(mgr, value, hash)` |
| `main+0x110d570` | AddPouch (give) | see §3; negative counts do nothing |
| `main+0x33a4fa0` | Remove from pouch | `(pouch mgr, &name, count, 0, 0, 0)`; also re-applies Link's gear via `0xf201b0` |
| `main+0x2df6c80` | Create one equipment slot on Link | `(equipment component, slot)`; reads `PlayerStatus.Equip*`; slot `0xd` = ocarina. Doesn't take gear off |
| `main+0xf201b0` | Re-apply all of Link's gear | `(owner, 0)`; owner holds the component at `+0x470`; takes off empty slots |
| `main+0xa77ff0` | HUD screen method 98 | `(screen)`; redraws the HUD, turns the D-pad on |
| `main+0x33e12c0` | Map screen method 98 | `(screen)`; re-reads the dungeon flags |
| `main+0x837240` | UI manager: screen by hash | `(ui mgr, hash)`; HUD hash `0x5c7f1c3c` |

Objects:
- **Link's equipment component**: vtable `main+0x4b49698`, `+0x18` = Link's actor (`game.equip_component`; several
  actors have one, match the owner).
- **Its owner**: vtable `main+0x4bdfe68`, component at `+0x470` (`game.equip_owner`).
- **UI manager**: `*[main+0x4c7e098]`; screen count at `+8`, screen array at `+0x10`. **Every UI screen's virtual
  method 98 (`vtable+0x310`) is its "refresh contents"** — what opening it or unpausing runs.
  `patch.refresh_screen(game, vtable)` calls it. HUD (PlayerStatus screen) = vtable `main+0x4beb710`, holds the
  D-pad widget (vtable `main+0x4be1068`) at `+0x330`; pause map screen = vtable `main+0x4be8300`.

## 5. Solved problems

### 5.1 Ocarina shows on the D-pad but doesn't play
- **Symptom:** after giving the ocarina (pouch count, `IsGet`, `EquipOcarina`), the D-pad shows it but pressing the
  button does nothing. Area reloads, the ocarina tutorial flag, `IsEquipDirect` either way, and matching every
  tutorial flag to a working save made no difference.
- **Cause:** Link's usable gear is a separate object. Pouch data and `PlayerStatus.Equip*` are only data; the game
  creates the object with `main+0x2df6c80(equipment component, slot)`, which reads the slot's `PlayerStatus.Equip*`
  name (slot `0xd` = `EquipOcarina`). The full set is applied in slots 0, 6–10, 0xd–0x10 by `main+0xf201bc`.
- **Fix:** set `EquipOcarina`, give through the patch (Saria's way, `IsEquipDirect = 0`), then call
  `main+0x2df6c80(component, 0xd)` through the call request (`patch.give_gear`). Link then wears it at his waist.
- **Found by:** the string `EquipOcarina` is referenced once, in `main+0x2df6c80`; its callers apply every equip
  slot in turn.

### 5.2 D-pad / HUD doesn't redraw until a pause
- **Symptom:** after a give, the D-pad (and the ocarina on it) only appears after pausing.
- **Cause:** pausing/unpausing calls the HUD screen's virtual method 98 (`main+0xa77ff0`, via `main+0xd212c0`),
  which rebuilds the HUD and turns the D-pad on.
- **Fix:** call `main+0xa77ff0(HUD screen)` after the give (`patch.give_gear`).
- **Found by:** snapshot of the D-pad widget before/after a pause (bytes `+0x28` 0→2, `+0x2a` 0→1, `+0x189` 1→0),
  then GDB write watchpoints on them (`tools/dpadwatch.py`) caught the call chain from the pause.

### 5.3 Requests misfire / vanish
- **Symptom:** requests sometimes didn't run; the game sometimes "gave" an item by itself.
- **Cause:** the mailbox `SimpleIntForMessage3` is also written by the game's save-date text (day of month).
- **Fix:** tag requests (`0x0CA7 << 16`); the hook ignores and doesn't clear anything else (patch v6).

### 5.4 Unticking the ocarina leaves it with Link
- **Cause / fix:** AddPouch with a negative count does nothing; the game's node uses `main+0x33a4fa0` for removals.
  The single-slot call doesn't take gear off; the full re-apply `main+0xf201b0(owner, 0)` does once the slot is
  empty. So: empty `EquipOcarina`, remove (patch v8), re-apply, redraw the HUD, clear `IsGet`.
- **Found by:** event scripts with `Num=-1` (`tools/evdump.py`), then the node's code (`main+0x28b4a70`).

### 5.5 Unticking worn gear leaves Link wearing it
- **Fix:** if `PlayerStatus.Equip<slot>` is that item, empty it and call `main+0xf201b0(owner, 0)` + HUD redraw
  (`patch.unequip`). Gear itself stays plain data: the player equips it in the pause menu, which runs the game's
  own equip code.

### 5.6 Unticking an item leaves it on an item button
- **Fix:** set any of the four button Enums holding it to `Empty`, then redraw the HUD (`patch.clear_buttons`).
- **Gotcha:** the in-game item wheel keeps its own copy of the buttons and writes it back when it closes, so a clear
  done while it's open is undone. `clear_buttons` keeps watching for 2 minutes while the item stays unticked.

### 5.7 Upgrades (wallet, bags, quiver, strength, scale, magic, double defense) don't take
- **Symptom:** the capacity/depth number changes but the pause screen still shows the old item (e.g. Kokiri wallet
  with a 500 capacity).
- **Cause:** the game's screens and story checks use pouch items, not the numbers (e.g. the Lost Woods scrub runs
  `EventQueryIsExistInPouch(Item_Wallet_Medium)`).
- **Fix:** hold the matching item through the game's add/remove code (`patch.UPGRADES`, `patch.set_upgrade`) and still
  write the number. Item names: `Item_StickBag_*`, `Item_NutBag_*`, `Item_BulletBag_*`, `Item_BombBag_*`,
  `Item_Quiver_*`, `Item_Wallet_*` (Small/Medium/Large), `Item_Scale_Silver/Gold`, `Item_FairyBless_MP/MPDouble/
  DefenseDouble`, `Armor_Child_Gauntlet_Goron`, `Armor_Adult_Gauntlet_Silver/Gold`. Wallet verified in game.

### 5.8 Dungeon map / compass don't show
- **Symptom:** the map screen shows "???" after ticking the map.
- **Cause 1:** entering a dungeon through the menu's warp leaves no explored room registered, so the map screen shows
  "???" whatever you own. Entering through the door works. (Without the map the screen shows explored rooms; with
  it, the whole layout.)
- **Cause 2:** the map screen reads the flags when it opens. Ticking while it's open needs a refresh.
- **Fix:** the flags `DungeonInfo.<Dungeon>.IsGet*` are all that's needed; after a toggle the menu calls the map
  screen's method 98 (`main+0x33e12c0`, `patch.refresh_screen`).
- **Found by:** GDB read watchpoint on the `IsGetMap` value (`tools/readwatch.py`): the map screen reads it on open
  at `main+0x33e158c` and `main+0xb2a2c4`.

### 5.9 Levitate jerks Link about, can't steer, and once crashed the game
- **Symptom:** holding L3 shot Link upward and yanked him about; he couldn't be moved with the left stick; one
  session ended in a crash. The log showed two "moved Link with record" lines in the same instant, and about ten
  record searches within one second ending in "no record moved Link".
- **Cause 1 (jerks):** releasing and re-pressing L3 while the first press was still searching for Link's physics
  record started a second loop. Both loops checked the shared on/off flag, so both kept writing heights with
  different starting points.
- **Cause 2 (crash):** each overlapping press ran `move_link`'s search, which writes into candidate records. Writing
  look-alike records is what broke the camera and softlocked the game before (see "position" in `game.py`).
- **Cause 3 (no steering):** only the height was written. The game treats Link as falling, and falling Link has no
  air control.
- **Fix:** one loop at a time (`Game._lev_lock` plus a press number that older loops check); a failed search waits
  `LEV_FIND_RETRY` seconds before trying again; the loop writes x, y, z, with the rise easing in and horizontal
  speed set from the left stick relative to the camera (camera forward from `[[main+0x4E98068]+0x68]` +0x38/+0x44,
  right = (-fz, fx)). It stops moving Link when the game's frame counter stops (menu open, emulator paused). The
  controls are now tap L3 to float or drop, hold R3 to keep the height.
- **Found by:** the log; a live test measured each 0.1 s of movement against the camera's forward and right, plus a
  screenshot before and after a move to the right.

### 5.10 Levitate floats Link through walls
- **Symptom:** steering while floating passed straight through walls and rocks.
- **Cause:** the game does resolve collisions for a position we write: between our writes it pushes Link back out
  of a wall. But the float wrote its own running x/z every millisecond, which undid each push-back.
- **Fix** (`Game._float`): each step starts from the game's current x/z when the game has moved Link *against* his
  heading (a push-back), and otherwise from our own position. A push-back marks the wall for `LEV_WALL_MEMORY`;
  while marked, the velocity into it is removed and moves towards it are refused. Push-backs only count once the
  heading has held for `LEV_STEADY_TIME` (0.1-0.2 s). The height stays ours. Settings > "levitate through
  walls" (`levitate_through_walls`, `Game.lev_through_walls`) skips all of this, which is the old behaviour.
- **Why each part is needed:**
  - Starting every step from the game's position also takes in the game's own momentum (it turns our moves into
    velocity at actor `+0x3A0/+0x3A8`): after the stick was let go, Link slid on 2.5 units.
  - Without the wall mark, Link crept about a unit into the rock (hidden inside it). The game's leftover momentum
    into the wall also counted as a push-back once he turned to back off, so he got stuck.
  - Without the steady-heading check, the momentum from the old direction counted as a push-back for about 0.25 s
    after every turn.
- **Found by:** a hover-and-push test into a known rock wall in SacredForestMeadow_Child, from (-5.94, 115.86,
  -106.58) along (0.978, -0.209). The wall is about 10.1 units away. Pushing down into the floor showed the same
  push-back (Link sank about 0.4 and held). Result: Link holds at 9.9-10.1 against the wall through 2.5 s at full
  speed, backs off normally, stops within 0.26 units of the stick being released, and reverses in about 0.25 s
  (that's the ease-in).
- **Physics record motion state** (after the position doubles, f32): `+0x24/+0x28/+0x2C` = requested velocity
  (z, x, y), `+0x3C/+0x40/+0x44` = the same after collision (a blocked component becomes 0); the actor's velocity at
  `+0x388` follows the second copy. The game ignores the position while this is all zero, hence `WAKE`.

### 5.11 Air walls: where they are and what they do
- **Data:** `tools/airwalls.py` builds `data/airwalls.json` from the layouts (`romfs/Banc/<Area>/<Map>_Static|_Dynamic
  .bcett.byml.zs`, the map name is the save's "map" string) and the actor packs (`romfs/Pack/Actor/<Actor>.pack.zs`, a
  SARC). Each placed actor whose name contains `AirWall` has `Translate`, `Rotate` (radians, X Y Z; applied as
  Rz Ry Rx like sead's makeR) and `Scale`. Its shape comes from the pack: PhysicsParam `ControllerSetPath` →
  ControllerSetParam `ShapeNamePathAry` (follow `$parent`) → ShapeParam `Box` (Center + OffsetTranslation,
  HalfExtents) or `Cylinder` (CenterA, CenterB, Radius). 3504 walls in 71 maps; 236 Polytope/PhshMesh/Cone shapes
  are left out.
- **What a wall does to Link** comes from the collision filter in `Phive/Config/PhiveConfig.byml` (in
  `romfs/Pack/Bootup.Nin_Ounce_NVN2.pack.zs`), not the rigid body's mask alone. A shape's layer hit mask is the
  RigidBodyEntityParam `EnableLayerHitMask` (none = `HitAll`) ANDed with each of its `MaterialPresets`'
  `LayerHitMaskEntity`; its sub-layer hit mask likewise from `EnableSubLayerHitMask` and the presets'
  `SubLayerHitMaskEntity` (masks by name in `LayerHitMaskEntityCollection` / `SubLayerHitMaskEntityCollection`,
  bit n = entry n of `LayerEntityCollection` / `SubLayerEntityCollection`). Link on foot is layer `Player`,
  sub-layer `Unspecified` (Player_Base `CharacterMatterRigidBodyParam/Player`); climbing he's `ClimbingPlayer`.
  - It stops him if both masks have his bits: 1067 walls. Those whose presets carry the `Unridable` shape tag push
    him off, as he can't stand on them: 246 (`FldObj_AirWall_Player_Unridable`, the moving `PushAirWall_Box`); the
    ledge in front of the Forest Temple (SacredForestMeadow_Child, A and B below) is one. The other 821 are plain
    invisible walls (`FldObj_AirWall_Player`, `_Cylinder`, `_GenerateNavMesh`, `Common_Cube`, `All`, `Base`,
    `NoHitFootStep_Cube`, `ExcludeNpc*`, `HitBomb_Cube`), e.g. round the rocks in the Lost Woods.
  - The rest don't touch him on foot, whatever their mask: `HitOnlyCharacter` walls (enemies, NPCs, the doors'
    `PushAirWall_DoorFront`), `Material_AirWall_ForCamera` (`HitOnlyCameraBodyAndSight`), and walls with no mask
    whose rigid body or presets leave out `Unspecified`: the animal, cucco, horse, rolling rock, hookshot, bomb and
    hiding hole walls. `FldObj_AirWall_ClimbPlayer` (`AirWallForClimb`, sub-layer `ClimbingPlayer` only) stops him
    only while he climbs.
  - `tools/airwalls.py` stores this as each wall's `"link"` (`push`, `block` or null); the menu shows each kind
    on its own (scene > environment).
- **romfs `.zs` files** use zstd dictionaries from `romfs/Pack/ZsDic.pack.zs` (a zstd SARC): `bcett.byml.zsdic`
  for layouts, `pack.zsdic` for packs, `zs.zsdic` otherwise (`tools/gamefiles.py`).
- **`FldObj_AirWall_Player_Unridable`:** a 4 x 4 x 4 box sitting on its origin (centre +2 up), scaled per
  placement, `HitOnlyPlayer`. In SacredForestMeadow_Child the temple ledge has two:
  - **A (Dynamic):** origin (-6.94, 118.49, -117.60), scale (1.46, 2.87, 2.87). It covers x -9.87 to -4.01,
    y 118.49 to 129.99, z -123.35 to -111.85. It sits on top of the ruin wall in front of the entrance (the screen
    overlay lines up with it).
  - **B (Static):** a 30-unit cube at (-8.67, 122.87, -128.83), tilted 15 degrees about x, above and behind A.
  - Both are in memory once each as f32 position triples (A at `0x215c6ce600`, B at `0x215c658500` this session).
- **Push-out:** while Link floated against A, the requested velocity (record `+0x24`) jumped to +79 to +85 units/s
  out of the wall, and Link left at about 47 units/s, losing it only slowly in the air (wallwatch, `tools/wallwatch.py`).
  It looks like penetration depth over one frame, so the deeper he is when the game resolves it, the harder the
  launch. Which face it chooses hasn't been measured cleanly yet (the overlay's writes were mixed in).
- **The frame counter (`main+0x4E84BA0`) keeps running in the game's menus** (pause screen, R item menu) while
  gameplay is frozen. The levitate used it to hold still while paused, so in the R menu it kept moving Link with no
  collision, which caused most of the launches and the "lost Link's record" stops. It now holds still when the game
  hasn't touched Link's record for `LEV_PAUSED_AFTER` (every gameplay frame moves him off what we wrote).
- **Camera** (`[[main+0x4E98068]+0x68]`): `+0x38` position, `+0x44` look-at, `+0x130` vertical fov (0.8727 =
  50 degrees), `+0x134/+0x138/+0x13C` sin/cos/tan of half of it, `+0x140` aspect (16/9); projection scales at
  `+0x9C` (1/(tan*aspect)) and `+0xB0` (1/tan). Lichii draws the picture in its largest child window
  (`win32.render_rect`).
- **The view** (scene > collision > show air walls): `airwall.py` projects every wall within 60 units with that camera onto a
  pair of see-through click-through windows over the picture: front faces filled on one at the "wall opacity" setting (scene > collision) (default 15%; Tk's `-alpha` and `-transparentcolor` work together), outlines on the other
  at 85% (back edges dashed). Redrawn up to 60 times a second. Cyan = stops Link, red = Link's inside it, violet
  = other kinds. (The native view adds amber for the walls that push him off.) Walls further from the camera than Link get his silhouette cut out (a capsule from his feet,
  `airwall.LINK_SIZE` by age from the map name, filled with the key colour), so he stays in front of them.
  - **Accuracy:** checked against a screen recording while panning (`pan` capture, 160 frames). Link's feet
    project onto his feet in every frame, still or at 95 degrees/s, and the picture trails the camera data by only
    about 0-50 ms.
  - **It can't hide behind scenery:** the overlay has no depth for the game's picture. Doing that needs the map's
    collision (the placed actors' `romfs/Phive/Shape/*.bphsh`, 5877 files, a compressed Havok mesh format, not
    decoded yet) and a ray test per line.
  The "air wall" watch shows Link's depth (physics point) and which face is nearest, relative to the face he went in
  through: back, THROUGH, up, down, side.

### 5.12 Levitate laggy with the air wall view on
- **Symptom:** with scene > collision > show air walls on, levitating (and input in general) felt laggy.
- **Measured** (the real App in a harness pretending Lichii is focused, Link hovering; the levitate thread's
  position writes counted):
  - View off: 240 writes/s, longest gap 6.5 ms, 1.3 ms per Tk tick.
  - View on: 88 writes/s with one **2.4 s stall**, 3.9 ms per tick (the stall didn't happen on every run).
  - The emulator's own frame rate was unaffected (51 frames/s with or without a 60 Hz see-through overlay).
- **Cause:** the levitate thread shares the process (and the GIL) with the Tk thread, which redrew every wall
  from scratch each tick. With Lichii in the foreground Windows favours it, so the overlay's threads get less
  CPU and the levitate loop misses its slots; while it isn't writing, gravity pulls Link down.
- **Fix:**
  - The process runs above normal priority (`win32.raise_priority`) and the levitate thread at highest
    (`win32.raise_thread_priority`); `sys.setswitchinterval(0.001)`.
  - The view caches each wall's world-space corners, projects each corner once, reuses canvas lines
    (`airwall.Painter`), redraws at most `AIRWALL_FPS` (30) times a second, and checks the picture's position
    twice a second.
  - The "inside a wall" test only runs when the view, its watch or the menu is showing.
  - Result: view on 168-176 writes/s, longest gap 11 ms, 1.4 ms per tick.
- **Also found:** the landing heal checked health once, 0.6 s after touching down, and once missed the damage
  (6 quarter hearts). It now watches for `LAND_HEAL_WINDOW` (1.5 s).

### 5.13 Moving Link from the menu
- `Game.teleport(x, y, z)` writes the physics record (`+ PHYS_Y`) followed by `WAKE` when the record is known,
  so it works while the game is paused: the record keeps what was written and the move shows when gameplay runs.
  `_record_ok` (record within 3 units of the drawn position) fails in between, so `_record_is_ours` also accepts
  a record that still holds our last write. While floating, the float loop takes the position (`_lev_jump`);
  during a soft landing the landing is ended first (its curve would pull him back).
- If the record isn't known yet, `move_link` finds it, which nudges Link and needs the game running.
- **The dive meter** isn't a save field (only "Diving depth", the scale) or one of the parameter objects on
  Link's status object (`[[link+8]+0x6A0]`: only health `+0x1C10` and max health `+0x1C18`, each listed twice,
  `+0x1CD8/+0x1CE0`). `tools/linkwatch.py` records his actor, status object and every object they point to
  (394 objects, 816 KiB) to find it from a dive.
- **What it isn't (so far):** `[link+0x6F8] +0x188..+0x194` follows the meter (0..1) but is a short history of
  gauge values for its animation, rewritten every frame (writing it does nothing). The ~4,100 objects two
  pointers out from Link hold 16 more 0..1 copies; holding each full during a dive didn't slow the meter. Code
  storing to +0x18C/+0x190/+0x194 together (`main+0x33ED40`, a 4x4 matrix normalise; `main+0x27BD5DC`, a
  vector copy) isn't it either. `tools/meterscan.py` now scans the whole heap (5.3 GiB) in batches while the
  meter drains, saving progress in `tools/meterscan_state.json`.

### 5.14 After a warp, a fresh attach found the wrong memory
- **Symptom:** after a menu warp to LostWood_Child, a newly started tool (or ootmenu after a restart, or after
  the save data went missing for 3 s) reported "save data not found" and no Link, while the game was running.
- **Cause:** `_find_base` scans the process from low addresses for the self-referencing value at
  `main+CHECK_OFF`. Lichii also keeps the guest RAM as a flat backing store (the 14 GiB block `guestmem` uses),
  where the same value matches. That page had become readable, so it was found first: guestbase `0x4e1000`,
  main `0x1ae892d2000` instead of `0x2304c200000` / `0x8073a000`.
- **Fix:** `_valid_base` also needs Link's vtable (`main+VT_LINKOBJ_OFF`) to start with a function inside main.
  Vtable entries are absolute guest addresses, so this only holds in the real mapping.

### 5.15 Levitate didn't lift Link off the ground (Lost Woods)
- **Symptom:** L3 started the float (no errors) but Link stayed on the ground.
- **Cause 1:** the frozen-gameplay check (§5.11) only watched the record. Standing, the game snaps Link to the
  ground without touching the record (his drawn height flickers), so after the first small lift the float
  decided gameplay was frozen and stopped rising.
- **Cause 2:** standing, the game also clears the motion state after the position (`WAKE`) and then ignores the
  record altogether. `WAKE` was written once at the start; in the meadow Link was airborne before it was cleared,
  in the Lost Woods it wasn't.
- **Fix:** gameplay counts as running if either the record or the drawn position changed; the first
  `LEV_START_GRACE` (0.3 s) never counts as frozen; the float rewrites `WAKE` whenever it finds it cleared.
  Tested in LostWood_Child: about 2.4 units in 1.2 s, then a normal landing.

### 5.16 Black screen after warping from the Minuet pad
- **Symptom:** a menu warp to SacredForestMeadow_Child / ForestMinuet made from inside the meadow, with Link on
  the Minuet pad, left the screen black for good (HUD too) while the game kept running.
- **Cause:** auto-exit puts Link into the nearest exit, and the nearest of the meadow's two exits is the
  Forest Temple door, 1.6 units from the pad (the other is the Lost Woods one, 205 away). The warp swapped the
  door's destination for the meadow: the door's own transition started (fade out) but never got its arrival,
  so the fade-in never came. After that, loading zones did nothing either (moving Link into the Lost Woods exit
  with a new warp armed didn't fire it).
- **Fix:** `arm_warp(..., keep_into=DUNGEONS)`: if an exit's original destination is a dungeon, the warp leaves
  it alone ("skipped", Link goes into the dungeon normally) and the menu remembers that exit for the session so
  auto-exit picks another one next time.
- **Not checked:** whether other door-like exits (houses, shops, grottos) behave the same.

### 5.17 Upgrade choices (magic meter etc.) jumped between values
- **Symptom:** inventory > magic > magic meter wouldn't settle on 0 / 240 / 480; one burst of presses logged
  "= 0" then "= 480" in the same second and ended with neither magic item held.
- **Cause:** upgrades change the pouch through the game's own code (`patch.set_upgrade`), which only runs while
  the game runs; with the menu open the emulator is paused, so the request waits. The choice read the value back
  from the pouch, i.e. the old one, so every press stepped from the old value and queued a stale change.
- **Fix:** while a request for that setting is queued or running (`patch.pending`), the choice shows and steps
  from the value last chosen; the queue keeps only the newest request per setting, so the last choice is what
  gets applied. Applies to every upgrade choice (magic, wallet, bags, quiver).

### 5.18 How the game changes maps; warps to warp-song pads crashed (native ootgz)
- **Mechanism:** an exit is a `FieldAndDungeonEntranceEventRequestActor` (map layouts) that requests event
  `Dm_SY_BancChangeGo` with `NextBanc`, `NextStartPos`, `NextStartPosAir`, `BootEventName` (the arrival
  event, from its `NextEventName`) and `IsCheckAir`. The flow's `GameSystemActor.EventReserveBancChange`
  is an AI module (`romfs/Pack/Actor/GameSystemActor.pack` › `AI/ReserveBancChange.module.ainb`) that
  only writes GameData: `Sequence_ReservedNextBanc`, `…NextStartPosName`, `…BootEventName`,
  `…EventStarterName` (String64 indexes 0x22, 0x23, 0x20, 0x21: these are game.py's `next_map`, `spawn`,
  `event`, `starter`), `…NextPosition`, `…DungeonRoomID`, `Sequence_BancChangeFadePattern`, and
  `Sequence_ControlInEvent = ChangeBancOnEvent`, which the Event sequence (`romfs/Sequence/Event.module`)
  turns into `ChangeBancOnEvent` › `ChangeBanc`. The native menu requests the same event the way the
  `SeqOneShotRequestEvent` node does (`main+0x32db1a0` in 1.1.1, layout in `z64hd.h`). Verified: instant
  warps to Kakariko and Hyrule Field.
- **Symptom:** warping to `SacredForestMeadow_Child / ForestMinuet` crashed after the game reserved the map.
- **Cause:** `Dm_SY_BancChangeGo` always reserves the spawn name as the *event starter*, and the standard
  arrival (`Dm_SY_BancChangeArrive`) uses that actor. At exit spawns the exit actor has that name; at
  warp-song pads there is only a `StartPos`. The game enters the pads from `Dm_SY_Ocarina` › `OcarinaWarp`
  with `EventStarterName` empty and `BootEventName = OcarinaWarp_Arrive` (blue warps and sage rooms also
  use an empty starter, with story events).
- **Fix (not yet confirmed in game):** `tools/spawnstarters.py` records, per spawn, the actors that share
  its name (`data/spawn_starters.json`: 145 of 336 have the exit actor, 191 have none). Warps to spawns with
  none pass `OcarinaWarp_Arrive` and clear the reserved starter as soon as the game has reserved the map.
- **Found by:** dumping every event flow's `EventReserveBancChange` (`evfl`) and comparing the layouts of an
  exit spawn and the Minuet pad.

### 5.19 Copies of Link's position; the native levitate (in progress)
- **A copy, not the record:** Link's actor `[+0x8]` → `[+0x458]` → `[+0x410]` → `+0x288` holds (x, y, z) doubles 0.585 above
  his drawn position, followed by a second (x, y, z) about 1 unit lower. A pointer search from the actor (depth 4)
  finds this and one more like it. **The game rewrites it from the real record every frame:** a per-frame trace
  of writing a rising y showed the game's value back each frame (wrote 115.338, read 115.062). Writing it does
  nothing lasting.
- **Crashes:** game.py's 48-byte WAKE written after one of these copies (over the second position) crashed the game,
  twice. So did test-writing loosely matched look-alikes (1.0 tolerance, 12 matches). Nothing is now written past a
  position unless the bytes there are all zero or plausible floats, and doubles there mark a copy.
- **Now:** levitate scans the heap like game.py (16 MiB per frame, regions nearest the actor first) for a match
  within 0.3 that is followed by float motion state, not doubles, and test-lifts each (at most 6). Not yet
  confirmed in game.

### 5.20 Base game support (1.0.0, package v65536)

- **Symptom:** The 1.1.1 IPS never matches the base build. Reusing the update's offsets would
  call or write unrelated locations.
- **Fix:** Ship one subsdk9 and two IPS32 patches. The loader chooses by main build ID. The
  module matches nnMain and MOD0, then checks the patch format, game revision, branch, and
  writable slot before installing the hook. Both stubs fall through to the game's original
  UnlockMutex when the slot is zero.
- **How:** Matched instruction sequences and callers across both executables, resolved the
  unlock import by name, followed code references to globals, and compared relocated vtable
  methods. The base hook is at main+0x3af2a6c, with executable padding at +0x3be6a00 and a
  slot at +0x4e2a070 after MOD0's bss end. The base item's event calls AddPouch and returns
  without the update's separate notification, so the base profile omits that call.
- **String64:** The base game's data puts Sequence_CurrentBanc at index 0x1d. The update puts
  it at 0x1f. This broke both the old anchor test and the warp fields at fixed indices.
  Both GetString64 implementations use the type at manager+0x608 and the value pointer at
  object+0x68 (stride 0x108). The module resolves strings by hash and checks the value's
  capacity and mapping. A failed warp fallback now ends the pending request.
- **Verified:** The port built on v0.0.1 ran on both revisions in Lichii. The loader selected the
  correct IPS, and the shared module initialized the frame, input, and drawing hooks. The menu
  opened on 1.0.0. A host-side read/write test used the different String64 indices from each build.
  Full in-game warping after the String64 fix still needs a play test.

### 5.21 Locking Link's X, Y and Z
- **Cause:** the game's collision resolver and its body-position stores move Link after the module writes him, so
  a written position doesn't hold through address writes. The stores are: collision `main+0x18D2C18`, `+0x18D2C20`; body `main+0x180EA24` (`stp d0, d1,
  [x23, #0x10]`, X and Y together) and `main+0x180EA2C` (`str d0, [x23, #0x20]`, Z). Link's body centre is the
  physics record (`levitate.c`), 0.585 above his drawn position.
- **Fix:** We replace each store with a branch to a stub in the cave (`STORES_OFF` = 0x80). The stub
  saves x16/x17 and tests bits in the lock word. X and Y share the stp, so its stub runs one of four variants
  `lock.c` sets the bits from the menu (link > lock X / lock Y / lock Z) 
  and from save / load position (all three, while a load settles). The menu's
  locks let go when the map changes, since a warp runs Link through the same stores.
- **Axes:** Y is height; X and Z are horizontal.
- **Checked:** `mkips` writes the stubs and branches for `1.1.1`; the words were compared with clang's assembly
  (`stp`/`ldp`, `adrp`/`ldr` of the slot, the `tbnz` bit numbers, the original stores) and every branch resolves to
  its intended address. The module files compile for aarch64.
- **1.0.0** (22nd November 2026): the same stores at `main+0x17C8D24` (X+Y), `+0x17C8D2C` (Z), `+0x188D3B8` and
  `+0x188D3C0` (collision), each matched on all 65 masked words round it in a memory dump; the lock word is
  `main+0x4E2A078`, after the hook slot as in 1.1.1 (MOD0's bss ends at `0x4E2A068`). Patch version 5.
- **Not verified in game on 1.0.0.**

### 5.22 Loading zones don't fire while Link's collision is off
- **Symptom:** floating into a loading zone with levitate's **through walls** on did nothing; the exit fired as
  soon as levitate was turned off (log: collision bypass off, then `exit taken to GanonCastle` 0.8 s later).
- **Cause:** the bypass is the game's ForceNoHit mode on Link's controller bodies (cct.c), which also stops the
  zone's trigger (an `Area` actor's rigid body sensor) from seeing him.
- **Fix:** `tools/exits.py` builds `data/exits.json` from the map layouts: an `AiGroups` entry whose `Logic` is
  `FieldDungeonEntranceSet` references the exit (`FieldAndDungeonEntranceEventRequestActor`, destination in
  `NextBancParamName{Child,Adult}`) and its `Area`, a unit box (-0.5..0.5 across, 0..1 up) placed like an air wall
  (151 zones in 49 maps, all boxes). levitate.c ends levitation (and walk through walls hands collision back) while
  Link's middle is within 0.5 of one, and refuses L3 there. Doors entered with A aren't in this list.

## 6. Things that do not work

- **Using character 0x7F for an icon:** menu, log, splash, and input panels already use it as an
  invisible sizing marker. The 0.0.3 JS Height padlock made those markers visible throughout the UI.
  In 0.0.4 the 0x7F glyph is blank and the padlock uses 0x80. The real menu layout and font are covered
  by a host regression test that checks sizing markers stay blank; the menu and watch were also
  rendered headlessly. In-game verification of the correction is still pending.

Each of these was tried and verified not to help:
- `SeqOneShotResetPlayerStatus` flags (HUD screen `+0x384 = -1`, `+0x388 = 1`): consumed, but no redraw.
- `main+0xe35440(screen)` (the "event end in" routine): no redraw.
- `IsEquipDirect = 1` vs `0`: neither makes the ocarina playable on its own (§5.1).
- Reloading the area (warp): doesn't create the ocarina on Link.
- `ActionGuide_IsEnd.Ocarina` / shortcut tutorial flags: unrelated to the ocarina playing.
- AddPouch with a negative count: does nothing.
- `main+0x2df6c80` with an empty slot: doesn't take gear off.
- AddPouch with `Item_Dungeon_Map/Compass/BossKey/SmallKey`: sets none of the dungeon flags.
- Link's velocity on the actor (`+0x388/+0x38C/+0x390`): output only. Writing `+0x38C` (y) every millisecond while
  he stood still kept the value but didn't move him, and the game set it back to 0 afterwards (§5.9).
- Steering by writing velocity instead of position (§5.10), tested while hovering at 4 units/s for 1 s each:
  - Record `+0x24/+0x28` (requested velocity) and actor `+0x388/+0x390`: no movement.
  - Record `+0x3C/+0x40` (after collision): moves Link only about 25% of the asked amount (the game rewrites it
    every frame), and it comes after collision, so it can't respect walls anyway.
- Zeroing actor `+0x3A0/+0x3A8` (the game's horizontal momentum) every ms while steering: Link went straight
  through the wall. The push-back is carried by that velocity.
- Writing the `Sequence_*Pause*` GameData values (`GamePauseRequestCount` at 1 for 2 s, `PreGamePauseRequestCount`
  at 1 for 20 s): nothing happens. They only keep count for the pause graphs (§9.1). **Don't leave one changed**
  (debug > pause research can): at 1, the game's own pause screen and menus would skip their real pause request.

## 7. How to investigate

- **Ask the game's scripts first.** `tools/evdump.py` on the relevant `.bfevfl` shows which node and parameters the
  game uses (giving the ocarina, removing an item, opening a chest).
- **Screenshots without bothering the player:** `PrintWindow(hwnd, dc, 2)` on Lichii's main window captures the game
  even when covered.
- **Catch the code that touches something** with Lichii's GDB stub (Emulation > Configure > Debug > Enable GDB Stub,
  port 6543): `tools/dpadwatch.py` (write watchpoints), `tools/readwatch.py <field>` (read watchpoint),
  `tools/magicwatch.py`. Notes:
  - With the stub on, the game is held at boot until a debugger connects; start the script first.
  - **Lichii leaves the game halted when a debugger detaches.** The scripts send `c` and close the socket instead.
  - While a watchpoint is set, that memory page can't be written from outside (`WriteProcessMemory` fails).
  - Turn the stub off afterwards, or the next game start hangs at the launch screen.
- **Snapshot diffs:** `tools/snap.py` (all save fields) or a raw read of an object before/after an action.
- **From an object to its methods:** read its vtable from live memory, map entries to `main+` offsets, then look
  them up with `re64.py`. Functions with no direct callers are usually virtual; search main's data for the
  pointer to find the vtable and slot.
- **Ground rules that saved time:** never write save files (back up with `python flags.py backup`); verify each
  claim with a memory read or screenshot before asking the player to test; warping can mislead tests (§5.8).

## 8. Open problems

### Native collision bypass (0.0.9 test build)

- **Symptom confirmed by the user in 0.0.8:** some walls pass, others block Link or trap him.
  The session log shows the record returning to y=110.463 while the mod requests heights from
  108.701 to 112.102, and drawn y remains 109.623. `frozen` remains zero in those attempts.
  The pause heuristic therefore does not explain this particular blockage. Disabling only the
  mod's push-back response never disabled the game's collision solver.
- **Verified by disassembly and read-only live inspection:** the character property resolves as
  `[ [ [ [ [link_actor()+0x270]+0x50]+0x20]+0x10]+8 ]`, each bracket a u64 dereference.
  Its vtable is main+0x4C34F10 (`CharacterProperty`); +0x20 points to the controller body,
  vtable main+0x4C34658. These are module offsets, not session addresses.
- `OneShotPhysicsCctChangePosition` at main+0x2900120 follows this chain and calls property
  vtable +0xE8 = main+0x6B2530 with a pointer to three f32 coordinates. This preserves the
  rotation, changes the transform translation, then invokes the attached-body updates through
  vtable +0x250 = main+0x6B23A0. The controller transform translation and Link's drawn position
  matched in live reads. The scanned double record uses the capsule centre; the mod measures
  its vertical offset from drawn feet when floating starts rather than assuming child/adult height.
- `OneshotChangeForceNoHitMode` (name getter main+0x295F800, executor main+0x295F0D0) reaches
  that same body through property +0x20 at main+0x295F564..574. Its collision toggle calls
  main+0x158D40(body, bool). That function locks the body and queues the filter change. The
  current state is body +0x70 bit 11, overridden by `[body+0x60]+0xF8` bit 0 when that pending
  object's +0x100 bit 19 is set. The setter preserves the other filter fields.
- **Implementation:** `cct.c` validates the current actor/controller types, attached lists and
  pending state; remembers the prior collision mode; calls ForceNoHit while through-walls
  levitation is active; and uses the game's position setter alongside the existing physics
  record. L3, disable and mode changes restore the prior collision state. Unavailable objects
  defer restoration until they can be validated; replaced actors/controllers are never written.
  The frozen-state shortcut can no longer bypass the physics record's mapping check.
- **Validation boundary:** native build and host lifecycle/integration tests pass. Live reads
  verified the chain/types and static analysis verified the call paths. The emulator build ID matches the installed test module. With the option enabled, a live
  read confirms body +0x70 bit 11 becomes set and the controller transform matches drawn position.
  The user subsequently confirmed traversal through the previously blocking wall. The first retest had
  through-walls off (log and saved setting both confirmed), so it did not exercise the bypass. The release fall fix is documented below. Camera behavior
  is not separately verified. The old stationary-position pause heuristic remains.

- **Manual vertical controls (fork 0.0.8):** X commands ascent, B commands descent, neither/both
  hold height; R3 overrides both. At a legacy saved speed of 0, manual movement uses 4 units/s.
  With through-walls enabled, B descent bypasses the mod's upward floor correction, which would
  otherwise undo downward movement every frame. X/B/R3 are masked from the game only while floating.
  Host tests verify direction/release/reversal, floor feedback, simultaneous horizontal movement,
  menus, legacy zero speed and mask cleanup. This is not a collision-mask change: wall/ceiling
  blocking, camera follow and the stationary-position pause heuristic remain unresolved. No game
  runtime verification of the controls has been performed.


- **Fast fall after L3 release (fork 0.0.7):** the user reports a very fast fall when stopping
  levitation. Previously `drop()` stopped position writes without clearing any physics velocity.
  At that stage stored falling velocity was suspected but unconfirmed. That release cleared only the
  documented requested/post-collision y velocities (`record +0x2C/+0x44`, §5.10), once, after checking
  the record still follows Link, is readable/writable and has plausible motion state. It does not
  change position, horizontal velocity or gravity. Host tests cover the exact writes, L3/disable,
  invalid records and no further writes after release. The game may have additional momentum state;
  the user has now confirmed that 0.0.7 still drops too fast. Clearing these fields is insufficient;
  that attempt was insufficient. See the verified 0.0.9 follow-up below.

- **Zero-rise hover (fork 0.0.6):** rise speed previously offered a minimum of 2, and settings
  rejected 0. The menu now offers 0 and settings retain it. At zero rise, vertical levitation velocity
  is explicitly cleared: simply adding 0 to the options would leave a previous upward velocity
  unchanged because the acceleration step is also 0. Host tests verify stationary hover without R3,
  horizontal wall passage at fixed height, switching from positive rise to 0 and back, and persistence.
  Floor corrections from 0.0.5 still apply; the game's runtime behavior remains to be tested.

- **Levitation floor support (fork 0.0.5):** `float_step` read the game's current y but then wrote
  its own accumulated y back, discarding upward physics corrections even while passing through walls.
  It now retains the higher of those heights before adding the requested rise. Horizontal wall
  push-back remains controlled by the existing option; downward drift does not lower the hover.
  Host tests exercise floor corrections, rising platforms, both wall modes, pause/drop and record loss.
  This is not a floor raycast or a change to collision masks: an upward change can also be an impulse,
  and missing geometry or a floor crossed between physics updates cannot be detected here. Actual
  wall/floor behavior, slopes and moving platforms still need in-game verification.

- **JS Height reset/lock (0.0.3):** the user supplied the f32 chain
  `[[link_actor() + 0x270] + 0x358] + 0x18E8` and reported `FLT_MAX` on fresh load, with jumps/sidehops
  replacing it with takeoff Y + 0.70. The watch reads it dynamically; optional reset writes `FLT_MAX`
  once and lock repeats that write in `watches_frame`. Both resolve the current actor and check
  alignment, overflow and read/write mapping before writing only four bytes. Lock is session-only,
  defaults off, and continues independently of HUD visibility. Its interaction with the game's
  threshold setter/check ordering has not been runtime-verified. A per-frame freeze cannot guarantee
  suppression of a setter and check that run between mod frames; the game's setter would need separate
  investigation if that occurs.

- Warping into a dungeon doesn't register the current room, so the map screen shows "???" until you walk through a
  door.
- Upgrades other than the wallet are wired up (§5.7) but not yet confirmed in game.
- Walk through walls (cheats > player): when the stick holds Link against a wall for 0.2 s, the cheat switches off
  his controller's collision with the game's ForceNoHit (`cct.c`, "Native collision bypass") and walks him on at the
  height he was at through the controller's position setter, until the stick is let go or turned; then collision
  comes back and the landing is protected as after a float. Being retested in game with this method. Earlier tries,
  SacredForestMeadow_Child, a rock wall at about (8.56, 119.94, -105.41) in the physics record:
  - Writing Link's x/z each frame doesn't get him through: the game puts the record back at the wall every frame.
    Against a wall his requested velocity (record `+0x24`) drops from about 4.4 units/s at a run to 0.1-0.5.
  - Clearing the Player bits in Phive's layer table (PhiveConfig `LayerEntityFilterTable`, read at boot by
    `main+0xca326c` into two arrays of 22 u64 masks, 4 copies on the heap) did let him through, while it held his
    height: the game reads that table at run time. It changed every Player-layer body and needed a 21 s heap search
    (later spread over frames), so the cheat moved to the controller's own ForceNoHit (0.0.9's levitate method).
- Count items (sticks, nuts, bombs, bombchus, beans), bottles and masks aren't cleared from item buttons when
  removed.
- Unticking an ocarina leaves `ActionGuide_IsEnd.Navi` set (the D-pad stays, empty).
- Collision view (gz's): shelved. The collision files are decoded (`tools/phive.py`); the plan, costs and format
  are in [COLLISION.md](COLLISION.md).
- Leads in §9 not followed up yet: a player debug move state that may survive (levitate without searching for the
  physics record), the game's own debug line and triangle shaders (collision view, stage 2), and the scene's
  managers (a Link state name, the exit Link is nearest to, the map's collision, the explored room after a warp).

## 9. Leads: pausing, debug leftovers, scene managers

Found from outside the game (memory reads and test writes in Lichii, static analysis of main) and not used by
sloperinaGZ yet. Same build as everything else here.

### 9.1 Pausing: the pause manager
- **The `Sequence_*Pause*` values only keep count.** Opening the map (Minus) or the pause screen (Plus) sets
  `PreGamePauseRequestCount` and `IsPreGamePause` to 1, about 60 ms later `GamePauseRequestCount` and `IsPause` to 1
  with the pre request back to 0; closing puts both back to 0 with a one-frame `PostGamePauseRequestCount` and
  `IsPostGamePause`. A dialogue changes none of them. The menu starts to fade in with the pre request, so
  **settings > hide in game menus** hides the HUD from `IsPreGamePause` / `PreGamePauseRequestCount` on
  (`gd_game_pausing`); waiting for `IsPause` left it over the menu for a few frames. They belong to the sequence graphs `ChangeGamePause.module`,
  `ChangePreGamePause.module` and `ChangePostGamePause.module`: on input `IsPause` they add or take 1 from the count,
  at 1 run `SeqOneShotRequestPause` with `PauseType` `GamePause`, back at 0 `SeqOneShotReleasePause`, and below 0
  raise an app error ("[Seq]GamePause underflow, please report"). Callers: `GamePauseOn`/`Off`,
  `PlusMinusPauseOff`, `OcarinaPauseOn`/`Off`, `ShortCutPauseOn`/`Off`.
- **The item wheel pauses exactly like the pause screen** (`ShortCutPauseOn` runs `ChangePreGamePause` and
  `GamePauseOn`: the same values, at the same times). What tells them apart is an unnamed Enum, `0x578D41E1`
  (values `None`, `ItemMenu`, `EquipmentMenu`, `CollectMenu`, `SystemMenu`, `Save`, `Load`, `Option`, `ActionGuide`,
  `ToTitle`, `WorldMap`, `DungeonMap`, `FieldMap`, `UnopenMap`, `ChallengeMenu`, `ChallengeMenuFullScreen`,
  `ImportantMenu`, `Ocarina`, `Title`, `Loading`, `GameOver`): the menu screen that's up. Logged in Lichii
  (debug > pause research, 2026-11-15): the wheel left it `None`; Plus set `ItemMenu` and Minus `UnopenMap` one
  frame after the pre request, back to `None` with the post request. The wheel's own fields (`UI_IsNeedOpenShortCut`,
  `UI_IsShortCutBCancel`, `UI_CurrentShortCutCategory`, `Sequence_ShortCutOpenHoldCounter`) didn't change.
  **hide in game menus** now also needs it to be other than `None` (`gd_game_menu_screen`); paused with it `None`
  (the wheel), **hide over item wheel** decides instead.
- **The pause manager** is `*[main+0x4e97e38]`: `+0x00` the number of pause types (20), `+0x08` the type table,
  sorted by ID, 0x20 bytes per type: `+0x00` name, `+0x08` ID (MurmurHash3 of the name), `+0x0C` and `+0x10`
  bitmasks (probably what the type stops), `+0x1C` the request count (bit 31 is a separate flag; keep it). `+0x48`
  is a listener whose notify function (`main+0xafba60`) is an empty `ret`.
- `SeqOneShotRequestPause` (vtable `main+0x4c6e208`, work in slot 39 at `main+0x3bb4260`) turns `PauseType` into an
  ID and calls `main+0xafb980`, which finds the type, adds 1 to its count atomically and notifies the listener.
  Release is the next function, `main+0xafba70`. So a pause is just a count above 0, and raising one from outside the
  game works like the game's own request. From inside, those two functions are the ones to call (their arguments
  weren't traced).
- The 20 types: `ResetGimmickFromScript`, `LoadingPause`, `UpdateBancResource`, `DebugPause`, `SceneBeginning`,
  `EventPlayMovie`, `EventDisableController_Camera`, `EventSetupOrRestore`, `WarpPause`, `PostGamePause`,
  `SystemPauseWhileLoading`, `UpdateResourcePause`, `EventBancChangePause`, `ResetGimmick`, `PreGamePause`,
  `GameOverPause`, `GamePause`, `EventDisableController`, `SceneTransition`, `ClickPause`. IDs: `GamePause`
  `0xba597383`, `EventDisableController` `0xd468c7d1`, `EventDisableController_Camera` `0x5cf23aab`, `DebugPause`
  `0x2d67e2da`, `ClickPause` `0xfbad2d6a`.
- **Tested in Lichii**, a count raised to 1 from outside and back to 0:
  - `EventDisableController`: Link ignores the controller while the world keeps moving (gz's menu behaviour). It
    was raised and lowered for each of 16 menu opens in one session, while the game's own events raised and lowered
    it too, with no trouble.
  - `GamePause`: everything stops like the pause screen and the game keeps drawing, but some objects disappear while
    it lasts (the game's pause screen hides that behind a captured picture).
- For frame advance (§8): `GamePause` stops the world without stopping the picture, with the missing objects as the
  catch. `DebugPause` and `ClickPause` look like debug leftovers and haven't been tried; either may be the
  developers' own stop.

### 9.2 Debug leftovers
A name in main doesn't mean the feature still works: release builds keep the names of registered nodes and
parameters while the code behind them is compiled out.
- **Scene selector: almost certainly compiled out.** RomConfig (`sInstance` at `main+0x4e97e60`) is filled from
  `System/RomConfig/%s%s` (`main+0x3bc1230`) and `System/GameRomConfig/%s%s` (`main+0x9b9a90`), with the build name
  picked from `Debug`, `Develop`, `Pilot` and `Product` by an index hard-coded to 3 (Product). The reader
  (`main+0x9b9de0`) stores `BootupScene` (+0x8) and `UseSceneSelector` (a byte at +0x118). The reflection type's
  constructor defaults `UseSceneSelector` to 1 and the Product config sets 0, but nothing reads +0x118: the boot only
  checks whether `BootupScene` is empty (scene manager init `main+0x3ba9fb0`, update `main+0x3baa670`), and no
  string names a scene select screen. Not checked: whether romfs also holds Debug or Develop RomConfig files.
- **Debug drawing: the game side is compiled out, the renderer side isn't.**
  - Every `ExecuteDebugDraw*` and `ExecuteDebugEntry*` node keeps its parameter list, but its per-frame method is a
    bare `ret` (`ExecuteDebugDrawBox`, slot 29; the same for `DrawSphere`, `EntryCapsule`, `EntrySphere`, `AppLog`
    and others).
  - `ExecuteDebugEntryRigidBodyShape` still has a body. It asks `main+0x38bb7b0` to draw each `RigidBodyConst`,
    `RigidBodyEntity` and `RigidBodySensor`, which loads the physics object `X` (`*(*(main+0x4c7df00))`, then
    `+0xC0`) and returns at once if `X+0xE0`, the debug draw manager, is null. It always is: the physics setup
    (`main+0x1206010`) creates X's members at +0xD8 and +0xE8 to +0x108 but nothing at +0xE0.
  - Still there: the render layer table (`main+0x4c0bab0`: `CalcView`, `CalcGPU`, `BG`, `Opa(Debug)`, `Opa`, `Xlu`,
    `Xlu(Debug)`, `PostEffect`, `DebugInfo`, `FinalImage`, `Text(Debug)`) and the system shader list
    (`main+0x4c04608`: `debug_primitive`, `debug_shape_instanced`, `debug_point_instanced`, `debug_line_instanced`,
    `debug_triangle_instanced`, `debug_colored_mesh`). These are the candidates for COLLISION.md's stage 2, route 1
    (a shader the game has already built). Whether they're loaded at run time wasn't checked.
  - `phive_debug_draw` is the water simulation's height field view, not collision. Havok's Visual Debugger viewers
    (`hknpShapeViewer`, `hknpBroadPhaseViewer` and others) are linked in; whether the VDB server starts wasn't
    checked.
- **Player debug move: may survive.** `ExecutePlayerDebugMove` and `ExecutePlayerSubDebugMove` are registered
  nodes, and Link's behaviour tree has `SelectorPlayerSchedulerDebug` and `SelectorPlayerSubSchedulerDebugMove`.
  Unlike the draw nodes, `ExecutePlayerDebugMove` isn't empty: slot 29 (`main+0x27d3e60`) follows `[this+8]`,
  `+0x8`, `+0x1F0`, `+0x270`, `+0x358` to a player component and calls `main+0x933270` on it with request 0xF. Not
  traced further. From inside the game it can simply be tried; if it's a free-move state, it's a levitate that
  doesn't need Link's physics record (§5.19).
- Other names: `FocusDebugCamera` (compared as a command name at `main+0x3b54b34`); the scene parameters
  `DebugBootScript`, `IsChildWhenChangeBanc` and `IsForceSwitchChildOrAdult`; the rendering switches
  `dev_colorization`, `dev_scope_draw`, `dev_color_bars`, `dev_lkt_ui` and `tera_develop`;
  `SeqExecuteResetGameDataAllForDebug`; `engine__debug__ClickMenu` (no references). Not found: anything named cheat,
  noclip, god mode, level or stage select, or ImGui.

### 9.3 The scene's managers
`S = *[*[main+0x4e97648]+0x1F0]` (the scene manager's current scene) holds 41 component pointers at `*[S+0x58]`
(count at `S+0x50`). Each component's vtable has a name getter (`adrp x0; add x0; ret`) among its first 24 slots.
Read in Kokiri Forest:

| Entry (offset) | Component | Entry (offset) | Component |
|---|---|---|---|
| 0 (+0x000) | Sequence (`FormalSequence.root`) | 22 (+0x0B0) | WarpMgr |
| 5 (+0x028) | PlayingInfoMgr | 23 (+0x0B8) | OptimizeChangeBancMgr |
| 6 (+0x030) | GlobalGameParameterTable | 24 (+0x0C0) | SystemCullingMgr |
| 7 (+0x038) | BancLoadMgr | 25 (+0x0C8) | DropMgr |
| 8 (+0x040) | LocationMgr | 26 (+0x0D0) | LimiterActorCountMgr |
| 9 (+0x048) | NavMeshMgr | 27 (+0x0D8) | CompoundNavMeshObjectMgr |
| 10 (+0x050) | StaticCompoundBodyMgr | 28 (+0x0E0) | GameSafetyMgr |
| 11 (+0x058) | PhiveVolumeStatsMgr | 29 (+0x0E8) | AreaCullingMgr |
| 12 (+0x060) | AutoSaveMgr | 30 (+0x0F0) | RouteRailMgr |
| 13 (+0x068) | LoadProgressMgr | 31 (+0x0F8) | DungeonMgr |
| 15 (+0x078) | ResidentActorMgr | 32 (+0x100) | OcarinaMgr |
| 16 (+0x080) | BakeLoadMgr | 33 (+0x108) | OcarinaCreationMgr |
| 17 (+0x088) | OwnedHorseMgr | 34 (+0x110) | NaviMgr |
| 18 (+0x090) | MinigameMgr | 36 (+0x120) | NpcMgr |
| 19 (+0x098) | TimerMgr | 37 (+0x128) | AgonyStoneTargetMgr |
| 20 (+0x0A0) | LoadingPauseMgr | 38 (+0x130) | FoliageLoadMgr |
| 21 (+0x0A8) | AmiiboMgr | 39 (+0x138) | EventSubstituteStarterMgr |

Entries 1 to 4 and 14 were empty; 35 and 40 had no name getter.

- **NaviMgr keeps a copy of Link's transform** every frame for Navi (the game's `QueryNaviGetPlayerPosAndRot`
  reads it): a row-major 3x3 rotation at +0x894 (yaw = atan2(+0x89C, +0x8B4)), the root position (his feet) at
  +0x8B8, the position at +0x8D0 and the velocity in units per second at +0x8DC. The chain held through loading
  zones, a death, the title screen and a game reboot. It's only a copy: writing it doesn't move Link.
- Leads, not followed up:
  - AutoSaveMgr `+0x1DC` holds a short name that follows what Link is doing: `Move`, `Wait`, `EvtTalk`, `EvtWait`,
    `EvtLookAroundAttention`. A start for a Link state watch.
  - WarpMgr (`+0x170`, a flag at `+0x148`) and OptimizeChangeBancMgr (`+0x80`) hold the destination of the exit Link
    is nearest to; in the Lost Woods they switched between `GoronCity_Child`, `KokiriForest_Child` and
    `LostWood_Child` as he walked.
  - StaticCompoundBodyMgr is the obvious place to look for the map's collision in memory (COLLISION.md, "the
    unknowns"), and DungeonMgr for the explored room a menu warp leaves unset (§8).



### 9.4 L3 release: controller velocity and landing history (0.0.9-test2)

- Read-only observation of the running 0.0.9-test build found body `+0xE0` velocity
  `(0, -60, 0)` while Link hovered; actor output `+0x388` and momentum `+0x3A0`
  also reported approximately -60 Y. Clearing only scanned-record `+0x2C/+0x44`
  in 0.0.7/0.0.8 did not reset the actual controller (confirmed by the user).
- `ExecutePhysicsCctReduceVerticalGravityPositionVelocity`, helper main+0x278AA90,
  reads body+0xE0 and calls main+0x12DBE0 with a new vector. This setter locks the
  body and queues linear velocity at pending+0x3C with pending+0x100 bit 7. The
  mod now calls it each floating frame and once at release, preserving applied
  X/Z and clearing Y. Collision restoration follows the release reset. Gravity
  is not altered and velocity is not clamped during the subsequent fall.
- `QueryPhysicsCctIsOnGround` at main+0x2AABA20 reads property+0x48, selects the
  result pointer at +0x40 + 8*[+0x58], and tests result+0x10 == 0. This is used
  to end landing protection after six supported frames, rather than a timer.
- main+0x5FCF6C computes player+0x190C = player+0x18D4 - actor+0x320; +0x5FD128
  maintains the peak Y at player+0x18E0. +0x5FD19C feeds actor's base component
  `[[actor+0x270]+0x2B8]` at +0x1898. main+0x484A10 computes its fall distance
  +0x189C = max(origin Y - actor Y, 0). During levitation and its first landing,
  these origins follow current Y and distances are cleared. The base backpointer
  +0x1340 and player backpointer +0x18 must match the current actor before writes.
  JS Height +0x18E8, health, gravity and actual position are untouched by this reset.
- The old 90-frame health refund was removed. It could expire during a long drop
  and restored damage after it happened. The new mechanism resets fall history;
  it does not suppress enemies or other damage and does not provide void immunity.
- Host tests pass for terminal-speed reset, preserving horizontal motion, null/
  unavailable/replaced objects, exact fall-history targets, long drops, supported
  landing, and ending protection. Native v131072 build passes without warnings.
  **User runtime result: gentle fall, no damage.** The test2 log independently
  shows the release reset queued, collision restored, and normal fall damage restored after
  landing. This is a confirmed test case, not exhaustive coverage of every scene/event.
- User confirmed the preceding test's native through-walls bypass passes the wall
  that previously trapped Link, after turning through walls ON. Void-trigger
  visualization and optional levitation void immunity are still under investigation.

### 9.5 Actors, their life and position (from the disassembly, v131072; checked in game 9th November 2026)
- **The actor manager** `mgr = *(main+0x4E90D90)` (`+0x00` a mutex: lock `main+0x3C42460`, unlock `main+0x3C42470`).
  The live actors are in an intrusive red-black tree at `[mgr+0x38]`: node N has left `+0x00`, right `+0x08`,
  parent|colour `+0x10`; each node is at `+8` in an entry E (`E = N - 8`) with the actor at `[E+0x28]` and the next
  entry with the same key at `[E+0x38]` (an entry pointer, no -8). The game walks it in order under the lock
  (`main+0x29b2d10`, `0x880cd0`, `0xaa7e48`). No count was found (`mgr+0x28` or `+0x40` may be one).
- **An actor:** name `char *` at `+0x20` (`"Enemy_Gibdo"`, compared at `0x29eceac`), state at `+0x2C` (the walks
  skip below 2; 7..11 is dying, dead or deleted), world position (3 floats) at `+0x31C` (`QueryActorGetPosition`,
  `0x29a8940`), its rotation matrix from `+0x328`. Game actors share Link's vtable `vt_linkobj`.
- **Life:** the Life component is `[[actor+0x270]+0x2B8]` (vtable `main+0x4B4C460`, name "ライフ"); its parameter
  objects (vtable `vt_param`, s32 at `+8`) are the current life at `+0x1C10` and the max at `+0x1C18`
  (`QueryActorLife` `0x29afbc0`, `QueryLifeGetLifeByActor` `0x2a487a0`). So the `+0x1C10/+0x1C18` block in `link.c` is
  this generic component, not Link's own.
- **Checked in game** (SacredForestMeadow, Forest Temple): the walk finds 140-1000 actors; Link's life read this way
  matches `link.c`; Wolfos (9), Stalfos_Weak (18), Skulltula (2), Bubble_Blue (1) lose life as they're hit. Enemies'
  current/max objects are not `vt_param` objects like Link's (another kind, value also an s32 at `+8`).
- **Has it appeared?** The state at `+0x2C`: an enemy the game keeps waiting, alive and at full life, sits at **6**
  (Wolfos still in the ground, Stalfos before they drop, Poe sisters, Skulltulas before they come down) or 3, and goes
  to **4** (or 5) when it appears. Found by logging Wolfos before and after they jumped out, the two that stayed
  hidden as a control: also `+0x30` 8 -> 0, `+0x6E4` ffffffff -> 0 and `+0x518` 0x100 -> 0x42f changed with it.
- **1.0.0 (v0):** the manager is `*(main+0x4E20930)` (the slot's neighbours `frame_counter` and `camera_ptr` sit
  0x70460 below v131072's, and so does it): its singleton is made at `0xCCE6B0` (0x490 bytes), its constructor
  `0xCCE730` makes a mutex at `+0x00`, zeroes `+0x28` and `+0x40` and leaves the tree's root at `+0x38` null. The
  actors' fields are v131072's: 42 functions read the life through `+0x270`, `+0x2B8` and `+0x1C10` / `+0x1C18`,
  hundreds the position as floats at `+0x31C`. Without it enemy health showed nothing on 1.0.0.
- A dungeon keeps all its rooms' actors: Forest Temple had 32+ live enemies, so scene > enemy health keeps the 32
  nearest Link within 30 m. Its summary (`actors:` lines) goes to the log every 300 frames while it's on.

### 9.6 The scene's depth (1.0.0, nxnative; checked 10th November 2026)
- **The texture:** Depth32F (NVN `0x34`) at the render resolution: 1536x864, smaller under dynamic resolution
  (1457x820 seen), at offset 0 of the game's 305 MB render-target pool (flags `0x2a1`). The texture *object* is
  new every frame or two (16+ addresses, its texture-pool ID with it): never keep it, take it from the game's
  calls. Cleared to 1, test LESS, depth range 0..1, GL style (near = -w), no jitter in the projection.
- **Its copy:** right after its depth pre-pass the game copies it whole into a second Depth32F of the same size
  (`nvnCommandBufferCopyTextureToTexture`), then a half-size one the same way: one each per gameplay present.
  scenedepth.c takes the first full-size Depth32F copy since the last present (and a smaller one only if it's
  at least half the area of the last). The pre-pass is recorded on several command buffers, each object in an
  occlusion query (`ReportCounter` SAMPLES_PASSED / `ResetCounter`); no pass of the characters alone was found.
- **Later:** after its last use (the upscaler) the game places small HUD targets at the depth's own pool offset
  (177x40 RGBA8 at +0 ... +0x24600, 192x192 R8 at +0): on the console they overwrite its top left before the
  present (nxnative keeps textures of different formats apart, so there it stays whole). Read it at the copy.
- **Conversion:** the camera (`camera()`) holds sead's PerspectiveProjection at `+0x90`: the projection at `+0x9C`,
  device z scale / offset `+0x120` / `+0x124` (1, 0), near `+0x128` (0.1), far `+0x12C` (25000), vertical fov
  `+0x130`, aspect `+0x140`. Distance ahead = n f / (f - d (f - n)); d = f (1 - n / z) / (f - n) (`view_depth`).
  Link's torso read 3.41 m where the CPU projection put his middle at 3.54.
- **Memory:** the game's heap gave one 9.6 MB block but not 15 MB (a Depth32F the picture's size at 2560x1440),
  nor a second 5.3 MB block next to the 5.3 MB depth; held 1 MB blocks ran out at about 10 in gameplay. The
  pictures (`gameimage.c`) showed it too: a tester's decoding of `Common.bntx` got its 10 MB window, then failed
  early on (after 5.6 and 8 of the 144.5 MiB it decodes) on the decoder's 17 KB and 41 KB blocks while the game
  loaded; on the Mac, 4.6 MB for the cached pictures wasn't there while gz's style still held its 23 MB of
  panels and see-through buffers.

### 9.7 Where a load puts Link: the restart point, SavePos and the safe-place search (v131072; checked in game)
After a save and load Link sometimes appears somewhere other than where he saved, sometimes past a wall. Two
things cause it: the save records a *restart point*, not Link's position, and the load runs a search around that
point.
- **The restart point** (P = Link's player component `[[actor+0x270]+0x358]`) is updated every frame by
  `main+0x60A41C`. It's skipped (the point stays where it was) when any of these hold:
  - state id (`[[[actor+0x270]+0x188]+0x1C0]+0x28`, or `+0x4C` when that is `0x11A`) is `0x22`, `0x23`, `0x24`, `0x56`
    or `0xFF` (unless P`+0xE5D` bit 2), or `0x9C`;
  - P`+0x21` bit 1, `[[main+0x4E94840]+0x68]` ≠ 0 (probably an event running), `+0xB8` bit 14, P`+0x13D0` bit 9;
  - not grounded (P`+0x13D0` bit 2) and not standing on a body;
  - on a body: P`+0x1C34 & 0xC8`, P`+0x13D0 & 0x1182800`, P`+0x2C` bit 19, or the P`+0x15D4` timer > 0;
  - hard-coded boxes in WaterTemple and CrumblingGanonCastle;
  - deep water (water info `+0x78` depth, `+0x38` surface y, compared with Link's y).
  - P`+0x13D0 & 0x1182800` (with P`+0x1C34 & 0xC8` and P`+0x2C` bit 19). P`+0x13D0` is Link's ground flags,
    cleared and rebuilt every frame by `main+0x5F90A0`:
    - bit 11: outside |x| ≤ 5000, -3000 ≤ y ≤ 5000, |z| ≤ 4000.
    - bit 24 (`main+0x5FA87C`): **off valid ground**. A sensor component (`[actor+0x270]+0x460`, filled by
      `main+0x38AEF0` from grid lookups round Link's rounded position) has two probes:
      - `+0x37C` bit 0 hit, `+0x510` distance;
      - `+0xBBC` bit 0 hit, `+0x115C` distance.

      Bit 24 is set when the first hits 5 m or more away (or with no distance; not while P`+0x13D0` bit 18 is set),
      or misses, whatever the second does (and then bit 25 too when the second hits 8 m or more away). On map
      floor both hit close by. On out-of-bounds floor only the second hits, so the point stops following Link
      there. Out-of-bounds floor near valid ground (within 5 m) is expected to keep it following (not tested).
    - bit 20 (`main+0x5FB84C`), the same step's next part, after bit 24 is settled: the cell's attribute 8
      (`[S+0x108]` vfunc `+0xB0`, arg 8, returns value and valid bytes) is set, or it isn't and vfunc `+0xB8` gives
      1 or less (that value is only read when the first probe hits). Seen holding the point out of bounds as
      young Link, where bit 24 wasn't set.
    - The bit-24 step is skipped while an event runs (`[[main+0x4E94840]+0x68]` ≠ 0, the same check as the update's),
      if `[actor+0x270]+0x460` `+0xA8` bit 1 is clear, or if P`+0x1C54` bit 0 is set.
    - **Putting the ocarina away** is an event. In the frame it ends, the flags are rebuilt while it's still running
      (no bit 24) and the update runs after it has ended, so the point jumps to Link once. The mod only shows where
      bit 24 or 20 holds the point (`savepos.c`, a dark green mark); it doesn't move the point. Clearing the bit from the
      mod's frame does nothing: the mod runs outside the window between the rebuild and the update.

  On map collision the point becomes the actor's position (`actor+0x31C`), stored at P`+0x18A4` with yaw P`+0x18C4`,
  and P`+0x18B0` = 1 (ready; P`+0x30` bit 11 is set too). On a body (a moving block, a platform) `main+0x2E7FE80`
  decides; a refusal sets the cooldown P`+0x18CC` = 35, which counts down and blocks the hand-off below; unsafe
  frames set it to 1. P`+0x1948` not NaN: an area sensor forces the point (P`+0x1950..0x1974`). A second copy at
  P`+0x18B4`/`+0x18C8` is used while P`+0x1C34` bit 4. The point is also read by `main+0x2814620`, the enter
  of the AI node `ExecutePlayerUseWarpItem` (Farore's Wind), and by `main+0x30170E0`.
- **The hand-off** at `main+0x60830C` (inside `main+0x6080C0`, which also writes `PlayerStatus.CurrentPos` /
  `CurrentDir` every frame) copies it to AutoSaveMgr (scene component 12, §9.3) slot B: yaw `+0x178`, position
  `+0x17C`, valid `+0x188` = 1, `+0x189`, `+0x18A`. Slot A (`+0x198` yaw, `+0x19C` position, valid `+0x1A8`) comes from
  elsewhere and wins when valid.
- **SavePos:** every frame AutoSaveMgr's update (`main+0x32762B0`) writes the chosen slot to `PlayerStatus.SavePos`
  (member `0x796246EA`) and `SavePosRadY` (member `0x3BE1E51E`, MurmurHash3 of that name) and sets bool
  `0x945046CE`. A save stores both CurrentPos and SavePos. SavePos always equals the restart point and CurrentPos
  Link's position; saving from the same spot reloads at the same place every time (it isn't a timing effect).
- **The load:** `TitleToMainWithLoadSave` places Link at SavePos / SavePosRadY, then `SceneBeginning.module.ainb`
  runs `SeqQueryPlayerSearchSafetyPlace` (input Position, IsRiding) and `SeqOneShotSetPlayerPos` with its result
  (`IsWaitBeforePlayerSafetyPos` waits for it).
- **The query node** (name `0x3302290`, vtable slot `0x4BCF368`, binder `0xD87BA0`), calc `main+0x945220`:
  - the player: `main+0x945110` (node → scene) → `main+0x945170` (scene component `[S+0x58]+0x78`, ResidentActorMgr,
    component 15) → `main+0x37CCA0` → `main+0x375BA0(&handle, 0, 1)`, a counted handle with the actor at `+8`,
    released by `main+0x947300(handle+0x20)` when its byte `+0x10` is 1. Probably the actor `link_actor()` returns
    (not checked);
  - Position is `[node+0x20] & ~3`, the output `[node+0x30] & ~3`, IsRiding `[[node+0x28]]`; if riding,
    `main+0x30256C0(player, &info)` fills an 8-byte info `{u32, u8}`, passed only when its u32 isn't 0;
  - `found = main+0x945310(player, &pos, &out, riding_info_or_NULL, 0, 0)`, stored at `node+0x38`.
- **`main+0x945310`** (the search):
  - `main+0xA47390(&flag, &a, &b, player, &pos, riding)` checks the spot, with extents = an empty box copied from
    `[main+0x4C7F978]` (FLT_MAX / -FLT_MAX). What it checks isn't known. It sets `fallback` (below): 1 when flag = 1
    and (`a` = 0 or `b` ≠ 4), else 0.
  - Riding, or the check failed: `main+0xA46090` searches sideways. If it finds a place the result is (x, saved y, z)
    and the search stops.
  - Otherwise (or if that failed) `main+0x3024A20` runs the ring search. If it finds nothing, out = the input and
    found = 0.
- **The ring search** `main+0x3024A20(out, player, &pos, fallback, extents, riding, info, flag)`. Candidate offsets
  (x, y, z) from the saved point, in the order tested:

  | ring | radius | directions | heights y |
  |---|---|---|---|
  | 1 | 1, plus the centre | 8: +z, +x+z, +x, +x-z, -z, -x-z, -x, -x+z | centre 0/2/4, the 8 around it 0/1/2 |
  | 2 | 2 | 8, turned 22.5° | 0/2/4 |
  | 3 | 3 | 8 | 0/2/4 |
  | 4 | 4 | 8, turned 22.5° | 0/2/4 |
  | 5 | 5 | 8 | 0/2/4 |

  Inside a ring it goes height by height (all directions at the first height, then the next). Riding: the first
  height only. At most 123 candidates (41 riding).
  - It stops after the first ring with a candidate that is both *found* and *good*. After ring 5 with none good: if
    `fallback` and something was found, it takes the best found; otherwise out = input and it returns 0.
- **One candidate** `main+0x3025930`:
  - the ground under it: `main+0xA46090(..., 1, ...)`;
  - a line-of-sight ray `main+0xA47B40` from the saved point + half Link's height to that ground + half his height
    (height from `main+0x601D80`); blocked → rejected;
  - good = `main+0x3025B70`: straight or L-shaped sweeps (`main+0x3025C90`) between the two;
  - the best so far: good beats not good, then the nearest.
- **Nothing checks that Link could walk there** beyond that ray and those sweeps. That is how a point gets past a wall.
- **In game:** a load that put Link past a wall matched a ring-5 candidate exactly (direction +x-z, height 4, landing
  on the ground just above it), so the point itself and rings 1–4 had all failed; other spots reloaded exactly where
  saved. Arrivals after Farore's Wind are its own warp, not this search.
  - Guess (not verified): that point was slightly below the nearby floor, inside collision. That would fail the
    near candidates, while rays and sweeps that start inside geometry pass through the wall behind it.
- **Not known:** what `main+0xA47390` checks. GameSafetyMgr (`main+0x32857B0`, component 28) handles the story's
  NoEscape areas (`StartPos_` anchors; 28 entries in RSDB `GameSafetySetting.Product.111`); what it keeps is in
  §9.14.

**The mod's preview** (`savepos.c`, scene > save position):
- AutoSaveMgr is found among the current scene's components by its update (`main+0x32762B0`) in its vtable; slots A
  and B are read from it and drawn, the one SavePos takes marked.
- Where a load would put Link: `main+0x945310(link_actor(), &pos, &out, NULL, 0, 0)` with pos = the slot SavePos
  takes; `out` is 3 floats, it returns found. Riding Epona the game passes a riding info (built by `main+0x30256C0`);
  the preview always searches as on foot.
- **Link's own body is part of the scene the search sees.** On a load he stands on the point; elsewhere his body can
  be taken for ground or block a sweep. So the search is run while he stands on the point and that answer kept when
  he walks off; a point he isn't on is searched as it is and marked approximate.
- **It tests today's geometry, not the reloaded scene's:** a moving block will be back at its starting place after
  a load, and actors placed since will be gone. The preview is only exact on static map collision.
- Cost: up to 123 candidates, each with ground casts, a ray and sweeps, so it's run only when the point changes (at
  most every few frames while it moves) and refreshed now and then while Link stands on it.
- In Ghidra `thunk_FUN_03c41c50` is marked no-return, so decompiles stop after it (FUN_00945220, FUN_00375BA0):
  read the disassembly (`tools/re64.py dump/v131072 func <offset>`).
- **1.0.0 (v0; found 22nd November 2026):** `main` was dumped from Lichii's memory (its exefs `main` is `ZBIC`
  compressed): MOD0 at `main+0x3BE705C`, main at `0x80FC1000` that run (the base that puts the most vtable entries
  on `pacibsp`). AutoSaveMgr's update is `main+0x3217250` (v131072's `0x32762B0` matched by masked signature; same
  code but for its global loads), its vtable `main+0x4B3B5F8` (v131072's `0x4BA0848`, all six matchable slots the
  same). The safety search is `main+0x9444A0`, identical but for its GOT load; both callers match (`0x9443B0`,
  `0x26691F0`). The scene manager `[main+0x4E271E8]` and event state `[main+0x4E243E0]` are 0x70460 below v131072's,
  as the other bss slots are (§9.5). In game: the scene's entry 12 was AutoSaveMgr with slot B set and `EvtWait`
  at `+0x1DC`, and the event state is the event manager object (`[[main+0x4C183E8]]`). Player fields
  (`+0x13D0`, `+0x1948`, the actor's `+0x31C` / `+0x330` / `+0x348`) are used by as many instructions in both builds.

### 9.8 Where Link voids out: the ground map, its two probes and the void areas (v131072; from the disassembly, the view checked in game 14th November 2026)
Link can void on ground that looks solid and walk on thin air without voiding, because the void check doesn't use
the collision. It uses a voxel map of the walkable ground. Checked in game: the deadly cells, the 8 unit rule and
the flags they set (the view and its log agree with where Link voids). From the disassembly and the files only:
the probes' workings, what switches the flags off, and the void areas' shapes.
- **The ground map** (VolumeStats, `Phive/VolumeStats/<Area>/*.bpvsts.zs`): its manager is
  `[[[[main+0x4C7DF00]]+0xC0]+0x170]`. `+0x10` heads a linked list of the loaded areas (`+0x18` the first node,
  `+0x24` an i32: where the node is in an area; a node's `+0x08` the next). An area's holder `[area+0x90]` has
  `+0x28` a count and `+0x30` pointers to its chunks. A map has many loaded at once (96 to 144 seen), most of them
  far from Link.
- **A chunk** is 256^3 one-unit cells as a sparse octree of 8 levels: its origin (3 x i32, cells) at `+0x00`,
  the levels' node arrays at `+0x168 + level * 8`. A node is a u32: bits 0-7 which of its 8 children exist (bit
  `x | y << 1 | z << 2`), bits 8-31 the index of its first child in the next level's array; the children there are
  come one after another, so child c's index is that plus the count of set bits below c. Chunks overlap: one only
  answers for its cells 3..253. A cell that's set is walkable space just over the ground.
- **The cells' records:** each level has a bit stream at `chunk+0x28 + level * 0x28`: `+0x10` the data, `+0x18` the
  bits a record, `+0x1C` its format; record n of level L belongs to node n of level L + 1 (level 7's to the cells).
  The 7 formats are a table at `main+0x3EE8840`, 0x74 bytes each: `+0x04 + f` a byte, 1 if the format has field f,
  `+0x1C + f * 4` the field's first bit (a format past the table reads as format 0). A field is read from the
  deepest level whose format has it, bits lowest first. Only format 6 has fields 20 (1 bit) and 21 (4 bits, times
  2/3); in the files the overworld's chunks use it and the dungeons' don't, so a dungeon's cells should never be deadly
  (not checked in game).
- **The probes** are in Link's probe object `[[actor+0x270]+0x460]`, run once a frame (`main+0x388340`):
  - probe A casts straight down from a little over Link (5000 units long, so from any height), from Link's middle and
    in turn from 8 points 0.6 units round it: hit `+0x37C` bit 0, where it landed `+0x360`;
  - probe C looks for the nearest set cell with 64 rays all round (only 7 of them a frame once it has one) and
    keeps the best it has found until a nearer one turns up: hit `+0xBBC` bit 0, the cell `+0xBA0`, its distance
    `+0x1158`, and that distance level (x and z only) `+0x115C`. The old point is never checked again, so off the
    map it's as good as frozen;
    - **How probe C updates (`main+0x38AEF0`, 1.0.0 `main+0x4D8810`):** starts from the probe's query position
      `Q = probe+0xE0` (when probe A hit, first casts from probe A's hit cell centre toward `Q`, snapping `Q` to
      any intervening hit cell centre). Computes the current **3D** distance `|Q - C|` (not 2D horizontal) to
      probe C's remembered point `C = probe+0xBA0`. Across a 10-frame phase counter (`probe+0x1160` = 0..9,
      stride 10, 6–7 rays/frame; all 64 when `+0xBBC` bit 0 is clear), fires 64 Fibonacci-sphere rays
      `y = 1 - (2i+1)/64`, `r = sin(acos(y))`, `theta = i * 3.8832221` (`2*pi/phi`), up to `L_max = [[probe+0xA0]+0x150]+0x44`
      (`5000.0` if `< 0`). The octree DDA (`main+0x543240`) with filter `main+0x3209720` hits only when a ray
      **exits** a mapped leaf cell (level 7) across face `d` (`0:-X, 1:+X, 2:-Y, 3:+Y, 4:-Z, 5:+Z`) whose
      6-bit `Intersect` (field 0) or `IntersectWater` (field 19) bit `1 << d` is set. Probe C replaces `C` if
      and only if a ray hits at a **strictly smaller 3D distance** than `|Q - C|`.
    - **Phive collision raycast for ground shading:** `main+0x5FF750` (1.0.0 `main+0x582D00`, unique 48-word
      signature match) casts `(from, to, &hit, ignore, layer=0x1A, 0)` using a `0x180`-byte stack `RayCastHit`
      with vtables `main+0x497FBA0` / `main+0x4C31470` (1.0.0 `main+0x491B278` / `main+0x4BCB868`), returning
      hit pos at `+0x24` and normal at `+0x30`. voidmap.c samples ground heights on a world-snapped disk
      around Link (radius 11 cells, incrementally shifted as Link walks) and evaluates probe C's 64-ray check
      on each cell to shade ground green (fence moves) or red (fence stays stuck).
  - `+0xA8` bit 18: Link is more than 5 units outside the probe's own box.
- **The flags:** `main+0x5F90A0` rebuilds Link's ground flags P`+0x13D0` every frame (P = `[[actor+0x270]+0x358]`):
  - bit 24 (0x1000000): probe A missed, or its hit is 5 units or more under Link. Never voids by itself (it only stops
    the restart point following Link, §9.7);
  - bit 25 (0x2000000): probe A missed **and** probe C's cell is 8.0 units or more away level. Voids;
  - bit 19 (0x80000): the cell probe A landed on has field 20 set and field 21 over 3.0, or probe `+0xA8` bit 18.
    Voids.

  None of them is set while the probe hasn't run since its reset (`+0xA8` bit 1 clear), while
  `[[main+0x4C7E090]+0x68]` ≠ 0 (an event running: talking, a chest, a cutscene, a loading zone) or while P`+0x1C54`
  bit 0 (the frame after an event). `main+0x82C510` turns them into the void (its type at P`+0x191C`); it only runs
  with Link's own update, so nothing voids while the game is paused (the ocarina, the menus).
- **The void areas** are placed `Area` actors that a layout's `AiGroups` tie to a logic module
  `Gimmick_AreaContact_FallDown`, `..._FallDownMountian` or `..._FallDown_GanonCastle` (their node
  `HoldSensorHitInfoGiveAbyssActor`, `main+0x31E4614`, sends a death request, reason FallIntoAbyss, to what its
  sensor holds; `..._FallDown_Item` is for dropped items and isn't one). 131 in 39 maps. 73 of them are thin
  discs (a cylinder of scale 1, 0.1, 1) at the holes down into grottos, which start with the same fall; 58 are
  boxes, the real void volumes (Ganon's Castle has one 800 units across under everything). An Area has no shape in
  its pack: `main+0x334FD40` makes it from the placement, reading the blackboard's `RigidBodyShapeType` (1 when
  it isn't given: a box; 3: a cylinder; 0: a sphere). Its origin is the middle of its bottom: the shape's centre is
  Scale.y up the actor's own Y axis, a box's half extents are Scale, a cylinder's radius Scale.x and height
  2 Scale.y. Not checked in game: the exact placement of each shape, and whether every one is on all the time.
- **Not from either:** floors whose material is tagged Abyss void on touch (P`+0x13E8` bit 4, read from the
  ground under Link within about 2 units), and crushers and the like send the same death request. The tag
  (user shape tag 4; presets `Floor_Abyss`, `Material_Sand_Abyss`, `Material_Metal_Spike_Abyss`) is on 84
  triangles of the whole game's static collision and on no actor's mesh but one room's: a flat floor at
  y -39.4 under the Shadow Temple's chasm (10 triangles), and two spike blocks there, which hurt instead
  (a Spike sub-material cancels the tag). So it isn't drawn.
- **The view** (voidmap.c, airwall.c): the cells round Link (48 x 64 x 48, 48 of the 64 under Link) are read out
  of the chunks that reach that box every 120 frames or when Link has moved 4 cells; the outlines are made Link's
  layer first, then outward, so that what's left out when there are too many is far away. About 3 ms a read on
  Lichii in the busiest place found (Kokiri Forest's hillsides, more deadly cells than fit).
  - **A first version missed cells next to Link** twice: it filled its lines from the box's bottom layer up, so
    the room ran out before Link's layer in a place with many deadly cells; and it stopped at the list's first 64
    chunks, with the ones round Link later in it. A log line saying what the view held where Link voided told
    the two apart.
  - **Faces in the world on the game's UI shader:** its `view` uniform is a full 4x4, so a second one with a
    perspective matrix (x and y by the half fov's tangents, w the distance ahead, z scaled so z / w is the game's
    depth) draws a quad whose element matrix puts it in the world (`gpuui_world`, `gpuui_face`), and the scene's
    depth hides it. Worked as written.

- **1.0.0** (22nd November 2026, from a memory dump): the ground map's slot is `main+0x4C18258` (v131072's 40 loads
  of `0x4C7DF00` map to it) and the record formats `main+0x3E89420` (the same 7 x 0x74 bytes). In game its manager
  (`+0xC0`, `+0x170`) held a loaded area with 18 chunks. Not yet checked in game with the view on.

### 9.9 Ragdolling Link on demand (v131072; from the disassembly, not yet checked in game)
The game ragdolls Link in events with the AI node `ExecuteEventPlayerImmediateRagdollDown` (vtable: name getter
`main+0x49980A0`, enter `+0x50` = `main+0x26B3EC0`, update `+0x58` = `main+0x26B3FB0`, leave `+0x60` =
`main+0x26B42C0`). The same calls are in `ExecutePlayerSlippingDown` (enter `main+0x280C7A0`, leave
`main+0x280CE80`), the player's own slip-and-fall. **The menu's link > ragdoll (`ragdoll.c`) makes the enter's and
the leave's calls.**
- **Finding a node's code:** a node's name getter (`adrp x0; add x0; ret` to its name) sits in its vtable; enter,
  update and leave follow it at `+0x50`, `+0x58` and `+0x60` (checked on `ExecutePlayerUseWarpItem`, §9.7). The
  dump keeps vtable entries as plain `main+` offsets, so search the data for the getter's offset.
- **Objects:** P = the player component `[[actor+0x270]+0x358]` (`main+0x944E50`). C = Link's ragdoll controller,
  `main+0x5F88C0(H)` with H = `[[[actor+0x270]+0x50]+0x20]+0x10` (the object before the controller property in
  `cct.c`'s chain; also `main+0x837620(actor)`). `main+0x5F88C0` walks H's list (count `+0x50`, entries `+0x58`)
  by type and returns NULL when none matches.
- **The enter:** `main+0x9339F0(P, 0.0)` (not followed); `main+0x972A40(P, &zero, 0)` and `main+0x92FDE0(P,
  &zero, 0)` set the body's linear and angular velocity (scaled by 60 when the bool is set); P`+0x1030` = 0;
  `main+0x933270(P, 0, main+0x4997FE8, 0xF, 0)` plays the AS whose names are at `main+0x4997FE8` (`SlippingDown`,
  `SlippingDownLand`) in P's slot 0 (slots of 0xD8 at P`+0x230`); then **start**: `main+0x3218D30(C, &req)`.
  `req` (0x30 bytes): `+0` actor, `+8` f32 1.0, `+0x1C` 1, `+0x20` 2, `+0x24` preset (0 here; 1 from the slipping
  node; indexes the name offsets at `main+0x3F0A39C`: `Default`, `BongoBongoClapEnd`), `+0x28` 0 (`Default`), the
  rest 0. It sets C`+0x17C` = 3 and asks for the reaction `ImmediateDown`.
- **The update** only drives C while Link is in the air (P`+0x13D0` bit 0 clear): `main+0x321C040(C, &drive)`
  then `main+0x38911A0(C)`. On the ground it does nothing to the ragdoll. `ragdoll.c` doesn't drive it, and of the
  enter it leaves out `main+0x9339F0` and the P`+0x1030` write (not understood).
- **The leave (end):** `main+0x3219090(C, &{actor, u8 0})` (returns at once while C`+0x17C` = 0), then P`+0x1C3E`
  |= 1. The slipping node's leave checks C`+0x188` = 3 first.
- **Bombs and big hits** don't use ImmediateDown: `ExecutePlayerReactionHugeHit` (vtable getter `main+0x27FCB70`, enter `main+0x27FB9C0`, update `main+0x27FC220`, leave `main+0x27FC3A0`; MediumHit enter `main+0x27FCF70`) asks C for a knock-back with `main+0x3217550(C, &req)` (C`+0x17C` = 1). `req` (0x40 bytes): `+0` actor, `+8` f32 1.0, `+0xC` f32 the hit's angle against Link's facing, `+0x10` f32 the throw's pitch (radians, up to pi/2; from a table via `main+0x27FCA50`), `+0x14` 12.0, `+0x18` 6.0, `+0x1C` 1, `+0x20` 2, `+0x24` the reaction (`main+0x3F0A218` table: `Rolling_Weak`, `Rolling_Medium` (HugeHit's default), `Rolling_Strong`, `Rolling_RestraintThrown`), `+0x28` the variant (`OnGround`, `InAir`, `InWater`, `Climb`, `Ride`, `LikeLike`, `Morpha`, `BongoBongo`), `+0x2C` 0 = `Default`, `+0x30` vec3 the direction (x, z kept, normalised into C`+0x118`), `+0x3C` 1, or 2 when the hit is more than 75 degrees off his facing. **`ragdoll.c` now does this** (medium, his facing as the direction, angle pi, pitch 0.35 guessed), not yet checked in game.
- **Not checked:** whether Link's normal AI lets the ragdoll last outside an event, and whether C`+0x17C` going
  back to 0 is how the game ends one itself (`ragdoll.c` logs C`+0x17C` / `+0x188` as they change, tagged
  `ragdoll:`). 1.0.0 offsets aren't known.

### 9.10 Ending the running event (1.0.0; 1.1.1 addresses in brackets; checked in game on 1.0.0)
- **The event manager:** M = `[[main+0x4C183E8]]` [`[[main+0x4C7E090]]`] (`event_mgr_ptr`). E = `[M+0x68]` is the
  running event (0: none; the same value as `[[main+0x4E94840]+0x68]`, §9.7). E holds its flows: pointers at
  `E+0xEB8`, count (s32) at `E+0xEB0`, current index (u32) at `E+0xF18`. A flow's name is a `char *` at `flow+0x8`,
  its player P at `flow+0x220`, and a u64 of flags at `flow+0x3B8`:
  - bit 11 (`0x800`): stop. Set by `main+0x3ADD5B0(flow)`.
  - bit 12 (and 13): ended by the flowchart. Set by `main+0x3ADD5C0(flow, bool)`, which also sets bit 11 of
    `[flow+0x228]+0x1F8`.
  - bits 25-26: a timeline is playing (below).
- **The event's update** [`main+0x3B3BB68`] calls the current flow's P slot 3; when it
  reports done, it moves on to the flow named at `[flow+0x98]` unless bits 11-12 are set.
- **Two kinds of event:**
  - **Timeline** (a played-back cutscene): the flow's bit 25 is set while its timeline plays. The flow's vtable (1.1.1
    `main+0x4C62D58`) starts the timeline with slot 18 `main+0x3AE1D60` [`main+0x3B3F460`] (time `flow+0x3F8` and the
    timeline player `[flow+0x3E0]` back to 0, then `main+0x3ADD7D0(flow, true)` [`main+0x3B3AF50`] sets bit 25),
    advances it with slot 20 and clears bits 25, 26 and 28 at its end with slot 21. Calling slot 18 on a running
    event restarts its timeline.
  - **Flowchart** (camera moves and text boxes): bit 25 is never set. Kakariko Village's first arrival
    (`Dm_Kakariko_0000`, flags `0x80004083`) and Impa's farewell (`HyruleField_DoorOfTime_SendOff` >
    `Dm_HrlField_0020`) are flowcharts.
- **The game's skip** (timelines only): `SeqOneShotEventRequestForceSkip` (vtable `main+0x4B4B678`, slot 39
  `main+0x32729D0` [`main+0x4BB0970`, `main+0x32D1890`]) and `SeqExecuteEventForceSkipAndWaitFinish` (enter
  `main+0x3260700`, update `main+0x3260770` [`main+0x32BF870`, `main+0x32BF8E0`]) call `main+0x3ADD7B0(flow)`
  [`main+0x3B3AF30`] on the current flow: P's vtable slot 11, `main+0x3AE3F30` [`main+0x3B41630`] for P vtables
  `main+0x4ABC640`, `main+0x4BFD318` [`main+0x4B21610`, `main+0x4C62F20`]:
  ```
  if (P+0x2C == 3 /* playing; slot 8 */ && [P+0x10]+0x3B8 bit 25 /* timeline playing */
      && main+0x3AE2F40([P+0x20]))  /* [main+0x3B40640]: settings object (slot 9) +0x1A6 == 0 and +0x1A5 < 1 */
    P+0x28 |= 0x20;                /* skip requested */
  ```
  The WaitFinish update repeats it every 180 updates until `[M+0x68]` is 0 and `M+0xB8` bit 14 is clear. Only the
  title sequences use these nodes (`Sequence/BeforeTitle(.110)`, `CloseTitle(.110)`).
- **The manager's cancel** (any event): in its update's loop over events (`M+0x278`, `main+0x3AEE85C`), an event
  without `E+0xF24` set is cancelled when `M+0xB8` bit 2 is set (all events) or bit 3 is set and it is the running
  event: `main+0x3ADD5B0` on the current flow (bit 11) and `E+0xF24` = 1. The node runner (`main+0xE52450`) then
  drops the flow, and the event is gone the next frame (both flowcharts above, checked in game). A flowchart's own
  end (`main+0xE53114`) does the same with `main+0x3ADD5C0(flow, true)`.
- **No other event starts while one runs** [`main+0xD7A9D0`, the acceptance behind the request submit]: a request
  is refused when `M+0xB8` bit 0 is set, when the running event's name at `[M+0xC0]` differs from its own, or
  whenever `[M+0x68]` ≠ 0 and it isn't a sub-event. The `PrioritySetting` priorities (table `main+0x3F6FCF0`
  [1.1.1]: 1, 10, 50, 300, 400, 600, 999; lower wins) only decide between queued requests (`M+0x300`) while nothing
  runs. So a warp or map reload can't interrupt an event (checked in game: refused, `M+0xB8` = `0x28000`).
- **Settings:**
  - `SkipSetting.IsDisableSkip` (`engine__event__SkipSetting`, byte `+0x39`): set only by the component
    `Event/EventSetting/EventSettingComponent/SkipSetting/DisableSkip`, used by `Dm_Kokiri_0000`, `Dm_Title_0005` and
    `Dm_StaffCredit_0005`. The force skip and the cancel don't read it.
  - Skip settings named in main but used by no event file, AINB or event flow in romfs: `IsEventSkipedMostRecent`,
    `IsStopByEventSkip`, `BgmsStopOnEventSkip`, `IsSkipAutoPlayNextEvent`, `IsASEventSkipOnlyAttack`,
    `IsASEventSkipPosUpdate`. `IsNotOpenIfSkipedMostRecent` is used once, by `AI/MiniGameNpcCommon.event.root.ainb`.
  - A screen named `UIEventSkipScreen` exists (`main+0x33DC040` [1.1.1]); what opens it wasn't traced.
- **Story flags:** an event ended early doesn't run the rest of its flowchart, so its later GameData writes and the
  `HavePlayedEvent.<event>` the game sets when it plays may be missing; the flag log (`flaglog.c`) shows which.
- **`eventskip.c`** (scene > end cutscene): skips every flow with bit 25 set (with the bounds checks, repeated every
  180 frames like the WaitFinish node, up to 1800 frames); an event with no timeline gets the manager's cancel. It
  logs each flow on the first press (`eventskip: flow i/n ...`) and when the event is gone.

### 9.11 Save captions: the CaptionMgr and reloading it (v131072; from the disassembly, not yet checked in game)
After **save management > restore** the files on the save mount are the archive's and loading a slot gives the
restored progress (checked in Lichii: `SeqExecuteLoadSaveData` reads `progress.sav` from the mount when it loads),
but the Load list still shows the old captions (location, date, picture). They come from the **CaptionMgr**, which
reads every slot's caption once, at boot, and keeps it.
- **The instance:** `*[main+0x4E78E38]` (also reached as `[[main+0x4C81660]]`), created at boot by
  `main+0xEC9370` (size 0x98, vtable `main+0x4BDFFD8`, only a destructor in it). Fields:
  - `+0x08` count (s32), `+0x0C` capacity (6), `+0x10` pointer to the entries of the six normal slots; `+0x48`,
    `+0x4C`, `+0x50` the same for six more, read from `slot_06`..`slot_11` (index + 6, `main+0xCBC428`): the Master
    Quest slots. `+0x91` (u8) picks which six the menus show; `SeqOneShotSetMQMode` (vtable `main+0x4BBD360`, work
    `main+0x32E5480`) sets it.
  - `+0x88` latest index (s32, -1 none; `SeqOneShotSetCaptionMgrLatestIndex`, work `main+0x32DFF80`, sets it from
    the slot loaded). `main+0x33AD5D0(mgr)` returns it, or when it's -1 the slot of the newest caption.
  - `+0x8C` state (u8): 0 idle/empty, 1 loading, 2 sorting, 3 (a different job, `main+0x9C5210`), 4 loaded.
  - `+0x8D` bit 0: load requested. `+0x8E` / `+0x8F`: some normal / MQ slot has a save.
  - `+0x90` (u8) "can receive load request": 1 from the constructor; `SeqOneShotSetCaptionMgrCanReceiveLoadRequest`
    (vtable `main+0x4BB9958`, work `main+0x32DFDA0`) writes its input there, but no `Sequence/*.ainb` uses that node
    and no code reading the byte was found. Not needed.
  - An entry (0x5A0 bytes) has its state at `+0x00` (0 empty, 1 reading, 3 read, 4 picture building, 5 done),
    its slot `+0x04`, and `+0x280` bit 0 = the slot has a save.
- **"Stable"** (what `SeqExecuteWaitCaptionMgrStable`, update `main+0x32C6410`, and `ConfirmationLoad` wait for):
  state 0, or state 4 with every entry of both lists in state 5. The menus' getter `main+0x33AD490(mgr, &entry,
  index)` returns false unless state is 4 and all entries are 5.
- **The update** `main+0x4E5420(mgr)`, called from `main+0x340A460` (slot 2 of the singleton at
  `*[main+0x4E7CFA8]`, vtable `main+0x4BF0FA0`, which also holds the `save` mount): state 0 with `+0x8D` bit 0 →
  1; state 1 steps each entry in turn (`main+0xCBC350` asks the scene's save reader for `slot_NN`'s caption, which
  sets the entry to 1; `main+0x4EA190` waits for the reader (bit 0 of its `+0x158` done → `main+0xBAEBB0` takes
  the caption → 3; bit 1, no file → 5, no save); `main+0xBAE850` builds the picture → 4; `main+0x9C5320` → 5),
  then → 2; state 2 sorts both lists (`main+0xBAE520`), → 4 and clears `+0x8D` bit 0. "Serialize" in the node names
  is that taking of the read file into the entry.
- **`SeqOneshotRequestLoadAndSerializeCaption`** (vtable `main+0x4BC3030`, work in slot 39 `main+0xF6DA00`) calls
  `main+0xF6DA40(mgr)`: if the state is 1 to 3 (or 4 with an entry not done) **or the state is 4**, it only sets
  `+0x8D` bit 0 and returns, and nothing reads that bit in state 4. So once loaded, the game's request does nothing:
  only boot (`FormalSequence.root`, `BeforeInitializeCommon.module`) loads captions. From state 0 (after a check
  on the scene's save reader) it sets bit 0 and calls `main+0xBB0010(mgr)`: every entry of both lists emptied
  (`main+0xBAFF10(entry, index, isMQ)`), `+0x8E` cleared, state 0 (`+0x88` and `+0x91` kept). The full reset
  `main+0xBAFE50(mgr)` (also clears `+0x88` to -1, `+0x8D`, `+0x91`) only runs from a callback the save singleton
  keeps (`main+0xBAFD40`, at its `+0x788`), when the CaptionMgr is stable.
- **To reload after a restore (untested):** on the game's thread, once `stable(mgr)`: `main+0xBB0010(mgr)`, then
  `+0x8D |= 1` (what `main+0xF6DA40` does from state 0, without its check), optionally `+0x88 = -1`. The update
  then reads all twelve slots again, one at a time, and the Load list (which waits for stable) shows them. Don't do
  it while the game saves or loads: the read goes through the same save reader, one request at a time
  (`main+0xCBC350` doesn't queue when it's busy).
- **Not part of the save mount:** the game safety temporary save is `savt:/TemporaryGame.sav` (temporary storage,
  `main+0x3032330`); a restore leaves it alone.
- **To check in game:** `[*[main+0x4E78E38]+0x8C]` is 4 in the field and `+0x08` / `+0x48` are 6; after the reload
  it goes 0 → 1 → 2 → 4; a breakpoint at `main+0xCBC350` is hit twelve times (more if the reader is busy); and
  `main+0x4E5420` runs every frame on the same thread as the mod's frame hook.
- **Checked in Lichii:** after a restore, `main+0xBB0010` then `main+0xF6DA40` makes the update read every slot's
  caption again (about 58 frames).
- **What the Load list shows comes straight from the CaptionMgr** (from the disassembly and the sequence graphs, 18th
  November 2026; not checked in game unless stated). The GameData struct `CaptionData` (`0x46926D32`) holds only
  `SelectedSavePathIndex` (`0xD13D898B`), the slot to load. The list screen is `UISystemLoadListScreen`
  (`SystemLoadList_00`, name hash `0x73033AAB`), vtable `main+0x4BEEBA0`:
  - `+0x2B0` its list (0x80 bytes, vtable `main+0x4BEEF08`, made by slot 35 `main+0x3400B40` for the part
    `N_SlideList_00`): `+0x30` the scroll widget, `+0x60` (s32) / `+0x68` the rows (0x290-byte items, made by slot 93
    `main+0x3400410` through `main+0x34006D0`). `+0x2C0` (u8) "closable" (`SeqQueryIsClosableLoadListScreen`): 1 from
    slot 104 (`main+0x3400970`), 0 after a decide (slot 50).
  - A row is filled when the list binds it (list vtable slot 18, `main+0x3400C70` → `main+0x33CC800(item,
    index)`): the getter `main+0x33AD490`; if it returns false (not loaded) **the row is left as it was**; otherwise
    `main+0x33CC8F0` writes the date into `SimpleIntForMessage`/`2`/`3`, `SimpleHourForMessage(As24Hour)`,
    `SimpleMinForMessage` to format the text (the patch mailbox's day-of-month clash, §5.3), sets the icons, and
    `main+0xE4B660(picture, &entry->+0xF0, 0)` copies the entry's texture handle into the picture pane (item `+0x168`).
    The row keeps the index (item `+0x258`), not the entry pointer. Rows are bound when the screen is refreshed and
    when scrolling brings a row in (list vtable slot 9, `main+0x37739F0`: shows the row, then binds it); not every
    frame. Focus (screen slot 46, `main+0x34009D0`) and decide (slot 50, `main+0x3400AB0` → `main+0x33CE110(item,
    button, row)`) call the getter again: focus writes the row's slot (-1 if the getter fails) to the save singleton's
    `+0x540` (`*[main+0x4E7CFA8]`); decide writes it to `CaptionData.SelectedSavePathIndex` and the row to
    `SelectedSaveDataListBtnIndex`. So an A while the captions aren't loaded gives -1 and `LoadList` just waits.
  - **Slot 98, `main+0x3400810(screen)`, refreshes it** (what opening it runs): every row reset, every row hidden
    (`main+0x3773490(list)`: `SetVisible(false)` on each row's pane, `main+0x20AEA0`, pane `+0xA8` bit 0), the row
    count set from the CaptionMgr (`+0x0C`, or `+0x4C` in MQ mode) with the list marked for binding
    (`main+0x3773510`, list `+0x55 |= 4`), focus on `SelectedSaveDataListBtnIndex` (clamped, `main+0x37735D0`),
    and `CaptionData.SelectedSavePathIndex = -1` (decide sets it).
  - The confirmation (`LoadCheck`) uses `UISystemSaveLoadScreen` (`SystemSaveLoad_00`, vtable `main+0x4BEF840`),
    loaded on entry and closed and unloaded on leaving; its slot 98 (`main+0x3405000` → `main+0x3405080`) binds
    the chosen row the same way in Load mode (`+0x2AC == 1`), otherwise shows the manual-save caption. The getter's
    only callers are `main+0x33CC800`, `main+0x33CC8F0`, `main+0x33CE110` and `main+0x34009D0`.
- **An entry's picture:** `+0xF0` (u64) the texture handle (-1 none), copied from `+0x90` by `main+0x9C5320` (state
  4 → 5); `+0x28` the texture and `+0x38` / `+0x40` its memory; `+0x18` the read file (freed once the picture is
  made); `+0x1C0`..`+0x1D0` the date; `+0x280` bit 1: has a picture. Emptying it (`main+0xBAFF10`, run by
  `BB0010`) sets `+0xF0` to -1 and destroys and frees the texture's memory at once (no deferral seen). **So a Load
  list that's up while `BB0010` runs keeps drawing the old handles** of freed textures until it's refreshed, and a
  refresh before the captions are loaded leaves them too (rows are shown, the getter returns false, the row isn't
  touched). What that does on hardware isn't known (garbage at best, a GPU fault at worst); the game itself only
  empties the entries at boot and when nothing shows them.
- **`SeqExecuteSerializeCaption`** (vtable `main+0x4BA82C0`; outputs `SerializedCaption` and `Size` registered in
  slot 28 `main+0xDA9C00`; update in slot 30 `main+0xB19E70`) has nothing to do with the list: it asks the save
  singleton `*[main+0x4E7CFA8]` to serialize a caption of the moment (`+0x480` state 6: start, `main+0xB19F00(+0x78)`;
  state 10: done, buffer `+0x470`, size `+0x478`). Only `PlusMinusPauseCapture.module` uses it, after the screen
  capture: the result goes into the GameData binary `UI_ManualSaveCaption` (`UI_IsValidManualSaveCaption = true`),
  which `Save.module` saves and `SeqOneShotManualSsetCaptionData` (work `main+0x32D73A0`) puts into one entry
  (`main+0xBAE810`: empty it, take the caption, build the picture). `FormalSequence.root` and
  `BeforeInitializeCommon.module` use `SeqOneshotRequestLoadAndSerializeCaption` (above), not this.
- **The menus' flow** (`ConfirmationLoad`, `LoadList`, `PlusMenu`): Load on the System page runs `ConfirmationLoad`:
  it sets the menu screen Enum (`SeqOneShotSetCurrentFullScreenUIType`, the Enum `0x578D41E1` of §9.1) to 6 (`Load`),
  waits for autosave and for `SeqExecuteWaitCaptionMgrStable` (state 0 counts as stable), then, if a save exists,
  `SelectedSaveDataListBtnIndex = -1` and `LoadList`: load and open `SystemLoadList_00`, then each frame TrigA / TrigB
  of the `UI` controller (`SeqQueryControllerTrigMaskChecker`). A: the screen's decide, then `CaptionData.
  SelectedSavePathIndex` (-1: back to waiting) → close the list (not unloaded) → `LoadCheck`, whose "List" comes back
  into `LoadList` (opened again: refreshed). B (only while closable): cancel sound, close and unload the list →
  "Menu" → `SeqOneShotMoveBoxCursorByButton(PlusMenu_00, Pa_System_00/Pa_Button_01)` (the cursor back on Load) →
  "No" → `PlusMenu` opens `PlusMenu_00` again and sets the Enum back to 4 (`SystemMenu`). So backing out of the list
  should let the reload run (unchecked: what the Enum reads after B).
- **Refreshing an open Load list after a restore (proposal, untested):** on the game's thread (`savebackup_frame`,
  as the map screen's refresh, §5.8), when the Enum is `Load`, the `UISystemLoadListScreen` is in the UI manager's
  list (vtable `main+0x4BEEBA0`, `+0x2B0` not 0) and the `UISystemSaveLoadScreen` (`main+0x4BEF840`) isn't:
  1. hide the rows: `main+0x3773490([screen+0x2B0])`, and keep the controller from the game (sticks too) until the
     end, so scrolling doesn't show and bind a row meanwhile (it would show it with its old picture; A and B are
     harmless: A gives -1, B unloads the list);
  2. a few frames later (the GPU may still draw the last frames), `BB0010` and the request as now;
  3. once settled, the screen's slot 98 (`main+0x3400810(screen)`, i.e. `ui_refresh_now(0x4BEEBA0)`): the rows are
     bound again from the reloaded captions; then give the controller back.
  If the list closes meanwhile, nothing needs refreshing (the next opening binds the new captions). Not covered:
  touch (the list's buttons take touches).
- **The fallback (B, reload, A) would work but is clumsier:** B closes the list and puts the cursor on Load (above);
  the A has to wait for `PlusMenu` to take input again, and for the reload to finish (an A while the state is still
  0 after `BB0010` opens the list on empty captions, since 0 counts as stable). Both presses must be edges on the
  `UI` controller, and `input_block_game` (the mod's menu open) drops forced buttons.
- **To check in game:** the list screen is found by vtable only while the list is loaded, `[+0x2B0]` not 0 and
  `+0x2C0` 1 when idle; the Enum after B; `main+0x3773490` empties the rows and slot 98 alone brings them back
  (before trying it with a reload), with the cursor where it was and A loading that row; a breakpoint at
  `main+0x33CC800` is hit for each visible row after the refresh with the getter returning true.
- **1.0.0 support (19 November 2026):** the extracted base executable was compared directly with v131072.
  Its `SeqOneshotRequestLoadAndSerializeCaption` work function at `0xF40FC0` calls `0xF41000` and
  loads the CaptionMgr through GOT `0x4C1B9B0`, whose stored address is `0x4E08A40`.
  The request, entry reset, row-hide and both UI refresh functions match the corresponding complete
  instruction ranges after masking relocated addresses. The UI vtables were recovered from their
  unique refresh-function pointers at slot 98, rather than assuming a fixed version-to-version delta.

  | Role | 1.0.0 | 1.1.1 |
  | --- | --- | --- |
  | CaptionMgr pointer | `0x4E08A40` | `0x4E78E38` |
  | Empty manager | `0x963FA0` | `0xBB0010` |
  | Request reload | `0xF41000` | `0xF6DA40` |
  | Empty one entry | `0x963EA0` | `0xBAFF10` |
  | Load-list vtable | `0x4B89340` | `0x4BEEBA0` |
  | Hide list rows | `0x3710A70` | `0x3773490` |
  | Save/load confirmation vtable | `0x4B89FE0` | `0x4BEF840` |

  The reset routine has one relevant semantic difference: base clears `mgr+0x91` (Master Quest
  list selection), whereas the update preserves it. `start_reading` now saves and restores that byte
  around a full reset so refreshing captions does not switch the displayed save family. The partial
  entry reset already preserves it. The game's existing per-frame update performs the reads; no extra
  update calls were enabled. Both versions retain their separate Build-ID-selected address tables.

  Validation: `tests/run_save_caption_reload.py` extracts the actual reload controller and tests full
  and partial reloads, both native reset behaviors, normal/MQ modes, hide-before-free timing, busy and
  confirmation waits, and failure cleanup. Local execution of both native manager reset bodies with
  stubbed entry cleanup confirmed the 12 entry callbacks, shared fields and different MQ behavior.
  The local native probe skips PAC entry/exit; it does not validate real textures, save I/O or gameplay.
  On a physical console running 1.0.0, two archive restores refreshed six changed slots in 38 frames
  and one changed slot in 8 frames; the user confirmed the restored save loaded without restarting.
  These runs used the console fork with the same reload-controller changes and address mappings.
  Master Quest was covered by the host/native-reset checks, not a confirmed console run.
  No new Lichii or 1.1.1 runtime test is claimed.

### 9.12 Direct pointer paths replacing memory walks (1.0.0 and 1.1.1)

Disassembly of both `1.0.0` (`v65536`) and `1.1.1` (`v131072`) `main` binaries mapped every remaining
heap/struct/vtable walk in `sloperinaGZ` to O(1) direct paths (shared struct layouts in both builds):

1. **Link's Havok physics record (`cct.c`, `levitate.c`):**
   - `phive_mgr_ptr`: `main+0x4C18258` (`1.0.0`), `main+0x4C7DF00` (`1.1.1`): the same slot as `voxel_mgr_ptr`
     (the ground map hangs off the same manager, at `+0xC0` / `+0x170`).
   - From Link's controller body `cct_body = [[[[[[link_actor()+0x270]+0x50]+0x20]+0x10]+8]+0x20]`:
     - `world_idx = (*(u64 *)(cct_body + 0x70) >> 5) & 1`
     - `body_id = (u32)(*(u64 *)(*(uintptr_t *)(cct_body + 0x78) + 8))`, `body_idx = body_id & 0xFFFFFF`
     - `hknpWorld = [[[*(*(main+phive_mgr_ptr) + 0xC0) + 0xC8 + world_idx * 8] + 0xE0]`
     - `hknpBody = [hknpWorld + 0x48][body_idx / 42] + (body_idx % 42) * 0xC0` (`+0xB8 == cct_body`)
     - `motion_id = *(u32 *)(hknpBody + 0x80)` (`< *(u32 *)(hknpWorld + 0x798)`)
     - `hknpMotion = [hknpWorld + 0x708][motion_id >> 6] + (motion_id & 0x3F) * 0x80` (`*(u32 *)(hknpMotion + 0x60) == body_id`)
   - The physics record is `hknpMotion + 0x10` (position doubles followed by 48-byte motion state; `+0x50` is
     `hknpMotion + 0x60`/`+0x64`, which holds `body_id` above `0x5F7F3DCD`).
2. **Magic meter (`cheats.c`):**
   - Inline inside Link's player component at `[[link_actor() + 0x270] + 0x358] + 0x1B78` (`1.0.0`: `0x421030`
     called by `0x6DD260`/`0x6DDB68`; `1.1.1`: `0x1639B0` called by `0x608CD0`/`0x6095D8`). Value at `+0x44`,
     hashes at `+0x38`.
3. **Link's equipment owner and component (`link.c`):**
   - Callers of `equip_all_fn` (`1.0.0`: `0xEE0840`, `1.1.1`: `0xF201B0`) pass `owner = *(*(main + pouch_mgr_ptr))`
     (`1.0.0`: `0x4C18428`, `1.1.1`: `0x4C7E0D0`; vtable `vt_equip_owner`), and `equip_all_fn` reads
     `component = *(owner + 0x470)` (vtable `vt_equip`, `+0x18 == link_actor()`).
4. **GameData type entries (`gamedata.c`):**
   - Verified in the `GameDataMgr` constructor (`1.0.0`: `0x11BCDC0`, `1.1.1`: `0x11FD010`): `GD_BOOL` `+0x08`,
     `GD_INT` `+0xC8`, `GD_FLOAT` `+0x188`, `GD_ENUM` `+0x248`, `String64` `+0x608`.
5. **AutoSaveMgr (`savepos.c`):**
   - Verified in restart-point handoff (`1.0.0`: `0x6DC870..0x6DC8B0`, `1.1.1`: `0x6082C4..0x608304`):
     `AutoSaveMgr` is `scene->components[12]` (`[scene+0x58] + 12 * 8`), checked against `vt_autosave`
     (`1.0.0`: `0x4B3B5F8`, `1.1.1`: `0x4BA0848`).
- **1.0.0, again** (22nd November 2026): a memory dump of the running game, mapped independently (globals through
  the code that loads them, functions by masked signature, vtables slot by slot), gave the same seven addresses, and
  `caption_update` `0x4E9760` (what `0x33A95D0`, v131072's `0x340A460` matched whole, calls), so every caption
  offset is filled on both builds.

### 9.13 Quest stages: the ChallengeMgr (v131072; from the disassembly, checked in game 19th November 2026)
A quest's stage is a GameData Enum (`Step_<name>`, the **story** menu's values), but the game's **ChallengeMgr**
keeps every quest's stage itself and reads GameData back only after a save load (leo's finding in #practicerom).
Writing the Enum alone changes what reads GameData (dialogue choices, expression flags) and not what asks the
manager: NPC schedules (`AISchedule`) and the stage's own events.
- **The instance:** `*[main+0x4E29950]` (`SeqOneShotChallengeSyncToGameData`'s work, `main+0xD8D000`, loads it
  through GOT `main+0x4C7F9F0`). Its quests are in three lists of 352-byte entries, count (s32) and array at
  `+0x40`/`+0x48`, `+0x50`/`+0x58`, `+0x60`/`+0x68`. In an entry: `+0x08` the quest's key, murmur3 of its name
  without `Step_` (`Meet_DekuTree` 0x4DBCA27F); `+0x10` the stage (murmur3 of its name); `+0x16` flags (u16; 0x400
  a new stage is queued); `+0xA0` the GameData field (murmur3 of `Step_<name>`); `+0xA4` the queued stage.
- **Setting a stage:** `main+0x2CF54A0` (mgr, quest key, stage, flag, reset, force), what
  `OneShotChallengeSetStep` (`main+0x28BE1A0`) calls. It finds the entry by key, refuses (returns false) when the
  quest is at NotReady or Complete unless `force`, and otherwise queues the stage (`+0xA4`, flags 0x400; `flag`
  clears 0x8 instead of setting it; `reset` marks the stage's sub-entries); the manager's update
  (`main+0x2CF3E6C`) makes the change and writes GameData. `TriggerChallengeSetStep`'s path calls it with 0, 0, 0.
- **Re-reading GameData:** `mgr+0x72 = 1` (what `SeqOneShotChallengeSyncToGameData` does, unless `mgr+0x75` bit 0)
  makes the next update read every quest's field and queue the stages that differ, then clear the byte.

The story menu sets a stage through `main+0x2CF54A0` (with force) where the build's addresses are known, else it
writes GameData. Checked in Lichii on 1.1.1, Kokiri Forest save: the manager held the new stage two frames after
the menu set it, and the game wrote GameData itself; with **deku tree** set to complete and the map reloaded, the
game played its own "Deku Tree completed" scene (`KokiriForest_Get_StoneOfForest_Completed_Event`, which clears
`KokiriForest_Npc_Kokiri_Mido_Child_Blocker`), and after one more reload Mido no longer stood on the path to the Deku
Tree. Writing the Enum alone, as before, left the manager at Ready: no scene, and Mido still blocked.
- **1.0.0** (22nd November 2026, from a memory dump): the instance `*[main+0x4DB9998]`, through GOT `main+0x4C19D48`
  (v131072's loads of its GOT slot matched, 10 of 10 agreeing); the set-step `main+0x2CA16F0` (v131072's matched by
  masked signature, unique over 64 words; leo gives the same address, and #43 found both separately). The set-step's
  code is the same, so the manager's fields it uses are at the same offsets.

### 9.14 The safety section: GameSafetyMgr's setting and flags (v131072; from the disassembly, the two values read in game; 1.0.0 checked in game)
What a minigame, a boss fight or a story section turns off (saving, warping, ...) is one **GameSafetySetting**: a row
of RSDB `GameSafetySetting.Product.111` (28 rows), named by a Challenge (`GameSafetySetting`, or a step's
`ReplaceGameSafetySetting`). GameSafetyMgr, the scene's component 28 (§9.3), holds the one applied. The **safety**
and **safety flags** watches read it (`safety.c`).
- **The manager:** vtable `main+0x4BA1058`. Slot 8 (`main+0x3286630`) is its name getter (`adrp x0; add x0; ret`,
  giving `GameSafetyMgr`; the only function in main that returns that string), slot 6 is `main+0x32857B0`. The mod
  finds the component by this vtable (`vt_safety_mgr`), entry 28 first, as it finds AutoSaveMgr (§9.7).
- **`+0x54` (u32): the applied setting's `NameHash`**, MurmurHash3_32 (seed 0) of the row's name: the file name of
  `Work/Game/GameSafetySetting/<name>.game__GameSafetySetting.gyml`, not the whole path. All 28 names are in
  `safety.c`; the test (`tests/safety.c`) hashes each one.
- **`+0x50` (u16): that setting's switches, packed** by `main+0x3285B50`..`0x3285C40`, which looks the row up by the
  hash. Bit 0: a Challenge entered an `IsMiniGame: true` step (`main+0x2CF1160` sets it and writes the hash). Bit 3:
  a safety section is active. Then the row's fields: bit 5 `IsCheckSafetyArea`, 6 `IsDisableSave`, 7 `IsDisableWarp`,
  8 `IsDisableAutoPlacement`, 9 `IsDisableOpenTreasureBox`, 10 `IsDisableGrab`, 11 `IsDisableBoot`,
  12 `IsDisableTimeProgress`, 13 `DoSaveWhenEnd`, 14 `IsDisableLoadingPause`, 15 `IsNoDispErrorForTemporarySave`.
- **A field left out of a row has its default, and several defaults are true.** `IsDisableSave` is (read at
  `main+0x5A2288`), which is why only `Common_DisableWarp` writes `IsDisableSave: false`.
- **In game (1.1.1):** with a `MiniGame_TimeStop` challenge running, `+0x54` read `0xFAE899F0` (that name's hash)
  and `+0x50` read `0x14C8`: bits 3, 6, 7, 10 and 12. The row sets `IsDisableTimeProgress: true` and
  `DoSaveWhenEnd`, `IsCheckSafetyArea` and `IsDisableBoot` to false and leaves the rest out, so `IsDisableWarp` and
  `IsDisableGrab` default to true as well. The manager's first word was `main+0x4BA1058`.
- **Not known:** bits 1, 2 and 4; why bit 0 was clear in that read (the step may not have been an `IsMiniGame` one);
  what the two values hold when no section is active.
- **1.0.0** (22nd November 2026, from a memory dump): the vtable is `main+0x4B3BE08` (v131072's matched: its entries
  are v131072's 0x5F080 lower, and slot 8 returns `GameSafetyMgr`). The packing (`0x3226AD0`) and the minigame bit's
  setter (`0x2C9D3B0`) match v131072's whole but for relocated addresses and another object's fields, so `+0x50` and
  `+0x54` are the same. In Lichii (22nd November 2026, read from memory) the title scene's 41 components had
  AutoSaveMgr at entry 12 and GameSafetyMgr, with this vtable, at entry 28, both values 0.
- **Checked in game on 1.0.0** (22nd November 2026): the mod found GameSafetyMgr at scene entry 28 by this vtable,
  and the watches followed the slingshot minigame. Outside any section, after it, the manager held flags `0x0040`
  (save) and name hash 0.

### 9.15 The ocarina breath T-pose (v131072 and 1.0.0, checked in game)
- **Symptom:** after holding one ocarina note for 3-5 s, Link's arms T-pose whenever he runs holding a bomb. Not
  standing still. Idle fidgets, attacks, a boots change or reloading clear it.
- **Link's AS** (`romfs/AS/Player_Child.root.asb`, `Player_Adult`; version 0x41B, string pool at the header's word
  14) blends 9 layers (slots, header words 8/9, 0x10 bytes each: name, default bone group): Lower, Upper, RightArm,
  LeftArm, Face, Finger, Mouth, MaskChange, MaskEquip. 16 bone groups (header words 21/22, 0xC bytes: entries'
  offset, name, count; each entry a bone name and u16 0 in / 1 out, children included). Commands are 0x30 bytes
  from 0x80 (name first, node index last); nodes 0x24 bytes from 0x58D0 (u16 type, body offset at +8); type 3 plays
  an animation, type 27 is the node the breath's clip sits under (its label: "ocarina performance (includes Face and
  Spine) [for breathing]").
- **Cause:** a held note plays the command `DemoOcarinaBreath` in the Face layer (the player component P keeps the
  layer's index at P`+0xFEC` = 4; `main+0xA2CD98` compares the playing command's name with it). That switches
  the Face layer's bone mask from `Face_NoMouth` (Root out, Face_Root in, Mouth_Root out) to `OcarinaBreath` (Root
  out, Spine_2 in, Weapon_L/R and the ten finger roots out): the whole upper body. It happens on every breath, not
  only when the ocarina is cancelled during one. Nothing on the way out of the ocarina sets it back. Only Face
  animations that name their own group do: idle fidgets (`VoiceServic*`), `CutS1`, `BootsChange`, events.
  Until then the Face layer drives Link's chest and arms, and while he runs with a bomb, his arms are in bind pose.
- **Where the mask is:** slots S = `[[[[actor+0x270]+0x40]+0x18]+0x18]` (count S`+0x18`, entries S`+0x20` of 0x48
  bytes; the first qword is the slot's object O, `[O+0x10]` the playing command's name). Each O has a mask
  table T = `[O+0x17198]` (= O`+0x17238`), which holds the mask of the *next* layer (entry 3 holds Face's). I =
  `[T+8]` (vtable `main+0x4C12FD8`; T's is `main+0x4B48D68`, a generic wrapper): I`+8` capacity, `[I+0x10]` one
  byte per entry (0x80 in, 0x83 out), I`+0x1C` a count, `[I+0x28]` u32 entries with the skeleton bone index in
  the top 16 bits. Bone indices are the bfres skeleton's order (`romfs/Model/Player_Adult.Player_Adult.bfres`,
  FSKL: bone array at +0x10, 0x58 bytes a bone, name first): adult Root 0, Spine_2 0x06, Weapon_L 0x12, Weapon_R
  0x42, Face_Root 0x6B, Mouth_Root 0x89; child Spine_2 0x0B, Weapon_L 0x17, Face_Root 0x6B, Mouth_Root 0x8D.
  The same slot walk is in 1.0.0 (`main+0x5754D4`, same offsets).
- **Fix / feature:** `tpose.c` (link > bomb t-pose) writes entry 1 of the Face table as Spine_2 every frame (the
  game rebuilds the table on its next mask change; a single write lasted about a second). Unticking writes
  Face_Root back. It finds the table by its contents (entry 0 Root out, entry 1 Face_Root or Spine_2 in), so the
  slot it sits in doesn't matter. Face_Root is 0x6B in both skeletons, so the age comes from `age_current`.
  `Face_NoMouth` uses only 2 entries (I`+0x1C` = 2): the Mouth_Root seen in entry 2 is left over past the count,
  and the game doesn't rewrite it. The first build matched on entries 1 and 2 together. After the game put entry 1
  back and left entry 2 at Weapon_L, nothing matched, so it stopped writing ("tpose: no Face mask table found").
- **1.0.0** (22nd November 2026, read from memory in Lichii, adult Link): 9 slots, every slot object's `+0x17198`
  pointing at its own `+0x17238` (MaskEquip's elsewhere), and the Face table in slot 3 (before FaceDefault's slot 4)
  with entry 1 Face_Root (0x6B), 2 entries in use: the same as v131072, so the offset is fixed for both builds and
  the mod no longer searches the slot objects for it.
- **Not checked:** child Link (indices from the child bfres, assumed to match as they do for adult), and which Face
  animation leaves the arms in bind pose while running. The T-pose itself was checked on 1.0.0 (adult) after the
  offset was fixed.

### 9.16 Hookshot storage (1.0.0; read live in Lichii)
- **The record:** Link's AI component `A = [[actor+0x270]+0x3D8]` keeps a small hash map: buckets at `[A+0x20]`
  (0x38 bytes each: `+0x00` key, `+0x08` count, `+0x10` pointer to the records, 0xF8 bytes each), bucket count at
  `A+0x2C`. The longshot's record has key `0xAD28A0F2`, the hookshot's `0x9472DF51` (`main+0x2DAC3C0`, the code
  behind `QueryEquipmentUserTryGetHookshotHookState`, reads the current weapon's). In it:
  - `+0xA0` vec3: where the shot started (Link's hand); `+0xAC` vec3: the spot it aimed at, worked out again on
    every shot.
  - `+0xD8` u8: 1 while a hookshot action runs. `+0xDE` u8: the hook state: 0 idle, 2 flying out, 3 hooked and
    pulling, 4 coming back after a miss. `+0xE0` u8: ready; cleared as a shot starts, set again when it ends.
- **Storage:** a hit on the shot's first frame (a bomb's `DamageM`) ends the action before the hook flies (`+0xDE`
  never leaves 0) and before `+0xE0` is set again. The next shot goes 0 → 3 without flying and pulls Link towards
  the left-over `+0xAC`. `+0xE0` clear alone isn't storage: a normal shot that ends on a climbable net leaves it
  clear too, so the mod tells storage by a shot whose hook state never left 0.
- **Forcing it** (`hsstore.c`): on the frame `+0xD8` goes 0 → 1 with the state still 0, the mod writes the words a
  real bomb's `DamageM` left in Link's hit component `[actor+0x270]+0x2B8` (recorded live; frame numbers made
  current, the bomb put in front of Link). The level byte `+0x2D1` alone (2 = medium) isn't enough: the reaction
  also reads the hit's contact records (lists at `+0x210` and `+0x1338`).
- **The hook** is an actor named `Shootable_Hook` in the actor tree (9.5): state at `+0x2C` (4 or 5 out, 6 put
  away), position at `+0x31C`.
- **What a stored shot does depends on the wall it meets:**
  - **Pull** (state 0 → 3): the hook glides out to the aimed spot and Link is pulled there.
  - **Dink:** the game runs it as a normal shot (0 → 2) and the hook bounces off within a frame or two (the mod
    logs it as "fake"). What decides pull or dink isn't known yet.
  - **Clip through a climbable wall or ladder:** the hook can stop about 0.2 behind the wall's face. The pull
    ends in front of it, but the climb then snaps Link to the hook's depth, behind the face; he lets go and falls
    behind the wall. Hookshot targets have no climb, so they don't clip.
- **1.1.1:** while pulling, it checks every frame that a real object is hooked and ends the shot cleanly if not.
  So a stored shot should just let go there (from the code, not tried in game). The mod's forcing is 1.0.0 only
  (`hs_hit_vt`, listed in 1.1.1's `absent`; the menu leaves the option out there); telling storage and drawing the hook only read, on both.
- **The hit's vtable** (the pointer the recorded `DamageM` left at `H+0x108`, `+0x148`, `+0x338`): 1.0.0
  `main+0x4910168`; 1.1.1 `main+0x4974A90` (its slot 3 names the same parameters, `TargetActor`, `MinRadius`,
  `Fovy`, `IsReferenceTargetPosOnce`, ..., and refers to the table 0x90 after it, as 1.0.0's does). The rest of the
  hit component's layout on 1.1.1 hasn't been checked, so it isn't used there.
- **Levitating with through walls off** also gives storage, without a hit: levitate holds Link still with
  collision on, the physics reports him standing on something (`[[actor+0x270]+0x358]+0x13D0` bit 0) but not
  grounded (bit 2), so items work in the air and a hookshot shot is cancelled on its first frame.

### 9.17 GameData serialization, UInt/Vector3 layouts, WorldMgr, NaviMgr, CCT rotation and Sequence flags (1.0.0; 1.1.1 in brackets)
- **`GameDataMgr` binary save/load:** `gd_request_save` is at `main+0x36FED60` [`main+0x37617B0`] and `gd_request_load`
  is at `main+0x36FEDD0` [`main+0x3761820`] (`bool (mgr, buf, int file_idx)`). Both operate on descriptor `0` in the
  array at `[mgr+0xCC8]` (file count `u32` at `mgr+0xCC0`, descriptor stride `0x80`, active saves `s32` at
  `mgr+0xD0C`, copy-sync busy `u8` at `mgr+0xD10`). Inside `desc0`: `+0x50` buffer size (`u32`), `+0x58` written bytes
  (`u32`), `+0x60` save buffer pointer, `+0x68` load buffer pointer, `+0x78` flags (`0x01` save requested, `0x02` load
  requested, `0x04` save worker active), `+0x7A` `load_ok` (`u8`). Serialized save buffers start with magic
  `0x01020304`. `GameDataMgr::update` drains requested save/load work and deserializes all 33 typed containers,
  change queues, and dirty/reset bits.
- **`UInt` and `Vector3` GameData containers:** Derived from `SetUInt` (`1.0.0: main+0xE5AC30`), `SetVector3` (`1.0.0:
  main+0xE6D9B0`), and the queue flush (`1.0.0: main+0xB9A9CC`; same offsets in `1.1.1`). Both are 0x60-byte type
  entries in `GameDataMgr`:
  - `UInt`: `mgr+0x788`, object stride `0x28`, value `+0x1C`, copy `+0x20` (holds e.g. `0x3F8D9D86`).
  - `Vector3`: `mgr+0x3C8`, object stride `0x40`, value `+0x24`, copy `+0x30` (holds e.g. `0x8773F02D`).
- **`WorldMgr` (time of day is not pulled automatically from GameData on map load):** `wm = [[main+0x4C18BF0]]`
  [`[[main+0x4C7E898]]`]. `[wm+0x578]` is the clock object, whose vtable slot `+0x30` returns the seconds into the day.
  `setTime(wm, seconds)` is at `main+0x33F1580` [`main+0x34522D0`] (called by `ExecuteWmSetTime`); `onSave(wm)` is at
  `main+0x33F1410` [`main+0x3452160`] (flushes live clock/weather into GameData before save serialization); `onLoad(wm)`
  is at `main+0x33F1300` [`main+0x3452050`].
- **`NaviMgr` reload hook:** `scene = [[main+0x4C184D0]]` [`[[main+0x4C7E178]]`]; `navi = navi_get(scene)` at
  `main+0x945E50` [`main+0xCAF800`]; `navi_on_load(navi)` at `main+0x3230F70` [`main+0x3290060`].
- **Character controller rotation setter:** In `vt_cct_property`, slot `+0x110` (`OneShotCctChangeRotation`, `1.0.0:
  main+0x65EE00`, `1.1.1: main+0x6B8910`) takes `(property, const float mat33[9])` in the same row-major 3x3 layout as
  actor `+0x328` (so yaw `(fx, fz)` is `{ fz, 0, fx, 0, 1, 0, -fx, 0, fz }`), paired with the position setter at slot
  `+0xE8`.
- **Sequence AINB control and debug flags (`Sequence/*.ainb`):**
  - `DebugParam.IsRequestedResetGimmick` (`0x3FC35576`, `Bool`, `save_file == -1`) and `Sequence_ResetGimmickOption`
    (`0x37B1DAC9`, `Int`): `Main.module` N15 checks `0x3FC35576` and runs `ResetGimmickImpl.module` in place without a
    loading screen, deleting resettable actors (`option == 0` unconditionally; `option == 2` applies a 30 m distance
    filter), resetting `PreActor` flags, re-traversing around `SeqGetPlayerPos`, and clearing `0x3FC35576` back to
    `false` (`Main.module` N14). Note: `Main.module` N15's `BancChange` branch (`[x19+0x29]`) is dead code in retail
    `1.0.0: main+0x32A4980`.
  - `Sequence_IsAlreadyResetForLoadSave` (`0x8CD71F3A`, `Bool`): skips ResetType 0 and ResetType 8 wipes in
    `ChangeBanc.module` N7–N9 and auto-clears to `false`.
  - `DebugParam.IsSetChildhoodManually` (`0x86724737`, `Bool`): makes `LoadBancImpl.module` N29–N35 honor the existing
    `Sequence_IsChildhood` value and auto-clears to `false`.
    The scene setup (1.1.1 `main+0x9B76A0`) reads it again later in the load: with it off, it writes the scene's
    `GameBancParam.IsChildWhenInitializingScene` to `Sequence_IsChildhood`. A map that forces an age
    (`IsForceSwitchChildOrAdult`, e.g. `GerudoFortress_Adult`) therefore keeps Link's age only if the flag is set
    again after `LoadBancImpl` clears it; warps > override age writes it every frame until the map has loaded
    (checked in game on 1.1.1: child Link in Gerudo Fortress).
  - `DebugParam.IsOverrideTraversePos` (`0x7325CE62`, `Bool`) + `DebugParam.OverrideTraversePos` (`0x527FA715`,
    `Vector3`): overrides chunk/actor traverse coordinates in `OnLoadBancParam.module` N12–N16 (not auto-cleared by the
    engine).
  - Clearing `Sequence_ReservedBootEventName` and `Sequence_ReservedEventStarterName` to `""` causes
    `AfterChangeBanc.module` N16–N23 to skip the arrival event and ~190-frame arrival camera.

### 9.18 The player camera and actor AI node layout (1.0.0; 1.1.1 in brackets)
- **Where the player camera lives:**
  - The game's camera save (`1.0.0: main+0x288D980` → `0x2B39920`) reads an actor handle at `[[main+0x4C18B70]]+0x38`
    [`[[main+0x4C7E818]]+0x38`] (a GOT slot: read twice). Handle resolution (`1.0.0: main+0xCF90C0`): entry =
    `[handle+8]`, valid while `[entry+0x48] == [handle+0x10]`, actor = `[entry+0x40]`.
  - The player camera component is `[[actor+0x270]+0x360]` (`1.0.0` vtable `main+0x4AE94F8`, named `プレイヤーカメラ`),
    with its state at component `+0x5B8`. Link's own `[[link_actor+0x270]+0x360]` is not the active player camera.
- **State (`component+0x5B8`) and component layout (checked in game on 1.0.0):**
  - The camera view is `0x28` bytes (eye `+0x0`, look-at `+0xC`, FOV/angle `+0x24`), stored five times at `+0x450`,
    `+0x478`, `+0x4A0`, `+0x4C8`, and `+0x4F0` (`+0x450` is drawn; `+0x478..+0x4C8` ease toward target `+0x4F0`).
  - The camera's own save point (`1.0.0: main+0x2E2D4D0` → `0x2B1ADF0`) copies `+0x450` (`0x28` bytes) and orbit
    ranges (`0x868` `0x30`, `0x898` `8`, `0x8A0` `4`, `0x8A4` `8`, `0x8AC` `4`, `0x8BC` `8`, `0x8C4` `4`, `0x90C` `4`,
    `0x910` `4`) to `+0x914`; `0x2B1AE90` restores the orbit ranges and `0x2B1AEE0` hands the saved view to return-to
    actions (`ExecutePlayerCameraEventReturnSavePoint`).
  - Snap: `main+0x2E2D5D0(component)` [`main+0x2E821D0`] (→ `0x2B1AFC0` [`0x2B6E280`]) copies `+0x4F0` across the other
    four view slots when state `+0x7E4` is set, resets component params, zeroes `+0x810..+0x830`, and sets `+0x804` to
    `(1, 1)`. During arrival, state `+0x7F0..+0x850` holds the turn target (`+0x7F8 = 180`, `+0x81C` direction vector;
    zero otherwise).
  - The component itself holds duplicate float views/directions at `+0x130..+0x1B4` (`+0x148`, `+0x170`, `+0x194`,
    distances `+0x1A0..+0x1A8`) and `+0x208..+0x2EC` (`+0x208..+0x2F0`).
  - Hand-off timing: after a full area load with an arrival event, the arrival camera renders first and the player
    camera takes over ~190 frames later, observable when the drawn eye (`[[main+0x4E27C08]+0x68]+0x38`) matches state
    `+0x450`.
  - (`[[actor+0x270]+0x368]`, per-frame `1.0.0: main+0x2E2D9B0` from `0x2BCC9E0`, is a separate target tracker with
    look-at `+0x128` and rotation `+0x20..`.)
- **Camera AI action nodes (`ExecutePlayerCameraChase`, etc.):**
  - Camera logic runs as AI action node objects: `ExecutePlayerCameraChase` has vtable `main+0x4959790` [`main+0x49BE0B8`]
    (factory `1.0.0: 0xE0EE60`, enter slot 29 `0x2750A70` [`0x27A2510`] sets `[node+0x20] = camera_c + 0x5B8`), and
    `ExecutePlayerCameraKeep` has vtable `main+0x495C0C0` (`1.0.0`).
  - Polar maths (`1.0.0: 0x642690` [`1.1.1: 0x718800`], inverse `0x2B35E90`) computes `{distance, elevation, azimuth}` in
    degrees, eased via `0x27738F0`. In `ExecutePlayerCameraChase`, float fields at `+0x14C..+0x170` (smoothed offset and
    target), `+0x188..+0x1A8` (including elevation `+0x18C` and azimuth `+0x1A4`), and `+0x1C4` (distance) drive the view
    each frame.
- **Direct actor AI node traversal (verified in `1.0.0` & `1.1.1`):**
  - `actor+0x270 -> +0x48` (`AIComponent`; verified in `1.0.0: 0x9AC64`, `0x7530A0` and `1.1.1: 0xC76D4`; note `+0x50` is
    `PhysicsComponent`) `-> +0x28` (`AIControllerSet`, count `u32` at `+0x00`, entries `void**` at `+0x08`).
  - Each `entry+0x18` is an `AIControllerBase` (`+0x1F0 == actor`; `unit_cnt` `u32` at `+0x18`, `unit_slots` at `+0x20`,
    stride `0x18`, `unit = [slot+0x00]`).
  - Each `AIControllerUnit` (`1.0.0: 0x9CDCC`, `1.1.1: 0xC586C`, back-pointer `[unit+0x08] == ctrl_base`) holds `node_cnt`
    `u32` at `+0x28` and `node_slots` pointer at `+0x30` (stride `0x18`, `node = [slot+0x00]`, last-visited generation
    `u32` at `+0x0C` compared against `[unit+0xD0]`). Every attached camera action node has `[node+0x20] == state` and
    its node index (`u16`) at `node+0x1C`.

### 9.19 The game's sequence scripts and the ResetGimmick item wheel bug (both builds; checked in game on 1.0.0 20th November 2026)
- **Where they are:** the game flow runs as AINB graphs in `romfs/Sequence/*.ainb` (version `0x408`), read here with the
  `ainb` package (PyPI) after `tools/gamefiles.py read`. `Main.module.ainb` (11395 bytes) is the same in 1.0.0 and 1.1.1.
- **Main.module:** its root (node 23) is an `Element_Simultaneous` running `UpdateShortCutOpenHoldFrameCounter.module`
  (node 22: what opens the item wheel on L/R) beside a branch picker (node 15) with children `GameOver`, `ResetGimmick`,
  `MinusMenu`, `PlusMenu`, `Event`, `AssignableItemShortCut`, `BancChange` and `None`.
  - `ResetGimmick` is taken while `DebugParam.IsRequestedResetGimmick` is set: a sequence (node 12) runs
    `ResetGimmickImpl.module` (requests a pause, FastLoadOn/Off, suspends and resumes the 3D layer, deletes and recreates
    the reset actors, releases the pause) then clears the flag.
  - **Bug:** node 12's transition goes to node 15 alone, not the root, so afterwards the hold counter never runs and
    the item wheel can't be opened (`Sequence_ShortCutOpenHoldCounter` stays 0) until something leaves Main: a map change
    (`ChangeBancOnMain` transitions to node 23), or a menu or event (they end Main, which restarts from its root).
  - Fix (memfile.c): the game keeps the file loaded. Before each fast load it's found through the Sequence component
    (below) by its first `0x40` bytes, and the transition plug's `s32` node index at file `+0x1204` (`15`, followed by
    transition entry `7`) is set to `23`. The runtime reads it from there: patching it fixes the wheel.
- **Where the file is** (static, the same offsets in both builds; 1.0.0 addresses, 1.1.1 in brackets):
  - Sequence `C = [[S+0x58]+0]` (§9.3; vtable [`main+0x4C72AD8`], update slot 6 `main+0x3B60410` [`0x3BBE020`]).
  - `X = [C+0xE8]`: its AI controllers' holders, `u32` count at `+0`, array at `+8` (made at `main+0x3B60224`
    [`0x3BBDE34`], filled by the loader `main+0x3A59460` [`0x3AB8E90`]).
  - A holder's controller is `[holder+0x18]` (the loader passes `holder+0x18` to the controller setup `0x382D0`
    [1.1.1], and the setup's caller then stores the component at `[[holder]+0x18]+0x1F0`).
  - The controller lists the AINB definitions it loaded, the root's and its modules': `u32` count at `+0x168`, array at
    `+0x170` (stored at `main+0x7B5E0` [`0x38480`]; the loader's cap is `0x15E`).
  - A definition (vtable [`main+0x4C26A98`], `0x68` bytes, `[resource+0x28]`) holds the file's bytes at `+0x10`
    (init `main+0x82C374` [`0x840D60`]: `str x8,[x0,#0x10]; ldr w3,[x8,#4]; cmp w3,#0x408`).
  - No name is kept with a definition ("Main.module" isn't in main): it's told apart by its header.
- **UpdateShortCutOpenHoldFrameCounter:** on L/R pressed with the shortcut openable (`SeqQueryIsOpenableShortCut`)
  it sets `UI_IsNeedOpenShortCut`, zeroes the counter and sets `UI_CurrentShortCutCategory` = AssignableItem; while L/R
  are held it adds the frame step rate to the counter, and a counter in `[1, 9999]` sets `UI_IsNeedOpenShortCut` too.
- The UI side (`1.0.0: main+0x32A4980`, update slot of a 0x30-byte controller with vtable `main+0x4B6AD10`, factory
  `main+0xE0BDC0` registered under hash `0x4AA865D7`) also reads `IsRequestedResetGimmick` and moves to its state at
  `+0x28` while it's set (current state `+0x1E`, pending `+0x2A`), but its state was the same before and after a reset:
  it isn't what broke the wheel.
- `Sequence/ResetGimmick.module` (used from `Event.module` when `Sequence_ControlInEvent` = ResetGimmick) runs the same
  `ResetGimmickImpl` inside an event, then sets the control back to Idle.

### 9.20 Nayru's Love (1.0.0; 1.1.1 in brackets; checked in game on 1.0.0)
- The player component `[[actor+0x270]+0x358]` (getter `main+0x943FE0` [`0x944E50`]) holds the time left (float,
  frames) at `+0x1634`, its length at `+0x1638`, and `-1.0` at `+0x163C` whenever it's set or reset.
- Active = time left `> 1.19e-7` (`main+0x2E23590` [`0x2E77F70`]; feeds the blackboard `IsUsingNayruLove`). A frame
  update (`main+0xBF0138`) sets the barrier's sensor `[component+0x1138]` on or off from that, through
  `[[sm]+0x88](sm, [component+0x1138], on)` with `sm = [[[[actor+0x270]+0x40]+0x18]+0x18]+0x40`; so writing the time
  is enough to bring the barrier back or end it.
- Start: `main+0x2E235B0` [`0x2E77F90`] (from `ExecutePlayerUseNayruLove`, vtable `main+0x49673A0`) sets the time from
  the NayruLoveTime parameter × 60; the cast also costs 120 MP once. Reset: `main+0x2E23670` [`0x2E78050`] (from the
  AINB node `ResetNayruLove`, `main+0x28B8C40`) zeroes it and resets an anim value.
- The time is mirrored to a GameData float in `PlayerStatus` (member hash `0xD59B0D24`), read only when the component
  is built (a map load) and written from the component every frame, so a GameData restore alone doesn't bring it back.

### 9.21 Farore's Wind: WarpMgr and the sparkle (1.0.0; 1.1.1 in brackets; checked in game on 1.0.0)
- The point is GameData struct `WarpPoint_FaroreWind` (`0x66CED69E`, 9 members, saved): flat keys `BancName`
  `0xB09FBD99` (String64), `LocationName` `0xEA129426`, `Pos` `0xBEB37B31` (Vector3, Link's feet when cast). Setting it
  (AINB node, `main+0x28E2D70`) writes all members at once; casting and arrival read them live.
- The sparkle is actor `FaroreWindWarpPoint` (no AI: an effect and links), placed by **WarpMgr**, a game component
  made once (factory `main+0xE0FE10`, constructor `main+0xE0FE60`, registered as `WarpMgr` at `main+0x4DFC950`). It's
  the scene's component 22 (§9.3): the game's getter `main+0x27C2AD0` [`0x28147E0`] reads `[[[M+0x1F0]+0x58]+0xB0]`
  after checking the count at `+0x50` is at least 23, with `M = [main+0x4E271E8]` [`0x4E97648`] (the scene manager;
  the GOT slot at `navi_scene_ptr` holds that address in both builds). Its caller `main+0x2886F30` [`0x28D94D0`]
  then calls `place`. Vtable `main+0x4B3CFF8` [`0x4BA2248`]; it's `0xD0` bytes, 8-aligned, with a listener vtable
  at `+0xB8`.
  - `WarpMgr::place(mgr, x, y, z)` `main+0x3251430` [`0x32B0750`] (from `OneShotFaroreWindSetActorPos`,
    `main+0x2886F98`): writes the sparkle's transform at `+0x4C..+0x7B` (y + 2, or + 1.5 as a child) and sets `+0xB0`
    bit `4`; the actor moves on the next update.
  - Slot 6 `main+0x465210` [`0x57F4F0`] (mgr, change bits) is its GameData change listener: four `u32` of change bits
    and a hash map at `+0x10`, all zero meaning everything. It compares `BancName` with the current map and rewrites the
    transform at `+0x7C`, or hides it, but doesn't move an existing sparkle.
- A GameData restore doesn't notify listeners, and ResetGimmick keeps a sparkle cast in the current visit (one placed by
  the map load is rebuilt from GameData), so after a fast load it stays where it was last cast. memfile.c finds WarpMgr
  through that chain (checking its vtable) and calls `place` with the restored point.

### 9.22 Link's actions: the BSA component and climbing (1.0.0; 1.1.1 in brackets; climbing not solved)
- The player AI asks for actions by **verb** through the BSA component `[[actor+0x270]+0x188]` (same offset in both
  builds); the current verb is the `u32` at `+0x54`: Wait 40, Walk 1, Fall 33, Swim 84, Climb (vines, walls) 70,
  ClimbLadder 71 (checked in game; after letting go it reads 32 then 0).
- `ExecuteBSASetActionVerb` (vtable `main+0x491A0F8`, enter `main+0x25DC2C0` [`0x262C160`]) builds a `0x60`-byte request
  and calls `main+0x93440(bsa, request, 1)` [`0xC08F0`]: NaN at `+0x0/+0xC/+0x18/+0x2C/+0x30`, `u32` `4` at `+0x24` and
  `0x11A` at `+0x28`, `0x10` at `+0x34`, optional variation at `+0x38`/`+0x40`, action slot `+0x48` (`0..4`: `0` main
  `[bsa+0x1C0]`, `1` weapon/sub `[bsa+0x1C8]`), verb `+0x4C`, flags
  `u16` `+0x50` (bit 0 and `0x100`: the two force booleans; `0x400` skips the controller at `[bsa+0x3C68]`), requester
  node `+0x58` (the game itself sends some with none, `main+0x973EC`). Generic builder: `main+0x2AFB200`.
- Schedulers (`Player.scheduler.*.module.ainb` in `Player_Base.pack`) choose the next verb from the current action;
  their selectors are native (`SelectorPlayerSchedulerWalk`, vtable `main+0x4AA5620`).
- **Restoring a climb failed:** forced ClimbLadder/Climb requests right after a teleport (flags `0x100`, then `0x501`)
  were never taken; Link stays in Fall. Starting a climb seems to need the grab found by the climb check
  (`FilterClimbingPlayerClimbCheck`, `IsEnableGrabClimbPlayer`, CCT `CharacterExternalPlayerClimb(Ladder)`);
  `ExecutePlayerClimbLadder`'s enter (`main+0x277D4E0`) sets player component flags (`+0xE50`, `+0xE38`) and clears
  `+0x150C`/`+0x1514`. Events can start Link climbing (event setting `IsPlayerEventStartWithClimb`, parsed at
  `main+0x60E8F4` into byte `+0xBB`): a lead.

### 9.23 Finding objects with no static pointer: a heap walk over frames (replaced)
- Heap regions are `svcQueryMemory` type 5, read-write (about 9.4 GB under the emulator). Reading all of them in one
  frame took ~3.3 s; 8 MB a frame from when Link first exists finished in ~17–26 s but was felt as lower performance
  after boot, and 1 ms a frame took about a minute. memfile.c used such a walk to find the `Main.module` file
  (16-aligned) and WarpMgr (8-aligned; a pass that stepped 16 bytes missed it).
- Both are now reached through the scene's components instead (§9.19, §9.21): no walk, nothing to wait for after boot.
  levitate.c's search for Link's physics record (§5.19) is still a walk.
### 9.24 ChallengeMgr on save loads and memfiles (both builds; 1.0.0 addresses, 1.1.1 in brackets)
- 1.0.0: the instance `*[main+0x4DB9998]` (GOT `main+0x4C19D48`), set-step `main+0x2CA16F0` (1.1.1's matched over 40
  instructions), update `main+0x2CA00BC`; every offset below is the same in both builds.
- The update (vtable slot `+0x10`, `main+0x2CA00B0` [`0x2CF3E60`]) does nothing until `+0x70` is 1 (set once by the
  init). With `+0x72` = 1 it compares every quest's GameData field with its stage and applies any that differ, in
  either direction, without set-step's NotReady/Complete refusal (and without flagging a regression at `+0x71`);
  a field that isn't found is left alone. It then clears `+0x72` and `+0x73` together (`strh wzr`).
- `+0x73` (SetChallengeMgrSaveLoading, work [`0x32E0100`]: set unconditionally) is read before each stage change:
  set, the change is made quietly. `+0x75` bit 0 skips the resync (SyncToGameData's helper `main+0xD468F0`
  [`0xD8D040`]: `if (!(+0x75 & 1)) +0x72 = 1`); nothing in main sets it.
- **A save load** (`OnLoadGameSaveData.module`, run by the title's continue, the pause menu's Load and the game
  over retry): the GameData resets, `OnResetGameData` (SetChallengeMgrSaveLoading), the deserializer, then
  `AfterLoadSaveData`: SetSavedSelectedChallenge (work `main+0x3289D20` [`0x32E8D50`]: reads UInt `0x2029D3D8` and
  selects that quest, `main+0x2CA1A00` [`0x2CF57B0`], or none, `main+0x2CA19C0` [`0x2CF5770`]), WorldMgr's and NaviMgr's
  on-load, ..., then SyncToGameData; the map load follows.
- A memfile restores GameData through the deserializer alone, so the manager kept its stages: loading one taken
  after Ganondorf into the castle collapse on a fresh boot left Zelda standing. memfile.c (quests_on_load) now
  does what the save load does after WorldMgr's and NaviMgr's on-load: the selected quest, `+0x73`, then `+0x72`.

### 9.25 Void-outs ("Abyss"): what triggers one (v131072; from the disassembly and game files; the grid's 8 m rule checked in game)
The game calls a void-out an *abyss*. The player gets reaction `PreAbyss` (reaction type 1 of `None, PreAbyss,
PreEvent, Guard, ...`), whose AI (`Player.scheduler.reactionpreabyss` / `Player.action.reactionpreabyss` in the
Player packs) runs `ExecutePlayerReactionPreAbyss` and the event `DmF_SY_FallDown` (fade, `EventPlayerAbyss`,
`EventPlayerPostAbyss`, Navi's hint, `EventTriggerPlayerSetInvincibleAfterReturnAbyss`). The event branches on the
**abyss type** P`+0x191C` (P = Link's player component, §9.7) with `EventQueryCheckPlayerAbyssType`.
- **The decision** is `main+0x82C510(P)` (called from `main+0x82BBB0` and `main+0x27D4DD0`). It writes the type to
  P`+0x191C` and to a global (`[[main+0x4C7FC80]]+0x2B30`, flag `+0x2B34` = 1). It's the only store of a non-zero
  type; the other stores (`main+0xE5837C`, `0x11A73E0`, `0x26B0B0C`, `0x27935D4`) clear it. It fires when any of:
  - **contact** (type 3): the decision takes the strongest abyss value among the sensor bodies Link touches (the
    list at `L+0x40` with L = `[[[[[actor+0x270]+0x1B0]+0x18]+0x270]+0x198]`; each body: flags `+0x268` bit 2,
    `+0x44 & 0x14` clear, byte `+0x35` bit 7 clear, value `+0x68` when `+0x6C` = 1, else 0) and `main+0x82C710`
    compares it with a threshold param `+0x110`: contact when the value is at least the threshold, or, with a
    threshold ≤ 0, when it's above 0.
    These are the placed `Gimmick_AreaContact_FallDown*` / `*_FallDownMountian` / `*_FallDown_GanonCastle` logic
    areas and similar prefabs (Water Temple whirlpool, Shadow/Spirit/Ganon's Castle rooms, StalfosStage), whose
    graphs use `HoldSensorHitInfoGiveAbyssActor[Condition]`;
  - **ground flags** P`+0x13D0` **bit 25 or bit 19** (`ldrh [P+0x13D2]; tst #0x208`), both from the VolumeStats
    grid below;
  - P`+0x13E8` bit 4, with exceptions by P`+0x13D0` bit 7:
    - bit 7 clear: unless P`+0x13FB` bit 2;
    - bit 7 set and P`+0x7E8` = 2: never;
    - bit 7 set otherwise: always, and with P`+0x13D0` bit 8 and P`+0x1C30` bit 6 also when the height P`+0x16A0` is
      above the float `*main+0xE13F70()` (or NaN) (not looked at further);
  - P`+0x28` bit 2.

  Without contact the type is 7 (P`+0x13D0` bit 7), 4/5/8/9 (P`+0x28` bits 3/4/5/6), else **1**.
  It returns nothing; both callers read P`+0x191C` right after it and start the void-out when it isn't 0
  (`main+0x27D4DD0` picks the reaction by type). 1.0.0 (v65536): `main+0x818640`, the same code but for branch
  targets and its global (`+0x1DA8` / `+0x1DAC`); its callers are `main+0x817CE0` and `main+0x2783230`.
- **The mod's link > no void outs** patches its first instruction (`pacibsp`) to a cave stub that returns at
  once while bit 4 of the lock slot is set, so no type is ever stored (`mkips.c`, `lock.c`).
- **The grid is VolumeStats** (PhiveConfig `VolumeStats*`): a baked sparse voxel octree per map in
  `romfs/Phive/VolumeStats/<Map>/<Map>[_Xn_Zn][_presence_<state>].bpvsts.zs` (1065 files; field maps are tiled like
  the static compounds; structures `Default1m`, `Dungeon1m`, `Field1m`, `Field4m`, ...). Attributes: `Intersect,
  Material, WaterDepth_, WaterDist, WaterArea_, InWater, IsWaterSurface, WaterMat_, WaterFlow_, DungeonRoomId,
  InDoor, OnGroundRoomArea, InHidingHole, HoleIndex, GlobalWindFlow, GlobalWaterFlow, IntersectForAir,
  SixDirMaterials, buffer, IntersectWater, OutsideOuterBoundary, OuterBoundaryDist`. Its box per scene is in
  `romfs/Game/VolumeStatsAABB/`. **Several maps have one grid per game state**, chosen by flags: `_presence_` files
  for the Water Temple's water levels, Lake Hylia (`LakeHrl`, `IceDel`, `Dm_Awake_Water`), Forest Temple
  (`Left`/`Right` corridors), Spirit Temple `Lift`, Gerudo Training Ground `Room04`, Dodongo's Cavern `Clear22`,
  Bottom of the Well `Blow_Ocarina`, the Royal Family's Tomb `RFTomb_Open`, Zora's Domain/River water.
  The `.bpvsts` format isn't decoded.
- **The sensor** (`[actor+0x270]+0x460`, filled each frame by `main+0x38AEF0` from cells round Link's position
  rounded to 1 m; the "grid lookups" of §9.7) holds two probes: probe 1 `+0x37C` hit / `+0x510` distance and probe 2
  `+0xBBC` hit / `+0x115C` distance. The flag builder `main+0x5F90A0` turns them into:
  - probe 1 hits within 5 m → valid ground, no bit 24.
  - probe 1 hits 5 m or more away (or with no distance) → bit 24 (the restart point holds), unless P`+0x13D0` bit 18
    is set. Probe 2 isn't read then.
  - probe 1 misses → bit 24 whatever probe 2 does (a miss too); **probe 2 hitting at 8 m or more → bit 25 too
    (void)**.
  - Then, in every case, attribute 8 of the cell (`[S+0x108]` vfunc `+0xB0`, arg 8: value + valid byte) and its
    value (vfunc `+0xB8`, a float; not read when probe 1 missed): set and value > 3 → **bit 19 (void)**; set → bit 20
    (the restart point holds, §9.7) whatever the value; not set and value ≤ 1 → bit 20 too. Probably
    `OutsideOuterBoundary` / `OuterBoundaryDist` (guess: attribute 8 isn't matched to a name).
    If the sensor's `+0xA8` bit 18 is set, bit 19 is set at once. `main+0x38AEF0` sets that bit when Link is more
    than 5 m outside the box at sensor `+0xEC..+0x100` (the VolumeStats AABB, by its shape).
  - The whole step is skipped while an event runs (`[[main+0x4E94840]+0x68]` ≠ 0), when sensor `+0xA8` bit 1 is
    clear, or when P`+0x1C54` bit 0 is set.
  - Probe 2 (Probe C) stores its `VolumeStatsHit` at sensor `+0x948..+0xD4F` (vtable `+0x948`, 8 level records `+0x950..+0xB8F`, `max_level` `+0xB90`, `chunk_origin` `+0xB94`, anchor `point` `+0xBA0`, `cell` `+0xBAC`, `hit_face` `+0xBB8`, `has_point` `+0xBBC` bit 0, `extra_flags` `+0xBC0`, `extra_count` `+0xD48`) plus `dist3` `+0x1158`, `dist2` `+0x115C`, and the 10-frame Fibonacci ray rotation counter `ray_phase` `+0x1160` (0..9; identical offsets in 1.0.0 and 1.1.1). When `has_point == 0`, `main+0x38AEF0` (`0x38B554`) resets `ray_phase` to 0 and runs all 64 Fibonacci-sphere rays in a single frame; when `has_point != 0`, it shoots 6–7 rays/frame (stride 10) and only shifts `point` if a ray hits strictly closer in 3D than `|Q - C|`. Every `VolumeStatsHit` reader (`0x388340`, `0x38AEF0`, `0x38E3DC0`, `0x541A20`) skips level/extra record pointers when `max_level < 0` and `extra_count < 0`, so `memfile.c` safely saves/restores Probe C's anchor/cell/phase (zeroing records and setting both counts to `-1`, and appending under `EXTRA_VOID_RING` in `.szm` `XTRA`), and clears Probe C (`has_point = 0`) while teleporting or loading older `.szm` files without ring data to force a fresh 64-ray scan.
- **Checked in game** (Sacred Forest Meadow, child; a temporary void-out log, not in the repo, ~50 positions round
  (-31..-28, 122..131, -121..-115), a ledge west of the grid's box, x min -29):
  - Probe 2's distance is **how far the nearest valid grid ground is, not floor below Link.** Link stood on solid
    ground at y 131.16 with 8.16 and fell 7 m down a slope with it going 7.78 → 8.74. It changes smoothly
    with position (by up to 1.7 m over 0.6 m sideways), so it looks like an interpolated distance field.
  - Probe 1 missed (bit 24) everywhere there. **Every void-out had probe 2 at 8.06–8.87; every position under 8
    (7.06–7.98) held**, and that included standing for seconds at 7.85. Nothing else differed. Bit 25 comes on
    one frame after the distance passes 8, and the void-out comes the frame after that.
  - Teleporting there didn't void at random either: the same rule held.
  - One void-out came from bit 19 with probe 1 hitting (levitating up at (-21.9, 120.8, -114.7)), the boundary
    attribute case above.
- **Why void-outs can seem random:**
  - mostly, **the 8 m contour runs through places Link can stand**, so a few tenths of a metre in where he lands
    or stops decides it, and in-game nothing marks where it is (seen above);
  - field maps load their grid tile by tile, and dungeons swap whole grids with game state (above);
  - the check doesn't run during events, so ending one (the ocarina, a text box) can let it fire later;
  - the contact areas are logic graphs with their own conditions (`...Condition` nodes), not plain volumes.
- **Not the trigger:** `Obj_Abyss_A_01` (placed in many dungeons) is only a dark bottom model with a dummy physics
  param. The collision's user shape tag `Abyss` (bit 4, mask 0x10, PhiveConfig material presets `Floor_Abyss`,
  `Material_Sand_Abyss`, `Material_Metal_Spike_Abyss`) exists, but no code reading it was found yet.
- Where a void-out puts Link back (`EventPlayerPostAbyss`, `main+0x26BA6F0`'s node) wasn't followed.
### 9.26 Returning to the title screen (both builds; 1.0.0 addresses, 1.1.1 in brackets; checked in game on both, 24th November 2026)
- **No call does it:** the game reaches its title screen by a state change of the sequence graph
  (`romfs/Sequence/FormalSequence.root.ainb`, read with the `ainb` package 0.2.17; 0.2.18 doesn't import). Its
  `Title` state (node 0, `Title.module`) is entered by the transition named `Title` of two states only: `PlusMenu`
  and `Event`. `Main` has no such transition.
- **The pause screen's System > Return to Title** (`PlusMenu.module`): the string selector on the decided button
  (node 51) takes `Pa_System_00/Pa_Button_04` to `ConfirmationBackToTitle.module`, which opens `SystemWindow_00`
  (preset `BackToTitle`: the strings `NewGame`, `ResetOption`, `BackToTitle`, `BackToTitleFromGameOver`... in main
  are these confirmation presets, not requests) and waits for its buttons; Yes runs `PlusMenuToTitle.module`, then
  `DeletePouchAssignableItem`, `ResetCursorLog` and `ResetSystemMenuCursor`, and the state ends with `Title`. All of
  it is chosen by the screens' buttons, so it can't be asked for without driving the pause screen.
- **Leaving an event:** `Event.module` runs `AfterEvent.module` once no main event plays; its string selector on the
  String16 `Sequence_EventResult` (`0x6000F875`) takes `Title` to `EventToTitle.module` and the state ends with
  `Title`. This is how Game Over's Quit leaves play: `DmF_SY_GameOver` › `DisplayGameOver` runs
  `GameSystemActor.EventSetEventResult(EventResult=Title)` and sets `Sequence_ToTitleFromGameOver`. In
  `GameSystemActor.pack` › `AI/GameSystemActor.event.root.ainb` that command is node 33, whose only child writes
  `Sequence_EventResult` through `OneShotSetNonStructGameDataString16`. `Defeat_Ganon` does the same with
  `Sequence_ToTitleFromGameClear`; no other event flow uses the command.
- **EventToTitle and PlusMenuToTitle do the same work** (close `SystemWindow_00` / `SystemBG_00`, prepare the loading
  screens, unload the resident screens, `XLinkModuleReset`, `System_IsSaveTemporaryStorage` = false,
  `GameSafetyMgrStopSafety`, `UnloadBanc` with the resident actors, Link's pouch and the menu actor deleted,
  `ChangeScenery`, `BeforeTitle`, `CloseLoadingFade`). EventToTitle also closes `GameOver_00` and clears the texture
  cache, and only with `Sequence_ToTitleFromGameOver` (`0x53F4FC89`) releases `GameOverPause`, only with
  `Sequence_RequestLastGameOverSave` (`0x2C095360`) saves, and only with `Sequence_ToTitleFromGameClear`
  (`0x04D31E64`) makes the game clear save. With all three false it saves nothing. The graphs are byte for byte the
  same in both builds except EventToTitle, where 1.1.1 adds two `SeqExecuteGenericDummyAction` nodes.
- **The event has to be a real one:** Main's branch picker (a native selector, `_LocalModule_cf31571495c4` is empty;
  1.1.1 vtable `main+0x4BD0158`, select slot 39 `main+0x3303CA0`) takes `GameOver` while `IsGameOverTrig` is set and
  `Event` while `[M+0x68]` (the running event, §9.10) isn't 0. So something must run as an event for AfterEvent to
  read the result.
- **The carrier:** `DefaultParentEvent` (`romfs/Event/EventFlow`, the same file in both builds) is one
  `GameSystemActor.EventWait(Frames=30)`. It has its own row in `RSDB/EventSetting` (`FlowchartEvent`: non-members
  `AllPause`; StarterPos base setting) and many `Dm_*` cutscene rows use its flowchart as theirs. The running flow's
  name (`flow+0x8`, §9.10) is `DefaultParentEvent`.
- **The game's SetString16** `main+0xF83410` [`main+0xF69FF0`] (`manager, const char *const *value, hash`): what both
  String16 nodes call (1.1.1 `SeqOneShotSetNonStructGameDataString16` slot 40 `main+0x3BB64F0`, vtable from
  `main+0x4C6F630`; 1.0.0 `main+0x3B588E0`, through the GameData manager `[[main+0x4C184D8]]`). It finds the field in
  the String16 type's hash table (manager `+0x4A0`, so the type entry is at manager `+0x488`) and queues the write
  (`+0x4B8`, `0x28`-byte entries, string copied), applied at the next flush like SetInt's. 1.0.0's was matched by
  masked signature over 64 instructions: one match, the same over 16 to 64, and its four callers line up with 1.1.1's.
- **The other states lead to Event too** (from the graphs): a game over is an event (Main's `GameOver` branch runs
  `BeforeGameOver`, which clears `IsGameOverTrig`, then `BeforeEvent`, then the state ends with `Event`), and so is a
  cutscene, a dialogue or a chest. AfterEvent's string selector reads `Sequence_EventResult` as soon as it's entered,
  so any running event whose end is reached with "Title" written takes the game to the title screen. The menus don't:
  `PlusMenu` and `MinusMenu` end with `Event` only through `MenuToEvent.module`, on their own screens' choices (a warp
  from the map, ...), never on a requested event; they go back to Main when closed.
- **Starting whatever Link does:** `DefaultParentEvent` waits until Link can join an event. `RSDB/EventSetting` rows
  with the component list `FlowchartWithIgnoreDissentPlay` (109 of 2380, `Dm_SY_BancChangeGo` among them) don't;
  `DefaultBootEvent` (camera and player-state settling, base setting PlayerPos) is one with no lasting effect.
- **Don't end a map change's event before its load:** ending `Dm_SY_BancChangeGo` two frames after it reserved the
  map (`Sequence_ReservedNextBanc` set) reached the title screen, but Continue afterwards loaded the map and stayed
  black with Link not in play (checked in game on 1.1.1, 24th November 2026). Waiting until `Sequence_IsLoadingBanc`
  has come on (and gone off) first works: the load, then the arrival event (`Dm_SY_Ocarina_move_Ed` after a Bolero
  warp), then the title screen, and Continue plays normally.
- **title.c** (remote `title`, **debug > return to title screen**), every frame until Link has been gone for 10
  frames with nothing loading (a minute at most):
  - a map loading or reserved and not yet loaded (see above), the game saving or loading, the module's own warp or
    memfile: wait (a warp armed for the next exit is cancelled);
  - a game menu up (`gd_game_menu_screen`, or paused / pausing): B for 3 frames in every 6 (given to the game even
    while the module's menu or free camera holds the controls);
  - an event running: write "Title" with SetString16 every frame, and after 2 frames end it with the event
    manager's cancel (eventskip.c), again every 20 frames while one runs; if the flow's name has `GameOver` in it,
    `Sequence_ToTitleFromGameOver` is set too (EventToTitle then releases the game over's pause, as Quit does);
  - otherwise: request `DefaultParentEvent`, then `DefaultBootEvent`, alternately every 30 frames.
  "Title" is only written while an event runs, so AfterEvent is its only reader; it's written `""` at the end. Frame
  advance doesn't hold the game meanwhile, and the free camera is turned off.
- **Checked in game** (24th November 2026): ordinary play (1.1.1: 50 frames), the pause screen (1.1.1: 83 frames;
  1.0.0: 96 frames, the first request not taken while the screen closed), a warp under way (1.1.1: through the load
  and the arrival event), the game held by frame advance (1.1.1); Continue afterwards each time. **Not checked:** a
  game over, a dialogue or chest, Link mid-air or swimming.
- **Not used:** driving the pause screen (open it with `DebugParam.IsRequestedTransitionPlusMenu`, which
  `BeforeMinusMenu` clears, then pick System > Return to Title > Yes): it depends on the screens' cursors and
  animations.

### 9.27 Killing an actor: the death request and the delete request (both builds; 1.1.1 addresses, 1.0.0 in brackets)
- **The Death component** of an actor is `D = [[actor+0x270]+0x1C0]` (NULL where it has none); `D+0x18` is its actor.
  Every death goes through `0x3A8BB0` [`0x3EF850`] `(D, u32 reason, u32 sub, bool internal) -> bool`: false if `D+0x2C`
  bit 5 is set (rejects every reason), if the actor is already dying for an equal or higher reason, if the reason is
  16 or more, or if the actor's `DeathReasonProcedures` table (`romfs/System/CombinationDataTableData/*
  _game__death__DeathReasonProcedures`, chosen by its `Component/DeathParam`) has no cell for the reason or the cell
  says `IsIgnoreThisDeathReason`. Otherwise it records the request (a compare-and-swap on `D+0x20`: made for callers
  on other threads) and D's own update runs the procedure; an actor not running (state other than 4) with
  RequestDelete or RequestFadeOutDelete is deleted at once.
- **Reasons** (the reflected enum `game::death::DeathReason`, table `main+0x3EF8090` [`0x3E98BF0`]): 0 AI,
  1 ChemicalCriticalHitFire, 2 Drowned, 3 Eaten, 4 PickedUp, 5 Exploded, 6 Burned, 7 Unequiped, 8 FallIntoAbyss,
  9 PhysicsKinematicCollision, 10 LifeDepleted, 11 LifeCondition, 12 UpperCrushed, 13 SideCrushed, 14 Frozen,
  15 AutoPlacementMgr. No plain "kill". The game always passes sub reason 0.
- **Who sends one:** the void areas' node `HoldSensorHitInfoGiveAbyssActor` (a message, type `0x613B0000`, reason 8,
  that D's update `0x3A88B0` passes on); the AI node `OneShotDeathRequestDeath` (on its own actor) and
  `OneShotDeathRequestDeathToTargetActor`, which calls **`0x2D0A540` [`0x2CB69B0`] `(void **D, reason, sub) -> bool`**:
  it checks D's type, then `0x3A8BB0(D, reason, sub, 0)`, synchronously, on another actor. 1.0.0's is the only
  masked-signature match of 1.1.1's over 24 and 40 instructions.
- **FallIntoAbyss** is what debug > actors > kill sends (actors.c, `actor_kill`): the default and enemy tables fade the
  actor out and delete it, with no drop or death effect (the enemy table also clears its respawn flag), and the
  tables that ignore it (the dungeon bosses, Bongo Bongo, Volvagia, Dead Hand's hand, Skull Kid, the club Moblin,
  crack objects and some more: 18 of 112) refuse it, so their scripted deaths aren't skipped. LifeDepleted (the
  real kill, with drops) is ignored by 1 table only, but 78 wait for the AI's signal, so with life not at 0 an enemy
  might stay "dying"; Upper/SideCrushed would kill bosses outright. Not used.
- **With no Death component** (most NPCs and props, all scenery and the game's system actors; about 139 of 341
  Enemy_ packs have one, 161 of 997 Obj_, 32 of 379 Npc_, all Npc_Equipment*): the engine's delete request,
  **`0xA6490` [`0xC4FA0`] `(actor, u32 reason) -> bool`**, what `OneShotGenericDeleteThisActor` calls with reason 0
  (`OneShotGenericFarDeleteThisActor`: 3). False if the state is 7..11 or a delete was already asked for (`+0x30`
  bit 16) or a per-actor mask forbids it; otherwise it sets that bit and queues the actor on the manager, taking
  the manager's lock (`mgr+0x3C8`) itself when called from outside the actor system's threads. Unique match over
  32 and 64 instructions. It skips everything a death does: no respawn flag, no drop, nothing counted as killed.
  The mod only deletes Npc_, Obj_ and Item_ actors this way, never Link.
- **Checked in game** (24th November 2026, Lichii): a Deku Baba (taken; state 7, then gone: about 180 frames on
  1.1.1, under 800 on 1.0.0), the Deku Tree's spider web and its interactive part (taken, gone), Gohma (refused, on
  1.1.1), a Kokiri (no Death component: deleted) on both builds; the game ran on normally after each. Not checked: a
  room that opens once its enemies are dead, after one is killed this way.

### 9.28 Moving an actor: the warp requests, and why fetch was dropped (1.1.1 addresses, 1.0.0 in brackets)
- **Tried for debug > actors > fetch** (the chosen actor to Link), not kept. The AI nodes that teleport actors
  (`OneShotWarpToHomeMatrix`, `OneShotWarpNpcFairyToTagetPosAndRot`, `OneShotPoeWarpTo`; a node's execute is the
  vtable entry 0xA0 bytes after its name function's) go through **`0xC6FB30` [`0xC35500`] `(actor, const float m[12],
  bool)`**: m is 3x4 by rows, the position in the fourth column. It calls the physics body's warp
  **`0x4C6630` [`0x665790`] `(physics [[actor+0x270]+0x50], const float pos[3], const float rot[9], bool)`** (NaN in
  `[0]`: keep that part as it is; the actor is `[physics+0x38]`): it stores the matrix at `[physics+0x20]+0x1D0` and
  sets bits 0 and 1 of `[physics+0x20]+0x228` (pending). Only if that returns false does `0xC6FD60` write the actor's
  own request (position `+0x358`, rotation `+0x364`, clearing bit 15 of `+0x4A8`). The actor's rotation is 3x3 by rows
  at `+0x328`, after its position at `+0x31C`. Both matched uniquely over 24, 40 and 64 instructions.
- **In game** (1.1.1, 24th November 2026): taken every time, but nothing moved. A gossip stone (static), a Kokiri
  (40 m away, its AI schedule), a Deku Baba and a Skullwalltula (their AI): `+0x31C` stayed put and the body's
  pending bits stayed set for good (logged for 40 frames). So the physics only applies a warp to bodies it steps
  freely; one left pending could fire later, which is why fetch isn't offered. Not tried: loose things (items,
  pots, bombs), which may be the ones it works for.

### 9.29 Making an actor: the create request (both builds; 1.1.1 addresses, 1.0.0 in brackets)
- **The call:** **`0x372A70` [`0x97FEB0`] `(mgr, const char *const *name, const arg *, handle *, u32 queue, 0, 0, 0,
  false, u32 *result, 0) -> bool`**, on the manager `sys_ptr(main+0x4E91888)` [`0x4E21428`] (not the actor tree's
  manager). It's what the AI node `ExecuteGenericRequestCreateActor` calls (vtable `0x49A0818`, Enter `0x26E62B0`); in
  1.1.1 the game also makes `FldObj_AirWall_Player_GenerateNavMesh_Dynamic` this way with no handle and queue 0,
  which is how debug > actors calls it. 1.0.0's: unique masked-signature match over 24 and 48 instructions (the whole
  function, 826 instructions, differs only in addresses); its manager slot from the matched caller, 0x70460 below.
- **The argument** (0xA0 bytes, zeroed): position `+0x00`, rotation 3x3 by rows `+0x0C`, scale `+0x30` (with flag 4),
  instance name `+0x40` (never NULL: `""`), flags `+0x98` (1 position, 2 rotation, 4 scale). The rest (links,
  overrides, a heap) stays 0, so a made actor has none of a placed one's map data.
- **What it does:** finds or makes the name's template from `RSDB/ActorInfo` (resident), asks for
  `Pack/Actor/<name>.pack` itself (asynchronously, shared with any copy already loaded), and queues the request for
  the actor workers. So any actor with a pack can be made on any map. The result code: 0 queued, 1 the manager isn't
  taking any (a map change), 2 no name, 3 no template, 4 setup failed, 5 no pack name, 6 no heap, 7 and 8 out of
  memory. A name with no ActorInfo row takes a template slot for good, so spawn actor by name or type only offers the 1856 enemies,
  NPCs, objects and items both versions have a pack for (`tools/actornames.py` writes `src/ootgz/actornames.c` from
  the dumps' romfs).
- **Checked in game** (24th November 2026, Lichii): a copy of a guidepost in Kokiri Forest (1.1.1), of a Deku Baba in
  the Deku Tree (1.1.1: running, life 2/2, killed by debug > actors > kill), and an Armos (`Enemy_Amos`, whose pack
  Kokiri Forest doesn't load) in Kokiri Forest on both builds: each in the actor tree 10 frames after the request,
  running, life 1/1. Not checked: what a made actor does without its placement data (NPCs with event flows, doors,
  bosses), whether made actors go with a map change, and memory limits after many.
### 9.30 A free camera: the render camera's matrix, and what the world is streamed round (1.1.1; 1.0.0 in brackets where checked)
- **The picture:** the render camera `[[main+camera_ptr]+0x68]` (link.c's `camera()`) is a double-precision sead
  look-at camera: eye and look-at as doubles at `+0x60` and `+0x78`, as floats at `+0x38` and `+0x44`, up (floats) at
  `+0x50`. Its vtable (`main+0x4BF8A88` in 1.1.1) has its matrix function twice, slots 4 and 11 (`0x7139C0`, a leaf:
  `(camera, float out[12])`, from the doubles and up). It runs after the game's camera logic has set eye and look-at,
  so giving that one object a copy of its vtable with those slots leading to the module first (eye and look-at
  written, then the game's function) puts any view on screen, culled to it, with the overlays (which read the same
  camera) following. debug > free camera does this (freecam.c); the object gets its own vtable back when it's off,
  and is hooked again when the game makes a new one. Checked in game (24th November 2026, 1.1.1): 100 m above Kokiri
  Forest and 450 m across Hyrule Field, and back.
- **Up:** the matrix is built with the camera's up as well (floats at `+0x50`), and the game's isn't straight up: its
  own camera leans it with its view (`0.215 0.968 -0.126` at Lon Lon Ranch, 1.1.1, written again every frame). Left
  as it was, the free camera's picture rolled by that much, the horizon crooked; freecam.c sets it to `0 1 0` in the
  same hook (checked in game, 25th November 2026).
- **Not like this:** writing the player camera's views (`component+0x5B8`, +0x450..+0x4F0, §9.18) from the frame hook
  and snapping: the drawn eye the next frame was the game's again (the hook runs before the camera's update).
- **The traverse system** (what the world is placed round): the game interface `[[main+0x4E91888]]` [`0x4E21428`]
  holds at `+8` a store of 7 positions (vtable `main+0x4B99818` [`0x4B34748`]: `+0x20` set(pos, i), `+0x38` tryGet(out,
  i), `+0x48`/`+0x50` set/clear additional position i, at index i + 2). Index 0, the base, follows Link exactly
  every frame (logged once a second while he walked). Events put positions in 2 to 6 (`TraverseSetting`,
  `SetTraverseBasePos`), and `0x9B7ED0` [`0x9CF350`] `(mgr [main+0x4E91930], true)` traverses again, as
  `OneShotRequestInvokeTraverse` does.
- **Not enough for the world to follow a free camera** (tried in Hyrule Field, 1.1.1): with the camera's position in
  additional position 0 (index 2, read back each time) and a traverse every 50 m, 450 m from Link there was 1 actor
  within 80 m of the camera (and 73 round Link), the same as with neither (1 and 74); the terrain there stayed the far,
  simple version. Writing index 0 as well, from the frame hook and from the camera's matrix function, changed
  nothing either. So the terrain, and the actors that come with its areas, stream round something else (Link's own
  position, read where he's updated, is the likeliest). Moving Link with the camera would stream the world natively
  but sets off loading zones, triggers and voids.


### 9.31 The time of day: the WorldMgr's clock, and when it runs (both builds; 1.0.0 addresses; checked in game on both, 24th November 2026)
- **Reading and setting it** (as §9.17; timeofday.c, memfile.c): `wm = [[main+wm_ptr]]`, `[wm+0x578]` is the clock,
  whose vtable slot `+0x30` returns the seconds into the day. `setTime(wm, seconds)` `main+0x33F1580` doesn't set the
  clock itself: it calls `main+0xEC1F10` on `[wm+0x510]`, which stores the seconds at `+0x48` (and `-1` at `+0x4C`)
  and sets request bits in the flags at `+8`. The WorldMgr's update applies the request later.
- **Before the story starts the clock** the clock reads exactly 06:00, doesn't move, and every request is ignored
  (the user's early save, Hyrule Field, both builds). Talking to Kaepora Gaebora in Hyrule Field on leaving the Lost
  Woods starts it, and so does Impa's escort after Zelda's Lullaby (from the user). On a later save (Goron City)
  setting the hour and minute, the presets and freezing (setting it back every 10 frames) all work.
- **What starts it isn't found yet.** Ruled out:
  - Kaepora's flow (`HyruleField_Goto_HyruleCastle_Kaepora`, flowchart `Goto_HyruleCastle`) writes no GameData of its
    own: it plays `Dm_HrlField_0010`, whose only clock actions are
    `EventWorldManagerController.EventTrigger_SetTime(7:30, then 7:00)`, and moves the quest `Goto_HyruleCastle` to
    Step02. Impa's escort cutscene (`Dm_HrlField_0020`) has no clock action at all, so the switch is probably a
    condition the WorldMgr checks rather than something either event sets.
  - Setting `Goto_HyruleCastle` to Step02, and `DoorOfTime` to Ready and to Step01, in memory (story menu) didn't start
    the clock at once. No map was reloaded afterwards (that might autosave the changed story state).
  - `IsWorldTimeStop` and `IsDisableTimeProgress` are strings in main but not GameData (no field has their hashes).
    `HyruleField_Child`'s GameBancParam and WorldParam have no time switch (the WorldParam's `$parent`,
    `MainField`, isn't a loose romfs file).
- **Next:** the same quest steps followed by a map reload, or watching `[wm+0x510]+8` / `+0x48` with the GDB stub to
  find what applies or drops the request.

### 9.32 The world overlays over the game's pauses (both builds; checked in game on 1.0.0, 24th November 2026)
- The overlays drawn in the world (airwall.c: air walls, void out, save positions, the hookshot's hook) were
  hidden whenever the game paused (`gd_game_paused`), so the item wheel, which pauses the game, made them go and come
  back at once each time it opened and closed: flashing when it's opened and closed quickly.
- **The item wheel turns the game's camera** (the camera angle watch went from 329.5 to 58.3 with it open) and the
  game draws the world behind it from there, but the scene depth the module copies (scenedepth.c) isn't refreshed
  meanwhile. So with the overlays kept live, the depth-tested faces (the void shading, the fence and columns' sides)
  were dropped (`gpuui_face` needs a fresh depth), and keeping the last depth with the live overlays tested them
  against the old view and smeared them over the wheel.
- **What's done instead:** while the game is paused, the last frame built stays as it was, with the depth it was
  drawn against (`scenedepth_keep_last`), and fades out over 20 frames where the overlays are hidden (the pause
  screen and map, and the item wheel with **hide world overlays over item wheel**, on at first); they fade in again
  after (`airwall_fade`, applied in overlayui.c). Off, they stay over the wheel as they were.
- **Telling the wheel from the pause screen:** the menu screen Enum (`0x578D41E1`) is `None` (`0xB6EEDE09`) for the
  R item list. When the user opened their wheel, the overlays were hidden as for a menu screen, so the wheel they use
  may set it; the log's pause line now gives the value. Enemy health bars (enemyhp.c) still go and come back at once.

### 9.33 A map's places: dungeon rooms, grottos and interiors in the layouts (1.1.1 dump; checked in game, 25th November 2026)
- **No separate maps for them.** `romfs/Banc` has 33 maps (and two folders of shared data); grottos and interiors
  are placed inside their parent map's layout (`<Layout>_{Static,Dynamic}.bcett.byml.zs`), the way the dungeon rooms
  are inside the dungeon's. `tools/places.py` writes them to `data/places.json` (392 places in 48 layouts), built into
  `data_places` / `data_placemaps` (data.c) for the explorer (placer.c), found by the map name (`GD_STR_MAP`).
- **Dungeon rooms:** one `DungeonRoomCenterAnchor` per room (11 to 34 in each dungeon), with no room number; the
  number is the `Dynamic.DefaultDungeonRoomIndex` most of the nearest numbered actors carry. Labelled by the room's
  enemies and chests (as conversation-log/dungeon-rooms-2026-11-10.md).
- **Grottos** (the game's "hiding holes"): each is an AI group `HidingHole_Level_Hole_NN_Prefab_xxxx` (NN its kind)
  whose references are the grotto's actors, a `DestinationAnchor` where the game puts Link when he drops in, the
  inside `Area` and the hole's `Area` on the surface (Hyrule Field's ReDead grotto: anchor 154.2 136.5 18.7, the hole
  163.6 155.7 27.9). One grotto can be several groups at one spot (its hole, its room, `..._EnvArea_*` its look):
  merged within 6 m across. About half have no anchor (Kakariko's: its entrance under a rock); those are placed at
  their lowest actors. 13 in Hyrule Field (child).
- **Interiors** are inside their buildings where they stand: a building `Land_<Town>_House_*` (not its
  `HouseOutside` shell) has its origin on the floor inside (Kakariko's `House_A_03`: an NPC 0.7 m from it), and
  interiors elsewhere are made of `Land_*_Interior_*` / `*_Inside_*` pieces, and in Hyrule Field and Gerudo Valley
  marked by `IndoorAreaBox` volumes too (none in Kakariko or Kokiri Forest). Doors (`Obj_Kakariko_Door_*`,
  `Obj_Door_Hinge_*`) are plain openings with no links. Gathered where they're within 7 m across and 5 m up.
  Checked: Link moved to a Kakariko house's origin by load position's move stands inside it, on its floor.
- **Not here:** loading zones (exits.json) only lead to other maps, never into a grotto or a house. Not checked: going
  to a dungeon room by load position's move, without the room change the Wallmaster's flow makes
  (shelved-room-warp-2026-11-10.md), may leave the room asleep.
- **What wakes a room** (Forest Temple, 25th November 2026): with Link at the entrance, the camera in room 4 shows
  its walls, floor and chest but not its two Stalfos; Link moved there by load position's move wakes it at once
  (both Stalfos, 18/18, the map marking him in it). Writing GameData `DungeonRoom` (Int `0x9B529F63`, -1 outside a
  dungeon) to 4 directly stuck (the game didn't put it back) but woke nothing, the map still marking him at the
  entrance: it's a record of the room, not what wakes one. The waking follows Link himself, likely through the room
  number in the VolumeStats grid where he stands (`DungeonRoomId`, §9.21); not found yet.
- **What the explorer does for now:** going to a room moves Link into it (load position's move) so it wakes, and B
  puts him back where he was (placer.c, `home`).
- **To wake a room with Link left where he is** (the explorer's room loading as it should be; estimated 2 to 4 hours,
  about an even chance): dump main from Lichii's memory (main-memory-dump in the session notes) and find
  1. the event action `GameSystemActor.EventChangeDungeonRoom` (`ChangeRoomID`; the Wallmaster's
     `Enemy_Wallmaster_ReturnToEntrance` flow and `Defeat_Ganon` use it, shelved-room-warp-2026-11-10.md): its
     handler, and the manager it tells (the current room, and what creates and deletes the rooms' actors);
  2. who reads the VolumeStats `DungeonRoomId` at Link's position each frame and hands it to that manager;
  3. whether the room's actors are then made by room alone, or also only within a distance of Link (the traverse
     positions of §9.30: index 0 follows Link; an additional position there woke nothing in Hyrule Field).
  Then, from the explorer: call the room change for the room the camera is on (or override what the manager reads
  for Link), and give the room back to Link's own when exploring ends. If actors are also made by distance from
  Link, a traverse position at the room may be needed too; if that can't be done, moving Link stays.
### 9.34 A spawned horse of one's own: the owned horse and the rodeo bit (1.1.1 addresses, 1.0.0 in brackets)
- **Why a spawned Epona won't be ridden:** a ridden horse whose **rodeo bit** is set bucks the rider off. It's bit 3
  of the u32 at QuadrupedComponent `+0x2E8`, QuadrupedComponent `[[actor+0x270]+0x388]` (`QueryQuadrupedCanRodeo`
  `0x2ACB9F0` [`0x2A789A0`], `OneShotQuadrupedChangeCanRodeo` `0x2916B00`). `Animal_Epona_Adult`'s QuadrupedParam has
  `CanRodeoDefault` true (`Animal_Horse`'s false). The component's init (`0x9784D8`) sets it from that and clears it
  for the owned horse (the actor's id `+0x18` equal to OwnedHorseMgr `+0x80` or `+0xB0`); the horse's setup clears it
  after the Lon Lon race (`Step_Get_Epona` == AfterWonGambleRace), and the rodeo clears it when it succeeds. A spawned
  Epona is none of these, so she bucks, adult Link included, on a save that has her or not.
- **Owned:** OwnedHorseMgr is `0xB3CC40` [`0x26AD4E0`] `(scene manager [main+scene_mgr_ptr])`; `0x329D990`
  [`0x323E840`] `(mgr, horse)` registers a horse as the owned one (what the map's horse setup and the horse component
  call; it does nothing unless the horse's kind is 1 or 2), after which `+0x80` is the horse's id. It doesn't touch
  the rodeo bit (the init ran before it), so quick spawn's **epona (owned)** registers her and clears the bit for 60
  frames (the init can set it again just after she appears).
- **Checked in game** (24th November 2026, 1.1.1): adult Link in Gerudo Fortress, on a save that never won Epona,
  mounts and rides her (registered alone, or with the Epona quest at Complete, she still bucked); child Link in Lon
  Lon Ranch mounts her too, slowly and with odd poses (his model has no horse animations). 1.0.0's addresses are
  masked-signature matches through the callers, not run. Not tried: whether Epona's Song then calls her (the save
  had no song).

### 9.35 Lank and Lonk, and changing the game's files at run time (beta; 1.1.1; checked in game, 26th November 2026)
- **What decides the proportions:** child and adult Link are two actors (`Player_Child`, `Player_Adult`), each with
  its own model and its own `Component/AnimationParam` in its pack (`content:/Pack/Actor/Player_Child.pack.zs`,
  `Player_Adult.pack.zs`). Its AnimationResources name the animation sets by ModelProjectName: the child's
  `Player_Child_Demo_Animation`, `_01` to `_04` and `_Cba`; the adult's the same six (`Player_Adult_…`) plus
  `Player_Adult_02_H_Animation` and `Player_Dark_Animation`. The animations carry the bone lengths of the skeleton
  they were made for, so a model playing the other age's sets takes that age's proportions: the cutscene look
  (a scene made for one age played at the other) in normal play.
- **When the names are read:** when the game makes Link, which is only at a file load or an age change (a reload
  or a map change at the same age keeps him). It reads the age's pack through main's nn::fs imports (`OpenFile`,
  then `ReadFile(size_t *, FileHandle, long, void *, size_t, const ReadOption &)` in pieces of 902,912 bytes) or,
  when it read it before, makes him from what it kept: the AnimationParam file alone, in a 64 KiB block of its own
  (names at offset 0x124), seen to last across age changes; at other times the pack was read again.
- **gamepatch.c (any file of the game):** wraps main's `OpenFile`, `CloseFile`, `GetFileSize` and three
  `ReadFile`s (`got_hook`). A patch names a file, a member and a fix that changes bytes in place. While it's on,
  opening its file reads it here (`gamefile_read_zs`), runs the fix and answers that handle's size and reads with
  one zstd frame of stored blocks (no dictionary: the game decodes it; 1.18 MiB for the child's pack). Switching a
  patch also runs its fix on the copies the game kept: the ones found before at once, then a search (a piece each
  frame) for its anchor, a 4-byte aligned word, in memory regions up to the patch's size limit. For these packs
  that's regions up to 1 MiB: 1.1 GiB of the 9.6 GiB of readable and writable memory, about 4 s; all of it took
  20 s and cost frames. The copies found are remembered for the next switch.
- **remake.c (making Link again):** `remake_start(age)` reloads the map Link is in, as it is named, with his age
  forced (age.c's `age_reload_map_as`: the age set and DebugParam.IsSetChildhoodManually held through the load, as
  warps > override age does), so it works in a map with no version for the other age (GerudoFortress_Adult). One
  load when that changes his age, else two, through the other age and back (sword, shield and clothes come back).
  It starts once the search for kept copies has ended and no event runs, waits for each load's arrival event to
  end (a warp asked for during it is refused), and puts Link back where he stood. For any change that shows when
  Link is made.
- **lank.c:** two patches, one on each age's pack, whose fix swaps `Child` and `Adult` in the six shared names (the
  same length, so nothing moves), after checking the key and the age's list are there (the actor database also
  lists the names, without the key), and the **link > lank / lonk mode (beta)** menu: a mode (nothing, lank: the
  child's patch on, lonk: the adult's) and **apply and reload**, which switches the patches, sets warps > override
  age to the mode's age (nothing: disabled again) and makes Link again at that age (nothing: the age the map's
  name gives). Not kept in the settings.
- **Beyond these:** a new change to the game's files is a client of gamepatch.h (and remake.h when it's read as
  Link is made); the shared files only call the modules' init, frame and menu functions.
- **Checked in game (26th November 2026, through the menu's own buttons):** in Kokiri Forest (child map) lank
  (two loads: Lank), lonk (one load: Lonk, an adult in the child map), nothing (the normal child); in Gerudo
  Fortress (adult only) lank (one load: Lank, a child in the adult map), lonk, nothing (two loads through the
  child: the normal adult). Link ended each where he stood. No restart in between. Not checked: 1.0.0, which has
  the same imports and files.
- **What didn't work:** changing the names only in memory once (the pack may be read again), the
  `Model/%s.anim.bfres` formatter calls (they name cutscene and model files, not these sets) and the BYML
  string-field reader of the parameter files (`main+0x2B5C480`, never called for them).

### 9.36 Security guards: how they see Link (both builds; 1.1.1 addresses, 1.0.0 in brackets; values checked in game 26th November 2026)
- **Which actors:** the Gerudo fortress's (`Npc_Gerudo_GEN_SecurityGuard`, `Npc_Gerudo_FortressGuard`, its child) and
  `Npc_Hylia_Soldier_SecurityGuard`. Each has a `SecurityGuardParam` (`GameParameter/SecurityGuardParam/…`) and the
  `NpcSecurityGuard` AI. The getter `main+0x28F72D0` [0x28A4DA0] (actor) searches the actor's parameter table
  `[[actor+0x270]+0xA8]` (count `+0x30`, sorted entries `+0x38`, each `{u32 hash, …, +8 holder}`, the parameter at
  `holder+0x150`) for hash `0x6160B32A`, and **gives a shared default to any actor without one** (Kaepora and Navi
  came back as guards until guardview.c searched the table itself).
- **When a guard finds Link** (`_LocalModule_48bfd38fee41`, in its pack's AI): only while
  `QueryNpcSecurityGuardIsSecurityMode` is true: `main+0x2BF660` [0x2AFC60] (actor), a blackboard bool (hash
  `0xF2F955A4`). Then, if `AlertMode.UseCustomLOS` (the AINB's own blackboard names it):
  `NpcSecurityGuard.action.CheckLineOfSight` (AI.Global pack) with IsAlert true, else with IsAlert false, sets
  IsFoundPlayer; failing both, the close check below. Without UseCustomLOS (the castle soldiers): the engine's sight
  (perception sense 1) or the close check. In security mode the node `OneshotCustomPerceptionParam` switches the
  sight to the receiver's `CustomSightParam.SecurityGuard`: DiscoverAngle 90, DiscoverHorizontalRange 7.5,
  vertical ±7.5 (both guards' `PerceptionReceiverParam`).
- **CheckLineOfSight:** from the Head bone's world matrix (QueryModelGetBoneMatrix34f), the front
  `SelfSpecifiedBoneFrontDir` (a column of the head's rotation by `CustomLOSBaseZ`; Skl_Root's +Z while an AS hold
  event 102 sets IgnoreCustomLOSRotateTracking). `SelectorNpcSecurityGuardCheckCustomLOSDistance` (`main+0x2CF9D70`):
  Link's distance across (XZ) from the head under `range × HorizontalDistMag` (1), and his height between the head's
  + `R[1]·(below, dist, 0)` and `R[1]·(above, dist, 0)` (R the head's rotation: the band tilts with the head).
  Then the angle between the front and Link's look-at point (`QueryAIScheduleGetVectorDegree180`, compared as abs)
  under `QueryNpcSecurityGuardGetCustomLOSAngleInFront`; then three rays (to Link's position, look-at point and
  between) must not all be blocked.
- **The custom LOS's numbers**, `(actor, const bool *is_alert) -> float`: range `0x2CF9550` [0x2CA5790] (7.5 with no
  param), below `0x2CF9760` [0x2CA59A0] (-2), above `0x2CF9960` [0x2CA5BA0] (2), angle `0x2CF9B60` [0x2CA5DA0] (90).
  Each picks `CustomLOSAreaParam.PlayerRidingHorse` if the player is on a horse, else `Dark` if
  `QueryNpcSecurityGuardIsInDarkArea` (`0x2CF8DD0` [0x2CA5010]: the actor's blackboard IsInLightArea /
  IsInDarkArea), else `Light`, then its `Alert` or `Find` set. The Gerudo's file: Light find 14 m 45°, alert 24 m 45°,
  ±3.9 m; Dark alert 14.5 m 50° ±3 m (find has no range: the default); horse 8 / 16 m 75°. Read in game for a
  spawned `Npc_Gerudo_GEN_SecurityGuard` in Hyrule Field (daylight): 14 m 45°, 24 m 45°, -3.9..3.9, on both builds.
- **Which head axis:** `CustomLOSBaseX` and `CustomLOSBaseY` are MurmurHash3 enum names: `"Z"` and `"X"`. In game the
  head's +X column is up (its Y component 1.00) and its +Y column, flattened, matched the body's +Z (the actor's rows
  at `+0x328`) and pointed at Link, for both guards. So the front is the head's +Y.
- **The AlertMode object** (`main+0x28F73A0` [0x28A4E70] (param), inheriting from the parent param when unset): bools
  packed before the set flags: `UseCustomLOS` value `+0xFD`, flag `+0x114`; CloseToPlayer object `+0x58` (flag
  `+0xFF`) with ThresholdInDarkArea `+0x38`, InLightArea `+0x3C`, WhenPlayerRidingHorse `+0x40` (flags `+0x44..46`);
  CustomLOSAreaParam `+0x60` (flag `+0x115`) with Dark `+0x38`, Light `+0x40`, Horse `+0x48`, each with Alert `+0x38`
  and Find `+0x40` (HorizontalDistance `+0x3C`). Read in game: the Gerudo's `+0xFD` 1, the soldier's 0; both
  CloseToPlayer light 3.0 (the struct default for the soldier).
- **The close check** (`_LocalModule_6ee85ae20086`): any perceived enemy-affinity emitter within the CloseToPlayer
  threshold (light, dark or horse), not in cover.
- **The bone:** `main+0x1B3620` [0x7C6DA0] (model, `const char *const *name`) returns `{u16 part, u16 bone}` through
  x8; the model is the model component's (`[[actor+0x270]+8]`) vtable slot `0xF8`; the bone's world matrix (3 rows
  of 4) is `(**(parts[part]))->vtable[0x78/8](skeleton, out, bone)`, parts at `model+0x28`.
- **The guard's state:** its BSA component `[[actor+0x270]+0x188]`, brain verb `u32 +0x28` (QueryBSAGetBrainVerb, calc
  `main+0x29BB9E0` [0x2969540], same code). The BrainVerb enum's names are in main in order: 3 MoveTo, 5 Alerted,
  8 Lookout, 19 Patrol, 51 SuspiciousIindividualDiscovered (sic: caught), 54 SuspiciousIindividualNotice, 59
  VeryCloseNotice. Seen in the castle courtyard (26th November 2026): patrolling soldiers 19 and 8, the gate pair 3,
  and the one that caught Link 51 as its "Who's there?!" played.
- **The courtyard's child soldiers** (`Npc_Hylia_Soldier_021`, `023` to `027_Child`) have SecurityGuardParams of their own
  with custom sight: 12 m / 50° find, 20 m alert (021 and 024: find band -3..-1 m, i.e. lower than the head), 023 10 m
  / 15 m at 60°; the gate pair `Npc_Hylia_Soldier_010_Child` the standard sight. All were on duty.
- **Patrol paths:** a map layout's `Rails` named `Guide` (tools/rails.py -> data/rails.json): points with Translate, a
  Rotate (Y: the way the NPC faces there with IsAdjustPosAndDirToPoint) and WaitFrame / WaitASKeyName
  (Act_LookAround, 180 frames, for the courtyard). NpcCommon_Patrol's `ActionSeq_RailMoveToDefault` walks one, back
  and forth (IsOneWay false) or round a closed one. Which rail an actor walks is in the layout after all: the actor's
  own `Rails` list (`{Dst: the rail's Hash, Name/Gyaml "Reference"}`, besides its `Links`), in the same map's Static
  or Dynamic layout. 207 such links to Guide rails; 177 of the 182 rails have one: Gerudo guards, castle soldiers,
  townspeople (Zelda's 11, Dampé's), dogs, cuccos, rolling rocks, moving floors and spikes, Poes. The actor's
  layout Hash is the instance id the game keeps at `[[actor+0x3D8]+0x88]` (memfile.c; to be confirmed against a rail
  owner with remote's `actor`). guardview.c draws a guard's nearby rails (within 4 m) as before, and any actor's own
  rails by that id (data_rail_owners) when its **path** switch is on.
- **What hides Link, drawn:** the module's ground cast (`main+0x5FF750`, layer 0x1A = 26) is the layer the
  CheckLineOfSight rays test for blockers (LayerHitMask 26). guardview.c casts 25 rays across a guard's widest sight
  from the head to Link's middle height over its feet (0.8 m), 75 a frame round the guards, and stops each sight there.
  In the courtyard they stopped at the hedges, crates and shelves.
- **Triangles:** gpuui draws a quad as a 4-vertex strip of the unit square (0,0), (1,0), (0,1), (1,1); drawing 3 of
  them is the triangle corner, corner + u, corner + v (gpuui_triangle). A fan of parallelograms left streaks where
  each overlapped the next.
- **Not known:** whether the engine's sight takes DiscoverAngle either way of the front or as the whole cone (it's
  stored as degrees and radians, `deg × π/180`, no halving): guardview.c draws the larger. Whether the Gerudo's alert
  set catches Link at once or first has her look (IsFindPlayerImmediate is set for both guards). Guards spawned in
  Hyrule Field are off duty there; the courtyard's were on duty.
- **Found by:** the guards' packs (gamefiles.py), the AINB graphs (the `ainb` package), node names to their vtables
  (getName at slot 19, the node's calc at 39) and the functions they call; 1.0.0's by masked signature, each unique.

### 9.37 Where Link can talk to or act on an actor: attention clients (1.1.1 files; checked in game 26th November 2026)
- **The files:** an actor's ActorParam names `Components.AttentionSenderRef`, an AttentionSenderParam whose
  `ClientList` names attention clients (`Attention/ClientParam/*.game__attention__ClientParam`, in the actor's pack or
  `Pack/ResidentCommon.pack.zs`; NpcStandard_Talk etc. there). `$parent` chains merge key by key.
- **A client:** `ActionType` (what A does) and `AttentionType` (Action, or LockOn: only while Z-targeting), MurmurHash3
  of names found by hashing main's strings: Talk 171795720, Check, Grab, Lift, Take, Ride, OpenDoor 4203576013,
  OpenTreasureBox 3246976983, OpenShutter 3911446764, OpenChainLock 3248323484, OpenForEvent, BringAlong 1400088906,
  Touch, PullOut, PutIn, UseBeansHole, GrabSword, DiggingHoleTour, BlueFishTarget, BlueFireTarget; Action 4120604650,
  LockOn 3016662870. Its `CheckList` checks all have to pass; `CheckType` is the hash of the block's name after
  "CheckType" (AreaFan, AreaCylinderFan, AreaSphere, AreaBox, Line, Terror, GameData, RideHorse, Screen...).
- **AreaBox (the check: `main+0x2C565D0`, 1.1.1):** the actor's model box grown by `AttentionMin` on its negative
  sides and `AttentionMax` on its positive ones, moved by `Offset`, all in the actor's axes (+X its matrix's column 0,
  +Z its front). The model box is the actor's own, live: `actor+0x3B8` (min x y z) and `+0x3C4` (max), its whole
  model's bounds (the same offsets in 1.0.0: the same instructions at `main+0x2C0296C`), unless `ForceEditModelArea`
  gives `ModelMin / ModelMax`; one inside out falls back to those (0 by default). It isn't its physics shape: a
  broad shopkeeper's is x -1.62..1.63, y 0..2.24, z -0.31..0.47 where his standing capsule is 0.8 m round. No size is
  added. Once held, each side grows by its margin: `MarginMin / Max` with `ForceEditMargin`, else
  `AutoMarginLockOn` (1.0) for a lock-on client and `AutoMarginDefault` (0.1) for the rest (the check object's `+8`
  is 1 for the lock-on one; its margin vectors from `+0xB0`, 0x18 apart). The height band gets 1 or 1.5 m more for
  some riders / Link states (`[x0+0x10]`, `[[actor+0x270]+0x160]+0x79`): not followed here.
- **AreaFan** `AttentionFanRadius / Angle / Top / Bottom` plus `Margin*` (more once acquired) and `Model*` (unused unless
  `ForceEditModeArea`); `IsAreaExpandFromPlayer`: the fan is Link's (the actor has to be in front of him). Most NPCs'
  Talk: 2 m, 60°, 2 m over / 3 m under, 0.5 margin; their LockOn Talk: 3.5 m, 0.75 / 1.75. A sign (Obj_SignBoard):
  Link's fan 1 m AND the sign's own fan 1 m, 180° (its front half), so the angle is the whole fan's.
- **AreaFan (the check: `main+0x2C54430`):** reach = `AttentionFanRadius` + the model box's widest half (the
  largest of its x and z bounds, either way; 0.01 m with none; 0 with the check's `+0x39` set, IsIgnoreClientSize)
  + some per-state extra Link's components give (0 seen), + the margin once held, times a scale (1 for Link on
  foot). Angle: `atan2` of the actor in the fan's frame against `AttentionFanAngle` × pi / 180, so the whole
  angle either way; skipped while a lock-on holds with IsAngleCheckIgnoreWhileLockOn. Its margin as the box's
  (`MarginFanRadius` with ForceEditMargin, else AutoMarginLockOn / Default).
- **The survey:** 615 actors have clients with an action: Talk 688 clients, Check 211, OpenDoor 85, Lift 76, Grab 74,
  OpenShutter 37, OpenTreasureBox 13 (tools/interactions.py -> data/interactions.json, several minutes over every pack).
- **Measured in Gerudo's Fortress** (Npc_Gerudo_FortressGuard, Link placed facing her, the A prompt read off
  screenshots): Talk came up at 2.32 and 2.38 m, not at 2.42 m (the file's 2.0); with ZL held, Talk came up at 3.0 m
  (the LockOn client); walking backwards with ZL held it stayed at 3.72, 3.92, 4.10, 4.29 m and broke by 4.47 m (the
  file's 3.5 + MarginFanRadius 0.5). Both are about 0.4 m past the file: her model box's widest half (read then as
  her standing capsule's 0.4 m, which came out the same). So the reach is to her edge, the margin holds once taken
  up, and a lock-on's angle isn't checked then
  (IsAngleCheckIgnoreWhileLockOn). The remote `status` line's lock stayed 3 throughout: it isn't this lock.
- **What A acts on now (found in game, 1.1.1):** Link's components `[link+0x270]` hold at `+0x578` an object whose
  `+0x1A70` points to the do-action target's record: the actor at `+8` (again at `+0x98`, `+0x128`), set while the A
  prompt shows and cleared when it goes. Its `+0x1A60` points to the client ZL would take for an action: a lock-on
  client's record (vtable `main+0x4B29BC0`, the actor at `+8`, its checks at `+0x60` (count `+0x68`): for the
  shopkeeper Screen, Swim, Fan, Box, Line, Terror, in the file's order), ZL held or not; with none it points back at
  `+0x1A70` (3.4 m from a townsperson, facing him: `+0x1A60`; 2.4 m: `+0x1A70`). Found with remote's `findref`, comparing
  prompt and no prompt. Not the sender's reservation (`[[actor+0x270]+0x160]+0xAC`, its reserver handle `+0xB8`,
  SelectorAttentionSenderIsPlayerReservedDoAction `main+0x749340`): that's set when A is pressed. 1.0.0 not checked.
- **The fan's angle is either way of Link's facing:** the prompt stayed up 54 deg off (AttentionFanAngle 60).
- **Measured round Npc_Hylia_022_Shop_Child** (Treasure Chest Shop, facing world +X, his matrix's +X world -Z; Link
  placed by the remote, `+0x1A60` read through the GDB stub, binary search along rays, each try from far away): ZL
  took his lock-on Talk out to 4.48 m in front (box: 0.47 + AttentionMax 1 + Offset 3 = 4.47) and 1.61 m to his
  +Z side (box: -1.62 - AttentionMin 0), straight edges both (the same at 2, 3 and 4 m out); the -Z side is a wall
  at 1.6 m (box: 2.63). Once taken it held to 2.6 m on the +Z side, not 2.7 (1.62 + AutoMarginLockOn 1.0). His fan
  (3.5 + 1.63) never limited it. The near side is his counter. The camera matters: with it turned to face Link
  (the shopkeeper off screen) nothing was taken anywhere: the client's Screen check. Coming from inside, the margin
  makes every edge look further out: measure from outside.
- **Not checked:** spheres and cylinder fans (taken like fans: the model box's widest half added); the per-age
  overrides (`CheckOverrideParam`: AttentionFanRadiusChild / Adult...); the runtime's copy of the clients (the
  drawing goes by the files, by actor name, with the model box read live); a teleport's effect on held attention (placing Link at
  2.8 m after taking it up at 1.8 m showed no prompt).

### 9.38 When a swinging door closes behind Link (1.1.1; drawn in game, the market's shop doors, 26th November 2026)
- The doors (Obj_Door_*, Obj_Kakariko_Door_*, ...: 39 actors, by AIInfo / event AI) run `AI/Door.root`,
  `Event/FieldDoor.event.root` or `PrivateDoorForNpc_Single.root` (AI.Global pack), all three through
  `_LocalModule_e4e2d47532fa`: `SelectorDoorCanClose` with `DoorRadius` 2.0 and `CloseWaitFrame` 135 (its props; the
  same everywhere), fed the door's perceive list (sense 0: every emitter it perceives).
- **The check** (`main+0x5E58A0`, the node's calc): for each perceived emitter, skip it if its distance squared (`+4`
  of the perceive data) is over `DoorRadius` squared, or (with flags in `[emitter+0x1E2] & 0x3FFC`) by affiliation;
  else, unless LogicOpen, the dot of (its position - the door's `+0x31C`) with the door's X axis (rotation column 0,
  `+0x328 / +0x334 / +0x340`) must be below 0 for it to count. Anyone counting holds the door open; with nobody, it
  counts `CloseWaitFrame` frames and closes.
- **The countdown** is the node's own f32 at `node+0x20` (1.1.1 `main+0x5E5A14` on): while anyone counts (and on its
  first run) it's set to CloseWaitFrame (read through the input pointer `node+0x30`), as is `+0x24`, with `+0x28` set
  to -1 (`main+0x5E5F50`, 1.0.0 `main+0x593A34`: the same instructions); with nobody, while it's over 0 it's counted
  down (`main+0x5ED3D0`), and at 0 the door closes (`main+0x5E5B38`). DoorRadius comes through `node+0x40`.
- **Where the nodes are:** two per door. The door's own root AI (RestrictedDoor_Single_InOutSeparateCondition.root,
  in the actor's pack) has the module (node 23, CloseWaitFrame 135, IsForceClose from QueryLogicGetBool
  "ForceClose"), made the first time the door opens and kept (a unit of its AIComponent controller then: unit 6 of
  11 for Obj_Door_ShopTreasureBox); it runs when the door was opened for an NPC. When Link opens it, his walk-through
  event (FieldDoor.event) runs its own, made for the event; nothing of the door's leads to it. So both are found by
  class: vtable `main+0x4AFBCC8` (1.1.1; 1.0.0 `main+0x4A96CF8`, calc `main+0x593380`), the calc at slot 39 (28 of
  the first 48 slots shared with the other AINB node classes); `[node+8]` its unit, the unit's `+8` its controller,
  whose `+0x1F0` is the door. interactview.c searches the heap for them, 6 MiB a frame, while a door is watched; seen
  to find the door's own in game.
- **What opens it** (the root's module `_LocalModule_49dbd032a361`, DoActionType 4): SelectorAttentionSenderIsPlayer
  ReservedDoAction (`main+0x749340`) on the door's attention sender `[[door+0x270]+0x160]`: reserved when its `+0xAC`
  is 1 and its `+0xB0` is `0x9AB0000` + the action (OpenDoor 4); the reserver through the handle at `+0xB8`
  ({`+8` the actor-table entry (`[entry+0x40]` the actor, its generation at `+0x48`), `+0x10` the generation}).
  PlayerReserved (the reserver has a player component, `[[actor+0x270]+0x358]`) opens it with Link's event;
  NonPlayerReserved opens it unless a logic bool says no; NotReserved runs the close check. The door's own entry is
  among its fields (`door+0x1B0` for Obj_Door_ShopTreasureBox), so **open door** reserves the sender with the door
  itself for 20 frames, then puts it back (not yet seen in game).
- **Whether it's open:** the door's AI sets the actor blackboard bool `IsOpenDoor` as it starts opening (OpenStart) and
  clears it as it starts shutting (CloseStart). The actor blackboard is `[[actor+0x270]+0]+0x10`: bool names (C
  strings) at `+0x190`, count `+0x188`; values a byte each at `+0x1A0` (count `+0x198`); flags `+0x1E0`, change counts
  `+0x1F0` (ExecuteBlackboardSetBool `main+0x7AC2E0`, vtable `main+0x4982148`: type 1 resolves the name there once and
  keeps the byte's address at `node+0x30`). Obj_Door_ShopTreasureBox has two: IsOpenDoor, IsOpenToInside. Read 1
  with the door open (remote `peek`), but it stays 1 after a door Link went through has shut: his event shuts it, and
  the door's own AI (whose close module would clear it) never ran (its count's node still all 0). So **door open**
  reads the pose instead: the door model's one bone, Root, which Obj_Base_Door_A's Door_Push_* / Door_Close_* swing.
- **Where they sit:** a unit's node list (`unit+0x28/+0x30`) leaves out its modules' nodes, but they're made just after
  their unit: the shop door's two SelectorDoorCanClose nodes (two per close module) at its unit + 0xAE0 / + 0xD00, in
  both its own controller (unit 6) and its event's (`[[comps+0x88]+0x50]`, unit 2). So the door's controllers' units
  are each followed by 0x4000 bytes looked through for the class: all four found at once, no heap search needed.
- **Link's event's close node:** under the event performer's controller (`[[comps+0x88]+0x50]`, +0x1F0 the door, 11
  units / 179 nodes at the event's start), its close module made partway through the event; the heap search (6 MiB a
  frame) was too slow to catch it, so the door's controllers' nodes are looked through every 6 frames instead.
- **Shutting at once:** the calc reads DoorRadius through `node+0x40`; pointing that at a 0 means nobody counts as
  near, and a count of 0 shuts it. Seen in game (26th November 2026): with all 4 of the shop door's nodes known it
  shut from 86-100 deg each time; with only the door's own (idle) node known it didn't. So the shut now looks through
  the door's controllers at once and every frame while it lasts, and keeps every node's count at 0 with nobody near
  until the door's pose reads shut (under 1 deg) or 90 frames pass.
- So: a half disc of 2 m on the door's -X side (where it swings to), drawn as **door close** (interactview.c). Not
  checked in game: walking out of it and timing the close.


### 9.39 Hitboxes, bump boxes and invincibility (1.1.1; drawn in game, 27th November 2026; 1.0.0 not checked)
- **The two Havok worlds** (Havok 2025.2, hknp): cct.c's path (`[[[[phive_mgr]+0xC0]+0xC8 + 8 w]+0xE0]`) gives
  world 0, the solid bodies ("entities"), and world 1, the sensors. Bodies are 0xC0 bytes, 42 to a block, the blocks'
  pointers at `[world+0x48]`, the capacity at `+0x130` (6144 and 10240 in Hyrule Castle's grounds, about 500 and
  600 in use).
- **A body:** its transform at `+0x00` (three columns of four floats: world = c0 x + c1 y + c2 z + the position at
  `+0x30`); flags `+0x50`; id `+0x58`; `+0x5C` a collision filter word that isn't phive's layer; its bounding box from
  `+0x60` (max x y z, then min z x y; FLT_MAX while it isn't in the world); its shape `+0x78`; motion id `+0x80`;
  phive's own object for it (the "owner", cct.c's controller body for Link) `+0xB8`.
- **The owner:** `[[owner+0x108]+8]` its actor (none for some: one of Link's AttackAll sensors, a fire); `+0x120` its
  layer hit mask (bit n: layer n of PhiveConfig's LayerEntityCollection, or LayerSensorCollection for a sensor; the
  file's EnableLayerHitMask, e.g. HitToPlayer `0x80004` on a Deku Baba's AtkHead, 0x7FFFFFF Default where the file
  gives none); `[[owner+0x150]+8]` its own layer, the low five bits an index in the same collection (the top bits
  are flags: 0x84 for an enemy's Character bodies), `[[owner+0x150]+0xC]` a number the same on all of an actor's
  bodies (Link's 0x20), even where the actor isn't at +0x108. Checked over every body in a capture: Link's character
  controller Player (3), his sword PlayerAttack (7), NPCs' and enemies' bodies Character (4), their targets Enemy /
  NPC, a Deku Baba's head and a Deku Scrub's nut EnemyAttack (8), chemistry ChemicalObject / ChemicalElement, areas
  GeneralArea. The owner's class: 1.1.1 `main+0x4C34658` (CharacterMatterRigidBody, cct.c's `vt_cct_body`: every
  character's controller), `+0x4C378C0` (other entities, ragdoll parts), `+0x4C37D20` (sensors).
- **A shape** (hknpConvexShape and kin): its size `+0x08`; (offset, count) pairs, each offset from the pair itself:
  points `+0x38` (12 bytes each), planes `+0x40`, faces `+0x48` (4 bytes: first index u16, count u8, an angle u8),
  face point indices `+0x50` (u8); its convex radius `+0x68`. One point: a sphere round it; two: a capsule (a Deku
  Scrub's TgtBody: (0, 0.5, 0), (0, 0.2, 0), 0.6, as its ShapeParam says); more: boxes (vtable `main+0x48936E8`, 8
  points, 6 faces), cylinders as polytopes (`+0x4893780`, 32 points, 18 faces), hulls (`+0x4893AE8`, Link's
  controller prism). Every decoded face is planar. Meshes and compounds (scenery) have other layouts.
- **Switched on, and collected:** a body can stay in the world switched off: phive's set-enabled (`main+0x15B800`,
  what OneShotSensorHitAtSensorOff ends in through `main+0x8D56F0`) keeps it in bit 6 (0x40) of `[owner+0x70]`, set
  for off (a request in flight sits in `[owner+0x60]`: `+0x84` the state, `+0x100` bit 2 pending). A Deku Baba's
  AtkHead read off between bites (`0x...960`). A sensor's contact collection, its controller unit's
  ContactCollectionName ("Atk", "Tgt"), is at `[owner+0x158]`; the castle grounds' wagon horse (Animal_Horse_Wagon,
  its layout Hash's low half `0x4E501C67` is the owner's `[[+0x150]+0xC]`) has an AttackAll capsule 1 m round, switched
  on, mask Default, and none: Link stood in it unhurt. One of Link's own AttackAll sensors has none either. So the
  hit view, like gz's of the game's AT / AC / OC lists, draws only bodies switched on and sensors with a collection.
  Where an attack's power is kept (ExecuteSensorHitSetupAttack, `main+0x8D28C0` → `main+0x8D29C0`: into slot
  objects of the collection's, `+0x80` on, fields `+0x190` to `+0x1A4`, through `main+0x2EA53D0`) isn't read.
- **A bomb's blast** isn't an attack sensor: Item_Bomb has none (only Object, ChemicalObject and PerceptionEmitter
  sensors). Its explosion is its chemistry's ExplodeBlast element (BombFruit_ExplodeBlast, ElementType 3011306755,
  Size 4.3), which the game puts in the sensor world as a sphere of that radius on the ChemicalElement layer (12),
  the bomb its actor, switched on, with a contact collection, for one frame (seen held in Link's hands, 27th November
  2026: the frame before the fireball). The lit fuse is a much smaller element on the same bomb. 14 actors' packs have
  an ExplodeBlast (Item_Bomb, Item_Bombchu, Obj_BombFlower_A_01 / A_04 and the Death Mountain one, Enemy_Dodongo
  (4.5), Enemy_BabyDodongo, the Dodongo boss's, Enemy_Beamos (and For_High_Place), Enemy_Amos (and AlwaysRespawn),
  Enemy_MiddleBoss_FlareDancer, Npc_Hylia_ImperialSoldier_Bomb); none smaller than 4.3. Nothing a pointer from the
  sensor's owner, contact collection or actor holds the element's type, so the module goes by the actor's name and
  a radius of 2 m or more. Other elements (water, torches' fire, light) are left out. The remote `button <0..3>
  [item]` (pouch.c) reads or sets an item button: 2 is ZR (`SlingShot`, `Hookshot`, `Bomb` by name).
- **An actor put away keeps its bodies:** a bombchu that has gone off isn't deleted: its actor goes to state 6 (as an
  enemy that hasn't appeared yet) and its character controller stays in the world, switched on, where it fell (seen
  27th November 2026, 400 frames on). So bump boxes and hurtboxes count only for actors in state 4 or 5; attacks
  aren't held to it (a blast's last frame; switched off ones aren't drawn anyway).
- **Link's items** (27th November 2026, used from ZR): each a PlayerAttack sensor that the game switches on while it
  can hit: a Deku Nut's 4 m flash (from the frame it bursts), Din's Fire (ExpandFireField, its own sensor, and a fire
  element), the boomerang for its flight (switched off when caught), a slingshot seed in flight (a 5 to 10 cm
  capsule). The nut's and Din's Fire's have no contact collection, so Link's own aren't held to it. Spent ones are
  put away (state 6: nuts, Din's Fire, the boomerang's) with the sensor still on, so attacks too count only for
  running actors (not a bomb's blast). The boomerang's AttackAll sensor stays on in Link's hand: only PlayerAttack
  counts for his things. Arrows (Arrow_Atk_0, PlayerAttack) not tried (adult).
- **What's an attack, a bump:** the files' sensors (tools: every RigidBodySensorParam) put an enemy's attack on
  EnemyAttack (226 of them with no EnableLayerHitMask), NPCAttack (moving spikes) or AttackAll (cuccos, Epona,
  falling rocks, crazy floor tiles); its mask has Player (bit 2) while it can hit Link. A character controller's
  mask (`0x3FBEFF`) hits Player; ragdoll parts have 0 or ground-only masks (`0x298365`).
- **Invincibility:** the Life component `L = [[actor+0x270]+0x2B8]` has two queues of reasons (u32 name hashes,
  rings: array `+0`, capacity `+8`, first `+0xC`, count `+0x10`) at `L+0x1C28` and `L+0x1C60`: either count over 0 is
  invincible (QueryIsInvincibleAny, `main+0x2A32460`: `[L+0x1C70] > 0 || [L+0x1C38] > 0`). They're LifeParam's
  InitInvincibilityType (an enum: None, `0x1A1CFA7E` into the first ring, `0xBA2AC343` into the second, the "no
  damage reaction" one, QueryLifeIsInvincibleNoDamageReaction), pushed by the component's setup (`main+0x8D9310`)
  with the reason `0xE89F6A7C`; AI nodes add and remove others. `L+0x1CA0` sums it up (0, 1, 2) with life
  (`+0x1CA8`) and max (`+0x1CAC`). The pair of reads appears 9 times in each build (the same offsets on 1.0.0), and
  1.0.0 pushes the same reason (`main+0x50E214`). A third queue, timed reservations (0x14 bytes each), is at `L+0x60` (count `+0x70`,
  ExecuteLifeReserveInvincibility, `main+0x2E47B40`): not read.
- **Link's invincibility after a hit** isn't in those: `[L+0x230]` holds timers (set through `main+0x2F7E270`, as
  Farore's Wind's and a revival fairy's are), four slots of 16 bytes from `+0x50`, the first float the time left:
  30 when a Deku Baba bit him, down to 0 in about 16 frames (the second float is last frame's, and keeps its last
  value). Sampled every 2 frames through two bites (his life 80 to 78, refilled by infinite health); the rings
  stayed empty.
- **The game's default life:** an actor whose LifeParam names no LifeParameters has `Default`'s MaxLife 100. Of the
  697 actors with a Life component, 425 have InitInvincibilityType `0x1A1CFA7E` (all NPCs, Deku Scrubs, Bongo Bongo's
  hands, signs, switches, trees); those are the "100/100" ones. Enemy health and the actor browser leave out an actor
  with no Death component, or whose start-up invincibility is still there.
- **The boomerang's sensors:** while Link holds it, only a TgtHitAll hurtbox (layer 6, his number). Thrown
  (a tap of ZR), it leaves his hand about 10 frames later and its PlayerAttack sensor (layer 7) comes on 2 frames
  after that: a 32-point convex shape, radius 0.05, more than 40 faces. 13 frames later an AttackAll sensor (layer 10)
  joins it, and all three change from Link's number (`0x1E`) to the boomerang's own. Caught, the PlayerAttack one goes
  off; the AttackAll one stays on in his hand (so Link's AttackAll sensors are left out).
- **A Lichii hang** (twice today, once on 25th November): about 2 s after an enemy was spawned (a Wolfos, then a Deku
  Baba), Lichii's GPU thread stopped ("Waited N s for the GPU thread to submit tick ..."), right after it logged
  "ASTC GPU decodes" and built a pipeline on the GPU thread; the module's game thread ran on until it stalled behind
  it. Other spawns of the same actors were fine.

### 9.40 Drawing under the game's HUD (1.1.1; checked in game, 27th November 2026; 1.0.0 not checked)
- **Where the HUD goes:** the game draws its HUD into a picture of its own, 1920x1080 (NVN format 0x3F), with the
  scene composited in first, and its last pass (its own command buffer and queue, just before the present) binds
  that picture and then the swapchain texture and scales it up (draws not seen through the C entry points: in
  blocks it calls). So whatever is drawn into the swapchain texture at present time is over the HUD. Read back
  (remote `texdump`): two 1920x1080 targets had the hearts and the map in them; the full-size RGBA8 one is part of
  the scene's lighting.
- **Found by clearing a magenta rectangle** (remote `hudtest`) into a target right after the game's
  nvnCommandBufferSetRenderTargets, in its own command buffer, at each bind in turn. Per presented frame the main
  command buffer binds the picture about 10 times: the 2nd right after a 192x192 R8 target, the rest one after
  another. Cleared at the 5th or 6th, the rectangle is under the hearts and items and isn't drawn over; at the 3rd,
  4th or 8th it's over them. A clear after the last pass's bind of the swapchain texture is covered by the scaled
  picture.
- **What the module does** (nvn.c): the HUD's picture is learnt from the last pass (the target it binds just before
  the swapchain texture); the world's lines and faces (air walls, hitboxes, guard sight...) are recorded at present
  time into a command buffer of ours aimed at it (gpuui's slots 2 to 4, our command memory after the panels'), and
  the game's command buffer calls it (nvnCommandBufferCallCommands) at the 4th bind of the picture in a row: the
  newest handle, kept until a newer one is ready (frames are recorded ahead of the present, so dropping it once
  called missed every other frame). A fence after each present guards the slot a frame called. 98% of frames get it;
  without a call (title, menus, before the picture is known) they're drawn at present time as before. Frame advance's
  held picture already has them when its frame did.
- **Why it's off (WORLD_UNDER_HUD 0):** the 4th bind in a row isn't a fixed point. Over a few hundred frames the runs
  of the picture's binds on a command buffer were 1 to 15 long (2: 1940 times, 6: 429, 9: 159, 10: 144...), so some
  frames called the overlays twice and some not at all (calls per present, 64 at a time: 60 to 65), and judging a
  present by whether its frame called them drew them again at present time after every gap (the game makes no frame
  for some presents, two for the next). The user saw it flash. A real marker for the HUD's start (the UI renderer's
  own command buffer or calls) would be needed.
- **The game's state after our block:** its HUD draws on correctly (its programs and pools set again; ours restore
  the texture and sampler pools).
- **Hooks:** wrapping the game's draw calls, EndRecording / SubmitCommands and CallCommands / CopyCommands for a full
  frame trace crashed the game about a minute in, three times (an abort in nn::fs::CloseFile from its "movie"
  module); wrapping only nvnCommandBufferSetRenderTargets ran four minutes clean. The trace is off unless built with
  FRAMETRACE_HOOKS.

### 9.41 The world stopped while the game draws: the pause manager's masks (1.1.1; checked in game, 27th November 2026; 1.0.0 not checked)
- **What the game's systems read** is the pause manager's `+0x30` (§9.1): three words, the OR of the masks
  (`+0x0C`, `+0x10`, `+0x14`) of the types whose count is above 0, worked out once a frame after the module's frame
  hook. So a count raised by the module took effect a frame late (raised while frame advance held the game, the next
  frame still played); working the three words out at once as well (pausemgr.c) makes it the very next frame, both
  ways. Checked with a boomerang in flight: held, raised, 30 frames drawn, lowered, held: the same position, and one
  step moved it one frame's worth.
- **Masks** (1.1.1): `GamePause` `7f3a9f14 2c9022af 0001269f`, `DebugPause` `7c2f4ab0 a698251f 0001e28d`,
  `ClickPause` `782f4820 a698252f 0001e28f`. Tried by giving `DebugPause` other masks (remote `pausetype <type> masks`)
  and raising it, the free camera flown, the actors within 30 m compared before and after:
  - `DebugPause`'s own: everything stopped (Navi too), but so is the game's camera: its view isn't built at all (the
    render camera's matrix function, §9.30, isn't called), so the free camera's doesn't show.
  - `GamePause`'s: everything stopped and the view goes on, but Link isn't drawn. The third word's `0x10` is what
    hides him (and, alone, what stops him).
  - `DebugPause`'s with the third word's `0x2` (or `0x400`) as well: everything stopped, Link drawn, lighting as
    ever. The game still doesn't build its view, but the free camera builds it itself where the game's call would
    (the camera's own `+0x08`, as its matrix function's out), so its view shows. Used for frame advance's camera
    tools (ootgz.c, `pausemgr_freeze`).
  - The shared bits alone (`DebugPause` and `GamePause`'s AND) don't stop Link's pose; `GamePause`'s without its two
    `0x10`s leave the actors running. With the second word's `DebugPause`-only bits added to that, everything
    stopped but the lighting went white (its exposure).
- **What isn't drawn while stopped:** an actor off the screen when the world stopped (Malon and the Gossip Stone at
  Hyrule Castle, seen from the free camera) isn't drawn until it runs again, with every mask that stops it; one on the
  screen stays drawn. Its visibility is evidently worked out in its own update.
- **The game's camera** doesn't run while stopped, so its view isn't built again when the free camera goes off:
  freecam.c keeps the game's eye, look-at and up (as its camera last set them) and puts them back and builds the view
  itself.

### 9.42 Guarding with a shield (1.1.1; checked in game, 27th November 2026; 1.0.0 not checked)
- **No shape decides it.** Each shield has a target box (`Tgt`, 0.8 × 0.8 × 0.2 m, on the sensor Object layer, its
  hit mask CustomReceiver alone) and a ChemicalObject sphere (0.3 m), both on whether Link guards or not; neither can
  touch an enemy's attack. What decides it is Link's `Life/DamageParameters/Player`: `GuardableAngleMaxDeg` 120
  (also `...ForToggle`), `BackwardAttackAngleMinDeg` 150, `IsUseGuardableAngleForToggle` false,
  `BackwardAtkAngleMinDegForToggle` 90; `SensorHitParam` `GuardBasePosZOffset` 0; `PlayerGameParam`
  `MoveShieldLimitAngleLeft/Right/Up/Down` 50 (the shield turns towards the attacker that far).
- **Measured** with a Deku Baba (Kakariko, adult, ZL held: guarding without a target is Link's main action 65,
  a blocked hit 142, a hit taken 134 with 16 frames of invincibility), Link placed 1.5 m from it at a bearing, the
  angle taken from his facing on the frame before the reaction: blocked at 1–83° and at −110°, −76°; hit at +115°,
  +131°, +136°, +139°. So about 110° each way from his front: 60° round the shield (120 in all) with the shield
  turned up to 50°, as the parameters read. Deku and Hylian alike; the Mirror Shield blocked in front and was hit at
  +130° (no bite landed between 70° and 110° with it). Lichii hung twice after the Baba was spawned (the known
  spawn hang). Guarding turns Link to the camera's direction
  within ten frames, and often towards the Baba, so most placements ended with it in front.
- **Shields differ** only in `ShieldParam.BadConditionAttackGuard` (attack conditions they can block: the Deku
  Shield has two false that the Hylian and Mirror have true, likely fire) and the Mirror Shield's light reflection
  (`LightReflect` chemical element, `MirrorShieldParam` beam absorbing) and Link's `ReflectShield` body (a 0.3 m
  capsule on his shield hand, entity layer Player, sub-layer mask HitOnlyBeam: beams only).
- **Not flat.** The guard direction is a 3D vector (`GuardDir`, `GuardDirection` in main) and the shield turns up
  and down as far as left and right (`MoveShieldLimitAngleUp/Down` 50), so what's guarded is every direction
  within about 60 + 50 = 110° of Link's front: a sphere with a 70° cone cut out behind. Above and below weren't
  measured (the Baba bites level); the check's code wasn't found (the parameter's loader is `main+0x2F6A360`, the
  angle at `+0x188` of its block, the toggle flag at `+0x1A5`; no function reading both was found).
- Drawn by hitview.c (**shield guard angle**) while Link's action is 65 or 142: that sphere (1.3 m, only for
  seeing it) at the shield's height, its back cone and rim.

### 9.43 When an actor is guarding, and enemies' guards (1.1.1; checked in game, 27th November 2026; 1.0.0 not checked)
- **The Guard hold event.** `QueryASGetHoldEventGuard` (`main+0x2984F80`, Dark Link's AI uses it) reads the actor's
  animation: `[[actor+0x270]+0x40]` the AS component, `+0x18`, `+0x18` its slots (count `+0x18`, array `+0x20`,
  0x48 bytes each), each slot's `[+0x58]` hold events, bit `0x10` of their `+0x40` the Guard event. Seen on for
  Link while he guards (slot 1) and off otherwise; on for a Stalfos in 112 of 210 frames while Link swung at it,
  which took no damage then (it took hits when it wasn't).
- **Not the BSA's request.** `ExecuteBSASetIsRequestedGuard` sets bit `0x20` of `[[[actor+0x270]+0x188]+0x24]` (the
  BSA component, `main+0x974EB0`); a Stalfos guarding never had it.
- **Enemies' angle:** every enemy pack that has DamageParameters (145) has `GuardableAngleMaxDeg` 100 (the
  default), Link 120; enemies have no MoveShield parameters. A Stalfos guarding with Link placed beside or behind it
  was square to him again (within 2°) before his sword was out, ten frames on, so side blocks couldn't be measured;
  drawn as 50° each way from its front. The Stalfos's own `GuardMaxAngle` 105 and `QuickGuardProbability` 0.5
  (StalfosParam) are its AI's. Its `AntiOverlapShield` (a Character body on its Root bone, HitOnlyPlayer) is a bump
  box. Dark Link: `GuardableAngleMaxDeg` 100 as well, Link's `Player_ReflectShield` capsule and a shield actor of its
  own (`Weapon_Shield_DarkLinkShield`); not seen in game.
- Remote `guardbits <actor>` (debugging): those bits, its facing (from its rotation at `+0x328`) and life.

### 9.44 Collectables' pickup sensors (1.1.1; checked in game, 27th November 2026; 1.0.0 not checked)
- A drop (a spawned `Obj_Rupee_Green`, `Obj_Rupee_Red`, `Obj_Heart`, `Obj_MagicJar_Small`) has a body in the entity
  world (SmallObject layer, a 0.1 m capsule or a 0.2 m sphere for the heart, its hit mask without Player: it lands
  but Link doesn't bump it) and a sensor on the sensor **Object** layer (0), its own shape (32 points), hit mask
  all, with a contact collection: what Link's body sensors touch when he takes it. Link's `ForArea` sensor (Player
  layer, a 0.4 m capsule 0.4 to 1.5 m up) has the sub-layer mask HitToArea: areas only, not pickups.
- Pots and other breakables have Object sensors too, so hitview.c tells collectables by name: `Obj_Rupee_`,
  `Obj_RupeeToken`, `Obj_Heart`, `Obj_MagicJar`, `Obj_ArrowBundle`, `Obj_DekuNut/Seed/Stick`,
  `Obj_ScrubBusinessSeed`, `Obj_Fairy`, `Obj_Bomb`, `Obj_Bombchu`, and the animals `Animal_Insect`,
  `Animal_Kokiri_Insect`, `Animal_Fish`, `Animal_Butterfly`, `Insect_` (any of their sensors on the Object,
  SmallObject or target layers). Bugs and fish weren't near to check.
- Spawned drops in Hyrule Field sometimes fell through the ground (a heart and a magic jar ended 13 to 17 m below).

### 9.45 Bodies taken out of the world (1.1.1; checked in game, 27th November 2026; 1.0.0 not checked)
- After a save loads, a set of Link's bodies stays where he was placed: an entity capsule (0.35 m), a hurt capsule
  (0.4 m), attack sensors and more, the Player actor's, switched on (`[owner+0x70]` bit 6 clear), contacts set, and
  never moved again. hknp keeps them in its body arrays; the hit view drew the hurt capsule as a blue box at the
  spawn point. A Gossip Stone's spare bodies, one of Ingo's, one of Navi's and one of the Deku Shield's are the same.
- What they share is `[owner+0x70]` bits 25 and 41. Phive's removal (`main+0x4D8F80`) clears bits 15 and 10 and sets
  25 and 41; its add (`main+0x4DAB20`) sets 10, 32 and 25, and 15 unless switched off; the world's step (functions
  round `main+0x281D0`) clears 25 and 41 once it has dealt with the change. Bit 15 is only awake: Link's live
  hurtbox loses it standing still.
- The contact pass (`main+0x354EC0`, at `+0x354FA4`) drops a pair where either owner (`+0x110`, `+0x118` of the
  pair) has bit 41, through `main+0x3503F0`. So bit 41 is out of the world, whatever bit 6 says; the hit view treats
  it as switched off. Of 636 bodies in Hyrule Field, the 19 with bit 41 were all such spares.
- Collisions switched off the other way, ForceNoHit (`[owner+0x70]` bit 11, or `[[owner+0x60]+0xF8]` bit 0 while
  that request's `+0x100` bit 19 is set; see the levitation notes), leave a body in place too: a spawned Stalfos
  kept a 0.3 m Character capsule (state `0x300100001c80`, bit 6 clear) where it had been while it walked on, drawn
  white. Of the switched-on bodies in Hyrule Field only the camera's PerceptionEmitter and a removed spare of
  Ingo's had bit 11; the hit view leaves these out as well.

### 9.46 A placed actor's layout entry, and what chests hold (1.1.1; checked in game, 27th November 2026; 1.0.0 not checked)
- Chests are placed as `Obj_TBox_A_01` and the like (`_NoBake`, `_A_02` to `_A_06`, `Obj_TBox_GlassTruth_A_01`,
  `Obj_HrlCity_Shop_TBox_A_01/02`) but run under another name: Hyrule Field's four are `TBox_Field_Iron`. What one
  gives is its layout entry's `Dynamic.Drop__DropActor` (238 of the 248 chests in all layouts): rupees
  (`TBox_Rupee_Green` to `_Gold`), dungeon keys, maps, compasses, `Item_*` equipment, `Item_ScrubBusinessSeed` (20).
  Nine also have `Drop__CheckActor` and `Drop__DropActor_Substitute` (a Deku Shield chest gives rupees once Link
  has one); the treasure chest game's have `GetEventName`, `DropViewActor` and `IsEnableRepeatOpen`. Ten have none
  (Inside Ganon's Castle's four `Obj_TBox_A_04` among them). Pots, grass, rocks, crates, beehives and a few enemies
  carry `Drop__DropActor` too.
- The layout stays loaded: an actor's PreActor `[actor+0x3D8]` (whose `+0x88` is its instance id, the layout's
  Hash) has at `+0xB0` its Dynamic dictionary as the BYML file's start (u64, "YB" version 7), the node's offset in
  it (u32) and its type (u32, 0xC1), and at `+0xC0` its Phive the same. Read so, Hyrule Field's chests gave what the
  Static and Dynamic layout files say (`Item_ScrubBusinessSeed` on the castle wall, `TBox_Rupee_Red` twice).
  actors.c's `actor_layout_string` reads a string from it; the actor browser shows it.
- An actor keeps its pack's name at `actor+0x260` (`Obj_TBox_A_01`, `Obj_TBox_A_01_NoBake`) beside its ActorName at
  `+0x20` (`TBox_Field_Iron` for both: the pack's `ActorNameRef`). The two are the same for most actors; they differ
  for chests, a chest's rupee (`Obj_Rupee_Red` from pack `TBox_Rupee_Red`) and a second Ingo
  (`Npc_Hylia_Ingo_003_Adult`). Data keyed by pack (interactions.json) is looked up by `+0x260`: a field chest's
  OpenTreasureBox range (a 0.5 m fan round its model box, all the way round, and Link's 2 m fan) then draws, its
  A prompt showing at the disc's edge. Its only client isn't a lock-on one: a chest can't be Z-targeted.
- **What stops Link at a chest** isn't settled. Its bodies (spawned `Obj_TBox_A_01` on flat ground): `Body` (Object,
  8-point box, hit mask none), `CloseBody` (GroundObject, 16 points, mask with Player, bit 6 set yet solid), the
  lid's (Object, a compound) and `AirWall` (GroundObject, hits SmallObject only). Link dropped on it stands 0.65 m
  up; with CloseBody's ForceNoHit on, 0.57 m; with all four on, still 0.57 m. Something else holds him there.

### 9.47 Animals that drown and come back: their timers and where they come back (1.1.1; checked in game, 27th November 2026; 1.0.0's vtables found by their names, not run)
- **Who:** every actor pack was searched for a drowning module. Cuccos (all `Animal_Cucco*`) run
  `Kokko.action.interruptdrown.module` (AI.Global), dogs (`Animal_Dog_*`) `Animal_Dog.action.InterruptDrown.module`,
  horses (`Animal_Horse`, `Animal_Horse_For_Ingo`, `Animal_Epona_Child`, `Animal_Epona_Adult`)
  `Animal_Horse.action.interruptdrown.module`, which runs `HorseReturnToSafePositionWithEvent.module`. Their
  InterruptSeqParam has `IsDrownWhenInWater`, `DrownWaterDepth` 0.3 m for a cucco, 1.6 m for a dog or horse. Enemies
  with `IsDrownWhenInWater` drown and die: they don't come back.
- **Where the module runs:** the BSA action controller, `[[[comps+0x188]+0x1B0]+0x80]` (vtable `main+0x4B479E8`,
  `+0x1F0` the actor) for a cucco and Epona, not the AIComponent (`comps+0x48` is 0 for a cucco). It lists its
  definitions (count `+0x168`, array `+0x170`; the file at a definition's `+0x10`, its name the file's first string:
  `[file+0x24]` is the string pool, the name at its offset 0). Its units (count `+0x18`, array `+0x20`, 0x18 each)
  go with them one for one: a unit's `+0x10` is its definition. A module's nodes are made just after its unit
  (`[node+8]` the unit, the node's index in its file a u16 at `+0x1C`), as with the doors (9.38).
- **The counters:** `SelectorGenericFrameCounter` (vtable `0x4AFD250` [1.0.0 `0x4A98280`], calc slot 39
  `main+0x9A76D0`) adds the frame step to `+0x20` and finishes at the f32 its `+0x50` points to (low bits flags);
  `ExecuteGenericCounter` (vtable `0x499F200` [`0x493A8D8`], update slot 30 `main+0x5EEAA0`) counts `+0x20` down
  through `main+0x5ED3D0` (the doors' countdown) from its length at `+0x2C`. They count the game's own time: a horse's
  goes down about 2.2 a frame (frame advance, Lake Hylia), a cucco's about 1 (its AI runs every other frame). They
  keep their last values once done.
- **A cucco** in water deeper than 0.3 m: node 35 (30) in the water, 125 (up to 120) turning to the land it means
  to jump for, then up to `DrownJumpCountMax` (3) jumps (node 77 up to 600 each till it lands, 119 its 10 in the
  air); landed on the ground, it's out. Still in the water, node 14 (60) as it sinks and fades out, then it's put
  back and fades in (18, 10) and lands (31, 60). Seen in Zora's River: in the water 30, looking for land, one jump to
  the bank; and spawned in the river, sinking 60, put back, again and again.
- **Where a cucco comes back:** its blackboard Vec3f `WarpPos` (the actor blackboard `[[comps+0]+0x10]`: Vec3f count
  `+0x200`, names `+0x208`, values `+0x218`, 12 bytes each). `Kokko.utility.root` keeps it equal to `LandingPos`, the
  last place it landed on safely (it follows Link while he carries it). The drown module puts it on the navmesh
  within 2 m of WarpPos (raised 1 m) and drops it from above; with no WarpPos, at its home (`OneShotWarpToHomeMatrix`,
  or 10 m over it to fall). Seen: it came back at WarpPos's x and z exactly, dropping from 7 m up.
- **The home position** (`QueryActorGetHomePosition`, `main+0x29A6030` [`0x2953BE0`]): `actor+0x2EC`, unless
  `actor+0x519` bit 1, then `[[[comps+0x50]+0x20]+0x180]+0x10`. A dog comes back there, 60 frames (node 3) after it
  starts drowning.
- **A horse:** `HorseReturnToSafePositionWithEvent`: node 17 (`WaitFrameBeforeFadeOut`: AnimFrameForFadeOut 340 -
  FadeOutFrame 30 = 310) struggling, 18 (30) fading out, 19 (30) unseen, then it's put on its last landing if
  `QueryHorseCheckIfSafePosition` says it's safe, else `ExecuteHorseFindSafePos` looks within 200 m; then 14 (30)
  fading in. Its last landing (`QueryRidableGetLastLandingMtx`, `main+0x2AD1870`): the ridable `[comps+0x3A0]`,
  `[ridable+0x78]` whose slot 4 (`main+0x302E340`: `add x0, x0, #0xc; ret`) gives a 3 by 4 matrix at `+0xC`
  (translation `+0x18`, `+0x28`, `+0x38`); NaN in its first word while it has none. The same offsets in 1.0.0
  (`0x994AA0`, `0x2E41F10`). Seen: Epona spawned in Lake Hylia counted 358 down to 0, vanished and came back on her
  landing (the lake bed, 125.6 m), floated up and drowned again.
- **The fade out** (`OneShotStartModelAlphaFadeOut`, vtable `0x4A3ECF8` [`0x49DA280`], calc `main+0x294F290`
  [`0x28FCF40`]): on the model component `[comps+0x260]` (getter `main+0xC71C20` [`0xA7F650`]) it sets the step,
  `1 / FadeFrame`, at `+0x11C` and bits `0xC0` of `+0x128`. Read every frame through Epona's: the step went from
  0.167 to 0.033 (1/30, FadeOutFrame) as her count 17 ended, and back 14 frames later, as count 18 (30 counts) ended
  and she was hidden: the fade goes in the same counts as the counters (`+0x118` stayed 1.000: not the alpha). A
  cucco's FadeFrame is 6 (node 11), the start of its 60 of sinking. A dog's module has no fade.
- **What can't be done while it drowns:** `OneShotAttentionSenderChangeClientValid` (`main+0x88B4B0`, vtable
  `0x49F1968`) on the attention sender `[comps+0x160]`: a byte per DoActionType at `+0x3C` (a bit per who switched
  it off; 0 on), and the sender's clients of that type (count `+0x90`, array `+0x98`, 0x90 each, the type at `+0x34`)
  get bit 0 of their `+0x20` cleared. DoActionType: None, Talk, Awake, Lift (3), OpenDoor (4), OpenShutter,
  OpenTreasureBox, PickUp, Take, Sleep, Check, Buy, Ride (12), PullOut, Crawl, Grab, BringAlong, UseBeansHole, Catch,
  PutIn, OpenForEvent, Rematch, Touch, GrabSword, OpenChainLock, BlueFireTarget, FatalBlow, BlueFishTarget,
  DiggingHoleTour (28; main's names at `0x3EEDC90`). The cucco's drown module switches Lift off on entering and on
  again leaving; seen: `+0x3F` 1 all through a cucco's drowning in Lake Hylia (Talk and Check off too: it has
  neither). interactview.c leaves out a client whose action is off.
- So (drownview.c): the actor browser's **drown timer watches** read these counters (the one changing is the step it's
  at; what's left divided by how much it changes a frame): **drown timer** until the fade starts, **fade timer**,
  **respawn in** until it's put back. **respawn point** draws WarpPos, home or the last landing.

### 9.48 Held actors (Lifter / Liftable), ModelComponent alpha fade, and Cucco drown restore (1.1.1; 1.0.0 in brackets; disassembly verified)
- **Lifter & Liftable components:** Link's `PlayerLifterComponent` is `[[link+0x270]+0x2C8]` (vtable `main+0x4B4E6C8`,
  base `LifterComponent` `0x4B4C648`); a liftable actor's `LiftableComponent` is `[[actor+0x270]+0x2C0]`.
  - `lifter+0x2B0` (`u32`): state (`0` Nothing, `1` Setup, `2` BindWithLiftable, `3` OnlyLifting; `(s & ~1) == 2` is
    actively holding). `lifter+0x1A8`: 32-byte `ActorLink` holder whose inner 24-byte `ActorLink` starts at
    `lifter+0x1B0` (`entry` at `+0x1B8`, `gen` at `+0x1C0`, same layout as §9.19 handles).
  - `liftable+0x2C` (`u32`, mask `0x11C` bound states), `liftable+0xC0` (`ActorLink` back to Link), `liftable+0x276`
    bit 2 (liftable enabled).
  - Native lift functions (matched in both builds):
    - `ActorLink::set(link, actor, flags)` `main+0xBC960` [`0xE4CE0`] (init with vtable `event_req_sub_vt + 0x10`,
      `+0x10 = 0x1FFFFFFFF`; destructor `event_req_sub_dtor`).
    - `LifterComponent::drop(lifter, reason, s0, s1)` `main+0x2E50DB0` [`0x2DFC500`].
    - `LifterComponent::forceLift(lifter, link, &key, flag)` `main+0x2E50E40` [`0x2DFC590`]: checks `canLift`
      (`0x2E50570` — requires lifter state 0, target running state 4/5, non-null LiftableComponent; no distance check),
      jumps straight to state `3` (`OnlyLifting`), binds `lifter+0x1A8`, and sends bind message `0x7A890000` to target.
    - `player_lift_anim(link_actor, link)` `main+0x30185B0` [`0x2FC3780`]: updates Link's lift animation params for the
      held actor's `LiftableType`.
- **Skipping pickup animation (`ExecutePlayerLift` & `SelectorPlayerSubSchedulerMain`):**
  - `ActionVerb` string table starts at `main+0x4B4ABC8` (`0` = `None`): `PreLift` `0x2A` (42), `Lift` `0x2B` (43),
    `InterruptDrown` `0x9D` (157), `InterruptStruggle` `0x9E` (158), `HoldItem` `0xED` (237).
  - `SelectorPlayerSubSchedulerMain` (`main+0x2B435B0`, init `0x11EEBF0`): routes verb `0x2B` (`Lift`, weapon/sub
    slot 1 at `[bsa+0x1C8]`) to `Player.sub.scheduler.lift` / `Player.sub.action.lift` (`ExecutePlayerLift`), whereas
    `0xED` (`HoldItem`) routes to `Player.sub.action.holditem` (inventory items in right hand).
  - `BSAComponent::setActionVerb` returns `0` on success and `3` if `[slot+0x7C]` bit 1 (`0x02`) is already set
    ("request already submitted this frame"); clearing bit 1 before submitting overrides an earlier scheduler request
    on the same frame.
  - `ExecutePlayerLift::enter` (`main+0x27E4FF0` [`0x2793440`]): when `player_comp+0xE40` bit 1 (`#2`) is set, skips
    the pickup animation and enters state `3` (already holding, `0x27E58F0`).
- **Actor Blackboard vs AIComponent Blackboard & Cucco `InterruptStruggle`:**
  - BSA-only actors (Cuccos, Epona) have `comps+0x48 == NULL`; their Actor Blackboard is `[[comps+0x00]+0x10]` with the
    same typed table layout (`S32` `0x0A8/0x0B0`, `F32` `0x120/0x128`, `Bool` `0x198/0x1A0`, `Vec3f` `0x210/0x218`,
    `U32` `0x558/0x560`; name count/ptr at `count_off - 0x10` / `-0x08`).
  - `Kokko.action.interruptstruggle.module` runs while Link holds a Cucco (`QueryLiftableIsBindState`) and updates
    Actor Blackboard `WarpPos`, `LandingPos`, and `IsSafeLandedFromDrown` from `QueryLiftableGetLifterLandingInfo`
    whenever Link is on safe ground.
- **Cucco `InterruptDrown` unit internals & `ModelComponent` alpha fade:**
  - `AIControllerUnit`: `unit+0x60` (`u32`) node count, `unit+0x68` (`Node**`) array indexed by node index; `unit+0x80`
    (`u32`) bit count, `unit+0x88` (`u32*`) active-node bitmap (cleared by node leave `main+0x37ABB30`).
  - `SelectorGenericFrameCounter::calc_` (`main+0x9A76D0`): active while `(node+0x18 & 2)` and `*(u8*)(node+0x1E) ==
    *(u8*)(node+0x58)`, adding frame step to `*(float*)(node+0x20)`.
  - `Kokko.action.interruptdrown.module` keeps `LandingWaterCount` (S32, jump count `0..3`) and `IsForceWarp` (Bool;
    checked at node `123` to jump straight to fade-out + `CUCCO_SINK` node `14`) in the BSA action controller's own
    blackboard `[ctrl+0x1F8]` (`BlackboardType = 0`).
  - `ModelComponent` (`[comps+0x260]`) alpha fade update (`main+0x2E1EB10` [`0x2DCA4xx`]; identical offsets in 1.0.0):
    `+0x80` current alpha (`0..1`), `+0x84` previous alpha, `+0x11C` step (`1/FadeFrame`, default `1/6`), `+0x120`
    dirty flags (`0x2500` refreshes renderer), `+0x128` fade flags (bit 2 fade-in, bit 3 fade-out, bits 6–7 sync `+0x84`,
    bit 14 hide at 0), `+0x134` (`u8`) fade-out active, `[[comps+0x10]+0xB0]` (`u8`: bit 2 fading, bit 4 hidden, bit 5
    done).
- **Teleporting distant / LOD-paused actors (`PhysicsComponent::forceSetPosAndRot` `main+0x4C6630` [`0x665790`]):**
  - `forceSetPosAndRot` writes the new 3×4 matrix into `[phys+0x20]+0x1D0` (setting dirty bits `[phys+0x20]+0x228 |= 3`)
    and calls CCT `setPos`, but **does not write `actor+0x31C` (`A_POS`) or `actor+0x328` (`A_ROT`) directly**.
  - Actors with `LODParam` `UsePhysicsPause: True`, `CalcSkipRescueDistance: 50.0` (`NpcCommon`), and
    `IsPhysicsPauseDuringActorPause: True` (`Animal_Cucco_CatchCucco`) stay paused (`state = 5`) when far from Link
    (e.g. a Kakariko Cucco on the surface ~71 m from the underground grotto), so their physics-to-actor sync never runs
    if `A_POS` isn't also updated immediately. Writing `A_POS`/`A_ROT` alongside `forceSetPosAndRot` lets LOD wake the
    actor (`state 5 -> 4`) within ~2 frames so `L_ACTORS` and `L_HELD` work cleanly in grottos too.
