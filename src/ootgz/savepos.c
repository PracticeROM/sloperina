/* Where a load puts Link: AutoSaveMgr's slots (FINDINGS §9.7).
 *
 * AutoSaveMgr is entry 12 of the current scene's components (§9.3), checked against its vtable
 * (z64hd vt_autosave). It holds two slots: B (+0x178), which Link's restart point is handed
 * on to (main+0x60830C), and A (+0x198), set elsewhere, which wins when it's valid. Each frame the game copies
 * the winner to PlayerStatus.SavePos / SavePosRadY, and a save stores that. Each is drawn as a ring on the
 * ground, an arrow the way Link will face and a pole; the one SavePos takes has a cross on its pole.
 *
 * Where a load puts him: the load runs a search round SavePos for a safe place (main+0x945310, called by
 * SceneBeginning's SeqQueryPlayerSearchSafetyPlace). It's run here with Link's actor and the slot SavePos takes,
 * and its answer drawn in yellow. The search only looks at that point and the scene, but Link's own body is part
 * of the scene: on a load he stands on the point, here he may be anywhere within its reach, where his body can
 * be taken for ground or block a sweep. So it's run while he stands on the point (as on a load), and that answer
 * kept when he walks off; a point he isn't on (slot A, or one handed on after he left) is searched as it is, and
 * marked approximate, until he stands there. It tests the scene as it is now: a moving block or an actor that won't be there after a load can
 * change the answer. Riding Epona the game passes it a riding info; here it's always run as on foot. */
#include "savepos.h"

#include "airwall.h"
#include "lang.h"
#include "link.h"
#include "sys.h"
#include "z64hd.h"

float sinf(float x); /* from sdk */
float cosf(float x);
float sqrtf(float x);
float atan2f(float y, float x);

bool savepos_shown;
bool savepos_load = true;

/* Where putting the ocarina away would move slot B: Link's feet, while the only thing that stops the game moving
 * it there is the ground not being valid (savepos_frame) */
static struct {
  bool shown;
  bool no_valid_ground; /* none near (bit 24); else valid ground marked not to keep the point (bit 20) */
  savepos_slot_t slot;
} ocarina;

#define SCENE_CURRENT 0x1F0    /* [scene manager +this]: the current scene */
#define SCENE_COUNT 0x50       /* u32 */
#define SCENE_COMPONENTS 0x58  /* pointer to the components' pointers */
#define SCENE_COMPONENTS_MAX 64
#define AUTOSAVE_ENTRY 12      /* scene components[12] = AutoSaveMgr */
static const uint32_t slot_offset[SAVEPOS_SLOTS] = { 0x198, 0x178 };
#define SLOT_SIZE 0x14         /* f32 yaw, f32 x, y, z, u8 valid (and two flags) */
#define SLOT_VALID 0x10

/* the markers (metres) */
#define RING_R 0.4f
#define RING_SEGMENTS 12
#define RING_LIFT 0.02f   /* over the point, so the ground doesn't cut it */
#define ARROW_LEN 0.75f
#define ARROW_HEAD 0.22f
#define CROSS_R 0.15f
static const float pole_height[SAVEPOS_SLOTS] = { 1.8f, 1.4f }; /* apart, for when both are in one place */
#define TWO_PI 6.2831853f
#define LOAD_POLE 2.2f
#define MOVED_MIN 0.01f   /* metres: closer than this the load puts him where he saved */
#define SEARCH_GAP 4      /* frames between searches while SavePos moves */
#define SEARCH_REFRESH 60 /* and while it doesn't and he's on it, for what moves round it */
#define ON_POINT 0.15f    /* metres: Link this close to the point is standing on it, as on a load */
#define OCARINA_POLE 1.0f /* slot B where the ocarina would put it: lower than the real one's */
#define ACTOR_POS 0x31C   /* the actor's position (his feet; what the restart point is taken from) */

static uintptr_t cached_scene, cached_mgr;

/* AutoSaveMgr of the current scene (entry AUTOSAVE_ENTRY), or 0. The scene and the manager are kept
 * while they stay the same. */
static uintptr_t autosave_mgr(void)
{
  if (!z64hd_game || !z64hd_game->vt_autosave || !z64hd_game->scene_mgr_ptr)
    return 0;
  uintptr_t scene_mgr = sys_ptr(MAIN(z64hd_game->scene_mgr_ptr));
  uintptr_t scene = scene_mgr ? sys_ptr(scene_mgr + SCENE_CURRENT) : 0;
  if (!scene)
    return 0;
  uintptr_t want_vtable = MAIN(z64hd_game->vt_autosave);
  if (scene == cached_scene && cached_mgr && sys_ptr(cached_mgr) == want_vtable)
    return cached_mgr;
  cached_scene = scene;
  cached_mgr = 0;
  uint32_t count;
  uintptr_t components = sys_ptr(scene + SCENE_COMPONENTS);
  if (!components || !sys_read32(scene + SCENE_COUNT, &count) ||
      count <= AUTOSAVE_ENTRY || count > SCENE_COMPONENTS_MAX)
    return 0;
  uintptr_t component = sys_ptr(components + AUTOSAVE_ENTRY * sizeof(uintptr_t));
  if (component && sys_ptr(component) == want_vtable) {
    cached_mgr = component;
    log_printf("save position: AutoSaveMgr %#lx (scene entry %u)", (unsigned long)component,
               (unsigned)AUTOSAVE_ENTRY);
  }
  return cached_mgr;
}

/* FINDINGS §9.3 (a lead): AutoSaveMgr +0x1DC holds a short name for what Link is doing. Read as text in
 * place; anything that doesn't look like a name (unprintable, unterminated) gives "". */
#define LINK_STATE 0x1DC
#define LINK_STATE_MAX 32

bool savepos_link_state(char *buf, int len)
{
  buf[0] = 0;
  uintptr_t mgr = autosave_mgr();
  if (!mgr || len < 2 || !sys_mapped(mgr + LINK_STATE, LINK_STATE_MAX, SYS_PERM_R))
    return false;
  const volatile char *s = (const volatile char *)(mgr + LINK_STATE);
  for (int i = 0; i < LINK_STATE_MAX; i++) {
    char c = s[i];
    if (!c) {
      if (i == 0 || i >= len)
        break;
      for (int j = 0; j <= i; j++)
        buf[j] = s[j];
      return true;
    }
    if (c < 0x21 || c > 0x7E)
      break;
  }
  buf[0] = 0;
  return false;
}

bool savepos_slots(savepos_slot_t slots[SAVEPOS_SLOTS])
{
  uintptr_t mgr = autosave_mgr();
  /* B is first in memory, A after it */
  if (!mgr || !sys_mapped(mgr + slot_offset[SAVEPOS_B], slot_offset[SAVEPOS_A] + SLOT_SIZE - slot_offset[SAVEPOS_B],
                          SYS_PERM_R))
    return false;
  for (int s = 0; s < SAVEPOS_SLOTS; s++) {
    const volatile uint8_t *slot = (const volatile uint8_t *)(mgr + slot_offset[s]);
    const volatile float *f = (const volatile float *)slot;
    slots[s].yaw = f[0];
    for (int i = 0; i < 3; i++)
      slots[s].pos[i] = f[1 + i];
    slots[s].valid = slot[SLOT_VALID] != 0;
  }
  return true;
}

/* --- markers ------------------------------------------------------------------------------------ */

static int n_out, max_out;
static float (*ends_out)[2][3];
static uint8_t *colours_out;

static void line(const float a[3], const float b[3], uint8_t colour)
{
  if (n_out >= max_out)
    return;
  memcpy(ends_out[n_out][0], a, sizeof(float) * 3);
  memcpy(ends_out[n_out][1], b, sizeof(float) * 3);
  colours_out[n_out++] = colour;
}

static void ring(const float centre[3], uint8_t colour)
{
  float y = centre[1] + RING_LIFT;
  for (int j = 0; j < RING_SEGMENTS; j++) {
    float a0 = TWO_PI * j / RING_SEGMENTS, a1 = TWO_PI * (j + 1) / RING_SEGMENTS;
    float u[3] = { centre[0] + RING_R * sinf(a0), y, centre[2] + RING_R * cosf(a0) };
    float v[3] = { centre[0] + RING_R * sinf(a1), y, centre[2] + RING_R * cosf(a1) };
    line(u, v, colour);
  }
}

/* along the ground, from one point to another */
static void ground_line(const float from[3], const float to[3], uint8_t colour)
{
  float a[3] = { from[0], from[1] + RING_LIFT, from[2] };
  float b[3] = { to[0], to[1] + RING_LIFT, to[2] };
  line(a, b, colour);
}

static void marker(const savepos_slot_t *slot, float pole, bool taken, uint8_t colour)
{
  const float *p = slot->pos;
  float y = p[1] + RING_LIFT;
  ring(p, colour);
  /* the way he'll face: yaw as atan2(x, z) */
  float dx = sinf(slot->yaw), dz = cosf(slot->yaw);
  float from[3] = { p[0], y, p[2] };
  float tip[3] = { p[0] + ARROW_LEN * dx, y, p[2] + ARROW_LEN * dz };
  line(from, tip, colour);
  for (int side = -1; side <= 1; side += 2) {
    /* back along the arrow and out to one side (the side: (dz, -dx)) */
    float barb[3] = { tip[0] - ARROW_HEAD * (dx + side * dz), y, tip[2] - ARROW_HEAD * (dz - side * dx) };
    line(tip, barb, colour);
  }
  float top[3] = { p[0], p[1] + pole, p[2] };
  line(p, top, colour);
  if (taken) {
    float a[3] = { top[0] - CROSS_R, top[1], top[2] }, b[3] = { top[0] + CROSS_R, top[1], top[2] };
    float c[3] = { top[0], top[1], top[2] - CROSS_R }, d[3] = { top[0], top[1], top[2] + CROSS_R };
    line(a, b, colour);
    line(c, d, colour);
  }
}

/* --- the load's search ---------------------------------------------------------------------------- */

typedef bool (*safety_search_t)(uintptr_t actor, const float pos[3], float out[3], const void *riding, bool flag,
                                int unused);

typedef struct {
  const char *name;  /* for the log */
  bool done;         /* a search has run for `from` */
  bool found;        /* it found a place (else the load leaves him at `from`) */
  bool exact;        /* Link stood on `from` when it ran, as on a load */
  float from[3], to[3];
  uint32_t frame;    /* frames_seen when it ran */
} search_t;

/* round the slot a save keeps (yellow), and round slot B as the ocarina would set it (pale yellow) */
static search_t load_search = { .name = "load" };
static search_t ocarina_search = { .name = "load after the ocarina" };
static uint32_t frames_seen;

static float distance(const float a[3], const float b[3])
{
  float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* Link's position (his actor's), false if it can't be read */
static bool link_pos(uintptr_t actor, float out[3])
{
  if (!actor || !sys_mapped(actor + ACTOR_POS, 12, SYS_PERM_R))
    return false;
  for (int i = 0; i < 3; i++)
    out[i] = ((const volatile float *)(actor + ACTOR_POS))[i];
  return true;
}

static bool search_moves_link(const search_t *s)
{
  return s->done && s->found && distance(s->from, s->to) > MOVED_MIN;
}

static void run_search(search_t *s, uintptr_t actor, const float from[3], bool on_point, const float link[3])
{
  static bool logged;
  if (!logged) /* if the first call takes the game down, the log says where */
    log_printf("save position: first search from (%.2f, %.2f, %.2f)", (double)from[0], (double)from[1],
               (double)from[2]);
  float out[3] = { from[0], from[1], from[2] };
  bool found = MAIN_FN(safety_search_t, z64hd_game->safety_search)(actor, from, out, 0, false, 0);
  bool moved = found && distance(from, out) > MOVED_MIN;
  if (!logged || moved != search_moves_link(s) || on_point != s->exact)
    log_printf("save position: %s %s (%.2f, %.2f, %.2f)%s; from (%.2f, %.2f, %.2f), link %s (%.2f, %.2f, %.2f)",
               s->name, moved ? "moves link to" : "keeps link at", (double)out[0], (double)out[1], (double)out[2],
               found ? "" : ", nothing found", (double)from[0], (double)from[1], (double)from[2],
               on_point ? "on it" : "away", (double)link[0], (double)link[1], (double)link[2]);
  logged = true;
  s->done = true;
  s->found = found;
  s->exact = on_point;
  memcpy(s->from, from, sizeof(s->from));
  memcpy(s->to, out, sizeof(s->to));
  s->frame = frames_seen;
}

/* the search for `from`: a new point at once (then at most every SEARCH_GAP frames while it moves); the same one
 * again only with Link on it, as on a load (at once if the answer was approximate, then every SEARCH_REFRESH
 * frames) */
static void update_search(search_t *s, const float from[3])
{
  uintptr_t actor = link_actor();
  float link[3];
  if (!actor || !z64hd_game->safety_search || !link_pos(actor, link)) {
    s->done = false; /* not an old answer for another place */
    return;
  }
  bool on_point = distance(link, from) < ON_POINT;
  uint32_t since = frames_seen - s->frame;
  bool same = s->done && s->from[0] == from[0] && s->from[1] == from[1] && s->from[2] == from[2];
  if (!s->done || (!same && since >= SEARCH_GAP) || (same && on_point && (!s->exact || since >= SEARCH_REFRESH)))
    run_search(s, actor, from, on_point, link);
}

/* where the load puts him: a ring and a tall pole, and a line from the saved point when it moves him */
static void load_marker(const search_t *s, uint8_t colour)
{
  ring(s->to, colour);
  float top[3] = { s->to[0], s->to[1] + LOAD_POLE, s->to[2] };
  line(s->to, top, colour);
  if (search_moves_link(s))
    ground_line(s->from, s->to, colour);
}

void savepos_load_text(char *buf, int len)
{
  const search_t *s = &load_search;
  if (!savepos_shown || !savepos_load || !s->done)
    snprintf(buf, len, TF("load  not searched"));
  else if (!search_moves_link(s))
    snprintf(buf, len, TF("load  stays%s"), s->exact ? "" : T("  (approx.)"));
  else
    snprintf(buf, len, TF("load  %.2f  %.2f  %.2f  moved %.2f%s"), (double)s->to[0], (double)s->to[1],
             (double)s->to[2], (double)distance(s->from, s->to), s->exact ? "" : T("  (approx.)"));
}

int savepos_lines(float ends[][2][3], uint8_t colours[], int max)
{
  frames_seen++;
  savepos_slot_t slots[SAVEPOS_SLOTS];
  if (!savepos_shown || !savepos_slots(slots))
    return 0;
  ends_out = ends;
  colours_out = colours;
  n_out = 0;
  max_out = max;
  static const uint8_t colour[SAVEPOS_SLOTS] = { AW_SLOT_A, AW_SLOT_B };
  int taken = slots[SAVEPOS_A].valid ? SAVEPOS_A : SAVEPOS_B; /* A wins when it's set */
  for (int s = 0; s < SAVEPOS_SLOTS; s++)
    if (slots[s].valid)
      marker(&slots[s], pole_height[s], s == taken, colour[s]);

  if (savepos_load && slots[taken].valid) {
    update_search(&load_search, slots[taken].pos);
    if (load_search.done)
      load_marker(&load_search, AW_LOAD);
  } else {
    load_search.done = false;
  }

  /* slot B as it would be with the ocarina put away, and where a load would put him from it (that only changes the
   * load while slot A isn't set) */
  if (ocarina.shown)
    marker(&ocarina.slot, OCARINA_POLE, taken == SAVEPOS_B, AW_OCARINA);
  if (ocarina.shown && savepos_load && taken == SAVEPOS_B) {
    update_search(&ocarina_search, ocarina.slot.pos);
    if (ocarina_search.done)
      load_marker(&ocarina_search, AW_OCARINA_LOAD);
  } else {
    ocarina_search.done = false;
  }
  return n_out;
}

/* --- where the ocarina would put slot B ------------------------------------------------------------------- */

/* What the restart point update (main+0x60A41C) reads, FINDINGS 9.7. Link's player component: actor +0x270, +0x358. */
#define ACTOR_COMPONENTS 0x270
#define COMPONENTS_PLAYER 0x358
#define COMPONENTS_BODY 0x3A8   /* +0x38 == 1: standing on a body (a moving block, a platform) */
#define ACTOR_FACING_X 0x330    /* the way he faces, as the update takes it: atan2(x, z) */
#define ACTOR_FACING_Z 0x348
#define P_GROUND 0x13D0         /* ground flags, rebuilt each frame by main+0x5F90A0 */
#define P_FORCED 0x1948         /* f32, NaN unless an area sensor forces the point */
#define NAN_BITS 0x7FC00000u
#define EVENT_RUNNING 0x68      /* [main+event_state_ptr] +this: non-zero while an event runs (the ocarina is one) */

/* the ground flags */
#define GROUND_STANDING (1u << 2)
#define GROUND_NO_VALID_NEAR (1u << 24)   /* no valid ground within 5 m (main+0x5FA87C) */
#define GROUND_MARKED_NOT_KEPT (1u << 20) /* valid ground near, marked not to keep the point (main+0x5FB84C) */
#define GROUND_HOLDS_OTHERWISE 0x0082A02u /* the update's other reasons in these flags (bits 1, 9, 11, 13, 19) */

/* Link's state id ([[components+0x188]+0x1C0] +0x28, or +0x4C when that is 0x11A), or -1 */
static int link_state(uintptr_t components)
{
  uintptr_t holder = sys_ptr(components + 0x188);
  uintptr_t state = holder ? sys_ptr(holder + 0x1C0) : 0;
  uint32_t id;
  if (!state || !sys_read32(state + 0x28, &id))
    return -1;
  if (id == 0x11A && !sys_read32(state + 0x4C, &id))
    return -1;
  return (int)id;
}

static bool standing_on_body(uintptr_t components)
{
  uintptr_t body = sys_ptr(components + COMPONENTS_BODY);
  uint32_t kind;
  return body && sys_read32(body + 0x38, &kind) && kind == 1;
}

static bool event_running(void)
{
  uintptr_t events = sys_ptr(MAIN(z64hd_game->event_state_ptr));
  return !events || sys_ptr(events + EVENT_RUNNING);
}

/* the states the update skips in (FINDINGS 9.7), or Link's state can't be read */
static bool state_holds_point(int state)
{
  return state < 0 || state == 0x22 || state == 0x23 || state == 0x24 || state == 0x56 || state == 0x9C ||
         state == 0xFF;
}

/* The game would move the restart point to Link now but for the ground not being valid: one of those two flags is
 * set, and none of the update's other main reasons to skip holds. */
static bool only_ground_holds_point(uintptr_t components, uintptr_t player)
{
  uint32_t ground = *(const volatile uint32_t *)(player + P_GROUND);
  if (!(ground & (GROUND_NO_VALID_NEAR | GROUND_MARKED_NOT_KEPT)))
    return false;
  if (!(ground & GROUND_STANDING) || ground & GROUND_HOLDS_OTHERWISE)
    return false;
  if (*(const volatile uint32_t *)(player + P_FORCED) != NAN_BITS)
    return false;
  if (event_running() || standing_on_body(components) || state_holds_point(link_state(components)))
    return false;
  return true;
}

/* Read only: where the game won't move slot B to Link, notes where putting the ocarina away would. */
void savepos_frame(void)
{
  ocarina.shown = false;
  if (!z64hd_game || !z64hd_game->event_state_ptr)
    return;
  uintptr_t actor = link_actor();
  uintptr_t components = actor ? sys_ptr(actor + ACTOR_COMPONENTS) : 0;
  uintptr_t player = components ? sys_ptr(components + COMPONENTS_PLAYER) : 0;
  if (!player || !sys_mapped(player + P_GROUND, P_FORCED + 4 - P_GROUND, SYS_PERM_R) ||
      !sys_mapped(actor + ACTOR_POS, ACTOR_FACING_Z + 4 - ACTOR_POS, SYS_PERM_R))
    return;
  if (!only_ground_holds_point(components, player))
    return;
  const volatile float *a = (const volatile float *)actor;
  ocarina.shown = true;
  ocarina.no_valid_ground = *(const volatile uint32_t *)(player + P_GROUND) & GROUND_NO_VALID_NEAR;
  for (int i = 0; i < 3; i++)
    ocarina.slot.pos[i] = a[ACTOR_POS / 4 + i];
  ocarina.slot.yaw = atan2f(a[ACTOR_FACING_X / 4], a[ACTOR_FACING_Z / 4]);
  ocarina.slot.valid = true;
}

void savepos_here_text(char *buf, int len)
{
  if (!ocarina.shown)
    snprintf(buf, len, TF("here  kept by the game"));
  else if (ocarina.no_valid_ground)
    snprintf(buf, len, TF("here  not kept: no valid ground near (ocarina keeps it)"));
  else
    snprintf(buf, len, TF("here  not kept: ground marked not to keep (ocarina keeps it)"));
}

void savepos_text(int slot, char *buf, int len)
{
  static const char *const names[SAVEPOS_SLOTS] = { "a", "b" };
  savepos_slot_t slots[SAVEPOS_SLOTS];
  if (!savepos_slots(slots)) {
    snprintf(buf, len, TF("%s  not found"), names[slot]);
    return;
  }
  const savepos_slot_t *s = &slots[slot];
  if (!s->valid) {
    snprintf(buf, len, TF("%s  not set"), names[slot]);
    return;
  }
  float degrees = s->yaw * (360.0f / TWO_PI);
  bool taken = slot == SAVEPOS_A || !slots[SAVEPOS_A].valid;
  snprintf(buf, len, TF("%s  %.2f  %.2f  %.2f  yaw %.0f%s"), names[slot], (double)s->pos[0], (double)s->pos[1],
           (double)s->pos[2], (double)degrees, taken ? "  *" : "");
}
