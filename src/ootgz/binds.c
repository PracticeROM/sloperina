/* Commands and their binds (binds.h), after gz's input.c: the same state machine per bind (a bit per button of
 * the bind gone down in order), the same rule that a bind inside another active one gives way, and gz's binder
 * (settings > rebind commands) for recording one. */
#include "binds.h"

#include "gfx.h"
#include "input.h"
#include "lang.h"
#include "sys.h"

const command_info_t command_info[CMD_COUNT] = {
  [CMD_MENU] = { "show/hide menu", CMD_GROUP_MENU, true },
  [CMD_RETURN] = { "return from menu", CMD_GROUP_MENU, true },
  [CMD_BACK] = { "back", CMD_GROUP_MENU, false },
  [CMD_UP] = { "menu up", CMD_GROUP_MENU, true },
  [CMD_DOWN] = { "menu down", CMD_GROUP_MENU, true },
  [CMD_LEFT] = { "menu left", CMD_GROUP_MENU, true },
  [CMD_RIGHT] = { "menu right", CMD_GROUP_MENU, true },
  [CMD_SELECT] = { "menu select", CMD_GROUP_MENU, true },
  [CMD_ADVANCE] = { "frame advance", CMD_GROUP_FRAME_ADVANCE, false },
  [CMD_RUN] = { "run again", CMD_GROUP_FRAME_ADVANCE, false, true },
  [CMD_SAVEPOS] = { "save position", CMD_GROUP_LINK, false },
  [CMD_LOADPOS] = { "load position", CMD_GROUP_LINK, false },
  [CMD_RAGDOLL] = { "ragdoll", CMD_GROUP_LINK, false },
  [CMD_LEVITATE] = { "levitate on/off", CMD_GROUP_LINK, false },
  [CMD_RISE] = { "levitate up", CMD_GROUP_LINK, false },
  [CMD_SINK] = { "levitate down", CMD_GROUP_LINK, false },
  [CMD_HOVER] = { "levitate hold height", CMD_GROUP_LINK, false },
  [CMD_RELOAD_MAP] = { "reload map", CMD_GROUP_SCENE, false },
  [CMD_END_CUTSCENE] = { "end cutscene", CMD_GROUP_SCENE, false },
  [CMD_MEMFILE_SAVE] = { "save memfile", CMD_GROUP_MEMFILES, false },
  [CMD_MEMFILE_LOAD] = { "load memfile", CMD_GROUP_MEMFILES, false },
  [CMD_MEMFILE_NEXT] = { "next memfile slot", CMD_GROUP_MEMFILES, false },
  [CMD_MEMFILE_PREV] = { "previous memfile slot", CMD_GROUP_MEMFILES, false },
  [CMD_WATCHES] = { "show/hide watches", CMD_GROUP_WATCHES, false },
  [CMD_TIMER] = { "start/stop timer", CMD_GROUP_WATCHES, false },
  [CMD_RESET_TIMER] = { "reset timer", CMD_GROUP_WATCHES, false },
  [CMD_RESET_LAG] = { "reset lag counter", CMD_GROUP_WATCHES, false },
  [CMD_RESET_JS] = { "reset jump height", CMD_GROUP_WATCHES, false },
};

/* their keys: bind_<key>= in the settings file, {key} in menu text (not menu text) */
const char *const command_keys[CMD_COUNT] = {
  [CMD_MENU] = "menu", /* not menu text */
  [CMD_RETURN] = "return", /* not menu text */
  [CMD_BACK] = "back", /* not menu text */
  [CMD_UP] = "menu_up", /* not menu text */
  [CMD_DOWN] = "menu_down", /* not menu text */
  [CMD_LEFT] = "menu_left", /* not menu text */
  [CMD_RIGHT] = "menu_right", /* not menu text */
  [CMD_SELECT] = "select", /* not menu text */
  [CMD_ADVANCE] = "advance", /* not menu text */
  [CMD_RUN] = "run", /* not menu text */
  [CMD_SAVEPOS] = "savepos", /* not menu text */
  [CMD_LOADPOS] = "loadpos", /* not menu text */
  [CMD_RAGDOLL] = "ragdoll", /* not menu text */
  [CMD_LEVITATE] = "levitate", /* not menu text */
  [CMD_RISE] = "rise", /* not menu text */
  [CMD_SINK] = "sink", /* not menu text */
  [CMD_HOVER] = "hover", /* not menu text */
  [CMD_RELOAD_MAP] = "reload_map", /* not menu text */
  [CMD_END_CUTSCENE] = "end_cutscene", /* not menu text */
  [CMD_MEMFILE_SAVE] = "memfile_save", /* not menu text */
  [CMD_MEMFILE_LOAD] = "memfile_load", /* not menu text */
  [CMD_MEMFILE_NEXT] = "memfile_next", /* not menu text */
  [CMD_MEMFILE_PREV] = "memfile_prev", /* not menu text */
  [CMD_WATCHES] = "watches", /* not menu text */
  [CMD_TIMER] = "timer", /* not menu text */
  [CMD_RESET_TIMER] = "reset_timer", /* not menu text */
  [CMD_RESET_LAG] = "reset_lag", /* not menu text */
  [CMD_RESET_JS] = "reset_jump", /* not menu text */
};

const char *const command_group_names[CMD_GROUPS] = {
  "menu", "frame advance", "link", "scene", "memfiles", "watches and timer",
};

/* input.h's bit numbers */
enum {
  B_A = 0, B_B = 1, B_X = 2, B_Y = 3, B_L3 = 4, B_R3 = 5, B_L = 6, B_R = 7, B_ZL = 8, B_ZR = 9,
  B_PLUS = 10, B_MINUS = 11, B_DLEFT = 12, B_DUP = 13, B_DRIGHT = 14, B_DDOWN = 15,
  B_RS_DOWN = 23, N_BUTTONS = 26,
};
/* their names: the words nativeui.c draws as glyphs (not menu text: the same in every language) */
static const char *const button_names[N_BUTTONS] = {
  "A", "B", "X", "Y", "L3", "R3", "L", "R", "ZL", "ZR", "plus", "minus", /* not menu text */
  "d-left", "d-up", "d-right", "d-down", "ls-left", "ls-up", "ls-right", "ls-down", /* not menu text */
  "rs-left", "rs-up", "rs-right", "rs-down", "SL", "SR", /* not menu text */
};
/* the bits that cancel a bind in progress when pressed: the buttons, not the sticks' directions */
#define CANCEL_BITS (0xFFFFu | (3u << 24))

#define B1(a) ((uint32_t)(a) | 0xFFFFFF00u)
#define B2(a, b) ((uint32_t)(a) | (uint32_t)(b) << 8 | 0xFFFF0000u)
#define B3(a, b, c) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | 0xFF000000u)

/* gz's defaults, with this module's buttons (the right stick for the C buttons) */
static const uint32_t defaults[CMD_COUNT] = {
  [CMD_MENU] = B2(B_R, B_RS_DOWN), [CMD_RETURN] = B2(B_R, B_DLEFT), [CMD_BACK] = B1(B_B),
  [CMD_UP] = B1(B_DUP), [CMD_DOWN] = B1(B_DDOWN), [CMD_LEFT] = B1(B_DLEFT), [CMD_RIGHT] = B1(B_DRIGHT),
  [CMD_SELECT] = B1(B_L),
  [CMD_ADVANCE] = B1(B_L), [CMD_RUN] = B2(B_L, B_L3),
  [CMD_SAVEPOS] = B2(B_R, B_DUP), [CMD_LOADPOS] = B2(B_R, B_DDOWN), [CMD_RAGDOLL] = B1(B_R3),
  [CMD_LEVITATE] = B1(B_L3), [CMD_RISE] = B1(B_X), [CMD_SINK] = B1(B_B), [CMD_HOVER] = B1(B_R3),
  [CMD_RELOAD_MAP] = BIND_NONE, [CMD_END_CUTSCENE] = BIND_NONE, /* no buttons of their own: for whoever wants them */
  [CMD_MEMFILE_SAVE] = BIND_NONE, [CMD_MEMFILE_LOAD] = BIND_NONE,
  [CMD_MEMFILE_NEXT] = BIND_NONE, [CMD_MEMFILE_PREV] = BIND_NONE,
  [CMD_WATCHES] = B2(B_R, B_DRIGHT), [CMD_TIMER] = B3(B_R, B_A, B_DLEFT), [CMD_RESET_TIMER] = B3(B_R, B_B, B_DLEFT),
  [CMD_RESET_LAG] = B3(B_R, B_B, B_DRIGHT), [CMD_RESET_JS] = B3(B_R, B_A, B_DRIGHT),
};

uint32_t binds[CMD_COUNT];
bool binds_enabled = true;
static bool binds_ready; /* binds_reset has filled them (at module init, before the settings) */

uint32_t bind_default(command_t cmd) { return defaults[cmd]; }

void binds_reset(void)
{
  memcpy(binds, defaults, sizeof(binds));
  binds_ready = true;
  binds_changed = true;
}

static int component(uint32_t bind, int i) { return (int)(bind >> (8 * i) & 0xFF); }

int bind_button(uint32_t bind, int i) { return component(bind, i); }

int bind_length(uint32_t bind)
{
  int n = 0;
  while (n < BIND_MAX && component(bind, n) < N_BUTTONS)
    n++;
  return n;
}

uint32_t bind_mask(uint32_t bind)
{
  uint32_t mask = 0;
  for (int i = 0, n = bind_length(bind); i < n; i++)
    mask |= 1u << component(bind, i);
  return mask;
}

/* --- names ------------------------------------------------------------------------------------------- */

void bind_name(uint32_t bind, char *buf, int len)
{
  int n = bind_length(bind), at = 0;
  buf[0] = 0;
  if (!n) {
    snprintf(buf, len, "%s", T("unbound"));
    return;
  }
  for (int i = 0; i < n && at < len; i++)
    at += snprintf(buf + at, len - at, "%s%s", i ? " + " : "", button_names[component(bind, i)]);
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

/* is text[0..n) the word (any case)? */
static bool same_word(const char *text, int n, const char *word)
{
  int i = 0;
  for (; i < n && word[i]; i++)
    if (lower(text[i]) != lower(word[i]))
      return false;
  return i == n && !word[i];
}

bool bind_parse(const char *text, uint32_t *out)
{
  uint32_t bind = BIND_NONE;
  int n = 0;
  const char *p = text;
  while (*p == ' ')
    p++;
  if (!*p || same_word(p, (int)strlen(p), "unbound") || same_word(p, (int)strlen(p), "none")) {
    *out = BIND_NONE;
    return true;
  }
  while (*p) {
    while (*p == ' ')
      p++;
    const char *start = p;
    while (*p && *p != ' ' && !(*p == '+' && p > start)) /* "+" between buttons; "plus" is the button */
      p++;
    int len = (int)(p - start);
    int button = -1;
    for (int b = 0; b < N_BUTTONS && button < 0; b++)
      if (same_word(start, len, button_names[b]))
        button = b;
    if (button < 0 || n >= BIND_MAX)
      return false;
    if (!(bind_mask(bind) & 1u << button)) { /* each button once */
      bind = (bind & ~(0xFFu << (8 * n))) | (uint32_t)button << (8 * n);
      n++;
    }
    while (*p == ' ' || *p == '+')
      p++;
  }
  *out = bind;
  return n > 0;
}

/* --- evaluation (gz's input_update) ------------------------------------------------------------------ */

#define REPEAT_FIRST 12 /* frames before a held bind repeats (input.c's menu repeat) */
#define REPEAT_EVERY 3

static bool disabled[CMD_COUNT];
static uint8_t component_state[CMD_COUNT]; /* bit i: the bind's i-th button went down, in order */
static int active_frames[CMD_COUNT];       /* 0: not active */
static bool pressed_now[CMD_COUNT], repeat_now[CMD_COUNT], cancelled_now[CMD_COUNT];
static uint32_t held_now;

void binds_set_enabled(command_t cmd, bool enabled) { disabled[cmd] = !enabled; }

void binds_update(uint32_t held, uint32_t pressed, uint32_t released)
{
  held_now = held;
  if (!binds_ready)
    binds_reset();
  bool active[CMD_COUNT];
  uint32_t masks[CMD_COUNT];
  for (int i = 0; i < CMD_COUNT; i++) {
    uint32_t bind = binds[i];
    masks[i] = bind_mask(bind);
    uint8_t *cs = &component_state[i];
    cancelled_now[i] = false;
    int j = 0, n = bind_length(bind);
    if (!binds_enabled || disabled[i] || !n) {
      *cs = 0;
      active[i] = false;
      continue;
    }
    if (command_info[i].any_order) { /* all held, the last of them just pressed; until one is let go */
      bool all = (held & masks[i]) == masks[i];
      active[i] = all && (active_frames[i] || (pressed & masks[i]));
      *cs = active[i] ? 1 : 0;
      continue;
    }
    uint8_t before = *cs;
    for (; j < n; j++) {
      uint32_t bit = 1u << component(bind, j);
      uint8_t csm = (uint8_t)(1u << j);
      if (*cs & csm) {
        if (held & bit)
          continue;
        /* let go: from the middle, the bind starts again; the last, it waits for that one again */
        if (*cs & ~((1u << (j + 1)) - 1))
          *cs = 0;
        else
          *cs &= (uint8_t)~csm;
        break;
      }
      if (released & bit) {
        *cs = 0;
        break;
      }
      if (before && (pressed & ~masks[i] & CANCEL_BITS)) { /* something else pressed meanwhile */
        *cs = 0;
        cancelled_now[i] = true;
        break;
      }
      if (pressed & bit)
        *cs |= csm;
      else
        break;
    }
    active[i] = *cs && j == n;
  }
  /* a bind inside another active one gives way to it */
  for (int i = 0; i < CMD_COUNT; i++) {
    for (int k = 0; active[i] && k < CMD_COUNT; k++) {
      if (k != i && active[k] && masks[i] != masks[k] && (masks[i] & masks[k]) == masks[i]) {
        component_state[i] = 0;
        active[i] = false;
        cancelled_now[i] = true;
      }
    }
  }
  for (int i = 0; i < CMD_COUNT; i++) {
    if (!active[i] && active_frames[i] && component_state[i] == 0 && (held & masks[i]) == masks[i] &&
        masks[i] && !cancelled_now[i])
      cancelled_now[i] = true; /* its buttons are still down: something ended it, not letting go */
    active_frames[i] = active[i] ? active_frames[i] + 1 : 0;
    pressed_now[i] = active_frames[i] == 1;
    repeat_now[i] = pressed_now[i] || (active_frames[i] > REPEAT_FIRST && (active_frames[i] - REPEAT_FIRST) %
                                                                              REPEAT_EVERY == 1);
  }
}

bool cmd_pressed(command_t cmd) { return pressed_now[cmd]; }
bool cmd_repeat(command_t cmd) { return repeat_now[cmd]; }
bool cmd_active(command_t cmd) { return active_frames[cmd] > 0; }
bool cmd_cancelled(command_t cmd) { return cancelled_now[cmd]; }
uint32_t cmd_mask(command_t cmd) { return bind_mask(binds[cmd]); }

bool cmd_held(command_t cmd)
{
  uint32_t mask = cmd_mask(cmd);
  return binds_enabled && !disabled[cmd] && mask && (held_now & mask) == mask;
}

/* --- menu text --------------------------------------------------------------------------------------- */

/* the menu's default words in hints, and the commands now bound in their place (two: "d-left/right") */
static const struct { const char *word; command_t first, second; } menu_words[] = {
  { "d-left/right", CMD_LEFT, CMD_RIGHT }, { "d-l/r", CMD_LEFT, CMD_RIGHT }, /* not menu text */
  { "d-up/down", CMD_UP, CMD_DOWN }, { "d-u/d", CMD_UP, CMD_DOWN }, /* not menu text */
  { "R + d-left", CMD_RETURN, CMD_COUNT }, /* not menu text */
  { "d-left", CMD_LEFT, CMD_COUNT }, { "d-right", CMD_RIGHT, CMD_COUNT }, /* not menu text */
  { "d-up", CMD_UP, CMD_COUNT }, { "d-down", CMD_DOWN, CMD_COUNT }, /* not menu text */
  { "L", CMD_SELECT, CMD_COUNT }, { "B", CMD_BACK, CMD_COUNT }, /* not menu text */
};
#define N_MENU_WORDS (sizeof(menu_words) / sizeof(*menu_words))

static bool word_char(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '/' ||
         c == '_';
}

static bool changed(command_t cmd) { return cmd < CMD_COUNT && binds[cmd] != defaults[cmd]; }

const char *binds_expand(const char *in, char *out, int len)
{
  if (!in)
    return in;
  bool any_menu = false;
  for (int c = CMD_RETURN; c <= CMD_SELECT; c++)
    any_menu |= changed((command_t)c);
  bool any_key = false;
  for (const char *p = in; *p; p++)
    any_key |= *p == '{';
  if (!any_menu && !any_key)
    return in;
  int at = 0;
  char name[48], second[48];
  for (const char *p = in; *p && at < len - 1;) {
    if (*p == '{') { /* {key} */
      const char *end = p + 1;
      while (*end && *end != '}')
        end++;
      int found = -1;
      for (int c = 0; c < CMD_COUNT && found < 0 && *end; c++)
        if (same_word(p + 1, (int)(end - p - 1), command_keys[c]))
          found = c;
      if (found >= 0) {
        bind_name(binds[found], name, sizeof(name));
        at += snprintf(out + at, len - at, "%s", name);
        p = end + 1;
        at = at < len - 1 ? at : len - 1;
        continue;
      }
    }
    if (any_menu && (p == in || !word_char(p[-1]))) { /* a menu word, whole */
      int matched = -1, n = 0;
      for (unsigned w = 0; w < N_MENU_WORDS && matched < 0; w++) {
        n = (int)strlen(menu_words[w].word);
        int k = 0;
        while (k < n && p[k] == menu_words[w].word[k])
          k++;
        if (k == n && !word_char(p[n]))
          matched = (int)w;
      }
      if (matched >= 0 && (changed(menu_words[matched].first) || changed(menu_words[matched].second))) {
        bind_name(binds[menu_words[matched].first], name, sizeof(name));
        if (menu_words[matched].second < CMD_COUNT) {
          bind_name(binds[menu_words[matched].second], second, sizeof(second));
          at += snprintf(out + at, len - at, "%s/%s", name, second);
        } else {
          at += snprintf(out + at, len - at, "%s", name);
        }
        p += n;
        at = at < len - 1 ? at : len - 1;
        continue;
      }
      if (matched >= 0) { /* as it is */
        for (int k = 0; k < n && at < len - 1; k++)
          out[at++] = *p++;
        continue;
      }
    }
    out[at++] = *p++;
  }
  out[at < len ? at : len - 1] = 0;
  return out;
}

/* --- recording a bind (gz's binder) ------------------------------------------------------------------- */

#define CLEAR_FRAMES (GAME_FPS / 2)   /* the button that chose the row held this long: unbind */
#define GIVE_UP_FRAMES (6 * GAME_FPS) /* nothing pressed for this long: the bind stays as it was */

static int recording = -1;  /* the command, or -1 */
/* 1 waiting for everything to be let go, 2 for a button, 3 taking them, 4 asking: keep it or not */
static int record_state;
static int record_frames;
static uint32_t record_bind, record_chooser;
static char record_text[64];
bool binds_changed;

void binds_record_start(command_t cmd)
{
  recording = cmd;
  record_state = 1;
  record_frames = 0;
  record_bind = BIND_NONE;
  record_chooser = held_now; /* what chose the row: held on, it unbinds */
  binds_enabled = false;
}

bool binds_recording(command_t cmd) { return recording == (int)cmd; }

const char *binds_record_text(void)
{
  if (record_state < 3)
    return T("press the buttons, in order");
  bind_name(record_bind, record_text, sizeof(record_text));
  return record_text;
}

const char *binds_record_hint(void)
{
  switch (record_state) {
    case 1: return "let go of everything (L held: unbound)";
    case 2: return "press the new buttons in order (nothing for 6 seconds: no change)";
    case 3: return "let go of them all when done";
    default: return "L: keep this bind   B: cancel (no change)";
  }
}

static void record_end(const char *how)
{
  char name[48];
  bind_name(binds[recording], name, sizeof(name));
  gfx_logf("%s: %s%s", T(command_info[recording].name), name, how);
  recording = -1;
  record_state = 0;
  binds_enabled = true;
}

/* all of a command's buttons held, one of them just pressed (any order): the menu's own, read raw while the
 * commands are off */
static bool raw_press(command_t cmd, uint32_t held, uint32_t pressed)
{
  uint32_t mask = bind_mask(binds[cmd]);
  return mask && (held & mask) == mask && (pressed & mask);
}

bool binds_record_frame(void)
{
  if (recording < 0)
    return false;
  uint32_t held = input_held(), pressed = input_pressed();
  record_frames++;
  if (record_state == 1) {
    if (!held) {
      record_state = 2;
      record_frames = 0;
    } else if (record_chooser && (held & record_chooser) == record_chooser && record_frames >= CLEAR_FRAMES) {
      if (command_info[recording].required) {
        record_end(T(" (needed: kept)"));
      } else {
        binds[recording] = BIND_NONE;
        binds_changed = true;
        record_end("");
      }
      return false;
    }
    return true;
  }
  if (record_state == 4) { /* select (as bound now) keeps it, back or return leaves the old one */
    if (raw_press(CMD_SELECT, held, pressed)) {
      binds_changed |= binds[recording] != record_bind;
      binds[recording] = record_bind;
      record_end("");
      return false;
    }
    if (raw_press(CMD_BACK, held, pressed) || raw_press(CMD_RETURN, held, pressed)) {
      record_end(T(" (cancelled: no change)"));
      return false;
    }
    return true;
  }
  if (record_state == 2) {
    if (!pressed) {
      if (record_frames < GIVE_UP_FRAMES)
        return true;
      record_end(T(" (nothing pressed: no change)"));
      return false;
    }
    record_state = 3;
  }
  /* 3: each button as it goes down, until all are let go */
  int n = bind_length(record_bind);
  for (int b = 0; b < N_BUTTONS && n < BIND_MAX; b++) {
    if (pressed & 1u << b && !(bind_mask(record_bind) & 1u << b)) {
      record_bind = (record_bind & ~(0xFFu << (8 * n))) | (uint32_t)b << (8 * n);
      n++;
    }
  }
  if (!held)
    record_state = 4;
  return true;
}
