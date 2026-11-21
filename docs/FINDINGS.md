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

### 5.21 Locking Link's X, Y and Z (1.1.1 only)
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
- **Not verified:** `1.0.0` has no store offsets, so locks there only show a notice that it will not work.

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

      Bit 24 is set when the first hits 5 m or more away (or with no distance), or misses while the second hits
      (and then bit 25 too when that one is 8 m or more away). On map floor both hit close by. On out-of-bounds
      floor only the second hits, so the point stops following Link there. Out-of-bounds floor near valid
      ground (within 5 m) is expected to keep it following (not tested).
    - bit 20 (`main+0x5FB84C`), the same step's other branch: the first probe hits close by, but the cell's
      attribute 8 (`[S+0x108]` vfunc `+0xB0`, arg 8, returns value and valid bytes) is set, or vfunc `+0xB8`
      gives 1 or less. Seen holding the point out of bounds as young Link, where bit 24 wasn't set.
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
  P`+0x18B4`/`+0x18C8` is used while P`+0x1C34` bit 4. The void-out respawn (`main+0x2814620`) warps to the same
  point; `main+0x30170E0` reads it too.
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
  NoEscape areas (`StartPos_` anchors; 28 entries in RSDB `GameSafetySetting.Product.111`); not looked at further.

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
   - `phive_mgr_ptr`: `main+0x4C18258` (`1.0.0`), `main+0x4C7DF00` (`1.1.1`). (Kept separate from `voxel_mgr_ptr`
     so `voidmap.c` gating on 1.0.0 is unchanged.)
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
