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
  - [2.4 Cheats](#24-cheats)
  - [2.5 Inventory](#25-inventory)
  - [2.6 Watches](#26-watches)
  - [2.7 Save / memfile management](#27-save--memfile-management)
  - [2.8 Debug](#28-debug)
  - [2.9 Settings](#29-settings)
  - [2.10 About](#210-about)
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
every shortcut below, can be changed in **settings > rebind commands** ([2.9.4](#294-rebind-commands)). The menu's hints and the
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
| **L3** | Levitate on / off (if enabled, [2.9.2](#292-levitate)) |
| **X / B** held | Levitate up / down; release to hover |
| **R3** held | Levitate: hold height even if X/B is held |
| **R3** | Ragdoll (as a bomb hit) when not levitating |
| **L** | Frame advance (if enabled, [2.9.3](#293-frame-advance)) |
| **L + L3** (either order) | Frame advance: run again |
| **R + right stick up** | Free camera on / off ([2.8](#28-debug)) |

**reload map** and **end cutscene** can be given buttons too (**settings > rebind commands**, under **scene**); they
have none at first.

## 2 Menus

### 2.1 Link

Five submenus (**equips** the last), then **lank / lonk mode**, then under **return** what can only be read: Link's **position** (x, y, z), his **current age**, the **map** and whether he has **hookshot storage** (yes / no; - without a hookshot).

#### 2.1.1 Status

| Item | What it does |
|---|---|
| **current health** | Link's health in hearts and quarters. D-pad changes it a quarter heart at a time; **L** fills it. |
| **heart containers** | 1 to 20. Lowering it below your health lowers your health too. |
| **rupees** | Up to your wallet's size. **L** fills it. |
| **age** | **child** or **adult**: what the next **reload map** (**scene interaction**) gives you. |

#### 2.1.2 Scene interaction

| Item | What it does |
|---|---|
| **no void outs** | The game never voids Link out, whatever the cause (out-of-bounds ground, a bottomless pit, the fall-down areas in dungeons). He keeps falling instead: levitate, load a position or warp to get back. A void-out already under way when it's ticked still finishes. Off when the game restarts. |
| **show hookshot hook** | Draws the hookshot in the world, through walls. **Gold** box: the hook while it's out, with a gold line (the chain) from Link's hand. **Grey** cross: where a hook was put away. **Red**: a line from where the shot started to the spot it aimed at, with a cross there, while a shot runs or while storage holds it: that's where a stored shot pulls Link to. |
| **reload map** | Reloads the map you're in, at the age set in **status**. Changing age loads the other age's version of the map and swaps sword and shield with the ones from the other age (adult-only tunics and boots come off a child). |

#### 2.1.3 Position

| Item | What it does |
|---|---|
| **x / y / z** | Link's position, in metres. **d-left** / **d-right** move him along that axis by the **step** (with **L** held, ten steps), from where he is, as **load position** moves him; locked or not (the locks hold him at the new spot). A press while the last move is still settling is dropped. |
| **step (m)** | How far each press of **x**, **y** or **z** moves him: **0.01**, **0.1**, **1** (to start with) or **10**. |
| **lock X / lock Y / lock Z** | Hold Link's X (map North/South), Y (height) or Z (map East/West) where he is. They let go after a map change. |
| **save position (R + d-up)** | Remembers where Link is, which way he faces and the map (gz's save position). Works at the first press, anywhere. |
| **load position (R + d-down)** | Puts Link back there, facing the same way. Only in the same map. With nothing saved: "No position saved yet". |

#### 2.1.4 Actions

| Item | What it does |
|---|---|
| **ragdoll (R3)** | Knocks Link over and tumbles him backwards: the game's own reaction to a bomb, so he can't attack and doesn't sink while it lasts. He gets up by himself and it unticks then; it can't be cut short. When the game wouldn't take a hit (a map's arrival, an event) it says so and does nothing. On 1.1.1 the game caps the tumble's speed; 1.0 has no cap. |
| **bomb t-pose** | Turns on the ocarina breath glitch: Link's arms go into a T-pose while he runs holding a bomb. In the game it comes from holding one ocarina note until Link takes a breath (about 4.5 s), and lasts until an idle animation, an attack or a boots change. While ticked it stays on through those. Unticking puts the normal pose back, which also clears the glitch if the ocarina caused it. Checked on 1.1.1 and 1.0.0 as adult Link. |
| **force hookshot storage** | Gives hookshot storage on purpose: the moment a hookshot or longshot shot starts, Link takes a bomb's medium hit, which cancels the shot before the hook flies. The next shot then skips flying and pulls Link straight towards the spot the cancelled shot aimed at, through walls if it was behind one (on another map it crashes the game). Says "hookshot storage" when it worked. A shot that uses storage isn't hit, so it can be used with this ticked. 1.0.0 only: on 1.1.1 the game lets go when nothing real is hooked, so the option isn't in the menu there. |

#### 2.1.5 Equips

What Link is wearing and holding right now (read only).

#### 2.1.6 Lank / lonk mode (beta)

Beta: it changes the game's own files as the game reads them and in memory. It starts off each time the game starts.

| Item | What it does |
|---|---|
| **mode** | **nothing** (Link as the game has him), **lank** (child Link playing the adult's animations, so stretched to adult proportions) or **lonk** (adult Link playing the child's, so squashed to the child's), as in the cutscenes played at the wrong age. |
| **apply and reload** | Makes the chosen mode the one in force and reloads the map you're in with Link as the mode has him: the child for lank, the adult for lonk, in any map, also one made for the other age (Lank in Gerudo Fortress). The game makes Link anew only when his age changes, so when he already has that age the reload goes through the other age and back (two loads; sword, shield and clothes come back). He ends where he stood. It also sets **warps > override age** to the mode's age, so warps from the menu keep him that way; **nothing** puts that back to disabled and reloads the map at the age it was made for. An exit taken on foot lets the next map decide his age as usual. |

### 2.2 Warps

| Item | What it does |
|---|---|
| **places** | Every map, sorted into **overworld (child)**, **overworld (adult)**, **dungeons**, **boss & battle rooms** and **other**. Pick a map, then a spawn point; you go there straight away with the game's own fade. |
| **override age** | **disabled** (the map decides, as in the game), **child** or **adult**: the age Link arrives at. Child or adult also works in a map made for the other age (child Link in Gerudo Fortress): the age is set when you warp, sword and shield swapped as by **link > status > age**, and the map's own age switch is held off until the map has loaded. |
| **cancel warp** | Cancels a warp that's waiting (see below). |
| **arrival cutscenes** | Arrive with the spawn point's own arrival scene instead of the standard one. |

If the game ever refuses the instant warp, the warp waits for you instead: walk through any exit or loading zone
and you'll come out at the chosen place. Under **return**: the **current** map and the **warp** state.

### 2.3 Scene

| Item | What it does |
|---|---|
| **end cutscene** | Ends the cutscene (or dialogue, chest opening or other event) that's playing. A played-back cutscene is skipped the way the game's own skip does; any other event is cancelled the way the game cancels one. Flags the rest of the event would have set aren't set, so it may play again. |
| **time of day** | The game's clock: the time now (and whether it's day, 06:00 to 18:00, or night), **hour** and **minute** to set it, **freeze time** to hold it where it is, and **dawn**, **noon**, **dusk** and **midnight** to jump to. It sets the clock the way the game's own `ExecuteWmSetTime` does. Until the story has started the clock (early in a new game it stays at 06:00), setting it does nothing. Freezing isn't saved in the settings. |
| **explorer** | Go round this map's dungeon rooms, grottos and interiors with the free camera, and go to them. See [2.3.4](#234-explorer). |
| *collision* | |
| **environment** | See below. |
| **void out** | See [2.3.3](#233-void-out). |
| *save and load* | |
| **save position** | See [2.3.2](#232-save-position). |
| *enemies* | |
| **display enemy health** | A bar with **life/max** over each living enemy within 30 m of Link (red under a quarter); it goes when the enemy reaches 0. Enemies that can't be killed (Deku Scrubs, Bongo Bongo's hands: the game makes them invincible from the start, and gives them its default life of 100) have none. Enemies that haven't appeared yet (Wolfos still in the ground, Stalfos before they drop) have none, and none are drawn while the game is paused. In the game's style a bar fades out while its enemy is hidden behind walls or other scenery. |
| **display through walls** | Also show the bars of enemies hidden behind the scenery (the game's style). Indented under **display enemy health**. |
| **reload map** | Reloads the current map (at the age set in **link**). Last, above **return**. |

#### 2.3.1 Environment

Three submenus: **air walls**, **guards** and **actors**.

**air walls**: the map's invisible walls, drawn as outlines over the game.

| Item | What it does |
|---|---|
| **show** | Draw the kinds ticked under it (its options, indented below it). **Red**: a wall Link is inside, whatever its kind. |
| **that push link back** | **Amber**: walls Link can't stand on, so he's pushed back off them (the ledge in front of the Forest Temple in Sacred Forest Meadow as a child). On by default. |
| **that stop link** | **Cyan**: invisible walls he can't go through (round the rocks in the Lost Woods). On by default. |
| **other kinds** | **Violet**: walls that don't touch Link on foot: those for enemies, NPCs, animals, Epona, the camera, bombs and the hookshot, and those that only stop him while he climbs. |
| **through link** | Also draw them where Link is in front of them (in the game's style they're hidden behind him). |

The two lines under the air walls menu, below **return** and apart from its options, count the walls in this map and those near you, and say whether Link is inside one
(how deep, and which way the game will push him out). Outlines show through the scenery; in the game's style
Link stays in front of them.

**guards**: its **guard vision** draws, on the ground round each security guard within 60 m of Link (the Gerudo
fortress's guards, Hyrule Castle's soldiers, the castle courtyard's child soldiers), where it catches him. The
numbers are the game's own for that guard as it is now, read each frame (FINDINGS 9.36).

- **The cone**: where the guard finds Link. It starts at the guard's head and turns as the head looks round. A Gerudo
  in daylight: 45° either side, 14 m; a courtyard soldier: 50° either side, 12 m; at night or on horseback the
  game's other values. Link counts only within the guard's height band (a few metres above or below the head).
  Where the guard has a further alert sight (the Gerudo, the courtyard soldiers) it's drawn past the cone, fainter.
  A Hyrule Castle soldier on the engine's ordinary sight: 7.5 m, drawn 90° either side.
- **What hides Link**: rays from the guard's head across its sight stop at walls, hedges, crates and the like (the
  collision the guards' own sight rays test), and the cone stops with them.
- **The ring** round each guard: closer than this (3 m in daylight, 2 m in the dark) it finds Link whichever way it
  faces.
- **The colour is what the guard is doing** (its AI's state): **green** searching (on duty, patrolling or looking
  round), **blue** alerted (it's noticed something), **red** it's caught Link, or Link is inside its sight now;
  **grey** off duty, so it doesn't catch him.

**patrol paths** (indented under it; on at first): the path each guard walks, in pale yellow: back and forth, or
round a loop, with a ring where it stops to look round and a tick the way it faces there.

Under them: how many guards are near, how many on duty, and how many are alerted or have caught Link. Hearing isn't
drawn. In **debug > actors > browse currently loaded actors** each guard has switches of its own (off at first): see
**actor options** there.

**actors**: its **interaction ranges** draws, in four parts each with its own option under it (all on at first):
**talk radius** (the filled area where A works), **z-target talk** (the further outline while Z-targeting), **target
break line** (dashed: where each breaks walking away; it goes with the parts that are on, or both when it's on alone) and
**door open/close radius** (orange: a swinging door is held open while anyone, Link or an NPC, is within 2 m of it on the side it
swings to; once nobody is, it closes 135 frames later; bright while Link is holding it). It draws round everything near Link (within 20 m) that he can talk to or act on,
where he can: an NPC's **Talk** in **cyan**, anything else (a sign's **Check**, **Open Door**, **Open Treasure Box**,
**Lift**, **Grab**...) in **lilac**, both brighter while Link is in range now. The game's test is a fan in front of
Link: the actor has to be within its reach (2 m for most NPCs) and within its angle of the way he faces (60° either way
for most), and within a height band. So the filled disc is where he has to stand, and he has to face it there.
Some add a shape of the actor's own (a sign: the half in front of it; a shopkeeper: a box in front of his counter;
a door: a box beside it), and then only where both hold is filled. A darker outline further out: the reach while Z-targeting (3.5 m for most NPCs). The reach is to the actor's edge, so
its own size is added, read from the actor as the game does (its model's bounds: 0.4 m for a Gerudo guard, so Talk
comes up at 2.4 m from her), and a box is built out from those bounds (a broad shopkeeper's box is 3.2 m across before
it's grown). **Dashed**, round each: where it
breaks as you walk away. Once something has Link's attention its reach holds a little further (its margin, 0.5 m
for most), and while Z-targeting the way he faces stops mattering: a Gerudo guard's Z-targeted Talk breaks at 4.4 m
(3.5 + 0.5 + her 0.4, measured walking backwards with ZL held). The bright colour holds until you pass the dashed
line. The numbers come from each actor's files (tools/interactions.py) and its model; the game also checks things
not drawn here (a clear line to it, that it's on screen for a Z-targeted one, the story's flags, being on a horse):
treat them as a guide. **The game's own answer**: a pale diamond on a stem over the actor the A prompt is for right
now, read from Link's attention (not worked out), and its name on the menu's line (**A: ...**); its areas are drawn
bright while it is. Without one, a darker diamond over what **ZL** would take for an action (a shopkeeper you have to
Z-target to talk to), whether ZL is held or not, and **ZL: ...** on the line.

**hitboxes**: the game's own physics shapes round Link and whatever is near him or the camera (within 25 m), read
from the game every frame, in gz's hit view colours. As in gz, only what can touch something now is drawn: a shape
the game has switched off (an enemy's bite between bites, Link's sword between swings) isn't, and nor is one the game
collects no hits for (the castle grounds' wagon horse has one round it that never hurts Link). **show** turns them
on; under it, each kind has a switch, **off**, **outline** or **fill**:
- **attack hitboxes** (fill at first): **red** (gz's AT): an attack while it can hit: an enemy's bite or swing, a projectile
  (a Deku Scrub's nut), a bomb's or bombchu's blast (or a Dodongo's, a bomb flower's...: its 4.3 m sphere, for the
  one frame the game has it; seen from inside, the view goes red), and Link's own attacks while they can hit: his
  sword mid-swing, a Deku Nut's flash, Din's Fire, the boomerang in flight (not once it's back in his hand), a
  slingshot seed or an arrow in flight. The game has a sword swing's live for only a few frames
  (4 in normal play); under frame advance fewer (1 seen), as the game applies the change that switches it off while
  frame advance holds it.
- **bump boxes** (fill at first): **white** (gz's OC): the bodies Link bumps into and pushes against (each
  character's standing shape, and such as a Wolfos's head), and his own.
- **invincibility** (fill at first): **purple**: where Link, or an enemy that can be fought, is hit, while it's
  invincible: Link for about half a second after he's hurt, an enemy while it can't be hurt. (In gz these vanish.)
- **hurtboxes** (fill at first): **blue** (gz's AC): where Link and the actors can be hit now.
- **collectables** (fill at first): **gold**: where Link takes a drop by touching it (rupees and tokens, hearts,
  heart pieces and containers, magic jars, arrow bundles, Deku Nuts, Seeds and Sticks, bomb and bombchu drops,
  scrub seeds, fairies) and the bugs, fish and butterflies he can catch.
- **shield guard angle** (on at first): **green**, while Link guards with his shield (and as it takes a blocked
  hit): a bubble round him at the shield's height, with a cone cut out at his back, its edge ringed: an attack from
  any direction within 110° of his front (to the sides, above or below) is blocked, one from the cone behind him
  gets through. Straight above and below him are covered. Enemies get one too while their shield is up (a Stalfos,
  Dark Link, anything that guards): narrower, 50° each way from their front, as they turn their whole body to
  face what they block. The same for the Deku, Hylian and Mirror shields (it's Link's,
  not the shield's; which attacks each shield can block at all, the Deku Shield's fire, isn't drawn). The fan's
  size is only for seeing it: the game checks the angle, not a distance.

**only link's**: **show** draws Link's own alone (his body, his hurtboxes, his sword). **through walls** (off at
first): also where walls, scenery or the actors themselves are in front (otherwise hidden by whatever is nearer, as
gz's are). Either way Link hides what's behind him, as he does the air walls: of his own shapes, only their near
sides show over him. An actor's own hitboxes can be turned on from the actor browser (**debug > actors > browse currently
loaded actors**, **minus**), each kind on its own: **attack hitboxes**, **bump boxes**, **invincibility**, **hurtboxes** and
**collectables**
(the browser's panel shows which are ticked). A kind ticked there is drawn for that actor whatever **show** says, in
the style chosen here for it (filled if that's **off**).

Spheres are drawn as their outline and a ring round their middle, capsules as their two end rings, sides and
rounded ends, boxes and other shapes edge by edge; filled, as solid shapes at about 30% opacity (each surface once,
as gz's), so what's inside shows. Like the air walls and the other world overlays, they're drawn over the game's HUD
(drawing them under it flashed). They fade out whenever the game pauses, the item wheel included (whatever **hide
world overlays over item wheel** says): what they show stops with the game, and the wheel turns the game's camera.
Under the list: how many of each there are near.

#### 2.3.2 Save position

Where a save puts Link when it's loaded. A save doesn't keep where Link stands: it keeps a
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
the one a save would keep, or **not set**, or **not found** (the title screen, loading); and where a load
would put Link and how far that is from the slot, or **stays**.

#### 2.3.3 Void out

Where Link voids, drawn over the game from the game's own ground map. The void check doesn't use
the collision: each frame the game looks straight down from Link into a map of one-unit cells that mark
walkable ground, and keeps the nearest such cell it has found round Link. Link voids when the cell underneath
is a deadly one, or when nothing is underneath and that nearest cell is 8 units or more away to the side.

| Item | What it does |
|---|---|
| **show** | Draw the parts ticked under it (its options, indented below it). |
| **edge of the ground map** | **White**: where the mapped ground ends. Past the line the game finds nothing under Link. Not voiding by itself: see the fence. Off by default. |
| **deadly cells** | **Red**: columns over the cells that void Link at once. A column runs from its cell (including deep cells more than 48 units below Link) up to the next mapped ground over it, and expands 0.6 units outward into adjacent unmapped air to match the game's 3×3 downward probe. On by default. |
| **8 unit fence** | **Orange**: a ring 8 units round the ground cell the game is holding for Link now. With nothing underfoot, Link voids on leaving it; it turns **red** when that happens. The rule only measures sideways, so the real boundary has no top or bottom: 4 units of it are drawn at Link's height. A line runs from Link to the cell while Link is off the map. On by default. |
| **where the fence moves** | Translucent ground shading around Link showing whether standing there will shift the 8-unit fence's anchor point: **green** where one of the game's 64 ground-map rays finds a closer mapped surface in 3D (moving the fence safely), **red** where the check fails and the fence stays frozen (or would snap ≥ 8 units sideways while off the map). On by default. |
| **void areas** | **Purple**: the areas placed in the map that void whatever is inside them, **red** while Link is in one. The holes down into grottos are made the same way and are drawn too (thin discs at the hole). On by default. |
| **map's outer box** | **Cyan**: the sides and bottom floor 5 units outside the ground map's outer bounding box (`VolumeStatsAABB`), shown when Link is within 28 units of a wall or floor; turns **red** if Link crosses outside and voids. On by default. |
| **through the scenery** | Also draw all of it where the scenery is in front (otherwise it's hidden behind the scenery, in the game's style). |

The columns, the fence, the outer box and the void areas have see-through sides. The line under the menu, below **return**,
says where Link stands for the game: **on the map**, **off the map** with how far the held cell is out of 8, or
**VOID** with the rule that fired. Each void from the ground map also goes in the log file with what caused it and
where (`void out: ...`).

Not drawn: floors the game tags as an abyss, which void on touch (the whole game has one, at the bottom of the
Shadow Temple's chasm), and a few dungeon rooms' own void scripts. The
cells round Link are read again every two seconds or when Link has moved 4 units, out to 24 units to the side, 48 units down
(with deeper deadly cells projected upward from below) and 16 units up; where there are more lines than fit, the farthest are left out.

#### 2.3.4 Explorer

**scene > explorer** goes round the map you're in by its places: its dungeon rooms, grottos and interiors (the houses,
shops and huts, which in this game are inside their buildings where they stand). The menu closes and the free
camera looks down on a yellow beam, as **debug > actors > browse currently loaded actors** does ([2.8](#28-debug)):

- **d-right** and **d-left** go up and down the places, the camera gliding to each: rooms in their numbers' order
  (d-right up, d-left down),
  the rest nearest first. An interior or a grotto is looked at from close and low, under its roof; zoom out with
  **ZL**. The panel at the top left says which (a room's number, a grotto or an interior), what's in it on the
  line under (a room's or grotto's enemies and chests, an interior's people), the **type** shown, and where in
  the list it is (**3 of 28**).
- **L** and **R** choose which: all **places**, **rooms**, **grottos** or **interiors**, only those the map has,
  moving on to the first of them.
- **A** goes there: Link to the place (or, with none chosen, to where the beam stands), as **load position** moves him (your saved position stays);
  you carry on exploring until **B**.
- In a dungeon, going to a room takes Link into it: the game wakes a room (its enemies, chests, switches) only
  for where Link is, so this is what shows it as it is. The room's enemies can reach him meanwhile, and a room's
  events can start. **B** puts him back where he was when you started exploring, or where **A** last put him.
  **Y** turns this on and off (**take Link**, on to start with and remembered; the bar at the bottom says which):
  off, only the camera goes, Link stays where he is (or goes back there, if he was in a room), and a room shows its
  walls but not its enemies.
- the **left stick**, **d-up** / **d-down** (height), **L3** (back to Link), the **right stick**, **ZL** / **ZR** (zoom) and **X**
  move the beam and the camera as when browsing actors; **B** finishes and gives the camera back.

The places come from the game's map layouts (`tools/places.py`). Far from Link the world isn't loaded, so the
beam may stand in the air (orange) until Link goes there. Some grottos have no landing point of their own, and
the beam stands at their entrance instead. Going to a dungeon room wakes it as walking in does (checked in the Forest Temple).

### 2.4 Cheats

**infinite**: energy, rupees, magic, deku sticks, deku nuts, deku seeds, bombs, arrows, bombchus, magic beans,
business scrub seeds (30): each kept full while ticked.

**player**: **2x movement speed** makes Link move twice as far each frame. **walk through walls**: hold the stick
against a wall, rock or air wall and Link walks on through it at the height he was at, until you let go of the stick
or turn it. While floating, levitate's own **through walls** setting applies instead. (To keep jump height at its
maximum, see **watches > lock jump height**.)

### 2.5 Inventory

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

### 2.6 Watches

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
  pitch / distance, input, stick, lag, timer, frame, map, air wall, jump height, safety, safety flags, time of
  day, door closes, drown timer, fade timer, respawn in, drown state. Each can be in the list once. **door closes** is for the
  door chosen in the actor browser (its **close timer watch**), and **drown state**, **drown timer**, **fade timer**
  and **respawn in** for the animal chosen there (its **drown timer watches**): added from here, they show
  **-** until one is.
  **jump height** is the height your last jump set (takeoff + 0.7; FLT_MAX until you jump after a load), which
  the jumpslash launch is limited to. It's always in the list (hide it to keep it off screen).
  **safety** and **safety flags** show the game's safety section, below.
  **time of day** is the game's clock: **24h** as a gossip stone tells it (`21:05`, the default), **12h**
  (`9:05 PM`) or **sec**, the seconds into the day (0 to 86399). **N/A** while there's no clock (not in play).
- **+ (custom watch)**: a watch of any memory address. Type the address (D-pad left / right: digit, up / down:
  change it), **L** to add. It starts from your last memory watch, as in gz. One watch per address. Changed your
  mind? **B**, or D-pad left past the first digit to the **x** that takes the folder's place and **L**: no watch is
  added.

**Display types.** Positions and speeds: **f32** (decimal), **s32** (whole number), **x32** (the float's bits).
Angles: **deg**, **hex** (the game's 16-bit angle, 0x4000 = 90°), **s16**, **rad**. Counters: **dec**, **hex**.
Timer: **m:s**, **sec**. Stick: **%**, **raw**. Safety flags: **hex**, **flags** (a word for each one set).
Memory watches: **u8 s8 x8 u16 s16 x16 u32 s32 x32 f32 x64**.

**safety** and **safety flags** (**N/A** while there's no scene) read the game's
GameSafetyMgr: what a minigame, a boss fight or a story section is stopping you from doing right now. **safety** is
the name of the setting the game has applied, such as `MiniGame_TimeStop` or `SageOfFire` (its hash, if it isn't one
of the game's 28). **safety flags** is that setting's switches, as the game packs them:

| Bit | Word | Set when |
|---|---|---|
| `0x0001` | mini | a challenge is in a minigame step |
| `0x0008` | on | a safety section is active |
| `0x0020` | area | the setting's `IsCheckSafetyArea` |
| `0x0040` | save | `IsDisableSave` |
| `0x0080` | warp | `IsDisableWarp` |
| `0x0100` | place | `IsDisableAutoPlacement` |
| `0x0200` | box | `IsDisableOpenTreasureBox` |
| `0x0400` | grab | `IsDisableGrab` |
| `0x0800` | boot | `IsDisableBoot` |
| `0x1000` | time | `IsDisableTimeProgress` |
| `0x2000` | end | `DoSaveWhenEnd` |
| `0x4000` | pause | `IsDisableLoadingPause` |
| `0x8000` | err | `IsNoDispErrorForTemporarySave` |

So `0x14c8`, as read with `MiniGame_TimeStop` applied, shows as **on save warp grab time**. Any other bit shows as
a number after the words. Both watches only read; the wrench opens the value in **debug > memory**.

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

### 2.7 Save / memfile management

Two submenus: **save management** (the game's save on the SD card) and **memfiles** (snapshots in memory).

#### 2.7.1 Save management

The **save management** menu: copies of the game's save folder on the SD card, made after each save or
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
not the options, and the notice **save restored: load it from the game's Load screen** shows. The game's
Load list is then read again from the restored files: only the slots whose save the restore changed, about a tenth
of a second each (all 12, about a second, if that can't be told; none if nothing changed), and **the game's Load list
now shows the restored save** says when it's done. Restored with the pause screen's **Load** list open, the list
updates where it is: its rows disappear for about a second (the game doesn't take the controller meanwhile) and
come back with the restored saves. Under the game's Save list, a save, game over or **Load this save?** it waits
until that has closed. The game keeps running what
it loaded until then: open the pause screen's **System > Load** and load the slot. Do that before an autosave (a
loading zone or an event), which would write the old game over its slot. The pre-restore copy isn't trimmed straight
away, so the archive being restored is never deleted to make room for it.

The line under **return** shows the last archive this session and its kind, or why there's none. A notice, **save
backed up**, shows after each automatic one (**save archived** after **archive now**). Only `save-backups/` is kept to
100; the oldest go first. To restore one with the game closed instead, copy its contents back over the game's save
folder (on an emulator: right-click the game > *Open save data location*).

#### 2.7.2 Memfiles

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

Memfiles also bring back Nayru's Love (on with its time left, or off), Farore's Wind's warp point and sparkle, and the 8-unit void fence anchor (older `.szm` files without a saved fence trigger a fresh scan around Link's restored position instead of keeping a stale fence).

Memfiles are only written to or read from the SD card when you export or import one. A `.szm` file can be shared: it records the game version it was made on (1.0.0 or 1.1.1) and the sloperina version that wrote it, and is refused on another game version, by an older sloperina that can't read its format, or if it's damaged. Newer sloperina versions read memfiles made by older ones; a part an older file doesn't have (or a newer file has that this version doesn't know) is left as the map sets it. Memfiles exported by the early test builds of this feature (format 1) can't be imported. A memfile holds a whole save's GameData, so loading someone else's replaces your current game's progress in memory, just as loading your own does: the game's save files are only changed if the game saves afterwards.

Saving is refused during a cutscene, a game over, or while the game is paused (pause screen, map, item wheel); loading is refused during a cutscene or while paused. Loading during a game over is allowed (it reloads the map).

Caveats:
- Link climbing a ladder, vine or wall isn't restored: he's put back at the spot and drops.

### 2.8 Debug

**return to title screen**: back to the game's title screen from anything: ordinary play, a cutscene or other event, a game over, the pause screen or map (it presses B for them until they've closed), the game held by frame advance (it lets the game go). A map change already on its way (an exit, a warp) is let finish first, and a save being written too; a warp waiting for the next exit is cancelled. It leaves play the way the game's own Game Over > Quit does, by ending an event with the result "Title", so it usually takes one to two seconds. Nothing is saved: whatever isn't saved is lost, as with the pause screen's Return to Title. It gives up, with a notice, if the game hasn't left play within a minute. The free camera is turned off on the way. It can be given buttons in **settings > rebind commands** (under **debug**; unbound to start with).

**actors**: like gz's actors menu.

- **browse currently loaded actors**: the free camera over a beam on the ground, as when placing (below) but with nothing to place: for going from actor to actor, going to them and killing them. What **d-left** and **d-right** go through starts as **look at** says, under it: **all kinds** (enemies, NPCs, objects, items and chests; to start with), **enemies**, **npcs** or **objects** (chests among them). While browsing, **L** and **R** change it, moving on to the nearest of them at once. (Placing, they go through all kinds.) Leaving the beam on an actor (letting go of the stick) locks on to it, as **d-left** and **d-right** do, and the camera follows it as it walks (a guard on its patrol); moving the stick lets go. **Actor options**: the panel lists the switches the actor has, one under another with a checkbox each, off at first: a guard's **vision** and **patrol path**; any other actor the map gives a path to walk (a townsperson, a dog, a cucco, a moving platform: the layout links it to its rail) its **path**, drawn as a guard's is; and for anything Link can talk to or act on its **talk radius**, **z-target talk** and **target break line** (on something that can't be Z-targeted, a chest or a sign, it's where the A prompt goes as Link walks away: **open prompt break line**, **check prompt break line**; and it has no z-target switch) (named by what A does: a sign's **check radius**, a door's **open door prompt radius**, its A prompt's reach, apart from its **door open/close radius**), and a door's **door open/close radius** ([2.3.1](#231-environment)) and **close timer watch**: ticked, the **door closes** watch joins the watches (and the watches are turned on) for that door: the frames before it shuts once nobody holds it open (135 while someone does: it counts down from there and shuts at 0). A door counts in its own AI when it was opened for an NPC and in its opening event when Link opened it; the watch shows whichever is counting. The counts are looked for while the door is watched, so a new one can take a moment to show; **closed** before the door has been opened. Unticking takes the watch off; it's forgotten when the door goes (a map change). A cucco, a dog or a horse (Epona too) drowns in deep water and comes back, and has two more: **drown timer watches** and **respawn point**. **drown timer watches** puts four watches in the watches (and turns the watches on) for that animal, each one you can hide or take off in the watch list on its own:
  - **drown state**: what it's doing: **idle**; **drowning** (a horse or dog), or for a cucco in water over 0.3 m deep **in the water**, **looking for land** (it turns to the nearest land) and **jump 1 of 3** to **3 of 3** (it jumps for the land); **fading out**; **hidden**; **finding a safe spot** (a horse whose last landing isn't safe); **respawned** for a moment once it's back; **jumped out** (a cucco that reached the ground: it doesn't drown).
  - **drown timer**: the frames until it starts to fade out. A horse struggles for about 140 frames first. A cucco: what's left of the step it's at (in the water 30, looking for land up to 120), and **-** while it jumps (where each lands decides what comes next); still in the water after its jumps it starts to fade at once. A dog doesn't fade: the frames until it's put back.
  - **fade timer**: the fade out: how long it takes while it's still drowning, then what's left of it as it fades. A horse fades for about 14 frames and is then hidden; a cucco for its first 6 counts of sinking. **0** once it has faded; **no fade** for a dog.
  - **respawn in**: the frames until it's put back. A horse: from when it starts struggling; it's put back about 28 frames after its fade starts (its fade, then hidden for as long again). A cucco: **60 after its jumps** until it starts to sink, then counting down. A dog: the same as its drown timer.

  All in frames of the game (frame advance's): the game counts these in its own time (a horse's counts go down about 2.2 a frame), and the watches turn them into frames by how fast they've been going. The timers show **0** once their part is over and **-** while it's idle. Unticking takes the four off; they're forgotten when the animal goes (a map change). While a cucco drowns the game won't let Link lift it, and its **lift radius** and break line aren't drawn meanwhile. **respawn point** draws, in pink, where it comes back: a ring on the ground with a beam, and a line from the beam to the animal while it's away from it. A cucco comes back where it last landed safely (the game keeps that spot as it walks, and while Link carries it, where Link stands), put on the ground near it and dropped from above; a dog at its home (where it was placed or spawned); a horse where it last landed, if the game finds that spot safe (it checks when the horse drowns; if not, it looks for the nearest safe spot, up to 200 m away, which can't be shown beforehand). A horse that hasn't landed anywhere yet (spawned in the water) has none to show. Several animals' points can be shown at once. A door's menu also has **open door** while it's shut and **close door** while it's open (the pose of its panel, however it was opened; the item changes as the door does). **open door** opens it by itself, as it opens for an NPC, wherever Link is; **close door** shuts it at once, even with Link (or anyone) in the half disc that would hold it open, pushing whoever is in the way. Either is done once, and the door then goes on as it does (open, it shuts 135 frames after nobody is near). Neither needs the watch. **door open/close radius** also draws, in red, where the door pushes whatever is in its way as it shuts: a half disc as wide as the door, on the side it swings to. They're its own, whatever scene > environment says (that draws them for everything near). Its hitbox switches are the kinds it has ([2.3.1](#231-environment)): **attack hitboxes**, **bump boxes**, **hurtboxes**, **invincibility** (for something with hurtboxes that can be fought) and **collectables** (a rupee, a heart, a bug), each listed only when its bodies include one, whether it's on at the moment or not (an enemy's attack counts between swings). A chest, a fence or a sign has none. An actor with no switches at all has no **actor options** (**minus** does nothing). **minus** (**actor options** in the controls bar) opens a menu of them for that actor; **return** closes it and you're back browsing, on the same actor. They're forgotten when it goes (a map change). The browser also looks round its own spot, so an actor far from Link is drawn too. Under its name, what the map's layout says it holds: a chest's **contains** (the item it gives, by the game's name: `TBox_Rupee_Red`, `Item_Dungeon_SmallKey`, `Item_ScrubBusinessSeed` the business scrub seed) and, for the chests that check, what it gives instead when Link already has something (**TBox_Rupee_Red if Link has TBox_Shield_Deku**); a pot's, a rock's or a bush's **drops**. It's read from the layout the game has loaded, so spawned actors have none, and the treasure chest game's chests show what their layout says, which may not be what the game decides when it's played. Under its switches, a **position** heading with the actor's x, y and z, or, when the beam isn't on an actor, a **cursor** heading with the beam's spot (it follows the sticks). The explorer shows its cursor the same way, and the free camera a small panel with the camera's position. What NPCs hold (a guard's spear) isn't stepped to: it stands where its NPC does.
  - **A** goes to the actor: Link is put where it is, the way **load position** moves him, without changing your saved position. Inside something solid (a bed, a door) the game puts him straight back.
  - **Y** kills or despawns it, sending the actor the game's own death request, the one a fall into a bottomless pit sends: it fades out, with no drop. Where the game refuses it (bosses and the like, and some NPCs), where the actor has no death of its own, or where it's taken but the actor is still standing half a second later, the actor is despawned instead: the game's own delete, as its AI uses, with no death, drop or respawn flag. A despawned boss's fight won't finish as it should. Scenery, the game's system actors and Link are never touched. A killed or despawned actor may be back when the map reloads.
- **quick spawn**: the actors to make, by kind: **animals** (Epona, a cucco, a bug, a cow...), **enemies**,
  **characters** (the child's or the adult's, by Link's age), **objects** and **pickups**. Each starts with a short
  list of the likeliest; under it (all but **animals**), **all enemies**, **all characters**, **all objects** or
  **all items** has every one of that kind the game has (`Enemy_`, `Npc_`, `Obj_`, `Item_`: 341, 348, 997 and
  139; **character equipment**, a submenu of **characters** above **all characters**, has the 31 `Npc_Equipment_` ones, kept out of **all characters**). **name has** narrows it to the names with a piece you type (any case); five show at a time, with the arrows
  above and below them to scroll (held: it keeps going) and the scroll bar beside them, and the top arrow says which
  of how many are showing. **L** on one asks the game to make it, placed with the sticks first (see **spawn where**)
  or 2 m in front of Link and facing him; its pack is loaded if the map doesn't already have it. The game makes it the
  way its own AI does and it says "spawned" once it's there (usually in a few frames). It gets none of the placement
  data a map's own copy has (links, rails, switches), so some actors may stand idle. The `_Dictionary` actors are the enemy guide's models. **epona (owned)**, in **animals**, makes her your own horse, so she lets you ride
  her (child Link too, though his ride looks rough). Talking to a spawned character
  can start its own event, which may change flags or the save.
- **remove spawned**: deletes every actor made in this menu that's still in the map (they go with a map change
  anyway).
- **spawn where**: **placed with the sticks** (to start with) or **in front of Link**. Placed, the menu closes and the free camera looks down on a yellow beam standing on a ring, with an arrow for the way the actor will face, as furniture is placed in The Sims on a console. The spot starts 3 m in front of Link, or where the free camera is looking if it's already on.

  While placing or browsing, a panel at the top left names what's under the beam (with its life under the name if it can be killed, and **invincible** while it can't be hurt; nothing for an NPC, sign or Deku Scrub, which the game makes invincible from the start with its default life of 100) and what d-left and d-right go through, and the controls are shown in a band across the bottom of the screen with the game's own button glyphs, fading in and out with it (in gz's menu style they're in the panel instead):
  - the **left stick** moves the spot over the ground, as the camera faces; it keeps to the ground (up steps and slopes, off ledges) and turns orange where there's no ground under it, keeping its height
  - **d-up** and **d-down** move it up and down: it turns orange, keeps that height as the left stick moves it, and a line goes down from it to the ground under it
  - **L3** (pressing the left stick): back to Link, the spot on the ground where he stands and the camera with it
  - **R3** (pressing the right stick; **R3 hide controls** in the bar): hides the controls bar, the beam and the panel, for a clear picture; **R3** again shows them (the game's own HUD is left as its options have it)
  - the **right stick** swings the camera round the spot and up and down; **ZR** and **ZL** zoom in and out; holding **X** moves the spot (and its height) four times faster
  - **d-left** and **d-right** step through the actors of the kind shown within 40 m of the spot, nearest first: **d-right** on to the next, **d-left** back to the one before. The spot locks on to each, and the camera glides to it and follows it smoothly, so one that moves doesn't jerk the view. The left stick lets go of it, carrying on from where it is (the next press then starts from the actors near there). Navi and the ocarina's fairy are never in the list.
  - the ring turns red over an actor: the one locked on to, or one of any kind within 1 m of the beam. **Y** kills or despawns it (see above).
  - browsing: **A** goes to the actor locked on to or under the beam, or with none, to where the beam stands (Link moved there as **load position** moves him); you carry on browsing until **B**
  - placing: **L** and **R** turn the arrow, 15 degrees a press (it starts facing the camera), and **A** makes the actor there; you can carry on and make more
  - **B** finishes and gives the camera back (to Link, or to the free camera if it was on).

  The game gets no input meanwhile, as with the free camera. Opening the menu holds it until the menu's closed; turning the free camera off ends it. Remote control's `input` sticks and buttons work it too.

**free camera**: takes the picture off Link so you can fly it anywhere. Close the menu to fly, with the same controls as the explorer where they're alike, shown in a bar across the bottom of the screen: the left stick moves along the view, the right stick looks round, **d-up** rises, **d-down** sinks and holding **X** goes six times faster; **ZR** and **ZL** zoom in and out (the camera moving along its view); **A** goes back to Link (behind him, looking at him); **R3** hides the controls bar and shows it again; **L3** hands the controller to Link and back again (the bar says which L3 goes to next: **L3 control Link** while you're moving the camera, **L3 control camera** while you're controlling Link): with Link, the game gets every button but L3 and R3 (so A is his as ever; **R3** still hides the bar) and the camera stays where it is, so you can watch him from there; **Y** follows Link (**Y follow Link: on / off** in the bar): the camera keeps its angle and where it is from him and goes with him as he moves, with you controlling him or not, and moving the camera meanwhile changes where it keeps from him; **B** turns it off and gives the game's camera back, as it was (so does unticking it, or its bind). The game gets no input meanwhile, so Link stands where he was, and the module's other shortcuts (levitate, ragdoll, frame advance...) wait until it's off. What's drawn and culled is the free camera's view, and the watches, air walls and other overlays are drawn from it too. The world isn't loaded round it, though: the game streams the terrain and places the actors round Link, so far from him you see the far, simple terrain and none of the actors there. **R + right stick up** turns it on and off too (**settings > rebind commands**, under **debug**, **free camera on/off**; settings files from before it had a bind get it once), and remote control's `input` sticks and buttons fly it too.

**tests > dive tests**: **from heights** and **while moving** measure how deep Link goes into water by how he falls in. Start
one with Link swimming at the surface (facing open water for **while moving**) and leave the controls alone: it
measures the surface, drops him again and again from set heights (from heights: 1 to 192 above the surface; while moving:
16 and 96 above it, 0 to 13.6 units/s the way he faces), twice each, and writes each drop's fall time, speed
going in, and deepest point to `log.txt` ("dive test: ..."). Choose it again to stop.

#### 2.8.1 Heap

How much memory the game has and uses (refreshed every second), and **view the heap in memory**.

#### 2.8.2 Memory

gz's memory viewer.

| Item | What it does |
|---|---|
| **address** | **L** to type an address; D-pad left / right moves a page. |
| **view** | byte, halfword, word, float, pointer. |
| **go to** | Jump to: main, sloperinaGZ, link actor, link status, camera, game data, save strings. |
| **↑ ↓** | Scroll a row (hold **L** to keep scrolling). |
| *rows* | 8 bytes each. **L** edits a row: D-pad left / right moves along it, up / down changes the digit (written at once). On the row's address, **L** adds a watch there; in pointer view, **L** on a value goes where it points. **R + D-pad left** stops editing. |

Unreadable memory shows as `??`; read-only memory can't be changed. **Editing memory can crash the game.**

#### 2.8.3 Pause research

The game's own pause state, for development: values you can watch, change and log.

#### 2.8.4 Flag logging

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
change is also kept in the log menu: choose **event flags** as its filter (2.8.5).

Each row has the date and time (the console's clock, e.g. `13/11/2026 15:47:02`), the seconds since this session's recording
started, the frame, the map, Link's position, what Link was doing (`EvtTalk` while he talks), the
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

#### 2.8.5 Log

Under **log history**: the last 128 messages sloperinaGZ wrote, newest first, five at a time. **↑** above them and
**↓** below them scroll (hold **L** to keep going), and in the game's style a scroll bar beside them shows where you are.

| Item | What it does |
|---|---|
| **filter** `< all >` | Which messages: **all**, only **problems** (anything that couldn't be done), **event flags**, or one kind (warp, settings, notice... whatever kinds there are). |

**event flags** shows the flag changes recorded by flag logging (2.8.4), each with the date and time, e.g.
`13/11 15:47:02 HavePlayedEvent.Npc_Kokiri_Mido_Child_C1C2_TT 0 -> 1`. It keeps the last 128 flag changes in a list of
their own, so other messages don't push them out. Flags without a known name show their hash.

Everything is also written to **`sloperinaGZ/log.txt`** on the SD card (on an emulator, its virtual SD card;
Lichii build 150+: `%APPDATA%\lichii\sdmc\sloperinaGZ\log.txt`; Fritchii v5.8+: `%APPDATA%\fritchii\sdmc\sloperinaGZ\log.txt`; Eden and older Lichii / Fritchii: `%APPDATA%\eden\sdmc\sloperinaGZ\log.txt`), with the time since the game started and the game's frame counter (`[   12.345 f742] ...`: the **frame** watch's number, to match a line to a frame), about once a
second. It starts fresh each time the game starts. Send it along when you report a problem.

#### 2.8.6 Story

| Item | What it does |
|---|---|
| **all tutorials seen** | Marks every tutorial message as seen, so they stop popping up. |
| *each quest* | The quest's current stage; D-pad left / right (or **L**) changes it. It takes effect after a loading zone. |

### 2.9 Settings

The settings menu lists its menus: **menu settings**, **input display**, **levitate**, **frame advance** and **rebind
commands**, and has the **remote control** switch ([2.9.5](#295-remote-control)).

**menu settings**: **language** ([6](#6-languages)), **size** (auto, small 2x, medium 2.5x, large 3x: the menu, watches
and log), **background opacity** (0 to 100%, 100% at first; below 100% the game shows through, slightly behind) and
**hide in game menus** (on at first): no watches, input display or log while the game's pause screen (Plus) or map
(Minus) is open, so they don't cover it. They show again when it closes, and while this menu is open (to move them).
They stay over the item wheel unless **hide over item wheel** is on.

**hide over item wheel** (off at first): the same while the game's item wheel is up.

**hide world overlays over item wheel** (on at first): the overlays drawn in the world (air walls, void out, save
positions, the hookshot's hook) fade out while the game's item wheel is up, as they do for the pause screen and map,
and fade in again when it closes. They fade over a third of a second rather than going and coming back at once, so
opening and closing the wheel quickly doesn't make them flash. Off, they stay over the wheel as they were the moment
it opened (the game turns its camera for the wheel, so they no longer line up with the picture behind it). Enemy
health bars still go and come back at once.

**watches while exploring** and **input display while exploring** (both off at first): the watches and the input
display are hidden while the explorer, **browse currently loaded actors**, placing an actor or the free camera has the controller,
so they don't cover the picture or the controls bar. On, each stays on screen there. Either way they're back as soon
as the tool ends, and while this menu is open over it.

**switch keyboard** (on at first): naming something (an archive, a folder, a watch's label) opens the Switch's own
keyboard, with its controls: the left stick or D-pad between keys, **L** / **R** to move the cursor in the text, **B**
to delete, **Y** for a space, **+** to finish (touch and a USB keyboard work too). The game waits while it's up.
Only the characters the menu's font has are kept (others become `_`). Off, or if the Switch's keyboard can't be
shown, sloperinaGZ's own keyboard is used.

**watch box header** (on at first): a small **watches** header over the watches box, underlined.

#### 2.9.1 Input display

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

#### 2.9.2 Levitate

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

#### 2.9.3 Frame advance

| Item | What it does |
|---|---|
| **enabled** | Turns frame advance on ([4](#4-about-frame-advance)). Off by default; updating from 0.0.3 or older turns it off once. |
| **in pause screen** | Off (the default): while the game's pause screen or an item menu is open, the button is the game's and doesn't start frame advance. On: it starts frame advance there too. |
| **pause icon** | gz's pause icon at the top left while paused. |

Its buttons are the **frame advance** and **run again** commands (**L** and **L + L3** at first; **settings >
rebind commands**). Bound to one button, the game doesn't see that button while frame advance is on; a combination's buttons
stay the game's. Settings from before the commands with frame advance on **R** become **R** and **R + L3**.

#### 2.9.4 Rebind commands

gz's **commands** menu, here **rebind commands**: each of sloperinaGZ's controls and the buttons it's bound to, under **menu**, **frame
advance**, **link**, **scene**, **memfiles**, **watches and timer** and **debug**. The **scene** commands, **reload map** ([2.1.2](#212-scene-interaction)) and **end
cutscene** ([2.3](#23-scene)), the memfile commands and **debug**'s **return to title screen** ([2.8](#28-debug)) start out unbound. A bind is up to four buttons pressed in that order (**R**, then
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

#### 2.9.5 Remote control

**remote control** (off at first) lets a script or a tool on the PC use sloperinaGZ without the controller: it runs the
lines of a text file as if each were pressed or chosen in the menu. Nothing happens while it's off.

With it on, sloperinaGZ looks ten times a second for `sloperinaGZ/remote/cmd.txt` on the SD card (on an emulator, its
virtual SD card: a folder on the PC). It reads the file, deletes it (so the next can be written) and runs its lines in
order on the game's thread. What each line did is added to `sloperinaGZ/remote/out.txt` and to the log.
`python3 tools/remote.py "fire savepos" "wait 30" "fire loadpos"` writes the file and prints the answers; write
`cmd.txt` under another name and rename it, so it's never read half written.

| Line | What it does |
|---|---|
| `fire <command> [frames]` | A command of **rebind commands**, by its name in the settings file (`savepos`, `loadpos`, `ragdoll`, `levitate`, `timer`...), as if its buttons were pressed now; held for that many frames more (`rise`, `sink`, `hover`). |
| `do <menu path>` | Chooses a menu item: an action runs, a checkbox flips, an option moves to its next choice, a **return** goes back a level in the menu on screen. |
| `set <menu path> = <value>` | Sets a checkbox (`on` / `off`), an option (a choice's text or number) or a number. |
| `get <menu path>` | The item's value. |
| `list [<menu path>]` | A menu's items: number, kind, name and value. Without a path, the top menu. |
| `open [<menu path>]` | Opens the menu on the screen at that menu, or at that item of its menu with the cursor on it (without a path, the top menu): for a look at it, or a screenshot. |
| `menu` | What the menu on screen shows: `menu closed`, or `menu open path=[<menu path>] cursor=@<item number> kind=<its kind> editing=<0\|1> line=[<its line>]`. For a tool that presses the menu's own buttons and wants to know where they took it. |
| `wait <frames>` | The next line runs that many frames later. |
| `repeat <n>` ... `end` | Runs the lines between that many times (up to four inside one another): with `wait` and `input`, a sequence of any length. |
| `wait until <condition> [max <frames>]` | The next line runs once the condition holds (see below); an error after `max` frames (1,800 unless said), Held frames don't count: held by frame advance's buttons it waits on, and goes on when the game runs; held by the script's own `pause` or `step` (nothing else would let it go) it ends with an error (`talk`, `choose` and `place` too). |
| `repeat until <condition> [max <rounds>]` ... `end` | Runs the lines between until the condition holds at the `end` (so at least once); an error after `max` rounds (100 unless said). |
| `onerr stop` / `onerr go` | `stop`: the script ends at the first line that fails (with what that line was waiting for), and `out.txt` says `end (onerr stop)`. `go` (each script's start): it carries on. |
| `pause` | Holds the game as frame advance does (frame advance needn't be on). |
| `step [frames]` | Lets that many frames through (one unless said), then holds the game again. Pauses first if the game was running. |
| `run` | Lets the game run again. |
| `input <buttons> [ls <x> <y>] [rs <x> <y>] [for <frames>]` | Buttons and sticks for the game, on top of the controller's: names with `+` between (`A+ZL`, at most four), a number (`0x41`) or `-` for none; a stick is -32767 to 32767 each way. For one frame unless said; `for 0` lasts until the next `input`; `input off` ends it. The menu's shortcuts don't see these buttons: use `fire` for those. |
| `status` | The menu screen that's up (`screen`: `None` in play, `Loading`, `ItemMenu`, `GameOver`...), the item wheel (`wheel`: `closed`, `opening`, `open`), the message window (`dialog`: `closed`, `typing`, `waiting` for A, `turning` the page, `choices` appearing, `choice:<n>` with the cursor on option n) and its message (`msg`, e.g. `Npc_Hylia_Soldier_008_Child:Talk_03`), the event that's running (`event`, its current flow), the game's frame counter, the map, where Link is and which way he faces (`facing`, degrees), how he moves (`vy`, `hs`: his speed up and across), what he holds and has on ZR (`hand`, `zr`), whether ZL has a target (`lock`: 2 yes, 3 no), whether he's on the ground, his main and weapon action ids (`act`, `wpn`), whether the menu and the game's pause screen are open, whether the game is held, and the warp's state. `vy` and `hs` are the velocity as the game last set it, so they can keep a value while Link stands still (after a landing, for one). |
| `trace <frames>` / `trace off` | A line for each of the next frames, without holding the script up: `<line> t y= vy= hs= x= z= hand= zr= paused= lock= ground= act= wpn=`. |
| `actors [metres]` | The running actors within that distance of Link (6 unless said), nearest first, by the game's names, with where each is (`x y z`). |
| `spawn <actor> [metres]` | The game makes that actor that far in front of Link (2 m unless said), facing him, as **debug > actors > quick spawn** does; the line ends once it's running in the scene (`ok spawn <actor>: made after <n> frames`), or with `err` if the game refused it or it didn't appear within 600 frames. Only the names the game has (those in quick spawn's **all** lists, and the animals), in any case (`Enemy_Wolfos`). |
| `place <x> <y> <z> [facing]` | Puts Link on that spot of this map as **load position** does, and goes on once he's there and load position has let go of him (it loads the position again while it only found him; an error after 120 frames). With a facing (degrees, 0 = +z) he is turned to it; without, he faces as he did. |
| `place near <actor> [metres]` | Puts Link that far (1.2 m unless said) from the nearest actor of that name, on the side he's on, facing it. |
| `title` | Goes back to the game's title screen from whatever the game is doing (as **debug > return to title screen**), also with the game held (it's let go). `ok` once Link is gone and the game is at (or fading to) its title screen; `err` with the reason if it couldn't start (already at the title screen) or the game didn't leave play within a minute. Whatever isn't saved is lost, as with the pause screen's Return to Title. |
| `talk [max <frames>]` | Presses A through the message windows while the text types or waits, until the talk is over (no event and no window for 10 frames) or a choice comes up (a choice that's up when it starts doesn't count). |
| `choose <n>` | At a choice, moves the cursor to option n (0 = the first) and takes it. |
| `flag <field> [value]` | Reads a saved Bool or Int field (by its name, e.g. `Market_Item_Bottle_TutorialEvent_Started`, or a `0x` hash), or sets it. |
| `str <field> [value]` | The same for a String field; `-` sets it empty. |
| `event <name> [here \| actor <name> \| at <x> <y> <z> [facing]]` | Starts a game event by its name (a row of the game's `RSDB/EventSetting` table, e.g. `Item_Bottle_TutorialEvent_Animal_Insect_GetEvent`), as the game's own event request does. The starter, whose place an event like that one puts its actors at, is Link (`here`), the nearest actor of that name, or a spot; without one it's the world's origin. |
| `echo <text>` | Puts the text in `out.txt` (a marker). |
| `peek <hex address> [bytes]` | Memory at that address (rounded down to 8), as 8-byte words, 64 bytes or up to 256: for finding where the game keeps something. |
| `actor <name>` | Each actor of that name: its address, its layout instance id and how many paths it walks (the **path** option). |
| `drown <name \| 0xactor> [show]` | Puts the drown timer watches on the first animal of that name, or the actor at that address (as **drown timer watches** does, without adding them to the list), and says what it is (**cucco**, **dog**, **horse** or **doesn't drown**), what its drown state, drown timer, fade timer and respawn in watches read now and where it comes back. **show** flips its **respawn point** too. |
| `aidump <name>` | To the log (`sloperinaGZ/log.txt`), for each actor of that name: its drowning module's controller, unit and counters (their node indices and frames left), its home, its WarpPos and where it comes back. |
| `doortimer <name>` | Puts the door close watch on the first door of that name (as **close timer watch** does, without adding the watch to the list) and says what it reads now: the frames left, or **closed**. |
| `findref <hex>` | For finding what the game keeps: where Link's components (two pointers deep) hold that value, or for an actor's address also its components, attention sender and id. |
| `id <word>` | The word that starts this script's lines in `out.txt` (otherwise `-`), to tell them from another's. |

**Conditions** for `wait until` and `repeat until`: `control` (the game in play, no event, no message window, not
paused), `still` (control, and Link at rest for 8 frames: after a warp he slides in, and the item wheel won't open
until he has stopped), `moved [metres]` (control, and Link more than that far, 1 m unless said, from where the wait
began: a warp or a teleport has landed), `map <name>` (that map, loaded and in play), `dialog`, `nodialog`, `choice`,
`event`, `noevent`, `paused`, `unpaused`, `wheel` (the item wheel open and taking the stick), `zr <item number>`
(`status` shows the number), `actor <name> [within <metres>]` (10 m unless said) and `value <menu path> = <value>`
(the item's value as `get` shows it: the whole of it, its number, or the name in its brackets, `bug` for
`10 (bug)`).

A **menu path** is the names from the top menu down with `/` between them (or, starting `~/`, from the menu on the screen: a guard's menu opened from the actor browser, `~/vision`), in any case: `cheats/infinite magic`,
`scene/end cutscene`, `settings/frame advance/enabled`. `name#2` is the second item of that name in its menu and `@7`
the menu's item number 7 (as `list` numbers them), for lines with no name. The names are the English ones whatever the
language. An item that opens something to type or pick in (a keyboard, a list) needs the menu and isn't run.

Each line of `out.txt` is `<frame> <id> <line number> ok <what happened>` or `... err <why not>`, with the game's
frame counter when it ran, and `<frame> <id> end` closes a script. A script waits where a line does (`wait`, `step`,
`pause`, `run`), so "the same input on the same frame after loading a position" is a script, not a matter of timing
on the PC: `fire loadpos`, `wait 60`, `pause`, `input A for 1`, `step 30`, `status`. While the game is held `wait`
can't pass (no frames do: use `step`), and a `fire` takes effect on the first frame let through. A script that ends
with the game held leaves it held for the next one; `run`, or turning **remote control** off, lets it go on.

At most 65,535 bytes a script, and 64 of its lines a frame. Blank lines and lines that start with `#` are skipped.

### 2.10 About

The name and version, the build date ("Build Date: ..."), and the website.

## 3 On the screen

| What | Where | Notes |
|---|---|---|
| Menu | Top left, under the D-pad icon | Always on top. |
| Watches | The corner you choose, or anywhere | [2.6](#26-watches) |
| Log | Bottom right | What you just did; stays a second, then fades. gz's grey text with a shadow. |
| Input display | Bottom left, or the corner you choose, or anywhere, or attached above or below the watches | [2.9.1](#291-input-display); in the bottom left, it moves above the watches if they're there too. |
| Pause icon | Top left | Two bars: paused by frame advance. |
| Air walls | Over the game | [2.3.1](#231-environment) |
| Guard vision | Over the game | [2.3.1](#231-environment) |
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
- **The free camera and the actor browser work while paused** (**debug > free camera** or its bind, **debug > actors
  > browse currently loaded actors**, the explorer): the game goes on drawing with its world stopped (the game's
  own pause, as its pause screen's, made to leave its camera running), so you can fly the camera and step from actor
  to actor with every control as when the game runs, and nothing in the world moves meanwhile: Link, the actors,
  a boomerang in flight stay exactly where they were. With the free camera, frame advance's button works as ever:
  a tap plays one frame (the world moves on by just that one, the camera staying where you put it), held they play
  one after another; and with the game running, a tap pauses it with the camera kept. (In the actor browser it
  waits, as its **L** and **R** are its own, and so does **run again**, whose **L3** is the camera's.) Leaving it
  (**B**, or unticking the free camera) holds the game again where it was, with its own view back; the next tap
  plays the very next frame. **One catch:** an actor that was off the screen when the game paused isn't
  drawn while it's paused (what's drawn is decided in each actor's own update, which is what's stopped); the browser
  still steps to it and shows its options, and it's back as soon as a frame plays.

## 5 Settings file

Settings are saved on the SD card in `sloperinaGZ/settings.ini` (on an emulator, its virtual SD card) a couple of
seconds after you close the menu. It's plain `key=value` text: cheats, watches (which, in what order, their types,
names and whether shown), the watches box, menu size, background, hiding in game menus, language, input display,
levitate, frame advance, save position, flag logging, void out, remote control and the commands' binds. Delete it to go back to the defaults. Your game saves are never touched.

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
