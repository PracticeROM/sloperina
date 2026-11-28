/* Drowning (FINDINGS 9.47): the actors that drown in deep water and come back (cuccos, dogs, horses and Epona), their
 * drown timer watch and where they come back.
 *
 * Their action AI (the BSA action controller, [[[comps+0x188]+0x1B0]+0x80], +0x1F0 the actor) loads a drowning module
 * among its definitions (count +0x168, array +0x170; a definition's file at +0x10, its name the file's first string):
 * Kokko.action.interruptdrown (cuccos), Animal_Dog.action.InterruptDrown, Animal_Horse.action.interruptdrown (which
 * runs HorseReturnToSafePositionWithEvent). The controller's units (count +0x18, array +0x20, 0x18 each) go with its
 * definitions (unit +0x10 the definition), and a unit's module nodes are made just after it ([node+8] the unit, the
 * node's index in its file at +0x1C): their counters, read here, are the drowning's timers. */
#include "drownview.h"

#include <stdio.h>
#include <string.h>

#include "airwall.h"
#include "enemyhp.h" /* actor_read */
#include "gfx.h"
#include "lang.h"
#include "link.h" /* frame_counter */
#include "memfile.h" /* memfile_loading */
#include "sys.h"
#include "watches.h"
#include "z64hd.h"

float sinf(float x), cosf(float x); /* from sdk */

#define PI 3.14159265f

/* --- an actor's drowning module, its units and counters ----------------------------------------------------- */

#define CTRL_ACTOR 0x1F0
#define CTRL_UNITS_N 0x18
#define CTRL_UNITS 0x20
#define CTRL_DEFS_N 0x168
#define CTRL_DEFS 0x170
#define DEFS_MAX 0x15E    /* the loader's cap */
#define UNIT_DEF 0x10
#define UNIT_SIZE 0x18    /* a unit's slot in the controller's array */
#define NODE_UNIT 8
#define NODE_INDEX 0x1C   /* u16: its index in its file */
#define NODE_SPAN 0x8000  /* bytes after a unit its module's nodes are looked for in */
#define FRAME_COUNT_NOW 0x20  /* SelectorGenericFrameCounter: frames gone by */
#define FRAME_COUNT_LEN 0x50  /* ... a pointer (low bits flags) to its length */
#define GENERIC_LEFT 0x20     /* ExecuteGenericCounter: frames left */
#define GENERIC_LEN 0x2C      /* ... its length */
#define RECHECK 60            /* frames an actor's lookup is kept */
#define NODES_MAX 12

typedef struct {
  uintptr_t node;
  uint16_t index;
  bool frames;  /* a SelectorGenericFrameCounter (counts up), else an ExecuteGenericCounter (counts down) */
  float last;   /* its value last frame (the watched actor's) */
  float delta;  /* how much it changed this frame */
  int quiet;    /* frames since it changed */
} counter_t;

typedef struct {
  uintptr_t actor;
  uint32_t at;          /* frame looked up */
  int kind;             /* DV_* */
  uintptr_t ctrl, unit, back_unit; /* the drowning's unit; a horse's HorseReturnToSafePositionWithEvent unit */
  counter_t counters[NODES_MAX];
  int n_counters;
} info_t;

#define INFOS 8
static info_t infos[INFOS];
static int next_info;
static uint32_t now; /* frames, counted here */

static bool is_ctrl(uintptr_t p, uintptr_t actor)
{
  return p && !(p & 7) && sys_mapped(p, CTRL_DEFS + 8, SYS_PERM_R) && sys_ptr(p + CTRL_ACTOR) == actor;
}

/* a definition's name (its file's first string: "Kokko.action.interruptdrown.module"), or 0 */
static const char *def_name(uintptr_t def)
{
  uintptr_t file = def && !(def & 7) && sys_mapped(def, 0x18, SYS_PERM_R) ? sys_ptr(def + 0x10) : 0;
  if (!file || !sys_mapped(file, 0x80, SYS_PERM_R) || memcmp((const void *)file, "AIB ", 4))
    return 0;
  uint32_t pool = *(const uint32_t *)(file + 0x24);
  if (pool < 0x80 || pool > 0x1000000 || !sys_mapped(file + pool, 64, SYS_PERM_R))
    return 0;
  return (const char *)(file + pool);
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

/* s starts with w (any case) */
static bool starts(const char *s, const char *w)
{
  for (; *w; s++, w++)
    if (lower(*s) != lower(*w))
      return false;
  return true;
}

/* the drowning module this controller loaded: its kind, and its definition (and a horse's return one) */
static int drown_def(uintptr_t ctrl, uintptr_t *def, uintptr_t *back)
{
  uint32_t n = 0;
  if (!sys_read32(ctrl + CTRL_DEFS_N, &n) || !n || n > DEFS_MAX)
    return DV_NONE;
  uintptr_t defs = sys_ptr(ctrl + CTRL_DEFS);
  if (!defs || (defs & 7) || !sys_mapped(defs, n * 8, SYS_PERM_R))
    return DV_NONE;
  int kind = DV_NONE;
  *def = *back = 0;
  for (uint32_t i = 0; i < n; i++) {
    uintptr_t d = sys_ptr(defs + 8 * i);
    const char *name = def_name(d);
    if (!name)
      continue;
    if (starts(name, "Kokko.action.interruptdrown.module"))
      kind = DV_CUCCO, *def = d;
    else if (starts(name, "Animal_Dog.action.InterruptDrown.module"))
      kind = DV_DOG, *def = d;
    else if (starts(name, "Animal_Horse.action.interruptdrown.module"))
      kind = DV_HORSE, *def = d;
    else if (starts(name, "HorseReturnToSafePositionWithEvent.module"))
      *back = d;
  }
  if (kind == DV_HORSE && !*back)
    return DV_NONE;
  return kind;
}

/* the controller's unit for a definition */
static uintptr_t unit_of(uintptr_t ctrl, uintptr_t def)
{
  uint32_t n = 0;
  if (!def || !sys_read32(ctrl + CTRL_UNITS_N, &n) || n > 64)
    return 0;
  uintptr_t units = sys_ptr(ctrl + CTRL_UNITS);
  if (!units || (units & 7) || !sys_mapped(units, n * UNIT_SIZE, SYS_PERM_R))
    return 0;
  for (uint32_t j = 0; j < n; j++) {
    uintptr_t unit = sys_ptr(units + UNIT_SIZE * j);
    if (unit && !(unit & 7) && sys_mapped(unit, 0x40, SYS_PERM_R) && sys_ptr(unit + UNIT_DEF) == def)
      return unit;
  }
  return 0;
}

static bool counter_ok(const counter_t *c, uintptr_t unit)
{
  if (!z64hd_game || !sys_mapped(c->node, 0x58, SYS_PERM_R) || sys_ptr(c->node + NODE_UNIT) != unit)
    return false;
  uint32_t vt = c->frames ? z64hd_game->frame_counter_node_vtable : z64hd_game->generic_counter_node_vtable;
  return vt && sys_ptr(c->node) == z64hd_main + vt;
}

/* the unit's counter nodes, made just after it */
static void find_counters(info_t *in, uintptr_t unit)
{
  if (!unit || !z64hd_game || !z64hd_game->frame_counter_node_vtable)
    return;
  const uint64_t frames = z64hd_main + z64hd_game->frame_counter_node_vtable,
                 generic = z64hd_main + z64hd_game->generic_counter_node_vtable;
  size_t span = NODE_SPAN;
  while (span >= 0x1000 && !sys_mapped(unit, span, SYS_PERM_R))
    span /= 2;
  if (span < 0x1000)
    return;
  for (const uint64_t *w = (const uint64_t *)unit; (uintptr_t)w < unit + span && in->n_counters < NODES_MAX; w++) {
    if (*w != frames && (*w != generic || !z64hd_game->generic_counter_node_vtable))
      continue;
    uintptr_t node = (uintptr_t)w;
    if (!sys_mapped(node, 0x58, SYS_PERM_R) || sys_ptr(node + NODE_UNIT) != unit)
      continue;
    counter_t *c = &in->counters[in->n_counters++];
    c->node = node;
    c->index = *(const uint16_t *)(node + NODE_INDEX);
    c->frames = *w == frames;
    c->last = -1;
    c->quiet = 1000;
  }
}

/* the action controller: where cuccos' and horses' is ([[[comps+0x188]+0x1B0]+0x80]), else looked for under it */
static uintptr_t find_ctrl(uintptr_t actor, int *kind, uintptr_t *def, uintptr_t *back)
{
  *kind = DV_NONE;
  uintptr_t comps = sys_ptr(actor + 0x270);
  if (!comps || (comps & 7) || !sys_mapped(comps, 0x190, SYS_PERM_R))
    return 0;
  uintptr_t bsa = sys_ptr(comps + 0x188);
  if (!bsa || (bsa & 7) || !sys_mapped(bsa, 0x200, SYS_PERM_R))
    return 0;
  uintptr_t holder = sys_ptr(bsa + 0x1B0);
  uintptr_t ctrl = holder && !(holder & 7) && sys_mapped(holder, 0x88, SYS_PERM_R) ? sys_ptr(holder + 0x80) : 0;
  if (is_ctrl(ctrl, actor) && (*kind = drown_def(ctrl, def, back)) != DV_NONE)
    return ctrl;
  for (int j = 0; j < 0x200; j += 8) {
    uintptr_t d = sys_ptr(bsa + j);
    if (!d || (d & 7) || d == actor || d == comps || !sys_mapped(d, 0x100, SYS_PERM_R))
      continue;
    for (int i = 0; i < 0x100; i += 8) {
      uintptr_t c = sys_ptr(d + i);
      if (is_ctrl(c, actor) && (*kind = drown_def(c, def, back)) != DV_NONE)
        return c;
    }
  }
  *kind = DV_NONE;
  return 0;
}

static info_t *info_of(uintptr_t actor)
{
  if (!actor)
    return 0;
  info_t *in = 0;
  for (int i = 0; i < INFOS && !in; i++)
    if (infos[i].actor == actor)
      in = &infos[i];
  if (in && now - in->at < RECHECK && (in->kind == DV_NONE || is_ctrl(in->ctrl, actor)))
    return in;
  if (in && in->kind != DV_NONE && is_ctrl(in->ctrl, actor)) { /* still good: kept (its counters' history too) */
    in->at = now;
    return in;
  }
  if (!in) {
    in = &infos[next_info];
    next_info = (next_info + 1) % INFOS;
  }
  memset(in, 0, sizeof(*in));
  in->actor = actor;
  in->at = now;
  uintptr_t def = 0, back = 0;
  in->ctrl = find_ctrl(actor, &in->kind, &def, &back);
  if (in->kind != DV_NONE) {
    in->unit = unit_of(in->ctrl, def);
    in->back_unit = unit_of(in->ctrl, back);
    find_counters(in, in->kind == DV_HORSE ? in->back_unit : in->unit);
  }
  return in;
}

int drownview_kind(uintptr_t actor)
{
  info_t *in = info_of(actor);
  return in ? in->kind : DV_NONE;
}

/* a counter's frames left and its length; false if it isn't there any more */
static bool counter_read(const info_t *in, const counter_t *c, float *left, float *length)
{
  uintptr_t unit = in->kind == DV_HORSE ? in->back_unit : in->unit;
  if (!counter_ok(c, unit))
    return false;
  float v, len = 0;
  memcpy(&v, (const void *)(c->node + (c->frames ? FRAME_COUNT_NOW : GENERIC_LEFT)), 4);
  if (c->frames) {
    uintptr_t p = sys_ptr(c->node + FRAME_COUNT_LEN) & ~(uintptr_t)3;
    if (p && sys_mapped(p, 4, SYS_PERM_R))
      memcpy(&len, (const void *)p, 4);
    *left = len - v;
  } else {
    memcpy(&len, (const void *)(c->node + GENERIC_LEN), 4);
    *left = v;
  }
  if (*left < 0)
    *left = 0;
  *length = len;
  return true;
}

/* --- the watches: the actor chosen in the actor browser ------------------------------------------------------- */

/* the cucco's module's counters (Kokko.action.interruptdrown: node indices), the dog's, the horse's return's */
enum {
  CUCCO_IN_WATER = 35,  /* 30 in the water first */
  CUCCO_LOOK = 125,     /* up to 120 turning to the land it'll jump for */
  CUCCO_JUMP = 77,      /* each jump (DrownJumpCountMax: 3), up to 600 till it lands */
  CUCCO_JUMP_AIR = 119, /* 10 in the air before it checks where it landed */
  CUCCO_SINK = 14,      /* still in the water after them: it fades out (FadeFrame 6) and after 60 it's put back */
  CUCCO_FADE_IN = 18,   /* 10 fading in */
  CUCCO_LAND = 31,      /* 60 on landing */
  DOG_SINK = 3,         /* 60, then it's put back at home (no fade) */
  HORSE_STRUGGLE = 17,  /* WaitFrameBeforeFadeOut: AnimFrameForFadeOut 340 - FadeOutFrame 30; then it fades out */
  HORSE_FADE_OUT = 18,  /* FadeOutFrame 30, from the fade's start; then it's hidden */
  HORSE_GONE = 19,      /* 30 hidden, then it's put on a safe spot */
  HORSE_FADE_IN = 14,   /* FadeInFrame 30 */
};
#define CUCCO_JUMPS 3
#define HORSE_FADE_OUT_LEN 30 /* (counts, while the counters haven't run) */
#define HORSE_GONE_LEN 30
#define CUCCO_SINK_LEN 60
#define CUCCO_FADE_LEN 6 /* its fade out (FadeFrame 6): the first 6 of the sink's count. The model's fade
                          * (OneShotStartModelAlphaFadeOut) goes in the same counts: a horse's 1/30 a count, hidden
                          * when its count of 30 (HORSE_FADE_OUT) ends, 14 frames on */
#define SHOW_BACK 90      /* frames "back" is shown after it comes back */
#define FINDING 600       /* frames "finding a safe spot" is kept up for a horse */
#define HANDOVER 3        /* frames between one counter ending and the next starting, still counted as the first */

#define REBIND_FRAMES 60  /* frames after a memfile load to rebind a watched actor by Banc InstanceId */

static uintptr_t watched;
static uint64_t watched_id;     /* Banc PreActor InstanceId, to rebind across memfile loads */
static char watched_pack[64];   /* fallback for dynamically spawned held actors (id == 0) */
static int watched_rebind;      /* frames left to rebind after memfile load completes */
static int active = -1;     /* the counter running now (its index in the info), -1 none */
static int last_index = -1; /* the node index of the last one that ran */
static int since = 1000;    /* frames since one ran */
static int jumps;           /* the cucco's jumps this time in the water */
/* The counters count the game's own time (two a frame for a horse; a cucco's AI runs every other frame), so what they
 * say is turned into frames by how much the running one has gone on each frame lately */
#define RATE_FRAMES 32
static float rate_of[RATE_FRAMES];
static int n_rate, rate_at;

static void reset_watch_history(uintptr_t actor)
{
  active = -1, last_index = -1, since = 1000, jumps = 0, n_rate = 0, rate_at = 0;
  for (int i = 0; actor && i < INFOS; i++)
    if (infos[i].actor == actor) {
      infos[i].at = 0; /* force re-validation of controller/nodes after load */
      for (int c = 0; c < infos[i].n_counters; c++)
        infos[i].counters[c].last = -1, infos[i].counters[c].quiet = 1000;
    }
}

static bool has_drown_watches(void)
{
  return watch_has(W_DROWN_STATE) || watch_has(W_DROWN) || watch_has(W_FADE_OUT) || watch_has(W_RESPAWN);
}

static void remove_drown_watches(void)
{
  for (int i = 0; i < watch_count; i++)
    if (watch_list[i].id == W_DROWN_STATE || watch_list[i].id == W_DROWN || watch_list[i].id == W_FADE_OUT ||
        watch_list[i].id == W_RESPAWN)
      watch_remove(i--);
}

static bool str_eq(const char *a, const char *b)
{
  while (*a && *a == *b)
    a++, b++;
  return *a == *b;
}

static uintptr_t find_rebind_actor(uint64_t id, const char *pack)
{
  if (id)
    return actor_find_instance(id);
  if (!pack || !pack[0])
    return 0;
  actor_near_t near_list[16];
  int n = actors_near(3.0f, near_list, 16);
  for (int i = 0; i < n; i++) {
    const char *p = actor_pack_name(near_list[i].actor);
    if (p && str_eq(p, pack))
      return near_list[i].actor;
  }
  return 0;
}

static float per_frame(void)
{
  float sum = 0;
  for (int i = 0; i < n_rate; i++)
    sum += rate_of[i];
  return n_rate >= 4 && sum > 0 ? sum / (float)n_rate : 0;
}

void drownview_watch(uintptr_t actor)
{
  if (actor == watched && (!actor || watched_rebind == 0))
    return;
  watched = actor;
  watched_id = 0;
  watched_pack[0] = 0;
  watched_rebind = 0;
  reset_watch_history(actor);
  if (actor) {
    actor_instance_id(actor, &watched_id);
    const char *pack = actor_pack_name(actor);
    if (pack && pack[0])
      snprintf(watched_pack, sizeof(watched_pack), "%s", pack);
    log_printf("drown watch: %#lx (id %#lx)", (unsigned long)actor, (unsigned long)watched_id);
  }
}

uintptr_t drownview_watched(void) { return watched; }

static void watch_step(void)
{
  info_t *in = info_of(watched);
  if (!in || in->kind == DV_NONE)
    return;
  int best = -1;
  for (int i = 0; i < in->n_counters; i++) {
    counter_t *c = &in->counters[i];
    float left, len, v;
    if (!counter_read(in, c, &left, &len))
      continue;
    memcpy(&v, (const void *)(c->node + (c->frames ? FRAME_COUNT_NOW : GENERIC_LEFT)), 4);
    bool changed = c->last >= 0 && v != c->last;
    float d = changed ? v - c->last : 0;
    c->delta = d < 0 ? -d : d;
    if (in->kind == DV_CUCCO && c->index == CUCCO_JUMP && changed && (c->quiet > 2 || v < c->last))
      jumps++; /* a new jump: started again, or after a pause */
    if (in->kind == DV_CUCCO && c->index == CUCCO_IN_WATER && changed && c->quiet > 2)
      jumps = 0; /* in the water again */
    c->quiet = changed ? 0 : c->quiet + 1;
    c->last = v;
    if (c->quiet <= 1 && (best < 0 || c->quiet < in->counters[best].quiet))
      best = i;
  }
  /* a jump's air time runs inside the jump: the jump it is */
  if (best >= 0 && in->kind == DV_CUCCO && in->counters[best].index == CUCCO_JUMP_AIR)
    for (int i = 0; i < in->n_counters; i++)
      if (in->counters[i].index == CUCCO_JUMP && in->counters[i].quiet <= 1)
        best = i;
  active = best;
  if (best >= 0 && in->counters[best].delta < 8) { /* (not a counter starting again) */
    rate_of[rate_at] = in->counters[best].delta;
    rate_at = (rate_at + 1) % RATE_FRAMES;
    if (n_rate < RATE_FRAMES)
      n_rate++;
  }
  if (best >= 0)
    last_index = in->counters[best].index, since = 0;
  else if (since < 100000)
    since++;
}

/* a counter's frames left by its node index (whole: its length instead), or fallback (one not found, not run) */
static float left_of(const info_t *in, int index, float fallback, bool whole)
{
  for (int i = 0; i < in->n_counters; i++)
    if (in->counters[i].index == index) {
      float left, len;
      if (!counter_read(in, &in->counters[i], &left, &len))
        break;
      if (whole)
        return len > 0 ? len : fallback;
      return left;
    }
  return fallback;
}

/* where it is: drowning before the fade, fading out, faded (hidden until it's put back), back, out on its own */
enum { ST_NONE, ST_DROWNING, ST_FADING, ST_GONE, ST_FINDING, ST_BACK, ST_OUT };

static int stage(const info_t *in, int index, bool running, float sunk)
{
  switch (in->kind) {
    case DV_CUCCO:
      if (running && (index == CUCCO_IN_WATER || index == CUCCO_LOOK || index == CUCCO_JUMP || index == CUCCO_JUMP_AIR))
        return ST_DROWNING;
      if (running && index == CUCCO_SINK)
        return sunk < CUCCO_FADE_LEN ? ST_FADING : ST_GONE;
      if ((index == CUCCO_SINK || index == CUCCO_FADE_IN || index == CUCCO_LAND) && since < SHOW_BACK)
        return ST_BACK;
      if ((index == CUCCO_JUMP || index == CUCCO_JUMP_AIR) && since < SHOW_BACK)
        return ST_OUT;
      return ST_NONE;
    case DV_DOG:
      if (running && index == DOG_SINK)
        return ST_DROWNING;
      return index == DOG_SINK && since < SHOW_BACK ? ST_BACK : ST_NONE;
    default: /* a horse: struggling, its fade out (HORSE_FADE_OUT's count), hidden (HORSE_GONE's) */
      if (running && index == HORSE_STRUGGLE)
        return ST_DROWNING;
      if (running && index == HORSE_FADE_OUT)
        return ST_FADING;
      if (running && index == HORSE_GONE)
        return ST_GONE;
      if (index == HORSE_GONE && since < FINDING) /* its count done: the safe spot being found */
        return ST_FINDING;
      if ((index == HORSE_FADE_IN || index == HORSE_GONE) && since < FINDING + SHOW_BACK)
        return ST_BACK;
      return ST_NONE;
  }
}

void drownview_text(int which, char *buf, int len)
{
  info_t *in = watched ? info_of(watched) : 0;
  if (!in || in->kind == DV_NONE) {
    snprintf(buf, len, "-");
    return;
  }
  int index = active >= 0 ? in->counters[active].index : last_index;
  /* (the next counter starts a frame or two after one ends: the one that ended still counts meanwhile) */
  bool running = active >= 0 || (last_index >= 0 && since <= HANDOVER);
  float rate = per_frame(), scale = rate > 0 ? 1 / rate : 1; /* the game's counts to frames */
  float counts = running ? left_of(in, index, 0, false) : 0, left = counts * scale;
  float sunk = in->kind == DV_CUCCO && index == CUCCO_SINK ? left_of(in, CUCCO_SINK, CUCCO_SINK_LEN, true) - counts : 0;
  int st = stage(in, index, running, sunk);
  if (which == DW_STATE) {
    if (st == ST_DROWNING && in->kind == DV_CUCCO && index == CUCCO_IN_WATER)
      snprintf(buf, len, "%s", T("in the water"));
    else if (st == ST_DROWNING && in->kind == DV_CUCCO && index == CUCCO_LOOK)
      snprintf(buf, len, "%s", T("looking for land"));
    else if (st == ST_DROWNING && in->kind == DV_CUCCO)
      snprintf(buf, len, TF("jump %d of %d"), jumps < 1 ? 1 : jumps > CUCCO_JUMPS ? CUCCO_JUMPS : jumps, CUCCO_JUMPS);
    else {
      static const char *const words[] = { [ST_NONE] = "idle", [ST_DROWNING] = "drowning", [ST_FADING] = "fading out",
                                           [ST_GONE] = "hidden", [ST_FINDING] = "finding a safe spot",
                                           [ST_BACK] = "respawned", [ST_OUT] = "jumped out" };
      snprintf(buf, len, "%s", T(words[st]));
    }
    return;
  }
  if (st == ST_NONE || st == ST_OUT) {
    snprintf(buf, len, "-");
    return;
  }
  if (st == ST_BACK) {
    snprintf(buf, len, "0");
    return;
  }
  float out_len =
      (in->kind == DV_CUCCO ? CUCCO_FADE_LEN : left_of(in, HORSE_FADE_OUT, HORSE_FADE_OUT_LEN, true)) * scale;
  float gone_len = left_of(in, HORSE_GONE, HORSE_GONE_LEN, true) * scale;
  float fade_left = 0, back = -1; /* frames left of the fade out; until it's put back (-1 not known yet) */
  if (in->kind == DV_DOG)
    back = left;
  else if (in->kind == DV_CUCCO) {
    if (st == ST_FADING || st == ST_GONE)
      back = left, fade_left = (CUCCO_FADE_LEN - sunk) * scale;
  } else if (st == ST_DROWNING)
    back = left + out_len + gone_len;
  else if (st == ST_FADING)
    back = left + gone_len, fade_left = left;
  else if (st == ST_GONE)
    back = left;
  switch (which) {
    case DW_DROWN: /* until it starts to fade out (a cucco: what's left of its step; its jumps can't be known) */
      if (st != ST_DROWNING)
        snprintf(buf, len, "0");
      else if (in->kind == DV_CUCCO && index != CUCCO_IN_WATER && index != CUCCO_LOOK)
        snprintf(buf, len, "-");
      else
        snprintf(buf, len, "%.0f", (double)left);
      break;
    case DW_FADE: /* the fade out: its length before it, what's left of it during it */
      if (in->kind == DV_DOG)
        snprintf(buf, len, "%s", T("no fade"));
      else if (st == ST_DROWNING)
        snprintf(buf, len, "%.0f", (double)out_len);
      else if (st == ST_FADING)
        snprintf(buf, len, "%.0f", (double)(fade_left > 0 ? fade_left : 0));
      else
        snprintf(buf, len, "0");
      break;
    default: /* until it's put back */
      if (st == ST_FINDING)
        snprintf(buf, len, "%s", T("finding a safe spot"));
      else if (back < 0)
        snprintf(buf, len, TF("%.0f after its jumps"), (double)(CUCCO_SINK_LEN * scale));
      else
        snprintf(buf, len, "%.0f", (double)back);
      break;
  }
}

/* --- where it comes back --------------------------------------------------------------------------------------- */

#define ACTOR_HOME 0x2EC        /* its home position (QueryActorGetHomePosition), unless +0x519 bit 1 says ... */
#define ACTOR_HOME_FLAGS 0x519  /* ... it's [[[comps+0x50]+0x20]+0x180]+0x10 */
#define BB_VEC3_COUNT 0x200     /* the actor blackboard's Vec3f names: count, names (char *), values (12 bytes) */
#define BB_VEC3_NAMES 0x208
#define BB_VEC3_VALUES 0x218

static bool finite3(const float p[3])
{
  for (int i = 0; i < 3; i++)
    if (!(p[i] == p[i]) || p[i] > 1e6f || p[i] < -1e6f)
      return false;
  return true;
}

static bool home_of(uintptr_t actor, float out[3])
{
  uintptr_t at = actor + ACTOR_HOME;
  if (*(const uint8_t *)(actor + ACTOR_HOME_FLAGS) & 2) {
    uintptr_t comps = sys_ptr(actor + 0x270);
    uintptr_t phys = comps && !(comps & 7) ? sys_ptr(comps + 0x50) : 0;
    uintptr_t a = phys && !(phys & 7) && sys_mapped(phys, 0x28, SYS_PERM_R) ? sys_ptr(phys + 0x20) : 0;
    uintptr_t b = a && !(a & 7) && sys_mapped(a, 0x188, SYS_PERM_R) ? sys_ptr(a + 0x180) : 0;
    if (b && !(b & 7) && sys_mapped(b + 0x10, 12, SYS_PERM_R))
      at = b + 0x10;
  }
  memcpy(out, (const void *)at, 12);
  return finite3(out);
}

/* a Vec3f of the actor's blackboard by name ("WarpPos") */
static bool blackboard_vec3(uintptr_t actor, const char *name, float out[3])
{
  uintptr_t comps = sys_ptr(actor + 0x270);
  uintptr_t first = comps && !(comps & 7) ? sys_ptr(comps) : 0;
  uintptr_t bb = first && !(first & 7) && sys_mapped(first, 0x18, SYS_PERM_R) ? sys_ptr(first + 0x10) : 0;
  uint32_t n = 0;
  if (!bb || (bb & 7) || !sys_mapped(bb, BB_VEC3_VALUES + 8, SYS_PERM_R) || !sys_read32(bb + BB_VEC3_COUNT, &n) ||
      !n || n > 64)
    return false;
  uintptr_t names = sys_ptr(bb + BB_VEC3_NAMES), values = sys_ptr(bb + BB_VEC3_VALUES);
  if (!names || (names & 7) || !values || !sys_mapped(names, n * 8, SYS_PERM_R) ||
      !sys_mapped(values, n * 12, SYS_PERM_R))
    return false;
  size_t len = strlen(name) + 1;
  for (uint32_t i = 0; i < n; i++) {
    uintptr_t s = sys_ptr(names + 8 * i);
    if (s && sys_mapped(s, len, SYS_PERM_R) && !memcmp((const void *)s, name, len)) {
      memcpy(out, (const void *)(values + 12 * i), 12);
      return finite3(out);
    }
  }
  return false;
}

/* a horse's last landing (QueryRidableGetLastLandingMtx): its ridable [comps+0x3A0], whose [+0x78] gives a matrix
 * (3 rows of 4) at +0xC through its slot 4 (add x0, x0, #0xc; ret: checked); NaN there: none yet */
static bool last_landing(uintptr_t actor, float out[3])
{
  uintptr_t comps = sys_ptr(actor + 0x270);
  uintptr_t rid = comps && !(comps & 7) && sys_mapped(comps, 0x3A8, SYS_PERM_R) ? sys_ptr(comps + 0x3A0) : 0;
  uintptr_t land = rid && !(rid & 7) && sys_mapped(rid, 0x80, SYS_PERM_R) ? sys_ptr(rid + 0x78) : 0;
  if (!land || (land & 7) || !sys_mapped(land, 0x3C, SYS_PERM_R))
    return false;
  uintptr_t vt = sys_ptr(land), get = vt && sys_mapped(vt, 0x28, SYS_PERM_R) ? sys_ptr(vt + 0x20) : 0;
  if (!get || !sys_mapped(get, 8, SYS_PERM_R) || *(const uint32_t *)get != 0x91003000u ||
      *(const uint32_t *)(get + 4) != 0xD65F03C0u)
    return false;
  const float *m = (const float *)(land + 0xC);
  if (!(m[0] == m[0]))
    return false;
  out[0] = m[3], out[1] = m[7], out[2] = m[11];
  return finite3(out);
}

int drownview_respawn(uintptr_t actor, float out[3])
{
  switch (drownview_kind(actor)) {
    case DV_CUCCO: /* its WarpPos, where it last landed safely (put on the navmesh near it), else home */
      if (blackboard_vec3(actor, "WarpPos", out) && (out[0] != 0 || out[1] != 0 || out[2] != 0))
        return DV_AT_WARP;
      return home_of(actor, out) ? DV_AT_HOME : DV_AT_NONE;
    case DV_DOG:
      return home_of(actor, out) ? DV_AT_HOME : DV_AT_NONE;
    case DV_HORSE:
      return last_landing(actor, out) ? DV_AT_LANDING : DV_AT_SAFE_SPOT;
    default:
      return DV_AT_NONE;
  }
}

/* each actor's own switch: its respawn point drawn */
#define SHOWN_MAX 8
typedef struct {
  uintptr_t actor;
  uint64_t id;
  char pack[64];
  int rebind;
} shown_entry_t;
static shown_entry_t shown[SHOWN_MAX];
static int n_shown;

bool drownview_own(uintptr_t actor)
{
  for (int i = 0; actor && i < n_shown; i++)
    if (shown[i].actor == actor)
      return true;
  return false;
}

void drownview_toggle(uintptr_t actor)
{
  for (int i = 0; i < n_shown; i++)
    if (shown[i].actor == actor) {
      shown[i] = shown[--n_shown];
      log_printf("respawn point: %#lx off", (unsigned long)actor);
      return;
    }
  if (n_shown == SHOWN_MAX)
    memmove(&shown[0], &shown[1], sizeof(shown[0]) * --n_shown);
  shown_entry_t *e = &shown[n_shown++];
  *e = (shown_entry_t){ .actor = actor };
  actor_instance_id(actor, &e->id);
  const char *pack = actor_pack_name(actor);
  if (pack && pack[0])
    snprintf(e->pack, sizeof(e->pack), "%s", pack);
  float p[3] = { 0, 0, 0 };
  int at = drownview_respawn(actor, p);
  log_printf("respawn point: %#lx on (%d: %.2f %.2f %.2f)", (unsigned long)actor, at, (double)p[0], (double)p[1],
             (double)p[2]);
  if (at == DV_AT_SAFE_SPOT)
    gfx_notice(T("it hasn't landed anywhere yet: it comes back on the nearest safe spot"));
}

bool drownview_active(void)
{
  for (int i = 0; i < n_shown; i++)
    if (shown[i].actor)
      return true;
  return false;
}

void drownview_frame(void)
{
  now++;
  static bool was_loading;
  bool loading = memfile_loading();
  bool in_load = loading || was_loading;
  actor_read_t a;
  if (watched) {
    if (!actor_read(watched, &a) || !a.running) {
      if (in_load && (watched_id || watched_pack[0])) {
        watched = 0;
        watched_rebind = REBIND_FRAMES;
        reset_watch_history(0);
      } else {
        drownview_watch(0);
        remove_drown_watches();
      }
    } else if (was_loading && !loading) {
      reset_watch_history(watched);
    }
  } else if (watched_id || watched_pack[0]) {
    if (!has_drown_watches()) {
      watched_id = 0;
      watched_pack[0] = 0;
      watched_rebind = 0;
    } else if (!loading) {
      uintptr_t found = find_rebind_actor(watched_id, watched_pack);
      if (found && drownview_kind(found) != DV_NONE) {
        watched = found;
        watched_rebind = 0;
        reset_watch_history(found);
        log_printf("drown watch: rebound %#lx (id %#lx)", (unsigned long)found, (unsigned long)watched_id);
      } else if (--watched_rebind <= 0) {
        watched_id = 0;
        watched_pack[0] = 0;
        watched_rebind = 0;
        remove_drown_watches();
      }
    }
  }
  for (int i = 0; i < n_shown; i++) {
    if (shown[i].actor) {
      if (!actor_read(shown[i].actor, &a) || !a.running) {
        if (in_load && (shown[i].id || shown[i].pack[0])) {
          shown[i].actor = 0;
          shown[i].rebind = REBIND_FRAMES;
        } else {
          shown[i--] = shown[--n_shown];
        }
      }
    } else if (!loading) {
      uintptr_t found = find_rebind_actor(shown[i].id, shown[i].pack);
      if (found && drownview_kind(found) != DV_NONE) {
        shown[i].actor = found;
        shown[i].rebind = 0;
        log_printf("respawn point: rebound %#lx (id %#lx)", (unsigned long)found, (unsigned long)shown[i].id);
      } else if (--shown[i].rebind <= 0) {
        shown[i--] = shown[--n_shown];
      }
    }
  }
  was_loading = loading;
  static uint32_t last_frame;
  uint32_t frame = z64hd_game ? frame_counter() : 0;
  if (watched && frame != last_frame) /* only frames the game ran (frame advance holds it, the pause screen...) */
    watch_step();
  last_frame = frame;
}

/* the respawn points: a ring on the ground with a beam, and a line to the actor while it's away from it */
#define RING_RADIUS 0.5f
#define RING_SEGMENTS 16
#define BEAM_HEIGHT 2.0f
#define BEAM_WIDTH 0.04f

static int n_out, max_out;
static float (*out_ends)[2][3];
static uint8_t *out_colours;

static void line(const float a[3], const float b[3])
{
  if (n_out >= max_out)
    return;
  memcpy(out_ends[n_out][0], a, sizeof(float) * 3);
  memcpy(out_ends[n_out][1], b, sizeof(float) * 3);
  out_colours[n_out++] = AW_RESPAWN;
}

int drownview_lines(float ends[][2][3], uint8_t colours[], int max)
{
  out_ends = ends, out_colours = colours, n_out = 0, max_out = max;
  for (int s = 0; s < n_shown; s++) {
    if (!shown[s].actor)
      continue;
    float p[3];
    int at = drownview_respawn(shown[s].actor, p);
    if (at == DV_AT_NONE || at == DV_AT_SAFE_SPOT)
      continue;
    float prev[3], q[3];
    for (int i = 0; i <= RING_SEGMENTS; i++) {
      float ang = (float)i * (2 * PI / RING_SEGMENTS);
      q[0] = p[0] + sinf(ang) * RING_RADIUS, q[1] = p[1] + 0.03f, q[2] = p[2] + cosf(ang) * RING_RADIUS;
      if (i)
        line(prev, q);
      memcpy(prev, q, sizeof(prev));
    }
    for (int i = 0; i < 3; i++) {
      float ang = (float)i * (2 * PI / 3);
      float bottom[3] = { p[0] + sinf(ang) * BEAM_WIDTH, p[1], p[2] + cosf(ang) * BEAM_WIDTH };
      float top[3] = { bottom[0], p[1] + BEAM_HEIGHT, bottom[2] };
      line(bottom, top);
    }
    actor_read_t a;
    if (actor_read(shown[s].actor, &a)) {
      float dx = a.pos[0] - p[0], dy = a.pos[1] - p[1], dz = a.pos[2] - p[2];
      if (dx * dx + dy * dy + dz * dz > 1.0f) {
        float top[3] = { p[0], p[1] + BEAM_HEIGHT, p[2] };
        line(top, a.pos);
      }
    }
  }
  return n_out;
}

/* --- debugging: an actor's drowning module, counters and respawn point, to the log (remote "aidump") ------------ */

void drownview_dump(uintptr_t actor)
{
  info_t *in = info_of(actor);
  float home[3] = { 0, 0, 0 }, warp[3] = { 0, 0, 0 }, p[3] = { 0, 0, 0 };
  home_of(actor, home);
  bool has_warp = blackboard_vec3(actor, "WarpPos", warp);
  int at = drownview_respawn(actor, p);
  log_printf("aidump: %#lx: kind %d, ctrl %#lx, unit %#lx, back %#lx, %d counters; home %.2f %.2f %.2f, WarpPos %s "
             "%.2f %.2f %.2f, respawn %d at %.2f %.2f %.2f", /* not menu text */
             (unsigned long)actor, in ? in->kind : -1, (unsigned long)(in ? in->ctrl : 0),
             (unsigned long)(in ? in->unit : 0), (unsigned long)(in ? in->back_unit : 0), in ? in->n_counters : 0,
             (double)home[0], (double)home[1], (double)home[2], has_warp ? "yes" : "no", (double)warp[0],
             (double)warp[1], (double)warp[2], at, (double)p[0], (double)p[1], (double)p[2]);
  if (!in || in->kind == DV_NONE)
    return;
  for (int i = 0; i < in->n_counters; i++) {
    float left = -1, len = -1;
    counter_read(in, &in->counters[i], &left, &len);
    const char *what = in->counters[i].frames ? "frame counter" : "generic counter"; /* not menu text */
    log_printf("aidump:  counter node %u (%s) %#lx: %.1f left of %.1f", (unsigned)in->counters[i].index, what,
               (unsigned long)in->counters[i].node, (double)left, (double)len);
  }
}
