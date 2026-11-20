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

void *aligned_alloc(size_t align, size_t size); /* from main */
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
#define GD_SAVE_MAGIC       0x01020304u
#define GD_MAX_SAVE_BUF     (4u << 20)

/* --- timings & limits -------------------------------------------------------------------------- */

#define SAVE_REQ_TIMEOUT   60
#define SAVE_WAIT_TIMEOUT  180
#define GD_LOAD_TIMEOUT    120
#define FAST_MOVE_TIMEOUT  15
#define FAST_MOVE_RETRY    3
#define RESET_TIMEOUT      180
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

typedef struct {
  bool              used;
  uint8_t          *save_buf;
  uint32_t          save_cap, save_size;
  session_scalar_t *s_scalars;
  uint32_t          s_scalar_count;
  session_str_t    *s_strs;
  uint32_t          s_str_count;
  char              map[GD_STR_LEN], start[GD_STR_LEN];
  double            x, y, z, fx, fz;
  bool              have_pos, have_life, have_magic, have_time;
  int32_t           life, magic;
  bool              have_nayru;
  float             nayru_left, nayru_total; /* Nayru's Love: frames left and its length; 0 when off */
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

static enum {
  L_IDLE,
  L_SAVING_REQ,
  L_SAVING_WAIT,
  L_FAST_WAIT_GD,
  L_FAST_MOVE_PRE,
  L_FAST_WAIT_RESET,
  L_WAIT_LOADING,
  L_FULL_WAIT_GD,
  L_WAIT_ARRIVAL,
  L_WAIT_SETTLE,
  L_MOVING,
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

/* --- small helpers ----------------------------------------------------------------------------- */

static void *alloc16(size_t n)
{
  size_t sz = n ? ((n + 15u) & ~(size_t)15u) : 16u;
  return aligned_alloc(16, sz);
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
  free(st->save_buf);
  free(st->s_scalars);
  free(st->s_strs);
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
    free(inflight_save_buf);
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

static bool capture_session(memfile_t *st)
{
  st->s_scalars = alloc16((size_t)(memfilefields_count > 0 ? memfilefields_count : 1) * sizeof(session_scalar_t));
  st->s_strs = alloc16(16 * sizeof(session_str_t));
  if (!st->s_scalars || !st->s_strs)
    return false;
  uint32_t str_cap = 16;
  for (int k = 0; k < K_COUNT; k++) {
    uint32_t stride, value_off;
    gd_type_t *t = kind_type(k, &stride, &value_off);
    if (!t) {
      if (k == K_UINT || k == K_VEC3)
        continue;
      return false;
    }
    for (uint32_t s_i = 0; s_i < t->table_size; s_i++) {
      uint32_t hash = t->table[2 * s_i], idx = t->table[2 * s_i + 1];
      if (!hash || idx >= t->count || save_file_of(t, stride, idx) != -1 || !is_session_field(hash))
        continue;
      const uint8_t *v = value_at(k, t, stride, value_off, idx);
      if (!v)
        continue;
      if (k == K_STR) {
        if (st->s_str_count >= str_cap) {
          uint32_t ncap = str_cap * 2;
          session_str_t *nb = alloc16((size_t)ncap * sizeof(session_str_t));
          if (!nb)
            return false;
          memcpy(nb, st->s_strs, (size_t)st->s_str_count * sizeof(session_str_t));
          free(st->s_strs);
          st->s_strs = nb;
          str_cap = ncap;
        }
        session_str_t *e = &st->s_strs[st->s_str_count++];
        e->hash = hash;
        memcpy(e->val, v, GD_STR_LEN);
        e->val[GD_STR_LEN - 1] = 0;
      } else if ((int)st->s_scalar_count < memfilefields_count) {
        session_scalar_t *e = &st->s_scalars[st->s_scalar_count++];
        e->hash = hash;
        e->kind = (uint8_t)k;
        e->width = kind_width[k];
        memset(e->val, 0, sizeof(e->val));
        memcpy(e->val, v, e->width);
      }
    }
  }
  return true;
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
 * The chain (the same offsets in both builds; FINDINGS 9.14): the scene's component 0 is Sequence; [+0xE8] lists
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
 * 22, as the game's own getter reads it (1.0.0 main+0x27C2AD0, 1.1.1 main+0x28147E0; FINDINGS 9.16). */
#define SCENE_CURRENT    0x1F0 /* [scene manager +this]: the current scene */
#define SCENE_COUNT      0x50  /* u32 */
#define SCENE_COMPONENTS 0x58  /* pointer to the components' pointers */
#define SCENE_WARP_MGR   22

/* a WarpMgr as its constructor (main+0xE0FE60) leaves it: its vtable, and a listener's vtable at +0xB8 */
static bool is_warp_mgr(uintptr_t m, uintptr_t vt)
{
  if (!sys_mapped(m, 0xD0, SYS_PERM_R | SYS_PERM_W) || sys_ptr(m) != vt)
    return false;
  uintptr_t listener = sys_ptr(m + 0xB8);
  return listener >= MAIN(0) && listener < MAIN(0x5000000);
}

typedef void (*gd_changed_t)(uintptr_t component, const void *change_bits);
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

static void finish_load(void)
{
  if (fast_load) {
    refresh_warp_mgr(&memfiles[active_slot]);
  }
  gfx_notice("memfile loaded");
  status[0] = 0;
  set_phase(L_IDLE);
}

static void begin_move_or_camera(const memfile_t *st)
{
  if (st->have_pos && move_link(st)) {
    set_phase(L_MOVING);
  } else if (st->have_pos) {
    log_printf("memfile %d load: Link couldn't be moved to its position", active_slot + 1); /* not menu text */
    gfx_notice("memfile loaded, but Link couldn't be moved to its position");
    status[0] = 0;
    set_phase(L_IDLE);
  } else if (st->have_camera) {
    camera_frames = camera_guard = 0;
    set_phase(L_CAMERA);
  } else {
    log_printf("memfile %d loaded (no position)", active_slot + 1); /* not menu text */
    finish_load();
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
  staging.save_size = written;
  staging.used = true;
  free_memfile(&memfiles[active_slot]);
  memfiles[active_slot] = staging;
  staging = (memfile_t){ 0 };
  status[0] = 0;
  set_phase(L_IDLE);
  const memfile_t *st = &memfiles[active_slot];
  log_printf("memfile %d saved: %s at \"%s\", %.2f %.2f %.2f facing %.3f,%.3f, life %d, magic %d, time %02u:%02u, "
             "%u bytes binary, %u scalars + %u strings session", /* not menu text */
             active_slot + 1, st->map, st->start, st->x, st->y, st->z, st->fx, st->fz, (int)st->life, (int)st->magic,
             st->have_time ? (unsigned)(st->time / 3600) : 99u, st->have_time ? (unsigned)(st->time / 60 % 60) : 99u,
             (unsigned)st->save_size, (unsigned)st->s_scalar_count, (unsigned)st->s_str_count);
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
  staging.save_buf = alloc16(buf_size);
  staging.save_cap = buf_size;
  if (!staging.save_buf || !capture_session(&staging)) {
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
  if (in_cutscene()) {
    gfx_notice("can't load a memfile during a cutscene");
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
      restore_vitals_and_gear(st);
      if (st->have_pos && move_link(st)) {
        snprintf(status, sizeof(status), TF("loading memfile %d: placing Link"), active_slot + 1);
        set_phase(L_FAST_MOVE_PRE);
      } else {
        gd_poke_int(F_RESET_GIMMICK_OPT, 0);
        gd_set_int(F_RESET_GIMMICK_OPT, 0);
        patch_main_module();
        gd_set_bool(F_REQ_RESET_GIMMICK, true);
        saw_reset_req = false;
        set_phase(L_FAST_WAIT_RESET);
      }
      break;
    case L_FAST_MOVE_PRE:
      if (link_at(st, phase_frames) || phase_frames >= FAST_MOVE_TIMEOUT) {
        gd_poke_int(F_RESET_GIMMICK_OPT, 0);
        gd_set_int(F_RESET_GIMMICK_OPT, 0);
        patch_main_module();
        gd_set_bool(F_REQ_RESET_GIMMICK, true);
        saw_reset_req = false;
        set_phase(L_FAST_WAIT_RESET);
      } else if (phase_frames % FAST_MOVE_RETRY == 0) {
        move_link(st);
      }
      break;
    case L_FAST_WAIT_RESET: {
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
      set_phase(L_WAIT_ARRIVAL);
      break;
    case L_WAIT_ARRIVAL: {
      const char *map = gd_get_str(GD_STR_MAP);
      if (!loading && map && str_equal(map, st->map) && link_actor() && (link_actor() != old_actor || saw_unload)) {
        clear_overrides();
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
      cct_clear_fall_distance(link_actor());
      double fx = 0, fz = 0;
      link_facing(&fx, &fz);
      log_printf("memfile %d loaded (Link placed after %d frames, facing %.3f,%.3f, saved %.3f,%.3f)",
                 active_slot + 1, phase_frames, fx, fz, st->fx, st->fz); /* not menu text */
      if (st->have_camera) {
        camera_frames = camera_guard = 0;
        set_phase(L_CAMERA);
      } else {
        finish_load();
      }
    } else if (phase_frames > MOVE_TIMEOUT) {
      fail("memfile loaded, but Link couldn't be moved to its position");
    } else if (phase_frames % MOVE_RETRY_EVERY == 0) {
      move_link(st);
    }
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
  if (phase == L_IDLE)
    return;
  phase_frames++;

  uint8_t *mgr = 0;
  uint8_t *desc0 = gd_file0_desc(&mgr, 0);
  if (phase == L_SAVING_REQ || phase == L_SAVING_WAIT) {
    step_saving(mgr, desc0);
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
#define SZM_SCALAR_SIZE 18u /* format 2's records; a file's own are at least this */
#define SZM_STRING_SIZE (4u + GD_STR_LEN)
#define SZM_FILE_MAX    (GD_MAX_SAVE_BUF + (1u << 20))
#define SZM_SLACK       0x10000u /* room for every section but the snapshot and the session fields */
#define SZM_DIR         SD_DIR "/" MEMFILE_DIR_NAME
#define SZM_PATH_LEN    (sizeof(SZM_DIR) + MEMFILE_FOLDER_LEN + MEMFILE_NAME_MAX + 8)

enum { STATE_POS = 1, STATE_LIFE = 2, STATE_MAGIC = 4, STATE_TIME = 8, STATE_NAYRU = 16 };
enum { CAM_STATE = 1, CAM_COMPONENT = 2 };

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

/* the memfile as a .szm file's bytes (heap; the caller frees), or 0 */
static uint8_t *szm_encode(const memfile_t *st, const char *name, uint32_t *size_out)
{
  uint32_t cap = (uint32_t)sizeof(szm_header_t) + st->save_size + st->s_scalar_count * SZM_SCALAR_SIZE +
                 st->s_str_count * SZM_STRING_SIZE + CAMERA_ACTIONS * (16u + ACTION_BYTES) + SZM_SLACK;
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

  if (!o.ok) {
    free(o.p);
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
  st->s_scalars = alloc16((size_t)(count ? count : 1) * sizeof(session_scalar_t));
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
  st->s_strs = alloc16((size_t)(count ? count : 1) * sizeof(session_str_t));
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

  bool have_state = false;
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
        return "not enough memory for that memfile";
      memcpy(st->save_buf, body, size);
      st->save_cap = st->save_size = size;
    } else if (tag == SZM_SCALARS && !st->s_scalars) {
      if (!szm_scalars(body, size, st))
        return "not enough memory for that memfile";
    } else if (tag == SZM_STRINGS && !st->s_strs) {
      if (!szm_strings(body, size, st))
        return "not enough memory for that memfile";
    } else if (tag == SZM_STATE) {
      have_state = szm_state(&in, st);
    } else if (tag == SZM_CAMERA) {
      szm_camera(&in, st);
    }
  }
  if (!st->save_buf || !have_state)
    return "that memfile is damaged";
  if ((!st->s_scalars && !szm_scalars(p, 0, st)) || (!st->s_strs && !szm_strings(p, 0, st)))
    return "not enough memory for that memfile";
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
  if (memfile_busy()) {
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
  free(bytes);
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
  if (phase != L_IDLE) {
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
    free(bytes);
    gfx_notice(size > (int64_t)SZM_FILE_MAX ? "that file isn't a memfile" : "couldn't read that memfile");
    return false;
  }
  static memfile_t loaded;
  loaded = (memfile_t){ 0 };
  const char *why = szm_decode(bytes, (uint32_t)size, &loaded);
  free(bytes);
  if (why) {
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
