/* Commands from a file, for scripts and tools: with settings > remote control on, a script written to
 * sd:/sloperinaGZ/remote/cmd.txt is taken (the file is deleted: the next may be written) and run on the game's
 * thread, a line at a time, as if each were pressed or chosen in the menu. What each line did goes to
 * sd:/sloperinaGZ/remote/out.txt and the log. On an emulator the SD card is a folder on the PC, so anything that
 * can write a file can drive the module; tools/remote.py does.
 *
 * A script's lines (blank ones and # comments are skipped):
 *   id <word>                  the word that starts this script's lines in out.txt (otherwise "-")
 *   fire <command> [frames]    a command of settings > rebind commands, by its settings.ini key (savepos,
 *                              loadpos, ragdoll, levitate...), held for that many frames more (rise, sink)
 *   do <menu path>             choose a menu item: an action runs, a checkbox flips, an option moves on
 *   set <menu path> = <value>  a checkbox (on / off), an option (its text or number) or a number
 *   get <menu path>            its value and its line as the menu shows it
 *   list [<menu path>]         a menu's items: number, kind, name, value
 *   open [<menu path>]         the menu on screen at that menu (or at that item of its menu), for a look at it
 *   wait <frames>              the next line runs that many frames later (ended, with an error, if the game is
 *                              held meanwhile: no frames pass then)
 *   wait until <condition> [max <frames>]
 *                              the next line runs once the condition holds (control, still, moved, map, dialog,
 *                              nodialog, choice, event, noevent, paused, unpaused, wheel, zr, actor, value: see
 *                              the manual); an error after max frames (1800); held frames don't count, and if
 *                              the script's own pause holds the game it ends with an error
 *   repeat <n> ... end         the lines between, that many times (up to four inside one another); with wait
 *                              and input, a sequence of any length
 *   repeat until <condition> [max <rounds>] ... end
 *                              the lines between until the condition holds at the end (100 rounds unless said)
 *   onerr stop | go            stop: the script ends at the first line that fails; go (each script's start):
 *                              it carries on
 *   pause                      hold the game, as frame advance does (it needn't be on)
 *   step [frames]              let that many frames through (pausing first), then hold again
 *   run                        let the game run again
 *   input <buttons> [ls <x> <y>] [rs <x> <y>] [for <frames>]
 *                              buttons and sticks for the game, on top of the controller: "A+ZL", "0x41" or
 *                              "-" for none; sticks -32767..32767; for 1 frame unless said (for 0: until the
 *                              next input line). "input off" ends it.
 *   status                     the menu screen, the item wheel, the message window and its message, the running
 *                              event, the frame, the map, where Link is and which way he faces, how he moves (vy, hs:
 *                              his speed up and across), what he holds and has on ZR, whether ZL has a target
 *                              (lock: 2 yes, 3 no), whether he's on the ground, his main and weapon action ids,
 *                              whether the game is held; vy to wpn come from the build's offsets (z64hd.h), "-"
 *                              and -1 where it has none. vy and hs are the actor's velocity as the game last set
 *                              it: it can keep a value while Link stands still (after a landing, for one)
 *   trace <frames> | off       a line for each of the next frames: "<line> t y= vy= hs= x= z= hand= zr= paused=
 *                              lock= ground= act= wpn="
 *   place <x> <y> <z> [facing] Link to that spot of this map, facing that way (degrees, 0 = +z), as load position
 *                              moves him; goes on once he's there (an error after 120 frames)
 *   place near <actor> [metres] Link that far (1.2 m) from the nearest actor of that name, facing it
 *   actors [metres]            the running actors within that distance of Link (6 unless said), nearest first,
 *                              with where each is
 *   talk [max <frames>]        A through the message windows until the talk is over or a choice comes up
 *   choose <n>                 at a choice, the cursor to option n (0 the first), and take it
 *   flag <field> [value]       a saved Bool or Int field (by name or 0x hash): read it, or set it
 *   str <field> [value]        the same for a String field ("-" empties it)
 *   event <name> [here | actor <name> | at <x> <y> <z> [facing]]
 *                              start a game event by name (warp.c's request), with that starter
 *   title                      back to the game's title screen, the way Game Over's Quit leaves play (FINDINGS
 *                              9.26): ok once the game runs the 30-frame event it leaves by, after which it fades
 *                              to the title; err if it can't now (paused, a cutscene, a map loading...). What
 *                              isn't saved is lost, as with the pause screen's Return to Title
 *   echo <text>                the text, into out.txt
 *   actor <name>               debugging: the addresses of the actors with that name
 *   attention                  debugging: Link's attention object and its do-action slot's address (FINDINGS 9.37)
 *   findref <hex>              debugging: where Link's components (two pointers deep) hold that value, or, for an
 *                              actor's address, its components, attention sender or id (+0x18)
 *   frametrace                 debugging: the game's next frame of NVN commands (render targets, draws, submits) to
 *                              sloperinaGZ/dump/frametrace.txt; only in a build with FRAMETRACE_HOOKS (nvn.c)
 *   texdump <hex texture> [bpp] debugging: that texture's rows at the next present to sloperinaGZ/dump/texture.bin
 *   hitlist                    debugging: the hitboxes drawn last frame: kind, actor, its state, layer, where
 *   pausetype [<name> <+n|-n>] debugging: the game's pause types and their counts, or add to one (FINDINGS 9.1);
 *                              pausetype <name> masks <hex> <hex> <hex>: what it stops, set (FINDINGS 9.41)
 *   equip shield <actor>       debugging: Link's shield (Weapon_Shield_Deku, _Hylian, _Mirror) worn, at once
 *   button <0..3> [<item>]     debugging: an item button's choice (murmur3), or put an item on it (2 is ZR)
 *   rtlist                     debugging: the colour targets the game bound in its last frame, and how often
 *   rtbinds                    debugging: the HUD's picture and how often the world's overlays went under the HUD
 *   hudtest on | off | <hex target> [<hex cmd>] <n>
 *                              debugging: a test rectangle in the world under the HUD (on), or a magenta clear at the
 *                              nth bind of a target in a frame (FINDINGS 9.40)
 *   physdump [name]            debugging: the Havok worlds' bodies, their shapes and what they point to, and the
 *                              actors' heads and components, raw, to sd:/sloperinaGZ/dump/<name>.phd (FINDINGS 9.39;
 *                              records: u64 address, u32 size, u32 tag, the bytes)
 * A menu path is the names from the top menu down (or, starting "~/", from the menu on screen), "/" between them, any case ("cheats/infinite magic",
 * "scene/end cutscene"); "name#2" is the second item of that name, "@7" the menu's item number 7 (list shows
 * the numbers), for lines with no name.
 *
 * out.txt gets one line per script line: "<frame> <id> <line number> ok|err <text>", and "<frame> <id> end"
 * when the script is done. */
#ifndef REMOTE_H
#define REMOTE_H

#include <stdbool.h>

extern bool remote_enabled; /* settings > remote control */

/* Once a frame on the game's thread, after the controller's read (held: false), and from frame advance's hold
 * while the game is held (held: true). */
void remote_frame(bool held);

/* For frame advance (ootgz.c): the script asked to hold the game; to let one frame through (true once per
 * frame of a step); to run again. Each is taken by the call. */
bool remote_take_pause(void);
/* the same while frame advance's camera tool (the free camera, the placer) has the world stopped: the game isn't held
 * then, but nothing moves, so a script's pause is done at once (else it would wait for ever) */
void remote_paused_by_tool(void);
bool remote_take_step(void);
bool remote_take_run(void);

#endif
