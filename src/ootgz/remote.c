/* Commands from a file (remote.h): the script's lines run on the game's thread from the frame hook, through the
 * same code the controller reaches: binds_fire for the commands, the menu items' own callbacks for the rest. */
#include "remote.h"

#include <stdarg.h>
#include <stdint.h>

#include "actornames.h"
#include "binds.h"
#include "drownview.h"
#include "enemyhp.h"
#include "eventskip.h"
#include "gamedata.h"
#include "input.h"
#include "pausemgr.h"
#include "link.h"
#include "menu.h"
#include "levitate.h"
#include "menus.h"
#include "frametrace.h"
#include "nvn.h"
#include "physdump.h"
#include "navstate.h"
#include "position.h"
#include "pouch.h"
#include "sdfs.h"
#include "settings.h"
#include "guardview.h"
#include "hitview.h"
#include "interactview.h"
#include "sys.h"
#include "title.h"
#include "warp.h"
#include "watches.h"
#include "z64hd.h"

#define REMOTE_DIR SD_DIR "/remote"
#define CMD_FILE REMOTE_DIR "/cmd.txt"
#define OUT_FILE REMOTE_DIR "/out.txt"

#define SCRIPT_MAX 65536  /* a script's text */
#define LINE_MAX_LEN 512  /* one line of it */
#define REPEAT_DEPTH 4    /* repeats inside repeats */
#define OUT_MAX 8192      /* results waiting to be written */
#define POLL_CALLS 6      /* look for cmd.txt every this many calls (10 times a second) */
#define LINES_PER_CALL 64 /* at most this many lines a frame: a long script doesn't hold the game up */
#define STICK_MAX 32767

bool remote_enabled;
static bool tool_paused(void); /* a camera tool has frame advance's world stopped (remote_paused_by_tool) */

static char script[SCRIPT_MAX];
static size_t script_len, cursor;
static int line_no;
static bool running;
static bool stop_on_error; /* onerr stop: the script ends at the first line that fails */
static bool stop_now;      /* ... and one has */
static char script_id[24] = "-";

/* the repeats the script is inside: where each one's lines start, and the rounds it still has */
static struct {
  size_t cursor;
  int line_no, left;
} repeats[REPEAT_DEPTH];
static int repeat_depth;

/* what the line being run waits for */
static int wait_left;               /* wait: frames still to go */
static bool pause_wanted;           /* pause: for frame advance to take */
static bool pause_waiting;          /* ... and until it holds the game */
static int steps_left;              /* step: frames still to let through */
static bool stepping;               /* ... and until the game is held again after them */
static bool script_held;            /* the game is held by this script's pause or step (not by a hand) */
static bool run_wanted, run_waiting; /* run: the same two */
static bool title_waiting;          /* title: until the game has started the event or it failed */

static int trace_left;               /* trace: frames it still reports */

static uint16_t fire_pending[CMD_COUNT]; /* fire while the game is held: given on the next frame (1 + frames held) */
static int input_frames;                 /* frames the script's input still lasts (0: until changed) */

/* --- text ------------------------------------------------------------------------------------ */

static char lowercase(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c; }
static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static char *skip_spaces(char *p)
{
  while (is_space(*p))
    p++;
  return p;
}

static void trim_end(char *s)
{
  size_t n = strlen(s);
  while (n && is_space(s[n - 1]))
    s[--n] = 0;
}

/* The next word of *p (ended in place), or "" at the end; *p moves past it. */
static char *take_word(char **p)
{
  char *start = skip_spaces(*p);
  char *end = start;
  while (*end && !is_space(*end))
    end++;
  if (*end)
    *end++ = 0;
  *p = end;
  return start;
}

static bool same(const char *a, const char *b)
{
  while (*a && lowercase(*a) == lowercase(*b)) {
    a++;
    b++;
  }
  return !*a && !*b;
}

/* "12", "-3", "0x1f" */
static bool parse_int(const char *s, int32_t *out)
{
  bool negative = *s == '-';
  if (negative || *s == '+')
    s++;
  int64_t value = 0;
  int digits = 0;
  if (s[0] == '0' && lowercase(s[1]) == 'x') {
    for (s += 2; *s; s++, digits++) {
      char c = lowercase(*s);
      int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
      if (digit < 0 || value > UINT32_MAX)
        return false;
      value = value * 16 + digit;
    }
  } else {
    for (; *s; s++, digits++) {
      if (*s < '0' || *s > '9' || value > UINT32_MAX)
        return false;
      value = value * 10 + (*s - '0');
    }
  }
  if (!digits || value > UINT32_MAX)
    return false;
  *out = (int32_t)(negative ? -value : value);
  return true;
}

/* A name as a script gives it: lower case, single spaces, without the "(... {bind} ...)" some labels end in
 * ("save position ({savepos})" is "save position"), and only what prints. */
static void plain_name(const char *in, char *out, size_t cap)
{
  size_t o = 0;
  bool space = false;
  for (size_t i = 0; in && in[i]; i++) {
    char c = in[i];
    if (c == '(') {
      size_t end = i;
      bool has_bind = false;
      while (in[end] && in[end] != ')') {
        if (in[end] == '{')
          has_bind = true;
        end++;
      }
      if (has_bind && in[end]) {
        i = end;
        continue;
      }
    }
    if (is_space(c) || c == '\n') {
      space = o > 0;
      continue;
    }
    if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x7F) /* the menu's own glyphs */
      continue;
    if (space && o + 1 < cap)
      out[o++] = ' ';
    space = false;
    if (o + 1 < cap)
      out[o++] = lowercase(c);
  }
  out[o] = 0;
}

/* --- results --------------------------------------------------------------------------------- */

static char out[OUT_MAX];
static size_t out_len;

static void out_flush(void)
{
  if (!out_len)
    return;
  fs_file_t file;
  if (fs_open(&file, OUT_FILE, FS_WRITE | FS_ALLOW_APPEND) != 0) {
    fs_create_dir(REMOTE_DIR); /* fails harmlessly if it exists */
    fs_create_file(OUT_FILE, 0);
    if (fs_open(&file, OUT_FILE, FS_WRITE | FS_ALLOW_APPEND) != 0) {
      out_len = 0;
      return;
    }
  }
  int64_t size = 0;
  fs_size(&size, file);
  fs_write_option_t flush = { FS_WRITE_FLUSH };
  fs_write(file, size, out, out_len, &flush);
  fs_close(file);
  out_len = 0;
}

/* one line of out.txt: "<frame> <id> <text>" */
static void out_line(const char *text)
{
  char line[480];
  int n = snprintf(line, sizeof(line), "%u %s %s\n", (unsigned)frame_counter(), script_id, text);
  if (n < 0)
    return;
  if ((size_t)n >= sizeof(line)) {
    n = (int)sizeof(line) - 1;
    line[n - 1] = '\n';
  }
  if (out_len + (size_t)n > sizeof(out))
    out_flush();
  memcpy(out + out_len, line, (size_t)n);
  out_len += (size_t)n;
}

/* the result of the line being run: "<line number> ok|err <text>", to out.txt and the log */
static void result(bool ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void result(bool ok, const char *fmt, ...)
{
  char text[512]; /* status's line: about 330 characters, more with a long message id and event */
  int n = snprintf(text, sizeof(text), "%d %s ", line_no, ok ? "ok" : "err");
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(text + n, sizeof(text) - (size_t)n, fmt, ap);
  va_end(ap);
  trim_end(text);
  out_line(text);
  log_printf("remote: %s %s", script_id, text);
  if (!ok && stop_on_error && running)
    stop_now = true;
}

/* --- menu items by name ---------------------------------------------------------------------- */

static const char *const kind_names[] = { "menu", "action", "check", "option", "number", "text", "back" };

static bool has_name(const menu_item_t *item) { return item->label && item->label[0] && item->kind != MI_BACK; }

/* After a part's name in a path: an optional "#n" (which of that name), then the "/" before the next part, or the
 * end. Gives n and how much was read; false if something else follows (the name was only the start of a longer one). */
static bool part_end(const char *after, int *nth, size_t *used)
{
  const char *p = after;
  *nth = 1;
  if (*p == '#') {
    int n = 0;
    for (p++; *p >= '0' && *p <= '9' && n < 10000; p++)
      n = n * 10 + (*p - '0');
    if (n < 1)
      return false;
    *nth = n;
  }
  while (*p == ' ')
    p++;
  if (*p == '/')
    p++;
  else if (*p)
    return false;
  while (*p == ' ')
    p++;
  *used = (size_t)(p - after);
  return true;
}

/* The item of `menu` that the start of `rest` names (a path from here on, as plain_name leaves it): "@7" is item
 * 7; otherwise the longest name that fits ("start/stop timer" before "start", as names may have a "/" in them),
 * "name#2" the second item of that name. Gives how much of `rest` it took, with the "/" after it. */
static menu_item_t *match_item(menu_t *menu, const char *rest, size_t *used)
{
  int nth = 1;
  size_t tail = 0;
  if (rest[0] == '@') {
    int index = 0;
    const char *p = rest + 1;
    if (*p < '0' || *p > '9')
      return 0;
    for (; *p >= '0' && *p <= '9' && index < 100000; p++)
      index = index * 10 + (*p - '0');
    if (index >= menu->n_items || !part_end(p, &nth, &tail) || nth != 1)
      return 0;
    *used = (size_t)(p - rest) + tail;
    return &menu->items[index];
  }
  char name[96];
  size_t best = 0;
  int best_nth = 1;
  for (int i = 0; i < menu->n_items; i++) {
    if (!has_name(&menu->items[i]))
      continue;
    plain_name(menu->items[i].label, name, sizeof(name));
    size_t len = strlen(name);
    bool fits = len > best;
    for (size_t k = 0; fits && k < len; k++)
      fits = rest[k] == name[k];
    if (fits && part_end(rest + len, &nth, &tail)) {
      best = len;
      best_nth = nth;
      *used = len + tail;
    }
  }
  for (int i = 0; best && i < menu->n_items; i++) {
    if (!has_name(&menu->items[i]))
      continue;
    plain_name(menu->items[i].label, name, sizeof(name));
    bool is_it = strlen(name) == best;
    for (size_t k = 0; is_it && k < best; k++)
      is_it = rest[k] == name[k];
    if (is_it && --best_nth == 0)
      return &menu->items[i];
  }
  return 0;
}

/* Follow a path from the top menu (or, starting "~", from the menu on screen). Gives the menu the last part is in and
 * its item (0 for the empty path: the top menu itself); returns what's wrong, or 0. */
static const char *resolve(const char *path, menu_t **menu_out, menu_item_t **item_out)
{
  static char problem[200];
  char plain[200];
  plain_name(path, plain, sizeof(plain));
  const char *rest = plain;
  menu_t *menu = &menu_root;
  menu_item_t *item = 0;
  if (*rest == '~') { /* the menu on screen: one opened from elsewhere (a guard's, from the actor browser) */
    if (!menu_current) {
      snprintf(problem, sizeof(problem), "no menu on screen for ~");
      return problem;
    }
    menu = menu_current;
    for (rest++; *rest == '/'; rest++)
      ;
  }
  while (*rest) {
    if (item) { /* the part before this one has to be a menu */
      if (item->kind != MI_SUBMENU || !item->submenu) {
        snprintf(problem, sizeof(problem), "\"%s\" isn't a menu", item->label ? item->label : "?");
        return problem;
      }
      menu = item->submenu;
    }
    size_t used = 0;
    item = match_item(menu, rest, &used);
    if (!item) {
      snprintf(problem, sizeof(problem), "no \"%.120s\" in %.40s", rest, menu->title ? menu->title : "the menu");
      return problem;
    }
    rest += used;
  }
  *menu_out = menu;
  *item_out = item;
  return 0;
}

static void choice_name(menu_item_t *item, int choice, char *buf, size_t cap)
{
  char raw[96] = "";
  if (choice >= 0 && choice < item->n_choices) {
    if (item->choices)
      snprintf(raw, sizeof(raw), "%s", item->choices[choice]);
    else if (item->choice_text)
      item->choice_text(item, choice, raw, sizeof(raw));
  }
  plain_name(raw, buf, cap);
}

/* an item's value as text: "on", "2 (medium)", "150", a read-only line's text; "" for an action or a menu */
static void item_value(menu_item_t *item, char *buf, size_t cap)
{
  char text[160] = "";
  buf[0] = 0;
  switch (item->kind) {
    case MI_CHECK:
      if (item->get_check)
        snprintf(buf, cap, "%s", item->get_check(item) ? "on" : "off");
      break;
    case MI_OPTION:
      if (item->get_option) {
        int choice = item->get_option(item);
        choice_name(item, choice, text, sizeof(text));
        snprintf(buf, cap, "%d (%s)", choice, text);
      }
      break;
    case MI_NUMBER:
      if (item->get_num) {
        int32_t number = item->get_num(item);
        if (number == NUM_UNKNOWN)
          snprintf(buf, cap, "unknown");
        else
          snprintf(buf, cap, "%d", (int)number);
      }
      break;
    default: /* a text line, or another item whose line is written as it's shown (a list's rows and arrows) */
      if (item->text) {
        item->text(item, text, sizeof(text));
        plain_name(text, buf, cap);
      }
      break;
  }
}

/* Run an item's callback as the menu would on L, with the menu closed. An item that opens something to type
 * or pick in (a keyboard, a list) needs the menu: what it opened is closed again. */
static const char *choose(menu_item_t *item)
{
  bool was_open = menu_current != 0;
  switch (item->kind) {
    case MI_ACTION:
    case MI_NUMBER:
      if (item->action)
        item->action(item);
      else if (item->press)
        item->press(item, 0); /* a list's scroll arrow, say */
      else
        return "nothing to do on it";
      break;
    case MI_CHECK:
      if (!item->get_check || !item->set_check)
        return "can't be changed";
      item->set_check(item, !item->get_check(item));
      break;
    case MI_OPTION: {
      if (!item->get_option || !item->set_option || item->n_choices < 1)
        return "can't be changed";
      int choice = item->get_option(item);
      item->set_option(item, choice < 0 ? 0 : (choice + 1) % item->n_choices);
      break;
    }
    case MI_SUBMENU:
      return "a menu: list it, or name an item in it";
    case MI_BACK: /* return: up a level in the menu on screen (closing it from a menu with none over it) */
      if (!menu_current)
        return "no menu on screen to go back in";
      menu_back();
      return 0;
    default:
      return "not something to choose";
  }
  if (!was_open && (menu_current || menu_editing)) {
    menu_close();
    return "it opened something to type or pick in: that needs the menu";
  }
  return 0;
}

static const char *assign(menu_item_t *item, char *value)
{
  int32_t number = 0;
  bool is_number = parse_int(value, &number);
  switch (item->kind) {
    case MI_CHECK: {
      bool on = same(value, "on") || same(value, "true") || same(value, "yes") || (is_number && number == 1);
      bool off = same(value, "off") || same(value, "false") || same(value, "no") || (is_number && number == 0);
      if (!item->set_check || (!on && !off))
        return "a checkbox: on or off";
      item->set_check(item, on);
      return 0;
    }
    case MI_OPTION: {
      if (!item->set_option)
        return "can't be changed";
      int choice = is_number ? number : -1;
      char wanted[96], name[96];
      plain_name(value, wanted, sizeof(wanted));
      for (int i = 0; choice < 0 && i < item->n_choices; i++) {
        choice_name(item, i, name, sizeof(name));
        if (same(name, wanted))
          choice = i;
      }
      if (choice < 0 || choice >= item->n_choices)
        return "not one of its choices (get shows the current one, by number and name)";
      item->set_option(item, choice);
      return 0;
    }
    case MI_NUMBER: {
      if (!item->set_num || !is_number)
        return "a number";
      int32_t max = item->num_max ? item->num_max(item) : INT32_MAX;
      if (number < item->num_min || (max != NUM_UNKNOWN && number > max))
        return "out of its range";
      item->set_num(item, number);
      return 0;
    }
    default:
      return "not something with a value: do chooses it";
  }
}

static void list_menu(menu_t *menu)
{
  char text[320], name[96], value[160];
  for (int i = 0; i < menu->n_items; i++) {
    menu_item_t *item = &menu->items[i];
    plain_name(item->label, name, sizeof(name));
    item_value(item, value, sizeof(value));
    snprintf(text, sizeof(text), "%d | @%d %s %s%s%s", line_no, i, kind_names[item->kind], name,
             value[0] ? " = " : "", value);
    out_line(text);
  }
  result(true, "%d items in %s", menu->n_items, menu->title ? menu->title : "the menu");
}

/* --- the commands ---------------------------------------------------------------------------- */

static int command_by_key(const char *key)
{
  for (int c = 0; c < CMD_COUNT; c++)
    if (same(key, command_keys[c]))
      return c;
  return -1;
}

static void input_off(void)
{
  input_remote_active = false;
  input_remote_buttons = 0;
  input_remote_sticks[0] = input_remote_sticks[1] = false;
  input_frames = 0;
}

/* "input <buttons> [ls <x> <y>] [rs <x> <y>] [for <frames>]" */
static const char *run_input(char *args)
{
  char *buttons = take_word(&args);
  if (same(buttons, "off")) {
    input_off();
    return 0;
  }
  uint32_t mask = 0, bind;
  int32_t number;
  if (same(buttons, "-") || same(buttons, "none"))
    mask = 0;
  else if (parse_int(buttons, &number))
    mask = (uint32_t)number;
  else if (bind_parse(buttons, &bind))
    mask = bind_mask(bind);
  else
    return "buttons: names with + between (A+ZL, at most four), a number (0x41) or -";
  bool sticks[2] = { false, false };
  int32_t axes[4] = { 0, 0, 0, 0 };
  int32_t frames = 1;
  for (char *word = take_word(&args); *word; word = take_word(&args)) {
    int stick = same(word, "ls") ? 0 : same(word, "rs") ? 1 : -1;
    if (stick >= 0) {
      for (int axis = 0; axis < 2; axis++)
        if (!parse_int(take_word(&args), &axes[2 * stick + axis]) || axes[2 * stick + axis] < -STICK_MAX ||
            axes[2 * stick + axis] > STICK_MAX)
          return "a stick: ls or rs, then x and y from -32767 to 32767";
      sticks[stick] = true;
    } else if (same(word, "for")) {
      if (!parse_int(take_word(&args), &frames) || frames < 0)
        return "for: a number of frames (0: until the next input)";
    } else {
      return "after the buttons: ls <x> <y>, rs <x> <y>, for <frames>";
    }
  }
  input_remote_buttons = mask & 0xFFFF; /* the buttons; the sticks' direction bits come from the sticks */
  memcpy(input_remote_sticks, sticks, sizeof(sticks));
  memcpy(input_remote_stick, axes, sizeof(axes));
  input_remote_active = true;
  input_frames = frames;
  return 0;
}

float hypotf(float x, float y); /* from sdk */

/* Link's own state, for status and trace: his velocity (the actor's, units a second), what he holds and what's
 * on ZR (the player object's: -1 nothing, 0 the Deku Stick, 3 the Boomerang...), whether ZL has a target (2 with
 * one, 3 with none), whether he's on the ground, the ids of his main and weapon actions (the game's action table:
 * 40 Wait, 1 Walk, 65 LockOnWait, 66 LockOnMove, 114 JumpAttack, 33 Fall, 32 Land; weapon 57 Drawn, 58 Sheathed,
 * 56 PutAway) and which way he faces (degrees, 0 = +z). The offsets are the running build's (z64hd.h); where one
 * isn't known (0) or can't be read, the value stays unknown: moving false, -1 for the rest. */
#define PLAYER_JS_HEIGHT 0x18E8 /* watches.c: js_height_address gives the player object + this */
#define OFFSET(field) (z64hd_game ? z64hd_game->field : 0)

typedef struct {
  bool moving; /* vx, vy, vz were read */
  float vx, vy, vz, facing;
  int32_t hand, zr, lock, ground, action, weapon;
} link_state_t;

/* the s32 at base + offset, or -1 where the offset isn't known or the word can't be read */
static int32_t read_s32(uintptr_t base, uint32_t offset)
{
  uint32_t word;
  return base && offset && sys_read32(base + offset, &word) ? (int32_t)word : -1;
}

/* the pointer at base + offset, or 0 */
static uintptr_t read_ptr(uintptr_t base, uint32_t offset)
{
  return base && offset ? sys_ptr(base + offset) : 0;
}

float atan2f(float y, float x); /* from sdk */

static void link_state(link_state_t *s)
{
  *s = (link_state_t){ false, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1 };
  double fx, fz;
  if (link_facing(&fx, &fz)) {
    s->facing = atan2f((float)fx, (float)fz) * (180.0f / 3.14159265f);
    if (s->facing < 0)
      s->facing += 360.0f;
  }
  uintptr_t link = link_actor();
  uint32_t velocity = OFFSET(link_velocity);
  if (link && velocity && sys_mapped(link + velocity, 12, SYS_PERM_R)) {
    const float *v = (const float *)(link + velocity);
    s->moving = true;
    s->vx = v[0];
    s->vy = v[1];
    s->vz = v[2];
  }
  uintptr_t js;
  if (!js_height_address(&js) || js < PLAYER_JS_HEIGHT)
    return;
  uintptr_t player = js - PLAYER_JS_HEIGHT;
  s->zr = read_s32(player, OFFSET(player_zr_item));
  s->hand = read_s32(player, OFFSET(player_held_kind));
  s->lock = read_s32(player, OFFSET(player_lock_state));
  s->ground = read_s32(player, OFFSET(player_on_ground));
  if (s->ground != -1)
    s->ground &= 1;
  uintptr_t actions = read_ptr(read_ptr(read_ptr(player, OFFSET(player_actions_1)), OFFSET(player_actions_2)),
                               OFFSET(player_actions_3));
  s->action = read_s32(read_ptr(actions, OFFSET(action_main_slot)), OFFSET(action_id));
  s->weapon = read_s32(read_ptr(actions, OFFSET(action_weapon_slot)), OFFSET(action_id));
}

/* "vy=6.125 hs=5.000", or "vy=- hs=-" where Link's velocity isn't known */
static void motion_text(const link_state_t *s, char *buf, size_t cap)
{
  if (s->moving)
    snprintf(buf, cap, "vy=%.3f hs=%.3f", (double)s->vy, (double)hypotf(s->vx, s->vz));
  else
    snprintf(buf, cap, "vy=- hs=-");
}

/* one frame of a trace: "<line> t y=... vy=... hs=... x=... z=... hand=... zr=... paused=..." */
static void trace_line(void)
{
  vec3f_t pos = { 0, 0, 0 };
  link_state_t s;
  link_drawn_pos(&pos);
  link_state(&s);
  char motion[48], text[240];
  motion_text(&s, motion, sizeof(motion));
  snprintf(text, sizeof(text), "%d t y=%.3f %s x=%.3f z=%.3f hand=%d zr=%d paused=%d lock=%d ground=%d act=%d wpn=%d",
           line_no, (double)pos.y, motion, (double)pos.x, (double)pos.z, (int)s.hand, (int)s.zr,
           (int)(gd_ready() && gd_game_paused(0, 0)), (int)s.lock, (int)s.ground, (int)s.action, (int)s.weapon);
  out_line(text);
}

/* "12.5", "-0.25", "110": a number with a decimal point */
static bool parse_decimal(const char *s, double *out)
{
  bool negative = *s == '-';
  if (negative || *s == '+')
    s++;
  double value = 0, scale = 1;
  int digits = 0;
  for (; *s >= '0' && *s <= '9'; s++, digits++)
    value = value * 10 + (*s - '0');
  if (*s == '.')
    for (s++; *s >= '0' && *s <= '9'; s++, digits++) {
      scale /= 10;
      value += (*s - '0') * scale;
    }
  if (*s || !digits)
    return false;
  *out = negative ? -value : value;
  return true;
}

#define PLACE_NEAR_ENOUGH 0.3f /* place: metres (across) from the spot that count as there */
#define PLACE_RETRY 20         /* ... frames between loads of the position while he isn't */
#define PLACE_MAX 120          /* ... frames before it gives up */

/* place: the line waits until Link is at the spot (load position's first use after a map load may only find his
 * record; the position is loaded again until he's there) */
static struct {
  bool active;
  double x, y, z, facing;
  int frames;
  char said[200];
} placing;

static void place_at(double x, double y, double z, double facing, const char *said)
{
  position_set(x, y, z, facing);
  position_load();
  placing.active = true;
  placing.x = x, placing.y = y, placing.z = z, placing.facing = facing;
  placing.frames = 0;
  snprintf(placing.said, sizeof(placing.said), "%s", said);
}

/* once a game frame while a place waits */
static void place_frame(void)
{
  vec3f_t now = { 0 };
  placing.frames++;
  if (link_drawn_pos(&now)) {
    float dx = now.x - (float)placing.x, dz = now.z - (float)placing.z;
    if (dx * dx + dz * dz <= PLACE_NEAR_ENOUGH * PLACE_NEAR_ENOUGH && !position_busy()) {
      placing.active = false;
      result(true, "%s after %d frames", placing.said, placing.frames);
      return;
    }
  }
  if (placing.frames >= PLACE_MAX) {
    placing.active = false;
    result(false, "place: Link not there after %d frames (at %.2f %.2f %.2f)", placing.frames, (double)now.x,
           (double)now.y, (double)now.z);
    return;
  }
  if (placing.frames % PLACE_RETRY == 0 && link_record_status() == REC_READY) {
    position_set(placing.x, placing.y, placing.z, placing.facing);
    position_load();
  }
}

/* "place <x> <y> <z> [facing]": Link to that spot of this map, as load position does it */
static void run_place(char *args)
{
  double v[4] = { 0, 0, 0, 0 };
  int n = 0;
  for (char *word = take_word(&args); *word; word = take_word(&args), n++)
    if (n >= 4 || !parse_decimal(word, &v[n])) {
      result(false, "place: x y z, then the way to face in degrees (0 = +z) if not as now");
      return;
    }
  if (n < 3) {
    result(false, "place: x y z, then the way to face in degrees (0 = +z) if not as now");
    return;
  }
  if (link_record_status() != REC_READY) {
    result(false, "place: Link not found");
    return;
  }
  if (n < 4) {
    link_state_t s;
    link_state(&s);
    v[3] = s.facing;
  }
  char said[200];
  snprintf(said, sizeof(said), "place %.3f %.3f %.3f facing %.1f", v[0], v[1], v[2], v[3]);
  place_at(v[0], v[1], v[2], v[3], said);
}

#define NEAR_SEARCH 64      /* place near / wait until actor: how many of the nearest actors are looked through */
#define NEAR_RADIUS 60.0f   /* ... within this many metres */
#define NEAR_DEFAULT 1.2    /* place near: metres from the actor unless said */

static bool same_name(const char *a, const char *b)
{
  if (!a || !b)
    return false;
  while (*a && *a == *b)
    a++, b++;
  return *a == *b;
}

/* the nearest running actor called name within radius metres, into *out; false if none */
static bool nearest_named(const char *name, float radius, actor_near_t *out)
{
  static actor_near_t found[NEAR_SEARCH];
  int n = actors_near(radius, found, NEAR_SEARCH);
  for (int i = 0; i < n; i++)
    if (same_name(found[i].name, name)) {
      *out = found[i];
      return true;
    }
  return false;
}

/* "place near <actor> [metres]": Link that far from the nearest actor of that name, on the side he's on now, facing it */
static void run_place_near(char *args)
{
  char *name = take_word(&args);
  char *metres = take_word(&args);
  double d = NEAR_DEFAULT;
  if (!*name || (*metres && (!parse_decimal(metres, &d) || d < 0))) {
    result(false, "place near: an actor's name (as actors lists it), then metres from it (%.1f unless said)", NEAR_DEFAULT);
    return;
  }
  actor_near_t a;
  vec3f_t me;
  if (link_record_status() != REC_READY || !link_drawn_pos(&me)) {
    result(false, "place near: Link not found");
    return;
  }
  if (!nearest_named(name, NEAR_RADIUS, &a)) {
    result(false, "place near: no %s within %.0f m", name, (double)NEAR_RADIUS);
    return;
  }
  double dx = me.x - a.pos[0], dz = me.z - a.pos[2];
  double len = hypotf((float)dx, (float)dz);
  if (len < 0.01) { /* on top of it: step back the way he faces */
    double fx, fz;
    if (!link_facing(&fx, &fz))
      fx = 0, fz = 1;
    dx = -fx, dz = -fz, len = 1;
  }
  double x = a.pos[0] + dx / len * d, z = a.pos[2] + dz / len * d, y = a.pos[1];
  double facing = atan2f((float)(a.pos[0] - x), (float)(a.pos[2] - z)) * (180.0 / 3.14159265);
  if (facing < 0)
    facing += 360;
  char said[200];
  snprintf(said, sizeof(said), "place near %s (%.2f %.2f %.2f): %.3f %.3f %.3f facing %.1f", name, (double)a.pos[0],
           (double)a.pos[1], (double)a.pos[2], x, y, z, facing);
  place_at(x, y, z, facing, said);
}

#define ACTORS_MAX 24

/* "spawn <actor> [metres]": the game makes that actor that far in front of Link (2 m unless said), facing him, as
 * debug > actors > quick spawn does, through the create request's handle (actor_spawn_handle, FINDINGS 9.29
 * and 9.34); the line ends when the actor made is running. Only the names the game has (actornames.c): a name the
 * game has no ActorInfo row for would take a template slot for good. */
#define SPAWN_AHEAD 2.0
#define SPAWN_WAIT 600 /* frames to wait for it (its pack may have to load) */

static struct {
  bool active;
  const char *name; /* actornames.c's */
  int frames;
  uintptr_t handle, actor; /* the request's status (the game writes it), then the actor made */
} spawning;

static const char *known_actor(const char *name)
{
  for (int i = 0; i < actor_names_count; i++)
    if (same(actor_names[i], name))
      return actor_names[i];
  return 0;
}

static void run_spawn(char *args)
{
  char *name = take_word(&args);
  char *metres = take_word(&args);
  double d = SPAWN_AHEAD;
  if (!*name || (*metres && !parse_decimal(metres, &d))) {
    result(false, "spawn: an actor's name (as debug > actors lists it), then metres in front of Link (%.0f unless "
           "said)", SPAWN_AHEAD);
    return;
  }
  const char *actor = known_actor(name);
  vec3f_t me;
  double fx = 0, fz = 1;
  if (!actor) {
    result(false, "spawn: %s isn't one of the %d actors the game has", name, actor_names_count);
    return;
  }
  if (!link_drawn_pos(&me)) {
    result(false, "spawn: Link not found");
    return;
  }
  link_facing(&fx, &fz);
  float at[3] = { (float)(me.x + fx * d), me.y, (float)(me.z + fz * d) };
  float yaw = atan2f((float)fx, (float)fz) * (180.0f / 3.14159265f) + 180.0f; /* facing Link */
  uint32_t code;
  if (!actor_spawn_handle(actor, at, yaw, &spawning.handle, &code)) {
    result(false, "spawn %s: the game refused it (%u)", actor, (unsigned)code);
    return;
  }
  spawning.active = true;
  spawning.name = actor;
  spawning.frames = 0;
  spawning.actor = 0;
}

/* while a spawn line waits: made (then running), failed, or too long */
static bool spawn_waiting(void)
{
  actor_read_t a;
  if (!spawning.actor) {
    spawn_poll_t now = actor_spawn_poll(&spawning.handle, &spawning.actor);
    if (now == SPAWN_FAILED) {
      spawning.active = false;
      result(false, "spawn %s: the game couldn't make it", spawning.name);
      return false;
    }
  }
  if (spawning.actor && actor_read(spawning.actor, &a) && a.running) {
    spawning.active = false;
    result(true, "spawn %s: made after %d frames", spawning.name, spawning.frames);
    return false;
  }
  if (spawning.actor && !actor_read(spawning.actor, &a)) {
    spawning.active = false;
    result(false, "spawn %s: made, but it went at once", spawning.name);
    return false;
  }
  if (spawning.frames >= SPAWN_WAIT) {
    actor_spawn_cancel(&spawning.handle);
    spawning.active = false;
    result(false, "spawn %s: not made after %d frames", spawning.name, spawning.frames);
    return false;
  }
  return true;
}

/* "findref <hex>": where Link's components hold that value (an actor's address, say): each component [[link+0x270]+k]
 * read 0x1000 bytes deep, and one more pointer down; for finding what the game keeps about Link's targets */
static uint64_t parse_hex(const char *s)
{
  uint64_t v = 0;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    s += 2;
  for (; *s; s++) {
    int d = *s >= '0' && *s <= '9' ? *s - '0' : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10 : *s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : -1;
    if (d < 0)
      break;
    v = v * 16 + (uint64_t)d;
  }
  return v;
}

static void run_findref(char *args)
{
  uint64_t value = parse_hex(take_word(&args));
  uintptr_t link = link_actor();
  if (!value || !link || !sys_mapped(link + 0x270, 8, SYS_PERM_R)) {
    result(false, "findref: a hex value, with Link there");
    return;
  }
  uintptr_t components = *(uintptr_t *)(link + 0x270);
  int found = 0;
  /* an actor's address: also its components, its attention sender, and its id (+0x18, u32) */
  uint64_t values[4] = { value, 0, 0, 0 };
  const char *what[4] = { "actor", "components", "sender", "id" };
  if (sys_mapped(value + 0x270, 8, SYS_PERM_R) && !(value & 7)) {
    values[1] = *(uint64_t *)(value + 0x270);
    if (values[1] && sys_mapped(values[1] + 0x160, 8, SYS_PERM_R))
      values[2] = *(uint64_t *)(values[1] + 0x160);
    values[3] = *(uint32_t *)(value + 0x18);
  }
  for (int k = 0; k < 0x800 && found < 24; k += 8) {
    if (!sys_mapped(components + k, 8, SYS_PERM_R))
      break;
    uintptr_t c = *(uintptr_t *)(components + k);
    if (!c || (c & 7) || !sys_mapped(c, 0x2000, SYS_PERM_R))
      continue;
    for (int o = 0; o < 0x2000 && found < 24; o += 4) {
      for (int w = 0; w < 4; w++) {
        if (!values[w])
          continue;
        bool hit = w == 3 ? *(uint32_t *)(c + o) == (uint32_t)values[w] : !(o & 7) && *(uint64_t *)(c + o) == values[w];
        if (hit) {
          result(true, "findref: %s at [comp+%#x]+%#x", what[w], k, o);
          found++;
        }
      }
      uint64_t v = (o & 7) ? 0 : *(uint64_t *)(c + o);
      if (v && !(v & 7) && sys_mapped(v, 0x200, SYS_PERM_R))
        for (int p = 0; p < 0x200 && found < 24; p += 8)
          for (int w = 0; w < 3; w++)
            if (values[w] && *(uint64_t *)(v + p) == values[w]) {
              result(true, "findref: %s at [[comp+%#x]+%#x]+%#x", what[w], k, o, p);
              found++;
            }
    }
  }
  result(true, "findref: %d found", found);
}

/* "attention": Link, his attention object ([[link+0x270]+0x578]) and what its do-action slot (+0x1A70) holds, for a
 * debugger's watchpoint */
static void run_attention(void)
{
  uintptr_t link = link_actor(), comps = 0, attention = 0, record = 0, target = 0;
  if (link && sys_mapped(link + 0x270, 8, SYS_PERM_R))
    comps = *(uintptr_t *)(link + 0x270);
  if (comps && sys_mapped(comps + 0x578, 8, SYS_PERM_R))
    attention = *(uintptr_t *)(comps + 0x578);
  if (attention && sys_mapped(attention + 0x1A70, 8, SYS_PERM_R))
    record = *(uintptr_t *)(attention + 0x1A70);
  if (record && sys_mapped(record + 8, 8, SYS_PERM_R))
    target = *(uintptr_t *)(record + 8);
  result(true, "attention: link %#lx components %#lx attention %#lx slot %#lx record %#lx target %#lx main %#lx",
         (unsigned long)link, (unsigned long)comps, (unsigned long)attention, (unsigned long)(attention + 0x1A70),
         (unsigned long)record, (unsigned long)target, (unsigned long)z64hd_main);
}

/* "actor <name>": the addresses of the actors with that name (any state), for findref and a debugger */
static void run_actor(char *args)
{
  char *name = take_word(&args);
  uintptr_t found[8];
  int n = *name ? actors_named(name, found, 8) : 0;
  for (int i = 0; i < n; i++) { /* its instance id ([[actor+0x3D8]+0x88]) and the rails it walks (guardview.c) */
    uintptr_t pre = sys_ptr(found[i] + 0x3D8);
    uint64_t instance = pre && !(pre & 7) && sys_mapped(pre + 0x88, 8, SYS_PERM_R) ? *(uint64_t *)(pre + 0x88) : 0;
    result(true, "actor: %s %#lx instance %llu rails %d", name, (unsigned long)found[i], (unsigned long long)instance,
           guardview_rails_of(found[i]));
  }
  result(n > 0, "actor: %d named %s", n, name);
}

/* "peek <hex address> [bytes]": memory, as 8-byte words (up to 256 bytes), for finding where the game keeps things */
static void run_peek(char *args)
{
  char *word = take_word(&args);
  uint64_t addr = 0;
  int32_t size = 64;
  for (const char *c = word[0] == '0' && (word[1] == 'x' || word[1] == 'X') ? word + 2 : word; *c; c++) {
    int d = *c >= '0' && *c <= '9' ? *c - '0' : *c >= 'a' && *c <= 'f' ? *c - 'a' + 10
          : *c >= 'A' && *c <= 'F' ? *c - 'A' + 10 : -1;
    if (d < 0) {
      result(false, "peek: a hex address, then how many bytes (up to 256)");
      return;
    }
    addr = addr * 16 + (uint64_t)d;
  }
  char *count = take_word(&args);
  if (*count && (!parse_int(count, &size) || size < 8 || size > 256)) {
    result(false, "peek: a hex address, then how many bytes (up to 256)");
    return;
  }
  addr &= ~(uint64_t)7;
  if (!addr || !sys_mapped((uintptr_t)addr, (size_t)size, SYS_PERM_R)) {
    result(false, "peek: %#llx isn't readable", (unsigned long long)addr);
    return;
  }
  char text[160];
  for (int32_t o = 0; o < size; o += 16) {
    const uint64_t *q = (const uint64_t *)(uintptr_t)(addr + (uint64_t)o);
    if (o + 8 < size)
      snprintf(text, sizeof(text), "%d | +%#05x %016llx %016llx", line_no, (int)o, (unsigned long long)q[0],
               (unsigned long long)q[1]);
    else
      snprintf(text, sizeof(text), "%d | +%#05x %016llx", line_no, (int)o, (unsigned long long)q[0]);
    out_line(text);
  }
  result(true, "peek %#llx %d", (unsigned long long)addr, (int)size);
}

/* "doortimer <name>": the door close watch put on the first door of that name (as its actor option does, without
 * adding the watch), and what it reads now: run it again while the door is open to see the search find its node */
static void run_door_timer(char *args)
{
  char *name = take_word(&args);
  uintptr_t found[8];
  int n = *name ? actors_named(name, found, 8) : 0;
  if (!n) {
    result(false, "doortimer: none named %s", name);
    return;
  }
  interactview_door_watch(found[0]);
  char text[32];
  interactview_door_text(text, sizeof(text));
  result(true, "doortimer: %s %#lx: %s, open %d (turned %.1f deg)", name, (unsigned long)found[0], text,
         interactview_door_is_open(found[0]), (double)interactview_door_angle(found[0]));
}

static uint64_t parse_hex(const char *s);

/* "drown <name | 0xactor> [show]": the drown timer watch put on the first of that name, or that actor (as its actor
 * option does, without adding the watch), what it reads now and where it comes back; show: its respawn point drawn
 * too (or not: it flips) */
static void run_drown(char *args)
{
  char *name = take_word(&args);
  bool show = same(take_word(&args), "show");
  uintptr_t found[8];
  int n = 0;
  if (name[0] == '0' && name[1] == 'x') {
    actor_read_t a;
    found[0] = (uintptr_t)parse_hex(name);
    n = actor_read(found[0], &a) ? 1 : 0;
  } else if (*name)
    n = actors_named(name, found, 8);
  if (!n) {
    result(false, "drown: none named %s", name);
    return;
  }
  static const char *const kinds[] = { "doesn't drown", "cucco", "dog", "horse" };
  static const char *const ats[] = { "nowhere", "home", "WarpPos", "last landing", "the nearest safe spot" };
  int kind = drownview_kind(found[0]);
  drownview_watch(kind ? found[0] : 0);
  if (show && kind)
    drownview_toggle(found[0]);
  char state[40], drown[40], fade[40], back[40];
  drownview_text(DW_STATE, state, sizeof(state));
  drownview_text(DW_DROWN, drown, sizeof(drown));
  drownview_text(DW_FADE, fade, sizeof(fade));
  drownview_text(DW_RESPAWN, back, sizeof(back));
  float p[3] = { 0, 0, 0 };
  int at = drownview_respawn(found[0], p);
  result(kind != 0, "drown: %s %#lx (%s): %s, drown %s, fade %s, respawn %s; back at %s %.2f %.2f %.2f", name,
         (unsigned long)found[0], kinds[kind >= 0 && kind < 4 ? kind : 0], state, drown, fade, back,
         ats[at >= 0 && at < 5 ? at : 0], (double)p[0], (double)p[1], (double)p[2]);
}

static void run_actors(const char *args)
{
  int32_t radius = 6;
  if (*args && (!parse_int(args, &radius) || radius < 1)) {
    result(false, "actors: a distance in metres");
    return;
  }
  static actor_near_t near[ACTORS_MAX];
  int n = actors_near((float)radius, near, ACTORS_MAX);
  char text[160];
  for (int i = 0; i < n; i++) {
    snprintf(text, sizeof(text), "%d | %.2f %.80s %.2f %.2f %.2f", line_no, (double)near[i].distance, near[i].name,
             (double)near[i].pos[0], (double)near[i].pos[1], (double)near[i].pos[2]);
    out_line(text);
  }
  result(true, "%d actors within %d m", n, (int)radius);
}

/* flag <name or 0xhash> [value]: read a saved Bool (else Int) field, or set it */
static void run_flag(char *args)
{
  char *name = take_word(&args);
  args = skip_spaces(args);
  if (!name || !*name) {
    result(false, "flag: a field name or 0x hash, then optionally a value");
    return;
  }
  if (!gd_ready()) {
    result(false, "flag: the game's fields aren't ready");
    return;
  }
  uint32_t hash = 0;
  if (name[0] == '0' && (name[1] == 'x' || name[1] == 'X')) {
    for (const char *p = name + 2; *p; p++) {
      int d = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10
            : *p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : -1;
      if (d < 0) {
        result(false, "flag: %s is not a hash", name);
        return;
      }
      hash = hash * 16 + (uint32_t)d;
    }
  } else {
    hash = murmur3(name);
  }
  bool b;
  int32_t i;
  bool is_bool = gd_get_bool(hash, &b);
  bool is_int = !is_bool && gd_get_int(hash, &i);
  if (!is_bool && !is_int) {
    result(false, "flag: no Bool or Int field %s (0x%08x)", name, (unsigned)hash);
    return;
  }
  if (*args) {
    int32_t value;
    if (!parse_int(args, &value)) {
      result(false, "flag: the value is a number");
      return;
    }
    if (is_bool) {
      gd_set_bool(hash, value != 0);
      gd_get_bool(hash, &b);
    } else {
      gd_poke_int(hash, value);
      gd_get_int(hash, &i);
    }
  }
  if (is_bool)
    result(true, "%s 0x%08x bool %d", name, (unsigned)hash, (int)b);
  else
    result(true, "%s 0x%08x int %d", name, (unsigned)hash, (int)i);
}

/* event <name>: start a game event by its name (RSDB EventSetting's row: "Item_Bottle_TutorialEvent_Animal_Insect_GetEvent")
 * the way SeqOneShotRequestEvent does (warp.c); the result is the manager's answer */
/* event ... here | actor <name> | at: the starter, for the result line */
static struct {
  uintptr_t actor;
  char what[64];
} starter;

float cosf(float x); /* from sdk */
float sinf(float x); /* from sdk */

static void run_event(char *args)
{
  char *name = take_word(&args);
  if (!*name) {
    result(false, "event: an event's name");
    return;
  }
  /* "here" (Link), "actor <name>" (the nearest of that name) or "at <x> <y> <z> [facing]": the starter, whose
   * place (and facing) the event puts its StarterPos actors at */
  char *where = take_word(&args);
  bool with_starter = *where != 0;
  double sp[4] = { 0, 0, 0, 0 };
  starter.actor = 0;
  if (same(where, "here")) {
    vec3f_t me;
    link_state_t st;
    starter.actor = link_actor();
    if (!link_drawn_pos(&me)) {
      result(false, "event: Link not found");
      return;
    }
    link_state(&st);
    sp[0] = me.x, sp[1] = me.y, sp[2] = me.z, sp[3] = st.facing;
    snprintf(starter.what, sizeof(starter.what), "Link");
  } else if (same(where, "actor")) {
    actor_near_t a;
    char *who = take_word(&args);
    if (!*who) {
      result(false, "event: actor <name>");
      return;
    }
    if (!nearest_named(who, NEAR_RADIUS, &a)) {
      result(false, "event: no %s within %.0f m", who, (double)NEAR_RADIUS);
      return;
    }
    starter.actor = a.actor;
    sp[0] = a.pos[0], sp[1] = a.pos[1], sp[2] = a.pos[2];
    snprintf(starter.what, sizeof(starter.what), "%s", who);
  } else if (same(where, "at")) {
    int n = 0;
    for (char *w = take_word(&args); *w && n < 5; w = take_word(&args), n++)
      if (n >= 4 || !parse_decimal(w, &sp[n]))
        n = 9;
    if (n < 3 || n > 4) {
      result(false, "event: at <x> <y> <z> [facing]");
      return;
    }
    snprintf(starter.what, sizeof(starter.what), "at %.2f %.2f %.2f", sp[0], sp[1], sp[2]);
  } else if (with_starter) {
    result(false, "event: <name> [here | actor <name> | at <x> <y> <z> [facing]]");
    return;
  }
  if (with_starter && (!z64hd_game || !z64hd_game->event_req_starter_mtx || !z64hd_game->actor_handle_entry)) {
    result(false, "event: a starter isn't known for this game version");
    return;
  }
  if (!z64hd_game || !z64hd_game->event_req_ctor || !z64hd_game->event_req_submit || !z64hd_game->event_mgr_ptr) {
    result(false, "event: not known for this game version");
    return;
  }
  uintptr_t holder = sys_ptr(MAIN(z64hd_game->event_mgr_ptr));
  if (!holder || !sys_ptr(holder)) {
    result(false, "event: no event manager");
    return;
  }
  bool ok;
  if (with_starter) {
    float c = cosf((float)(sp[3] * 3.14159265 / 180)), sn = sinf((float)(sp[3] * 3.14159265 / 180));
    float m[12] = { c, 0, sn, (float)sp[0], 0, 1, 0, (float)sp[1], -sn, 0, c, (float)sp[2] };
    ok = warp_request_event_from(name, m, starter.actor);
  } else {
    ok = warp_request_event(name);
  }
  if (with_starter)
    result(ok, "event %s, starter %s (%.2f %.2f %.2f facing %.1f): %s", name, starter.what, sp[0], sp[1], sp[2],
           sp[3], ok ? "accepted" : "refused");
  else
    result(ok, "event %s: %s", name, ok ? "accepted" : "refused");
}

/* str <name or 0xhash> [value]: read a String field, or set it ("-" for empty) */
static void run_str(char *args)
{
  char *name = take_word(&args);
  char *value = skip_spaces(args);
  trim_end(value);
  if (!*name) {
    result(false, "str: a field name or 0x hash, then optionally a value (- for empty)");
    return;
  }
  if (!gd_ready()) {
    result(false, "str: the game's fields aren't ready");
    return;
  }
  uint32_t hash = 0;
  if (name[0] == '0' && (name[1] == 'x' || name[1] == 'X')) {
    int32_t h;
    if (!parse_int(name, &h)) {
      result(false, "str: %s is not a hash", name);
      return;
    }
    hash = (uint32_t)h;
  } else {
    hash = murmur3(name);
  }
  if (*value && !gd_set_str(hash, same(value, "-") ? "" : value)) {
    result(false, "str: can't set %s (0x%08x)", name, (unsigned)hash);
    return;
  }
  const char *now = gd_get_str(hash);
  if (!now) {
    result(false, "str: no String field %s (0x%08x)", name, (unsigned)hash);
    return;
  }
  result(true, "%s 0x%08x = \"%s\"", name, (unsigned)hash, now);
}

static void run_status(bool held)
{
  link_state_t s;
  link_state(&s);
  vec3f_t pos = { 0, 0, 0 };
  bool has_pos = link_drawn_pos(&pos);
  const char *map = gd_ready() ? gd_get_str(GD_STR_MAP) : 0;
  const char *warp = warp_state();
  char motion[48];
  motion_text(&s, motion, sizeof(motion));
  dialog_t d;
  dialog_read(&d);
  char dialog[24], event[EVENTSKIP_NAME_MAX];
  if (d.state == DLG_CHOICE)
    snprintf(dialog, sizeof(dialog), "choice:%d", d.cursor);
  else if (d.state == DLG_OTHER)
    snprintf(dialog, sizeof(dialog), "other:%d", d.raw);
  else
    snprintf(dialog, sizeof(dialog), "%s", dialog_state_name(d.state));
  if (!eventskip_flow_name(event))
    snprintf(event, sizeof(event), "%s", eventskip_running() ? "?" : "-");
  const char *screen = gd_ready() ? gd_menu_screen_name() : 0;
  static const char *const wheel_names[] = { "closed", "opening", "open" };
  int wheel = wheel_phase();
  result(true, "screen=%s wheel=%s dialog=%s msg=%s event=%s frame=%u map=%s link=%s x=%.3f y=%.3f z=%.3f facing=%.1f %s hand=%d zr=%d lock=%d ground=%d act=%d "
               "wpn=%d held=%d menu=%d game_paused=%d warp=%s",
         screen ? screen : "-", wheel >= 0 && wheel <= 2 ? wheel_names[wheel] : "-", dialog, d.msg[0] ? d.msg : "-", event, (unsigned)frame_counter(), map && map[0] ? map : "?",
         has_pos ? "yes" : "no", (double)pos.x, (double)pos.y,
         (double)pos.z, (double)s.facing, motion, (int)s.hand, (int)s.zr, (int)s.lock, (int)s.ground,
         (int)s.action, (int)s.weapon, (int)held, menu_current != 0, (int)(gd_ready() && gd_game_paused(0, 0)),
         warp && warp[0] ? warp : "-");
}

/* --- waiting for the game: wait until, talk ------------------------------------------------------ */

#define UNTIL_DEFAULT 1800  /* frames before wait until / talk give up, unless said */
#define TALK_PRESS 2        /* talk: frames A is held */
#define TALK_GAP 6          /* ... and frames between presses */
#define TALK_QUIET 10       /* ... frames with no event and no window before it counts as over */

#define STILL_FRAMES 8 /* still: frames in a row at rest (after a warp Link slides in and the game ignores the item
                       * wheel until he has stopped) */
typedef enum { U_NONE, U_CONTROL, U_DIALOG, U_NODIALOG, U_CHOICE, U_EVENT, U_NOEVENT, U_PAUSED, U_UNPAUSED, U_MAP,
               U_ACTOR, U_MOVED, U_ZR, U_STILL, U_VALUE, U_WHEEL } until_t;
/* s starts with the word w (then a space or the end) */
static bool starts_word(const char *s, const char *w)
{
  while (*w && *s == *w)
    s++, w++;
  return !*w && (*s == ' ' || *s == '\t' || !*s);
}

/* a condition for wait until and repeat until */
typedef struct {
  until_t kind;
  char arg[96];     /* map, actor: the name; value: the menu path */
  char want[48];    /* value: the value wanted */
  float metres;
  vec3f_t from;     /* moved: where Link was when the wait began */
  int still_for;    /* still: frames in a row with control and Link at rest */
  int32_t zr;       /* zr: the item wanted on ZR */
} cond_t;
static struct {
  cond_t c;
  int left, waited;
} until;
/* repeat until: each level's condition (checked at its end) and rounds so far */
static struct {
  bool on;
  cond_t c;
  int rounds;
} repeat_until[REPEAT_DEPTH];
#define REPEAT_UNTIL_DEFAULT 100 /* rounds before repeat until gives up, unless said */

static struct {
  bool active;
  bool past_choice; /* a choice that was up when talk started is still up: not the one it stops at */
  int choose;       /* choose: the option wanted (-1: plain talk) */
  int left, frames, gap, quiet, messages;
  uint32_t a_mask, up_mask, down_mask;
  char msg[96];
} talk;

static uint32_t button_mask(const char *name)
{
  uint32_t bind;
  return bind_parse(name, &bind) ? bind_mask(bind) & 0xFFFF : 0;
}

static void press(uint32_t mask)
{
  input_remote_buttons = mask;
  input_remote_sticks[0] = input_remote_sticks[1] = false;
  input_remote_active = true;
  input_frames = TALK_PRESS;
  talk.gap = TALK_PRESS + TALK_GAP;
}

static bool same_name(const char *a, const char *b);

/* the game is in a map and playing: no load or menu screen up (the menu screen field reads Loading during a map
 * load, ItemMenu, GameOver... otherwise), and Link's main action isn't 0 (what it reads during loads and events) */
static bool in_play(void)
{
  const char *screen = gd_ready() ? gd_menu_screen_name() : 0;
  if (screen && !same_name(screen, "None"))
    return false;
  link_state_t s;
  link_state(&s);
  return link_actor() && s.action != 0;
}

/* the player can act: playing, no event, no message window, not paused */
static bool has_control(void)
{
  dialog_t d;
  dialog_read(&d);
  return in_play() && !eventskip_running() && !dialog_open(&d) && !(gd_ready() && gd_game_paused(0, 0));
}

/* a menu item's value as item_value gives it ("10 (bug)", "on", "250") is the one wanted: the whole of it, its
 * number, or the name in its brackets, case aside */
static bool value_is(const char *now, const char *want)
{
  size_t n = strlen(want);
  if (!n)
    return false;
  for (const char *p = now; *p; p++) {
    bool starts = p == now || p[-1] == '(';
    if (!starts)
      continue;
    size_t i = 0;
    while (i < n && p[i] && lowercase(p[i]) == lowercase(want[i]))
      i++;
    if (i == n && (!p[i] || p[i] == ' ' || p[i] == ')'))
      return true;
  }
  return false;
}

static bool cond_met(cond_t *c)
{
  dialog_t d;
  actor_near_t a;
  const char *map;
  switch (c->kind) {
  case U_CONTROL: return has_control();
  case U_DIALOG: return dialog_read(&d) && dialog_open(&d);
  case U_NODIALOG: return dialog_read(&d) && !dialog_open(&d);
  case U_CHOICE: return dialog_read(&d) && d.state == DLG_CHOICE;
  case U_EVENT: return eventskip_running();
  case U_NOEVENT: return !eventskip_running();
  case U_PAUSED: return gd_ready() && gd_game_paused(0, 0);
  case U_WHEEL: return wheel_phase() == 2;
  case U_UNPAUSED: return gd_ready() && !gd_game_paused(0, 0);
  case U_MAP: map = gd_ready() ? gd_get_str(GD_STR_MAP) : 0; return map && same_name(map, c->arg) && in_play();
  case U_ACTOR: return nearest_named(c->arg, c->metres, &a);
  case U_STILL: {
    link_state_t st;
    link_state(&st);
    bool rest = has_control() && st.moving && hypotf(st.vx, st.vz) < 0.01f && st.vy > -0.01f && st.vy < 0.01f;
    c->still_for = rest ? c->still_for + 1 : 0;
    return c->still_for >= STILL_FRAMES;
  }
  case U_ZR: {
    link_state_t s;
    link_state(&s);
    return s.zr == c->zr;
  }
  case U_MOVED: {
    vec3f_t now;
    if (!has_control() || !link_drawn_pos(&now))
      return false;
    float dx = now.x - c->from.x, dy = now.y - c->from.y, dz = now.z - c->from.z;
    return dx * dx + dy * dy + dz * dz > c->metres * c->metres;
  }
  case U_VALUE: {
    menu_t *menu = 0;
    menu_item_t *item = 0;
    char now[160];
    if (resolve(c->arg, &menu, &item) || !item)
      return false;
    item_value(item, now, sizeof(now));
    return value_is(now, c->want);
  }
  default: return true;
  }
}

/* a condition from args into *c, and its max (frames for wait until, rounds for repeat until) into *max if one
 * is given; false if it isn't one */
static bool parse_cond(char *args, cond_t *c, int32_t *max)
{
  char *what = take_word(&args);
  memset(c, 0, sizeof(*c));
  c->kind = same(what, "control") ? U_CONTROL : same(what, "dialog") ? U_DIALOG : same(what, "nodialog") ? U_NODIALOG
          : same(what, "choice") ? U_CHOICE : same(what, "event") ? U_EVENT : same(what, "noevent") ? U_NOEVENT
          : same(what, "paused") ? U_PAUSED : same(what, "unpaused") ? U_UNPAUSED : same(what, "map") ? U_MAP
          : same(what, "moved") ? U_MOVED : same(what, "zr") ? U_ZR : same(what, "still") ? U_STILL
          : same(what, "actor") ? U_ACTOR : same(what, "value") ? U_VALUE : same(what, "wheel") ? U_WHEEL : U_NONE;
  c->metres = 10;
  link_drawn_pos(&c->from);
  if (c->kind == U_VALUE) { /* value <menu path> = <value> */
    char *eq = args;
    while (*eq && *eq != '=')
      eq++;
    if (!*eq)
      return false;
    *eq = 0;
    trim_end(args);
    snprintf(c->arg, sizeof(c->arg), "%s", skip_spaces(args));
    args = eq + 1;
    snprintf(c->want, sizeof(c->want), "%s", take_word(&args));
    if (!c->arg[0] || !c->want[0])
      return false;
  }
  if (c->kind == U_ZR && !parse_int(take_word(&args), &c->zr))
    return false;
  if (c->kind == U_MOVED) { /* an optional distance first: a number */
    c->metres = 1;
    args = skip_spaces(args);
    double m;
    if ((*args >= '0' && *args <= '9') || *args == '.') {
      if (!parse_decimal(take_word(&args), &m) || m <= 0)
        return false;
      c->metres = (float)m;
    }
  }
  if (c->kind == U_MAP || c->kind == U_ACTOR) {
    char *name = take_word(&args);
    if (!*name)
      return false;
    snprintf(c->arg, sizeof(c->arg), "%s", name);
  }
  for (char *word = take_word(&args); c->kind != U_NONE && *word; word = take_word(&args)) {
    int32_t n;
    double m;
    if (same(word, "max") && parse_int(take_word(&args), &n) && n > 0)
      *max = n;
    else if (same(word, "within") && c->kind == U_ACTOR && parse_decimal(take_word(&args), &m) && m > 0)
      c->metres = (float)m;
    else
      return false;
  }
  return c->kind != U_NONE;
}

static const char cond_usage[] = "control, dialog, nodialog, choice, event, noevent, paused, unpaused, wheel (the item "
                                 "wheel open, taking the stick), moved [metres] "
                                 "(somewhere else, with control), still (control and at rest for 8 frames), zr <item "
                                 "number>, value <menu path> = <value>, map <name> (loaded) or actor <name> [within "
                                 "<metres>], then max <%s>";

/* "wait until <condition> [max <frames>]" */
static void run_wait_until(char *args)
{
  int32_t max = UNTIL_DEFAULT;
  until.waited = 0;
  if (!parse_cond(args, &until.c, &max)) {
    until.c.kind = U_NONE;
    char usage[400];
    snprintf(usage, sizeof(usage), cond_usage, "frames (1800 unless said)");
    result(false, "wait until: %s", usage);
    return;
  }
  until.left = max;
}

/* "talk [max <frames>]": press A through the message windows until the talk is over or a choice comes up */
static void run_talk(char *args)
{
  int32_t frames = UNTIL_DEFAULT;
  char *word = take_word(&args);
  if (*word && (!same(word, "max") || !parse_int(take_word(&args), &frames) || frames < 1)) {
    result(false, "talk: max <frames> (%d unless said)", UNTIL_DEFAULT);
    return;
  }
  dialog_t d;
  dialog_read(&d);
  memset(&talk, 0, sizeof(talk));
  talk.active = true;
  talk.choose = -1;
  talk.past_choice = d.state == DLG_CHOICE;
  talk.left = frames;
  talk.a_mask = button_mask("A");
}

/* "choose <n>": at a choice, move the cursor to option n (0 = the first) and take it with A */
static void run_choose(char *args)
{
  int32_t n;
  dialog_t d;
  dialog_read(&d);
  if (!parse_int(take_word(&args), &n) || n < 0 || n > 9) {
    result(false, "choose: the option, 0 for the first");
    return;
  }
  if (d.state != DLG_CHOICE && d.state != DLG_CHOICE_OPENING) {
    result(false, "choose: no choice is up (dialog %s)", dialog_state_name(d.state));
    return;
  }
  memset(&talk, 0, sizeof(talk));
  talk.active = true;
  talk.choose = n;
  talk.left = UNTIL_DEFAULT;
  talk.a_mask = button_mask("A");
  talk.up_mask = button_mask("d-up");
  talk.down_mask = button_mask("d-down");
}

/* once a game frame while choose runs */
static void choose_frame(const dialog_t *d)
{
  talk.frames++;
  if (talk.gap > 0) {
    talk.gap--;
    return;
  }
  if (d->state == DLG_CHOICE_OPENING)
    return;
  if (d->state != DLG_CHOICE) { /* taken */
    talk.active = false;
    result(true, "choose: took option %d after %d frames", talk.choose, talk.frames);
    return;
  }
  if (--talk.left <= 0) {
    talk.active = false;
    result(false, "choose: the cursor stayed at %d", d->cursor);
    return;
  }
  if (talk.messages) /* A pressed: wait for the window to leave the choice */
    return;
  if (d->cursor < talk.choose)
    press(talk.down_mask);
  else if (d->cursor > talk.choose)
    press(talk.up_mask);
  else {
    press(talk.a_mask);
    talk.messages = 1;
  }
}

/* once a game frame while talk runs */
static void talk_frame(void)
{
  dialog_t d;
  dialog_read(&d);
  if (talk.choose >= 0) {
    choose_frame(&d);
    return;
  }
  talk.frames++;
  if (talk.past_choice && d.state != DLG_CHOICE)
    talk.past_choice = false;
  if (dialog_open(&d) && d.msg[0] && !same_name(d.msg, talk.msg)) {
    snprintf(talk.msg, sizeof(talk.msg), "%s", d.msg);
    talk.messages++;
  }
  if (d.state == DLG_CHOICE && !talk.past_choice) {
    talk.active = false;
    result(true, "talk: choice at %s, cursor %d, after %d frames and %d messages", d.msg, d.cursor, talk.frames,
           talk.messages);
    return;
  }
  bool event = eventskip_running();
  talk.quiet = dialog_open(&d) || event ? 0 : talk.quiet + 1;
  if (talk.quiet >= TALK_QUIET) {
    talk.active = false;
    result(true, "talk: over after %d frames, %d messages, last %s", talk.frames, talk.messages,
           talk.msg[0] ? talk.msg : "-");
    return;
  }
  if (--talk.left <= 0) {
    talk.active = false;
    result(false, "talk: still going after %d frames (dialog %s, event %s), last %s", talk.frames,
           dialog_state_name(d.state), event ? "running" : "none", talk.msg[0] ? talk.msg : "-");
    return;
  }
  /* A while the text types or waits; the page turning and the choices appearing need nothing */
  if (talk.gap > 0) {
    talk.gap--;
  } else if (d.state == DLG_TYPING || d.state == DLG_WAITING) {
    press(talk.a_mask);
  }
}

/* Run one line. What it waits for, if anything, is left in the state above. */
static void run_line(char *line, bool held)
{
  char *args = line;
  char *verb = take_word(&args);
  args = skip_spaces(args);
  trim_end(args);
  int32_t number = 1;
  const char *problem = 0;
  menu_t *menu = 0;
  menu_item_t *item = 0;

  if (same(verb, "onerr")) {
    char *how = take_word(&args);
    if (same(how, "stop") || same(how, "go")) {
      stop_on_error = same(how, "stop");
      result(true, "onerr %s", how);
    } else {
      result(false, "onerr: stop (end the script at a line that fails) or go");
    }
  } else if (same(verb, "id")) {
    snprintf(script_id, sizeof(script_id), "%s", *args ? take_word(&args) : "-");
  } else if (same(verb, "repeat") && starts_word(args, "until")) { /* repeat until <condition> [max <rounds>] */
    char *rest = args;
    int32_t max = REPEAT_UNTIL_DEFAULT;
    cond_t c;
    take_word(&rest);
    if (repeat_depth >= REPEAT_DEPTH)
      result(false, "repeat: more than %d inside one another", REPEAT_DEPTH);
    else if (!parse_cond(rest, &c, &max)) {
      char usage[400];
      snprintf(usage, sizeof(usage), cond_usage, "rounds (100 unless said)");
      result(false, "repeat until: %s", usage);
    } else {
      repeat_until[repeat_depth].on = true;
      repeat_until[repeat_depth].c = c;
      repeat_until[repeat_depth].rounds = 0;
      repeats[repeat_depth].cursor = cursor;
      repeats[repeat_depth].line_no = line_no;
      repeats[repeat_depth++].left = max;
    }
  } else if (same(verb, "repeat")) {
    if (!parse_int(args, &number) || number < 1)
      result(false, "repeat: how many times");
    else if (repeat_depth >= REPEAT_DEPTH)
      result(false, "repeat: more than %d inside one another", REPEAT_DEPTH);
    else {
      repeat_until[repeat_depth].on = false;
      repeats[repeat_depth].cursor = cursor;
      repeats[repeat_depth].line_no = line_no;
      repeats[repeat_depth++].left = number;
    }
  } else if (same(verb, "end")) {
    int top = repeat_depth - 1;
    if (!repeat_depth)
      result(false, "end: no repeat to end");
    else if (repeat_until[top].on && (++repeat_until[top].rounds, cond_met(&repeat_until[top].c))) {
      result(true, "until met after %d rounds", repeat_until[top].rounds);
      repeat_depth--;
    } else if (repeat_until[top].on && repeat_until[top].rounds >= repeats[top].left) {
      result(false, "until: not met after %d rounds", repeat_until[top].rounds);
      repeat_depth--;
    } else if (repeat_until[top].on) {
      cursor = repeats[top].cursor;
      line_no = repeats[top].line_no;
    } else if (--repeats[repeat_depth - 1].left > 0) {
      cursor = repeats[repeat_depth - 1].cursor;
      line_no = repeats[repeat_depth - 1].line_no;
    } else {
      repeat_depth--;
    }
  } else if (same(verb, "echo")) {
    result(true, "%s", args);
  } else if (same(verb, "status")) {
    run_status(held);
  } else if (same(verb, "place")) {
    if (args[0] == 'n' && args[1] == 'e' && args[2] == 'a' && args[3] == 'r' && (args[4] == ' ' || !args[4]))
      run_place_near(skip_spaces(args + 4));
    else
      run_place(args);
  } else if (same(verb, "actor")) {
    run_actor(args);
  } else if (same(verb, "texdump")) {
    uint64_t texture = parse_hex(take_word(&args));
    int32_t bpp = 8;
    char *b = take_word(&args);
    if (*b)
      parse_int(b, &bpp);
    nvn_texdump((uintptr_t)texture, (int)bpp);
    result(texture != 0, "texdump %#llx", (unsigned long long)texture);
  } else if (same(verb, "hudtest")) { /* hudtest <hex target> [<hex cmd> <nth bind>] | off */
    char *what = take_word(&args);
    nvn_hud_test = same(what, "on");
    nvn_hud_test_target = 0;
    if (!same(what, "off") && !same(what, "on")) {
      nvn_hud_test_target = (uintptr_t)parse_hex(what);
      char *cmd = take_word(&args);
      int32_t k = 0;
      nvn_hud_test_cmd = *cmd ? (uintptr_t)parse_hex(cmd) : 0;
      parse_int(take_word(&args), &k);
      nvn_hud_test_bind = (int)k;
      nvn_hud_test = k > 0;
    }
    result(true, "hudtest %#lx %#lx %d", (unsigned long)nvn_hud_test_target, (unsigned long)nvn_hud_test_cmd,
           nvn_hud_test_bind);
  } else if (same(verb, "rtbinds")) {
    uintptr_t cmds[16];
    int binds[16];
    int n = nvn_hud_binds(cmds, binds, 16);
    for (int i = 0; i < n; i++)
      result(true, "rtbinds: cmd %#lx: %d binds", (unsigned long)cmds[i], binds[i]);
    result(true, "rtbinds: %d command buffers", n);
    static char text[3072];
    nvn_hud_bind_log(text, sizeof(text));
    for (char *line = text; *line;) {
      char *end = line;
      while (*end && *end != '\n')
        end++;
      char keep = *end;
      *end = 0;
      result(true, "rtbinds: %s", line);
      if (!keep)
        break;
      line = end + 1;
    }
  } else if (same(verb, "hitlist")) {
    static char text[24576];
    char *name = take_word(&args);
    if (*name)
      hitview_list_all(name, text, sizeof(text));
    else
      hitview_list(text, sizeof(text));
    int n = 0;
    for (char *line = text; *line; n++) {
      char *end = line;
      while (*end && *end != '\n')
        end++;
      char keep = *end;
      *end = 0;
      result(true, "hitlist: %s", line);
      if (!keep)
        break;
      line = end + 1;
    }
    result(true, "hitlist: %d", n);
  } else if (same(verb, "nohit")) { /* nohit <owner hex> <0|1>: debugging, a body's ForceNoHit (cct.c's switch) */
    char *word = take_word(&args);
    uint64_t owner = parse_hex(word);
    if (!owner || !sys_mapped((uintptr_t)owner + 0x70, 8, SYS_PERM_R) || !z64hd_game->cct_force_no_hit ||
        !parse_int(args, &number) || number < 0 || number > 1) {
      result(false, "nohit: a body owner (hex), then 0 or 1");
    } else {
      MAIN_FN(void (*)(uintptr_t, bool), z64hd_game->cct_force_no_hit)((uintptr_t)owner, number != 0);
      result(true, "nohit %#llx %d", (unsigned long long)owner, (int)number);
    }
  } else if (same(verb, "hitparts")) { /* hitparts <actor name>: debugging, the hitbox kinds the browser lists */
    uintptr_t found[8];
    int n = actors_named(args, found, 8);
    static const char *const part_names[HV_PARTS] = { "attack", "bump", "invincible", "hurt", "pickup" };
    for (int i = 0; i < n; i++) {
      uint8_t parts = hitview_parts_of(found[i]);
      char text[96] = "";
      for (int part = 0; part < HV_PARTS; part++)
        if (parts >> part & 1)
          snprintf(text + strlen(text), sizeof(text) - strlen(text), " %s", part_names[part]);
      result(true, "hitparts: %s %#lx:%s", args, (unsigned long)found[i], parts ? text : " none");
    }
    result(n > 0, "hitparts: %d named %s", n, args);
  } else if (same(verb, "guardbits")) { /* guardbits <actor name>: debugging, an actor's BSA flags (FINDINGS 9.43) */
    uintptr_t found[8];
    int n = actors_named(take_word(&args), found, 8);
    for (int i = 0; i < n; i++) {
      uintptr_t comps = sys_ptr(found[i] + 0x270), bsa = comps ? sys_ptr(comps + 0x188) : 0;
      uint32_t flags = 0;
      if (bsa && sys_mapped(bsa + 0x24, 4, SYS_PERM_R))
        flags = *(const volatile uint32_t *)(bsa + 0x24);
      const volatile float *rot = (const volatile float *)(found[i] + 0x328);
      double yaw = atan2f(rot[2], rot[8]) * 57.29578f;
      /* the animation's hold events (QueryASGetHoldEventGuard, main+0x2984F80): each slot's, bit 0x10 Guard */
      uintptr_t as = comps ? sys_ptr(comps + 0x40) : 0, a1 = as ? sys_ptr(as + 0x18) : 0, ctl = a1 ? sys_ptr(a1 + 0x18) : 0;
      uint32_t n_slots = 0, holds = 0;
      if (ctl && sys_mapped(ctl + 0x18, 0x10, SYS_PERM_R))
        n_slots = *(const volatile uint32_t *)(ctl + 0x18);
      uintptr_t slots = ctl ? sys_ptr(ctl + 0x20) : 0;
      for (uint32_t k = 0; slots && k < n_slots && k < 8; k++) {
        uintptr_t e = sys_ptr(slots + k * 0x48), h = e ? sys_ptr(e + 0x58) : 0;
        if (h && sys_mapped(h + 0x40, 4, SYS_PERM_R))
          holds |= (*(const volatile uint32_t *)(h + 0x40) & 0x10 ? 1u : 0u) << k;
      }
      actor_read_t ar;
      bool got = actor_read(found[i], &ar);
      int life = got && ar.has_life ? (int)ar.life : -1;
      result(true, "guardbits %#lx: bsa %#lx flags %#x guard %d, as slots %u guard holds %#x, facing %.1f, life %d at %.2f %.2f",
             (unsigned long)found[i], (unsigned long)bsa, (unsigned)flags, (int)(flags >> 5 & 1), (unsigned)n_slots,
             (unsigned)holds, yaw < 0 ? yaw + 360 : yaw, life, got ? (double)ar.pos[0] : 0.0, got ? (double)ar.pos[2] : 0.0);
    }
    result(true, "guardbits: %d", n);
  } else if (same(verb, "equip")) { /* equip shield <actor>: debugging */
    char *what = take_word(&args), *item = take_word(&args);
    static char kept[64];
    snprintf(kept, sizeof(kept), "%s", item);
    if (!same(what, "shield") || !*item || !pouch_equip_shield_now(kept))
      result(false, "equip: shield <actor name> (Weapon_Shield_Deku, _Hylian, _Mirror), Link's gear found");
    else
      result(true, "equip shield %s", kept);
  } else if (same(verb, "pausetype")) { /* pausetype [<name or hex id> <+n or -n>]: the game's pause types */
    char *name = take_word(&args), *by = take_word(&args);
    int32_t delta = 0;
    if (*name && same(by, "masks")) { /* pausetype <name> masks <hex> <hex> <hex>: what it stops (debugging) */
      uint32_t m[3], was[3];
      for (int k = 0; k < 3; k++)
        m[k] = pausemgr_id(take_word(&args));
      if (!pausemgr_set_masks(pausemgr_id(name), m, was)) {
        result(false, "pausetype: no such type");
        return;
      }
      result(true, "pausetype %s masks %08x %08x %08x (were %08x %08x %08x)", name, (unsigned)m[0], (unsigned)m[1],
             (unsigned)m[2], (unsigned)was[0], (unsigned)was[1], (unsigned)was[2]);
    } else if (*name) {
      uint32_t id = pausemgr_id(name);
      if (!*by || !parse_int(by[0] == '+' ? by + 1 : by, &delta) || !pausemgr_add(id, delta)) {
        result(false, "pausetype: a type's name or hex id, then +n or -n (and the manager found)");
        return;
      }
      result(true, "pausetype %s %#x: count %d", name, (unsigned)id, pausemgr_count(id));
    } else {
      static char text[2048];
      pausemgr_list(text, sizeof(text));
      for (char *line = text; *line;) {
        char *end = line;
        while (*end && *end != '\n')
          end++;
        char keep = *end;
        *end = 0;
        result(true, "pausetype: %s", line);
        if (!keep)
          break;
        line = end + 1;
      }
    }
  } else if (same(verb, "button")) { /* button <0..3> [<item>]: an item button's choice, or put an item on it */
    int32_t slot = -1;
    parse_int(take_word(&args), &slot);
    char *item = take_word(&args);
    uint32_t was = 0;
    bool ok = pouch_button((int)slot, item, &was);
    result(ok, "button %d: was %#x%s%s", (int)slot, (unsigned)was, *item ? ", now " : "", item);
  } else if (same(verb, "rtlist")) {
    uintptr_t tex[48];
    uint32_t sets[48];
    int n = frametrace_targets(tex, sets, 48);
    for (int i = 0; i < n; i++) {
      int w = 0, h = 0, f = 0;
      nvn_texture_info(tex[i], &w, &h, &f);
      result(true, "rtlist: %#lx %dx%d format %#x, bound %u times", (unsigned long)tex[i], w, h, f, (unsigned)sets[i]);
    }
    result(true, "rtlist: %d targets", n);
  } else if (same(verb, "frametrace")) {
    frametrace_arm();
    result(true, "frametrace: the next frame goes to the log");
  } else if (same(verb, "physdump")) {
    char *name = take_word(&args);
    int records = 0;
    int64_t bytes = 0;
    bool ok = physdump_write(*name ? name : "phys", &records, &bytes);
    result(ok, "physdump: %d records, %lld bytes", records, (long long)bytes);
  } else if (same(verb, "peek")) {
    run_peek(args);
  } else if (same(verb, "doortimer")) {
    run_door_timer(args);
  } else if (same(verb, "aidump")) {
    uintptr_t found[8];
    char *name = take_word(&args);
    int n = *name ? actors_named(name, found, 8) : 0;
    for (int i = 0; i < n; i++)
      drownview_dump(found[i]);
    result(n > 0, "aidump: %d of %s to the log", n, name);
  } else if (same(verb, "drown")) {
    run_drown(args);
  } else if (same(verb, "attention")) {
    run_attention();
  } else if (same(verb, "findref")) {
    run_findref(args);
  } else if (same(verb, "actors")) {
    run_actors(args);
  } else if (same(verb, "spawn")) {
    run_spawn(args);
  } else if (same(verb, "flag")) {
    run_flag(args);
  } else if (same(verb, "str")) {
    run_str(args);
  } else if (same(verb, "event")) {
    run_event(args);
  } else if (same(verb, "trace")) {
    if (same(args, "off"))
      number = 0;
    else if (!parse_int(args, &number) || number < 1) {
      result(false, "trace: a number of frames, or off");
      return;
    }
    trace_left = number;
    result(true, "trace %d", (int)number);
  } else if (same(verb, "talk")) {
    run_talk(args);
  } else if (same(verb, "choose")) {
    run_choose(args);
  } else if (same(verb, "wait") && args[0] == 'u' && args[1] == 'n') {
    char *rest = args;
    if (!same(take_word(&rest), "until"))
      result(false, "wait: a number of frames, or until ...");
    else
      run_wait_until(skip_spaces(rest));
  } else if (same(verb, "wait")) {
    if (!parse_int(args, &number) || number < 0)
      result(false, "wait: a number of frames");
    else if (held && number > 0)
      result(false, "wait: the game is held, no frames pass (step lets them through)");
    else {
      wait_left = number;
      result(true, "wait %d", (int)number);
    }
  } else if (same(verb, "fire")) {
    int cmd = command_by_key(take_word(&args));
    char *frames = take_word(&args);
    number = 0;
    if (cmd < 0)
      result(false, "fire: not a command (settings.ini's bind_<key> names: savepos, loadpos, ragdoll...)");
    else if (*frames && (!parse_int(frames, &number) || number < 0))
      result(false, "fire: a number of frames to hold it");
    else {
      if (held)
        fire_pending[cmd] = (uint16_t)(1 + (number > 60000 ? 60000 : number)); /* on the next frame let through */
      else
        binds_fire((command_t)cmd, number);
      result(true, "fire %s", command_keys[cmd]);
    }
  } else if (same(verb, "do")) {
    problem = resolve(args, &menu, &item);
    if (!problem && !item)
      problem = "which item?";
    if (!problem)
      problem = choose(item);
    if (problem)
      result(false, "do: %s", problem);
    else {
      char value[160];
      item_value(item, value, sizeof(value));
      result(true, "%s%s%s", kind_names[item->kind], value[0] ? " = " : "", value);
    }
  } else if (same(verb, "set")) {
    char *equals = args;
    while (*equals && *equals != '=')
      equals++;
    if (!*equals) {
      result(false, "set: <menu path> = <value>");
      return;
    }
    *equals = 0;
    char *value = skip_spaces(equals + 1);
    trim_end(args);
    problem = resolve(args, &menu, &item);
    if (!problem && !item)
      problem = "which item?";
    if (!problem)
      problem = assign(item, value);
    if (problem)
      result(false, "set: %s", problem);
    else {
      char now[160];
      item_value(item, now, sizeof(now));
      result(true, "%s = %s", kind_names[item->kind], now);
    }
  } else if (same(verb, "get")) {
    problem = resolve(args, &menu, &item);
    if (!problem && !item)
      problem = "which item?";
    if (problem)
      result(false, "get: %s", problem);
    else {
      char value[160];
      item_value(item, value, sizeof(value));
      result(true, "%s = %s", kind_names[item->kind], value);
    }
  } else if (same(verb, "list")) {
    problem = resolve(args, &menu, &item);
    if (!problem && item && (item->kind != MI_SUBMENU || !item->submenu))
      problem = "not a menu";
    if (problem)
      result(false, "list: %s", problem);
    else
      list_menu(item ? item->submenu : menu);
  } else if (same(verb, "open")) {
    /* the menu on screen at that menu, or at that item of its menu (for a look at it) */
    problem = resolve(args, &menu, &item);
    if (problem)
      result(false, "open: %s", problem);
    else {
      menu_t *target = item && item->kind == MI_SUBMENU && item->submenu ? item->submenu : menu;
      if (!menu_current)
        menu_open(&menu_root);
      menu_goto(target);
      if (item && target == menu)
        target->selected = (int)(item - menu->items);
      result(true, "open %s", target->title);
    }
  } else if (same(verb, "pause")) {
    if (held)
      result(true, "paused");
    else {
      pause_wanted = true;
      pause_waiting = true; /* its result once the game is held */
    }
  } else if (same(verb, "step")) {
    if (*args && (!parse_int(args, &number) || number < 1))
      result(false, "step: a number of frames");
    else {
      if (!held)
        pause_wanted = true;
      steps_left = number;
      stepping = true; /* its result once they've gone through and the game is held again */
    }
  } else if (same(verb, "run")) {
    if (!held && !tool_paused()) /* (a camera tool's stop is frame advance's too: run lets it go) */
      result(true, "running");
    else {
      run_wanted = true;
      run_waiting = true;
    }
  } else if (same(verb, "title")) {
    if (*args)
      result(false, "title: nothing goes after it");
    else if ((problem = title_start())) /* held: frame advance lets the game go (title_going) */
      result(false, "title: %s", problem);
    else
      title_waiting = true; /* its result once Link is gone, or not */
  } else if (same(verb, "input")) {
    problem = run_input(args);
    if (problem)
      result(false, "input: %s", problem);
    else
      result(true, "input 0x%x for %d", (unsigned)input_remote_buttons, input_frames);
  } else {
    result(false, "not a command: %s", verb);
  }
}

/* --- the script ------------------------------------------------------------------------------ */

/* The script's next line with something on it (ended in place), or 0 at its end. */
static char *next_line(void)
{
  static char line_buf[LINE_MAX_LEN]; /* a copy: the script's text stays as it is, for a repeat to run again */
  while (cursor < script_len) {
    size_t end = cursor, len = 0;
    for (; end < script_len && script[end] != '\n'; end++)
      if (len < sizeof(line_buf) - 1)
        line_buf[len++] = script[end];
    line_buf[len] = 0;
    cursor = end < script_len ? end + 1 : end;
    line_no++;
    char *line = skip_spaces(line_buf);
    trim_end(line);
    if (*line && *line != '#')
      return line;
  }
  return 0;
}

/* Take cmd.txt if there is one: read it, delete it (so the next can be written), start running it. */
static void poll(void)
{
  static int calls;
  static bool stuck; /* cmd.txt couldn't be deleted: said once, not run again and again */
  if (++calls < POLL_CALLS)
    return;
  calls = 0;
  fs_file_t file;
  if (fs_open(&file, CMD_FILE, FS_READ) != 0) {
    stuck = false;
    return;
  }
  int64_t size = 0;
  size_t got = 0;
  fs_size(&size, file);
  bool fits = size >= 0 && size < SCRIPT_MAX;
  if (fits && size && fs_read(&got, file, 0, script, (size_t)size) != 0)
    got = 0;
  fs_close(file);
  if (stuck)
    return;
  line_no = 0;
  snprintf(script_id, sizeof(script_id), "-");
  if (fs_delete_file(CMD_FILE) != 0) {
    stuck = true;
    result(false, "cmd.txt can't be deleted: not run");
    return;
  }
  if (!fits) {
    result(false, "cmd.txt is over %d bytes: not run", SCRIPT_MAX - 1);
    return;
  }
  script_len = got;
  script[got] = 0;
  cursor = 0;
  repeat_depth = 0;
  stop_on_error = false;
  running = true;
}

/* Whether the line last run is still waiting; says so (its result) when the wait is over. */
static bool waiting(bool held)
{
  if (wait_left > 0) {
    if (!held || stepping)
      return true;
    /* the game was held while it waited (frame advance, or leaving a camera tool under it): no frames pass now,
     * so it ends here rather than never */
    result(false, "wait: the game was held with %d frames to go (step lets them through)", (int)wait_left);
    wait_left = 0;
  }
  if (spawning.active && spawn_waiting())
    return true;
  if ((talk.active || placing.active || until.c.kind != U_NONE) && held && !stepping && script_held) {
    /* held by this script's own pause: nothing would let the game go, so it ends here rather than never. A hold
     * from frame advance's buttons just pauses it: it goes on when the game does. */
    const char *what = talk.active ? (talk.choose >= 0 ? "choose" : "talk") : placing.active ? "place" : "until";
    talk.active = placing.active = false;
    until.c.kind = U_NONE;
    result(false, "%s: the script holds the game (step or run lets it go)", what);
  }
  if (talk.active || placing.active)
    return true;
  if (until.c.kind != U_NONE) {
    if (cond_met(&until.c)) {
      until.c.kind = U_NONE;
      result(true, "until met after %d frames", until.waited);
    } else if (until.left <= 0) {
      until.c.kind = U_NONE;
      result(false, "until: not met after %d frames", until.waited);
    } else {
      return true;
    }
  }
  if (pause_waiting) {
    if (!held)
      return true;
    pause_waiting = false;
    script_held = true;
    result(true, "paused");
  }
  if (stepping) {
    if (steps_left > 0 || !held)
      return true;
    stepping = false;
    script_held = true;
    result(true, "stepped");
  }
  if (run_waiting) {
    if (held)
      return true;
    run_waiting = false;
    result(true, "running");
  }
  if (title_waiting) {
    const char *text = "";
    title_status_t now = title_status(&text);
    if (now == TITLE_PENDING)
      return true;
    title_waiting = false;
    result(now == TITLE_DONE, "title: %s", text);
  }
  return false;
}

/* onerr stop: end the script, and anything its last line left waiting */
static void stop_script(void)
{
  stop_now = false;
  running = false;
  wait_left = 0;
  talk.active = false;
  placing.active = false;
  until.c.kind = U_NONE;
  out_line("end (onerr stop)");
}

void remote_frame(bool held)
{
  frametrace_write(); /* the debugging captures the render thread made: written here, on the game's thread */
  nvn_texdump_write();
  if (!remote_enabled || !settings_sd()) {
    /* remote control off: what a script holds or has queued goes (its input would otherwise stay on the
     * controller, "for 0" for good, as nothing here counts it down) */
    if (input_remote_active)
      input_off();
    for (int c = 0; c < CMD_COUNT; c++)
      fire_pending[c] = 0;
    return;
  }
  if (!held) { /* a frame of the game */
    script_held = false;
    for (int c = 0; c < CMD_COUNT; c++)
      if (fire_pending[c]) {
        binds_fire((command_t)c, fire_pending[c] - 1);
        fire_pending[c] = 0;
      }
    if (input_frames > 0 && --input_frames == 0)
      input_off();
    if (wait_left > 0)
      wait_left--;
    if (spawning.active) /* frames since the spawn line */
      spawning.frames++;
    if (until.c.kind != U_NONE) {
      until.left--;
      until.waited++;
    }
    if (talk.active)
      talk_frame();
    if (placing.active)
      place_frame();
    if (trace_left > 0) { /* this frame's, before any line of it runs */
      trace_line();
      trace_left--;
    }
  }
  if (stop_now)
    stop_script();
  if (!running)
    poll();
  size_t traced = out_len; /* more than the trace's lines after this: the sender is waiting to read it */
  for (int lines = 0; running && lines < LINES_PER_CALL && !waiting(held); lines++) {
    if (stop_now) { /* a wait just ended in an error */
      stop_script();
      break;
    }
    char *line = next_line();
    if (!line) {
      running = false;
      out_line("end");
      break;
    }
    run_line(line, held);
    if (stop_now) {
      stop_script();
      break;
    }
  }
  /* a trace alone is written a buffer at a time, not a file write a frame; its last frames at once */
  if (out_len != traced || trace_left <= 0 || out_len > OUT_MAX - 512)
    out_flush();
}

bool remote_take_pause(void)
{
  bool wanted = pause_wanted;
  pause_wanted = false;
  return wanted;
}

static uint32_t tool_paused_at = 0xFFFFFFFFu; /* the last frame a camera tool had the world stopped */

static bool tool_paused(void)
{
  return frame_counter() - tool_paused_at <= 1;
}

void remote_paused_by_tool(void)
{
  tool_paused_at = frame_counter();
  if (!pause_wanted)
    return;
  pause_wanted = false;
  if (pause_waiting) {
    pause_waiting = false;
    result(true, "paused (a camera tool has the world stopped)");
  }
}

bool remote_take_step(void)
{
  if (steps_left <= 0)
    return false;
  steps_left--;
  return true;
}

bool remote_take_run(void)
{
  bool wanted = run_wanted;
  run_wanted = false;
  return wanted;
}
