/* Hookshot storage (1.0; read live).
 *
 * Link's AI component A = [[actor+0x270]+0x3D8] keeps a small hash map of records: buckets at [A+0x20], 0x38 bytes
 * each (+0 the key, +8 how many records, +0x10 the records, 0xF8 bytes each), count A+0x2C. The hookshot's is key
 * 0xAD28A0F2 for the longshot, 0x9472DF51 for the hookshot (main+0x2DAC3C0 on 1.0, the code behind
 * QueryEquipmentUserTryGetHookshotHookState, reads the current weapon's).
 * In its record:
 *   +0xD8 u8  1 while a hookshot action runs
 *   +0xDE u8  the hook state: 0 idle, 2 flying out, 3 hooked and pulling, 4 coming back after a miss
 *   +0xE0 u8  1 = ready; a shot clears it as it starts and sets it again when it ends (not always: a shot that
 *             ends with Link climbing a net leaves it clear too, so storage is told by the hook state instead)
 *   +0xAC vec3 the spot the shot aimed at
 * A hit on the shot's first frame (a bomb's DamageM) ends it before the hook flies (+0xDE never leaves 0) and
 * before +0xE0 is set again: that is storage. The next shot skips flying (state 0 -> 3) and pulls Link towards
 * the spot the cancelled shot left, through walls; on another map it crashes. 1.1.1 checks each frame of the
 * pull that a real object is hooked and lets go if not, so there it should do nothing (from the code; not tried
 * in game).
 *
 * Force: on the frame a shot starts (+0xD8 0 -> 1, state still 0) without storage held (not the shot that uses it)
 * it writes a medium hit into Link's hit
 * component H = [actor+0x270]+0x2B8: the words a real bomb's DamageM wrote there (recorded live), with the frame
 * numbers made current, the bomb put in front of Link and his own pointers. The level alone (H+0x2D1 = 2; 4 is
 * the huge hit ragdoll.c uses) isn't enough for a medium hit: the reaction selector also reads the hit's contact
 * records (the lists at H+0x210 and H+0x1338). Written from outside the game this way it gave DamageM from
 * standing every time, and storage on 2 to 3 shots of 12 (the write's timing was luck there). 1.0 only: the
 * record's vtable is a 1.0 offset, and storage doesn't work on 1.1.1 anyway. */
#include "hsstore.h"

#include "airwall.h"
#include "enemyhp.h" /* actors_named */
#include "gfx.h"
#include "link.h"
#include "sys.h"
#include "z64hd.h"

#define ACTOR_COMPONENTS 0x270
#define COMPONENTS_HIT 0x2B8
#define COMPONENTS_AI 0x3D8
#define AI_BUCKETS 0x20
#define AI_BUCKET_COUNT 0x2C
#define BUCKET_SIZE 0x38
#define BUCKET_RECORDS 0x10
#define RECORD_SIZE 0xF8
#define MAX_BUCKETS 64
/* the record keys: one per weapon, each with its own record and its own storage */
static const struct { uint32_t key; const char *name; } weapons[] = {
  { 0xAD28A0F2u, "longshot" }, /* the one tested */
  { 0x9472DF51u, "hookshot" }, /* same layout, the other record; assumed from that */
};
#define N_WEAPONS (sizeof(weapons) / sizeof(*weapons))
#define REC_ACTIVE 0xD8
#define REC_STATE 0xDE
#define REC_READY 0xE0
#define REC_TARGET 0xAC
#define H_SIZE 0x1D20
#define ACTOR_POSITION 0x31C
#define HIT_REVISION 65536 /* the build the record was taken from */

/* The medium hit, as a bomb left it in H (1.0) */
enum { W_RAW, W_FRAME, W_MAIN64, W_H64, W_POS, W_DIR };
static const struct { uint16_t off; uint8_t kind; uint32_t value; } hit_words[] = {
  { 0x0A0, W_RAW, 0xE7 },        { 0x0B8, W_RAW, 0xE7 },        { 0x0DC, W_RAW, 0x100 },
  { 0x0E0, W_RAW, 0x10 },        { 0x0E4, W_RAW, 0x1080000 },   { 0x0E8, W_FRAME, 0 },
  { 0x0EC, W_RAW, 2 },           { 0x108, W_MAIN64, 0x4910168 }, { 0x118, W_RAW, 0xE7 },
  { 0x128, W_POS, 0 },           /* Link's own position */
  { 0x134, W_RAW, 0x3F7D8D7E },  { 0x148, W_MAIN64, 0x4910168 }, { 0x164, W_FRAME, 0 },
  { 0x184, W_RAW, 0xBF365606 },  { 0x198, W_RAW, 1 },           { 0x200, W_FRAME, 0 },
  { 0x224, W_RAW, 8 },           { 0x228, W_RAW, 1 },           { 0x248, W_RAW, 0xE7 },
  { 0x250, W_RAW, 0x10 },        { 0x254, W_RAW, 0x1080000 },   { 0x27C, W_POS, 0 },
  { 0x288, W_DIR, 0 },           { 0x2A0, W_POS, 0 },           { 0x2AC, W_DIR, 0 },
  { 0x2B8, W_RAW, 0x343FD },     { 0x2BC, W_RAW, 0x10100 },     { 0x2C0, W_RAW, 0x80 },
  { 0x2C4, W_FRAME, 0 },         { 0x2C8, W_RAW, 2 },           { 0x2CC, W_RAW, 0x3000000 },
  { 0x2D0, W_RAW, 0x10207 },     /* byte 0x2D1: level 2, medium */
  { 0x2D8, W_H64, 0x2F8 },       { 0x2E0, W_H64, 0x400 },       { 0x2F0, W_RAW, 1 },
  { 0x2F8, W_RAW, 0 },           { 0x2FC, W_RAW, 0 },           { 0x31C, W_FRAME, 0 },
  { 0x338, W_MAIN64, 0x4910168 }, { 0x348, W_RAW, 0xE7 },       { 0x350, W_POS, 0 },
  { 0x35C, W_DIR, 0 },           { 0x3C8, W_POS, 0 },           { 0x3D4, W_DIR, 0 },
};
/* where the recording had Link, the contact point and its vectors (x, y, z); the direction 0x288 points from
 * Link to the bomb */
static const float rec_link[3] = { -15.208672f, 103.9f, -52.467163f };
static const float rec_contact[3] = { -16.383236f, 104.33767f, -52.167282f };
static const float rec_dir[3] = { -0.911341f, 0.339585f, 0.232677f };
static const float rec_push[3] = { -2.687742f, -0.546574f, -0.596417f };

static bool force_on;
/* +0xE0 clear isn't enough: a normal shot onto a climbable net also leaves it clear. Storage is kept here: the
 * last shot ended with the hook never out of state 0, and +0xE0 hasn't been set since. Kept through a load. */
typedef struct {
  bool stored;
  int have;          /* -1 no record, 0 no storage, 1 storage */
  bool last_active;
  /* the shot going on: whether it used storage, whether we hit it, the hook states it went through (a bit each) */
  bool shot_on, shot_used, shot_forced;
  uint8_t shot_states;
  int shot_frames;
} weapon_t;
static weapon_t track[N_WEAPONS] = { { .have = -1 }, { .have = -1 } };

static uintptr_t next(uintptr_t p, size_t off)
{
  return p && sys_mapped(p + off, 8, SYS_PERM_R) ? sys_ptr(p + off) : 0;
}

static uintptr_t weapon_record(uint32_t key)
{
  uintptr_t ai = next(next(link_actor(), ACTOR_COMPONENTS), COMPONENTS_AI);
  uint32_t count;
  if (!ai || !sys_read32(ai + AI_BUCKET_COUNT, &count) || !count || count > MAX_BUCKETS)
    return 0;
  uintptr_t buckets = next(ai, AI_BUCKETS);
  if (!buckets || !sys_mapped(buckets, count * BUCKET_SIZE, SYS_PERM_R))
    return 0;
  for (uint32_t i = 0; i < count; i++) {
    uintptr_t b = buckets + i * BUCKET_SIZE;
    uint32_t n;
    if (*(const volatile uint32_t *)b != key || !sys_read32(b + 8, &n) || !n)
      continue;
    uintptr_t rec = sys_ptr(b + BUCKET_RECORDS);
    return rec && sys_mapped(rec, RECORD_SIZE, SYS_PERM_R) ? rec : 0;
  }
  return 0;
}

static uint8_t u8_at(uintptr_t a) { return *(const volatile uint8_t *)a; }

int hsstore_have(void)
{
  int have = -1;
  for (unsigned i = 0; i < N_WEAPONS; i++)
    if (track[i].have > have)
      have = track[i].have;
  return have;
}

bool hsstore_force_enabled(void)
{
  return force_on;
}

static bool force_known(void)
{
  return z64hd_game && z64hd_game->revision == HIT_REVISION;
}

void hsstore_force_set(bool on)
{
  if (on && !force_known()) {
    gfx_notice("forcing hookshot storage needs the 1.0 game");
    return;
  }
  force_on = on;
}

/* (x, z) turned by the complex number (cr, ci) */
static void turn(const float *in, float cr, float ci, float *out)
{
  out[0] = in[0] * cr - in[2] * ci;
  out[1] = in[1];
  out[2] = in[0] * ci + in[2] * cr;
}

/* write the medium hit into Link's hit component, with the bomb in front of him; false if he can't be read */
static bool medium_hit(void)
{
  uintptr_t actor = link_actor();
  uintptr_t h = next(next(actor, ACTOR_COMPONENTS), COMPONENTS_HIT);
  double fx, fz;
  if (!h || (h & 7) || !sys_mapped(h, H_SIZE, SYS_PERM_R | SYS_PERM_W) ||
      !sys_mapped(actor + ACTOR_POSITION, 12, SYS_PERM_R) || !link_facing(&fx, &fz))
    return false;
  const float *pos = (const float *)(actor + ACTOR_POSITION);
  /* the turn that takes the recorded direction (x, z) onto his facing */
  float tx = rec_dir[0], tz = rec_dir[2], tl = __builtin_sqrtf(tx * tx + tz * tz);
  tx /= tl, tz /= tl;
  float cr = (float)fx * tx + (float)fz * tz, ci = (float)fz * tx - (float)fx * tz;
  float contact_off[3] = { rec_contact[0] - rec_link[0], rec_contact[1] - rec_link[1], rec_contact[2] - rec_link[2] };
  float contact[3], dir[3], push[3];
  turn(contact_off, cr, ci, contact);
  for (int i = 0; i < 3; i++)
    contact[i] += pos[i];
  turn(rec_dir, cr, ci, dir);
  turn(rec_push, cr, ci, push);
  uint32_t frame = frame_counter() + 1;
  for (unsigned i = 0; i < sizeof(hit_words) / sizeof(*hit_words); i++) {
    uintptr_t at = h + hit_words[i].off;
    switch (hit_words[i].kind) {
    case W_RAW: *(volatile uint32_t *)at = hit_words[i].value; break;
    case W_FRAME: *(volatile uint32_t *)at = frame; break;
    case W_MAIN64: *(volatile uint64_t *)at = MAIN(hit_words[i].value); break;
    case W_H64: *(volatile uint64_t *)at = h + hit_words[i].value; break;
    case W_POS: {
      const float *src = hit_words[i].off == 0x128 ? pos : contact;
      for (int k = 0; k < 3; k++)
        ((volatile float *)at)[k] = src[k];
      break;
    }
    case W_DIR: {
      const float *src = (hit_words[i].off == 0x2AC || hit_words[i].off == 0x3D4) ? push : dir;
      for (int k = 0; k < 3; k++)
        ((volatile float *)at)[k] = src[k];
      break;
    }
    }
  }
  return true;
}

/* --- telling storage from a shot that was only cancelled ------------------------------------------------------ */

static void weapon_frame(unsigned i)
{
  weapon_t *w = &track[i];
  const char *name = weapons[i].name;
  uintptr_t rec = weapon_record(weapons[i].key);
  if (!rec) {
    w->have = -1;
    w->last_active = w->shot_on = false;
    return;
  }
  bool active = u8_at(rec + REC_ACTIVE), ready = u8_at(rec + REC_READY);
  uint8_t state = u8_at(rec + REC_STATE);
  if (!active && ready)
    w->stored = false;

  if (active && !w->last_active) { /* a shot starts */
    w->shot_on = true, w->shot_used = w->stored, w->shot_forced = false, w->shot_states = 0, w->shot_frames = 0;
    if (force_on && force_known() && !w->stored && state == 0) {
      w->shot_forced = medium_hit();
      if (!w->shot_forced)
        log_printf("hs storage: couldn't write the hit (no hit component)");
    }
  }
  if (w->shot_on) {
    w->shot_states |= (uint8_t)(1u << (state & 7));
    w->shot_frames++;
    if (!active) { /* it ended: cancelled before the hook flew is storage */
      if (w->shot_used) /* storage held, but the hook flew: the game ran it as a normal shot */
        log_printf("hs storage: that %s storage was %s", name,
                   w->shot_states & 4 ? "fake (the hook flew)" : "real (no flying)"); /* not menu text */
      w->stored = w->shot_states == 1;
      log_printf("hs storage: %s shot (%s%s): %d frames, hook states%s%s%s%s%s", name,
                 w->shot_used ? "used storage" : "fresh", w->shot_forced ? ", hit" : "", /* not menu text */
                 w->shot_frames, w->shot_states & 1 ? " 0" : "",
                 w->shot_states & 4 ? " 2 (flew)" : "", w->shot_states & 8 ? " 3 (pulled)" : "", /* not menu text */
                 w->shot_states & 16 ? " 4 (came back)" : "", w->stored ? " -> storage" : ""); /* not menu text */
      w->shot_on = false;
    }
  }
  int have = w->stored && !active && !ready;
  if (have != w->have && w->have >= 0) {
    const float *t = (const float *)(rec + REC_TARGET);
    if (have) {
      log_printf("hs storage: %s on, aimed at %.2f, %.2f, %.2f", name, (double)t[0], (double)t[1], (double)t[2]);
      gfx_notice("hookshot storage");
    } else
      log_printf("hs storage: %s off (%s)", name, active ? "a shot used it" : "cleared");
  }
  w->have = have;
  w->last_active = active;
}

void hsstore_frame(void)
{
  for (unsigned i = 0; i < N_WEAPONS; i++)
    weapon_frame(i);
}

/* --- the visualiser -------------------------------------------------------------------------------------------- */
#define REC_START 0xA0   /* vec3: where the shot started (Link's hand) */
#define HOOK_NAME "Shootable_Hook"
#define HOOK_STATE 0x2C  /* 4, 5 out; 6 put away */
#define HOOK_OUT_FIRST 4
#define HOOK_OUT_LAST 5
#define MAX_HOOKS 8
#define HOOK_BOX 0.12f   /* metres, half the box's side */
#define HOOK_CROSS 0.10f
#define AIM_CROSS 0.06f
#define NEAR_LINK 40.0f  /* metres: hooks further off (or at the origin, never placed) are left out */

bool hsstore_shown;

static struct {
  float (*ends)[2][3];
  uint8_t *colours;
  int n, max;
} out;

static void line(const float a[3], const float b[3], uint8_t colour)
{
  if (out.n >= out.max)
    return;
  for (int k = 0; k < 3; k++)
    out.ends[out.n][0][k] = a[k], out.ends[out.n][1][k] = b[k];
  out.colours[out.n++] = colour;
}

static void cross(const float p[3], float r, uint8_t colour)
{
  for (int axis = 0; axis < 3; axis++) {
    float a[3] = { p[0], p[1], p[2] }, b[3] = { p[0], p[1], p[2] };
    a[axis] -= r, b[axis] += r;
    line(a, b, colour);
  }
}

static void box(const float p[3], float r, uint8_t colour)
{
  for (int axis = 0; axis < 3; axis++) /* the 4 edges along each axis */
    for (int s = 0; s < 4; s++) {
      int u = (axis + 1) % 3, v = (axis + 2) % 3;
      float a[3], b[3];
      a[axis] = p[axis] - r, b[axis] = p[axis] + r;
      a[u] = b[u] = p[u] + (s & 1 ? r : -r);
      a[v] = b[v] = p[v] + (s & 2 ? r : -r);
      line(a, b, colour);
    }
}

static bool finite3(const float v[3])
{
  for (int k = 0; k < 3; k++)
    if (!(v[k] == v[k]) || v[k] > 1e6f || v[k] < -1e6f)
      return false;
  return true;
}

static bool near_link(const float p[3], const vec3f_t *link)
{
  float dx = p[0] - link->x, dy = p[1] - link->y, dz = p[2] - link->z;
  return finite3(p) && !(p[0] == 0 && p[1] == 0 && p[2] == 0) && dx * dx + dy * dy + dz * dz < NEAR_LINK * NEAR_LINK;
}

static void copy3(uintptr_t at, float v[3])
{
  for (int k = 0; k < 3; k++)
    v[k] = ((const volatile float *)at)[k];
}

int hsstore_lines(float ends[][2][3], uint8_t colours[], int max)
{
  vec3f_t link;
  if (!hsstore_shown || !link_drawn_pos(&link))
    return 0;
  out.ends = ends, out.colours = colours, out.n = 0, out.max = max;

  /* each weapon's start and aimed spot, while a shot runs or storage holds it */
  float start[3];
  bool have_start = false;
  for (unsigned i = 0; i < N_WEAPONS; i++) {
    uintptr_t rec = weapon_record(weapons[i].key);
    if (!rec || !(u8_at(rec + REC_ACTIVE) || track[i].have > 0))
      continue;
    float a[3], b[3];
    copy3(rec + REC_START, a);
    copy3(rec + REC_TARGET, b);
    if (!near_link(a, &link) || !near_link(b, &link))
      continue;
    line(a, b, AW_HOOK_AIM);
    cross(b, AIM_CROSS, AW_HOOK_AIM);
    if (u8_at(rec + REC_ACTIVE))
      copy3(rec + REC_START, start), have_start = true;
  }

  /* the hooks: out first, so the budget keeps them */
  uintptr_t hooks[MAX_HOOKS];
  int n = actors_named(HOOK_NAME, hooks, MAX_HOOKS);
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < n; i++) {
      uint32_t state = *(const volatile uint32_t *)(hooks[i] + HOOK_STATE);
      bool hook_out = state >= HOOK_OUT_FIRST && state <= HOOK_OUT_LAST;
      if (hook_out != (pass == 0))
        continue;
      float p[3];
      copy3(hooks[i] + ACTOR_POSITION, p);
      if (!near_link(p, &link))
        continue;
      if (hook_out) {
        box(p, HOOK_BOX, AW_HOOK);
        if (have_start) /* the chain */
          line(start, p, AW_HOOK);
      } else {
        cross(p, HOOK_CROSS, AW_HOOK_IDLE);
      }
    }
  return out.n;
}
