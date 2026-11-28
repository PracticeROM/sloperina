/* Memfiles (memfiles_v2): snapshots backed by GameDataMgr's in-memory serializer/deserializer.
 *
 * Save captures:
 *  - WorldMgr::onSave flush + full file_idx 0 binary snapshot (all 33 GameData types, change queues, dirty/reset bits);
 *  - Hash-keyed snapshot of unsaved session fields (memfilefields.h);
 *  - Map, start point, Link's position/facing/HP/MP/age, WorldMgr clock, and player camera + AI action nodes.
 *
 * Load picks:
 *  - Fast in-place reload (ResetGimmick via DebugParam.IsRequestedResetGimmick) when on the same map & age;
 *  - Silent full reload (Dm_SY_BancChangeGo with cleared BootEvent/Starter + Sequence_IsAlreadyResetForLoadSave +
 *    DebugParam.IsSetChildhoodManually + DebugParam.IsOverrideTraversePos) otherwise, touching live GameData only
 *    after the engine accepts/starts the reload. */
#include "memfile.h"

#include "age.h"
#include "binds.h"
#include "cct.h"
#include "enemyhp.h"
#include "eventskip.h"
#include "flagnames.h"
#include "gamedata.h"
#include "gfx.h"
#include "got.h"
#include "lang.h"
#include "link.h"
#include "quests.h"
#include "memfilefields.h"
#include "pouch.h"
#include "sdfs.h"
#include "settings.h"
#include "sys.h"
#include "warp.h"
#include "z64hd.h"

#if __has_include("build_info.h")
#include "build_info.h" /* made by the Makefile */
#endif
#ifndef BUILD_VERSION
#define BUILD_VERSION "unknown" /* not menu text */
#endif

void *aligned_alloc(size_t align, size_t size); /* from main (fallback when BSS pool is full) */
void  free(void *p);                             /* from main */
float sqrtf(float x);                            /* from sdk */
float atan2f(float y, float x);                  /* from sdk */

/* --- GameData field hashes --------------------------------------------------------------------- */

#define F_IS_LOADING_BANC       0xEF2B60C2u /* Sequence_IsLoadingBanc */
#define F_IS_CHILDHOOD          0xCEA220D3u /* Sequence_IsChildhood */
#define F_MAGIC                 0x76B18200u /* PlayerStatus.MP */
#define F_REQ_RESET_GIMMICK     0x3FC35576u /* DebugParam.IsRequestedResetGimmick */
#define F_RESET_GIMMICK_OPT     0x37B1DAC9u /* Sequence_ResetGimmickOption */
#define F_ALREADY_RESET_LOAD    0x8CD71F3Au /* Sequence_IsAlreadyResetForLoadSave */
#define F_SET_CHILD_MANUALLY    0x86724737u /* DebugParam.IsSetChildhoodManually */
#define F_OVERRIDE_TRAVERSE     0x7325CE62u /* DebugParam.IsOverrideTraversePos */
#define F_OVERRIDE_TRAVERSE_POS 0x527FA715u /* DebugParam.OverrideTraversePos */
#define F_IS_GAME_OVER          0x2FD0BC6Cu /* IsGameOverTrig */

/* --- GameDataMgr save descriptor layout (1.0.0 & 1.1.1) ---------------------------------------- */

#define GD_MGR_FILE_COUNT   0xCC0u
#define GD_MGR_FILES_PTR    0xCC8u
#define GD_MGR_ACTIVE_SAVES 0xD0Cu
#define GD_MGR_SYNC_BUSY    0xD10u
#define GD_DESC_STRIDE      0x80u
#define GD_DESC_BUF_SIZE    0x50u
#define GD_DESC_WRITTEN     0x58u
#define GD_DESC_SAVE_BUF    0x60u
#define GD_DESC_LOAD_BUF    0x68u
#define GD_DESC_FLAGS       0x78u
#define GD_DESC_LOAD_OK     0x7Au
#define GD_FLAG_SAVE_REQ    0x01u
#define GD_FLAG_LOAD_REQ    0x02u
#define GD_FLAG_SAVE_WORK   0x04u
#define GD_SAVE_MAGIC        0x01020304u
#define GD_MAX_SAVE_BUF      (4u << 20)
#define MEMFILE_POOL_SIZE    (4u << 20) /* 4 MiB BSS pool (small enough not to crowd nnMain's startup heap) */
#define SESSION_SCALARS_MAX  1024u      /* > memfilefields_count */
#define SESSION_STRS_MAX     128u       /* > total String64 fields */

/* --- timings & limits -------------------------------------------------------------------------- */

#define SAVE_REQ_TIMEOUT   60
#define SAVE_WAIT_TIMEOUT  180
#define GD_LOAD_TIMEOUT    120
#define FAST_MOVE_TIMEOUT  15
#define FAST_MOVE_RETRY    3
#define RESET_TIMEOUT      180
#define EVENT_CANCEL_TIMEOUT (5 * GAME_FPS)
#define LOAD_START_TIMEOUT (10 * GAME_FPS)
#define ARRIVAL_TIMEOUT    (60 * GAME_FPS)
#define SETTLE_TIMEOUT     (2 * GAME_FPS)
#define QUIET_FRAMES       3
#define MOVE_TIMEOUT       GAME_FPS
#define MOVE_RETRY_EVERY   4
#define CAMERA_WAIT        (5 * GAME_FPS)
#define CAMERA_WRITE_FRAMES 10
#define CAMERA_SETTLE_MAX  GAME_FPS /* writes go on past CAMERA_WRITE_FRAMES until the look-at settles, up to this */
#define CAMERA_NEAR        0.1f     /* the drawn look-at this near the saved one (largest axis): settled */
#define CAMERA_STILL       0.01f    /* or moving less than this a frame: settled where it'll stay */
#define CAMERA_GUARD       (4 * GAME_FPS) /* after a full reload, the angle is watched this long ... */
#define CAMERA_REAPPLIES   3              /* ... and put back again this many times if the map turns it */
#define CAMERA_TURNED      5.0f           /* degrees: turned further than this from the saved angle */
#define DAY_SECONDS        86400
#define NAMES_LOGGED       24
#define MAIN_CODE_SIZE     0x3C80000u
#define MAIN_VTABLE_MAX    0x6000000u

/* --- camera constants -------------------------------------------------------------------------- */

#define CAMERA_BEGIN      0x450u
#define CAMERA_END        0x914u
#define CAMERA_BYTES      (CAMERA_END - CAMERA_BEGIN)
#define CAMERA_VIEW_BYTES 0x28u
#define CAMERA_COMP_HEAD  0x2F0u
#define CAMERA_TURN_OFF   0x7F0u
#define CAMERA_TURN_BYTES 0x60u
#define CAMERA_ACTIONS    16
#define ACTION_BYTES      0x200u
#define ACTION_WINDOW     0x800000u

static const uint16_t camera_views[] = { 0x450, 0x478, 0x4A0, 0x4C8, 0x4F0 };
static const struct { uint16_t offset, size; } camera_orbit[] = {
  { 0x868, 0x30 }, { 0x898, 8 }, { 0x8A0, 4 }, { 0x8A4, 8 }, { 0x8AC, 4 }, { 0x8BC, 8 }, { 0x8C4, 4 },
  { 0x90C, 4 }, { 0x910, 4 },
};
static const struct { uint16_t begin, end; } comp_head_ranges[] = {
  { 0x130, 0x1B4 }, { 0x208, 0x2F0 },
};
static const struct { uint16_t begin, end; } action_ranges[] = {
  { 0x14C, 0x170 }, { 0x188, 0x1A8 }, { 0x1C4, 0x1C8 },
};

/* --- session-field snapshot table -------------------------------------------------------------- */

enum { K_INT, K_BOOL, K_FLOAT, K_ENUM, K_STR, K_UINT, K_VEC3, K_COUNT };

static const char *const kind_names[K_COUNT] = {
  "int", "bool", "float", "enum", "string", "uint", "vector3", /* not menu text */
};
static const uint8_t kind_width[K_COUNT] = { 4, 1, 4, 4, GD_STR_LEN, 4, 12 };

#define GD_UINT_TYPE   0x788u
#define GD_UINT_STRIDE 0x28u
#define GD_UINT_VALUE  0x1Cu
#define GD_UINT_KNOWN  0x3F8D9D86u
#define GD_VEC3_TYPE   0x3C8u
#define GD_VEC3_STRIDE 0x40u
#define GD_VEC3_VALUE  0x24u
#define GD_VEC3_KNOWN  0x8773F02Du
#define MAX_OBJECTS    0x10000u

typedef struct {
  uint32_t hash;
  uint8_t  kind;
  uint8_t  width;
  uint8_t  val[12];
} session_scalar_t;

typedef struct {
  uint32_t hash;
  char     val[GD_STR_LEN];
} session_str_t;

#define MEMFILE_ACTORS_MAX   256u
#define MEMFILE_CUCCOS_MAX   32u
#define MEMFILE_BB_POOL_MAX  (128u << 10) /* 128 KiB cap for per-actor Blackboard primitive pool per slot */
#define HELD_NAME_LEN        64u
#define HELD_BB_MAX          4096u
#define HELD_FIND_FRAMES     6
#define HELD_SETTLE_FRAMES   2
#define HELD_SPAWN_TIMEOUT   120
#define HELD_REAPPLY_BB      4
#define HELD_WATCH_FRAMES    120
#define CUCCO_WATCH_MAX      4
#define CUCCO_WATCH_FRAMES   240
#define DROWN_RESTORE_TIMEOUT 40
#define ACTOR_RESTORE_FRAMES 4
#define ACTOR_DEAD_RADIUS    30.0f
#define SAVEPOS_SLOT_BYTES   0x14u /* AutoSaveMgr slot A (+0x198) and slot B (+0x178) */
#define PLAYER_RESTART_BYTES 0x2Cu /* Player component +0x18A4..+0x18CF restart point & cooldown */

enum { ACTOR_F_LIFE = 1, ACTOR_F_DEAD = 2, ACTOR_F_ENEMY = 4, ACTOR_F_BB = 8 };
enum { CUCCO_F_HOME = 1, CUCCO_F_DROWN = 2, CUCCO_F_FADE = 4 };
enum { HELD_F_LIFE = 1, HELD_F_CUCCO = 2 };

typedef struct {
  uint64_t instance_id; /* [[actor + 0x3D8] + 0x88]: Banc PreActor InstanceId */
  uint32_t name_hash;   /* FNV-1a of actor name: guards against colliding/reused IDs */
  float    pos[3];      /* actor + 0x31C */
  float    rot[9];      /* actor + 0x328 (row-major 3x3) */
  int32_t  life, max_life;
  uint8_t  state, flags;
  uint8_t  pad[2];
  uint32_t bb_off, bb_len; /* slice inside memfile_t.actor_bb */
} memfile_actor_t;

typedef struct {
  uint64_t instance_id;
  uint32_t name_hash;
  uint8_t  flags;              /* CUCCO_F_* */
  uint8_t  model_fade_active;  /* ModelComponent +0x134 */
  uint8_t  model_vis_flags;    /* [[comps+0x10]+0xB0] */
  uint8_t  pad0;
  uint16_t active_node;        /* Kokko.action.interruptdrown counter node index, or 0xFFFF */
  uint16_t pad1;
  float    home_pos[3];        /* QueryActorGetHomePosition fallback when WarpPos is unset */
  float    counter_val;        /* SelectorGenericFrameCounter +0x20 */
  float    air_val;            /* CUCCO_JUMP_AIR (119) +0x20 if active, else -1 */
  int32_t  landing_water_count;/* BSA ctrl blackboard "LandingWaterCount" */
  float    model_alpha;        /* ModelComponent +0x80 */
  float    model_fade_step;    /* ModelComponent +0x11C */
  uint32_t model_fade_flags;   /* ModelComponent +0x128 */
} memfile_cucco_t;

typedef struct {
  bool            valid;
  uint64_t        instance_id; /* 0 if non-Banc / dynamically spawned */
  uint32_t        name_hash;
  char            pack_name[HELD_NAME_LEN];
  float           pos[3];
  float           rot[9];
  int32_t         life, max_life;
  uint8_t         flags;       /* HELD_F_* */
  uint8_t        *actor_bb;    /* Actor Blackboard [[comps+0]+0x10] primitive snapshot */
  uint32_t        actor_bb_size;
  uint8_t        *ai_bb;       /* AIComponent Blackboard primitive snapshot (if present) */
  uint32_t        ai_bb_size;
  memfile_cucco_t cucco;       /* valid when flags & HELD_F_CUCCO */
} memfile_held_t;

typedef struct {
  bool              used;
  uint8_t          *save_buf;
  uint32_t          save_cap, save_size;
  session_scalar_t *s_scalars;
  uint32_t          s_scalar_count;
  session_str_t    *s_strs;
  uint32_t          s_str_count;
  memfile_actor_t  *actors;
  uint32_t          actor_count;
  uint8_t          *actor_bb;
  uint32_t          actor_bb_size;
  memfile_cucco_t  *cuccos;
  uint32_t          cucco_count;
  memfile_held_t    held;
  bool              have_actors, actors_truncated;
  char              map[GD_STR_LEN], start[GD_STR_LEN];
  double            x, y, z, fx, fz;
  bool              have_pos, have_life, have_magic, have_time;
  int32_t           life, magic;
  bool              have_nayru;
  float             nayru_left, nayru_total; /* Nayru's Love: frames left and its length; 0 when off */
  bool              have_vel, have_fall, have_savepos, have_restart, have_rng, have_void_ring;
  vec3f_t           vel;                     /* Link's CCT linear velocity (body +0xE0) */
  float             fall[6];                 /* player +0x18D4/+0x18E0/+0x18E8/+0x190C, base +0x1898/+0x189C */
  uint8_t           savepos_a[SAVEPOS_SLOT_BYTES];
  uint8_t           savepos_b[SAVEPOS_SLOT_BYTES];
  uint8_t           player_restart[PLAYER_RESTART_BYTES];
  uint32_t          rng[4];                  /* sead::GlobalRandom xorshift128 state */
  uint8_t           void_ring_hit;           /* Probe C +0xBBC bit 0 (has_point) */
  float             void_ring_point[3];      /* Probe C +0xBA0 (8-unit ring anchor) */
  float             void_ring_origin[3];     /* Probe C +0xB94 (chunk origin) */
  int32_t           void_ring_cell[3];       /* Probe C +0xBAC (chunk cell index) */
  int32_t           void_ring_face;          /* Probe C +0xBB8 (hit face 0..5) */
  uint32_t          void_ring_phase;         /* Probe C +0x1160 (Fibonacci ray phase 0..9) */
  uint32_t          time;
  int               child;
  bool              have_camera, have_component;
  uint8_t           camera[CAMERA_BYTES];
  uint8_t           comp_head[CAMERA_COMP_HEAD];
  uint8_t           comp_turn[CAMERA_TURN_BYTES];
  int               camera_actions;
  uint32_t          action_vtable[CAMERA_ACTIONS];
  uint16_t          action_node_idx[CAMERA_ACTIONS];
  uint8_t           action[CAMERA_ACTIONS][ACTION_BYTES];
  float             view[6];
} memfile_t;

static memfile_t memfiles[MEMFILE_SLOTS];
static memfile_t staging;
static uint8_t  *inflight_save_buf; /* never freed while GameDataMgr save worker holds it */
static int       slot;
bool             memfile_session = true;
bool             memfile_fast_reload = true;
bool             memfile_actors = true;

static enum {
  L_IDLE,
  L_SAVING_REQ,
  L_SAVING_WAIT,
  L_CANCEL_EVENT,
  L_FAST_WAIT_GD,
  L_FAST_MOVE_PRE,
  L_FAST_WAIT_RESET,
  L_WAIT_LOADING,
  L_FULL_WAIT_GD,
  L_WAIT_ARRIVAL,
  L_WAIT_SETTLE,
  L_MOVING,
  L_ACTORS,
  L_HELD,
  L_CAMERA,
} phase;

static int       active_slot;
static uintptr_t old_actor;
static bool      saw_unload, overrides_armed, saw_reset_req;
static bool      fast_load; /* the in-place reload: no arrival camera to wait for */
static int       phase_frames, quiet_frames, camera_frames;
static float     camera_last_at[3]; /* the drawn look-at last frame, to see it settle */
static int       camera_guard;      /* frames watched since the writes ended (full reload); 0: not watching */
static int       camera_reapplies;  /* times the angle was put back again while watching */
static char      status[96];
static bool      log_why;

/* --- BSS storage pool (outside main's small CRT heap, FINDINGS §9.6) --------------------------- */

/* Keep memfiles in a compact 4 MiB .bss pool (small enough to stay well inside nnMain's startup
 * memory budget) with immediate boundary-tag coalescing and in-place shrinking (finish_save trims
 * save_buf from GD_DESC_BUF_SIZE down to the actual written bytes with zero copy). If a transient
 * buffer or full pool exceeds the 4 MiB pool, alloc16 falls back to main's aligned_alloc. */
static _Alignas(16) uint8_t memfile_pool[MEMFILE_POOL_SIZE];
static bool memfile_pool_ready;

typedef struct {
  uint32_t size;      /* total block bytes including this 16-byte header (multiple of 16) */
  uint32_t prev_size; /* total bytes of immediately preceding block, or 0 for first */
  uint32_t used;      /* 1 = allocated, 0 = free */
  uint32_t pad;
} pool_hdr_t;
_Static_assert(sizeof(pool_hdr_t) == 16, "16-byte pool block header");

static void pool_init(void)
{
  if (memfile_pool_ready)
    return;
  *(pool_hdr_t *)memfile_pool = (pool_hdr_t){ MEMFILE_POOL_SIZE, 0, 0, 0 };
  memfile_pool_ready = true;
}

static bool in_pool(const void *p)
{
  uintptr_t base = (uintptr_t)memfile_pool;
  uintptr_t addr = (uintptr_t)p;
  return memfile_pool_ready && addr >= base + sizeof(pool_hdr_t) && addr < base + MEMFILE_POOL_SIZE;
}

static void *pool_alloc(size_t n)
{
  pool_init();
  if (n > MEMFILE_POOL_SIZE - sizeof(pool_hdr_t))
    return 0;
  size_t payload = n ? ((n + 15u) & ~(size_t)15u) : 16u;
  if (payload > MEMFILE_POOL_SIZE - sizeof(pool_hdr_t))
    return 0;
  uint32_t need = (uint32_t)(sizeof(pool_hdr_t) + payload);
  uint32_t best_off = MEMFILE_POOL_SIZE, best_size = MEMFILE_POOL_SIZE + 1u;
  for (uint32_t off = 0; off < MEMFILE_POOL_SIZE;) {
    pool_hdr_t *h = (pool_hdr_t *)(memfile_pool + off);
    if (!h->size || h->size > MEMFILE_POOL_SIZE - off)
      return 0;
    if (!h->used && h->size >= need && h->size < best_size) {
      best_off = off;
      best_size = h->size;
      if (best_size == need)
        break;
    }
    off += h->size;
  }
  if (best_off >= MEMFILE_POOL_SIZE)
    return 0;
  pool_hdr_t *best = (pool_hdr_t *)(memfile_pool + best_off);
  if (best->size - need >= sizeof(pool_hdr_t) + 16u) {
    uint32_t rem = best->size - need;
    uint32_t next_off = best_off + need;
    *(pool_hdr_t *)(memfile_pool + next_off) = (pool_hdr_t){ rem, need, 0, 0 };
    best->size = need;
    if (next_off + rem < MEMFILE_POOL_SIZE)
      ((pool_hdr_t *)(memfile_pool + next_off + rem))->prev_size = rem;
  }
  best->used = 1;
  return memfile_pool + best_off + sizeof(pool_hdr_t);
}

static void *alloc16(size_t n)
{
  void *p = pool_alloc(n);
  if (p)
    return p;
  size_t sz = n ? ((n + 15u) & ~(size_t)15u) : 16u;
  return aligned_alloc(16, sz);
}

static void free16(void *p)
{
  if (!p)
    return;
  if (!in_pool(p)) {
    free(p);
    return;
  }
  uint32_t off = (uint32_t)((uintptr_t)p - (uintptr_t)memfile_pool - sizeof(pool_hdr_t));
  if (off & 15u)
    return;
  pool_hdr_t *h = (pool_hdr_t *)(memfile_pool + off);
  if (!h->used || !h->size || h->size > MEMFILE_POOL_SIZE - off)
    return;
  h->used = 0;
  uint32_t next_off = off + h->size;
  if (next_off < MEMFILE_POOL_SIZE) {
    pool_hdr_t *next = (pool_hdr_t *)(memfile_pool + next_off);
    if (!next->used) {
      h->size += next->size;
      uint32_t after = off + h->size;
      if (after < MEMFILE_POOL_SIZE)
        ((pool_hdr_t *)(memfile_pool + after))->prev_size = h->size;
    }
  }
  if (h->prev_size && h->prev_size <= off) {
    uint32_t prev_off = off - h->prev_size;
    pool_hdr_t *prev = (pool_hdr_t *)(memfile_pool + prev_off);
    if (!prev->used) {
      prev->size += h->size;
      uint32_t after = prev_off + prev->size;
      if (after < MEMFILE_POOL_SIZE)
        ((pool_hdr_t *)(memfile_pool + after))->prev_size = prev->size;
    }
  }
}

/* Shrink an in-pool block in place, returning the unused tail to the pool and coalescing with next. */
static void pool_shrink(void *p, size_t new_n)
{
  if (!in_pool(p))
    return;
  uint32_t off = (uint32_t)((uintptr_t)p - (uintptr_t)memfile_pool - sizeof(pool_hdr_t));
  if (off & 15u)
    return;
  pool_hdr_t *h = (pool_hdr_t *)(memfile_pool + off);
  if (!h->used || !h->size || h->size > MEMFILE_POOL_SIZE - off)
    return;
  size_t payload = new_n ? ((new_n + 15u) & ~(size_t)15u) : 16u;
  if (payload > h->size - sizeof(pool_hdr_t))
    return;
  uint32_t need = (uint32_t)(sizeof(pool_hdr_t) + payload);
  if (h->size - need < sizeof(pool_hdr_t) + 16u)
    return;
  uint32_t rem = h->size - need;
  uint32_t tail_off = off + need;
  pool_hdr_t *tail = (pool_hdr_t *)(memfile_pool + tail_off);
  *tail = (pool_hdr_t){ rem, need, 0, 0 };
  h->size = need;
  uint32_t after = tail_off + rem;
  if (after < MEMFILE_POOL_SIZE) {
    pool_hdr_t *next = (pool_hdr_t *)(memfile_pool + after);
    if (!next->used) {
      tail->size += next->size;
      after = tail_off + tail->size;
      if (after < MEMFILE_POOL_SIZE)
        ((pool_hdr_t *)(memfile_pool + after))->prev_size = tail->size;
    } else {
      next->prev_size = tail->size;
    }
  }
}

static size_t pool_free_bytes(void)
{
  pool_init();
  size_t total = 0;
  for (uint32_t off = 0; off < MEMFILE_POOL_SIZE;) {
    const pool_hdr_t *h = (const pool_hdr_t *)(memfile_pool + off);
    if (!h->size || h->size > MEMFILE_POOL_SIZE - off)
      break;
    if (!h->used && h->size > sizeof(pool_hdr_t))
      total += h->size - sizeof(pool_hdr_t);
    off += h->size;
  }
  return total;
}

static bool pool_owns_in_memfile(const memfile_t *m, uint32_t off)
{
  const void *p = memfile_pool + off + sizeof(pool_hdr_t);
  return p == m->save_buf || p == m->s_scalars || p == m->s_strs || p == m->actors || p == m->actor_bb ||
         p == m->cuccos || p == m->held.actor_bb || p == m->held.ai_bb;
}

static bool pool_would_fit_reclaiming(size_t n, const memfile_t *reclaim)
{
  pool_init();
  if (n > MEMFILE_POOL_SIZE - sizeof(pool_hdr_t))
    return false;
  size_t payload = n ? ((n + 15u) & ~(size_t)15u) : 16u;
  if (payload > MEMFILE_POOL_SIZE - sizeof(pool_hdr_t))
    return false;
  uint32_t need = (uint32_t)(sizeof(pool_hdr_t) + payload);
  uint32_t run = 0;
  for (uint32_t off = 0; off < MEMFILE_POOL_SIZE;) {
    const pool_hdr_t *h = (const pool_hdr_t *)(memfile_pool + off);
    if (!h->size || h->size > MEMFILE_POOL_SIZE - off)
      return false;
    if (!h->used || (reclaim && pool_owns_in_memfile(reclaim, off))) {
      run += h->size;
      if (run >= need)
        return true;
    } else {
      run = 0;
    }
    off += h->size;
  }
  return false;
}

static bool str_equal(const char *a, const char *b)
{
  while (*a && *a == *b)
    a++, b++;
  return *a == *b;
}

static bool bytes_equal(const void *a, const void *b, size_t n)
{
  const uint8_t *pa = (const uint8_t *)a, *pb = (const uint8_t *)b;
  for (size_t i = 0; i < n; i++)
    if (pa[i] != pb[i])
      return false;
  return true;
}

static void free_memfile(memfile_t *st)
{
  free16(st->save_buf);
  free16(st->s_scalars);
  free16(st->s_strs);
  free16(st->actors);
  free16(st->actor_bb);
  free16(st->cuccos);
  free16(st->held.actor_bb);
  free16(st->held.ai_bb);
  *st = (memfile_t){ 0 };
}

static void set_phase(int p)
{
  phase = p;
  phase_frames = 0;
  quiet_frames = 0;
}

/* --- GameDataMgr save/load descriptors --------------------------------------------------------- */

typedef bool (*gd_req_save_t)(void *mgr, void *buf, int file_idx);
typedef bool (*gd_req_load_t)(void *mgr, const void *buf, int file_idx);

/* Return writable descriptor 0 inside GameDataMgr and validate its buffer size, or 0. */
static uint8_t *gd_file0_desc(uint8_t **mgr_out, uint32_t *buf_size_out)
{
  uint8_t *mgr = gd_manager();
  if (!mgr || !sys_mapped((uintptr_t)mgr + GD_MGR_FILE_COUNT, 0x58, SYS_PERM_R))
    return 0;
  uint32_t count = *(const uint32_t *)(mgr + GD_MGR_FILE_COUNT);
  uintptr_t files = sys_ptr((uintptr_t)mgr + GD_MGR_FILES_PTR);
  if (!count || !files || (files & 7u) || !sys_mapped(files, GD_DESC_STRIDE, SYS_PERM_R | SYS_PERM_W))
    return 0;
  uint32_t buf_size = *(const uint32_t *)(files + GD_DESC_BUF_SIZE);
  if (buf_size < 64u || buf_size > GD_MAX_SAVE_BUF)
    return 0;
  if (mgr_out)
    *mgr_out = mgr;
  if (buf_size_out)
    *buf_size_out = buf_size;
  return (uint8_t *)files;
}

static void retire_inflight_buf(void)
{
  if (!inflight_save_buf)
    return;
  uint8_t *mgr = 0;
  uint8_t *desc0 = gd_file0_desc(&mgr, 0);
  if (!desc0 || !(desc0[GD_DESC_FLAGS] & GD_FLAG_SAVE_WORK) ||
      sys_ptr((uintptr_t)(desc0 + GD_DESC_SAVE_BUF)) != (uintptr_t)inflight_save_buf) {
    free16(inflight_save_buf);
    inflight_save_buf = 0;
  }
}

/* --- WorldMgr & NaviMgr hooks ------------------------------------------------------------------ */

typedef uint32_t (*clock_get_t)(uintptr_t clock);
typedef void     (*wm_set_time_t)(uintptr_t wm, uint32_t seconds);
typedef void     (*mgr_void_t)(uintptr_t mgr);
typedef uintptr_t (*navi_get_t)(uintptr_t scene);

#define WM_CLOCK       0x578u
#define CLOCK_GET_TIME 0x30u

static uintptr_t world_manager(void)
{
  if (!z64hd_game || !z64hd_game->wm_ptr)
    return 0;
  uintptr_t holder = sys_ptr(MAIN(z64hd_game->wm_ptr));
  uintptr_t wm = holder ? sys_ptr(holder) : 0;
  return wm && sys_mapped(wm + WM_CLOCK, 8, SYS_PERM_R) ? wm : 0;
}

static bool time_of_day(uint32_t *out)
{
  uintptr_t wm = world_manager();
  uintptr_t clock = wm ? sys_ptr(wm + WM_CLOCK) : 0;
  uintptr_t vtable = clock && sys_mapped(clock, 8, SYS_PERM_R) ? sys_ptr(clock) : 0;
  uintptr_t fn = vtable && sys_mapped(vtable + CLOCK_GET_TIME, 8, SYS_PERM_R) ? sys_ptr(vtable + CLOCK_GET_TIME) : 0;
  if (!fn || fn < z64hd_main || fn - z64hd_main >= MAIN_CODE_SIZE)
    return false;
  int32_t t = (int32_t)((clock_get_t)fn)(clock) % DAY_SECONDS;
  *out = (uint32_t)(t < 0 ? t + DAY_SECONDS : t);
  return true;
}

static bool set_time_of_day(uint32_t seconds)
{
  uintptr_t wm = world_manager();
  if (!wm || !z64hd_game->wm_set_time)
    return false;
  MAIN_FN(wm_set_time_t, z64hd_game->wm_set_time)(wm, seconds % DAY_SECONDS);
  return true;
}

static void world_on_save(void)
{
  uintptr_t wm = world_manager();
  if (wm && z64hd_game->wm_on_save)
    MAIN_FN(mgr_void_t, z64hd_game->wm_on_save)(wm);
}

static void world_on_load(void)
{
  uintptr_t wm = world_manager();
  if (wm && z64hd_game->wm_on_load)
    MAIN_FN(mgr_void_t, z64hd_game->wm_on_load)(wm);
}

static void navi_on_load(void)
{
  if (!z64hd_game || !z64hd_game->navi_scene_ptr || !z64hd_game->navi_get || !z64hd_game->navi_on_load)
    return;
  uintptr_t holder = sys_ptr(MAIN(z64hd_game->navi_scene_ptr));
  uintptr_t scene = holder ? sys_ptr(holder) : 0;
  if (!scene || !sys_mapped(scene, 0x4A0, SYS_PERM_R))
    return;
  uintptr_t navi = MAIN_FN(navi_get_t, z64hd_game->navi_get)(scene);
  if (navi && sys_mapped(navi, 8, SYS_PERM_R))
    MAIN_FN(mgr_void_t, z64hd_game->navi_on_load)(navi);
}

/* the quests' stages: the ChallengeMgr keeps its own copy and re-reads GameData only on a save load */
static void quests_resync(void)
{
  if (!quests_on_load())
    log_printf("memfiles: quest manager not resynced (not known or not ready)"); /* not menu text */
}

/* --- session-field helpers (hash-keyed, save_file == -1) --------------------------------------- */

static gd_type_t *kind_type(int k, uint32_t *stride, uint32_t *value_off)
{
  gd_type_t *t;
  uint32_t index;
  if (k == K_STR) {
    t = gd_string_type();
    *stride = Z64HD_STR_STRIDE;
    *value_off = 0;
  } else if (k == K_UINT || k == K_VEC3) {
    t = gd_type_at(k == K_UINT ? GD_UINT_TYPE : GD_VEC3_TYPE);
    *stride = k == K_UINT ? GD_UINT_STRIDE : GD_VEC3_STRIDE;
    *value_off = k == K_UINT ? GD_UINT_VALUE : GD_VEC3_VALUE;
    if (t && !gd_type_index(t, k == K_UINT ? GD_UINT_KNOWN : GD_VEC3_KNOWN, &index))
      t = 0;
  } else {
    gd_kind_t gk = k == K_INT ? GD_INT : k == K_BOOL ? GD_BOOL : k == K_FLOAT ? GD_FLOAT : GD_ENUM;
    t = gd_type(gk);
    gd_layout(gk, stride, value_off);
  }
  if (!t || !t->count || t->count > MAX_OBJECTS ||
      !sys_mapped((uintptr_t)t->objects, (size_t)t->count * *stride, SYS_PERM_R | SYS_PERM_W))
    return 0;
  if (t->count > 1 && sys_ptr((uintptr_t)t->objects) != sys_ptr((uintptr_t)t->objects + *stride))
    return 0;
  return t;
}

static uint8_t *value_at(int k, gd_type_t *t, uint32_t stride, uint32_t value_off, uint32_t i)
{
  if (k == K_STR)
    return (uint8_t *)gd_str_index(i);
  return t->objects + (size_t)i * stride + value_off;
}

static int32_t save_file_of(gd_type_t *t, uint32_t stride, uint32_t i)
{
  int32_t save = -1;
  memcpy(&save, t->objects + (size_t)i * stride + GD_FIELD_SAVE_FILE, sizeof(save));
  return save;
}

static bool is_session_field(uint32_t hash)
{
  int lo = 0, hi = memfilefields_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (memfilefields[mid].hash == hash)
      return true;
    if (memfilefields[mid].hash < hash)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return false;
}

static const char *field_name(uint32_t hash)
{
  const char *name = flagname(hash);
  if (name)
    return name;
  int lo = 0, hi = memfilefields_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (memfilefields[mid].hash == hash)
      return memfilefields[mid].name;
    if (memfilefields[mid].hash < hash)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return 0;
}

enum { CAP_OK = 0, CAP_OOM = 1, CAP_BAD_GD = 2 };

static session_scalar_t cap_scalars[SESSION_SCALARS_MAX];
static session_str_t    cap_strs[SESSION_STRS_MAX];

static int capture_session(memfile_t *st)
{
  uint32_t scalar_n = 0, str_n = 0;
  uint32_t scalar_cap = memfilefields_count > 0 ? (uint32_t)memfilefields_count : 0u;
  if (scalar_cap > SESSION_SCALARS_MAX)
    scalar_cap = SESSION_SCALARS_MAX;
  for (int k = 0; k < K_COUNT; k++) {
    uint32_t stride, value_off;
    gd_type_t *t = kind_type(k, &stride, &value_off);
    if (!t) {
      if (k == K_UINT || k == K_VEC3)
        continue;
      log_printf("memfile save: session %s table unreadable", kind_names[k]); /* not menu text */
      return CAP_BAD_GD;
    }
    for (uint32_t s_i = 0; s_i < t->table_size; s_i++) {
      uint32_t hash = t->table[2 * s_i], idx = t->table[2 * s_i + 1];
      if (!hash || idx >= t->count || save_file_of(t, stride, idx) != -1 || !is_session_field(hash))
        continue;
      const uint8_t *v = value_at(k, t, stride, value_off, idx);
      if (!v)
        continue;
      if (k == K_STR) {
        if (str_n < SESSION_STRS_MAX) {
          session_str_t *e = &cap_strs[str_n++];
          e->hash = hash;
          memcpy(e->val, v, GD_STR_LEN);
          e->val[GD_STR_LEN - 1] = 0;
        }
      } else if (scalar_n < scalar_cap) {
        session_scalar_t *e = &cap_scalars[scalar_n++];
        e->hash = hash;
        e->kind = (uint8_t)k;
        e->width = kind_width[k];
        memset(e->val, 0, sizeof(e->val));
        memcpy(e->val, v, e->width);
      }
    }
  }
  if (scalar_n) {
    size_t bytes = (size_t)scalar_n * sizeof(session_scalar_t);
    st->s_scalars = alloc16(bytes);
    if (!st->s_scalars) {
      log_printf("memfile save: out of pool memory for %u session scalars (%u bytes, %u KiB free)", /* not menu text */
                 (unsigned)scalar_n, (unsigned)bytes, (unsigned)(pool_free_bytes() >> 10));
      return CAP_OOM;
    }
    memcpy(st->s_scalars, cap_scalars, bytes);
    st->s_scalar_count = scalar_n;
  }
  if (str_n) {
    size_t bytes = (size_t)str_n * sizeof(session_str_t);
    st->s_strs = alloc16(bytes);
    if (!st->s_strs) {
      log_printf("memfile save: out of pool memory for %u session strings (%u bytes, %u KiB free)", /* not menu text */
                 (unsigned)str_n, (unsigned)bytes, (unsigned)(pool_free_bytes() >> 10));
      return CAP_OOM;
    }
    memcpy(st->s_strs, cap_strs, bytes);
    st->s_str_count = str_n;
  }
  return CAP_OK;
}

static void log_restored_field(int k, uint32_t hash, int *logged)
{
  if (*logged >= NAMES_LOGGED)
    return;
  const char *name = field_name(hash);
  if (name)
    log_printf("  %s %s", kind_names[k], name); /* not menu text */
  else
    log_printf("  %s %08x", kind_names[k], (unsigned)hash); /* not menu text */
  (*logged)++;
}

static uint32_t restore_session(const memfile_t *st)
{
  if (!memfile_session)
    return 0;
  uint32_t total = 0;
  int logged = 0;
  gd_type_t *types[K_COUNT];
  uint32_t strides[K_COUNT], voffs[K_COUNT];
  for (int k = 0; k < K_COUNT; k++)
    types[k] = kind_type(k, &strides[k], &voffs[k]);

  for (uint32_t i = 0; i < st->s_scalar_count; i++) {
    const session_scalar_t *e = &st->s_scalars[i];
    gd_type_t *t = types[e->kind];
    uint32_t idx;
    if (!t || !gd_type_index(t, e->hash, &idx))
      continue;
    uint8_t *v = value_at(e->kind, t, strides[e->kind], voffs[e->kind], idx);
    if (!v || bytes_equal(v, e->val, e->width))
      continue;
    memcpy(v, e->val, e->width);
    memcpy(v + e->width, e->val, e->width);
    log_restored_field(e->kind, e->hash, &logged);
    total++;
  }
  if (types[K_STR]) {
    for (uint32_t i = 0; i < st->s_str_count; i++) {
      const session_str_t *e = &st->s_strs[i];
      uint32_t idx;
      if (!gd_type_index(types[K_STR], e->hash, &idx))
        continue;
      char *v = (char *)value_at(K_STR, types[K_STR], strides[K_STR], voffs[K_STR], idx);
      if (!v || bytes_equal(v, e->val, GD_STR_LEN))
        continue;
      memcpy(v, e->val, GD_STR_LEN);
      v[GD_STR_LEN - 1] = 0;
      log_restored_field(K_STR, e->hash, &logged);
      total++;
    }
  }
  log_printf("memfile %d load: %u session fields restored", active_slot + 1, (unsigned)total); /* not menu text */
  if (total > (uint32_t)logged && logged)
    log_printf("  ... and %u more", (unsigned)(total - (uint32_t)logged)); /* not menu text */
  return total;
}

/* --- player camera & direct AI action node traversal ------------------------------------------- */

static bool plain_float(float f)
{
  float a = f < 0 ? -f : f;
  return f == f && (a == 0 || (a > 1e-4f && a < 1e6f));
}

static uintptr_t handle_actor(uintptr_t handle)
{
  if (!sys_mapped(handle, 0x14, SYS_PERM_R) || *(const int32_t *)(handle + 0x10) == -1)
    return 0;
  uintptr_t entry = sys_ptr(handle + 8);
  if (!entry || (entry & 7u) || !sys_mapped(entry, 0x4C, SYS_PERM_R) ||
      *(const uint32_t *)(entry + 0x48) != *(const uint32_t *)(handle + 0x10))
    return 0;
  return sys_ptr(entry + 0x40);
}

static uintptr_t camera_component(uintptr_t actor, const char **why)
{
  if (!actor || (actor & 7u) || !sys_mapped(actor + 0x270, 8, SYS_PERM_R)) {
    *why = "no actor"; /* not menu text */
    return 0;
  }
  uintptr_t comps = sys_ptr(actor + 0x270);
  if (!comps || (comps & 7u) || !sys_mapped(comps + z64hd_game->player_camera_component, 8, SYS_PERM_R)) {
    *why = "no components"; /* not menu text */
    return 0;
  }
  uintptr_t camera_c = sys_ptr(comps + z64hd_game->player_camera_component);
  size_t state_span = z64hd_game->player_camera_state + CAMERA_END;
  size_t need = state_span > CAMERA_COMP_HEAD ? state_span : CAMERA_COMP_HEAD;
  if (!camera_c || (camera_c & 7u) || !sys_mapped(camera_c, need, SYS_PERM_R | SYS_PERM_W)) {
    *why = "no camera component"; /* not menu text */
    return 0;
  }
  uintptr_t vtable = sys_ptr(camera_c);
  if (vtable < z64hd_main || vtable - z64hd_main >= MAIN_VTABLE_MAX) {
    *why = "the component's vtable isn't main's"; /* not menu text */
    return 0;
  }
  return camera_c;
}

static uintptr_t resolve_camera(uintptr_t *actor_out)
{
  if (!z64hd_game || !z64hd_game->player_camera_component || !z64hd_game->player_camera_state)
    return 0;
  const char *why = "not known for this build"; /* not menu text */
  uintptr_t camera_c = 0, holder = 0, holder_actor = 0, chosen_actor = 0;
  if (z64hd_game->player_camera_holder) {
    uintptr_t slot_p = sys_ptr(MAIN(z64hd_game->player_camera_holder));
    holder = slot_p && sys_mapped(slot_p, 8, SYS_PERM_R) ? sys_ptr(slot_p) : 0;
    holder_actor = holder ? handle_actor(holder + 0x38) : 0;
    camera_c = camera_component(holder_actor, &why);
    if (camera_c)
      chosen_actor = holder_actor;
  }
  uintptr_t link = 0;
  if (!camera_c) {
    link = link_actor();
    camera_c = camera_component(link, &why);
    if (camera_c)
      chosen_actor = link;
  }
  if (log_why && !camera_c)
    log_printf("memfiles: player camera not found: %s (holder %lx actor %lx, Link %lx)", why, /* not menu text */
               (unsigned long)holder, (unsigned long)holder_actor, (unsigned long)link);
  log_why = false;
  if (actor_out)
    *actor_out = chosen_actor;
  return camera_c;
}

static uintptr_t camera_c_now(void) { return resolve_camera(0); }

static uint8_t *camera_state(void)
{
  uintptr_t c = camera_c_now();
  return c ? (uint8_t *)(c + z64hd_game->player_camera_state) : 0;
}

static bool camera_take(uint8_t out[CAMERA_BYTES])
{
  const uint8_t *state = camera_state();
  if (!state)
    return false;
  memcpy(out, state + CAMERA_BEGIN, CAMERA_BYTES);
  return true;
}

typedef void (*camera_snap_t)(uintptr_t component);

static bool camera_put(const uint8_t in[CAMERA_BYTES])
{
  uintptr_t camera_c = camera_c_now();
  if (!camera_c)
    return false;
  uint8_t *state = (uint8_t *)(camera_c + z64hd_game->player_camera_state);
  for (size_t i = 0; i < sizeof(camera_orbit) / sizeof(*camera_orbit); i++)
    memcpy(state + camera_orbit[i].offset, in + camera_orbit[i].offset - CAMERA_BEGIN, camera_orbit[i].size);
  for (size_t i = 0; i < sizeof(camera_views) / sizeof(*camera_views); i++)
    memcpy(state + camera_views[i], in + (0x450 - CAMERA_BEGIN), CAMERA_VIEW_BYTES);
  if (z64hd_game->player_camera_snap)
    MAIN_FN(camera_snap_t, z64hd_game->player_camera_snap)(camera_c);
  return true;
}

static bool camera_take_component(memfile_t *st)
{
  uintptr_t camera_c = camera_c_now();
  if (!camera_c)
    return false;
  memcpy(st->comp_head, (const void *)camera_c, CAMERA_COMP_HEAD);
  memcpy(st->comp_turn, (const void *)(camera_c + z64hd_game->player_camera_state + CAMERA_TURN_OFF), CAMERA_TURN_BYTES);
  return true;
}

static void copy_plain_floats(uint8_t *dst, const uint8_t *src, uint32_t begin, uint32_t end)
{
  for (uint32_t o = begin; o < end; o += 4) {
    float fa, fb;
    memcpy(&fa, src + o, 4);
    memcpy(&fb, dst + o, 4);
    if (plain_float(fa) && plain_float(fb))
      memcpy(dst + o, &fa, 4);
  }
}

static void camera_put_component(const memfile_t *st)
{
  uintptr_t camera_c = camera_c_now();
  if (!camera_c)
    return;
  uint8_t *now = (uint8_t *)camera_c;
  for (size_t r = 0; r < sizeof(comp_head_ranges) / sizeof(*comp_head_ranges); r++)
    copy_plain_floats(now, st->comp_head, comp_head_ranges[r].begin, comp_head_ranges[r].end);
  uint8_t *turn = now + z64hd_game->player_camera_state + CAMERA_TURN_OFF;
  for (uint32_t o = 0; o < CAMERA_TURN_BYTES; o += 4) {
    float fa, fb;
    memcpy(&fa, st->comp_turn + o, 4);
    memcpy(&fb, turn + o, 4);
    if (plain_float(fa) && plain_float(fb))
      memcpy(turn + o, &fa, 4);
  }
}

static bool is_action(uintptr_t p, uintptr_t state)
{
  if (!p || (p & 7u) || !sys_mapped(p, ACTION_BYTES, SYS_PERM_R | SYS_PERM_W) || sys_ptr(p + 0x20) != state)
    return false;
  uintptr_t vt = sys_ptr(p);
  return vt >= z64hd_main && vt - z64hd_main < MAIN_VTABLE_MAX;
}

static void add_action_unique(uintptr_t out[CAMERA_ACTIONS], int *n, uintptr_t node)
{
  for (int i = 0; i < *n; i++)
    if (out[i] == node)
      return;
  out[(*n)++] = node;
}

/* Direct pointer traversal: actor+0x270 -> +0x48 (AIComponent) -> +0x28 (AIControllerSet) ->
 * entry+0x18 (AIControllerBase, +0x1F0==actor) -> +0x18/+0x20 (unit_slots, stride 0x18) ->
 * unit (+0x08==ctrl_base) +0x28/+0x30 (node_slots, stride 0x18) -> node (+0x20==state). */
static int camera_find_actions_direct(uintptr_t actor, uintptr_t state, uintptr_t out[CAMERA_ACTIONS])
{
  uintptr_t comps = sys_ptr(actor + 0x270);
  uintptr_t ai_comp = comps && !(comps & 7u) ? sys_ptr(comps + 0x48) : 0;
  uintptr_t ctrl_set = ai_comp && !(ai_comp & 7u) ? sys_ptr(ai_comp + 0x28) : 0;
  uint32_t set_cnt = 0;
  if (!ctrl_set || (ctrl_set & 7u) || !sys_read32(ctrl_set, &set_cnt) || !set_cnt || set_cnt > 32)
    return 0;
  uintptr_t set_arr = sys_ptr(ctrl_set + 0x08);
  if (!set_arr || (set_arr & 7u) || !sys_mapped(set_arr, (size_t)set_cnt * 8, SYS_PERM_R))
    return 0;

  int n = 0;
  for (uint32_t i = 0; i < set_cnt && n < CAMERA_ACTIONS; i++) {
    uintptr_t entry = sys_ptr(set_arr + (size_t)i * 8);
    uintptr_t ctrl_base = entry && !(entry & 7u) ? sys_ptr(entry + 0x18) : 0;
    if (!ctrl_base || (ctrl_base & 7u) || !sys_mapped(ctrl_base, 0x1F8, SYS_PERM_R) ||
        sys_ptr(ctrl_base + 0x1F0) != actor)
      continue;
    uint32_t unit_cnt = *(const uint32_t *)(ctrl_base + 0x18);
    uintptr_t unit_slots = sys_ptr(ctrl_base + 0x20);
    if (!unit_cnt || unit_cnt > 64 || !unit_slots || (unit_slots & 7u) ||
        !sys_mapped(unit_slots, (size_t)unit_cnt * 0x18, SYS_PERM_R))
      continue;
    for (uint32_t j = 0; j < unit_cnt && n < CAMERA_ACTIONS; j++) {
      uintptr_t unit = sys_ptr(unit_slots + (size_t)j * 0x18);
      if (!unit || (unit & 7u) || !sys_mapped(unit, 0x40, SYS_PERM_R) ||
          sys_ptr(unit + 0x08) != ctrl_base)
        continue;
      uint32_t node_cnt = *(const uint32_t *)(unit + 0x28);
      uintptr_t node_slots = sys_ptr(unit + 0x30);
      if (!node_cnt || node_cnt > 1024 || !node_slots || (node_slots & 7u) ||
          !sys_mapped(node_slots, (size_t)node_cnt * 0x18, SYS_PERM_R))
        continue;
      for (uint32_t k = 0; k < node_cnt && n < CAMERA_ACTIONS; k++) {
        uintptr_t node = sys_ptr(node_slots + (size_t)k * 0x18);
        if (is_action(node, state))
          add_action_unique(out, &n, node);
      }
    }
  }
  return n;
}

/* Bounded non-GPU fallback scan around camera_c within its writable memory region. */
static int camera_find_actions_scan(uintptr_t camera_c, uintptr_t state, uintptr_t out[CAMERA_ACTIONS])
{
  sys_meminfo_t info;
  if (sys_query_memory(&info, camera_c) != 0 || !(info.perm & SYS_PERM_W))
    return 0;
  uintptr_t lo = camera_c > info.addr + ACTION_WINDOW ? camera_c - ACTION_WINDOW : info.addr;
  uintptr_t hi = info.addr + info.size;
  if (hi < info.addr)
    hi = UINTPTR_MAX;
  if (camera_c <= UINTPTR_MAX - ACTION_WINDOW && hi > camera_c + ACTION_WINDOW)
    hi = camera_c + ACTION_WINDOW;
  lo = (lo + 7u) & ~(uintptr_t)7u;
  if (hi < lo + 0x28u)
    return 0;
  int n = 0;
  for (uintptr_t w = lo + 0x20u; w + 8u <= hi && n < CAMERA_ACTIONS; w += 8u) {
    uintptr_t q = w - 0x20u;
    if (*(const volatile uint64_t *)w == state && q + ACTION_BYTES <= hi && is_action(q, state))
      out[n++] = q;
  }
  return n;
}

static int camera_find_actions(uintptr_t out[CAMERA_ACTIONS])
{
  uintptr_t actor = 0;
  uintptr_t camera_c = resolve_camera(&actor);
  if (!camera_c || !actor)
    return 0;
  uintptr_t state = camera_c + z64hd_game->player_camera_state;
  int n = camera_find_actions_direct(actor, state, out);
  const char *how = "direct"; /* not menu text */
  if (!n) {
    n = camera_find_actions_scan(camera_c, state, out);
    how = "scan"; /* not menu text */
  }
  if (n)
    log_printf("memfiles: %d camera action(s) found (%s)", n, how); /* not menu text */
  else
    log_printf("memfiles: no camera action node found"); /* not menu text */
  return n;
}

static void camera_take_actions(memfile_t *st)
{
  uintptr_t a[CAMERA_ACTIONS];
  st->camera_actions = camera_find_actions(a);
  for (int i = 0; i < st->camera_actions; i++) {
    st->action_vtable[i] = (uint32_t)(sys_ptr(a[i]) - z64hd_main);
    st->action_node_idx[i] = *(const uint16_t *)(a[i] + 0x1C);
    memcpy(st->action[i], (const void *)a[i], ACTION_BYTES);
  }
}

static void camera_put_actions(const memfile_t *st, bool find)
{
  static uintptr_t a[CAMERA_ACTIONS];
  static int n;
  uintptr_t state = (uintptr_t)camera_state();
  if (find)
    n = camera_find_actions(a);
  for (int i = 0; i < n; i++)
    if (!state || !is_action(a[i], state))
      a[i] = a[--n], i--;
  for (int i = 0; i < n; i++) {
    uint32_t vt = (uint32_t)(sys_ptr(a[i]) - z64hd_main);
    uint16_t node_idx = *(const uint16_t *)(a[i] + 0x1C);
    int match = -1;
    for (int k = 0; k < st->camera_actions; k++) {
      if (st->action_vtable[k] == vt && st->action_node_idx[k] == node_idx) {
        match = k;
        break;
      }
      if (match < 0 && st->action_vtable[k] == vt)
        match = k;
    }
    if (match < 0)
      continue;
    for (size_t r = 0; r < sizeof(action_ranges) / sizeof(*action_ranges); r++)
      copy_plain_floats((uint8_t *)a[i], st->action[match], action_ranges[r].begin, action_ranges[r].end);
  }
}

static bool camera_view(float out[6])
{
  uintptr_t cam = camera();
  if (!cam || !sys_mapped(cam + 0x38, 24, SYS_PERM_R))
    return false;
  memcpy(out, (const void *)(cam + 0x38), 24);
  return true;
}

typedef void (*camera_arrive_t)(uintptr_t component);

/* After an in-place load the camera's target eases towards Link's new place as if he'd moved fast: do what a map's
 * arrival does instead, rebuilding the target from the actor it follows and asking for a snap. */
static bool camera_arrive(void)
{
  uintptr_t camera_c = camera_c_now();
  if (!camera_c || !z64hd_game->player_camera_arrive || !sys_mapped(camera_c + 0x38, 8, SYS_PERM_R))
    return false;
  uintptr_t followed = sys_ptr(camera_c + 0x38);
  if (!followed || (followed & 7u) || !sys_mapped(followed, 0xC8, SYS_PERM_R))
    return false;
  MAIN_FN(camera_arrive_t, z64hd_game->player_camera_arrive)(camera_c);
  return true;
}

/* the largest axis between the drawn look-at and a, and how far it moved since last frame (in camera_last_at) */
static bool camera_at_gap(const float a[3], float *gap, float *moved)
{
  float v[6];
  if (!camera_view(v))
    return false;
  *gap = *moved = 0;
  for (int i = 0; i < 3; i++) {
    float d = v[3 + i] > a[i] ? v[3 + i] - a[i] : a[i] - v[3 + i];
    float m = v[3 + i] > camera_last_at[i] ? v[3 + i] - camera_last_at[i] : camera_last_at[i] - v[3 + i];
    *gap = d > *gap ? d : *gap;
    *moved = m > *moved ? m : *moved;
    camera_last_at[i] = v[3 + i];
  }
  return true;
}

/* the angle in degrees between the drawn look-at to eye direction and the saved one; false if unreadable */
static bool camera_angle_off(const memfile_t *st, float *deg)
{
  float v[6], a[3], b[3];
  if (!camera_view(v))
    return false;
  for (int i = 0; i < 3; i++) {
    a[i] = v[i] - v[3 + i];
    b[i] = st->view[i] - st->view[3 + i];
  }
  float cx = a[1] * b[2] - a[2] * b[1], cy = a[2] * b[0] - a[0] * b[2], cz = a[0] * b[1] - a[1] * b[0];
  float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  float cross = sqrtf(cx * cx + cy * cy + cz * cz);
  if (cross == 0 && dot == 0)
    return false;
  *deg = atan2f(cross, dot) * 57.29578f;
  return true;
}

static bool camera_drawn(void)
{
  const uint8_t *state = camera_state();
  float v[6], e[3];
  if (!state || !camera_view(v))
    return false;
  memcpy(e, state + 0x450, sizeof(e));
  for (int i = 0; i < 3; i++)
    if (v[i] - e[i] > 0.05f || e[i] - v[i] > 0.05f)
      return false;
  return true;
}

/* --- load override flags & Link helpers -------------------------------------------------------- */

static void arm_load_overrides(const memfile_t *st)
{
  gd_set_bool(F_ALREADY_RESET_LOAD, true);
  gd_set_bool(F_SET_CHILD_MANUALLY, true);
  if (st->have_pos) {
    float p[3] = { (float)st->x, (float)st->y, (float)st->z };
    gd_set_vec3(F_OVERRIDE_TRAVERSE_POS, p);
    gd_set_bool(F_OVERRIDE_TRAVERSE, true);
  } else {
    gd_set_bool(F_OVERRIDE_TRAVERSE, false);
  }
}

static void clear_overrides(void)
{
  gd_set_bool(F_OVERRIDE_TRAVERSE, false);
  gd_set_bool(F_SET_CHILD_MANUALLY, false);
  gd_set_bool(F_ALREADY_RESET_LOAD, false);
  gd_set_bool(F_REQ_RESET_GIMMICK, false);
}

static void clear_boot_event(void)
{
  gd_set_str(GD_STR_EVENT, "");
  gd_set_str(GD_STR_STARTER, "");
}

/* Require at least 2 frames after teleport so physics ticks once, tight position, and matching facing
 * (falling back to position-only after 12 frames in case a slope/wall constrains yaw). */
static bool link_at(const memfile_t *st, int frames)
{
  if (frames < 2)
    return false;
  vec3f_t p;
  if (!link_drawn_pos(&p))
    return false;
  if (p.x - st->x >= 0.25 || st->x - p.x >= 0.25 || p.y - st->y >= 0.25 || st->y - p.y >= 0.25 ||
      p.z - st->z >= 0.25 || st->z - p.z >= 0.25)
    return false;
  if (st->fx * st->fx + st->fz * st->fz >= 0.25 && frames < 12) {
    double fx = 0, fz = 0;
    if (!link_facing(&fx, &fz))
      return false;
    if (fx * st->fx + fz * st->fz < 0.98)
      return false;
  }
  return true;
}

static bool move_link(const memfile_t *st)
{
  vec3f_t to = { (float)st->x, (float)st->y, (float)st->z };
  double len = st->fx * st->fx + st->fz * st->fz;
  if (len < 0.25)
    return cct_teleport(&to, 0);
  float c = (float)st->fz, sn = (float)st->fx;
  const float rotation[9] = { c, 0, sn, 0, 1, 0, -sn, 0, c };
  return cct_teleport(&to, rotation);
}

/* --- Nayru's Love --------------------------------------------------------------------------------- */

/* Its time left lives in the player component, which a fast load (ResetGimmick) never rebuilds, and a frame
 * update copies it over the GameData mirror the deserializer just restored: so the time is put back here. */
#define PLAYER_COMPONENT 0x358u
#define NAYRU_LEFT       0x1634u /* float frames left; the barrier is on while above NAYRU_EPS */
#define NAYRU_TOTAL      0x1638u
#define NAYRU_MARK       0x163Cu /* -1.0 whenever it's set or reset */
#define NAYRU_EPS        1.19e-7f

typedef void (*component_fn_t)(uintptr_t component);

static uintptr_t player_component(void)
{
  uintptr_t link = link_actor();
  uintptr_t comps = link ? sys_ptr(link + 0x270) : 0;
  uintptr_t c = comps ? sys_ptr(comps + PLAYER_COMPONENT) : 0;
  return c && sys_mapped(c + NAYRU_LEFT, 12, SYS_PERM_R | SYS_PERM_W) ? c : 0;
}

static bool nayru_get(float *left, float *total)
{
  uintptr_t c = player_component();
  if (!c)
    return false;
  *left = *(volatile float *)(c + NAYRU_LEFT);
  *total = *(volatile float *)(c + NAYRU_TOTAL);
  if (!(*left > NAYRU_EPS))
    *left = *total = 0;
  return true;
}

static void restore_nayru(const memfile_t *st)
{
  uintptr_t c = st->have_nayru ? player_component() : 0;
  if (!c)
    return;
  volatile float *t = (volatile float *)(c + NAYRU_LEFT);
  bool on = t[0] > NAYRU_EPS;
  if (st->nayru_left > NAYRU_EPS) {
    if (!on)
      log_printf("memfile %d load: Nayru's Love on, %d frames left", active_slot + 1, /* not menu text */
                 (int)st->nayru_left);
    t[0] = st->nayru_left;
    t[1] = st->nayru_total;
    *(volatile float *)(c + NAYRU_MARK) = -1.0f;
  } else if (on) {
    log_printf("memfile %d load: Nayru's Love off", active_slot + 1); /* not menu text */
    if (z64hd_game->player_nayru_reset) {
      MAIN_FN(component_fn_t, z64hd_game->player_nayru_reset)(c);
    } else {
      t[0] = t[1] = 0;
      *(volatile float *)(c + NAYRU_MARK) = -1.0f;
    }
  }
}

static void restore_vitals_and_gear(const memfile_t *st)
{
  uint32_t now = 0;
  bool had = time_of_day(&now);
  if (st->have_time && set_time_of_day(st->time))
    log_printf("memfile %d load: time %02u:%02u -> %02u:%02u", active_slot + 1, /* not menu text */
               had ? (unsigned)(now / 3600) : 99u, had ? (unsigned)(now / 60 % 60) : 99u,
               (unsigned)(st->time / 3600), (unsigned)(st->time / 60 % 60));
  int32_t *cur, *max;
  if (st->have_life && link_health(&cur, &max) && *cur != st->life) {
    log_printf("memfile %d load: life %d -> %d", active_slot + 1, (int)*cur, (int)st->life); /* not menu text */
    *cur = st->life <= *max ? st->life : *max;
  }
  int32_t magic;
  if (st->have_magic && gd_get_int(F_MAGIC, &magic) && magic != st->magic) {
    log_printf("memfile %d load: magic %d -> %d", active_slot + 1, (int)magic, (int)st->magic); /* not menu text */
    gd_poke_int(F_MAGIC, st->magic);
    gd_set_int(F_MAGIC, st->magic);
  }
  if (gear_reapply_now())
    log_printf("memfile %d load: gear put on", active_slot + 1); /* not menu text */
  restore_nayru(st);
}

/* --- the item wheel after a fast load ----------------------------------------------------------- */

/* Sequence/Main.module.ainb runs, side by side from its root (node 23), UpdateShortCutOpenHoldFrameCounter
 * (what opens the item wheel on L/R) and the picker of its branches (node 15). The ResetGimmick branch (taken
 * for DebugParam.IsRequestedResetGimmick) ends with a transition to node 15 alone, so after one the item wheel
 * is dead until something leaves Main (a map change ends on node 23, menus and events re-enter it from the
 * root). The game keeps the file loaded: before each fast load, find it through the Sequence component and point
 * that transition at node 23 like the map change branch. The file is the same in 1.0.0 and 1.1.1.
 *
 * The chain (the same offsets in both builds; FINDINGS 9.19): the scene's component 0 is Sequence; [+0xE8] lists
 * its AI controllers' holders (count +0, array +8; the root's setup 1.0.0 main+0x3B60240, 1.1.1 main+0x3BBDE50);
 * a holder's controller is at +0x18; a controller lists the AINB files it loaded (count +0x168, array +0x170;
 * stored at 1.0.0 main+0x7B5E0, 1.1.1 main+0x38480), each a definition holding the file's bytes at +0x10
 * (1.0.0 main+0x82C374, 1.1.1 main+0x840D60). Main.module is told apart by its header. */
#define MAIN_AINB_SIZE     11395u
#define MAIN_AINB_RESET_TO 0x1204u               /* s32 node index: 15 in the file, 23 patched */
#define SEQ_HOLDERS        0xE8                  /* [Sequence +this]: its controllers' holders */
#define SEQ_HOLDERS_MAX    16
#define HOLDER_CONTROLLER  0x18
#define CTRL_DEF_COUNT     0x168                 /* u32 */
#define CTRL_DEFS          0x170
#define CTRL_DEFS_MAX      0x15E                 /* the loader's own cap */
#define DEF_FILE           0x10

static const uint8_t main_ainb_head[0x40] = {
  0x41, 0x49, 0x42, 0x20, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x24, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xfc, 0x08, 0x00, 0x00, 0x60, 0x27, 0x00, 0x00, 0x5c, 0x27, 0x00, 0x00, 0x58, 0x22, 0x00, 0x00,
  0x6c, 0x26, 0x00, 0x00, 0x98, 0x24, 0x00, 0x00, 0x6c, 0x26, 0x00, 0x00, 0x58, 0x22, 0x00, 0x00,
};
/* the transition plug's context: index 15 then 7 (its transition entry) and the next plug's 4 */
static const uint8_t main_ainb_reset_plug[12] = { 0x0f, 0, 0, 0, 0x07, 0, 0, 0, 0x04, 0, 0, 0 };

/* WarpMgr, the game component behind Farore's Wind's sparkle (actor FaroreWindWarpPoint), places the sparkle only
 * when told GameData changed (its vtable slot 6, with change bits; none set means all), which the deserializer
 * doesn't do, and ResetGimmick keeps the sparkle: so it's told after a fast load. It's the current scene's component
 * 22, as the game's own getter reads it (1.0.0 main+0x27C2AD0, 1.1.1 main+0x28147E0; FINDINGS 9.21). */
#define SCENE_CURRENT    0x1F0 /* [scene manager +this]: the current scene */
#define SCENE_COUNT      0x50  /* u32 */
#define SCENE_COMPONENTS 0x58  /* pointer to the components' pointers */
#define SCENE_AUTOSAVE   12
#define SCENE_MINIGAME   18
#define SCENE_TIMER      19
#define SCENE_WARP_MGR   22
#define AUTOSAVE_SLOT_B  0x178u
#define AUTOSAVE_SLOT_A  0x198u
#define PLAYER_RESTART   0x18A4u
#define PLAYER_FALL_ORIG 0x18D4u
#define PLAYER_FALL_PEAK 0x18E0u
#define PLAYER_JS_HEIGHT 0x18E8u
#define PLAYER_FALL_DIST 0x190Cu
#define BASE_FALL_ORIG    0x1898u
#define BASE_FALL_DIST    0x189Cu
#define PROBE_COMPONENT   0x460u  /* [[actor + 0x270] + 0x460]: VolumeStats ground/void sensor (1.0.0 & 1.1.1) */
#define PROBE_POS         0xE0u   /* 3 x f32: live probe query origin */
#define PROBE_C_RECORDS   0x950u  /* 8 x 0x48 VolumeStatsHit level records (vtable at +0x948 stays intact) */
#define PROBE_C_REC_BYTES 0x240u
#define PROBE_C_MAX_LEVEL 0xB90u  /* i32: -1 when level records empty */
#define PROBE_C_ORIGIN    0xB94u  /* 3 x f32: chunk origin */
#define PROBE_C_POINT     0xBA0u  /* 3 x f32: 8-unit fence anchor point */
#define PROBE_C_CELL      0xBACu  /* 3 x i32: cell index within chunk */
#define PROBE_C_FACE      0xBB8u  /* i32: hit face (0..5) */
#define PROBE_C_HIT       0xBBCu  /* u8: bit 0 = has_point */
#define PROBE_C_EXTRA_FLG 0xBC0u  /* u32 */
#define PROBE_C_EXTRA_CNT 0xD48u  /* i32: -1 when none */
#define PROBE_C_DIST3     0x1158u /* f32: 3D distance |Q - C| */
#define PROBE_C_DIST2     0x115Cu /* f32: horizontal distance (compared against 8.0 for void) */
#define PROBE_C_PHASE     0x1160u /* u32: 10-frame Fibonacci ray rotation counter (0..9) */
#define PROBE_MAP_SIZE    0x1168u
#define PROBE_DEFAULT_DY  0.585f
#define PROBE_FLT_MAX     0x7F7FFFFFu
#define PLAYER_GND_FLAGS  0x13D0u
#define PLAYER_GND_FAR    0x2000000u /* bit 25: FLAG_FAR_FROM_GROUND (8-unit void) */

static uintptr_t find_autosave_mgr(void)
{
  if (!z64hd_game || !z64hd_game->vt_autosave || !z64hd_game->scene_mgr_ptr)
    return 0;
  uintptr_t scene_mgr = sys_ptr(MAIN(z64hd_game->scene_mgr_ptr));
  uintptr_t scene = scene_mgr ? sys_ptr(scene_mgr + SCENE_CURRENT) : 0;
  uint32_t count = 0;
  if (!scene || !sys_read32(scene + SCENE_COUNT, &count) || count <= SCENE_AUTOSAVE)
    return 0;
  uintptr_t components = sys_ptr(scene + SCENE_COMPONENTS);
  uintptr_t m = components ? sys_ptr(components + SCENE_AUTOSAVE * sizeof(uintptr_t)) : 0;
  if (!m || (m & 7u) || !sys_mapped(m, AUTOSAVE_SLOT_A + SAVEPOS_SLOT_BYTES, SYS_PERM_R | SYS_PERM_W))
    return 0;
  return sys_ptr(m) == MAIN(z64hd_game->vt_autosave) ? m : 0;
}

static bool player_and_base_components(uintptr_t *player_out, uintptr_t *base_out)
{
  uintptr_t link = link_actor();
  uintptr_t comps = link ? sys_ptr(link + 0x270) : 0;
  uintptr_t player = comps ? sys_ptr(comps + PLAYER_COMPONENT) : 0;
  uintptr_t base = comps ? sys_ptr(comps + 0x2B8) : 0;
  if (!player || !base || (player & 7u) || (base & 7u) ||
      !sys_mapped(player, 0x1914, SYS_PERM_R | SYS_PERM_W) ||
      !sys_mapped(base, 0x18A4, SYS_PERM_R | SYS_PERM_W) ||
      sys_ptr(player + 0x18) != link || sys_ptr(base + 0x1340) != link)
    return false;
  *player_out = player;
  *base_out = base;
  return true;
}

static inline float fsqrtf(float x)
{
#ifdef __aarch64__
  float r;
  __asm__("fsqrt %s0, %s1" : "=w"(r) : "w"(x));
  return r;
#else
  return sqrtf(x);
#endif
}

static bool finite_vec3(const float v[3]);

static uintptr_t probe_component(void)
{
  uintptr_t link = link_actor();
  uintptr_t comps = link ? sys_ptr(link + 0x270) : 0;
  uintptr_t probe = comps ? sys_ptr(comps + PROBE_COMPONENT) : 0;
  return probe && !(probe & 7u) && sys_mapped(probe, PROBE_MAP_SIZE, SYS_PERM_R | SYS_PERM_W) ? probe : 0;
}

static bool near_saved_pos(const memfile_t *st)
{
  vec3f_t p;
  if (!st->have_pos || !link_drawn_pos(&p))
    return false;
  double dx = p.x - st->x, dy = p.y - st->y, dz = p.z - st->z;
  return dx * dx + dy * dy + dz * dz <= 4.0;
}

static void clear_far_flag(void)
{
  uintptr_t player = 0, base = 0;
  if (player_and_base_components(&player, &base))
    *(volatile uint32_t *)(player + PLAYER_GND_FLAGS) &= ~PLAYER_GND_FAR;
}

/* Clear Probe C's held anchor (pointer-free). Setting has_point=0 makes the game's Probe C update
 * (main+0x38AEF0) run all 64 Fibonacci rays in a single frame on its next tick instead of keeping a stale point. */
static void clear_void_ring(void)
{
  uintptr_t probe = probe_component();
  if (probe) {
    memset((void *)(probe + PROBE_C_RECORDS), 0, PROBE_C_REC_BYTES);
    *(volatile int32_t *)(probe + PROBE_C_MAX_LEVEL) = -1;
    *(volatile uint8_t *)(probe + PROBE_C_HIT) = 0;
    *(volatile uint32_t *)(probe + PROBE_C_EXTRA_FLG) = 0;
    *(volatile int32_t *)(probe + PROBE_C_EXTRA_CNT) = -1;
    *(volatile uint32_t *)(probe + PROBE_C_DIST3) = PROBE_FLT_MAX;
    *(volatile uint32_t *)(probe + PROBE_C_DIST2) = PROBE_FLT_MAX;
    *(volatile uint32_t *)(probe + PROBE_C_PHASE) = 0;
  }
  clear_far_flag();
}

/* Restore Probe C's saved 8-unit fence state without touching its vtable (+0x948) or copying heap pointers:
 * max_level=-1 and extra_count=-1 make every VolumeStatsHit reader skip record bitstreams safely across reloads.
 * For older memfiles without saved ring data, clear Probe C so the game rescans all 64 rays at Link's new spot. */
static void restore_void_ring(const memfile_t *st)
{
  if (!st->have_void_ring) {
    clear_void_ring();
    return;
  }
  uintptr_t probe = probe_component();
  if (!probe)
    return;
  memset((void *)(probe + PROBE_C_RECORDS), 0, PROBE_C_REC_BYTES);
  *(volatile int32_t *)(probe + PROBE_C_MAX_LEVEL) = -1;
  *(volatile uint32_t *)(probe + PROBE_C_EXTRA_FLG) = 0;
  *(volatile int32_t *)(probe + PROBE_C_EXTRA_CNT) = -1;
  if (st->void_ring_hit && finite_vec3(st->void_ring_point)) {
    memcpy((void *)(probe + PROBE_C_ORIGIN), st->void_ring_origin, sizeof(st->void_ring_origin));
    memcpy((void *)(probe + PROBE_C_POINT), st->void_ring_point, sizeof(st->void_ring_point));
    memcpy((void *)(probe + PROBE_C_CELL), st->void_ring_cell, sizeof(st->void_ring_cell));
    *(volatile int32_t *)(probe + PROBE_C_FACE) = st->void_ring_face;
    *(volatile uint8_t *)(probe + PROBE_C_HIT) = 1;
    *(volatile uint32_t *)(probe + PROBE_C_PHASE) = st->void_ring_phase % 10u;
    float q[3];
    if (!st->have_pos || near_saved_pos(st)) {
      memcpy(q, (const void *)(probe + PROBE_POS), sizeof(q));
    } else {
      q[0] = (float)st->x;
      q[1] = (float)st->y + PROBE_DEFAULT_DY;
      q[2] = (float)st->z;
    }
    float dx = q[0] - st->void_ring_point[0];
    float dy = q[1] - st->void_ring_point[1];
    float dz = q[2] - st->void_ring_point[2];
    float d2_sq = dx * dx + dz * dz;
    float d3_sq = d2_sq + dy * dy;
    *(volatile float *)(probe + PROBE_C_DIST3) = fsqrtf(d3_sq);
    *(volatile float *)(probe + PROBE_C_DIST2) = fsqrtf(d2_sq);
  } else {
    *(volatile uint8_t *)(probe + PROBE_C_HIT) = 0;
    *(volatile uint32_t *)(probe + PROBE_C_DIST3) = PROBE_FLT_MAX;
    *(volatile uint32_t *)(probe + PROBE_C_DIST2) = PROBE_FLT_MAX;
    *(volatile uint32_t *)(probe + PROBE_C_PHASE) = 0;
  }
  clear_far_flag();
}

static void sync_void_ring_during_load(const memfile_t *st, bool on_move_start)
{
  if (st->have_void_ring) {
    if (near_saved_pos(st))
      restore_void_ring(st);
    else if (on_move_start)
      clear_void_ring();
  } else if (on_move_start) {
    clear_void_ring();
  }
}

typedef void (*gd_changed_t)(uintptr_t component, const void *change_bits);

static uintptr_t global_random(void)
{
  if (!z64hd_game || !z64hd_game->global_random_ptr)
    return 0;
  uintptr_t holder = sys_ptr(MAIN(z64hd_game->global_random_ptr));
  uintptr_t rng = holder ? sys_ptr(holder) : 0;
  return rng && !(rng & 3u) && sys_mapped(rng, 16, SYS_PERM_R | SYS_PERM_W) ? rng : 0;
}

static void resync_scene_gd_component(uint32_t idx, uint32_t vt_off, const char *label)
{
  if (!vt_off || !z64hd_game || !z64hd_game->scene_mgr_ptr)
    return;
  uintptr_t scene_mgr = sys_ptr(MAIN(z64hd_game->scene_mgr_ptr));
  uintptr_t scene = scene_mgr ? sys_ptr(scene_mgr + SCENE_CURRENT) : 0;
  uint32_t count = 0;
  if (!scene || !sys_read32(scene + SCENE_COUNT, &count) || count <= idx)
    return;
  uintptr_t components = sys_ptr(scene + SCENE_COMPONENTS);
  uintptr_t m = components ? sys_ptr(components + idx * sizeof(uintptr_t)) : 0;
  uintptr_t vt = MAIN(vt_off);
  if (!m || (m & 7u) || !sys_mapped(m, 8, SYS_PERM_R) || sys_ptr(m) != vt)
    return;
  uintptr_t slot6 = sys_ptr(vt + 6 * 8);
  if (slot6 < MAIN(0) || slot6 >= MAIN(MAIN_CODE_SIZE))
    return;
  static const uint8_t everything[0x20];
  ((gd_changed_t)slot6)(m, everything);
  log_printf("memfiles: %s resynced from GameData", label); /* not menu text */
}

static void resync_scene_mgrs(void)
{
  if (!z64hd_game)
    return;
  resync_scene_gd_component(SCENE_TIMER, z64hd_game->vt_timer_mgr, "TimerMgr");
  resync_scene_gd_component(SCENE_MINIGAME, z64hd_game->vt_minigame_mgr, "MinigameMgr");
}

static void capture_link_extra(memfile_t *st)
{
  uintptr_t link = link_actor();
  st->have_vel = link && cct_velocity(link, &st->vel);
  uintptr_t player = 0, base = 0;
  if (player_and_base_components(&player, &base)) {
    st->fall[0] = *(const volatile float *)(player + PLAYER_FALL_ORIG);
    st->fall[1] = *(const volatile float *)(player + PLAYER_FALL_PEAK);
    st->fall[2] = *(const volatile float *)(player + PLAYER_JS_HEIGHT);
    st->fall[3] = *(const volatile float *)(player + PLAYER_FALL_DIST);
    st->fall[4] = *(const volatile float *)(base + BASE_FALL_ORIG);
    st->fall[5] = *(const volatile float *)(base + BASE_FALL_DIST);
    st->have_fall = true;
    memcpy(st->player_restart, (const void *)(player + PLAYER_RESTART), PLAYER_RESTART_BYTES);
    st->have_restart = true;
  }
  uintptr_t autosave = find_autosave_mgr();
  if (autosave) {
    memcpy(st->savepos_a, (const void *)(autosave + AUTOSAVE_SLOT_A), SAVEPOS_SLOT_BYTES);
    memcpy(st->savepos_b, (const void *)(autosave + AUTOSAVE_SLOT_B), SAVEPOS_SLOT_BYTES);
    st->have_savepos = true;
  }
  uintptr_t rng = global_random();
  if (rng) {
    memcpy(st->rng, (const void *)rng, sizeof(st->rng));
    st->have_rng = (st->rng[0] | st->rng[1] | st->rng[2] | st->rng[3]) != 0;
  }
  uintptr_t probe = probe_component();
  if (probe) {
    st->void_ring_hit = (*(const volatile uint8_t *)(probe + PROBE_C_HIT)) & 1u;
    memcpy(st->void_ring_point, (const void *)(probe + PROBE_C_POINT), sizeof(st->void_ring_point));
    memcpy(st->void_ring_origin, (const void *)(probe + PROBE_C_ORIGIN), sizeof(st->void_ring_origin));
    memcpy(st->void_ring_cell, (const void *)(probe + PROBE_C_CELL), sizeof(st->void_ring_cell));
    st->void_ring_face = *(const volatile int32_t *)(probe + PROBE_C_FACE);
    st->void_ring_phase = *(const volatile uint32_t *)(probe + PROBE_C_PHASE) % 10u;
    st->have_void_ring = !st->void_ring_hit || (finite_vec3(st->void_ring_point) && finite_vec3(st->void_ring_origin));
  }
}

static void restore_link_extra(const memfile_t *st)
{
  uintptr_t link = link_actor();
  uintptr_t player = 0, base = 0;
  bool have_pb = player_and_base_components(&player, &base);
  if (st->have_fall && have_pb) {
    *(volatile float *)(player + PLAYER_FALL_ORIG) = st->fall[0];
    *(volatile float *)(player + PLAYER_FALL_PEAK) = st->fall[1];
    *(volatile float *)(player + PLAYER_JS_HEIGHT) = st->fall[2];
    *(volatile float *)(player + PLAYER_FALL_DIST) = st->fall[3];
    *(volatile float *)(base + BASE_FALL_ORIG) = st->fall[4];
    *(volatile float *)(base + BASE_FALL_DIST) = st->fall[5];
  } else if (link) {
    cct_clear_fall_distance(link);
  }
  if (st->have_restart && have_pb)
    memcpy((void *)(player + PLAYER_RESTART), st->player_restart, PLAYER_RESTART_BYTES);
  uintptr_t autosave = st->have_savepos ? find_autosave_mgr() : 0;
  if (autosave) {
    memcpy((void *)(autosave + AUTOSAVE_SLOT_A), st->savepos_a, SAVEPOS_SLOT_BYTES);
    memcpy((void *)(autosave + AUTOSAVE_SLOT_B), st->savepos_b, SAVEPOS_SLOT_BYTES);
  }
  if (st->have_rng && (st->rng[0] | st->rng[1] | st->rng[2] | st->rng[3])) {
    uintptr_t rng = global_random();
    if (rng)
      memcpy((void *)rng, st->rng, sizeof(st->rng));
  }
  restore_void_ring(st);
  if (st->have_vel && link)
    cct_set_velocity(link, &st->vel);
}

/* a WarpMgr as its constructor (main+0xE0FE60) leaves it: its vtable, and a listener's vtable at +0xB8 */
static bool is_warp_mgr(uintptr_t m, uintptr_t vt)
{
  if (!sys_mapped(m, 0xD0, SYS_PERM_R | SYS_PERM_W) || sys_ptr(m) != vt)
    return false;
  uintptr_t listener = sys_ptr(m + 0xB8);
  return listener >= MAIN(0) && listener < MAIN(0x5000000);
}

typedef void (*warp_place_t)(uintptr_t mgr, float x, float y, float z);

#define F_FW_BANC 0xB09FBD99u /* WarpPoint_FaroreWind.BancName */
#define F_FW_POS  0xBEB37B31u /* WarpPoint_FaroreWind.Pos */

/* the current scene's WarpMgr, or 0 */
static uintptr_t find_warp_mgr(uintptr_t vt)
{
  if (!vt || !z64hd_game->scene_mgr_ptr)
    return 0;
  uintptr_t scene_mgr = sys_ptr(MAIN(z64hd_game->scene_mgr_ptr));
  uintptr_t scene = scene_mgr ? sys_ptr(scene_mgr + SCENE_CURRENT) : 0;
  uint32_t count;
  if (!scene || !sys_read32(scene + SCENE_COUNT, &count) || count <= SCENE_WARP_MGR)
    return 0;
  uintptr_t components = sys_ptr(scene + SCENE_COMPONENTS);
  uintptr_t m = components ? sys_ptr(components + SCENE_WARP_MGR * sizeof(uintptr_t)) : 0;
  return m && is_warp_mgr(m, vt) ? m : 0;
}

static void refresh_warp_mgr(const memfile_t *st)
{
  uintptr_t vt = z64hd_game->vt_warp_mgr ? MAIN(z64hd_game->vt_warp_mgr) : 0;
  uintptr_t warp_mgr = find_warp_mgr(vt);
  if (!warp_mgr) {
    log_printf("memfiles: WarpMgr not found: Farore's Wind sparkle not refreshed"); /* not menu text */
    return;
  }
  uintptr_t slot6 = sys_ptr(vt + 6 * 8);
  if (slot6 < MAIN(0) || slot6 >= MAIN(0x3C80000))
    return;
  static const uint8_t everything[0x20];
  ((gd_changed_t)slot6)(warp_mgr, everything);
  /* the point is on this map: put the sparkle there, the way casting does */
  const char *banc = gd_get_str(F_FW_BANC);
  float pos[3];
  if (banc && st->map[0] && str_equal(banc, st->map) && gd_get_vec3(F_FW_POS, pos) && z64hd_game->warp_mgr_place) {
    MAIN_FN(warp_place_t, z64hd_game->warp_mgr_place)(warp_mgr, pos[0], pos[1], pos[2]);
    log_printf("memfiles: Farore's Wind sparkle put at %.2f %.2f %.2f", /* not menu text */
               (double)pos[0], (double)pos[1], (double)pos[2]);
  } else {
    log_printf("memfiles: Farore's Wind sparkle re-read (no point on this map)"); /* not menu text */
  }
}

/* point Main.module's ResetGimmick transition at the root, if the file is at f; true when f is Main.module */
static bool patch_main_ainb(uintptr_t f)
{
  if (!sys_mapped(f, MAIN_AINB_SIZE, SYS_PERM_R | SYS_PERM_W) ||
      !bytes_equal((const void *)f, main_ainb_head, sizeof(main_ainb_head)))
    return false;
  uint8_t *plug = (uint8_t *)(f + MAIN_AINB_RESET_TO);
  if (!bytes_equal(plug + 4, main_ainb_reset_plug + 4, 8) || (plug[0] != 15 && plug[0] != 23) || plug[1] || plug[2] ||
      plug[3])
    return false;
  if (plug[0] != 23) {
    plug[0] = 23;
    log_printf("memfiles: Main.module at %lx: ResetGimmick now returns to the root", (unsigned long)f); /* not menu text */
  }
  return true;
}

/* before a fast load: patch the loaded Main.module (see above) */
static void patch_main_module(void)
{
  uintptr_t scene_mgr = z64hd_game->scene_mgr_ptr ? sys_ptr(MAIN(z64hd_game->scene_mgr_ptr)) : 0;
  uintptr_t scene = scene_mgr ? sys_ptr(scene_mgr + SCENE_CURRENT) : 0;
  uintptr_t components = scene ? sys_ptr(scene + SCENE_COMPONENTS) : 0;
  uintptr_t sequence = components ? sys_ptr(components) : 0;
  uintptr_t holders = sequence ? sys_ptr(sequence + SEQ_HOLDERS) : 0;
  uint32_t n_holders = 0;
  if (holders && sys_read32(holders, &n_holders) && n_holders <= SEQ_HOLDERS_MAX) {
    uintptr_t holder_list = sys_ptr(holders + 8);
    for (uint32_t i = 0; holder_list && i < n_holders; i++) {
      uintptr_t holder = sys_ptr(holder_list + i * sizeof(uintptr_t));
      uintptr_t ctrl = holder ? sys_ptr(holder + HOLDER_CONTROLLER) : 0;
      uint32_t n_defs = 0;
      if (!ctrl || !sys_read32(ctrl + CTRL_DEF_COUNT, &n_defs) || n_defs > CTRL_DEFS_MAX)
        continue;
      uintptr_t defs = sys_ptr(ctrl + CTRL_DEFS);
      for (uint32_t k = 0; defs && k < n_defs; k++) {
        uintptr_t def = sys_ptr(defs + k * sizeof(uintptr_t));
        if (def && patch_main_ainb(sys_ptr(def + DEF_FILE)))
          return;
      }
    }
  }
  log_printf("memfiles: Main.module not found: no item wheel after this fast load"); /* not menu text */
}

/* --- per-actor Blackboard primitive snapshot (1.0.0 & 1.1.1) ----------------------------------- */

#define BB_TABLES_N   5
#define BB_TABLE_MAX  256u
#define BB_STRUCT_LEN 0x5A0u

/* Blackboard typed array headers inside [AIControllerBase + 0x1F8] and Actor Blackboard [[comps + 0] + 0x10]
 * (same offsets in 1.0.0 and 1.1.1). Each table's names count/ptr sit at count_off - 0x10 and count_off - 0x08. */
static const struct { uint16_t count_off, ptr_off, elem_size; bool is_float; } bb_tables[BB_TABLES_N] = {
  { 0x0A8, 0x0B0,  4, false }, /* S32 */
  { 0x120, 0x128,  4, true  }, /* F32 */
  { 0x198, 0x1A0,  1, false }, /* Bool */
  { 0x210, 0x218, 12, true  }, /* Vec3f */
  { 0x558, 0x560,  4, false }, /* U32 */
};

static uintptr_t actor_ai_blackboard(uintptr_t actor)
{
  if (!sys_mapped(actor + 0x270, 8, SYS_PERM_R))
    return 0;
  uintptr_t comps = sys_ptr(actor + 0x270);
  if (!comps || (comps & 7u) || !sys_mapped(comps + 0x48, 8, SYS_PERM_R))
    return 0;
  uintptr_t ai_comp = sys_ptr(comps + 0x48);
  if (!ai_comp || (ai_comp & 7u) || !sys_mapped(ai_comp + 0x28, 8, SYS_PERM_R))
    return 0;
  uintptr_t ctrl_set = sys_ptr(ai_comp + 0x28);
  uint32_t set_cnt = 0;
  if (!ctrl_set || (ctrl_set & 7u) || !sys_read32(ctrl_set, &set_cnt) || !set_cnt || set_cnt > 32u)
    return 0;
  uintptr_t set_arr = sys_ptr(ctrl_set + 0x08);
  if (!set_arr || (set_arr & 7u) || !sys_mapped(set_arr, (size_t)set_cnt * 8, SYS_PERM_R))
    return 0;
  for (uint32_t i = 0; i < set_cnt; i++) {
    uintptr_t entry = sys_ptr(set_arr + (size_t)i * 8);
    uintptr_t ctrl = entry && !(entry & 7u) ? sys_ptr(entry + 0x18) : 0;
    if (!ctrl || (ctrl & 7u) || !sys_mapped(ctrl, 0x200, SYS_PERM_R) || sys_ptr(ctrl + 0x1F0) != actor)
      continue;
    uintptr_t bb = sys_ptr(ctrl + 0x1F8);
    if (bb && !(bb & 7u) && sys_mapped(bb, BB_STRUCT_LEN, SYS_PERM_R | SYS_PERM_W))
      return bb;
  }
  return 0;
}

/* Actor Blackboard [[comps + 0x00] + 0x10]: used by BSA-only actors such as Cuccos (WarpPos, LandingPos, HitCount). */
static uintptr_t actor_own_blackboard(uintptr_t actor)
{
  if (!sys_mapped(actor + 0x270, 8, SYS_PERM_R))
    return 0;
  uintptr_t comps = sys_ptr(actor + 0x270);
  if (!comps || (comps & 7u) || !sys_mapped(comps, 8, SYS_PERM_R))
    return 0;
  uintptr_t bb_comp = sys_ptr(comps);
  if (!bb_comp || (bb_comp & 7u) || !sys_mapped(bb_comp, 0x18, SYS_PERM_R))
    return 0;
  uintptr_t bb = sys_ptr(bb_comp + 0x10);
  return bb && !(bb & 7u) && sys_mapped(bb, BB_STRUCT_LEN, SYS_PERM_R | SYS_PERM_W) ? bb : 0;
}

static uintptr_t actor_blackboard(uintptr_t actor)
{
  uintptr_t bb = actor_ai_blackboard(actor);
  return bb ? bb : actor_own_blackboard(actor);
}

static bool capture_bb_blob(uintptr_t bb, uint8_t *dst, uint32_t cap, uint32_t *len_out)
{
  if (!bb)
    return false;
  uint16_t counts[BB_TABLES_N] = { 0 };
  uintptr_t ptrs[BB_TABLES_N] = { 0 };
  uint32_t total = BB_TABLES_N * sizeof(uint16_t);
  bool any = false;
  for (int t = 0; t < BB_TABLES_N; t++) {
    uint32_t cnt = 0;
    if (!sys_read32(bb + bb_tables[t].count_off, &cnt) || !cnt || cnt > BB_TABLE_MAX)
      continue;
    uintptr_t p = sys_ptr(bb + bb_tables[t].ptr_off);
    size_t bytes = (size_t)cnt * bb_tables[t].elem_size;
    if (!p || !sys_mapped(p, bytes, SYS_PERM_R))
      continue;
    counts[t] = (uint16_t)cnt;
    ptrs[t] = p;
    total += (uint32_t)bytes;
    any = true;
  }
  if (!any || total > cap)
    return false;
  uint32_t at = 0;
  memcpy(dst, counts, sizeof(counts));
  at += (uint32_t)sizeof(counts);
  for (int t = 0; t < BB_TABLES_N; t++) {
    if (!counts[t])
      continue;
    uint32_t bytes = (uint32_t)counts[t] * bb_tables[t].elem_size;
    memcpy(dst + at, (const void *)ptrs[t], bytes);
    at += bytes;
  }
  *len_out = total;
  return true;
}

static bool restore_bb_blob(uintptr_t bb, const uint8_t *src, uint32_t len)
{
  if (!bb || !src || len < BB_TABLES_N * sizeof(uint16_t))
    return false;
  uint16_t counts[BB_TABLES_N];
  memcpy(counts, src, sizeof(counts));
  uint32_t at = (uint32_t)sizeof(counts);
  bool wrote = false;
  for (int t = 0; t < BB_TABLES_N; t++) {
    uint32_t bytes = (uint32_t)counts[t] * bb_tables[t].elem_size;
    if (!counts[t])
      continue;
    if (at + bytes > len)
      break;
    uint32_t live_cnt = 0;
    if (sys_read32(bb + bb_tables[t].count_off, &live_cnt) && live_cnt == counts[t]) {
      uintptr_t dst = sys_ptr(bb + bb_tables[t].ptr_off);
      if (dst && sys_mapped(dst, bytes, SYS_PERM_R | SYS_PERM_W)) {
        if (bb_tables[t].is_float) {
          uint32_t nf = bytes / 4u;
          for (uint32_t k = 0; k < nf; k++) {
            float v = 0;
            memcpy(&v, src + at + k * 4u, 4);
            if (v == v && v > -1e9f && v < 1e9f) {
              memcpy((uint8_t *)dst + k * 4u, &v, 4);
              wrote = true;
            }
          }
        } else {
          memcpy((void *)dst, src + at, bytes);
          wrote = true;
        }
      }
    }
    at += bytes;
  }
  return wrote;
}

static bool bb_find_named(uintptr_t bb, int table_idx, const char *name, uintptr_t *val_addr, bool write)
{
  if (!bb || table_idx < 0 || table_idx >= BB_TABLES_N || !name || !name[0])
    return false;
  uint16_t cnt_off = bb_tables[table_idx].count_off;
  uint16_t ptr_off = bb_tables[table_idx].ptr_off;
  uint16_t esz = bb_tables[table_idx].elem_size;
  uint32_t n_names = 0, n_vals = 0;
  if (!sys_read32(bb + cnt_off - 0x10u, &n_names) || !sys_read32(bb + cnt_off, &n_vals) ||
      !n_names || n_names > BB_TABLE_MAX || n_vals < n_names)
    return false;
  uintptr_t names = sys_ptr(bb + cnt_off - 0x08u);
  uintptr_t vals = sys_ptr(bb + ptr_off);
  uint32_t perm = write ? (SYS_PERM_R | SYS_PERM_W) : SYS_PERM_R;
  if (!names || (names & 7u) || !vals || !sys_mapped(names, (size_t)n_names * 8u, SYS_PERM_R) ||
      !sys_mapped(vals, (size_t)n_names * esz, perm))
    return false;
  size_t len = strlen(name) + 1u;
  for (uint32_t i = 0; i < n_names; i++) {
    uintptr_t s = sys_ptr(names + (size_t)i * 8u);
    if (s && sys_mapped(s, len, SYS_PERM_R) && bytes_equal((const void *)s, name, len)) {
      *val_addr = vals + (size_t)i * esz;
      return true;
    }
  }
  return false;
}

static bool bb_read_s32(uintptr_t bb, const char *name, int32_t *out)
{
  uintptr_t p = 0;
  if (!bb_find_named(bb, 0, name, &p, false))
    return false;
  memcpy(out, (const void *)p, 4);
  return true;
}

static bool bb_write_s32(uintptr_t bb, const char *name, int32_t val)
{
  uintptr_t p = 0;
  if (!bb_find_named(bb, 0, name, &p, true))
    return false;
  memcpy((void *)p, &val, 4);
  return true;
}

static bool bb_read_bool(uintptr_t bb, const char *name, uint8_t *out)
{
  uintptr_t p = 0;
  if (!bb_find_named(bb, 2, name, &p, false))
    return false;
  *out = *(const uint8_t *)p ? 1u : 0u;
  return true;
}

static bool bb_write_bool(uintptr_t bb, const char *name, uint8_t val)
{
  uintptr_t p = 0;
  if (!bb_find_named(bb, 2, name, &p, true))
    return false;
  *(volatile uint8_t *)p = val ? 1u : 0u;
  return true;
}

static bool capture_actor_bb(memfile_t *st, memfile_actor_t *e, uintptr_t actor, uint8_t *pool, uint32_t *pool_at)
{
  uintptr_t bb = actor_blackboard(actor);
  uint32_t got = 0;
  if (!bb || *pool_at >= MEMFILE_BB_POOL_MAX ||
      !capture_bb_blob(bb, pool + *pool_at, MEMFILE_BB_POOL_MAX - *pool_at, &got))
    return false;
  e->bb_off = *pool_at;
  e->bb_len = got;
  *pool_at += got;
  e->flags |= ACTOR_F_BB;
  (void)st;
  return true;
}

static bool restore_actor_bb(const memfile_t *st, const memfile_actor_t *e, uintptr_t actor)
{
  if (!(e->flags & ACTOR_F_BB) || !st->actor_bb || e->bb_len < BB_TABLES_N * sizeof(uint16_t) ||
      e->bb_off > st->actor_bb_size || e->bb_len > st->actor_bb_size - e->bb_off)
    return false;
  return restore_bb_blob(actor_blackboard(actor), st->actor_bb + e->bb_off, e->bb_len);
}

/* --- scene actors: transform, HP, Blackboard and dead/alive state (1.0.0 & 1.1.1) -------------- */

#define AMGR_TREE_ROOT       0x38u
#define ANODE_LEFT           0x00u
#define ANODE_RIGHT          0x08u
#define AENTRY_NODE          0x08u
#define AENTRY_ACTOR         0x28u
#define AENTRY_NEXT          0x38u
#define AENTRY_SIZE          0x40u
#define A_NAME               0x20u
#define A_STATE              0x2Cu
#define A_COMPONENTS         0x270u
#define A_POS                0x31Cu
#define A_ROT                0x328u
#define A_FORCE_POS          0x358u
#define A_PREACTOR           0x3D8u
#define A_FLAGS              0x4A8u
#define A_HOME_POS           0x2ECu
#define A_HOME_FLAGS         0x519u
#define PREACTOR_INSTANCE_ID 0x88u
#define ACOMP_VIS            0x10u
#define ACOMP_PHYSICS        0x50u
#define ACOMP_BSA            0x188u
#define ACOMP_MODEL          0x260u
#define ACOMP_LIFE           0x2B8u
#define ACOMP_LIFTABLE       0x2C0u
#define ACOMP_LIFTER         0x2C8u
#define ALIFE_CUR            0x1C10u
#define ALIFE_MAX            0x1C18u
#define APARAM_VAL           0x08u
#define LIFTER_TARGET_LINK   0x1B0u /* inner ActorLink inside lifter+0x1A8 holder (entry +0x1B8, gen +0x1C0) */
#define LIFTER_STATE         0x2B0u
#define PLAYER_LIFT_FLAGS    0xE40u
#define BSA_CUR_VERB         0x54u
#define BSA_ACTION_HOLDER    0x1B0u
#define BSA_ACTION_SLOTS     0x1C0u
#define BSA_SLOT_FLAGS       0x7Cu
#define ACTION_VERB_LIFT     0x2Bu /* Lift (weapon/sub slot 1 -> Player.sub.action.lift / ExecutePlayerLift) */
#define ACTION_VERB_DROWN    0x9Du /* InterruptDrown (main slot 0) */
#define MODEL_ALPHA          0x80u
#define MODEL_ALPHA_PREV     0x84u
#define MODEL_FADE_STEP      0x11Cu
#define MODEL_DIRTY_FLAGS    0x120u
#define MODEL_FADE_FLAGS     0x128u
#define MODEL_FADE_ACTIVE    0x134u
#define VIS_FADE_FLAGS       0xB0u
#define ATREE_MAX_DEPTH      64
#define ATREE_MAX_VISITS     4096

/* Kokko.action.interruptdrown.module SelectorGenericFrameCounter node indices (FINDINGS 9.47). */
enum {
  CUCCO_NODE_IN_WATER = 35,
  CUCCO_NODE_LOOK     = 125,
  CUCCO_NODE_JUMP     = 77,
  CUCCO_NODE_JUMP_AIR = 119,
  CUCCO_NODE_SINK     = 14,
  CUCCO_NODE_FADE_IN  = 18,
  CUCCO_NODE_LAND     = 31,
};

typedef bool (*actor_force_pos_t)(uintptr_t physics_comp, const float pos[3], const float rot[9], bool immediate);
typedef void (*actor_req_del_t)(uintptr_t actor, uint32_t reason);
typedef void (*preactor_update_t)(void *node);
typedef void (*actor_link_set_t)(void *link, uintptr_t actor, uint16_t flags);
typedef void (*actor_link_dtor_t)(void *link);
typedef bool (*lifter_drop_t)(uintptr_t lifter, int reason, float s0, float s1);
typedef bool (*lifter_force_lift_t)(uintptr_t lifter, void *link, const char *const *key, bool flag);
typedef void (*player_lift_anim_t)(uintptr_t link_actor, void *link);
typedef int  (*bsa_set_verb_t)(uintptr_t bsa, const void *req, bool flag);

/* Call SeqExecuteUpdatePreActorFlag's update with all five inputs true: resyncs GameData-backed dead flags from
 * the restored GameData, clears in-memory PreActor dead flags (so defeated enemies like Beamos respawn on fast
 * ResetGimmick), and clears DisableUntilUntraverse so nearby actors respawn immediately. */
static void reset_preactor_flags(void)
{
  if (!z64hd_game || !z64hd_game->preactor_update_flags)
    return;
  uint8_t yes = 1;
  uint64_t node[10] = { 0 };
  node[4] = (uint64_t)(uintptr_t)&yes; /* +0x20: UpdateStaticPreActor */
  node[5] = (uint64_t)(uintptr_t)&yes; /* +0x28: UpdateDynamicPreActor */
  node[6] = (uint64_t)(uintptr_t)&yes; /* +0x30: UpdateDeadFlag */
  node[7] = (uint64_t)(uintptr_t)&yes; /* +0x38: ResetPreActorDeadFlag */
  node[8] = (uint64_t)(uintptr_t)&yes; /* +0x40: ResetDisableUntilUntraverse */
  MAIN_FN(preactor_update_t, z64hd_game->preactor_update_flags)(node);
}

static bool starts_with(const char *s, const char *prefix)
{
  for (; *prefix; s++, prefix++)
    if (*s != *prefix)
      return false;
  return true;
}

static bool finite_vec3(const float v[3])
{
  for (int i = 0; i < 3; i++)
    if (!(v[i] > -100000.0f && v[i] < 100000.0f))
      return false;
  return true;
}

static bool finite_mat33(const float m[9])
{
  for (int i = 0; i < 9; i++)
    if (!(m[i] > -10.0f && m[i] < 10.0f))
      return false;
  return true;
}

static uint32_t fnv1a_str(const char *s)
{
  uint32_t h = 2166136261u;
  for (int i = 0; i < 64 && s[i]; i++)
    h = (h ^ (uint8_t)s[i]) * 16777619u;
  return h;
}

static bool actor_preactor_id(uintptr_t actor, uint64_t *id_out)
{
  if (!sys_mapped(actor + A_PREACTOR, 8, SYS_PERM_R))
    return false;
  uintptr_t pre = sys_ptr(actor + A_PREACTOR);
  if (!pre || (pre & 7u) || !sys_mapped(pre + PREACTOR_INSTANCE_ID, 8, SYS_PERM_R))
    return false;
  uint64_t id = 0;
  memcpy(&id, (const void *)(pre + PREACTOR_INSTANCE_ID), sizeof(id));
  if (!id || id == UINT64_MAX || id == 0xFFFFFFFFull)
    return false;
  *id_out = id;
  return true;
}

static bool actor_life_ptrs(uintptr_t actor, int32_t **cur_out, int32_t **max_out, bool write)
{
  if (!sys_mapped(actor + A_COMPONENTS, 8, SYS_PERM_R))
    return false;
  uintptr_t comps = sys_ptr(actor + A_COMPONENTS);
  if (!comps || (comps & 7u) || !sys_mapped(comps + ACOMP_LIFE, 8, SYS_PERM_R))
    return false;
  uintptr_t life = sys_ptr(comps + ACOMP_LIFE);
  if (!life || (life & 7u) || !sys_mapped(life, ALIFE_MAX + 8, SYS_PERM_R))
    return false;
  uintptr_t cur = sys_ptr(life + ALIFE_CUR);
  uintptr_t top = sys_ptr(life + ALIFE_MAX);
  uint32_t perm = write ? (SYS_PERM_R | SYS_PERM_W) : SYS_PERM_R;
  if (!cur || (cur & 7u) || !top || (top & 7u) || !sys_mapped(cur, APARAM_VAL + 4, perm) ||
      !sys_mapped(top, APARAM_VAL + 4, SYS_PERM_R))
    return false;
  uintptr_t vt_cur = sys_ptr(cur), vt_top = sys_ptr(top);
  if (vt_cur < z64hd_main || vt_cur - z64hd_main >= MAIN_VTABLE_MAX || vt_top < z64hd_main ||
      vt_top - z64hd_main >= MAIN_VTABLE_MAX)
    return false;
  *cur_out = (int32_t *)(cur + APARAM_VAL);
  *max_out = (int32_t *)(top + APARAM_VAL);
  return true;
}

static uintptr_t actor_physics_comp(uintptr_t actor)
{
  if (!sys_mapped(actor + A_COMPONENTS, 8, SYS_PERM_R))
    return 0;
  uintptr_t comps = sys_ptr(actor + A_COMPONENTS);
  if (!comps || (comps & 7u) || !sys_mapped(comps + ACOMP_PHYSICS, 8, SYS_PERM_R))
    return 0;
  uintptr_t phys = sys_ptr(comps + ACOMP_PHYSICS);
  return phys && !(phys & 7u) && sys_mapped(phys, 0x40, SYS_PERM_R) ? phys : 0;
}

/* --- Cucco drown / fade / respawn state & BSA verb helper (1.0.0 & 1.1.1) ---------------------- */

static bool is_cucco_name(const char *name)
{
  return name && (starts_with(name, "Animal_Cucco") || starts_with(name, "Animal_SuperCucco"));
}

static bool starts_ci(const char *s, const char *w)
{
  for (; *w; s++, w++) {
    char a = *s >= 'A' && *s <= 'Z' ? (char)(*s + 32) : *s;
    char b = *w >= 'A' && *w <= 'Z' ? (char)(*w + 32) : *w;
    if (a != b)
      return false;
  }
  return true;
}

static uintptr_t actor_home_pos_addr(uintptr_t actor, bool write)
{
  uint32_t perm = write ? (SYS_PERM_R | SYS_PERM_W) : SYS_PERM_R;
  if (!sys_mapped(actor + A_HOME_POS, 12, perm) || !sys_mapped(actor + A_HOME_FLAGS, 1, SYS_PERM_R))
    return 0;
  uintptr_t at = actor + A_HOME_POS;
  if (*(const uint8_t *)(actor + A_HOME_FLAGS) & 2u) {
    uintptr_t phys = actor_physics_comp(actor);
    uintptr_t a = phys && sys_mapped(phys + 0x20, 8, SYS_PERM_R) ? sys_ptr(phys + 0x20) : 0;
    uintptr_t b = a && !(a & 7u) && sys_mapped(a + 0x180, 8, SYS_PERM_R) ? sys_ptr(a + 0x180) : 0;
    if (b && !(b & 7u) && sys_mapped(b + 0x10, 12, perm))
      at = b + 0x10;
  }
  return at;
}

static bool read_home_pos(uintptr_t actor, float out[3])
{
  uintptr_t at = actor_home_pos_addr(actor, false);
  if (!at)
    return false;
  memcpy(out, (const void *)at, 12);
  return finite_vec3(out);
}

static bool write_home_pos(uintptr_t actor, const float in[3])
{
  if (!finite_vec3(in))
    return false;
  uintptr_t at = actor_home_pos_addr(actor, true);
  if (!at)
    return false;
  memcpy((void *)at, in, 12);
  return true;
}

static uintptr_t actor_bsa_comp(uintptr_t actor)
{
  if (!sys_mapped(actor + A_COMPONENTS, 8, SYS_PERM_R))
    return 0;
  uintptr_t comps = sys_ptr(actor + A_COMPONENTS);
  if (!comps || (comps & 7u) || !sys_mapped(comps + ACOMP_BSA, 8, SYS_PERM_R))
    return 0;
  uintptr_t bsa = sys_ptr(comps + ACOMP_BSA);
  return bsa && !(bsa & 7u) && sys_mapped(bsa, 0x200, SYS_PERM_R) ? bsa : 0;
}

/* Send a 0x60-byte BSAComponent::setActionVerb request (FINDINGS 9.22). Returns 0 on success (3 = slot busy). */
static int bsa_request_verb(uintptr_t bsa, uint32_t verb, uint32_t slot, uint16_t flags)
{
  if (!bsa || !z64hd_game || !z64hd_game->bsa_set_action_verb)
    return -1;
  /* Clear slot->flags bit 1 ("already requested this frame") so a request sent after the scheduler ran isn't dropped. */
  if (slot <= 4u && sys_mapped(bsa + BSA_ACTION_SLOTS + (size_t)slot * 8u, 8, SYS_PERM_R)) {
    uintptr_t slot_obj = sys_ptr(bsa + BSA_ACTION_SLOTS + (size_t)slot * 8u);
    if (slot_obj && !(slot_obj & 7u) && sys_mapped(slot_obj + BSA_SLOT_FLAGS, 1, SYS_PERM_R | SYS_PERM_W))
      *(volatile uint8_t *)(slot_obj + BSA_SLOT_FLAGS) &= (uint8_t)~2u;
  }
  uint8_t req[0x60];
  memset(req, 0, sizeof(req));
  uint32_t qnan = 0x7FC00000u, four = 4u, c11a = 0x11Au, c10 = 0x10u;
  memcpy(req + 0x00, &qnan, 4);
  memcpy(req + 0x0C, &qnan, 4);
  memcpy(req + 0x18, &qnan, 4);
  memcpy(req + 0x24, &four, 4);
  memcpy(req + 0x28, &c11a, 4);
  memcpy(req + 0x2C, &qnan, 4);
  memcpy(req + 0x30, &qnan, 4);
  memcpy(req + 0x34, &c10, 4);
  memcpy(req + 0x48, &slot, 4);
  memcpy(req + 0x4C, &verb, 4);
  memcpy(req + 0x50, &flags, 2);
  return MAIN_FN(bsa_set_verb_t, z64hd_game->bsa_set_action_verb)(bsa, req, true);
}

static uintptr_t cucco_drown_unit(uintptr_t actor, uintptr_t *ctrl_out)
{
  if (ctrl_out)
    *ctrl_out = 0;
  uintptr_t bsa = actor_bsa_comp(actor);
  if (!bsa)
    return 0;
  uintptr_t holder = sys_ptr(bsa + BSA_ACTION_HOLDER);
  uintptr_t ctrl = holder && !(holder & 7u) && sys_mapped(holder, 0x88, SYS_PERM_R) ? sys_ptr(holder + 0x80) : 0;
  if (!ctrl || (ctrl & 7u) || !sys_mapped(ctrl, 0x200, SYS_PERM_R) || sys_ptr(ctrl + 0x1F0) != actor)
    return 0;
  uint32_t n_defs = 0;
  if (!sys_read32(ctrl + 0x168, &n_defs) || !n_defs || n_defs > 0x15Eu)
    return 0;
  uintptr_t defs = sys_ptr(ctrl + 0x170);
  if (!defs || (defs & 7u) || !sys_mapped(defs, (size_t)n_defs * 8u, SYS_PERM_R))
    return 0;
  uintptr_t def = 0;
  for (uint32_t i = 0; i < n_defs; i++) {
    uintptr_t d = sys_ptr(defs + (size_t)i * 8u);
    uintptr_t file = d && !(d & 7u) && sys_mapped(d, 0x18, SYS_PERM_R) ? sys_ptr(d + 0x10) : 0;
    if (!file || !sys_mapped(file, 0x80, SYS_PERM_R) || !bytes_equal((const void *)file, "AIB ", 4))
      continue;
    uint32_t pool = *(const uint32_t *)(file + 0x24);
    if (pool < 0x80u || pool > 0x1000000u || !sys_mapped(file + pool, 40, SYS_PERM_R))
      continue;
    if (starts_ci((const char *)(file + pool), "Kokko.action.interruptdrown.module")) {
      def = d;
      break;
    }
  }
  if (!def)
    return 0;
  uint32_t n_units = 0;
  if (!sys_read32(ctrl + 0x18, &n_units) || !n_units || n_units > 64u)
    return 0;
  uintptr_t units = sys_ptr(ctrl + 0x20);
  if (!units || (units & 7u) || !sys_mapped(units, (size_t)n_units * 0x18u, SYS_PERM_R))
    return 0;
  for (uint32_t j = 0; j < n_units; j++) {
    uintptr_t unit = sys_ptr(units + (size_t)j * 0x18u);
    if (unit && !(unit & 7u) && sys_mapped(unit, 0x90, SYS_PERM_R) && sys_ptr(unit + 0x10) == def) {
      if (ctrl_out)
        *ctrl_out = ctrl;
      return unit;
    }
  }
  return 0;
}

static uintptr_t unit_counter_node(uintptr_t unit, uint16_t idx)
{
  if (!unit || !z64hd_game || !z64hd_game->frame_counter_node_vtable)
    return 0;
  uint64_t vt = MAIN(z64hd_game->frame_counter_node_vtable);
  uint32_t n_nodes = 0;
  if (sys_read32(unit + 0x60, &n_nodes) && idx < n_nodes && n_nodes <= 512u) {
    uintptr_t arr = sys_ptr(unit + 0x68);
    if (arr && !(arr & 7u) && sys_mapped(arr + (size_t)idx * 8u, 8, SYS_PERM_R)) {
      uintptr_t node = sys_ptr(arr + (size_t)idx * 8u);
      if (node && !(node & 7u) && sys_mapped(node, 0x60, SYS_PERM_R | SYS_PERM_W) &&
          sys_ptr(node) == vt && sys_ptr(node + 8) == unit && *(const uint16_t *)(node + 0x1C) == idx)
        return node;
    }
  }
  /* Fallback scan immediately after the unit (as drownview.c does). */
  size_t span = 0x8000u;
  while (span >= 0x1000u && !sys_mapped(unit, span, SYS_PERM_R))
    span /= 2u;
  if (span < 0x1000u)
    return 0;
  for (const uint64_t *w = (const uint64_t *)unit; (uintptr_t)w < unit + span; w++) {
    if (*w != vt)
      continue;
    uintptr_t node = (uintptr_t)w;
    if (sys_mapped(node, 0x60, SYS_PERM_R | SYS_PERM_W) && sys_ptr(node + 8) == unit &&
        *(const uint16_t *)(node + 0x1C) == idx)
      return node;
  }
  return 0;
}

static bool unit_node_counting(uintptr_t unit, uintptr_t node, uint16_t idx)
{
  if (!unit || !node || !sys_mapped(node, 0x60, SYS_PERM_R))
    return false;
  uint32_t bits = 0;
  if (sys_read32(unit + 0x80, &bits) && idx < bits && bits <= 512u) {
    uintptr_t bmp = sys_ptr(unit + 0x88);
    if (bmp && !(bmp & 3u) && sys_mapped(bmp + ((size_t)(idx >> 5) * 4u), 4, SYS_PERM_R)) {
      uint32_t word = 0;
      memcpy(&word, (const void *)(bmp + ((size_t)(idx >> 5) * 4u)), 4);
      if (!(word & (1u << (idx & 31u))))
        return false;
    }
  }
  uint32_t nflags = *(const uint32_t *)(node + 0x18);
  if (!(nflags & 2u))
    return false;
  return *(const uint8_t *)(node + 0x1E) == *(const uint8_t *)(node + 0x58);
}

static uintptr_t actor_model_comp(uintptr_t actor, bool write)
{
  if (!sys_mapped(actor + A_COMPONENTS, 8, SYS_PERM_R))
    return 0;
  uintptr_t comps = sys_ptr(actor + A_COMPONENTS);
  if (!comps || (comps & 7u) || !sys_mapped(comps + ACOMP_MODEL, 8, SYS_PERM_R))
    return 0;
  uintptr_t model = sys_ptr(comps + ACOMP_MODEL);
  uint32_t perm = write ? (SYS_PERM_R | SYS_PERM_W) : SYS_PERM_R;
  return model && !(model & 7u) && sys_mapped(model, 0x140, perm) ? model : 0;
}

static uintptr_t actor_vis_flags_addr(uintptr_t actor, bool write)
{
  if (!sys_mapped(actor + A_COMPONENTS, 8, SYS_PERM_R))
    return 0;
  uintptr_t comps = sys_ptr(actor + A_COMPONENTS);
  if (!comps || (comps & 7u) || !sys_mapped(comps + ACOMP_VIS, 8, SYS_PERM_R))
    return 0;
  uintptr_t vis = sys_ptr(comps + ACOMP_VIS);
  uint32_t perm = write ? (SYS_PERM_R | SYS_PERM_W) : SYS_PERM_R;
  return vis && !(vis & 7u) && sys_mapped(vis + VIS_FADE_FLAGS, 1, perm) ? vis + VIS_FADE_FLAGS : 0;
}

static void capture_model_fade(uintptr_t actor, memfile_cucco_t *c)
{
  uintptr_t model = actor_model_comp(actor, false);
  if (!model)
    return;
  float alpha = 1.0f, step = 0.0f;
  uint32_t fflags = 0;
  memcpy(&alpha, (const void *)(model + MODEL_ALPHA), 4);
  memcpy(&step, (const void *)(model + MODEL_FADE_STEP), 4);
  memcpy(&fflags, (const void *)(model + MODEL_FADE_FLAGS), 4);
  uint8_t active = *(const uint8_t *)(model + MODEL_FADE_ACTIVE);
  uintptr_t vis_p = actor_vis_flags_addr(actor, false);
  uint8_t vflags = vis_p ? *(const uint8_t *)vis_p : 0;
  if (!(alpha >= 0.0f && alpha <= 1.0f))
    return;
  if (alpha < 0.999f || (fflags & 0x0Cu) || active || (vflags & 0x34u)) {
    c->flags |= CUCCO_F_FADE;
    c->model_alpha = alpha;
    c->model_fade_step = (step > 0.0f && step <= 1.0f) ? step : (1.0f / 6.0f);
    c->model_fade_flags = fflags;
    c->model_fade_active = active;
    c->model_vis_flags = vflags;
  }
}

static void restore_model_fade(uintptr_t actor, const memfile_cucco_t *c)
{
  if (!(c->flags & CUCCO_F_FADE))
    return;
  uintptr_t model = actor_model_comp(actor, true);
  if (!model)
    return;
  float alpha = c->model_alpha;
  if (!(alpha >= 0.0f && alpha <= 1.0f))
    return;
  float step = (c->model_fade_step > 0.0f && c->model_fade_step <= 1.0f) ? c->model_fade_step : (1.0f / 6.0f);
  memcpy((void *)(model + MODEL_ALPHA), &alpha, 4);
  memcpy((void *)(model + MODEL_ALPHA_PREV), &alpha, 4);
  memcpy((void *)(model + MODEL_FADE_STEP), &step, 4);
  uint32_t fflags = 0, dflags = 0;
  memcpy(&fflags, (const void *)(model + MODEL_FADE_FLAGS), 4);
  fflags = (fflags & ~0x40CCu) | (c->model_fade_flags & 0x40CCu);
  memcpy((void *)(model + MODEL_FADE_FLAGS), &fflags, 4);
  memcpy(&dflags, (const void *)(model + MODEL_DIRTY_FLAGS), 4);
  dflags |= 0x2500u;
  memcpy((void *)(model + MODEL_DIRTY_FLAGS), &dflags, 4);
  *(volatile uint8_t *)(model + MODEL_FADE_ACTIVE) = c->model_fade_active;
  uintptr_t vis_p = actor_vis_flags_addr(actor, true);
  if (vis_p) {
    uint8_t cur = *(const uint8_t *)vis_p;
    *(volatile uint8_t *)vis_p = (uint8_t)((cur & ~0x34u) | (c->model_vis_flags & 0x34u));
  }
}

static bool capture_cucco_state(uintptr_t actor, uint64_t instance_id, uint32_t name_hash, memfile_cucco_t *out)
{
  *out = (memfile_cucco_t){ 0 };
  out->instance_id = instance_id;
  out->name_hash = name_hash;
  out->active_node = 0xFFFFu;
  out->air_val = -1.0f;
  if (read_home_pos(actor, out->home_pos))
    out->flags |= CUCCO_F_HOME;
  capture_model_fade(actor, out);

  uintptr_t ctrl = 0;
  uintptr_t unit = cucco_drown_unit(actor, &ctrl);
  if (unit && ctrl) {
    static const uint16_t order[] = {
      CUCCO_NODE_SINK, CUCCO_NODE_FADE_IN, CUCCO_NODE_LAND,
      CUCCO_NODE_JUMP, CUCCO_NODE_LOOK, CUCCO_NODE_IN_WATER,
    };
    uintptr_t bsa = actor_bsa_comp(actor);
    uint32_t cur_verb = 0;
    bool in_drown = bsa && sys_read32(bsa + BSA_CUR_VERB, &cur_verb) && cur_verb == ACTION_VERB_DROWN;
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
      uintptr_t n = unit_counter_node(unit, order[i]);
      if (n && unit_node_counting(unit, n, order[i])) {
        float val = 0;
        memcpy(&val, (const void *)(n + 0x20), 4);
        if (val == val && val >= 0.0f && val < 10000.0f) {
          out->active_node = order[i];
          out->counter_val = val;
          out->flags |= CUCCO_F_DROWN;
          break;
        }
      }
    }
    if (in_drown && !(out->flags & CUCCO_F_DROWN))
      out->flags |= CUCCO_F_DROWN;
    if (out->active_node == CUCCO_NODE_JUMP) {
      uintptr_t air = unit_counter_node(unit, CUCCO_NODE_JUMP_AIR);
      if (air && unit_node_counting(unit, air, CUCCO_NODE_JUMP_AIR)) {
        float aval = 0;
        memcpy(&aval, (const void *)(air + 0x20), 4);
        if (aval == aval && aval >= 0.0f && aval < 1000.0f)
          out->air_val = aval;
      }
    }
    uintptr_t ctrl_bb = sys_ptr(ctrl + 0x1F8);
    if (ctrl_bb && !(ctrl_bb & 7u) && sys_mapped(ctrl_bb, BB_STRUCT_LEN, SYS_PERM_R)) {
      int32_t lwc = 0;
      if (bb_read_s32(ctrl_bb, "LandingWaterCount", &lwc))
        out->landing_water_count = lwc;
    }
  }
  if (out->flags & (CUCCO_F_DROWN | CUCCO_F_FADE))
    log_printf("memfile save: cucco id=%#lx flags=%#x node=%u ctr=%.1f lwc=%d alpha=%.2f fflags=%#x fact=%u vflags=%#x", /* not menu text */
               (unsigned long)instance_id, (unsigned)out->flags, (unsigned)out->active_node, (double)out->counter_val,
               (int)out->landing_water_count, (double)out->model_alpha, (unsigned)out->model_fade_flags,
               (unsigned)out->model_fade_active, (unsigned)out->model_vis_flags);
  return out->flags != 0;
}

/* Post-load asynchronous restore jobs: Cucco drown/fade/respawn timer fast-forward + held Actor BB re-apply. */
#define DROWN_JOBS_MAX 8

typedef struct {
  bool            active;
  uintptr_t       actor;
  memfile_cucco_t cucco;
  int             frames;
  bool            lwc_set;
} drown_job_t;

typedef struct {
  bool      active;
  uintptr_t actor;
  int       frames;
  uint32_t  last_state;
  uint32_t  last_verb;
  uint8_t   last_force_ret;
  bool      logged_fade;
  float     last_pos[3];
} cucco_watch_t;

static drown_job_t   drown_jobs[DROWN_JOBS_MAX];
static cucco_watch_t cucco_watches[CUCCO_WATCH_MAX];
static uintptr_t     held_reapply_actor;
static int           held_reapply_frames;
static int           held_reapply_slot;
static uintptr_t     held_watch_actor;
static int           held_watch_frames;
static uint32_t      held_watch_lstate;
static uint32_t      held_watch_astate;
static uint32_t      held_watch_verb;
static uintptr_t     held_spawn_handle;
static uintptr_t     held_target_actor;
static int           held_settle_frames;
static bool          held_spawn_tried;

static void clear_post_load_jobs(void)
{
  memset(drown_jobs, 0, sizeof(drown_jobs));
  memset(cucco_watches, 0, sizeof(cucco_watches));
  held_reapply_actor = 0;
  held_reapply_frames = 0;
  held_watch_actor = 0;
  held_watch_frames = 0;
  held_target_actor = 0;
  held_settle_frames = 0;
  held_spawn_tried = false;
  if (held_spawn_handle)
    actor_spawn_cancel(&held_spawn_handle);
}

static float actor_model_alpha(uintptr_t actor)
{
  uintptr_t model = actor_model_comp(actor, false);
  if (!model)
    return 1.0f;
  float a = 1.0f;
  memcpy(&a, (const void *)(model + MODEL_ALPHA), 4);
  return a;
}

static uint32_t actor_cur_verb(uintptr_t actor)
{
  uintptr_t bsa = actor_bsa_comp(actor);
  uint32_t v = 0;
  return bsa && sys_read32(bsa + BSA_CUR_VERB, &v) ? v : 0u;
}

static void watch_nearby_cucco(uintptr_t actor, const memfile_t *st)
{
  if (!actor || !st || !st->have_pos || !sys_mapped(actor + A_POS, 12, SYS_PERM_R))
    return;
  float p[3];
  memcpy(p, (const void *)(actor + A_POS), 12);
  float dx = p[0] - (float)st->x, dy = p[1] - (float)st->y, dz = p[2] - (float)st->z;
  if (dx * dx + dy * dy + dz * dz > 15.0f * 15.0f)
    return;
  for (int i = 0; i < CUCCO_WATCH_MAX; i++)
    if (cucco_watches[i].active && cucco_watches[i].actor == actor)
      return;
  for (int i = 0; i < CUCCO_WATCH_MAX; i++) {
    if (!cucco_watches[i].active) {
      uint32_t astate = *(const uint32_t *)(actor + A_STATE);
      uint32_t verb = actor_cur_verb(actor);
      uint8_t fr = 0;
      bb_read_bool(actor_own_blackboard(actor), "DoForceReturnHome", &fr);
      float alpha = actor_model_alpha(actor);
      cucco_watches[i] = (cucco_watch_t){
        .active = true,
        .actor = actor,
        .frames = 0,
        .last_state = astate,
        .last_verb = verb,
        .last_force_ret = fr,
        .logged_fade = alpha < 0.95f,
        .last_pos = { p[0], p[1], p[2] },
      };
      log_printf("memfiles: watch cucco %#lx pos=%.1f,%.1f,%.1f state=%u verb=%#x alpha=%.2f force_ret=%u", /* not menu text */
                 (unsigned long)actor, (double)p[0], (double)p[1], (double)p[2], (unsigned)astate, (unsigned)verb,
                 (double)alpha, (unsigned)fr);
      return;
    }
  }
}

static void queue_drown_job(uintptr_t actor, const memfile_cucco_t *c)
{
  if (!actor || !c || !(c->flags & CUCCO_F_DROWN))
    return;
  for (int i = 0; i < DROWN_JOBS_MAX; i++)
    if (drown_jobs[i].active && drown_jobs[i].actor == actor)
      return;
  for (int i = 0; i < DROWN_JOBS_MAX; i++) {
    if (!drown_jobs[i].active) {
      drown_jobs[i] = (drown_job_t){ .active = true, .actor = actor, .cucco = *c, .frames = 0, .lwc_set = false };
      uintptr_t bsa = actor_bsa_comp(actor);
      uint32_t cur_verb = 0;
      if (bsa && (!sys_read32(bsa + BSA_CUR_VERB, &cur_verb) || cur_verb != ACTION_VERB_DROWN))
        bsa_request_verb(bsa, ACTION_VERB_DROWN, 0, 0x101u);
      return;
    }
  }
}

static void set_counter_elapsed(uintptr_t node, float val)
{
  if (!node || !sys_mapped(node + 0x20, 4, SYS_PERM_R | SYS_PERM_W))
    return;
  memcpy((void *)(node + 0x20), &val, 4);
}

static void step_drown_job(drown_job_t *j)
{
  j->frames++;
  if (j->frames > DROWN_RESTORE_TIMEOUT || !sys_mapped(j->actor, A_PREACTOR + 8, SYS_PERM_R)) {
    j->active = false;
    return;
  }
  uint32_t state = *(const uint32_t *)(j->actor + A_STATE);
  if (state >= 7u) {
    j->active = false;
    return;
  }
  uintptr_t ctrl = 0;
  uintptr_t unit = cucco_drown_unit(j->actor, &ctrl);
  if (!unit || !ctrl)
    return;
  uintptr_t bsa = actor_bsa_comp(j->actor);
  uint32_t cur_verb = 0;
  bool in_drown = bsa && sys_read32(bsa + BSA_CUR_VERB, &cur_verb) && cur_verb == ACTION_VERB_DROWN;
  if (!in_drown) {
    if (j->frames <= 12)
      bsa_request_verb(bsa, ACTION_VERB_DROWN, 0, 0x101u);
    return;
  }

  uintptr_t ctrl_bb = sys_ptr(ctrl + 0x1F8);
  bool have_ctrl_bb = ctrl_bb && !(ctrl_bb & 7u) && sys_mapped(ctrl_bb, BB_STRUCT_LEN, SYS_PERM_R | SYS_PERM_W);
  uint16_t want = j->cucco.active_node;
  if (want == 0xFFFFu) {
    restore_model_fade(j->actor, &j->cucco);
    j->active = false;
    return;
  }

  /* Fast-forward through earlier stages cleanly using the engine's own transitions. */
  uintptr_t n35 = unit_counter_node(unit, CUCCO_NODE_IN_WATER);
  uintptr_t n125 = unit_counter_node(unit, CUCCO_NODE_LOOK);
  uintptr_t n77 = unit_counter_node(unit, CUCCO_NODE_JUMP);
  uintptr_t n14 = unit_counter_node(unit, CUCCO_NODE_SINK);
  uintptr_t n18 = unit_counter_node(unit, CUCCO_NODE_FADE_IN);
  uintptr_t n31 = unit_counter_node(unit, CUCCO_NODE_LAND);

  if (want == CUCCO_NODE_IN_WATER) {
    if (n35 && unit_node_counting(unit, n35, CUCCO_NODE_IN_WATER)) {
      set_counter_elapsed(n35, j->cucco.counter_val);
      j->active = false;
    }
    return;
  }
  if (want == CUCCO_NODE_LOOK) {
    if (n35 && unit_node_counting(unit, n35, CUCCO_NODE_IN_WATER))
      set_counter_elapsed(n35, 30.0f);
    if (n125 && unit_node_counting(unit, n125, CUCCO_NODE_LOOK)) {
      set_counter_elapsed(n125, j->cucco.counter_val);
      j->active = false;
    }
    return;
  }
  if (want == CUCCO_NODE_JUMP) {
    if (n35 && unit_node_counting(unit, n35, CUCCO_NODE_IN_WATER))
      set_counter_elapsed(n35, 30.0f);
    if (n125 && unit_node_counting(unit, n125, CUCCO_NODE_LOOK)) {
      if (have_ctrl_bb && !j->lwc_set) {
        bb_write_s32(ctrl_bb, "LandingWaterCount", j->cucco.landing_water_count);
        j->lwc_set = true;
      }
      set_counter_elapsed(n125, 120.0f);
    }
    if (n77 && unit_node_counting(unit, n77, CUCCO_NODE_JUMP)) {
      if (have_ctrl_bb && !j->lwc_set) {
        bb_write_s32(ctrl_bb, "LandingWaterCount", j->cucco.landing_water_count);
        j->lwc_set = true;
      }
      set_counter_elapsed(n77, j->cucco.counter_val);
      if (j->cucco.air_val >= 0.0f) {
        uintptr_t n119 = unit_counter_node(unit, CUCCO_NODE_JUMP_AIR);
        if (n119 && unit_node_counting(unit, n119, CUCCO_NODE_JUMP_AIR))
          set_counter_elapsed(n119, j->cucco.air_val);
      }
      j->active = false;
    }
    return;
  }

  /* SINK (14), FADE_IN (18), or LAND (31): skip jump loop via IsForceWarp / LandingWaterCount=4. */
  if (have_ctrl_bb) {
    bb_write_bool(ctrl_bb, "IsForceWarp", 1);
    bb_write_s32(ctrl_bb, "LandingWaterCount", 4);
  }
  if (n35 && unit_node_counting(unit, n35, CUCCO_NODE_IN_WATER))
    set_counter_elapsed(n35, 30.0f);
  if (n125 && unit_node_counting(unit, n125, CUCCO_NODE_LOOK))
    set_counter_elapsed(n125, 120.0f);
  if (n77 && unit_node_counting(unit, n77, CUCCO_NODE_JUMP))
    set_counter_elapsed(n77, 600.0f);

  if (want == CUCCO_NODE_SINK) {
    if (n14 && unit_node_counting(unit, n14, CUCCO_NODE_SINK)) {
      set_counter_elapsed(n14, j->cucco.counter_val);
      restore_model_fade(j->actor, &j->cucco);
      j->active = false;
    }
    return;
  }
  if (n14 && unit_node_counting(unit, n14, CUCCO_NODE_SINK))
    set_counter_elapsed(n14, 60.0f);
  if (want == CUCCO_NODE_FADE_IN) {
    if (n18 && unit_node_counting(unit, n18, CUCCO_NODE_FADE_IN)) {
      set_counter_elapsed(n18, j->cucco.counter_val);
      restore_model_fade(j->actor, &j->cucco);
      j->active = false;
    }
    return;
  }
  if (n18 && unit_node_counting(unit, n18, CUCCO_NODE_FADE_IN))
    set_counter_elapsed(n18, 10.0f);
  if (want == CUCCO_NODE_LAND && n31 && unit_node_counting(unit, n31, CUCCO_NODE_LAND)) {
    set_counter_elapsed(n31, j->cucco.counter_val);
    restore_model_fade(j->actor, &j->cucco);
    j->active = false;
  }
}

static uintptr_t link_lifter_comp(uintptr_t link);

static void step_post_load_jobs(void)
{
  bool loading = false;
  gd_get_bool(F_IS_LOADING_BANC, &loading);
  uintptr_t link = link_actor();
  if (loading || !link) {
    clear_post_load_jobs();
    return;
  }
  for (int i = 0; i < DROWN_JOBS_MAX; i++)
    if (drown_jobs[i].active)
      step_drown_job(&drown_jobs[i]);
  if (held_reapply_frames > 0 && held_reapply_actor) {
    held_reapply_frames--;
    if (held_reapply_slot >= 0 && held_reapply_slot < MEMFILE_SLOTS &&
        sys_mapped(held_reapply_actor, A_PREACTOR + 8, SYS_PERM_R) &&
        *(const uint32_t *)(held_reapply_actor + A_STATE) < 7u) {
      const memfile_held_t *h = &memfiles[held_reapply_slot].held;
      if (h->valid && h->actor_bb && h->actor_bb_size)
        restore_bb_blob(actor_own_blackboard(held_reapply_actor), h->actor_bb, h->actor_bb_size);
    } else {
      held_reapply_frames = 0;
    }
    if (held_reapply_frames <= 0)
      held_reapply_actor = 0;
  }
  if (held_watch_frames > 0 && held_watch_actor) {
    held_watch_frames--;
    int f = HELD_WATCH_FRAMES - held_watch_frames;
    uintptr_t lifter = link_lifter_comp(link);
    uint32_t lst = 99u;
    if (lifter)
      sys_read32(lifter + LIFTER_STATE, &lst);
    bool mapped = sys_mapped(held_watch_actor, A_PREACTOR + 8, SYS_PERM_R);
    uint32_t ast = mapped ? *(const uint32_t *)(held_watch_actor + A_STATE) : 99u;
    uint32_t verb = mapped ? actor_cur_verb(held_watch_actor) : 0u;
    if (lst != held_watch_lstate || ast != held_watch_astate || verb != held_watch_verb) {
      float p[3] = { 0, 0, 0 };
      if (mapped && sys_mapped(held_watch_actor + A_POS, 12, SYS_PERM_R))
        memcpy(p, (const void *)(held_watch_actor + A_POS), 12);
      float alpha = mapped ? actor_model_alpha(held_watch_actor) : -1.0f;
      log_printf("memfiles: held +%df target=%#lx lifter_st=%u->%u astate=%u->%u verb=%#x->%#x alpha=%.2f pos=%.1f,%.1f,%.1f", /* not menu text */
                 f, (unsigned long)held_watch_actor, (unsigned)held_watch_lstate, (unsigned)lst,
                 (unsigned)held_watch_astate, (unsigned)ast, (unsigned)held_watch_verb, (unsigned)verb,
                 (double)alpha, (double)p[0], (double)p[1], (double)p[2]);
      held_watch_lstate = lst;
      held_watch_astate = ast;
      held_watch_verb = verb;
    }
    if (!mapped || ast >= 7u || held_watch_frames <= 0)
      held_watch_actor = 0;
  }
  for (int i = 0; i < CUCCO_WATCH_MAX; i++) {
    cucco_watch_t *w = &cucco_watches[i];
    if (!w->active)
      continue;
    w->frames++;
    if (w->frames > CUCCO_WATCH_FRAMES || !sys_mapped(w->actor, A_PREACTOR + 8, SYS_PERM_R)) {
      if (w->frames <= CUCCO_WATCH_FRAMES)
        log_printf("memfiles: cucco +%df %#lx unmapped", w->frames, (unsigned long)w->actor); /* not menu text */
      w->active = false;
      continue;
    }
    uint32_t ast = *(const uint32_t *)(w->actor + A_STATE);
    uint32_t verb = actor_cur_verb(w->actor);
    uint8_t fr = 0;
    bb_read_bool(actor_own_blackboard(w->actor), "DoForceReturnHome", &fr);
    float alpha = actor_model_alpha(w->actor);
    float p[3] = { 0, 0, 0 };
    if (sys_mapped(w->actor + A_POS, 12, SYS_PERM_R))
      memcpy(p, (const void *)(w->actor + A_POS), 12);
    float dx = p[0] - w->last_pos[0], dy = p[1] - w->last_pos[1], dz = p[2] - w->last_pos[2];
    bool jumped = dx * dx + dy * dy + dz * dz > 25.0f;
    bool fade_now = alpha < 0.95f && !w->logged_fade;
    if (ast != w->last_state || verb != w->last_verb || fr != w->last_force_ret || fade_now || jumped) {
      log_printf("memfiles: cucco +%df %#lx state=%u->%u verb=%#x->%#x alpha=%.2f force_ret=%u pos=%.1f,%.1f,%.1f", /* not menu text */
                 w->frames, (unsigned long)w->actor, (unsigned)w->last_state, (unsigned)ast,
                 (unsigned)w->last_verb, (unsigned)verb, (double)alpha, (unsigned)fr,
                 (double)p[0], (double)p[1], (double)p[2]);
      w->last_state = ast;
      w->last_verb = verb;
      w->last_force_ret = fr;
      if (fade_now)
        w->logged_fade = true;
      memcpy(w->last_pos, p, 12);
    }
    if (ast >= 7u)
      w->active = false;
  }
}

/* Priority: 2 = Enemy/Npc (always kept first), 1 = Animal/Horse/Cucco or Life-bearing Obj/DgnObj/Gimmick, 0 = skip.
 * Never track static Obj_/DgnObj_ merely for having PhysicsComponent (dungeon walls/floors/doors all have one). */
static int actor_track_priority(const char *name, bool has_life, uint32_t state)
{
  if (starts_with(name, "Enemy_") || starts_with(name, "Npc_"))
    return 2;
  if (starts_with(name, "Animal_") || starts_with(name, "Horse") || starts_with(name, "Epona") ||
      starts_with(name, "Cucco"))
    return 1;
  if ((starts_with(name, "Obj_") || starts_with(name, "DgnObj_") || starts_with(name, "Gimmick_")) &&
      (has_life || state >= 7u))
    return 1;
  return 0;
}

typedef void (*actor_visit_fn)(uintptr_t actor, void *ctx);

static void walk_actor_tree(actor_visit_fn fn, void *ctx)
{
  if (!z64hd_game || !z64hd_game->actor_mgr_ptr)
    return;
  uintptr_t mgr = sys_ptr(MAIN(z64hd_game->actor_mgr_ptr));
  if (!mgr || (mgr & 7u) || !sys_mapped(mgr + AMGR_TREE_ROOT, 8, SYS_PERM_R))
    return;
  uintptr_t stack[ATREE_MAX_DEPTH];
  int depth = 0, visits = 0;
  uintptr_t node = sys_ptr(mgr + AMGR_TREE_ROOT);
  while ((node || depth) && visits < ATREE_MAX_VISITS) {
    while (node && depth < ATREE_MAX_DEPTH) {
      if ((node & 7u) || !sys_mapped(node, 16, SYS_PERM_R))
        return;
      stack[depth++] = node;
      node = sys_ptr(node + ANODE_LEFT);
    }
    if (node)
      return;
    node = stack[--depth];
    for (uintptr_t entry = node - AENTRY_NODE; entry && visits < ATREE_MAX_VISITS;
         entry = sys_ptr(entry + AENTRY_NEXT)) {
      visits++;
      if ((entry & 7u) || !sys_mapped(entry, AENTRY_SIZE, SYS_PERM_R))
        break;
      uintptr_t actor = sys_ptr(entry + AENTRY_ACTOR);
      if (actor && !(actor & 7u) && sys_mapped(actor, A_PREACTOR + 8, SYS_PERM_R))
        fn(actor, ctx);
    }
    node = sys_ptr(node + ANODE_RIGHT);
  }
}

#define SEEN_IDS_MAX 1024u

typedef struct {
  memfile_t       *st;
  memfile_actor_t *actors;
  memfile_cucco_t *cuccos;
  uint32_t         cucco_count;
  uintptr_t        link, cam_actor;
  uint8_t         *bb_pool;
  uint32_t         bb_at;
  uint8_t          prio[MEMFILE_ACTORS_MAX];
  uint64_t         seen_id[SEEN_IDS_MAX];
  uint8_t          seen_dup[SEEN_IDS_MAX];
  uint32_t         seen_count, dup_count;
} actor_cap_ctx_t;

static memfile_actor_t cap_actors[MEMFILE_ACTORS_MAX];
static memfile_cucco_t cap_cuccos[MEMFILE_CUCCOS_MAX];
static uint8_t         cap_bb_pool[MEMFILE_BB_POOL_MAX];
static actor_cap_ctx_t cap_actor_ctx;

/* Track every instance_id in the scene: if two distinct actors share an ID, poison it so neither is saved. */
static bool mark_id_unique(actor_cap_ctx_t *c, uint64_t id)
{
  for (uint32_t i = 0; i < c->seen_count; i++) {
    if (c->seen_id[i] == id) {
      if (!c->seen_dup[i]) {
        c->seen_dup[i] = 1;
        c->dup_count++;
        /* remove any already-captured entry with this colliding ID */
        for (uint32_t k = 0; k < c->st->actor_count; k++) {
          if (c->actors[k].instance_id == id) {
            uint32_t last = --c->st->actor_count;
            c->actors[k] = c->actors[last];
            c->prio[k] = c->prio[last];
            break;
          }
        }
      }
      return false;
    }
  }
  if (c->seen_count < SEEN_IDS_MAX) {
    c->seen_id[c->seen_count] = id;
    c->seen_dup[c->seen_count] = 0;
    c->seen_count++;
  }
  return true;
}

static void capture_actor_cb(uintptr_t actor, void *vctx)
{
  actor_cap_ctx_t *c = (actor_cap_ctx_t *)vctx;
  if (actor == c->link || actor == c->cam_actor)
    return;
  uint64_t id = 0;
  if (!actor_preactor_id(actor, &id))
    return;
  if (!mark_id_unique(c, id))
    return;
  uintptr_t name_p = sys_ptr(actor + A_NAME);
  if (!name_p || !sys_mapped(name_p, 16, SYS_PERM_R))
    return;
  const char *name = (const char *)name_p;
  uint32_t state = *(const uint32_t *)(actor + A_STATE);
  int32_t *cur_hp = 0, *max_hp = 0;
  bool has_life = actor_life_ptrs(actor, &cur_hp, &max_hp, false);
  int prio = actor_track_priority(name, has_life, state);
  if (!prio)
    return;

  uint32_t dst_idx = c->st->actor_count;
  if (dst_idx >= MEMFILE_ACTORS_MAX) {
    c->st->actors_truncated = true;
    if (prio < 2)
      return;
    uint32_t victim = MEMFILE_ACTORS_MAX;
    for (uint32_t i = 0; i < MEMFILE_ACTORS_MAX; i++)
      if (c->prio[i] < 2) {
        victim = i;
        break;
      }
    if (victim >= MEMFILE_ACTORS_MAX)
      return;
    dst_idx = victim;
  } else {
    c->st->actor_count++;
  }

  memfile_actor_t *e = &c->actors[dst_idx];
  *e = (memfile_actor_t){ 0 };
  e->instance_id = id;
  e->name_hash = fnv1a_str(name);
  memcpy(e->pos, (const void *)(actor + A_POS), sizeof(e->pos));
  memcpy(e->rot, (const void *)(actor + A_ROT), sizeof(e->rot));
  e->state = (uint8_t)(state > 255u ? 255u : state);
  e->flags = (uint8_t)((has_life ? ACTOR_F_LIFE : 0) | (state >= 7u ? ACTOR_F_DEAD : 0) |
                       (starts_with(name, "Enemy_") ? ACTOR_F_ENEMY : 0));
  if (has_life) {
    e->life = *cur_hp;
    e->max_life = *max_hp;
  }
  if (state < 7u && c->bb_pool)
    capture_actor_bb(c->st, e, actor, c->bb_pool, &c->bb_at);
  if (state < 7u && is_cucco_name(name) && c->cuccos && c->cucco_count < MEMFILE_CUCCOS_MAX) {
    memfile_cucco_t cs;
    if (capture_cucco_state(actor, id, e->name_hash, &cs))
      c->cuccos[c->cucco_count++] = cs;
  }
  c->prio[dst_idx] = (uint8_t)prio;
}

static bool capture_actors(memfile_t *st)
{
  if (!z64hd_game || !z64hd_game->actor_mgr_ptr)
    return true;
  uintptr_t mgr = sys_ptr(MAIN(z64hd_game->actor_mgr_ptr));
  if (!mgr || (mgr & 7u) || !sys_mapped(mgr + AMGR_TREE_ROOT, 8, SYS_PERM_R))
    return true;
  uintptr_t cam_actor = 0;
  resolve_camera(&cam_actor);
  cap_actor_ctx = (actor_cap_ctx_t){
    .st = st,
    .actors = cap_actors,
    .cuccos = cap_cuccos,
    .link = link_actor(),
    .cam_actor = cam_actor,
    .bb_pool = cap_bb_pool,
  };
  walk_actor_tree(capture_actor_cb, &cap_actor_ctx);
  if (cap_actor_ctx.dup_count)
    log_printf("memfiles: skipped %u colliding actor ID(s) during capture", (unsigned)cap_actor_ctx.dup_count); /* not menu text */
  uint32_t bb_used = cap_actor_ctx.bb_at;
  if (st->actor_count) {
    size_t bytes = (size_t)st->actor_count * sizeof(memfile_actor_t);
    st->actors = alloc16(bytes);
    if (!st->actors) {
      log_printf("memfile save: out of pool memory for %u actors (%u bytes, %u KiB free)", /* not menu text */
                 (unsigned)st->actor_count, (unsigned)bytes, (unsigned)(pool_free_bytes() >> 10));
      st->actor_count = 0;
      return false;
    }
    memcpy(st->actors, cap_actors, bytes);
  }
  if (bb_used) {
    st->actor_bb = alloc16(bb_used);
    if (!st->actor_bb) {
      log_printf("memfile save: out of pool memory for actor blackboard (%u bytes, %u KiB free)", /* not menu text */
                 (unsigned)bb_used, (unsigned)(pool_free_bytes() >> 10));
      return false;
    }
    memcpy(st->actor_bb, cap_bb_pool, bb_used);
    st->actor_bb_size = bb_used;
  }
  if (cap_actor_ctx.cucco_count) {
    size_t bytes = (size_t)cap_actor_ctx.cucco_count * sizeof(memfile_cucco_t);
    st->cuccos = alloc16(bytes);
    if (!st->cuccos) {
      log_printf("memfile save: out of pool memory for %u cuccos (%u bytes, %u KiB free)", /* not menu text */
                 (unsigned)cap_actor_ctx.cucco_count, (unsigned)bytes, (unsigned)(pool_free_bytes() >> 10));
      return false;
    }
    memcpy(st->cuccos, cap_cuccos, bytes);
    st->cucco_count = cap_actor_ctx.cucco_count;
  }
  st->have_actors = true;
  return true;
}

typedef struct {
  const memfile_t *st;
  uintptr_t        link, cam_actor;
  bool             delete_missing;
  uint8_t          used[MEMFILE_ACTORS_MAX];
  uint8_t         *bb_done;
  uint32_t         moved, hp_set, bb_set, deleted, missing_del;
} actor_rest_ctx_t;

static bool transform_differs(uintptr_t actor, const float pos[3], const float rot[9])
{
  const float *cp = (const float *)(actor + A_POS);
  for (int i = 0; i < 3; i++) {
    float d = cp[i] - pos[i];
    if (d > 0.01f || d < -0.01f)
      return true;
  }
  const float *cr = (const float *)(actor + A_ROT);
  for (int i = 0; i < 9; i++) {
    float d = cr[i] - rot[i];
    if (d > 0.001f || d < -0.001f)
      return true;
  }
  return false;
}

static void actor_apply_transform(uintptr_t actor, const float pos[3], const float rot[9])
{
  if (!finite_vec3(pos))
    return;
  bool ok_rot = finite_mat33(rot);
  bool phys_ok = false;
  uintptr_t phys = actor_physics_comp(actor);
  if (phys && z64hd_game->actor_force_set_pos) {
    float nan_rot = 0;
    uint32_t qnan = 0x7FC00000u;
    memcpy(&nan_rot, &qnan, 4);
    phys_ok = MAIN_FN(actor_force_pos_t, z64hd_game->actor_force_set_pos)(phys, pos, ok_rot ? rot : &nan_rot, true);
  }
  if (!phys_ok) {
    if (sys_mapped(actor + A_FORCE_POS, 12, SYS_PERM_R | SYS_PERM_W))
      memcpy((void *)(actor + A_FORCE_POS), pos, 12);
    if (sys_mapped(actor + A_FLAGS, 4, SYS_PERM_R | SYS_PERM_W))
      *(volatile uint32_t *)(actor + A_FLAGS) &= ~0x8000u;
  }
  /* Always sync A_POS/A_ROT immediately: PhysicsComponent::forceSetPosAndRot only writes the physics
   * body/CCT, leaving LOD/physics-paused distant actors (>50 m away) stuck at their old world pos so
   * LOD never wakes them up and L_HELD would bind them while still paused far away. */
  if (sys_mapped(actor + A_POS, 12, SYS_PERM_R | SYS_PERM_W))
    memcpy((void *)(actor + A_POS), pos, 12);
  if (ok_rot && sys_mapped(actor + A_ROT, 36, SYS_PERM_R | SYS_PERM_W))
    memcpy((void *)(actor + A_ROT), rot, 36);
}

static void restore_actor_cb(uintptr_t actor, void *vctx)
{
  actor_rest_ctx_t *c = (actor_rest_ctx_t *)vctx;
  if (actor == c->link || actor == c->cam_actor)
    return;
  uint64_t id = 0;
  if (!actor_preactor_id(actor, &id))
    return;
  uintptr_t name_p = sys_ptr(actor + A_NAME);
  if (!name_p || !sys_mapped(name_p, 16, SYS_PERM_R))
    return;
  const char *name = (const char *)name_p;
  uint32_t state = *(const uint32_t *)(actor + A_STATE);
  uint32_t nhash = fnv1a_str(name);

  int match_idx = -1;
  for (uint32_t i = 0; i < c->st->actor_count; i++) {
    if (c->st->actors[i].instance_id == id &&
        (!c->st->actors[i].name_hash || c->st->actors[i].name_hash == nhash)) {
      match_idx = (int)i;
      break;
    }
  }

  if (match_idx >= 0) {
    if (c->used[match_idx])
      return; /* 1-to-1 guard: never restore two live actors from one saved entry */
    int32_t *cur_hp = 0, *max_hp = 0;
    bool has_life = actor_life_ptrs(actor, &cur_hp, &max_hp, false);
    if (!actor_track_priority(name, has_life, state))
      return;
    c->used[match_idx] = 1;
    const memfile_actor_t *match = &c->st->actors[match_idx];
    if (match->flags & ACTOR_F_DEAD) {
      if (state < 7u && z64hd_game->actor_request_delete) {
        MAIN_FN(actor_req_del_t, z64hd_game->actor_request_delete)(actor, 0);
        c->deleted++;
      }
      return;
    }
    if (state >= 7u)
      return;
    if (transform_differs(actor, match->pos, match->rot)) {
      actor_apply_transform(actor, match->pos, match->rot);
      c->moved++;
    }
    if (match->flags & ACTOR_F_LIFE) {
      if (actor_life_ptrs(actor, &cur_hp, &max_hp, true)) {
        int32_t want = match->life;
        if (want < 0)
          want = 0;
        if (*max_hp > 0 && want > *max_hp)
          want = *max_hp;
        if (*cur_hp != want) {
          *cur_hp = want;
          c->hp_set++;
        }
      }
    }
    if (c->bb_done && !c->bb_done[match_idx] && restore_actor_bb(c->st, match, actor)) {
      c->bb_done[match_idx] = 1;
      c->bb_set++;
    }
    if (is_cucco_name(name)) {
      if (c->st->cuccos && c->st->cucco_count) {
        for (uint32_t ci = 0; ci < c->st->cucco_count; ci++) {
          const memfile_cucco_t *cc = &c->st->cuccos[ci];
          if (cc->instance_id == id && (!cc->name_hash || cc->name_hash == nhash)) {
            if (cc->flags & CUCCO_F_HOME)
              write_home_pos(actor, cc->home_pos);
            if (cc->flags & CUCCO_F_DROWN)
              queue_drown_job(actor, cc);
            else if (cc->flags & CUCCO_F_FADE)
              restore_model_fade(actor, cc);
            break;
          }
        }
      }
      watch_nearby_cucco(actor, c->st);
    }
    return;
  }

  /* Not in saved list: if it's a nearby Banc enemy (state 4..6) and our snapshot wasn't truncated, it was dead. */
  if (!c->delete_missing || c->st->actors_truncated || !c->st->have_pos || !z64hd_game->actor_request_delete)
    return;
  if (state < 4u || state > 6u || !starts_with(name, "Enemy_"))
    return;
  const float *pos = (const float *)(actor + A_POS);
  float dx = pos[0] - (float)c->st->x, dy = pos[1] - (float)c->st->y, dz = pos[2] - (float)c->st->z;
  if (dx * dx + dy * dy + dz * dz <= ACTOR_DEAD_RADIUS * ACTOR_DEAD_RADIUS) {
    MAIN_FN(actor_req_del_t, z64hd_game->actor_request_delete)(actor, 0);
    c->missing_del++;
  }
}

static uint32_t actor_tot_moved, actor_tot_hp, actor_tot_bb, actor_tot_dead, actor_tot_missing;
static uint8_t  actor_bb_done[MEMFILE_ACTORS_MAX];

static void restore_actors_pass(const memfile_t *st, bool first, bool last)
{
  if (!memfile_actors || !st->have_actors)
    return;
  if (first) {
    actor_tot_moved = actor_tot_hp = actor_tot_bb = actor_tot_dead = actor_tot_missing = 0;
    memset(actor_bb_done, 0, sizeof(actor_bb_done));
  }
  uintptr_t cam_actor = 0;
  resolve_camera(&cam_actor);
  actor_rest_ctx_t ctx = { st, link_actor(), cam_actor, true, { 0 }, actor_bb_done, 0, 0, 0, 0, 0 };
  walk_actor_tree(restore_actor_cb, &ctx);
  actor_tot_moved += ctx.moved;
  actor_tot_hp += ctx.hp_set;
  actor_tot_bb += ctx.bb_set;
  actor_tot_dead += ctx.deleted;
  actor_tot_missing += ctx.missing_del;
  if (last)
    log_printf("memfile %d load: actors restored (%u moved, %u hp, %u bb, %u dead, %u missing enemies removed)", /* not menu text */
               active_slot + 1, (unsigned)actor_tot_moved, (unsigned)actor_tot_hp, (unsigned)actor_tot_bb,
               (unsigned)actor_tot_dead, (unsigned)actor_tot_missing);
}

/* --- held actor capture & restore (PlayerLifterComponent / LiftableComponent, 1.0.0 & 1.1.1) --- */

static uintptr_t link_lifter_comp(uintptr_t link)
{
  if (!link || !sys_mapped(link + A_COMPONENTS, 8, SYS_PERM_R))
    return 0;
  uintptr_t comps = sys_ptr(link + A_COMPONENTS);
  if (!comps || (comps & 7u) || !sys_mapped(comps + ACOMP_LIFTER, 8, SYS_PERM_R))
    return 0;
  uintptr_t lifter = sys_ptr(comps + ACOMP_LIFTER);
  return lifter && !(lifter & 7u) && sys_mapped(lifter, LIFTER_STATE + 4u, SYS_PERM_R) ? lifter : 0;
}

static uintptr_t actor_liftable_comp(uintptr_t actor)
{
  if (!actor || !sys_mapped(actor + A_COMPONENTS, 8, SYS_PERM_R))
    return 0;
  uintptr_t comps = sys_ptr(actor + A_COMPONENTS);
  if (!comps || (comps & 7u) || !sys_mapped(comps + ACOMP_LIFTABLE, 8, SYS_PERM_R))
    return 0;
  uintptr_t liftable = sys_ptr(comps + ACOMP_LIFTABLE);
  return liftable && !(liftable & 7u) && sys_mapped(liftable, 0x280, SYS_PERM_R) ? liftable : 0;
}

static uintptr_t lifter_held_actor(uintptr_t lifter)
{
  if (!lifter)
    return 0;
  uint32_t st = 0;
  if (!sys_read32(lifter + LIFTER_STATE, &st) || (st & ~1u) != 2u)
    return 0;
  uintptr_t target = handle_actor(lifter + LIFTER_TARGET_LINK);
  if (!target || (target & 7u) || !sys_mapped(target, A_PREACTOR + 8, SYS_PERM_R))
    return 0;
  if (*(const uint32_t *)(target + A_STATE) >= 7u)
    return 0;
  return target;
}

static void drop_link_held_actor(void)
{
  if (!z64hd_game || !z64hd_game->lifter_drop)
    return;
  uintptr_t link = link_actor();
  uintptr_t lifter = link_lifter_comp(link);
  uint32_t lst = 0;
  if (lifter && sys_read32(lifter + LIFTER_STATE, &lst) && (lst & ~1u) == 2u)
    MAIN_FN(lifter_drop_t, z64hd_game->lifter_drop)(lifter, 0, 1.0f, 0.0f);
}

static uint8_t cap_held_bb[HELD_BB_MAX];

static bool capture_held_actor(memfile_t *st)
{
  uintptr_t link = link_actor();
  uintptr_t lifter = link_lifter_comp(link);
  uintptr_t target = lifter_held_actor(lifter);
  if (!target || target == link)
    return true;
  uintptr_t name_p = sys_ptr(target + A_NAME);
  if (!name_p || !sys_mapped(name_p, 16, SYS_PERM_R))
    return true;
  const char *name = (const char *)name_p;
  const char *pack = actor_pack_name(target);
  if (!pack || !pack[0])
    pack = name;

  memfile_held_t *h = &st->held;
  *h = (memfile_held_t){ 0 };
  h->valid = true;
  actor_preactor_id(target, &h->instance_id);
  h->name_hash = fnv1a_str(name);
  snprintf(h->pack_name, sizeof(h->pack_name), "%s", pack);
  memcpy(h->pos, (const void *)(target + A_POS), sizeof(h->pos));
  memcpy(h->rot, (const void *)(target + A_ROT), sizeof(h->rot));

  int32_t *cur_hp = 0, *max_hp = 0;
  if (actor_life_ptrs(target, &cur_hp, &max_hp, false)) {
    h->flags |= HELD_F_LIFE;
    h->life = *cur_hp;
    h->max_life = *max_hp;
  }

  uint32_t own_len = 0;
  if (capture_bb_blob(actor_own_blackboard(target), cap_held_bb, HELD_BB_MAX, &own_len) && own_len) {
    h->actor_bb = alloc16(own_len);
    if (!h->actor_bb)
      return false;
    memcpy(h->actor_bb, cap_held_bb, own_len);
    h->actor_bb_size = own_len;
  }
  uint32_t ai_len = 0;
  if (capture_bb_blob(actor_ai_blackboard(target), cap_held_bb, HELD_BB_MAX, &ai_len) && ai_len) {
    h->ai_bb = alloc16(ai_len);
    if (!h->ai_bb)
      return false;
    memcpy(h->ai_bb, cap_held_bb, ai_len);
    h->ai_bb_size = ai_len;
  }

  if (is_cucco_name(name) || is_cucco_name(h->pack_name)) {
    if (capture_cucco_state(target, h->instance_id, h->name_hash, &h->cucco))
      h->flags |= HELD_F_CUCCO;
  }
  return true;
}

typedef struct {
  uint64_t  instance_id;
  uint32_t  name_hash;
  uintptr_t link;
  uintptr_t found;
} find_held_ctx_t;

static void find_held_cb(uintptr_t actor, void *vctx)
{
  find_held_ctx_t *c = (find_held_ctx_t *)vctx;
  if (c->found || actor == c->link)
    return;
  uint64_t id = 0;
  if (!actor_preactor_id(actor, &id) || id != c->instance_id)
    return;
  uintptr_t name_p = sys_ptr(actor + A_NAME);
  if (!name_p || !sys_mapped(name_p, 16, SYS_PERM_R))
    return;
  if (c->name_hash && fnv1a_str((const char *)name_p) != c->name_hash)
    return;
  uint32_t state = *(const uint32_t *)(actor + A_STATE);
  if ((state & ~1u) != 4u)
    return;
  if (!actor_liftable_comp(actor))
    return;
  c->found = actor;
}

static void apply_held_actor_state(uintptr_t target, const memfile_held_t *h)
{
  actor_apply_transform(target, h->pos, h->rot);
  if (h->flags & HELD_F_LIFE) {
    int32_t *cur_hp = 0, *max_hp = 0;
    if (actor_life_ptrs(target, &cur_hp, &max_hp, true)) {
      int32_t want = h->life;
      if (want < 0)
        want = 0;
      if (*max_hp > 0 && want > *max_hp)
        want = *max_hp;
      *cur_hp = want;
    }
  }
  if (h->actor_bb && h->actor_bb_size)
    restore_bb_blob(actor_own_blackboard(target), h->actor_bb, h->actor_bb_size);
  if (h->ai_bb && h->ai_bb_size)
    restore_bb_blob(actor_ai_blackboard(target), h->ai_bb, h->ai_bb_size);
  if (h->flags & HELD_F_CUCCO) {
    if (h->cucco.flags & CUCCO_F_HOME)
      write_home_pos(target, h->cucco.home_pos);
    if (h->cucco.flags & CUCCO_F_FADE)
      restore_model_fade(target, &h->cucco);
  }
}

static bool bind_held_actor(uintptr_t link, uintptr_t target)
{
  if (!z64hd_game || !z64hd_game->actor_link_set || !z64hd_game->lifter_force_lift ||
      !z64hd_game->player_lift_anim || !z64hd_game->bsa_set_action_verb)
    return false;
  uintptr_t lifter = link_lifter_comp(link);
  if (!lifter)
    return false;
  uint64_t link_obj[3] = {
    z64hd_game->event_req_sub_vt ? MAIN(z64hd_game->event_req_sub_vt) + 0x10u : 0u,
    0u,
    0x1FFFFFFFFull,
  };
  MAIN_FN(actor_link_set_t, z64hd_game->actor_link_set)(link_obj, target, 0);
  static const char empty_key[] = "";
  const char *key_ptr = empty_key;
  bool ok = MAIN_FN(lifter_force_lift_t, z64hd_game->lifter_force_lift)(lifter, link_obj, &key_ptr, true);
  int bsa_rc = -1;
  if (ok) {
    MAIN_FN(player_lift_anim_t, z64hd_game->player_lift_anim)(link, link_obj);
    uintptr_t player = 0, base = 0;
    if (player_and_base_components(&player, &base) &&
        sys_mapped(player + PLAYER_LIFT_FLAGS, 4, SYS_PERM_R | SYS_PERM_W)) {
      *(volatile uint32_t *)(player + PLAYER_LIFT_FLAGS) |= 2u;
    }
    uintptr_t link_bsa = actor_bsa_comp(link);
    if (link_bsa)
      bsa_rc = bsa_request_verb(link_bsa, ACTION_VERB_LIFT, 1u, 0x101u);
  }
  uint32_t lst_after = 0;
  sys_read32(lifter + LIFTER_STATE, &lst_after);
  log_printf("memfiles: bind_held_actor target=%#lx force_lift=%d state=%u bsa_lift_rc=%d", /* not menu text */
             (unsigned long)target, ok ? 1 : 0, (unsigned)lst_after, bsa_rc);
  if (z64hd_game->event_req_sub_dtor)
    MAIN_FN(actor_link_dtor_t, z64hd_game->event_req_sub_dtor)(link_obj);
  return ok;
}

static void finish_load(void)
{
  if (fast_load) {
    refresh_warp_mgr(&memfiles[active_slot]);
  }
  gfx_notice("memfile loaded");
  status[0] = 0;
  set_phase(L_IDLE);
}

static void begin_camera_or_finish(const memfile_t *st)
{
  if (st->have_camera) {
    camera_frames = camera_guard = 0;
    set_phase(L_CAMERA);
  } else {
    finish_load();
  }
}

static void begin_held_or_camera(const memfile_t *st)
{
  if (st->held.valid && z64hd_game && z64hd_game->actor_link_set && z64hd_game->lifter_force_lift) {
    held_target_actor = 0;
    held_settle_frames = 0;
    held_spawn_tried = false;
    if (held_spawn_handle)
      actor_spawn_cancel(&held_spawn_handle);
    set_phase(L_HELD);
  } else {
    begin_camera_or_finish(st);
  }
}

static void step_held_restore(const memfile_t *st)
{
  const memfile_held_t *h = &st->held;
  uintptr_t link = link_actor();
  if (!h->valid || !link) {
    begin_camera_or_finish(st);
    return;
  }
  if (st->have_pos && !link_at(st, 0))
    move_link(st);

  if (!held_target_actor) {
    if (held_spawn_handle) {
      uintptr_t spawned = 0;
      spawn_poll_t sp = actor_spawn_poll(&held_spawn_handle, &spawned);
      if (sp == SPAWN_MADE && spawned) {
        held_target_actor = spawned;
      } else if (sp == SPAWN_FAILED || phase_frames > HELD_SPAWN_TIMEOUT) {
        if (held_spawn_handle)
          actor_spawn_cancel(&held_spawn_handle);
        log_printf("memfile %d load: held actor %s failed to spawn", active_slot + 1, h->pack_name); /* not menu text */
        restore_link_extra(st);
        begin_camera_or_finish(st);
        return;
      } else {
        return;
      }
    } else {
      if (h->instance_id) {
        find_held_ctx_t fc = { h->instance_id, h->name_hash, link, 0 };
        walk_actor_tree(find_held_cb, &fc);
        if (fc.found)
          held_target_actor = fc.found;
      }
      if (!held_target_actor) {
        if (h->instance_id && phase_frames < HELD_FIND_FRAMES)
          return;
        if (!held_spawn_tried && h->pack_name[0]) {
          held_spawn_tried = true;
          float spos[3] = {
            st->have_pos ? (float)st->x : h->pos[0],
            st->have_pos ? (float)st->y : h->pos[1],
            st->have_pos ? (float)st->z : h->pos[2],
          };
          float yaw = atan2f(h->rot[2], h->rot[0]) * (180.0f / 3.14159265f);
          uint32_t res = 0;
          if (actor_spawn_handle(h->pack_name, spos, yaw, &held_spawn_handle, &res))
            return;
        }
        log_printf("memfile %d load: held actor %s not found", active_slot + 1, h->pack_name); /* not menu text */
        restore_link_extra(st);
        begin_camera_or_finish(st);
        return;
      }
    }
  }

  if (!sys_mapped(held_target_actor, A_PREACTOR + 8, SYS_PERM_R)) {
    restore_link_extra(st);
    begin_camera_or_finish(st);
    return;
  }
  uint32_t tstate = *(const uint32_t *)(held_target_actor + A_STATE);
  if (tstate >= 7u) {
    log_printf("memfile %d load: held actor %s deleted before bind", active_slot + 1, h->pack_name); /* not menu text */
    restore_link_extra(st);
    begin_camera_or_finish(st);
    return;
  }
  if ((tstate & ~1u) != 4u || !actor_liftable_comp(held_target_actor)) {
    if (phase_frames > HELD_SPAWN_TIMEOUT) {
      log_printf("memfile %d load: held actor %s never became liftable", active_slot + 1, h->pack_name); /* not menu text */
      restore_link_extra(st);
      begin_camera_or_finish(st);
    }
    return;
  }

  uintptr_t lifter = link_lifter_comp(link);
  uint32_t lst = 0;
  if (lifter && sys_read32(lifter + LIFTER_STATE, &lst) && lst != 0u) {
    drop_link_held_actor();
    if (phase_frames <= HELD_SPAWN_TIMEOUT)
      return;
  }

  if (held_settle_frames > 0) {
    held_settle_frames--;
    actor_apply_transform(held_target_actor, h->pos, h->rot);
    if (held_settle_frames > 0)
      return;
  } else if (sys_mapped(held_target_actor + A_POS, 12, SYS_PERM_R)) {
    float cur[3];
    memcpy(cur, (const void *)(held_target_actor + A_POS), 12);
    float dx = cur[0] - h->pos[0], dy = cur[1] - h->pos[1], dz = cur[2] - h->pos[2];
    if (dx * dx + dy * dy + dz * dz > 4.0f) {
      /* Distant target (e.g. >50 m away in a grotto or non-tracked pot/rock): move it first and let
       * LOD/physics wake for a couple frames before binding. */
      apply_held_actor_state(held_target_actor, h);
      held_settle_frames = HELD_SETTLE_FRAMES;
      return;
    }
  }

  apply_held_actor_state(held_target_actor, h);
  bool bound = bind_held_actor(link, held_target_actor);
  log_printf("memfile %d load: held actor %s %#lx %s (astate=%u verb=%#x)", active_slot + 1, h->pack_name, /* not menu text */
             (unsigned long)held_target_actor, bound ? "bound" : "bind failed",
             (unsigned)tstate, (unsigned)actor_cur_verb(held_target_actor));
  if (bound) {
    if (h->actor_bb && h->actor_bb_size) {
      held_reapply_actor = held_target_actor;
      held_reapply_frames = HELD_REAPPLY_BB;
      held_reapply_slot = active_slot;
    }
    held_watch_actor = held_target_actor;
    held_watch_frames = HELD_WATCH_FRAMES;
    held_watch_lstate = 3u;
    if (lifter)
      sys_read32(lifter + LIFTER_STATE, &held_watch_lstate);
    held_watch_astate = tstate;
    held_watch_verb = actor_cur_verb(held_target_actor);
  }
  held_target_actor = 0;
  held_settle_frames = 0;
  restore_link_extra(st);
  begin_camera_or_finish(st);
}

static void begin_actors_or_camera(const memfile_t *st)
{
  if (memfile_actors && st->have_actors && z64hd_game && z64hd_game->actor_mgr_ptr) {
    set_phase(L_ACTORS);
  } else {
    begin_held_or_camera(st);
  }
}

static void begin_move_or_camera(const memfile_t *st)
{
  if (st->have_pos && move_link(st)) {
    sync_void_ring_during_load(st, true);
    set_phase(L_MOVING);
  } else if (st->have_pos) {
    log_printf("memfile %d load: Link couldn't be moved to its position", active_slot + 1); /* not menu text */
    gfx_notice("memfile loaded, but Link couldn't be moved to its position");
    status[0] = 0;
    set_phase(L_IDLE);
  } else {
    if (!st->have_camera && !(memfile_actors && st->have_actors))
      log_printf("memfile %d loaded (no position)", active_slot + 1); /* not menu text */
    begin_actors_or_camera(st);
  }
}

/* --- save -------------------------------------------------------------------------------------- */

static void save_fail(const char *notice)
{
  log_printf("memfile %d save: %s", active_slot + 1, notice); /* not menu text */
  gfx_notice(notice);
  uint8_t *desc0 = gd_file0_desc(0, 0);
  if (staging.save_buf && desc0 && (desc0[GD_DESC_FLAGS] & GD_FLAG_SAVE_WORK) &&
      sys_ptr((uintptr_t)(desc0 + GD_DESC_SAVE_BUF)) == (uintptr_t)staging.save_buf) {
    inflight_save_buf = staging.save_buf;
    staging.save_buf = 0;
  }
  free_memfile(&staging);
  status[0] = 0;
  set_phase(L_IDLE);
}

static void finish_save(const uint8_t *desc0)
{
  uint32_t written = *(const uint32_t *)(desc0 + GD_DESC_WRITTEN);
  uint32_t magic = *(const uint32_t *)staging.save_buf;
  if (magic != GD_SAVE_MAGIC || written < 16u || written > staging.save_cap) {
    save_fail("can't save a memfile: game data not readable");
    return;
  }
  uint32_t orig_cap = staging.save_cap;
  if (in_pool(staging.save_buf)) {
    pool_shrink(staging.save_buf, written);
    staging.save_cap = written;
  } else {
    /* staging.save_buf fell back to main's CRT heap: move the compact snapshot into the BSS pool if it fits */
    uint8_t *packed = pool_alloc(written);
    if (!packed && memfiles[active_slot].used && pool_would_fit_reclaiming(written, &memfiles[active_slot])) {
      free_memfile(&memfiles[active_slot]);
      packed = pool_alloc(written);
    }
    if (!packed && written + 4096u < staging.save_cap)
      packed = aligned_alloc(16, (written + 15u) & ~(size_t)15u);
    if (packed) {
      memcpy(packed, staging.save_buf, written);
      free(staging.save_buf);
      staging.save_buf = packed;
      staging.save_cap = written;
    }
  }
  staging.save_size = written;
  staging.used = true;
  free_memfile(&memfiles[active_slot]);
  memfiles[active_slot] = staging;
  staging = (memfile_t){ 0 };
  status[0] = 0;
  set_phase(L_IDLE);
  const memfile_t *st = &memfiles[active_slot];
  log_printf("memfile %d saved: %s at \"%s\", %.2f %.2f %.2f facing %.3f,%.3f, life %d, magic %d, time %02u:%02u, "
             "%u/%u bytes binary (%u KiB pool free), %u scalars + %u strings session, %u actors%s%s%s", /* not menu text */
             active_slot + 1, st->map, st->start, st->x, st->y, st->z, st->fx, st->fz, (int)st->life, (int)st->magic,
             st->have_time ? (unsigned)(st->time / 3600) : 99u, st->have_time ? (unsigned)(st->time / 60 % 60) : 99u,
             (unsigned)st->save_size, (unsigned)orig_cap, (unsigned)(pool_free_bytes() >> 10),
             (unsigned)st->s_scalar_count, (unsigned)st->s_str_count,
             (unsigned)st->actor_count, st->actors_truncated ? " (capped)" : "",
             st->held.valid ? ", holding " : "", st->held.valid ? st->held.pack_name : "");
  gfx_logf("memfile %d saved", active_slot + 1);
}

/* the load is over but its camera is still watched (camera_guard): a new save or load ends that */
static void end_camera_guard(void)
{
  if (phase == L_CAMERA && camera_guard)
    set_phase(L_IDLE);
}

/* --- when the game can't be saved or loaded from ------------------------------------------------- */

static bool in_cutscene(void) { return eventskip_known() && eventskip_running(); }

/* the game's pause screen or map is up or opening (the item wheel, which pauses with no menu screen, isn't) */
static bool pause_menu_up(void) { return gd_game_pausing() || (gd_game_paused(0, 0) && gd_game_menu_screen()); }

bool memfile_save(int s)
{
  if (s < 0 || s >= MEMFILE_SLOTS)
    return false;
  end_camera_guard();
  if (phase != L_IDLE) {
    gfx_notice("memfile is busy");
    return false;
  }
  bool loading = false;
  gd_get_bool(F_IS_LOADING_BANC, &loading);
  if (loading || warp_pending()) {
    gfx_notice("can't save a memfile while a map is loading");
    return false;
  }
  bool game_over = false;
  gd_get_bool(F_IS_GAME_OVER, &game_over);
  if (in_cutscene() || game_over) {
    gfx_notice(game_over ? "can't save a memfile during a game over" : "can't save a memfile during a cutscene");
    return false;
  }
  if (pause_menu_up()) {
    gfx_notice("can't save a memfile while the game is paused");
    return false;
  }
  retire_inflight_buf();
  if (inflight_save_buf) {
    gfx_notice("memfile is busy");
    return false;
  }
  uint8_t *mgr = 0;
  uint32_t buf_size = 0;
  if (!gd_ready() || !gd_file0_desc(&mgr, &buf_size) || !z64hd_game->gd_request_save) {
    gfx_notice("can't save a memfile: game data not found");
    return false;
  }
  const char *map = gd_get_str(GD_STR_MAP);
  if (!map || !map[0]) {
    gfx_notice("can't save a memfile: the map isn't known");
    return false;
  }

  free_memfile(&staging);
  clear_post_load_jobs();
  /* Capture small session/actor metadata first so staging.save_buf sits at the tail of the pool and
   * pool_shrink() in finish_save() coalesces its unused tail directly into the remaining free space. */
  int sess = capture_session(&staging);
  if (sess != CAP_OK) {
    free_memfile(&staging);
    gfx_notice(sess == CAP_BAD_GD ? "can't save a memfile: game data not readable"
                                  : "can't save a memfile: out of memory");
    return false;
  }
  if (!capture_actors(&staging) || !capture_held_actor(&staging)) {
    free_memfile(&staging);
    gfx_notice("can't save a memfile: out of memory");
    return false;
  }
  staging.save_buf = alloc16(buf_size);
  staging.save_cap = buf_size;
  if (!staging.save_buf) {
    log_printf("memfile %d save: out of memory for %u-byte save buffer (%u KiB pool free)", /* not menu text */
               s + 1, (unsigned)buf_size, (unsigned)(pool_free_bytes() >> 10));
    free_memfile(&staging);
    gfx_notice("can't save a memfile: out of memory");
    return false;
  }

  snprintf(staging.map, sizeof(staging.map), "%s", map);
  const char *start = gd_get_str(GD_STR_SPAWN);
  snprintf(staging.start, sizeof(staging.start), "%s", start ? start : "");

  vec3f_t p;
  staging.have_pos = link_drawn_pos(&p);
  staging.x = p.x, staging.y = p.y, staging.z = p.z;
  double fx = 0, fz = 0;
  if (staging.have_pos && link_facing(&fx, &fz))
    staging.fx = fx, staging.fz = fz;
  int32_t *cur, *max;
  staging.have_life = link_health(&cur, &max);
  staging.life = staging.have_life ? *cur : 0;
  staging.have_magic = gd_get_int(F_MAGIC, &staging.magic);
  staging.have_nayru = nayru_get(&staging.nayru_left, &staging.nayru_total);
  capture_link_extra(&staging);
  bool child = false;
  staging.child = gd_get_bool(F_IS_CHILDHOOD, &child) ? child : -1;
  staging.have_time = time_of_day(&staging.time);
  log_why = true;
  staging.have_camera = camera_take(staging.camera);
  staging.have_component = camera_take_component(&staging);
  camera_take_actions(&staging);
  if (!camera_view(staging.view))
    memset(staging.view, 0, sizeof(staging.view));

  active_slot = s;
  world_on_save();
  snprintf(status, sizeof(status), TF("saving memfile %d"), s + 1);
  if (MAIN_FN(gd_req_save_t, z64hd_game->gd_request_save)(mgr, staging.save_buf, 0))
    set_phase(L_SAVING_WAIT);
  else
    set_phase(L_SAVING_REQ);
  return true;
}

/* --- load -------------------------------------------------------------------------------------- */

static void fail(const char *notice)
{
  clear_overrides();
  clear_post_load_jobs();
  log_printf("memfile %d load: %s", active_slot + 1, notice); /* not menu text */
  gfx_notice(notice);
  status[0] = 0;
  set_phase(L_IDLE);
}

static bool can_fast_reload(const memfile_t *st)
{
  if (!memfile_fast_reload || st->child < 0 || !link_actor())
    return false;
  const char *cur_map = gd_get_str(GD_STR_MAP);
  bool cur_child = false;
  if (!cur_map || !str_equal(cur_map, st->map) || !gd_get_bool(F_IS_CHILDHOOD, &cur_child) ||
      (int)cur_child != st->child)
    return false;
  bool game_over = false, req = false;
  gd_get_bool(F_IS_GAME_OVER, &game_over);
  int32_t *cur_hp = 0, *max_hp = 0;
  bool alive = !link_health(&cur_hp, &max_hp) || *cur_hp > 0;
  return !game_over && alive && gd_get_bool(F_REQ_RESET_GIMMICK, &req);
}

static bool start_load_now(int s, uint8_t *mgr)
{
  clear_post_load_jobs();
  memfile_t *st = &memfiles[s];
  active_slot = s;
  if (can_fast_reload(st)) {
    if (!MAIN_FN(gd_req_load_t, z64hd_game->gd_request_load)(mgr, st->save_buf, 0)) {
      gfx_notice("can't load a memfile now: game data is busy");
      return false;
    }
    log_printf("memfile %d load (fast ResetGimmick): %s", s + 1, st->map); /* not menu text */
    fast_load = true;
    snprintf(status, sizeof(status), TF("loading memfile %d"), s + 1);
    set_phase(L_FAST_WAIT_GD);
    return true;
  }

  log_printf("memfile %d load (full): %s at \"%s\"", s + 1, st->map, st->start); /* not menu text */
  if (!age_warp_to_silent(st->map, st->start, st->have_pos)) {
    gfx_notice("can't load a memfile now: the game refused the map change");
    log_printf("memfile %d load: refused before GameData touched", s + 1); /* not menu text */
    return false;
  }
  old_actor = link_actor();
  saw_unload = false;
  overrides_armed = false;
  fast_load = false;
  snprintf(status, sizeof(status), TF("loading memfile %d"), s + 1);
  set_phase(L_WAIT_LOADING);
  return true;
}

bool memfile_load(int s)
{
  if (s < 0 || s >= MEMFILE_SLOTS)
    return false;
  memfile_t *st = &memfiles[s];
  if (!st->used || !st->save_buf) {
    gfx_notice("that memfile is empty");
    return false;
  }
  end_camera_guard();
  if (phase != L_IDLE) {
    gfx_notice("memfile is busy");
    return false;
  }
  bool loading = false;
  gd_get_bool(F_IS_LOADING_BANC, &loading);
  if (loading || warp_pending()) {
    gfx_notice("a map is already loading");
    return false;
  }
  if (pause_menu_up()) {
    gfx_notice("can't load a memfile while the game is paused");
    return false;
  }
  uint8_t *mgr = 0;
  uint32_t buf_size = 0;
  if (!gd_ready() || !gd_file0_desc(&mgr, &buf_size) || !z64hd_game->gd_request_load ||
      buf_size < st->save_size) {
    gfx_notice("can't load that memfile: game data not found");
    return false;
  }
  if (in_cutscene()) {
    if (!eventskip_start()) {
      gfx_notice("can't load a memfile during a cutscene");
      return false;
    }
    log_printf("memfile %d load: ending cutscene before loading", s + 1); /* not menu text */
    active_slot = s;
    snprintf(status, sizeof(status), TF("loading memfile %d"), s + 1);
    set_phase(L_CANCEL_EVENT);
    return true;
  }
  return start_load_now(s, mgr);
}

/* --- frame state machine ----------------------------------------------------------------------- */

static void step_saving(uint8_t *mgr, uint8_t *desc0)
{
  if (phase == L_SAVING_REQ) {
    if (!mgr || !desc0) {
      if (phase_frames > SAVE_REQ_TIMEOUT)
        save_fail("can't save a memfile: game data not readable");
      return;
    }
    if (MAIN_FN(gd_req_save_t, z64hd_game->gd_request_save)(mgr, staging.save_buf, 0))
      set_phase(L_SAVING_WAIT);
    else if (phase_frames > SAVE_REQ_TIMEOUT)
      save_fail("can't save a memfile: autosave busy");
    return;
  }
  if (!mgr || !desc0) {
    if (phase_frames > SAVE_WAIT_TIMEOUT)
      save_fail("can't save a memfile: game data not readable");
    return;
  }
  uint8_t flags = desc0[GD_DESC_FLAGS];
  uint8_t busy = mgr[GD_MGR_SYNC_BUSY];
  int32_t active = 0;
  memcpy(&active, mgr + GD_MGR_ACTIVE_SAVES, sizeof(active));
  if (!(flags & (GD_FLAG_SAVE_REQ | GD_FLAG_SAVE_WORK)) && active <= 0 && !busy)
    finish_save(desc0);
  else if (phase_frames > SAVE_WAIT_TIMEOUT)
    save_fail("can't save a memfile: save timed out");
}

static void trigger_reset_gimmick(void)
{
  drop_link_held_actor();
  reset_preactor_flags();
  gd_poke_int(F_RESET_GIMMICK_OPT, 0);
  gd_set_int(F_RESET_GIMMICK_OPT, 0);
  patch_main_module();
  gd_set_bool(F_REQ_RESET_GIMMICK, true);
  saw_reset_req = false;
  set_phase(L_FAST_WAIT_RESET);
}

static void step_fast_load(const memfile_t *st, const uint8_t *desc0)
{
  switch (phase) {
    case L_FAST_WAIT_GD:
      if (!desc0) {
        fail("memfile load: game data not readable");
        return;
      }
      if (desc0[GD_DESC_FLAGS] & GD_FLAG_LOAD_REQ) {
        if (phase_frames > GD_LOAD_TIMEOUT)
          fail("memfile load: game data timed out");
        return;
      }
      if (!desc0[GD_DESC_LOAD_OK]) {
        fail("memfile load: game data rejected");
        return;
      }
      restore_session(st);
      world_on_load();
      navi_on_load();
      quests_resync();
      resync_scene_mgrs();
      restore_vitals_and_gear(st);
      if (st->have_pos && move_link(st)) {
        sync_void_ring_during_load(st, true);
        snprintf(status, sizeof(status), TF("loading memfile %d: placing Link"), active_slot + 1);
        set_phase(L_FAST_MOVE_PRE);
      } else {
        trigger_reset_gimmick();
      }
      break;
    case L_FAST_MOVE_PRE:
      if (st->have_void_ring)
        sync_void_ring_during_load(st, false);
      if (link_at(st, phase_frames) || phase_frames >= FAST_MOVE_TIMEOUT) {
        trigger_reset_gimmick();
      } else if (phase_frames % FAST_MOVE_RETRY == 0) {
        move_link(st);
      }
      break;
    case L_FAST_WAIT_RESET: {
      if (st->have_void_ring && near_saved_pos(st))
        restore_void_ring(st);
      bool req = false;
      gd_get_bool(F_REQ_RESET_GIMMICK, &req);
      if (req)
        saw_reset_req = true;
      if ((phase_frames >= 2 || saw_reset_req) && !req) {
        clear_overrides();
        restore_vitals_and_gear(st);
        begin_move_or_camera(st);
      } else if (phase_frames > RESET_TIMEOUT) {
        clear_overrides();
        log_printf("memfile %d load: ResetGimmick timed out", active_slot + 1); /* not menu text */
        restore_vitals_and_gear(st);
        begin_move_or_camera(st);
      }
      break;
    }
    default:
      break;
  }
}

static void step_full_load(const memfile_t *st, uint8_t *mgr, const uint8_t *desc0, bool loading)
{
  switch (phase) {
    case L_WAIT_LOADING:
      if (!warp_requested()) {
        fail("memfile load: the map didn't reload");
        return;
      }
      if (!overrides_armed && !warp_pending()) {
        arm_load_overrides(st);
        clear_boot_event();
        overrides_armed = true;
      }
      if (loading || link_actor() != old_actor) {
        if (!overrides_armed) {
          arm_load_overrides(st);
          overrides_armed = true;
        }
        clear_boot_event();
        if (!mgr || !desc0 || !z64hd_game->gd_request_load ||
            !MAIN_FN(gd_req_load_t, z64hd_game->gd_request_load)(mgr, st->save_buf, 0)) {
          fail("memfile load: game data load failed");
          return;
        }
        set_phase(L_FULL_WAIT_GD);
      } else if (warp_abandoned() || phase_frames > LOAD_START_TIMEOUT) {
        if (warp_pending())
          warp_cancel();
        fail("memfile load: the map didn't reload");
      }
      break;
    case L_FULL_WAIT_GD:
      if (!desc0) {
        fail("memfile load: game data not readable");
        return;
      }
      if (desc0[GD_DESC_FLAGS] & GD_FLAG_LOAD_REQ) {
        if (phase_frames > GD_LOAD_TIMEOUT)
          fail("memfile load: game data timed out");
        return;
      }
      if (!desc0[GD_DESC_LOAD_OK]) {
        fail("memfile load: game data rejected");
        return;
      }
      restore_session(st);
      world_on_load();
      if (st->have_time)
        set_time_of_day(st->time);
      navi_on_load();
      quests_resync();
      resync_scene_mgrs();
      set_phase(L_WAIT_ARRIVAL);
      break;
    case L_WAIT_ARRIVAL: {
      const char *map = gd_get_str(GD_STR_MAP);
      if (!loading && map && str_equal(map, st->map) && link_actor() && (link_actor() != old_actor || saw_unload)) {
        clear_overrides();
        if (st->have_void_ring && near_saved_pos(st))
          restore_void_ring(st);
        snprintf(status, sizeof(status), TF("loading memfile %d: placing Link"), active_slot + 1);
        set_phase(L_WAIT_SETTLE);
      } else if (phase_frames > ARRIVAL_TIMEOUT) {
        fail("memfile load: the map didn't finish loading");
      }
      break;
    }
    case L_WAIT_SETTLE:
      if (loading || !link_actor()) {
        quiet_frames = 0;
        break;
      }
      if (st->have_void_ring && near_saved_pos(st))
        restore_void_ring(st);
      quiet_frames = eventskip_known() && eventskip_running() ? 0 : quiet_frames + 1;
      if (quiet_frames >= QUIET_FRAMES || phase_frames > SETTLE_TIMEOUT) {
        if (quiet_frames < QUIET_FRAMES)
          log_printf("memfile %d load: an event still runs, moving Link anyway", active_slot + 1); /* not menu text */
        restore_vitals_and_gear(st);
        begin_move_or_camera(st);
      }
      break;
    default:
      break;
  }
}

static void step_post_move(const memfile_t *st)
{
  if (phase == L_MOVING) {
    if (link_at(st, phase_frames)) {
      restore_link_extra(st);
      double fx = 0, fz = 0;
      link_facing(&fx, &fz);
      log_printf("memfile %d loaded (Link placed after %d frames, facing %.3f,%.3f, saved %.3f,%.3f)",
                 active_slot + 1, phase_frames, fx, fz, st->fx, st->fz); /* not menu text */
      begin_actors_or_camera(st);
    } else if (phase_frames > MOVE_TIMEOUT) {
      fail("memfile loaded, but Link couldn't be moved to its position");
    } else {
      if (st->have_void_ring)
        sync_void_ring_during_load(st, false);
      if (phase_frames % MOVE_RETRY_EVERY == 0)
        move_link(st);
    }
    return;
  }

  if (phase == L_ACTORS) {
    if (st->held.valid && st->have_pos && !link_at(st, 0))
      move_link(st);
    restore_actors_pass(st, phase_frames == 1, phase_frames >= ACTOR_RESTORE_FRAMES);
    if (phase_frames >= ACTOR_RESTORE_FRAMES)
      begin_held_or_camera(st);
    return;
  }

  if (phase == L_HELD) {
    step_held_restore(st);
    return;
  }

  log_why = phase_frames == 1;
  if (camera_guard) {
    /* a full reload: the new map can turn the camera back to its default after the restore; put it back again */
    float off = 0;
    camera_guard++;
    if (camera_angle_off(st, &off) && off > CAMERA_TURNED && camera_reapplies < CAMERA_REAPPLIES) {
      camera_reapplies++;
      log_printf("memfile %d load: camera turned away %d frames after the restore (%.1f degrees), " /* not menu text */
                 "put back again", active_slot + 1, camera_guard, off); /* not menu text */
      camera_guard = 0;
      camera_frames = 1; /* the writes again, the actions found afresh */
    } else if (camera_guard > CAMERA_GUARD) {
      log_printf("memfile %d load: camera held (%d re-applies, %.1f degrees off)", /* not menu text */
                 active_slot + 1, camera_reapplies, off);
      set_phase(L_IDLE);
      return;
    } else {
      return;
    }
  } else if (!camera_frames) {
    camera_reapplies = 0;
    if (fast_load) {
      camera_frames = 1; /* in place: the player camera stays the one drawn, so no handover to wait for */
    } else if (camera_drawn()) {
      log_printf("memfile %d load: the player camera is drawn after %d frames", /* not menu text */
                 active_slot + 1, phase_frames);
      camera_frames = 1;
    } else if (phase_frames > CAMERA_WAIT) {
      log_printf("memfile %d load: the player camera wasn't drawn within %d frames, " /* not menu text */
                 "put back anyway", active_slot + 1, CAMERA_WAIT); /* not menu text */
      camera_frames = 1;
    } else {
      if (log_why)
        camera_state();
      return;
    }
  } else {
    camera_frames++;
  }
  if (camera_frames == 1 && fast_load) {
    bool arrived = camera_arrive();
    log_printf("memfile %d load: camera target %s", active_slot + 1, /* not menu text */
               arrived ? "reset as on arrival" : "not reset (follow target not found)"); /* not menu text */
  }
  if (camera_frames == 1) {
    float v[6];
    if (camera_view(v))
      memcpy(camera_last_at, v + 3, sizeof(camera_last_at));
  }
  /* the saved angle, written every frame until the look-at has settled (at least CAMERA_WRITE_FRAMES) */
  if (st->have_component)
    camera_put_component(st);
  camera_put_actions(st, camera_frames == 1);
  if (!camera_put(st->camera))
    log_printf("memfile %d load: the player camera wasn't found", active_slot + 1); /* not menu text */
  float gap = 0, moved = 0;
  bool seen = camera_at_gap(st->view + 3, &gap, &moved);
  bool settled = !seen || gap <= CAMERA_NEAR || (camera_frames > 1 && moved <= CAMERA_STILL);
  if ((camera_frames >= CAMERA_WRITE_FRAMES && settled) || camera_frames >= CAMERA_SETTLE_MAX) {
    float v[6];
    if (camera_view(v))
      log_printf("memfile %d loaded: camera eye %.2f %.2f %.2f at %.2f %.2f %.2f " /* not menu text */
                 "(saved %.2f %.2f %.2f at %.2f %.2f %.2f), %d frames, look-at %.3f off%s", /* not menu text */
                 active_slot + 1, v[0], v[1], v[2], v[3], v[4], v[5], st->view[0], st->view[1], st->view[2],
                 st->view[3], st->view[4], st->view[5], camera_frames, gap,
                 settled ? "" : " (still moving)"); /* not menu text */
    if (fast_load) {
      finish_load();
    } else {
      if (!camera_reapplies) {
        gfx_notice("memfile loaded"); /* watched a while longer (camera_guard), quietly */
        status[0] = 0;
      }
      camera_guard = 1;
    }
  }
}

void memfile_frame(void)
{
  retire_inflight_buf();
  step_post_load_jobs();
  if (phase == L_IDLE)
    return;
  phase_frames++;

  uint8_t *mgr = 0;
  uint8_t *desc0 = gd_file0_desc(&mgr, 0);
  if (phase == L_SAVING_REQ || phase == L_SAVING_WAIT) {
    step_saving(mgr, desc0);
    return;
  }
  if (phase == L_CANCEL_EVENT) {
    if (!in_cutscene()) {
      if (!mgr || !desc0 || !start_load_now(active_slot, mgr)) {
        status[0] = 0;
        set_phase(L_IDLE);
      }
    } else if (phase_frames > EVENT_CANCEL_TIMEOUT) {
      fail("can't load a memfile: cutscene didn't end");
    }
    return;
  }

  const memfile_t *st = &memfiles[active_slot];
  bool loading = false;
  gd_get_bool(F_IS_LOADING_BANC, &loading);
  if (loading || !link_actor())
    saw_unload = true;

  if (phase == L_FAST_WAIT_GD || phase == L_FAST_MOVE_PRE || phase == L_FAST_WAIT_RESET)
    step_fast_load(st, desc0);
  else if (phase == L_WAIT_LOADING || phase == L_FULL_WAIT_GD || phase == L_WAIT_ARRIVAL || phase == L_WAIT_SETTLE)
    step_full_load(st, mgr, desc0, loading);
  else
    step_post_move(st);
}

/* --- sharing: .szm files ----------------------------------------------------------------------- */

/* A .szm file (little endian): szm_header_t, then tagged sections { u32 tag, u32 size, bytes }. Every part that
 * could grow says how big it is, so files stay shareable both ways between sloperina versions:
 *  - the header: header_size. Readers skip what follows the fields they know, and zero the fields an older,
 *    shorter header doesn't have (from SZM_HEADER_MIN).
 *  - sections: unknown tags are skipped. Within a section, new fields go at its end and are read as optional
 *    (`if (IN(in, x))`, a default otherwise): an older file then loads with that part off.
 *  - record lists (SSCL, SSTR) start with a u32 record size: records can grow at their end.
 *  - raw bytes from the game's memory (the camera's) are { u32 size, bytes }: a longer one is read as far as this
 *    version knows; a shorter one means that part can't be restored.
 *  - a camera action entry is { u32 size, fields }: fields can be added after its bytes.
 * SZM_FORMAT goes up only for a change that isn't one of those: an older reader then refuses the file, and the
 * section readers (in->format) read the older layouts into the current memfile_t. That's the migration: there's
 * no separate converter. The sloperina string in the header is only for the log; nothing depends on it.
 * Format 1 was only written by test builds before the above (refused). The game's version must match (GameData's
 * binary and the camera's layout are that version's own). */
#define SZM_MAGIC       "SZMF"
#define SZM_FORMAT      2u
#define SZM_FORMAT_MIN  2u   /* the oldest format this version reads */
#define SZM_HEADER_MIN  164u /* format 2's header: every file's header is at least this */
#define SZM_TAG(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#define SZM_GAMEDATA    SZM_TAG('G', 'D', 'A', 'T') /* GameDataMgr's file 0 snapshot */
#define SZM_SCALARS     SZM_TAG('S', 'S', 'C', 'L') /* session fields: { u32 hash, u8 kind, u8 width, u8 val[12] } */
#define SZM_STRINGS     SZM_TAG('S', 'S', 'T', 'R') /* session strings: { u32 hash, char val[64] } */
#define SZM_STATE       SZM_TAG('S', 'T', 'A', 'T') /* start point, Link, vitals, clock, age */
#define SZM_CAMERA      SZM_TAG('C', 'A', 'M', 'R') /* the player camera and its AI action nodes */
#define SZM_ACTORS      SZM_TAG('A', 'C', 'T', 'R') /* scene actors: { u8 flags, u32 record_size, records } */
#define SZM_ACTOR_BB    SZM_TAG('A', 'B', 'B', 'D') /* per-actor Blackboard primitive pool */
#define SZM_CUCCO       SZM_TAG('C', 'U', 'C', 'O') /* per-Cucco home/drown/fade/respawn state */
#define SZM_HELD        SZM_TAG('H', 'E', 'L', 'D') /* actor currently held by Link */
#define SZM_EXTRA       SZM_TAG('X', 'T', 'R', 'A') /* Link velocity, fall/JS history, SavePos, RNG, 8-unit ring */
#define SZM_SCALAR_SIZE 18u /* format 2's records; a file's own are at least this */
#define SZM_STRING_SIZE (4u + GD_STR_LEN)
#define SZM_ACTOR_SIZE  78u /* + u32 bb_off, u32 bb_len after name_hash */
#define SZM_CUCCO_SIZE  56u
#define SZM_FILE_MAX    (GD_MAX_SAVE_BUF + (1u << 20))
#define SZM_SLACK       0x10000u /* room for every section but the snapshot and the session fields */
#define SZM_DIR         SD_DIR "/" MEMFILE_DIR_NAME
#define SZM_PATH_LEN    (sizeof(SZM_DIR) + MEMFILE_FOLDER_LEN + MEMFILE_NAME_MAX + 8)

enum { STATE_POS = 1, STATE_LIFE = 2, STATE_MAGIC = 4, STATE_TIME = 8, STATE_NAYRU = 16 };
enum { EXTRA_VEL = 1, EXTRA_FALL = 2, EXTRA_SAVEPOS = 4, EXTRA_RESTART = 8, EXTRA_RNG = 16, EXTRA_VOID_RING = 32 };
enum { CAM_STATE = 1, CAM_COMPONENT = 2 };
enum { ACTORS_TRUNCATED = 1 };

typedef struct {
  char     magic[4];
  uint16_t format, header_size; /* readers skip to header_size: later versions may add to the header */
  uint32_t body_size, body_crc; /* the sections after the header: their length and CRC-32 */
  int8_t   child;               /* 1 child, 0 adult, -1 unknown */
  uint8_t  pad[3];
  char     game[16];            /* the game's version: "1.0.0", "1.1.1" */
  char     sloperina[32];       /* the sloperina that wrote it */
  char     map[GD_STR_LEN];
  char     name[32];            /* as it was exported */
} szm_header_t;
_Static_assert(sizeof(szm_header_t) >= SZM_HEADER_MIN, "szm_header_t only grows");

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
  uint32_t c = ~0u;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++)
      c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

static void copy_str(char *dst, size_t len, const char *src)
{
  size_t i = 0;
  for (; src && src[i] && i + 1 < len; i++)
    dst[i] = src[i];
  for (; i < len; i++)
    dst[i] = 0;
}

typedef struct { uint8_t *p; uint32_t n, cap; bool ok; } szm_out_t;

__attribute__((noinline)) static void out_bytes(szm_out_t *o, const void *b, uint32_t n)
{
  if (!o->ok || o->cap - o->n < n) {
    o->ok = false;
    return;
  }
  memcpy(o->p + o->n, b, n);
  o->n += n;
}
#define OUT(o, v) out_bytes((o), &(v), sizeof(v))

static uint32_t out_begin(szm_out_t *o, uint32_t tag)
{
  uint32_t at = o->n, zero = 0;
  OUT(o, tag);
  OUT(o, zero);
  return at;
}

static void out_end(szm_out_t *o, uint32_t at)
{
  if (o->ok) {
    uint32_t size = o->n - at - 8;
    memcpy(o->p + at + 4, &size, 4);
  }
}

/* a u32 size, filled in by out_sized_end, for what follows */
static uint32_t out_sized(szm_out_t *o)
{
  uint32_t at = o->n, zero = 0;
  OUT(o, zero);
  return at;
}

static void out_sized_end(szm_out_t *o, uint32_t at)
{
  if (o->ok) {
    uint32_t size = o->n - at - 4;
    memcpy(o->p + at, &size, 4);
  }
}

static void out_blob(szm_out_t *o, const void *b, uint32_t n)
{
  OUT(o, n);
  out_bytes(o, b, n);
}

/* the memfile as a .szm file's bytes (the caller frees with free16), or 0 */
static uint8_t *szm_encode(const memfile_t *st, const char *name, uint32_t *size_out)
{
  uint32_t cap = (uint32_t)sizeof(szm_header_t) + st->save_size + st->s_scalar_count * SZM_SCALAR_SIZE +
                 st->s_str_count * SZM_STRING_SIZE + st->actor_count * SZM_ACTOR_SIZE + st->actor_bb_size +
                 st->cucco_count * SZM_CUCCO_SIZE + st->held.actor_bb_size + st->held.ai_bb_size +
                 CAMERA_ACTIONS * (16u + ACTION_BYTES) + SZM_SLACK;
  szm_out_t o = { alloc16(cap), (uint32_t)sizeof(szm_header_t), cap, true };
  if (!o.p)
    return 0;

  uint32_t at = out_begin(&o, SZM_GAMEDATA);
  out_bytes(&o, st->save_buf, st->save_size);
  out_end(&o, at);

  at = out_begin(&o, SZM_SCALARS);
  uint32_t record = SZM_SCALAR_SIZE;
  OUT(&o, record);
  for (uint32_t i = 0; i < st->s_scalar_count; i++) {
    const session_scalar_t *e = &st->s_scalars[i];
    OUT(&o, e->hash);
    OUT(&o, e->kind);
    OUT(&o, e->width);
    out_bytes(&o, e->val, sizeof(e->val));
  }
  out_end(&o, at);

  at = out_begin(&o, SZM_STRINGS);
  record = SZM_STRING_SIZE;
  OUT(&o, record);
  for (uint32_t i = 0; i < st->s_str_count; i++) {
    OUT(&o, st->s_strs[i].hash);
    out_bytes(&o, st->s_strs[i].val, GD_STR_LEN);
  }
  out_end(&o, at);

  at = out_begin(&o, SZM_STATE);
  uint8_t flags = (st->have_pos ? STATE_POS : 0) | (st->have_life ? STATE_LIFE : 0) |
                  (st->have_magic ? STATE_MAGIC : 0) | (st->have_time ? STATE_TIME : 0) |
                  (st->have_nayru ? STATE_NAYRU : 0);
  int32_t child = st->child;
  out_bytes(&o, st->start, GD_STR_LEN);
  OUT(&o, st->x);
  OUT(&o, st->y);
  OUT(&o, st->z);
  OUT(&o, st->fx);
  OUT(&o, st->fz);
  OUT(&o, flags);
  OUT(&o, st->life);
  OUT(&o, st->magic);
  OUT(&o, st->nayru_left);
  OUT(&o, st->nayru_total);
  OUT(&o, st->time);
  OUT(&o, child);
  out_end(&o, at);

  at = out_begin(&o, SZM_CAMERA);
  uint8_t cam = (st->have_camera ? CAM_STATE : 0) | (st->have_component ? CAM_COMPONENT : 0);
  uint8_t actions = (uint8_t)(st->camera_actions > 0 ? st->camera_actions : 0);
  OUT(&o, cam);
  out_blob(&o, st->camera, CAMERA_BYTES);
  out_blob(&o, st->comp_head, CAMERA_COMP_HEAD);
  out_blob(&o, st->comp_turn, CAMERA_TURN_BYTES);
  OUT(&o, st->view);
  OUT(&o, actions);
  for (int i = 0; i < actions; i++) {
    uint32_t entry = out_sized(&o);
    OUT(&o, st->action_vtable[i]);
    OUT(&o, st->action_node_idx[i]);
    out_blob(&o, st->action[i], ACTION_BYTES);
    out_sized_end(&o, entry);
  }
  out_end(&o, at);

  if (st->have_actors) {
    at = out_begin(&o, SZM_ACTORS);
    uint8_t aflags = st->actors_truncated ? ACTORS_TRUNCATED : 0;
    record = SZM_ACTOR_SIZE;
    OUT(&o, aflags);
    OUT(&o, record);
    for (uint32_t i = 0; i < st->actor_count; i++) {
      const memfile_actor_t *e = &st->actors[i];
      OUT(&o, e->instance_id);
      OUT(&o, e->pos);
      OUT(&o, e->rot);
      OUT(&o, e->life);
      OUT(&o, e->max_life);
      OUT(&o, e->state);
      OUT(&o, e->flags);
      OUT(&o, e->name_hash);
      OUT(&o, e->bb_off);
      OUT(&o, e->bb_len);
    }
    out_end(&o, at);
  }

  if (st->actor_bb && st->actor_bb_size) {
    at = out_begin(&o, SZM_ACTOR_BB);
    out_bytes(&o, st->actor_bb, st->actor_bb_size);
    out_end(&o, at);
  }

  if (st->cuccos && st->cucco_count) {
    at = out_begin(&o, SZM_CUCCO);
    record = SZM_CUCCO_SIZE;
    OUT(&o, record);
    for (uint32_t i = 0; i < st->cucco_count; i++) {
      const memfile_cucco_t *c = &st->cuccos[i];
      uint8_t zero8 = 0;
      uint16_t zero16 = 0;
      OUT(&o, c->instance_id);
      OUT(&o, c->name_hash);
      OUT(&o, c->flags);
      OUT(&o, c->model_fade_active);
      OUT(&o, c->model_vis_flags);
      OUT(&o, zero8);
      OUT(&o, c->active_node);
      OUT(&o, zero16);
      OUT(&o, c->home_pos);
      OUT(&o, c->counter_val);
      OUT(&o, c->air_val);
      OUT(&o, c->landing_water_count);
      OUT(&o, c->model_alpha);
      OUT(&o, c->model_fade_step);
      OUT(&o, c->model_fade_flags);
    }
    out_end(&o, at);
  }

  if (st->held.valid) {
    const memfile_held_t *h = &st->held;
    at = out_begin(&o, SZM_HELD);
    OUT(&o, h->flags);
    OUT(&o, h->instance_id);
    OUT(&o, h->name_hash);
    out_bytes(&o, h->pack_name, HELD_NAME_LEN);
    OUT(&o, h->pos);
    OUT(&o, h->rot);
    OUT(&o, h->life);
    OUT(&o, h->max_life);
    out_blob(&o, h->actor_bb, h->actor_bb ? h->actor_bb_size : 0u);
    out_blob(&o, h->ai_bb, h->ai_bb ? h->ai_bb_size : 0u);
    if (h->flags & HELD_F_CUCCO) {
      OUT(&o, h->cucco.flags);
      OUT(&o, h->cucco.model_fade_active);
      OUT(&o, h->cucco.model_vis_flags);
      OUT(&o, h->cucco.active_node);
      OUT(&o, h->cucco.home_pos);
      OUT(&o, h->cucco.counter_val);
      OUT(&o, h->cucco.air_val);
      OUT(&o, h->cucco.landing_water_count);
      OUT(&o, h->cucco.model_alpha);
      OUT(&o, h->cucco.model_fade_step);
      OUT(&o, h->cucco.model_fade_flags);
    }
    out_end(&o, at);
  }

  if (st->have_vel || st->have_fall || st->have_savepos || st->have_restart || st->have_rng || st->have_void_ring) {
    at = out_begin(&o, SZM_EXTRA);
    uint8_t xflags = (st->have_vel ? EXTRA_VEL : 0) | (st->have_fall ? EXTRA_FALL : 0) |
                     (st->have_savepos ? EXTRA_SAVEPOS : 0) | (st->have_restart ? EXTRA_RESTART : 0) |
                     (st->have_rng ? EXTRA_RNG : 0) | (st->have_void_ring ? EXTRA_VOID_RING : 0);
    OUT(&o, xflags);
    OUT(&o, st->vel);
    OUT(&o, st->fall);
    out_bytes(&o, st->savepos_a, SAVEPOS_SLOT_BYTES);
    out_bytes(&o, st->savepos_b, SAVEPOS_SLOT_BYTES);
    out_bytes(&o, st->player_restart, PLAYER_RESTART_BYTES);
    OUT(&o, st->rng);
    OUT(&o, st->void_ring_hit);
    OUT(&o, st->void_ring_point);
    OUT(&o, st->void_ring_origin);
    OUT(&o, st->void_ring_cell);
    OUT(&o, st->void_ring_face);
    OUT(&o, st->void_ring_phase);
    out_end(&o, at);
  }

  if (!o.ok) {
    free16(o.p);
    return 0;
  }
  szm_header_t h = { 0 };
  memcpy(h.magic, SZM_MAGIC, 4);
  h.format = SZM_FORMAT;
  h.header_size = sizeof(h);
  h.body_size = o.n - (uint32_t)sizeof(h);
  h.body_crc = crc32(o.p + sizeof(h), h.body_size);
  h.child = (int8_t)st->child;
  copy_str(h.game, sizeof(h.game), z64hd_game->version);
  copy_str(h.sloperina, sizeof(h.sloperina), BUILD_VERSION);
  copy_str(h.map, sizeof(h.map), st->map);
  copy_str(h.name, sizeof(h.name), name);
  memcpy(o.p, &h, sizeof(h));
  *size_out = o.n;
  return o.p;
}

typedef struct { const uint8_t *p; uint32_t n, at; uint16_t format; } szm_in_t;

static bool in_bytes(szm_in_t *in, void *b, uint32_t n)
{
  if (in->n - in->at < n)
    return false;
  memcpy(b, in->p + in->at, n);
  in->at += n;
  return true;
}
#define IN(i, v) in_bytes((i), &(v), sizeof(v))

/* { u32 size, bytes } as a reader of its own (the caller's moves past it) */
static bool in_sized(szm_in_t *in, szm_in_t *sub)
{
  uint32_t n;
  if (!IN(in, n) || in->n - in->at < n)
    return false;
  *sub = (szm_in_t){ in->p + in->at, n, 0, in->format };
  in->at += n;
  return true;
}

/* { u32 size, bytes } into want bytes: false if it's shorter (a longer one's rest is a later version's) */
static bool in_blob(szm_in_t *in, void *b, uint32_t want)
{
  szm_in_t blob;
  return in_sized(in, &blob) && in_bytes(&blob, b, want);
}

static bool szm_state(szm_in_t *in, memfile_t *st)
{
  uint8_t flags = 0;
  int32_t child = -1;
  /* format 2's fields: all needed. Fields added later go after them, each read as optional. */
  if (!in_bytes(in, st->start, GD_STR_LEN) || !IN(in, st->x) || !IN(in, st->y) || !IN(in, st->z) ||
      !IN(in, st->fx) || !IN(in, st->fz) || !IN(in, flags) || !IN(in, st->life) || !IN(in, st->magic) ||
      !IN(in, st->nayru_left) || !IN(in, st->nayru_total) || !IN(in, st->time) || !IN(in, child))
    return false;
  st->start[GD_STR_LEN - 1] = 0;
  st->have_pos = flags & STATE_POS;
  st->have_life = flags & STATE_LIFE;
  st->have_magic = flags & STATE_MAGIC;
  st->have_time = (flags & STATE_TIME) && st->time < DAY_SECONDS;
  st->have_nayru = flags & STATE_NAYRU;
  st->child = child == 0 || child == 1 ? child : -1;
  return true;
}

/* the camera's section, or nothing of it (the load then leaves the camera as the map puts it) */
static void szm_camera(szm_in_t *in, memfile_t *st)
{
  uint8_t cam = 0, actions = 0;
  if (!IN(in, cam) || !in_blob(in, st->camera, CAMERA_BYTES) || !in_blob(in, st->comp_head, CAMERA_COMP_HEAD) ||
      !in_blob(in, st->comp_turn, CAMERA_TURN_BYTES) || !IN(in, st->view) || !IN(in, actions) ||
      actions > CAMERA_ACTIONS)
    return;
  for (int i = 0; i < actions; i++) {
    szm_in_t entry;
    if (!in_sized(in, &entry) || !IN(&entry, st->action_vtable[i]) || !IN(&entry, st->action_node_idx[i]) ||
        !in_blob(&entry, st->action[i], ACTION_BYTES))
      return;
  }
  st->camera_actions = actions;
  st->have_camera = cam & CAM_STATE;
  st->have_component = cam & CAM_COMPONENT;
}

/* a record list's records: { u32 record size, records }; 0 records if it's not one this version can read */
static uint32_t in_records(const uint8_t **p, uint32_t size, uint32_t min_record, uint32_t *record)
{
  if (size < 4)
    return 0;
  memcpy(record, *p, 4);
  *p += 4;
  return *record >= min_record ? (size - 4) / *record : 0;
}

static bool szm_scalars(const uint8_t *p, uint32_t size, memfile_t *st)
{
  uint32_t record = 0, count = in_records(&p, size, SZM_SCALAR_SIZE, &record);
  if (!count)
    return true;
  st->s_scalars = alloc16((size_t)count * sizeof(session_scalar_t));
  if (!st->s_scalars)
    return false;
  for (uint32_t i = 0; i < count; i++, p += record) {
    session_scalar_t e;
    memcpy(&e.hash, p, 4);
    e.kind = p[4];
    e.width = p[5];
    memcpy(e.val, p + 6, sizeof(e.val));
    if (e.kind >= K_COUNT || e.kind == K_STR || e.width != kind_width[e.kind])
      continue; /* not one this version restores */
    st->s_scalars[st->s_scalar_count++] = e;
  }
  return true;
}

static bool szm_strings(const uint8_t *p, uint32_t size, memfile_t *st)
{
  uint32_t record = 0, count = in_records(&p, size, SZM_STRING_SIZE, &record);
  if (!count)
    return true;
  st->s_strs = alloc16((size_t)count * sizeof(session_str_t));
  if (!st->s_strs)
    return false;
  for (uint32_t i = 0; i < count; i++, p += record) {
    session_str_t *e = &st->s_strs[st->s_str_count++];
    memcpy(&e->hash, p, 4);
    memcpy(e->val, p + 4, GD_STR_LEN);
    e->val[GD_STR_LEN - 1] = 0;
  }
  return true;
}

static bool szm_actors(const uint8_t *p, uint32_t size, memfile_t *st)
{
  if (size < 5u)
    return true;
  uint8_t aflags = p[0];
  p++;
  size--;
  uint32_t record = 0, count = in_records(&p, size, 66u, &record);
  if (count > MEMFILE_ACTORS_MAX) {
    count = MEMFILE_ACTORS_MAX;
    aflags |= ACTORS_TRUNCATED;
  }
  if (count) {
    st->actors = alloc16((size_t)count * sizeof(memfile_actor_t));
    if (!st->actors)
      return false;
    for (uint32_t i = 0; i < count; i++, p += record) {
      memfile_actor_t e = { 0 };
      memcpy(&e.instance_id, p, 8);
      memcpy(e.pos, p + 8, 12);
      memcpy(e.rot, p + 20, 36);
      memcpy(&e.life, p + 56, 4);
      memcpy(&e.max_life, p + 60, 4);
      e.state = p[64];
      e.flags = p[65];
      if (record >= 70u)
        memcpy(&e.name_hash, p + 66, 4);
      if (record >= 78u) {
        memcpy(&e.bb_off, p + 70, 4);
        memcpy(&e.bb_len, p + 74, 4);
      } else {
        e.flags &= (uint8_t)~ACTOR_F_BB;
      }
      if (e.instance_id && e.instance_id != UINT64_MAX && e.instance_id != 0xFFFFFFFFull)
        st->actors[st->actor_count++] = e;
    }
  }
  st->have_actors = true;
  st->actors_truncated = (aflags & ACTORS_TRUNCATED) != 0;
  return true;
}

static bool szm_cuccos(const uint8_t *p, uint32_t size, memfile_t *st)
{
  uint32_t record = 0, count = in_records(&p, size, SZM_CUCCO_SIZE, &record);
  if (count > MEMFILE_CUCCOS_MAX)
    count = MEMFILE_CUCCOS_MAX;
  if (!count)
    return true;
  st->cuccos = alloc16((size_t)count * sizeof(memfile_cucco_t));
  if (!st->cuccos)
    return false;
  for (uint32_t i = 0; i < count; i++, p += record) {
    memfile_cucco_t c = { 0 };
    memcpy(&c.instance_id, p + 0, 8);
    memcpy(&c.name_hash, p + 8, 4);
    c.flags = p[12];
    c.model_fade_active = p[13];
    c.model_vis_flags = p[14];
    memcpy(&c.active_node, p + 16, 2);
    memcpy(c.home_pos, p + 20, 12);
    memcpy(&c.counter_val, p + 32, 4);
    memcpy(&c.air_val, p + 36, 4);
    memcpy(&c.landing_water_count, p + 40, 4);
    memcpy(&c.model_alpha, p + 44, 4);
    memcpy(&c.model_fade_step, p + 48, 4);
    memcpy(&c.model_fade_flags, p + 52, 4);
    if (c.instance_id && c.instance_id != UINT64_MAX && c.instance_id != 0xFFFFFFFFull)
      st->cuccos[st->cucco_count++] = c;
  }
  return true;
}

static bool szm_held(szm_in_t *in, memfile_t *st)
{
  memfile_held_t h = { 0 };
  szm_in_t abb = { 0 }, aibb = { 0 };
  if (!IN(in, h.flags) || !IN(in, h.instance_id) || !IN(in, h.name_hash) ||
      !in_bytes(in, h.pack_name, HELD_NAME_LEN) || !IN(in, h.pos) || !IN(in, h.rot) ||
      !IN(in, h.life) || !IN(in, h.max_life) || !in_sized(in, &abb) || !in_sized(in, &aibb))
    return true;
  h.pack_name[HELD_NAME_LEN - 1] = 0;
  if (!h.pack_name[0] && !h.instance_id)
    return true;
  if (abb.n > HELD_BB_MAX || aibb.n > HELD_BB_MAX)
    return true;
  if (abb.n) {
    h.actor_bb = alloc16(abb.n);
    if (!h.actor_bb)
      return false;
    memcpy(h.actor_bb, abb.p, abb.n);
    h.actor_bb_size = abb.n;
  }
  if (aibb.n) {
    h.ai_bb = alloc16(aibb.n);
    if (!h.ai_bb) {
      free16(h.actor_bb);
      return false;
    }
    memcpy(h.ai_bb, aibb.p, aibb.n);
    h.ai_bb_size = aibb.n;
  }
  if (h.flags & HELD_F_CUCCO) {
    h.cucco.instance_id = h.instance_id;
    h.cucco.name_hash = h.name_hash;
    h.cucco.active_node = 0xFFFFu;
    h.cucco.air_val = -1.0f;
    if (!IN(in, h.cucco.flags) || !IN(in, h.cucco.model_fade_active) || !IN(in, h.cucco.model_vis_flags) ||
        !IN(in, h.cucco.active_node) || !IN(in, h.cucco.home_pos) || !IN(in, h.cucco.counter_val) ||
        !IN(in, h.cucco.air_val) || !IN(in, h.cucco.landing_water_count) || !IN(in, h.cucco.model_alpha) ||
        !IN(in, h.cucco.model_fade_step) || !IN(in, h.cucco.model_fade_flags))
      h.flags &= (uint8_t)~HELD_F_CUCCO;
  }
  h.valid = true;
  st->held = h;
  return true;
}

static void szm_extra(szm_in_t *in, memfile_t *st)
{
  uint8_t xflags = 0;
  if (!IN(in, xflags))
    return;
  if (IN(in, st->vel) && (xflags & EXTRA_VEL))
    st->have_vel = true;
  if (IN(in, st->fall) && (xflags & EXTRA_FALL))
    st->have_fall = true;
  if (in_bytes(in, st->savepos_a, SAVEPOS_SLOT_BYTES) && in_bytes(in, st->savepos_b, SAVEPOS_SLOT_BYTES) &&
      (xflags & EXTRA_SAVEPOS))
    st->have_savepos = true;
  if (in_bytes(in, st->player_restart, PLAYER_RESTART_BYTES) && (xflags & EXTRA_RESTART))
    st->have_restart = true;
  if (IN(in, st->rng) && (xflags & EXTRA_RNG) && (st->rng[0] | st->rng[1] | st->rng[2] | st->rng[3]))
    st->have_rng = true;
  if (IN(in, st->void_ring_hit) && IN(in, st->void_ring_point) && IN(in, st->void_ring_origin) &&
      IN(in, st->void_ring_cell) && IN(in, st->void_ring_face) && IN(in, st->void_ring_phase) &&
      (xflags & EXTRA_VOID_RING)) {
    st->void_ring_hit &= 1u;
    st->void_ring_phase %= 10u;
    if (!st->void_ring_hit || (finite_vec3(st->void_ring_point) && finite_vec3(st->void_ring_origin)))
      st->have_void_ring = true;
  }
}

static const char szm_no_mem[] = "not enough memory for that memfile";

/* read a .szm file's bytes into st (zeroed; free_memfile it either way): 0, or why it can't be used */
static const char *szm_decode(const uint8_t *p, uint32_t n, memfile_t *st)
{
  szm_header_t h = { 0 };
  if (n < SZM_HEADER_MIN)
    return "that file isn't a memfile";
  memcpy(&h, p, SZM_HEADER_MIN);
  if (!bytes_equal(h.magic, SZM_MAGIC, 4) || h.header_size < SZM_HEADER_MIN || h.header_size > n)
    return "that file isn't a memfile";
  memcpy(&h, p, h.header_size < sizeof(h) ? h.header_size : sizeof(h)); /* an older header: the rest stays 0 */
  if (h.format > SZM_FORMAT)
    return "that memfile needs a newer sloperina";
  if (h.format < SZM_FORMAT_MIN)
    return "that memfile was made by a test build of sloperina";
  h.game[sizeof(h.game) - 1] = 0;
  if (!str_equal(h.game, z64hd_game->version))
    return "that memfile is for another version of the game";
  if (h.body_size != n - h.header_size || crc32(p + h.header_size, h.body_size) != h.body_crc)
    return "that memfile is damaged";

  bool have_state = false, seen_scalars = false, seen_strs = false, seen_actors = false;
  for (uint32_t at = h.header_size; at < n;) {
    uint32_t tag, size;
    if (n - at < 8)
      return "that memfile is damaged";
    memcpy(&tag, p + at, 4);
    memcpy(&size, p + at + 4, 4);
    at += 8;
    if (size > n - at)
      return "that memfile is damaged";
    const uint8_t *body = p + at;
    szm_in_t in = { body, size, 0, h.format };
    at += size;
    if (tag == SZM_GAMEDATA) {
      uint32_t magic = 0;
      if (st->save_buf || size < 16u || size > GD_MAX_SAVE_BUF || (memcpy(&magic, body, 4), magic != GD_SAVE_MAGIC))
        return "that memfile's game data isn't readable";
      if (!(st->save_buf = alloc16(size)))
        return szm_no_mem;
      memcpy(st->save_buf, body, size);
      st->save_cap = st->save_size = size;
    } else if (tag == SZM_SCALARS && !seen_scalars) {
      seen_scalars = true;
      if (!szm_scalars(body, size, st))
        return szm_no_mem;
    } else if (tag == SZM_STRINGS && !seen_strs) {
      seen_strs = true;
      if (!szm_strings(body, size, st))
        return szm_no_mem;
    } else if (tag == SZM_STATE) {
      have_state = szm_state(&in, st);
    } else if (tag == SZM_CAMERA) {
      szm_camera(&in, st);
    } else if (tag == SZM_ACTORS && !seen_actors) {
      seen_actors = true;
      if (!szm_actors(body, size, st))
        return szm_no_mem;
    } else if (tag == SZM_ACTOR_BB && !st->actor_bb) {
      if (size && size <= MEMFILE_BB_POOL_MAX) {
        if (!(st->actor_bb = alloc16(size)))
          return szm_no_mem;
        memcpy(st->actor_bb, body, size);
        st->actor_bb_size = size;
      }
    } else if (tag == SZM_CUCCO && !st->cuccos) {
      if (!szm_cuccos(body, size, st))
        return szm_no_mem;
    } else if (tag == SZM_HELD && !st->held.valid) {
      if (!szm_held(&in, st))
        return szm_no_mem;
    } else if (tag == SZM_EXTRA) {
      szm_extra(&in, st);
    }
  }
  if (!st->save_buf || !have_state)
    return "that memfile is damaged";
  copy_str(st->map, sizeof(st->map), h.map);
  st->map[GD_STR_LEN - 1] = 0;
  if (!st->map[0])
    return "that memfile is damaged";
  st->used = true;
  log_printf("memfile file: \"%.32s\" for game %s, made by sloperina %.32s (format %u)", /* not menu text */
             h.name, h.game, h.sloperina, (unsigned)h.format);
  return 0;
}

/* --- sharing: the SD card ---------------------------------------------------------------------- */

typedef struct { void *handle; } fs_dir_t;
typedef struct {
  char    name[769];
  uint8_t pad0[3];
  int8_t  type; /* 0 directory, 1 file */
  uint8_t pad1[3];
  int64_t size;
} fs_dir_entry_t;
enum { FS_ENTRY_FILE = 1, FS_OPEN_DIR_ALL = 3 };
typedef nn_result_t (*open_dir_fn)(fs_dir_t *out, const char *path, int mode);
typedef nn_result_t (*read_dir_fn)(int64_t *count, fs_dir_entry_t *entries, fs_dir_t dir, int64_t max);
typedef void (*close_dir_fn)(fs_dir_t dir);

typedef nn_result_t (*delete_tree_fn)(const char *path);
enum { FS_ENTRY_DIR = 0 };

/* The folder looked at (relative to SZM_DIR, "" for it: the top, which up stops at; folders in folders to
 * MEMFILE_FOLDER_LEN), and the last list of it: its folders first, then its .szm files (without the extension),
 * each A to Z. Exports go into it and imports come from it. */
static char browsing[MEMFILE_FOLDER_LEN];
static char file_names[MEMFILE_FILES_MAX][MEMFILE_NAME_MAX + 1];
static int  file_count, folder_count;

/* the sdk's directory functions, looked up in its exports (as savebackup.c's) */
static struct {
  open_dir_fn    open;
  read_dir_fn    read;
  close_dir_fn   close;
  delete_tree_fn delete_tree;
} dirs;

static bool dir_fns(void)
{
  if (!dirs.open) {
    uintptr_t sdk = got_module_of((uintptr_t)&fs_open); /* an sdk function this module imports */
    dirs.read = (read_dir_fn)got_export(sdk, "_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl");
    dirs.close = (close_dir_fn)got_export(sdk, "_ZN2nn2fs14CloseDirectoryENS0_15DirectoryHandleE");
    dirs.delete_tree = (delete_tree_fn)got_export(sdk, "_ZN2nn2fs26DeleteDirectoryRecursivelyEPKc");
    dirs.open = (open_dir_fn)got_export(sdk, "_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci");
  }
  return dirs.open && dirs.read && dirs.close;
}

/* SZM_DIR/<folder>[/<name><ext>] */
static void szm_path(char out[SZM_PATH_LEN], const char *folder, const char *name, const char *ext)
{
  int n = snprintf(out, SZM_PATH_LEN, "%s%s%s", SZM_DIR, folder[0] ? "/" : "", folder);
  if (name)
    snprintf(out + n, SZM_PATH_LEN - n, "/%s%s", name, ext);
}

static bool dir_exists(const char *path)
{
  fs_dir_t d;
  if (!dir_fns() || dirs.open(&d, path, FS_OPEN_DIR_ALL) != 0)
    return false;
  dirs.close(d);
  return true;
}

/* the name typed, as a file name: no characters the SD card can't hold, no spaces or dots at either end */
static void clean_file_name(const char *typed, char out[MEMFILE_NAME_MAX + 1])
{
  int n = 0;
  while (typed && *typed == ' ')
    typed++;
  for (; typed && *typed && n < MEMFILE_NAME_MAX; typed++) {
    char c = *typed;
    out[n++] = (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
                c == '|' || (uint8_t)c < 0x20) ? '_' : c;
  }
  while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.'))
    n--;
  out[n] = 0;
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

static int compare_names(const char *a, const char *b)
{
  while (*a && lower(*a) == lower(*b))
    a++, b++;
  return (uint8_t)lower(*a) - (uint8_t)lower(*b);
}

bool memfile_used(int s) { return s >= 0 && s < MEMFILE_SLOTS && memfiles[s].used; }
const char *memfile_slot_map(int s) { return memfile_used(s) ? memfiles[s].map : ""; }

bool memfile_export(int s, const char *typed)
{
  if (!memfile_used(s) || !memfiles[s].save_buf) {
    gfx_notice("that memfile is empty");
    return false;
  }
  retire_inflight_buf();
  if (memfile_busy() || inflight_save_buf) {
    gfx_notice("memfile is busy");
    return false;
  }
  if (!settings_sd()) {
    gfx_notice("no SD card: memfiles can't be exported");
    return false;
  }
  char name[MEMFILE_NAME_MAX + 1], path[SZM_PATH_LEN];
  clean_file_name(typed, name);
  if (!name[0]) {
    gfx_notice("a memfile needs a name to export");
    return false;
  }
  szm_path(path, browsing, name, MEMFILE_EXT);
  fs_create_dir(SD_DIR);
  fs_create_dir(SZM_DIR);
  fs_file_t f;
  if (fs_open(&f, path, FS_READ) == 0) {
    fs_close(f);
    gfx_logf("%s is already a memfile on the SD card", name);
    return false;
  }
  uint32_t size = 0;
  uint8_t *bytes = szm_encode(&memfiles[s], name, &size);
  if (!bytes) {
    gfx_notice("not enough memory to export that memfile");
    return false;
  }
  fs_write_option_t flush = { FS_WRITE_FLUSH };
  bool ok = fs_create_file(path, size) == 0;
  if (ok) {
    ok = fs_open(&f, path, FS_WRITE) == 0;
    if (ok) {
      ok = fs_write(f, 0, bytes, size, &flush) == 0;
      fs_close(f);
    }
    if (!ok)
      fs_delete_file(path);
  }
  free16(bytes);
  if (!ok) {
    gfx_notice("couldn't write the memfile to the SD card");
    return false;
  }
  log_printf("memfile %d exported: %s (%u bytes)", s + 1, path, (unsigned)size); /* not menu text */
  gfx_logf(TF("memfile %d exported as %s"), s + 1, name);
  return true;
}

int memfile_list_files(void)
{
  file_count = folder_count = 0;
  char path[SZM_PATH_LEN];
  szm_path(path, browsing, 0, 0);
  fs_dir_t dir;
  if (!settings_sd() || !dir_fns() || dirs.open(&dir, path, FS_OPEN_DIR_ALL) != 0)
    return 0;
  static fs_dir_entry_t e;
  int64_t got = 0;
  const size_t ext = sizeof(MEMFILE_EXT) - 1;
  while (file_count < MEMFILE_FILES_MAX && dirs.read(&got, &e, dir, 1) == 0 && got == 1) {
    e.name[sizeof(e.name) - 1] = 0;
    size_t n = strlen(e.name);
    bool folder = e.type == FS_ENTRY_DIR;
    if (folder ? n > MEMFILE_NAME_MAX
               : e.type != FS_ENTRY_FILE || n <= ext || n - ext > MEMFILE_NAME_MAX ||
                   compare_names(e.name + n - ext, MEMFILE_EXT) != 0)
      continue;
    if (!folder)
      n -= ext;
    /* sorted as they come in: a folder among the folders (the list's start), a file among the files */
    int lo = folder ? 0 : folder_count, i = folder ? folder_count : file_count;
    for (int k = file_count; k > i; k--)
      memcpy(file_names[k], file_names[k - 1], sizeof(file_names[k]));
    file_count++;
    folder_count += folder;
    char name[MEMFILE_NAME_MAX + 1];
    memcpy(name, e.name, n);
    name[n] = 0;
    for (; i > lo && compare_names(file_names[i - 1], name) > 0; i--)
      memcpy(file_names[i], file_names[i - 1], sizeof(file_names[i]));
    memcpy(file_names[i], name, sizeof(name));
  }
  dirs.close(dir);
  return file_count;
}

int memfile_list_folders(void) { return folder_count; }

const char *memfile_file_name(int i) { return i >= 0 && i < file_count ? file_names[i] : ""; }

const char *memfile_browsing(void) { return browsing; }

bool memfile_browse_into(int i)
{
  if (i < 0 || i >= folder_count)
    return false;
  char next[MEMFILE_FOLDER_LEN];
  int n = snprintf(next, sizeof(next), browsing[0] ? "%s/%s" : "%s%s", browsing, file_names[i]);
  if (n >= (int)sizeof(next)) {
    gfx_notice("that folder is too deep to open here");
    return false;
  }
  memcpy(browsing, next, sizeof(browsing));
  return true;
}

void memfile_browse_up(void)
{
  int n = (int)strlen(browsing);
  while (n > 0 && browsing[n - 1] != '/')
    n--;
  browsing[n > 0 ? n - 1 : 0] = 0;
}

void memfile_browse_top(void) { browsing[0] = 0; }

bool memfile_delete_file(int i)
{
  if (i < folder_count || i >= file_count)
    return false;
  if (!settings_sd()) {
    gfx_notice("no SD card: memfile not deleted");
    return false;
  }
  char path[SZM_PATH_LEN], name[MEMFILE_NAME_MAX + 1];
  szm_path(path, browsing, file_names[i], MEMFILE_EXT);
  memcpy(name, file_names[i], sizeof(name));
  nn_result_t r = fs_delete_file(path);
  if (r != 0) {
    log_printf("memfiles: couldn't delete %s (0x%x)", path, (unsigned)r); /* not menu text */
    gfx_notice("memfile not deleted (see the log)");
    return false;
  }
  log_printf("memfiles: deleted %s", path); /* not menu text */
  gfx_logf(TF("deleted %s"), name);
  memfile_list_files(); /* the list without it */
  return true;
}

bool memfile_make_folder(const char *typed)
{
  if (!settings_sd() || !dir_fns()) {
    gfx_notice("no SD card: no folder made");
    return false;
  }
  char name[MEMFILE_NAME_MAX + 1], path[SZM_PATH_LEN];
  clean_file_name(typed, name);
  if (!name[0]) {
    gfx_notice("no name: no folder made");
    return false;
  }
  if (strlen(browsing) + 1 + strlen(name) >= MEMFILE_FOLDER_LEN) {
    gfx_notice("that folder would be too deep");
    return false;
  }
  szm_path(path, browsing, name, "");
  if (dir_exists(path)) {
    gfx_logf(TF("%s is already a folder here"), name);
    return false;
  }
  fs_create_dir(SD_DIR);
  fs_create_dir(SZM_DIR);
  fs_create_dir(path);
  if (!dir_exists(path)) {
    log_printf("memfiles: couldn't make the folder %s", path); /* not menu text */
    gfx_notice("folder not made (see the log)");
    return false;
  }
  log_printf("memfiles: made the folder %s", path); /* not menu text */
  gfx_logf(TF("folder made: %s"), name);
  return true;
}

bool memfile_delete_folder(void)
{
  if (!browsing[0]) {
    gfx_notice("the top folder can't be deleted");
    return false;
  }
  if (!settings_sd() || !dir_fns() || !dirs.delete_tree) {
    gfx_notice("no SD card: folder not deleted");
    return false;
  }
  char path[SZM_PATH_LEN];
  szm_path(path, browsing, 0, 0);
  nn_result_t r = dirs.delete_tree(path);
  if (r != 0) {
    log_printf("memfiles: couldn't delete the folder %s (0x%x)", path, (unsigned)r); /* not menu text */
    gfx_notice("folder not deleted (see the log)");
    return false;
  }
  log_printf("memfiles: deleted the folder %s", path); /* not menu text */
  memfile_browse_up();
  gfx_notice("folder deleted");
  return true;
}

bool memfile_import(int i, int s)
{
  if (i < folder_count || i >= file_count || s < 0 || s >= MEMFILE_SLOTS)
    return false;
  end_camera_guard();
  retire_inflight_buf();
  if (phase != L_IDLE || inflight_save_buf) {
    gfx_notice("memfile is busy");
    return false;
  }
  if (!settings_sd()) {
    gfx_notice("no SD card: memfiles can't be imported");
    return false;
  }
  char path[SZM_PATH_LEN];
  szm_path(path, browsing, file_names[i], MEMFILE_EXT);
  fs_file_t f;
  int64_t size = 0;
  if (fs_open(&f, path, FS_READ) != 0) {
    gfx_notice("couldn't open that memfile");
    return false;
  }
  uint8_t *bytes = 0;
  size_t got = 0;
  bool ok = fs_size(&size, f) == 0 && size > 0 && size <= (int64_t)SZM_FILE_MAX &&
            (bytes = alloc16((size_t)size)) != 0 && fs_read(&got, f, 0, bytes, (size_t)size) == 0 &&
            got == (size_t)size;
  fs_close(f);
  if (!ok) {
    free16(bytes);
    gfx_notice(size > (int64_t)SZM_FILE_MAX ? "that file isn't a memfile" : "couldn't read that memfile");
    return false;
  }
  static memfile_t loaded;
  loaded = (memfile_t){ 0 };
  const char *why = szm_decode(bytes, (uint32_t)size, &loaded);
  if (why == szm_no_mem && memfiles[s].used) {
    free_memfile(&loaded);
    if (pool_would_fit_reclaiming((size_t)size + 16384u, &memfiles[s])) {
      free_memfile(&memfiles[s]);
      why = szm_decode(bytes, (uint32_t)size, &loaded);
    }
  }
  free16(bytes);
  if (why) {
    if (why == szm_no_mem)
      log_printf("memfile %d import: out of pool memory for %u-byte file (%u KiB free)", /* not menu text */
                 s + 1, (unsigned)size, (unsigned)(pool_free_bytes() >> 10));
    free_memfile(&loaded);
    gfx_notice(why);
    return false;
  }
  free_memfile(&memfiles[s]);
  memfiles[s] = loaded;
  loaded = (memfile_t){ 0 };
  log_printf("memfile %d imported: %s (%s)", s + 1, path, memfiles[s].map); /* not menu text */
  gfx_logf("%s imported to memfile %d", file_names[i], s + 1);
  return true;
}

/* --- menu accessors ---------------------------------------------------------------------------- */

int  memfile_slot(void) { return slot; }
void memfile_set_slot(int s) { if (s >= 0 && s < MEMFILE_SLOTS) slot = s; }
bool memfile_busy(void) { return phase != L_IDLE && !(phase == L_CAMERA && camera_guard); }
bool memfile_loading(void) { return phase >= L_CANCEL_EVENT && !(phase == L_CAMERA && camera_guard); }

void memfile_binds_frame(bool menu_open)
{
  if (menu_open)
    return;
  if (cmd_pressed(CMD_MEMFILE_SAVE)) {
    memfile_save(slot);
  } else if (cmd_pressed(CMD_MEMFILE_LOAD)) {
    memfile_load(slot);
  } else if (cmd_pressed(CMD_MEMFILE_NEXT) || cmd_pressed(CMD_MEMFILE_PREV)) {
    int step = cmd_pressed(CMD_MEMFILE_NEXT) ? 1 : MEMFILE_SLOTS - 1;
    slot = (slot + step) % MEMFILE_SLOTS;
    char buf[64];
    memfile_slot_text(slot, buf, sizeof(buf));
    gfx_logf(TF("memfile slot %s"), buf);
  }
}

void memfile_slot_text(int s, char *buf, int len)
{
  if (s < 0 || s >= MEMFILE_SLOTS || !memfiles[s].used) {
    snprintf(buf, len, TF("%d: empty"), s + 1);
    return;
  }
  const memfile_t *st = &memfiles[s];
  snprintf(buf, len, "%d: %s%s", s + 1, st->map,
           st->child == 1 ? T(" (child)") : st->child == 0 ? T(" (adult)") : "");
}

const char *memfile_status(void) { return status; }
