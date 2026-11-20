# sloperinaGZ manual

Everything sloperinaGZ can do, menu by menu. New to it? Start with the [usage guide](USAGE.md).

## Contents

- [1 Introduction](#1-introduction)
  - [1.1 Controls](#11-controls)
  - [1.2 Reading the menu](#12-reading-the-menu)
  - [1.3 Shortcuts in the game](#13-shortcuts-in-the-game)
- [2 Menus](#2-menus)
  - [2.1 Link](#21-link)
  - [2.2 Warps](#22-warps)
  - [2.3 Scene](#23-scene)
  - [2.4 Memfiles](#24-memfiles)
  - [2.5 Cheats](#25-cheats)
  - [2.6 Inventory](#26-inventory)
  - [2.7 Equips](#27-equips)
  - [2.8 Watches](#28-watches)
  - [2.9 Save management](#29-save-management)
  - [2.10 Story](#210-story)
  - [2.11 Debug](#211-debug)
  - [2.12 Settings](#212-settings)
  - [2.13 About](#213-about)
- [3 On the screen](#3-on-the-screen)
- [4 About frame advance](#4-about-frame-advance)
- [5 Settings file](#5-settings-file)
- [6 Languages](#6-languages)
- [7 Known limits](#7-known-limits)

## 1 Introduction

sloperinaGZ is a practice menu for **The Legend of Zelda: Ocarina of Time (Switch 2)**, versions 1.0.0 and 1.1.1, modelled
on gz, the practice ROM for the N64 game. The game loads it itself, so it runs on a console (Atmosphère) and in
emulators alike. Open the menu with **R + right stick down**.

### 1.1 Controls

| Button | Does |
|---|---|
| **R + right stick down** | Open / close the menu |
| **D-pad up / down** | Move |
| **D-pad left / right** | Change the value, or pick a button in a row |
| **L** | Select / press |
| **L held + D-pad left / right** | Numbers: change by 10 |
| **R + D-pad left** or **B** | Back one menu (closes the menu at the top) |

While the menu is open the game doesn't see the controller. These are the defaults, as gz's: every one of them, and
every shortcut below, can be changed in **settings > rebind commands** ([2.12.4](#2124-rebind-commands)). The menu's hints and the
controls at its foot show the buttons as they're bound.

### 1.2 Reading the menu

- The **top line** shows where you are, e.g. `sloperinaGZ > inventory > collection`.
- The **bottom line** says what the buttons do for the selected item.
- **Grey lines** are headings or information; they can't be selected. Items under a heading are indented.
- `name >` opens another menu. `☑ name` is a checkbox. `name  < value >` is a switch. `name  < 3 > / 9` is a
  number (with its maximum).
- Some rows are **rows of buttons** (watches, the memory viewer): D-pad left / right picks a button, which turns
  blue, and **L** presses it.
- A **^** or **v** at the side means the list goes on above or below.
- Read-only information sits under **return**, after a blank line.

### 1.3 Shortcuts in the game

With the menu closed (the defaults; **settings > rebind commands** changes them):

| Shortcut | Does |
|---|---|
| **R + D-pad right** | Show / hide the watches |
| **R + A + D-pad left** | Start / stop the timer |
| **R + B + D-pad left** | Reset the timer |
| **R + B + D-pad right** | Reset the lag counter |
| **R + A + D-pad right** | Reset **jump height** to FLT_MAX (as after a load: the jumpslash then works at any height) |
| **R + D-pad up** | Save position: where Link is and which way he faces |
| **R + D-pad down** | Load position: put Link back there (same map only) |
| **L3** | Levitate on / off (if enabled, [2.12.2](#2122-levitate)) |
| **X / B** held | Levitate up / down; release to hover |
| **R3** held | Levitate: hold height even if X/B is held |
| **R3** | Ragdoll (as a bomb hit) when not levitating |
| **L** | Frame advance (if enabled, [2.12.3](#2123-frame-advance)) |
| **L + L3** (either order) | Frame advance: run again |

**reload map** and **end cutscene** can be given buttons too (**settings > rebind commands**, under **scene**); they
have none at first.

## 2 Menus

### 2.1 Link

| Item | What it does |
|---|---|
| **health** | Link's health in hearts and quarters. D-pad changes it a quarter heart at a time; **L** fills it. |
| **heart containers** | 1 to 20. Lowering it below your health lowers your health too. |
| **rupees** | Up to your wallet's size. **L** fills it. |
| **age** | **child** or **adult**: what the next **reload map** gives you. |
| **reload map** | Reloads the map you're in, at the age chosen above. Changing age loads the other age's version of the map and swaps sword and shield with the ones from the other age (adult-only tunics and boots come off a child). |

| **save position (R + d-up)** | Remembers where Link is, which way he faces and the map (gz's save position). The first save in an area only finds Link's position in memory ("Locating Link's starting position, please try again"; he's lifted a little while it's tested, as with levitate, and it's quicker while he's moving): press it again once it says it's found him. |
| **load position (R + d-down)** | Puts Link back there, facing the same way. Only in the same map. With nothing saved: "No position saved yet". |
| **ragdoll (R3)** | Knocks Link over and tumbles him backwards: the game's own reaction to a bomb, so he can't attack and doesn't sink while it lasts. He gets up by himself and it unticks then; it can't be cut short. When the game wouldn't take a hit (a map's arrival, an event) it says so and does nothing. On 1.1.1 the game caps the tumble's speed; 1.0 has no cap. |

Under **return**: Link's **position** (x, y, z), his **current age** and the **map**.

### 2.2 Warps

| Item | What it does |
|---|---|
| **places** | Every map, sorted into **overworld (child)**, **overworld (adult)**, **dungeons**, **boss & battle rooms** and **other**. Pick a map, then a spawn point; you go there straight away with the game's own fade. |
| **cancel warp** | Cancels a warp that's waiting (see below). |
| **arrival cutscenes** | Arrive with the spawn point's own arrival scene instead of the standard one. |

If the game ever refuses the instant warp, the warp waits for you instead: walk through any exit or loading zone
and you'll come out at the chosen place. Under **return**: the **current** map and the **warp** state.

### 2.3 Scene

| Item | What it does |
|---|---|
| **reload map** | Reloads the current map (at the age set in **link**). |
| **end cutscene** | Ends the cutscene (or dialogue, chest opening or other event) that's playing. A played-back cutscene is skipped the way the game's own skip does; any other event is cancelled the way the game cancels one. Flags the rest of the event would have set aren't set, so it may play again. |
| *collision* | |
| **environment** | See below. |
| **void out** | See [2.3.3](#233-void-out). |
| *save and load* | |
| **save position** | See [2.3.2](#232-save-position). |
| *enemies* | |
| **display enemy health** | A bar with **life/max** over each living enemy within 30 m of Link (red under a quarter); it goes when the enemy reaches 0. Enemies that haven't appeared yet (Wolfos still in the ground, Stalfos before they drop) have none, and none are drawn while the game is paused. In the game's style a bar fades out while its enemy is hidden behind walls or other scenery. |
| **display through walls** | Also show the bars of enemies hidden behind the scenery (the game's style). Indented under **display enemy health**. |

#### 2.3.1 Environment

**air walls**: the map's invisible walls, drawn as outlines over the game.

| Item | What it does |
|---|---|
| **show** | Draw the kinds ticked under it (its options, indented below it). **Red**: a wall Link is inside, whatever its kind. |
| **that push link back** | **Amber**: walls Link can't stand on, so he's pushed back off them (the ledge in front of the Forest Temple in Sacred Forest Meadow as a child). On by default. |
| **that stop link** | **Cyan**: invisible walls he can't go through (round the rocks in the Lost Woods). On by default. |
| **other kinds** | **Violet**: walls that don't touch Link on foot: those for enemies, NPCs, animals, Epona, the camera, bombs and the hookshot, and those that only stop him while he climbs. |
| **through link** | Also draw them where Link is in front of them (in the game's style they're hidden behind him). |

The two lines under the menu, below **return** and apart from its options, count the walls in this map and those near you, and say whether Link is inside one
(how deep, and which way the game will push him out). Outlines show through the scenery; in the game's style
Link stays in front of them.

#### 2.3.2 Save position

Where a save puts Link when it's loaded (1.1.1 only). A save doesn't keep where Link stands: it keeps a
restart point, which follows him while the game finds his spot safe and stays behind when it doesn't (in the
air, on a moving block or platform, in deep water, during some actions). The game keeps it in two slots and a
save takes **A** if it's set, else **B**. On loading, the game also searches round that point for a safe place,
up to 5 m away, so Link can still appear somewhere else.

| Item | What it does |
|---|---|
| **show slots** | Draw each slot that's set as a ring on the ground, an arrow the way Link will face and a pole. **Pink**: slot A, **green**: slot B. A cross on the pole marks the one a save would keep. |
| **where a load puts link** | **Yellow**: runs the game's own search round the slot a save would keep and draws its answer: a ring and a taller pole, and a line from the slot to it when the load would move Link. It tests the scene as it is now, so a moving block that would be back in its place after a load (or an enemy that would be gone) can make it differ. Link's own body counts in that search, so it's run while he stands on the slot (as he does on a load) and that answer is kept when he walks off; a slot he hasn't stood on is marked **approx.** until he does. Run as on foot, even on Epona. Indented under **show slots**; on by default. |
| **show slots**: dark green slot | Where Link stands, the game won't move the restart point (slot B) to him, so a save keeps the green slot B. Out of bounds that's ground more than about 5 m from ground the game counts as valid, or ground marked not to keep it. Putting the ocarina away does move it there once. Dark green and pale yellow show what you'd get then, as green and yellow show what you have now: slot B at Link's feet in **dark green** (ring, arrow, a lower pole) and, with **where a load puts link**, where a load would put him from it in **pale yellow** (ring, tall pole). The pale yellow marker is only drawn while slot A isn't set, since a save keeps A otherwise. The menu's **here** line says why the game won't keep the point, or that it does. Not shown where something else would hold the point anyway (jumping, a moving object, an event). |

The lines under the menu give each slot's x, y, z and the way Link will face (degrees), with a **\*** on
the one a save would keep, or **not set**, or **not found** (the title screen, loading, 1.0.0); and where a load
would put Link and how far that is from the slot, or **stays**.

#### 2.3.3 Void out

Where Link voids (1.1.1 only), drawn over the game from the game's own ground map. The void check doesn't use
the collision: each frame the game looks straight down from Link into a map of one-unit cells that mark
walkable ground, and keeps the nearest such cell it has found round Link. Link voids when the cell underneath
is a deadly one, or when nothing is underneath and that nearest cell is 8 units or more away to the side.

| Item | What it does |
|---|---|
| **show** | Draw the parts ticked under it (its options, indented below it). |
| **edge of the ground map** | **White**: where the mapped ground ends. Past the line the game finds nothing under Link. Not voiding by itself: see the fence. Off by default. |
| **deadly cells** | **Red**: columns over the cells that void Link at once. A column runs from its cell up to the next mapped ground over it, because the game looks down from any height: anywhere inside one voids. On by default. |
| **8 unit fence** | **Orange**: a ring 8 units round the ground cell the game is holding for Link now. With nothing underfoot, Link voids on leaving it; it turns **red** when that happens. The rule only measures sideways, so the real boundary has no top or bottom: 4 units of it are drawn at Link's height. A line runs from Link to the cell while Link is off the map. On by default. |
| **void areas** | **Purple**: the areas placed in the map that void whatever is inside them, **red** while Link is in one. The holes down into grottos are made the same way and are drawn too (thin discs at the hole). On by default. |
| **through the scenery** | Also draw all of it where the scenery is in front (otherwise it's hidden behind the scenery, in the game's style). |

The columns, the fence and the void areas have see-through sides. The line under the menu, below **return**,
says where Link stands for the game: **on the map**, **off the map** with how far the held cell is out of 8, or
**VOID** with the rule that fired. Each void from the ground map also goes in the log file with what caused it and
where (`void out: ...`).

Not drawn: floors the game tags as an abyss, which void on touch (the whole game has one, at the bottom of the
Shadow Temple's chasm), the ground map's own outer box (leaving it
by more than 5 units voids; it's named in the log when it happens), and a few dungeon rooms' own void scripts. The
cells round Link are read again every two seconds or when Link has moved 4 units, out to 24 units to the side, 48 units down
and 16 units up; where there are more lines than fit, the farthest are left out.

### 2.4 Memfiles

In-memory snapshots stored until the game is closed, capturing full GameData (via the game's own serializer), unsaved session flags, map, time of day, Link's position/facing/health/magic/gear, and the player camera + AI action state.

| Item | What it does |
|---|---|
| **slot** `< 1..5 >` | Picks which of the five memfile slots **save memfile**, **load memfile** and **import/export** use. |
| **save memfile** | Saves the current state into the selected slot (kept in memory only). |
| **load memfile** | Closes the menu and restores the selected slot's GameData, map, time, Link and camera. |
| **import/export** | Opens `sloperinaGZ/memfiles` on the SD card, one folder at a time: its path at the top, a row of buttons (left/right picks one, **L** presses it), a line, then the folder itself with its folders and `.szm` files under it. The buttons: **new folder** (asks for its name, made in this one); **export here** (asks for a name, the slot's map to start with, then writes the selected slot into this folder as `<name>.szm`; a name already used there is refused); **delete this folder** (not at the top: a screen says what's in it, with **no, keep it** first, then **yes, delete it** deletes the folder and every memfile and folder in it). **L** on the folder's own row goes up a folder (at `sloperinaGZ/memfiles`, as far up as it goes, it does nothing); **L** on a folder under it opens it; **L** on a file's name reads it into the selected slot (a slot in use: **L**, then **L** again to replace it); right to the red cross after it, then **L** and **L** again, deletes the file from the SD card. |
| **fast same-map** | When on (default), loading while already in the same map and age with no event running resets actors and gimmicks in place without a loading screen. Otherwise, triggers a silent full map reload. |
| **session flags** | When on (default), loads also restore unsaved session fields (`save_file == -1`, such as NPC talk state, minigame state, and destructibles/room state). |

Under **return**: each of the 5 slots (showing `empty` or `<map> (child/adult)`), plus the current save/load status while busy.

Memfiles also bring back Nayru's Love (on with its time left, or off) and Farore's Wind's warp point and sparkle.

Memfiles are only written to or read from the SD card when you export or import one. A `.szm` file can be shared: it records the game version it was made on (1.0.0 or 1.1.1) and the sloperina version that wrote it, and is refused on another game version, by an older sloperina that can't read its format, or if it's damaged. Newer sloperina versions read memfiles made by older ones; a part an older file doesn't have (or a newer file has that this version doesn't know) is left as the map sets it. Memfiles exported by the early test builds of this feature (format 1) can't be imported. A memfile holds a whole save's GameData, so loading someone else's replaces your current game's progress in memory, just as loading your own does: the game's save files are only changed if the game saves afterwards.

Saving is refused during a cutscene, a game over, or while the game is paused (pause screen, map, item wheel); loading is refused during a cutscene or while paused. Loading during a game over is allowed (it reloads the map).

Caveats:
- Link climbing a ladder, vine or wall isn't restored: he's put back at the spot and drops.

### 2.5 Cheats

**infinite**: energy, rupees, magic, deku sticks, deku nuts, deku seeds, bombs, arrows, bombchus, magic beans,
business scrub seeds (30): each kept full while ticked.

**player**: **2x movement speed** makes Link move twice as far each frame. **walk through walls**: hold the stick
against a wall, rock or air wall and Link walks on through it at the height he was at, until you let go of the stick
or turn it. While floating, levitate's own **through walls** setting applies instead. (To keep jump height at its
maximum, see **watches > lock jump height**.)

### 2.6 Inventory

| Menu | What's in it |
|---|---|
| **items** | **child items** (deku sticks, boomerang, fairy slingshot, magic beans, child trade item), **adult items** (fairy bow, megaton hammer, fire / ice / light arrow, the **hookshot** switch: none / hookshot / longshot, adult trade item), **universal** (bombs, bombchus, deku nuts, Din's fire, Farore's wind, Nayru's love, ocarina, bottles A to E, business scrub seed). |
| **ammo** | How much of each kind of ammo you carry, up to your bag's size. **L** fills it. |
| **equipment items** | **swords** (kokiri, master, and the **big sword** switch: none / giant's knife / broken giant's knife / biggoron's sword), **shields** (kokiri, hylian, mirror), **tunics** (goron, zora), **boots** (iron, hover), **other** (lenses of truth, echo gem, **mask slot**: one of the mask shop's masks at a time, **amiibo mask**: none / fairy-kid mask / princess mask, the masks Link's and Zelda's amiibo give; in the game's style each shows the chosen mask's picture). |
| **collection** | **quest rewards** (fairy ocarina, ocarina of time, gerudo card); **strength upgrades** (goron's bracelet, **gauntlets**: none / silver / gold); **capacity upgrades** (**scale**, and the size of each bag and the wallet: deku stick bag, deku nut bag, deku seed bag, bomb bag, quiver, wallet); **fairy upgrades** (double defense, **spin attack**: the Great Fairy of Power's stronger spin attack, set as her fountain's event having been seen, **magic**: your magic and the meter); **collectibles** (**skulltulas**: the tokens' count, and they show in the collection screen). |
| **quest items** | **spiritual stones** and **medallions**. |
| **songs** | **normal songs** and **warp songs**. |
| **dungeon items** | Per dungeon: map, compass, boss key, and **small keys** where it has them. |

Everything goes through the game's own code where it needs it: items appear on the pause screen, the HUD
redraws, taking away an item you're wearing takes it off Link, and an item on a button comes off the button.

### 2.7 Equips

What Link is wearing and holding right now (read only).

### 2.8 Watches

gz's watch list: values shown on the screen in a box while you play.

| Item | What it does |
|---|---|
| **show on screen** | Show the watches box (also **R + D-pad right** in the game). |
| **watches box > position** | Top-left, top-right, bottom-left, bottom-right, top-centre, bottom-centre, or **custom**. |
| **watches box > move watches position** | Move the box anywhere: the D-pad glides it (hold to speed up), **L** leaves it there. It's shown while you move it, and so is the input display if it's on. |

**watches**: your list, five at a time. With more, **↑** above it and **↓** below it scroll the list (hold **L** to keep
going), and in the game's style a scroll bar beside it shows where you are. Each watch is a
row of buttons:

| Button | Does |
|---|---|
| **✕** | Remove the watch. |
| **☑** | Show or hide it in the box (it stays in the list). |
| **🔧** | Open the value in **debug > memory** (for watches that read one place in memory). |
| *the name* | Rename it, with gz's on-screen keyboard. An empty name brings back its own name. |
| **< type >** | How the value is shown (next type each press). |
| **↑ ↓** | Move the watch up or down the list. |

Under the list:

- **📁 (known watches)**: the built-in watches: x, y, z, facing, move angle, speed, y speed, camera angle /
  pitch / distance, input, stick, lag, timer, frame, map, air wall, jump height. Each can be in the list once.
  **jump height** is the height your last jump set (takeoff + 0.7; FLT_MAX until you jump after a load), which
  the jumpslash launch is limited to. It's always in the list (hide it to keep it off screen).
- **+ (custom watch)**: a watch of any memory address. Type the address (D-pad left / right: digit, up / down:
  change it), **L** to add. It starts from your last memory watch, as in gz. One watch per address. Changed your
  mind? **B**, or D-pad left past the first digit to the **x** that takes the folder's place and **L**: no watch is
  added.

**Display types.** Positions and speeds: **f32** (decimal), **s32** (whole number), **x32** (the float's bits).
Angles: **deg**, **hex** (the game's 16-bit angle, 0x4000 = 90°), **s16**, **rad**. Counters: **dec**, **hex**.
Timer: **m:s**, **sec**. Stick: **%**, **raw**. Memory watches: **u8 s8 x8 u16 s16 x16 u32 s32 x32 f32 x64**.

**timer**: **start/stop timer**, **reset timer**, **reset lag counter** (also shortcuts,
[1.3](#13-shortcuts-in-the-game)). **reset jump height** is under its own heading, below.

**The keyboard** (renaming): the text line at the top (D-pad left / right moves the cursor, **L** finishes), then
rows of keys (D-pad picks a key, **L** types it) and a row of **shift**, **space**, **back**, **del** and **ok**.
**return** cancels. Up to 12 characters.

Memory watches aren't saved (addresses change every time the game starts); everything else is.

**jump height** is the seventh default watch. It resolves the current player's jumpslash height
threshold on each read, showing an f32 with nine significant digits, **Max** for `FLT_MAX`, or **N/A**
if unavailable. A padlock beside its name in the HUD and Watches list indicates that **lock jump height**
is enabled. Custom names are preserved; long names are shortened on screen to leave room for the icon.

Under the watch list, the **jump height** controls change that threshold:

- **reset jump height** writes `FLT_MAX` (about `3.40282347e+38`) once (also **R + A + D-pad right**). A later jump
  may change it again.
- **lock jump height** keeps writing `FLT_MAX` on each mod frame, including while the watch or menu is
  hidden. It follows the current player across scene changes and skips unavailable or read-only memory.
  The lock is off at each game launch and is not saved. Turn it off to stop writing; the game can then
  update the threshold normally on a later jump. Turning it off does not restore an old value.

These controls modify live gameplay memory. The watch itself still only reads it. This is a per-frame
freeze, not a patch of the game's setter: writes and checks within a single game frame may still happen
before the mod runs. Build and host tests do not establish whether this prevents every launch restriction
in-game; test normal jumps, sidehops, jumpslashes, scene changes, and restarts on the supported game version.

### 2.9 Save management

The top-level **save management** menu: copies of the game's save folder on the SD card, made after each save or
when you ask, and putting one back in place of the save.

**automatic save archive >** is a menu of its own: after a save the game makes, the whole save folder (every slot, and the options)
can be copied to `sloperinaGZ/save-backups/<date>_<time>/` on the SD card, e.g. `2026-11-15_21-30-05` (year first, so
they sort by date). The newest 100 are kept; older ones are deleted. Each is about 1 MB.

| Item | What it does |
|---|---|
| **enable** | Turns the automatic archives on. Off by default. The two below are its options. |
| **back up option** | **autosaves** (the game's own, at loading zones and events), **hard saves** (from the save screen, or game over's save prompt) or **both** (the default). Changing only the options is never archived. |
| **hard saves numbered** | On by default. A hard save becomes the next numbered archive where **archive now** puts them (**save new archives here**), named as it names them: `save-archives/Any% run/#008 - Goron City - 18-11-2026 15-47`, the location where the save was made, then the date and time. They go up from **next number** in sequence and are never deleted automatically. Off: hard saves are dated copies in `save-backups/`, with the autosaves. |
| **location in folder name** | Off by default. For the dated copies in `save-backups/`: adds where the save was made, as the game's save screen names it, after the date: `2026-11-15_21-30-05 - Temple of Time`. |

Under **save archives** (these work whether or not **enable** is on):

| Item | What it does |
|---|---|
| **manage archives >** | A small folder browser (below). The line under its **return** says where new archives go. |
| **archive now** | Opens the keyboard to name the archive, with the save's location filled in (up to 30 characters). On **ok** it copies the save folder, as the game last saved it, to `sloperinaGZ/save-archives/[<folder>/]#<number> - <name> - <date>`, e.g. `#007 - Before Volvagia - 18-11-2026 21-30` (the date is day-month-year, then the time), in the folder chosen with **save new archives here** (its line shows it, e.g. `archive now  to Any% run`). With the name left empty, the location is used. Progress since the last save isn't in the save folder yet, so save first to archive it. **return** cancels. Named archives are never deleted automatically. |
| **next number** | The number the next archive gets: one more than the highest in its folder, going up by one after each archive. Change it to number one again or to leave a gap; two archives can share a number. |

**manage archives** has two folders:

- **named archives** (`save-archives/`): under the folder's path, a row of buttons, picked with D-pad left / right,
  each said in the hint: **up a folder** (a folder with a green ^), **new folder** (a folder with a green +: names a
  folder and makes it here), **archive now** (a floppy disk with a green +: names an archive and copies the save
  into this folder, numbered from its highest), **save new archives here** (a floppy disk: **archive now** and numbered hard saves put
  them in this folder, numbered from its highest; with a green tick when they go here already) and **delete this
  folder** (a folder with a red x: it and every archive and folder in it). Delete asks first on a screen of its own,
  **delete this folder?**, with the folder, what it holds and that it can't be undone: **no, keep it** (where the
  cursor starts; back does the same) or **yes, delete it**. In
  `save-archives/` itself there's no **up a folder** or **delete this folder**. Under **in it**: its folders (the
  folder icon; **L** opens one), A to Z, then its archives, highest number first. The path shows at the top, e.g.
  `save-archives/Any% run/Forest`.
- **automatic archives** (`save-backups/`): the automatic ones, newest first.

In both, the folder's line and controls stay at the top and **return** at the bottom: only what's in it scrolls,
with the scroll bar beside it.

**L** on an archive opens it: its name, **restore** and **delete**. Each takes **L**, which shows **L again:
restore** (or **delete**), then **L** again; moving off it cancels. **delete** removes it from the SD card for good.
A folder in `save-archives/` whose name starts with `#` is an archive, any other a folder of them, so folders made
on a computer show too (names with `#` first are taken as archives). The chosen folder is kept in the settings
file as `archive_folder=`.

A restore first copies the save folder as it is to `save-backups/` (kind **before restore**), so a restore can be
undone by restoring that one. Then it copies the archive over the save folder, every slot and the account file but
not the options, and the notice **save restored: load it from the game's Load screen** shows. On 1.1.1 the game's
Load list is then read again from the restored files: only the slots whose save the restore changed, about a tenth
of a second each (all 12, about a second, if that can't be told; none if nothing changed), and **the game's Load list
now shows the restored save** says when it's done. Restored with the pause screen's **Load** list open, the list
updates where it is: its rows disappear for about a second (the game doesn't take the controller meanwhile) and
come back with the restored saves. Under the game's Save list, a save, game over or **Load this save?** it waits
until that has closed. On 1.0.0 the list shows the restored saves after the game is restarted. The game keeps running what
it loaded until then: open the pause screen's **System > Load** and load the slot. Do that before an autosave (a
loading zone or an event), which would write the old game over its slot. The pre-restore copy isn't trimmed straight
away, so the archive being restored is never deleted to make room for it.

The line under **return** shows the last archive this session and its kind, or why there's none. A notice, **save
backed up**, shows after each automatic one (**save archived** after **archive now**). Only `save-backups/` is kept to
100; the oldest go first. To restore one with the game closed instead, copy its contents back over the game's save
folder (on an emulator: right-click the game > *Open save data location*).

### 2.10 Story

| Item | What it does |
|---|---|
| **all tutorials seen** | Marks every tutorial message as seen, so they stop popping up. |
| *each quest* | The quest's current stage; D-pad left / right (or **L**) changes it. It takes effect after a loading zone. |

### 2.11 Debug

**tests > dive tests**: **from heights** and **while moving** measure how deep Link goes into water by how he falls in. Start
one with Link swimming at the surface (facing open water for **while moving**) and leave the controls alone: it
measures the surface, drops him again and again from set heights (from heights: 1 to 192 above the surface; while moving:
16 and 96 above it, 0 to 13.6 units/s the way he faces), twice each, and writes each drop's fall time, speed
going in, and deepest point to `log.txt` ("dive test: ..."). Choose it again to stop.

#### 2.11.1 Heap

How much memory the game has and uses (refreshed every second), and **view the heap in memory**.

#### 2.11.2 Memory

gz's memory viewer.

| Item | What it does |
|---|---|
| **address** | **L** to type an address; D-pad left / right moves a page. |
| **view** | byte, halfword, word, float, pointer. |
| **go to** | Jump to: main, sloperinaGZ, link actor, link status, camera, game data, save strings. |
| **↑ ↓** | Scroll a row (hold **L** to keep scrolling). |
| *rows* | 8 bytes each. **L** edits a row: D-pad left / right moves along it, up / down changes the digit (written at once). On the row's address, **L** adds a watch there; in pointer view, **L** on a value goes where it points. **R + D-pad left** stops editing. |

Unreadable memory shows as `??`; read-only memory can't be changed. **Editing memory can crash the game.**

#### 2.11.3 Pause research

The game's own pause state, for development: values you can watch, change and log.

#### 2.11.4 Flag logging

Records changes to the game's saved progress flags while you play, to learn what each flag is for. Every named story
flag, event flag, item flag, counter and quest stage kept in the save is compared with the frame before. Flags
without a known name are left out, and so is `Player_Rot_For_Map` (which way Link faces, for the map screen). The
files go to `sloperinaGZ/flaglog/` on the SD card. There are three, each with its own switch:

| Switch | File | What |
|---|---|---|
| **record flag changes** | `flags-NNN.csv` | Every flag change except player status. The newest one (the highest number) is added to, across sessions and save loads, with a `start` row for each session; `flags-001.csv` is made when there's none. To start a fresh file, rename or delete the old ones. |
| **record player status** | `PlayerStatusChanges.csv` | `PlayerStatus.*`: health, magic, rupees, what Link wears, bottles. One file, added to each time, with a `start` row for each session. |
| **record story events** | `StoryEvents.csv` | The story menu's values (the quests' stages) when the game changes them, with what most likely caused it. Your own changes from the story menu aren't recorded here. One file, added to each time. |

The switches are kept in the settings, so they stay on across starts. The lines under the menu show each file and
how many rows this session has added. While **record flag changes** is on, **Event flag written to CSV** shows in the bottom right
when a flag changes, at most once a second ("3 event flags written to CSV" when more changed in that time). Each
change is also kept in the log menu: choose **event flags** as its filter (2.11.5).

Each row has the date and time (the console's clock, e.g. `13/11/2026 15:47:02`), the seconds since this session's recording
started, the frame, the map, Link's position, what Link was doing (`EvtTalk` while he talks; 1.1.1 only), the
flag's type, hash and name, its old and new values, and who changed it: `game`, or `sloperinaGZ` for the menu and
cheats. Other kinds of row:
- **`area`:** Link entered a map. It gives the old and new map, and in the note the entrance, the arrival event
  and the event starter.
- **`String`:** a change to one of the game's named text fields, such as the map.
- **`bulk`:** loading a save changes thousands of flags at once, which shows as this one row.
- **`muted`:** a flag that keeps changing (a timer, or a value a cheat holds) is muted after 10 quick changes.

`StoryEvents.csv` adds, after those columns:

| Column | What |
|---|---|
| `cause` | In words, e.g. `talking: Npc_Kokiri_Mido_Child (Npc_Kokiri_Mido_Child_C1_N)`, `event: …`, `entering HyruleField_Adult -> LakeHylia_Adult - arrival event …`, `after the event …`, or `unknown`. |
| `event`, `event_ago` | The last event that played (a `HavePlayedEvent` or `HaveTalked_Special` flag going on) and how many seconds before. |
| `npc` | That event's NPC, when it's an NPC's and played in the last minute. |
| `last_area`, `area_ago`, `entrance`, `arrival_event` | The last map Link entered, how long before, the entrance and the event it played on arrival. |
| `message_target` | The name the game's messages are using at that moment (`UI_MessageTargetActorName`). |

On a PC with the game's files, `python tools/flaglog.py <file>.csv` writes `<file>-named.csv` for any of the three.
It adds a cause from the game's event scripts (`cause_from_scripts` in the story file, where the recorder's own
`cause` is kept), such as `talking: Npc_Kokiri_Mido_Child` or `getting <item>`. For changes made by event scripts it
also adds the script (`set_by`, e.g. `SageOfWater/IceCavern_WaterSerenade` for the Water Temple quest moving to
`Step04`), the NPC and the dialogue just before the change. The scripts say who *can* set a flag, so when several
can, the `match` column says how sure the pick is; see the tool's help.

#### 2.11.5 Log

Under **log history**: the last 128 messages sloperinaGZ wrote, newest first, five at a time. **↑** above them and
**↓** below them scroll (hold **L** to keep going), and in the game's style a scroll bar beside them shows where you are.

| Item | What it does |
|---|---|
| **filter** `< all >` | Which messages: **all**, only **problems** (anything that couldn't be done), **event flags**, or one kind (warp, settings, notice... whatever kinds there are). |

**event flags** shows the flag changes recorded by flag logging (2.11.4), each with the date and time, e.g.
`13/11 15:47:02 HavePlayedEvent.Npc_Kokiri_Mido_Child_C1C2_TT 0 -> 1`. It keeps the last 128 flag changes in a list of
their own, so other messages don't push them out. Flags without a known name show their hash.

Everything is also written to **`sloperinaGZ/log.txt`** on the SD card (on an emulator, its virtual SD card;
Lichii build 150+: `%APPDATA%\lichii\sdmc\sloperinaGZ\log.txt`; Fritchii v5.8+: `%APPDATA%\fritchii\sdmc\sloperinaGZ\log.txt`; Eden and older Lichii / Fritchii: `%APPDATA%\eden\sdmc\sloperinaGZ\log.txt`), with the time since the game started, about once a
second. It starts fresh each time the game starts. Send it along when you report a problem.

### 2.12 Settings

The settings menu lists its menus: **menu settings**, **input display**, **levitate**, **frame advance** and **rebind
commands**.

**menu settings**: **language** ([6](#6-languages)), **size** (auto, small 2x, medium 2.5x, large 3x: the menu, watches
and log), **background opacity** (0 to 100%, 100% at first; below 100% the game shows through, slightly behind) and
**hide in game menus** (on at first): no watches, input display or log while the game's pause screen (Plus) or map
(Minus) is open, so they don't cover it. They show again when it closes, and while this menu is open (to move them).
They stay over the item wheel unless **hide over item wheel** is on.

**hide over item wheel** (off at first): the same while the game's item wheel is up.

**switch keyboard** (on at first): naming something (an archive, a folder, a watch's label) opens the Switch's own
keyboard, with its controls: the left stick or D-pad between keys, **L** / **R** to move the cursor in the text, **B**
to delete, **Y** for a space, **+** to finish (touch and a USB keyboard work too). The game waits while it's up.
Only the characters the menu's font has are kept (others become `_`). Off, or if the Switch's keyboard can't be
shown, sloperinaGZ's own keyboard is used.

**watch box header** (on at first): a small **watches** header over the watches box, underlined.

#### 2.12.1 Input display

gz's input display, at the bottom left unless you move it (hidden while the menu is open).

| Item | What it does |
|---|---|
| **show** | Turn it on. The items below, up to **return**, are indented under it, as its options. |
| **background** | Put it on a background, like the watches. |
| **left stick**, **right stick**, **buttons** | What it shows. |
| **style** | **row**: gz's line of stick values and buttons. **controller**: a Switch 2 Pro Controller with the buttons lighting up, rings for the sticks and their values underneath. **minimal**: a Pro Controller as one short line of outlines that fill while held. |
| **size** | small, medium, large: half, three quarters or all of the menu's size. |
| **attach to watches** | **off** (the default), **above watches** or **below watches**: joins it to the watches box, just above or below it and as wide as the wider of the two, so they read as one box. The two then go where the watches go (their **position**, or where **move watches position** left them), kept whole on screen together. With the watches hidden it goes back to its own **position**. |
| **position** | The corner it's in on its own, or **custom** (where **move input display position** left it), as the watches box. Not used while it's attached to the watches. |
| **move input display position** | Move it anywhere: the D-pad glides it (hold to speed up), **L** leaves it there. It's shown while you move it, and so are the watches if they're on. |

#### 2.12.2 Levitate

| Item | What it does |
|---|---|
| **enabled** | **L3** toggles floating. Hold **X** to rise, **B** to descend; release both to hover. The left stick moves horizontally. X/B/R3 are hidden from the game while floating; L3 is reserved while enabled. Floating into a loading zone ends levitation there, so the zone takes Link as it would on foot (with **through walls** the game couldn't see him otherwise), and L3 can't start it again inside one or until the next map has loaded. **walk through walls** (cheats) also hands Link's collision back inside a loading zone. |
| **through walls** | Use the character controller's temporary collision bypass and position setter while floating. L3/off restores its previous collision mode. Held B commands descent through floors. This and the two below are indented under **enabled**, as its options. |
| **vertical speed** | X/B speed in units per second, replacing the old rise-speed label. Existing saved settings remain compatible; a saved **0** uses 4 units/s for manual movement. |
| **move speed** | Horizontal speed in units per second. |

The status line under **return** shows what levitate is doing.

No X/B input means hover at any vertical-speed setting. Both held together also hover; **R3**
overrides either button to hold height. Releasing or reversing the buttons clears the previous
vertical movement immediately. The mod menu suspends intentional movement.

With **through walls** on, Link can pass through walls and ceilings; hold **B** to descend
through floors. Exit geometry before stopping levitation, since normal collision resumes then.
With through walls off, normal floor/wall correction remains active.

**L3** ends levitation with zero vertical velocity. Normal gravity then accelerates the fall,
and landing protection prevents fall-distance damage through the first landing. Later ordinary
falls are unchanged. The old short health-refund timer has been removed.

The user confirmed traversal through a previously blocking wall, followed by a gentle L3 fall
and no landing damage. This does not establish behavior at every surface or during every event.
Void protection is not included (what voids Link can be seen with [2.3.3](#233-void-out)). Camera-follow and stationary-position
pause behavior have not been separately fixed.

#### 2.12.3 Frame advance

| Item | What it does |
|---|---|
| **enabled** | Turns frame advance on ([4](#4-about-frame-advance)). Off by default; updating from 0.0.3 or older turns it off once. |
| **in pause screen** | Off (the default): while the game's pause screen or an item menu is open, the button is the game's and doesn't start frame advance. On: it starts frame advance there too. |
| **pause icon** | gz's pause icon at the top left while paused. |

Its buttons are the **frame advance** and **run again** commands (**L** and **L + L3** at first; **settings >
rebind commands**). Bound to one button, the game doesn't see that button while frame advance is on; a combination's buttons
stay the game's. Settings from before the commands with frame advance on **R** become **R** and **R + L3**.

#### 2.12.4 Rebind commands

gz's **commands** menu, here **rebind commands**: each of sloperinaGZ's controls and the buttons it's bound to, under **menu**, **frame
advance**, **link**, **scene** and **watches and timer**. The **scene** commands, **reload map** ([2.1](#21-link)) and **end
cutscene** ([2.3](#23-scene)), start out unbound. A bind is up to four buttons pressed in that order (**R**, then
**right stick down**); letting go of one ends it, and pressing another button partway through cancels it. When one
bind is part of another that's also held (**R + D-pad left** holds **D-pad left**), only the bigger one counts, so
**R + A + D-pad right** resets jump height without also hiding the watches. **run again** takes its buttons in any
order, and levitate's **up**, **down** and **hold height** work while their buttons are held, in any order.

| Item | What it does |
|---|---|
| a command `< buttons >` | **L**, let go of everything, then press the new buttons in order and let go. The row shows what was recorded; then **L** keeps it, or **B** (or **R + D-pad left**) cancels and the old bind stays. Those are the menu's select and back as bound before the change, so rebinding select itself is confirmed with the old select. **L held** (half a second) at the start instead unbinds it. Nothing pressed for six seconds also leaves it as it was. The hint line says which step it's at; while recording, no command works (not even the menu's). |
| **restore defaults** | Every command back to the buttons above. |

A change is saved to the settings file straight away, without waiting for the menu to close, so it's kept if the
game is quit with the menu open.

The menu's own commands (show/hide menu, return, up, down, left, right, select) can't be unbound, so the menu can
always be reached; the others can. Each applies only where it means something: the menu's while the menu is open,
the shortcuts while it's closed, frame advance's while it's on, levitate's while it's enabled; show/hide menu always.
In the settings file each is `bind_<name>=<buttons>`, e.g. `bind_menu=R + rs-down` or `bind_ragdoll=unbound`. The
button names: `A B X Y L R ZL ZR L3 R3 plus minus d-up d-down d-left d-right ls-up ls-down ls-left ls-right rs-up
rs-down rs-left rs-right SL SR`.

### 2.13 About

The name and version, the build date ("Build Date: ..."), and the website.

## 3 On the screen

| What | Where | Notes |
|---|---|---|
| Menu | Top left, under the D-pad icon | Always on top. |
| Watches | The corner you choose, or anywhere | [2.8](#28-watches) |
| Log | Bottom right | What you just did; stays a second, then fades. gz's grey text with a shadow. |
| Input display | Bottom left, or the corner you choose, or anywhere, or attached above or below the watches | [2.12.1](#2121-input-display); in the bottom left, it moves above the watches if they're there too. |
| Pause icon | Top left | Two bars: paused by frame advance. |
| Air walls | Over the game | [2.3.1](#231-environment) |
| Save slots | Over the game | [2.3.2](#232-save-position) |
| Void out | Over the game | [2.3.3](#233-void-out) |

## 4 About frame advance

With **settings > frame advance > enabled** on and the menu closed (and, unless **in pause screen** is on, the
game's pause screen closed):

- **Tap the frame advance command** (**L** unless changed in **settings > rebind commands**): the game pauses (one more frame plays first, so the pause icon is on screen and
  you can see it hasn't crashed). Tap again: one frame. **Hold** it: after a moment, frames play
  one after another (about 15 a second) until you let go.
- **Run again** (**L + L3**, either order): the game runs again. It always works, even with the menu open (the menu
  stays open). Press L3 first there, then L, so the menu doesn't take the L as a select.
- A tap counts when you let go. You can hold or press anything else meanwhile (move the stick, hold A or Z...),
  as in gz: the frame you let through gets it. A shortcut that starts with the same button (with frame advance on
  **R**: R + D-pad, R + right stick down) stays a shortcut and doesn't advance.
- **Buffered input**: everything you press while paused, and the last direction you push each stick, is given to
  the next frame, even if you've let go by then.
- While paused the game draws nothing, so sloperinaGZ shows its last frame itself, about 60 times a second: the
  menu, the watches, the input display and the log stay live over it. The menu can be opened and used while
  paused; its button works the menu then. (If the game's picture is larger than 2560x1440, the last frame stays
  on screen as it was and a menu opened while paused can't be seen; run again then closes it.)

## 5 Settings file

Settings are saved on the SD card in `sloperinaGZ/settings.ini` (on an emulator, its virtual SD card) a couple of
seconds after you close the menu. It's plain `key=value` text: cheats, watches (which, in what order, their types,
names and whether shown), the watches box, menu size, background, hiding in game menus, language, input display,
levitate, frame advance, save position, flag logging, void out and the commands' binds. Delete it to go back to the defaults. Your game saves are never touched.

The game's pictures sloperinaGZ uses (the controllers in the input display, the menus' parts, the item icons)
are kept in `sloperinaGZ/cache/` once they're made from your copy of the game, so later starts don't have to make
them again. They're the game's own pictures: don't share them. Delete the folder whenever you like; they're made
afresh at the next start.

## 6 Languages

**settings > menu settings > language** lists English and every translation on the SD card (`sloperinaGZ/lang/<code>.json`).
To make one, see [INSTALLATION.md](../INSTALLATION.md#languages). The font only has plain letters, so accents are
dropped (é shows as e) and other alphabets can't be shown yet.

## 7 Known limits

- Game versions **1.0.0** (base) and **1.1.1** (update v131072) are supported.
- Collision shows the air walls; the full collision mesh isn't drawn yet.
- Memory watches aren't kept between runs.
- Some changes only show after the game redraws them (pause screen, loading zone); story stages need a loading
  zone.
