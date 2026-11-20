/* gz's commands and their button binds (gz's input.c / gz_command.c / settings > rebind commands): each of the module's
 * controls is a command with a bind of up to four buttons, pressed in that order, which settings > rebind commands can
 * change. Evaluated as gz does once a frame (input_update calls binds_update):
 *   - a bind's buttons go down in its order; one let go ends it;
 *   - another button pressed while it's in progress (some of its buttons down, not yet all) cancels it (the
 *     sticks' direction bits don't: they wobble as the stick is used); once active, it stays so until let go;
 *   - of two active binds, one whose buttons are all part of the other's (R + d-left holds d-left) gives way.
 * Buttons are input.h's bit numbers: A..d-pad, the sticks' directions, SL, SR. */
#ifndef BINDS_H
#define BINDS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  /* the menu */
  CMD_MENU,        /* show/hide the menu */
  CMD_RETURN,      /* up a level (closes at the top) */
  CMD_BACK,        /* the same, a second bind (B) */
  CMD_UP, CMD_DOWN, CMD_LEFT, CMD_RIGHT,
  CMD_SELECT,
  /* frame advance */
  CMD_ADVANCE,     /* tap: pause; tap: a frame; held: frames */
  CMD_RUN,         /* run again */
  /* Link */
  CMD_SAVEPOS, CMD_LOADPOS,
  CMD_RAGDOLL,
  CMD_LEVITATE,    /* levitate on / off */
  CMD_RISE, CMD_SINK, CMD_HOVER, /* while levitating: up, down, hold the height */
  /* the scene: unbound until given buttons in settings > rebind commands */
  CMD_RELOAD_MAP,  /* link > reload map */
  CMD_END_CUTSCENE, /* scene > end cutscene */
  /* memfiles */
  CMD_MEMFILE_SAVE, CMD_MEMFILE_LOAD, /* the selected slot */
  CMD_MEMFILE_NEXT, CMD_MEMFILE_PREV, /* select the next / previous slot, wrapping */
  /* watches and the timer */
  CMD_WATCHES, CMD_TIMER, CMD_RESET_TIMER, CMD_RESET_LAG, CMD_RESET_JS,
  CMD_COUNT
} command_t;

enum {
  CMD_GROUP_MENU, CMD_GROUP_FRAME_ADVANCE, CMD_GROUP_LINK, CMD_GROUP_SCENE, CMD_GROUP_MEMFILES, CMD_GROUP_WATCHES,
  CMD_GROUPS
};

typedef struct {
  const char *name;  /* as settings > rebind commands shows it */
  uint8_t group;     /* CMD_GROUP_* */
  bool required;     /* can't be left unbound: the menu couldn't be used */
  bool any_order;    /* its buttons in any order (run again: L3 then L, with the menu open, so L doesn't select) */
} command_info_t;

extern const command_info_t command_info[CMD_COUNT];
extern const char *const command_keys[CMD_COUNT]; /* the settings file's bind_<key>=, {key} in menu text */
extern const char *const command_group_names[CMD_GROUPS];

/* a bind: byte i is the i-th button's bit number, BIND_END after the last (all BIND_END: unbound) */
#define BIND_MAX 4
#define BIND_END 0xFFu
#define BIND_NONE 0xFFFFFFFFu
extern uint32_t binds[CMD_COUNT];
uint32_t bind_default(command_t cmd);
void binds_reset(void); /* every bind back to its default */
int bind_length(uint32_t bind);
int bind_button(uint32_t bind, int i); /* its i-th button's bit number (i < bind_length) */
uint32_t bind_mask(uint32_t bind); /* its buttons as input.h's bits */

/* names: "R + rs-down", "unbound" (the words nativeui.c draws as the game's button glyphs) */
void bind_name(uint32_t bind, char *buf, int len);
/* parse a name as bind_name writes it (any case; "+" between buttons); false if it isn't one */
bool bind_parse(const char *text, uint32_t *out);

/* once a frame, after the controller's read (input.c) */
void binds_update(uint32_t held, uint32_t pressed, uint32_t released);
extern bool binds_enabled; /* off while a bind is recorded: no command happens */
/* whether a command applies now (gz's input_bind_set_disable): one that doesn't is never active, and so never
 * makes another give way (L + L3 doesn't stop L3 toggling levitate while frame advance is off) */
void binds_set_enabled(command_t cmd, bool enabled);

bool cmd_pressed(command_t cmd);   /* became active this frame */
bool cmd_repeat(command_t cmd);    /* became active, or held long enough to repeat (menu navigation) */
bool cmd_active(command_t cmd);    /* active: all its buttons held, in order, nothing else pressed since */
bool cmd_cancelled(command_t cmd); /* stopped being active (or in progress) this frame by another button */
bool cmd_held(command_t cmd);      /* all its buttons are held, in any order (steady controls, modifiers) */
uint32_t cmd_mask(command_t cmd);  /* its buttons */

/* Menu text with the binds in it, into out: "{key}" is the command's bind's name ("save position ({savepos})"),
 * and when the menu's own binds have been changed, their default names in hints ("L:", "d-up/down", "B",
 * "R + d-left") are theirs. Returns out, or `in` itself when there's nothing to change. */
const char *binds_expand(const char *in, char *out, int len);

/* settings > rebind commands: record a new bind for cmd, starting now (its row was just chosen).
 * binds_record_frame runs it, once a frame, until it returns false: let go of everything, press the new buttons
 * in order and let go, then select (as bound now) keeps it and back or return leaves the old one. Holding the
 * button that chose the row at the start unbinds instead; nothing pressed for a while gives up. */
void binds_record_start(command_t cmd);
bool binds_record_frame(void);
bool binds_recording(command_t cmd);
const char *binds_record_text(void); /* what's recorded so far, while recording */
const char *binds_record_hint(void); /* what to do now (in English, for the menu's hint) */
/* set when a bind changes (recorded, unbound, defaults): settings.c saves at once, even with the menu open */
extern bool binds_changed;

#endif
