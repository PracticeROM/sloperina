/* Host unit tests for memfile.c (no game process). */
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "z64hd.h"

/* Intercept MAIN_FN calls from memfile.c so game function offsets dispatch to test doubles. */
static bool      fake_gd_req_save(void *mgr, void *buf, int file_idx);
static bool      fake_gd_req_load(void *mgr, const void *buf, int file_idx);
static void      fake_wm_set_time(uintptr_t wm, uint32_t seconds);
static void      fake_wm_on_save(uintptr_t mgr);
static void      fake_wm_on_load(uintptr_t mgr);
static uintptr_t fake_navi_get(uintptr_t scene);
static void      fake_navi_on_load(uintptr_t mgr);
static void      fake_camera_snap(uintptr_t component);
static void      fake_camera_arrive(uintptr_t component);
static bool      fake_actor_force_set_pos(uintptr_t phys, const float pos[3], const float rot[9], bool immediate);
static void      fake_actor_request_delete(uintptr_t actor, uint32_t reason);
static void      fake_preactor_update_flags(void *node);
static void      fake_actor_link_set(void *link, uintptr_t actor, uint16_t flags);
static void      fake_actor_link_dtor(void *link);
static bool      fake_lifter_drop(uintptr_t lifter, int reason, float s0, float s1);
static bool      fake_lifter_force_lift(uintptr_t lifter, void *link, const char *const *key, bool flag);
static void      fake_player_lift_anim(uintptr_t link_actor, void *link);
static int       fake_bsa_set_action_verb(uintptr_t bsa, const void *req, bool flag);

static struct z64hd_build test_build;
const struct z64hd_build *z64hd_game = &test_build;
uintptr_t z64hd_main;

#define OFF_GD_REQ_SAVE       0x1100u
#define OFF_GD_REQ_LOAD       0x1200u
#define OFF_WM_SET_TIME       0x1300u
#define OFF_WM_ON_SAVE        0x1400u
#define OFF_WM_ON_LOAD        0x1500u
#define OFF_NAVI_GET          0x1600u
#define OFF_NAVI_ON_LOAD      0x1700u
#define OFF_CAMERA_SNAP       0x1800u
#define OFF_CAMERA_ARRIVE     0x1900u
#define OFF_ACTOR_FORCE_POS   0x1A00u
#define OFF_ACTOR_REQ_DELETE  0x1B00u
#define OFF_PREACTOR_UPDATE   0x1C00u
#define OFF_ACTOR_LINK_SET    0x1D00u
#define OFF_ACTOR_LINK_DTOR   0x1E00u
#define OFF_LIFTER_DROP       0x1F00u
#define OFF_LIFTER_FORCE_LIFT 0x2000u
#define OFF_PLAYER_LIFT_ANIM  0x2100u
#define OFF_BSA_SET_VERB      0x2200u

static uintptr_t function_at(uint32_t off)
{
  if (off == OFF_GD_REQ_SAVE) return (uintptr_t)fake_gd_req_save;
  if (off == OFF_GD_REQ_LOAD) return (uintptr_t)fake_gd_req_load;
  if (off == OFF_WM_SET_TIME) return (uintptr_t)fake_wm_set_time;
  if (off == OFF_WM_ON_SAVE) return (uintptr_t)fake_wm_on_save;
  if (off == OFF_WM_ON_LOAD) return (uintptr_t)fake_wm_on_load;
  if (off == OFF_NAVI_GET) return (uintptr_t)fake_navi_get;
  if (off == OFF_NAVI_ON_LOAD) return (uintptr_t)fake_navi_on_load;
  if (off == OFF_CAMERA_ARRIVE) return (uintptr_t)fake_camera_arrive;
  if (off == OFF_ACTOR_FORCE_POS) return (uintptr_t)fake_actor_force_set_pos;
  if (off == OFF_ACTOR_REQ_DELETE) return (uintptr_t)fake_actor_request_delete;
  if (off == OFF_PREACTOR_UPDATE) return (uintptr_t)fake_preactor_update_flags;
  if (off == OFF_ACTOR_LINK_SET) return (uintptr_t)fake_actor_link_set;
  if (off == OFF_ACTOR_LINK_DTOR) return (uintptr_t)fake_actor_link_dtor;
  if (off == OFF_LIFTER_DROP) return (uintptr_t)fake_lifter_drop;
  if (off == OFF_LIFTER_FORCE_LIFT) return (uintptr_t)fake_lifter_force_lift;
  if (off == OFF_PLAYER_LIFT_ANIM) return (uintptr_t)fake_player_lift_anim;
  if (off == OFF_BSA_SET_VERB) return (uintptr_t)fake_bsa_set_action_verb;
  assert(off == OFF_CAMERA_SNAP);
  return (uintptr_t)fake_camera_snap;
}

#undef MAIN_FN
#define MAIN_FN(type, off) ((type)function_at(off))

#include "../src/ootgz/memfile.c"


/* --- a simulated SD card: folders and files, and a folder's listing -------------------------------- */

#define SD_FILES 16
#define SD_DIRS  16
static struct { char path[256]; bool used; } sd_dirs[SD_DIRS];
static char sd_list_path[256];
static struct { char path[256]; uint8_t *data; size_t size; bool used; } sd_files[SD_FILES];
static bool sd_mounted = true;
static int  sd_list_at; /* the listing's next entry: folders, then files */

bool settings_sd(void) { return sd_mounted; }

static int sd_find(const char *path)
{
  for (int i = 0; i < SD_FILES; i++)
    if (sd_files[i].used && !strcmp(sd_files[i].path, path))
      return i;
  return -1;
}

static int sd_find_dir(const char *path)
{
  for (int i = 0; i < SD_DIRS; i++)
    if (sd_dirs[i].used && !strcmp(sd_dirs[i].path, path))
      return i;
  return -1;
}

/* is path directly in dir? */
static bool sd_in(const char *path, const char *dir)
{
  const char *slash = strrchr(path, '/');
  size_t n = strlen(dir);
  return slash && (size_t)(slash - path) == n && !strncmp(path, dir, n);
}

/* is path dir or in it, at any depth? */
static bool sd_under(const char *path, const char *dir)
{
  size_t n = strlen(dir);
  return !strncmp(path, dir, n) && (path[n] == 0 || path[n] == '/');
}

nn_result_t fs_create_dir(const char *path)
{
  if (sd_find_dir(path) >= 0)
    return 1; /* already there, as nn::fs says */
  for (int i = 0; i < SD_DIRS; i++) {
    if (sd_dirs[i].used)
      continue;
    snprintf(sd_dirs[i].path, sizeof(sd_dirs[i].path), "%s", path);
    sd_dirs[i].used = true;
    return 0;
  }
  return 1;
}

static void sd_clear(void)
{
  for (int i = 0; i < SD_FILES; i++)
    if (sd_files[i].used)
      fs_delete_file(sd_files[i].path);
  memset(sd_dirs, 0, sizeof(sd_dirs));
}
nn_result_t fs_create_file(const char *path, int64_t size)
{
  if (sd_find(path) >= 0)
    return 1;
  for (int i = 0; i < SD_FILES; i++) {
    if (sd_files[i].used)
      continue;
    snprintf(sd_files[i].path, sizeof(sd_files[i].path), "%s", path);
    sd_files[i].data = calloc(1, (size_t)size + 1);
    sd_files[i].size = (size_t)size;
    sd_files[i].used = true;
    return 0;
  }
  return 1;
}
nn_result_t fs_delete_file(const char *path)
{
  int i = sd_find(path);
  if (i < 0)
    return 1;
  free(sd_files[i].data);
  sd_files[i] = (typeof(sd_files[i])){ 0 };
  return 0;
}
nn_result_t fs_open(fs_file_t *out, const char *path, int mode)
{
  (void)mode;
  int i = sd_find(path);
  if (i < 0)
    return 1;
  out->handle = &sd_files[i];
  return 0;
}
nn_result_t fs_size(int64_t *out, fs_file_t f) { *out = (int64_t)((typeof(&sd_files[0]))f.handle)->size; return 0; }
nn_result_t fs_read(size_t *got, fs_file_t f, int64_t offset, void *buf, size_t size)
{
  typeof(&sd_files[0]) e = f.handle;
  size_t n = (size_t)offset >= e->size ? 0 : e->size - (size_t)offset;
  if (n > size)
    n = size;
  memcpy(buf, e->data + offset, n);
  *got = n;
  return 0;
}
nn_result_t fs_write(fs_file_t f, int64_t offset, const void *buf, size_t size, const fs_write_option_t *opt)
{
  (void)opt;
  typeof(&sd_files[0]) e = f.handle;
  if ((size_t)offset + size > e->size)
    return 1;
  memcpy(e->data + offset, buf, size);
  return 0;
}
void fs_close(fs_file_t f) { (void)f; }

static nn_result_t fake_open_dir(fs_dir_t *out, const char *path, int mode)
{
  (void)mode;
  out->handle = 0;
  sd_list_at = 0;
  snprintf(sd_list_path, sizeof(sd_list_path), "%s", path);
  return sd_find_dir(path) < 0;
}
static nn_result_t fake_read_dir(int64_t *count, fs_dir_entry_t *e, fs_dir_t dir, int64_t max)
{
  (void)dir; (void)max;
  *count = 0;
  for (; sd_list_at < SD_DIRS + SD_FILES; sd_list_at++) {
    bool dir = sd_list_at < SD_DIRS;
    int i = dir ? sd_list_at : sd_list_at - SD_DIRS;
    const char *path = dir ? sd_dirs[i].path : sd_files[i].path;
    if (!(dir ? sd_dirs[i].used : sd_files[i].used) || !sd_in(path, sd_list_path))
      continue;
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", strrchr(path, '/') + 1);
    e->type = dir ? FS_ENTRY_DIR : FS_ENTRY_FILE;
    e->size = dir ? 0 : (int64_t)sd_files[i].size;
    sd_list_at++;
    *count = 1;
    break;
  }
  return 0;
}
static void fake_close_dir(fs_dir_t dir) { (void)dir; }
static nn_result_t fake_delete_tree(const char *path)
{
  if (sd_find_dir(path) < 0)
    return 1;
  for (int i = 0; i < SD_FILES; i++)
    if (sd_files[i].used && sd_under(sd_files[i].path, path))
      fs_delete_file(sd_files[i].path);
  for (int i = 0; i < SD_DIRS; i++)
    if (sd_dirs[i].used && sd_under(sd_dirs[i].path, path))
      sd_dirs[i].used = false;
  return 0;
}
uintptr_t got_module_of(uintptr_t addr) { (void)addr; return 1; }
void *got_export(uintptr_t module, const char *name)
{
  (void)module;
  if (strstr(name, "OpenDirectory")) return (void *)fake_open_dir;
  if (strstr(name, "ReadDirectory")) return (void *)fake_read_dir;
  if (strstr(name, "CloseDirectory")) return (void *)fake_close_dir;
  if (strstr(name, "DeleteDirectoryRecursively")) return (void *)fake_delete_tree;
  return 0;
}

/* --- simulated GameData, actors, world/navi/warp/camera state ---------------------------------- */

#define H_SESSION_INT   0x11112222u
#define H_SESSION_BOOL  0x33334444u
#define H_SESSION_STR   0x55556666u
#define H_SESSION_UINT  0x77778888u

const memfilefield_t memfilefields[] = {
  { H_SESSION_INT,  "Session.Int"  },
  { H_SESSION_BOOL, "Session.Bool" },
  { H_SESSION_STR,  "Session.Str"  },
  { H_SESSION_UINT, "Session.UInt" },
};
const int memfilefields_count = (int)(sizeof(memfilefields) / sizeof(memfilefields[0]));

/* Test memory region registry for sys_mapped / sys_ptr / sys_read32. */
typedef struct {
  uintptr_t base;
  size_t    size;
  uint32_t  perm;
} region_t;

#define MAX_REGIONS 128
static region_t regions[MAX_REGIONS];
static int      n_regions;

static void clear_regions(void) { n_regions = 0; }
static void add_region(const void *p, size_t size, uint32_t perm)
{
  assert(n_regions < MAX_REGIONS);
  regions[n_regions++] = (region_t){ (uintptr_t)p, size, perm };
}

bool sys_mapped(uintptr_t addr, size_t size, uint32_t perm)
{
  if (!addr || size > UINTPTR_MAX - addr)
    return false;
  for (int i = 0; i < n_regions; i++)
    if (addr >= regions[i].base && addr + size <= regions[i].base + regions[i].size &&
        (regions[i].perm & perm) == perm)
      return true;
  return false;
}

uintptr_t sys_ptr(uintptr_t addr)
{
  uintptr_t v = 0;
  if (sys_mapped(addr, sizeof(v), SYS_PERM_R))
    memcpy(&v, (const void *)addr, sizeof(v));
  return v;
}

bool sys_read32(uintptr_t addr, uint32_t *out)
{
  if (!sys_mapped(addr, sizeof(*out), SYS_PERM_R))
    return false;
  memcpy(out, (const void *)addr, sizeof(*out));
  return true;
}

static bool block_scan_query;
uint32_t sys_query_memory(sys_meminfo_t *info, uint64_t addr)
{
  if (block_scan_query || !info)
    return 1;
  for (int i = 0; i < n_regions; i++) {
    if (addr >= regions[i].base && addr < regions[i].base + regions[i].size) {
      memset(info, 0, sizeof(*info));
      info->addr = regions[i].base;
      info->size = regions[i].size;
      info->perm = regions[i].perm;
      return 0;
    }
  }
  return 1;
}

static char last_notice[128];
void gfx_notice(const char *s)
{
  snprintf(last_notice, sizeof(last_notice), "%s", s ? s : "");
}
void gfx_logf(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(last_notice, sizeof(last_notice), fmt, ap);
  va_end(ap);
}
void log_printf(const char *fmt, ...) { (void)fmt; }
const char *T(const char *s) { return s; }
const char *TF(const char *s) { return s; }
const char *flagname(uint32_t hash) { (void)hash; return 0; }

/* Typed field backing storage */
static uint8_t  gd_mgr_mem[0xE00];
static uint8_t  gd_desc0_mem[GD_DESC_STRIDE];
static uint32_t gd_tbl_int[16], gd_tbl_bool[16], gd_tbl_float[8], gd_tbl_enum[8];
static uint32_t gd_tbl_str[16], gd_tbl_uint[8], gd_tbl_vec3[8];
static uint8_t  gd_obj_int[8][0x28];
static uint8_t  gd_obj_bool[16][0x18];
static uint8_t  gd_obj_float[2][0x28];
static uint8_t  gd_obj_enum[2][0x38];
static uint8_t  gd_obj_str[8][Z64HD_STR_STRIDE];
static char     gd_str_bufs[8][GD_STR_LEN];
static uint8_t  gd_obj_uint[4][GD_UINT_STRIDE];
static uint8_t  gd_obj_vec3[4][GD_VEC3_STRIDE];
static gd_type_t type_int, type_bool, type_float, type_enum, type_str;

/* Lookup helper over fixed open-addressed test tables */
static uint32_t *find_pair(const gd_type_t *t, uint32_t hash)
{
  for (uint32_t i = 0; i < t->table_size; i++)
    if (t->table[2 * i] == hash)
      return &t->table[2 * i];
  return 0;
}

static void add_gd_entry(gd_type_t *t, uint32_t stride, uint32_t idx, uint32_t hash, int32_t save_file)
{
  uint32_t s = hash % t->table_size;
  while (t->table[2 * s] != 0)
    s = (s + 1) % t->table_size;
  t->table[2 * s] = hash;
  t->table[2 * s + 1] = idx;
  uintptr_t obj_vt = (uintptr_t)gd_mgr_mem;
  memcpy(t->objects + (size_t)idx * stride, &obj_vt, sizeof(obj_vt));
  memcpy(t->objects + (size_t)idx * stride + GD_FIELD_SAVE_FILE, &save_file, sizeof(save_file));
}

void *gd_manager(void) { return gd_mgr_mem; }
bool gd_ready(void) { return true; }
gd_type_t *gd_type(gd_kind_t k)
{
  return k == GD_INT ? &type_int : k == GD_BOOL ? &type_bool : k == GD_FLOAT ? &type_float :
         k == GD_ENUM ? &type_enum : 0;
}
gd_type_t *gd_string_type(void) { return &type_str; }
void gd_layout(gd_kind_t k, uint32_t *stride, uint32_t *voff)
{
  static const uint32_t st[GD_NTYPES] = { 0x28, 0x18, 0x28, 0x38 };
  static const uint32_t vo[GD_NTYPES] = { 0x1C, 0x16, 0x1C, 0x1C };
  *stride = st[k];
  *voff = vo[k];
}
gd_type_t *gd_type_at(uint32_t offset)
{
  if (offset == GD_UINT_TYPE || offset == GD_VEC3_TYPE)
    return (gd_type_t *)(gd_mgr_mem + offset);
  return 0;
}
bool gd_type_index(const gd_type_t *t, uint32_t hash, uint32_t *idx)
{
  uint32_t *p = t ? find_pair(t, hash) : 0;
  if (!p || p[1] >= t->count)
    return false;
  *idx = p[1];
  return true;
}

bool gd_get_int(uint32_t hash, int32_t *out)
{
  uint32_t i;
  if (!gd_type_index(&type_int, hash, &i))
    return false;
  memcpy(out, gd_obj_int[i] + 0x1C, 4);
  return true;
}
void gd_set_int(uint32_t hash, int32_t v) { gd_poke_int(hash, v); }
bool gd_poke_int(uint32_t hash, int32_t v)
{
  uint32_t i;
  if (!gd_type_index(&type_int, hash, &i))
    return false;
  memcpy(gd_obj_int[i] + 0x1C, &v, 4);
  memcpy(gd_obj_int[i] + 0x20, &v, 4);
  return true;
}
bool gd_get_bool(uint32_t hash, bool *out)
{
  uint32_t i;
  if (!gd_type_index(&type_bool, hash, &i))
    return false;
  *out = gd_obj_bool[i][0x16] != 0;
  return true;
}
bool gd_set_bool(uint32_t hash, bool v)
{
  uint32_t i;
  if (!gd_type_index(&type_bool, hash, &i))
    return false;
  gd_obj_bool[i][0x16] = gd_obj_bool[i][0x17] = (uint8_t)(v ? 1 : 0);
  return true;
}
/* the quest manager: counted (quests.c isn't built into the tests) */
static int quests_resyncs;
bool quests_on_load(void) { quests_resyncs++; return true; }
/* Farore's Wind's point isn't simulated: WarpMgr is told GameData changed, but there's no point to place */
bool gd_get_vec3(uint32_t hash, float v[3]) { (void)hash; (void)v; return false; }
const char *gd_str_index(uint32_t i) { return i < type_str.count ? gd_str_bufs[i] : 0; }
const char *gd_get_str(uint32_t hash)
{
  uint32_t i;
  return gd_type_index(&type_str, hash, &i) ? gd_str_bufs[i] : 0;
}
bool gd_set_str(uint32_t hash, const char *s)
{
  uint32_t i;
  if (!gd_type_index(&type_str, hash, &i))
    return false;
  snprintf(gd_str_bufs[i], GD_STR_LEN, "%s", s ? s : "");
  return true;
}
bool gd_set_vec3(uint32_t hash, const float v[3])
{
  gd_type_t *t = gd_type_at(GD_VEC3_TYPE);
  uint32_t i;
  if (!t || !gd_type_index(t, hash, &i))
    return false;
  memcpy(gd_obj_vec3[i] + GD_VEC3_VALUE, v, 12);
  memcpy(gd_obj_vec3[i] + GD_VEC3_VALUE + 12, v, 12);
  return true;
}
static void read_vec3(uint32_t hash, float v[3])
{
  gd_type_t *t = gd_type_at(GD_VEC3_TYPE);
  uint32_t i;
  assert(t && gd_type_index(t, hash, &i));
  memcpy(v, gd_obj_vec3[i] + GD_VEC3_VALUE, 12);
}

/* Mock WorldMgr, NaviMgr, Camera, Link, Warp, Eventskip, Gear */
static struct {
  uintptr_t wm_holder_ptr, wm_ptr;
  uintptr_t navi_scene_holder_ptr, navi_scene_ptr;
  uintptr_t cam_holder_ptr, cam_ptr;
  uintptr_t actor_mgr_ptr;
} main_slots;

static uint8_t wm_mem[0x600], clock_mem[0x80], clock_vt[0x40];
static uint8_t navi_scene_mem[0x500], navi_mem[0x40];
static uint32_t sim_time = 21600;
static int wm_save_calls, wm_load_calls, navi_load_calls, gear_calls;

static uint32_t fake_clock_get(uintptr_t c) { (void)c; return sim_time; }
static void fake_wm_set_time(uintptr_t wm, uint32_t sec) { (void)wm; sim_time = sec; }
static void fake_wm_on_save(uintptr_t mgr) { (void)mgr; wm_save_calls++; }
static void fake_wm_on_load(uintptr_t mgr) { (void)mgr; wm_load_calls++; }
static uintptr_t fake_navi_get(uintptr_t scene) { assert(scene == (uintptr_t)navi_scene_mem); return (uintptr_t)navi_mem; }
static void fake_navi_on_load(uintptr_t mgr) { assert(mgr == (uintptr_t)navi_mem); navi_load_calls++; }

static bool allow_req_save = true, allow_req_load = true;
static int  req_save_calls, req_load_calls;
static uint8_t *last_req_save_buf;
static const uint8_t *last_req_load_buf;

static bool fake_gd_req_save(void *mgr, void *buf, int file_idx)
{
  (void)mgr; (void)file_idx;
  req_save_calls++;
  if (!allow_req_save)
    return false;
  last_req_save_buf = (uint8_t *)buf;
  uintptr_t p = (uintptr_t)buf;
  memcpy(gd_desc0_mem + GD_DESC_SAVE_BUF, &p, sizeof(p));
  gd_desc0_mem[GD_DESC_FLAGS] = GD_FLAG_SAVE_REQ | GD_FLAG_SAVE_WORK;
  return true;
}

static bool fake_gd_req_load(void *mgr, const void *buf, int file_idx)
{
  (void)mgr; (void)file_idx;
  req_load_calls++;
  if (!allow_req_load)
    return false;
  last_req_load_buf = (const uint8_t *)buf;
  uintptr_t p = (uintptr_t)buf;
  memcpy(gd_desc0_mem + GD_DESC_LOAD_BUF, &p, sizeof(p));
  gd_desc0_mem[GD_DESC_FLAGS] = GD_FLAG_LOAD_REQ;
  gd_desc0_mem[GD_DESC_LOAD_OK] = 0;
  return true;
}

/* Mock Link state */
static uintptr_t sim_link_actor;
static int32_t   sim_hp, sim_max_hp;
static vec3f_t   sim_pos, sim_vel, last_set_vel;
static int       set_vel_calls;
static double    sim_fx, sim_fz;
static bool      sim_teleport_ok = true;
static int       teleport_calls;

uintptr_t link_actor(void) { return sim_link_actor; }
bool link_drawn_pos(vec3f_t *out) { *out = sim_pos; return sim_link_actor != 0; }
bool link_facing(double *fx, double *fz) { *fx = sim_fx; *fz = sim_fz; return sim_link_actor != 0; }
bool cct_velocity(uintptr_t actor, vec3f_t *out)
{
  if (!actor || actor != sim_link_actor)
    return false;
  *out = sim_vel;
  return true;
}
bool cct_set_velocity(uintptr_t actor, const vec3f_t *v)
{
  if (!actor || actor != sim_link_actor || !v)
    return false;
  last_set_vel = *v;
  sim_vel = *v;
  set_vel_calls++;
  return true;
}
bool link_health(int32_t **cur, int32_t **max)
{
  if (!sim_link_actor)
    return false;
  *cur = &sim_hp;
  *max = &sim_max_hp;
  return true;
}
bool cct_teleport(const vec3f_t *p, const float *rot)
{
  teleport_calls++;
  if (!sim_teleport_ok)
    return false;
  sim_pos = *p;
  if (rot) {
    sim_fx = rot[2];
    sim_fz = rot[0];
  }
  return true;
}
bool cct_clear_fall_distance(uintptr_t actor) { (void)actor; return true; }
bool gear_reapply_now(void) { gear_calls++; return true; }
bool cmd_pressed(command_t cmd) { (void)cmd; return false; }

/* Mock actor_pack_name / actor_spawn_handle / Lifter / BSA test doubles */
static int       link_set_calls, link_dtor_calls, lifter_drop_calls, lifter_force_calls, lift_anim_calls, bsa_verb_calls;
static uintptr_t last_link_set_actor, last_lift_target, last_lift_anim_actor, last_bsa_target;
static uint32_t  last_bsa_verb, last_bsa_slot;
static uint16_t  last_bsa_flags;
static uintptr_t sim_spawn_result_actor;
static int       sim_spawn_wait_frames, sim_spawn_calls, sim_spawn_cancel_calls;
static char      sim_spawn_last_name[64];
static uint16_t  sim_spawn_handle_state;

const char *actor_pack_name(uintptr_t actor)
{
  uintptr_t p = actor ? sys_ptr(actor + 0x260) : 0;
  return p && sys_mapped(p, 8, SYS_PERM_R) ? (const char *)p : 0;
}

bool actor_spawn_handle(const char *name, const float pos[3], float yaw_degrees, uintptr_t *handle, uint32_t *result)
{
  (void)pos; (void)yaw_degrees;
  sim_spawn_calls++;
  snprintf(sim_spawn_last_name, sizeof(sim_spawn_last_name), "%s", name ? name : "");
  if (!sim_spawn_result_actor) {
    *handle = 0;
    *result = 1;
    return false;
  }
  sim_spawn_handle_state = sim_spawn_wait_frames > 0 ? 1u : 3u;
  *handle = (uintptr_t)&sim_spawn_handle_state;
  *result = 0;
  return true;
}

spawn_poll_t actor_spawn_poll(uintptr_t *handle, uintptr_t *actor)
{
  *actor = 0;
  if (!*handle)
    return SPAWN_FAILED;
  if (sim_spawn_wait_frames > 0) {
    sim_spawn_wait_frames--;
    return SPAWN_WAITING;
  }
  *actor = sim_spawn_result_actor;
  *handle = 0;
  return *actor ? SPAWN_MADE : SPAWN_FAILED;
}

void actor_spawn_cancel(uintptr_t *handle)
{
  if (*handle) {
    sim_spawn_cancel_calls++;
    *handle = 0;
  }
}

static void fake_actor_link_set(void *link, uintptr_t actor, uint16_t flags)
{
  (void)flags;
  link_set_calls++;
  last_link_set_actor = actor;
  uint64_t *w = (uint64_t *)link;
  w[1] = actor; /* store actor directly for fake_lifter_force_lift / fake_player_lift_anim */
  w[2] = 1u;
}

static void fake_actor_link_dtor(void *link)
{
  (void)link;
  link_dtor_calls++;
}

static bool fake_lifter_drop(uintptr_t lifter, int reason, float s0, float s1)
{
  (void)reason; (void)s0; (void)s1;
  lifter_drop_calls++;
  if (sys_mapped(lifter + LIFTER_STATE, 4, SYS_PERM_R | SYS_PERM_W))
    *(uint32_t *)(lifter + LIFTER_STATE) = 0;
  return true;
}

static bool fake_lifter_force_lift(uintptr_t lifter, void *link, const char *const *key, bool flag)
{
  (void)key; (void)flag;
  lifter_force_calls++;
  uintptr_t actor = ((const uint64_t *)link)[1];
  last_lift_target = actor;
  if (sys_mapped(lifter + LIFTER_STATE, 4, SYS_PERM_R | SYS_PERM_W))
    *(uint32_t *)(lifter + LIFTER_STATE) = 3;
  return actor != 0;
}

static void fake_player_lift_anim(uintptr_t link_actor_arg, void *link)
{
  (void)link_actor_arg;
  lift_anim_calls++;
  last_lift_anim_actor = ((const uint64_t *)link)[1];
}

static int fake_bsa_set_action_verb(uintptr_t bsa, const void *req, bool flag)
{
  (void)flag;
  bsa_verb_calls++;
  last_bsa_target = bsa;
  const uint8_t *r = (const uint8_t *)req;
  memcpy(&last_bsa_slot, r + 0x48, 4);
  memcpy(&last_bsa_verb, r + 0x4C, 4);
  memcpy(&last_bsa_flags, r + 0x50, 2);
  if (last_bsa_slot <= 4u && sys_mapped(bsa + BSA_ACTION_SLOTS + (size_t)last_bsa_slot * 8u, 8, SYS_PERM_R)) {
    uintptr_t slot_obj = sys_ptr(bsa + BSA_ACTION_SLOTS + (size_t)last_bsa_slot * 8u);
    if (slot_obj && sys_mapped(slot_obj + BSA_SLOT_FLAGS, 1, SYS_PERM_R) &&
        (*(const uint8_t *)(slot_obj + BSA_SLOT_FLAGS) & 2u))
      return 3; /* mimics game's per-frame slot busy check */
  }
  if (last_bsa_slot == 0 && sys_mapped(bsa + BSA_CUR_VERB, 4, SYS_PERM_R | SYS_PERM_W))
    memcpy((void *)(bsa + BSA_CUR_VERB), &last_bsa_verb, 4);
  return 0;
}

/* Mock Warp & Eventskip */
static bool sim_warp_pending, sim_warp_requested, sim_warp_abandoned;
static bool sim_silent_warp_accept = true;
static int  warp_silent_calls, warp_cancel_calls;
static bool sim_ev_known = true, sim_ev_running;

bool warp_pending(void) { return sim_warp_pending; }
bool warp_requested(void) { return sim_warp_requested; }
bool warp_abandoned(void) { return sim_warp_abandoned; }
void warp_cancel(void) { warp_cancel_calls++; sim_warp_pending = false; }
bool age_warp_to_silent(const char *map, const char *start, bool have_pos)
{
  (void)map; (void)start; (void)have_pos;
  warp_silent_calls++;
  if (!sim_silent_warp_accept)
    return false;
  sim_warp_requested = true;
  sim_warp_pending = true;
  sim_warp_abandoned = false;
  return true;
}
static bool sim_ev_skip_accept = true;
static int  ev_skip_calls;
bool eventskip_known(void) { return sim_ev_known; }
bool eventskip_running(void) { return sim_ev_running; }
bool eventskip_start(void)
{
  ev_skip_calls++;
  return sim_ev_skip_accept && sim_ev_known && sim_ev_running;
}
static bool sim_paused;
bool gd_game_paused(bool *is_pause, int32_t *pause_requests)
{
  if (is_pause)
    *is_pause = sim_paused;
  if (pause_requests)
    *pause_requests = 0;
  return sim_paused;
}
bool gd_game_pausing(void) { return false; }
bool gd_game_menu_screen(void) { return true; }

/* Mock Player Camera + AI Node Chain (cam_region holds cam_comp + optional scan-only node) */
static uint8_t cam_holder_mem[0x60], cam_handle_entry[0x60];
static uint8_t cam_actor_mem[0x300], cam_comps_mem[0x400], cam_glob_mem[0x60];
static struct {
  uint8_t comp[0x1000];
  uint8_t scan_node[ACTION_BYTES];
} cam_region;
#define cam_comp_mem cam_region.comp
static uint8_t ai_comp_mem[0x100], ai_ctrl_set_mem[0x40];
static uint8_t ai_entries_arr[16], ai_entry0[0x30], ai_entry1[0x30];
static uint8_t ctrl_bad_mem[0x220], ctrl_ok_mem[0x220];
static uint8_t unit_slots_arr[0x40], unit_bad_mem[0x80], unit_ok_mem[0x80];
static uint8_t node_slots_arr[3 * 0x18];
static uint8_t node_bad_mem[ACTION_BYTES], node_ok_mem[ACTION_BYTES], node_ok2_mem[ACTION_BYTES];
static int cam_snap_calls, cam_arrive_calls;

uintptr_t camera(void) { return (uintptr_t)cam_glob_mem; }
static void fake_camera_snap(uintptr_t component)
{
  assert(component == (uintptr_t)cam_comp_mem);
  cam_snap_calls++;
}
static void fake_camera_arrive(uintptr_t component)
{
  assert(component == (uintptr_t)cam_comp_mem);
  cam_arrive_calls++;
}

static void init_camera_chain(uint32_t cam_state_off)
{
  block_scan_query = false;
  memset(cam_holder_mem, 0, sizeof(cam_holder_mem));
  memset(cam_handle_entry, 0, sizeof(cam_handle_entry));
  memset(cam_actor_mem, 0, sizeof(cam_actor_mem));
  memset(cam_comps_mem, 0, sizeof(cam_comps_mem));
  memset(&cam_region, 0, sizeof(cam_region));
  memset(cam_glob_mem, 0, sizeof(cam_glob_mem));
  memset(ai_comp_mem, 0, sizeof(ai_comp_mem));
  memset(ai_ctrl_set_mem, 0, sizeof(ai_ctrl_set_mem));
  memset(ai_entry0, 0, sizeof(ai_entry0));
  memset(ai_entry1, 0, sizeof(ai_entry1));
  memset(ctrl_bad_mem, 0, sizeof(ctrl_bad_mem));
  memset(ctrl_ok_mem, 0, sizeof(ctrl_ok_mem));
  memset(unit_slots_arr, 0, sizeof(unit_slots_arr));
  memset(unit_bad_mem, 0, sizeof(unit_bad_mem));
  memset(unit_ok_mem, 0, sizeof(unit_ok_mem));
  memset(node_slots_arr, 0, sizeof(node_slots_arr));
  memset(node_bad_mem, 0, sizeof(node_bad_mem));
  memset(node_ok_mem, 0, sizeof(node_ok_mem));
  memset(node_ok2_mem, 0, sizeof(node_ok2_mem));

  test_build.player_camera_component = 0x360;
  test_build.player_camera_state = cam_state_off;
  uintptr_t cam_vt = z64hd_main + 0x10000;
  uintptr_t p;

  main_slots.cam_ptr = (uintptr_t)cam_holder_mem;
  main_slots.cam_holder_ptr = (uintptr_t)&main_slots.cam_ptr;
  p = (uintptr_t)cam_handle_entry; memcpy(cam_holder_mem + 0x38 + 8, &p, 8);
  uint32_t seq = 7; memcpy(cam_holder_mem + 0x38 + 0x10, &seq, 4);
  memcpy(cam_handle_entry + 0x48, &seq, 4);
  p = (uintptr_t)cam_actor_mem; memcpy(cam_handle_entry + 0x40, &p, 8);

  p = (uintptr_t)cam_comps_mem; memcpy(cam_actor_mem + 0x270, &p, 8);
  p = (uintptr_t)cam_comp_mem; memcpy(cam_comps_mem + 0x360, &p, 8);
  memcpy(cam_comp_mem, &cam_vt, 8);
  p = (uintptr_t)cam_actor_mem; memcpy(cam_comp_mem + 0x38, &p, 8); /* the actor the camera follows */

  /* Verified AI node chain:
   * actor+0x270 -> +0x48 (AIComponent) -> +0x28 (AIControllerSet) -> entry1(+0x18) -> unit1 -> node_slots (+0x30) */
  p = (uintptr_t)ai_comp_mem; memcpy(cam_comps_mem + 0x48, &p, 8);
  p = (uintptr_t)ai_ctrl_set_mem; memcpy(ai_comp_mem + 0x28, &p, 8);
  uint32_t set_cnt = 2; memcpy(ai_ctrl_set_mem, &set_cnt, 4);
  p = (uintptr_t)ai_entries_arr; memcpy(ai_ctrl_set_mem + 0x08, &p, 8);
  p = (uintptr_t)ai_entry0; memcpy(ai_entries_arr, &p, 8);
  p = (uintptr_t)ai_entry1; memcpy(ai_entries_arr + 8, &p, 8);
  p = (uintptr_t)ctrl_bad_mem; memcpy(ai_entry0 + 0x18, &p, 8);
  p = (uintptr_t)ctrl_ok_mem; memcpy(ai_entry1 + 0x18, &p, 8);

  /* ctrl_bad has wrong actor back-pointer; ctrl_ok matches cam_actor_mem */
  p = (uintptr_t)ctrl_bad_mem; memcpy(ctrl_bad_mem + 0x1F0, &p, 8);
  p = (uintptr_t)cam_actor_mem; memcpy(ctrl_ok_mem + 0x1F0, &p, 8);
  uint32_t unit_cnt = 2; memcpy(ctrl_ok_mem + 0x18, &unit_cnt, 4);
  p = (uintptr_t)unit_slots_arr; memcpy(ctrl_ok_mem + 0x20, &p, 8);
  p = (uintptr_t)unit_bad_mem; memcpy(unit_slots_arr, &p, 8);
  p = (uintptr_t)unit_ok_mem; memcpy(unit_slots_arr + 0x18, &p, 8);

  /* unit_bad has wrong ctrl back-pointer; unit_ok matches ctrl_ok_mem */
  p = (uintptr_t)ctrl_bad_mem; memcpy(unit_bad_mem + 0x08, &p, 8);
  p = (uintptr_t)ctrl_ok_mem; memcpy(unit_ok_mem + 0x08, &p, 8);
  uint32_t node_cnt = 3; memcpy(unit_ok_mem + 0x28, &node_cnt, 4);
  p = (uintptr_t)node_slots_arr; memcpy(unit_ok_mem + 0x30, &p, 8);
  p = (uintptr_t)node_bad_mem; memcpy(node_slots_arr, &p, 8);
  p = (uintptr_t)node_ok_mem; memcpy(node_slots_arr + 0x18, &p, 8);
  p = (uintptr_t)node_ok2_mem; memcpy(node_slots_arr + 2 * 0x18, &p, 8);

  /* node_bad has wrong state ptr; node_ok & node_ok2 share vtable with distinct node indices (+0x1C) */
  uintptr_t bad_state = (uintptr_t)cam_comp_mem;
  uintptr_t ok_state = (uintptr_t)cam_comp_mem + cam_state_off;
  memcpy(node_bad_mem, &cam_vt, 8); memcpy(node_bad_mem + 0x20, &bad_state, 8);
  uintptr_t act_vt = z64hd_main + 0x20000;
  uint16_t idx1 = 10, idx2 = 25;
  memcpy(node_ok_mem, &act_vt, 8); memcpy(node_ok_mem + 0x1C, &idx1, 2); memcpy(node_ok_mem + 0x20, &ok_state, 8);
  memcpy(node_ok2_mem, &act_vt, 8); memcpy(node_ok2_mem + 0x1C, &idx2, 2); memcpy(node_ok2_mem + 0x20, &ok_state, 8);
}

static void register_default_regions(void)
{
  clear_regions();
  add_region(gd_mgr_mem, sizeof(gd_mgr_mem), SYS_PERM_R | SYS_PERM_W);
  add_region(gd_desc0_mem, sizeof(gd_desc0_mem), SYS_PERM_R | SYS_PERM_W);
  add_region(&main_slots, sizeof(main_slots), SYS_PERM_R | SYS_PERM_W);
  add_region(wm_mem, sizeof(wm_mem), SYS_PERM_R | SYS_PERM_W);
  add_region(clock_mem, sizeof(clock_mem), SYS_PERM_R | SYS_PERM_W);
  add_region(clock_vt, sizeof(clock_vt), SYS_PERM_R);
  add_region(navi_scene_mem, sizeof(navi_scene_mem), SYS_PERM_R | SYS_PERM_W);
  add_region(navi_mem, sizeof(navi_mem), SYS_PERM_R | SYS_PERM_W);

  add_region(gd_tbl_int, sizeof(gd_tbl_int), SYS_PERM_R);
  add_region(gd_tbl_bool, sizeof(gd_tbl_bool), SYS_PERM_R);
  add_region(gd_tbl_float, sizeof(gd_tbl_float), SYS_PERM_R);
  add_region(gd_tbl_enum, sizeof(gd_tbl_enum), SYS_PERM_R);
  add_region(gd_tbl_str, sizeof(gd_tbl_str), SYS_PERM_R);
  add_region(gd_tbl_uint, sizeof(gd_tbl_uint), SYS_PERM_R);
  add_region(gd_tbl_vec3, sizeof(gd_tbl_vec3), SYS_PERM_R);

  add_region(gd_obj_int, sizeof(gd_obj_int), SYS_PERM_R | SYS_PERM_W);
  add_region(gd_obj_bool, sizeof(gd_obj_bool), SYS_PERM_R | SYS_PERM_W);
  add_region(gd_obj_float, sizeof(gd_obj_float), SYS_PERM_R | SYS_PERM_W);
  add_region(gd_obj_enum, sizeof(gd_obj_enum), SYS_PERM_R | SYS_PERM_W);
  add_region(gd_obj_str, sizeof(gd_obj_str), SYS_PERM_R | SYS_PERM_W);
  add_region(gd_str_bufs, sizeof(gd_str_bufs), SYS_PERM_R | SYS_PERM_W);
  add_region(gd_obj_uint, sizeof(gd_obj_uint), SYS_PERM_R | SYS_PERM_W);
  add_region(gd_obj_vec3, sizeof(gd_obj_vec3), SYS_PERM_R | SYS_PERM_W);

  add_region(cam_holder_mem, sizeof(cam_holder_mem), SYS_PERM_R);
  add_region(cam_handle_entry, sizeof(cam_handle_entry), SYS_PERM_R);
  add_region(cam_actor_mem, sizeof(cam_actor_mem), SYS_PERM_R | SYS_PERM_W);
  add_region(cam_comps_mem, sizeof(cam_comps_mem), SYS_PERM_R);
  add_region(&cam_region, sizeof(cam_region), SYS_PERM_R | SYS_PERM_W);
  add_region(cam_glob_mem, sizeof(cam_glob_mem), SYS_PERM_R | SYS_PERM_W);
  add_region(ai_comp_mem, sizeof(ai_comp_mem), SYS_PERM_R);
  add_region(ai_ctrl_set_mem, sizeof(ai_ctrl_set_mem), SYS_PERM_R);
  add_region(ai_entries_arr, sizeof(ai_entries_arr), SYS_PERM_R);
  add_region(ai_entry0, sizeof(ai_entry0), SYS_PERM_R);
  add_region(ai_entry1, sizeof(ai_entry1), SYS_PERM_R);
  add_region(ctrl_bad_mem, sizeof(ctrl_bad_mem), SYS_PERM_R);
  add_region(ctrl_ok_mem, sizeof(ctrl_ok_mem), SYS_PERM_R);
  add_region(unit_slots_arr, sizeof(unit_slots_arr), SYS_PERM_R);
  add_region(unit_bad_mem, sizeof(unit_bad_mem), SYS_PERM_R);
  add_region(unit_ok_mem, sizeof(unit_ok_mem), SYS_PERM_R);
  add_region(node_slots_arr, sizeof(node_slots_arr), SYS_PERM_R);
  add_region(node_bad_mem, sizeof(node_bad_mem), SYS_PERM_R | SYS_PERM_W);
  add_region(node_ok_mem, sizeof(node_ok_mem), SYS_PERM_R | SYS_PERM_W);
  add_region(node_ok2_mem, sizeof(node_ok2_mem), SYS_PERM_R | SYS_PERM_W);
}

static void init_env(uint32_t cam_state_off)
{
  for (int i = 0; i < MEMFILE_SLOTS; i++)
    free_memfile(&memfiles[i]);
  free_memfile(&staging);
  free16(inflight_save_buf);
  inflight_save_buf = 0;
  phase = L_IDLE;
  slot = active_slot = phase_frames = quiet_frames = camera_frames = 0;
  memfile_session = memfile_fast_reload = true;
  last_notice[0] = status[0] = 0;

  uintptr_t fn_lo = (uintptr_t)fake_clock_get;
  uintptr_t dat_lo = (uintptr_t)&main_slots;
  uintptr_t lo = fn_lo < dat_lo ? fn_lo : dat_lo;
  z64hd_main = lo - 0x4000u;
  assert(fn_lo >= z64hd_main && fn_lo - z64hd_main < MAIN_CODE_SIZE);

  test_build = (struct z64hd_build){
    .gd_request_save = OFF_GD_REQ_SAVE,
    .gd_request_load = OFF_GD_REQ_LOAD,
    .wm_ptr = (uint32_t)((uintptr_t)&main_slots.wm_holder_ptr - z64hd_main),
    .wm_set_time = OFF_WM_SET_TIME,
    .wm_on_save = OFF_WM_ON_SAVE,
    .wm_on_load = OFF_WM_ON_LOAD,
    .navi_scene_ptr = (uint32_t)((uintptr_t)&main_slots.navi_scene_holder_ptr - z64hd_main),
    .navi_get = OFF_NAVI_GET,
    .navi_on_load = OFF_NAVI_ON_LOAD,
    .player_camera_holder = (uint32_t)((uintptr_t)&main_slots.cam_holder_ptr - z64hd_main),
    .player_camera_snap = OFF_CAMERA_SNAP,
    .player_camera_arrive = OFF_CAMERA_ARRIVE,
  };

  /* WorldMgr + Clock + NaviMgr */
  memset(wm_mem, 0, sizeof(wm_mem));
  memset(clock_mem, 0, sizeof(clock_mem));
  memset(clock_vt, 0, sizeof(clock_vt));
  main_slots.wm_ptr = (uintptr_t)wm_mem;
  main_slots.wm_holder_ptr = (uintptr_t)&main_slots.wm_ptr;
  uintptr_t p = (uintptr_t)clock_mem; memcpy(wm_mem + WM_CLOCK, &p, 8);
  p = (uintptr_t)clock_vt; memcpy(clock_mem, &p, 8);
  p = (uintptr_t)fake_clock_get; memcpy(clock_vt + CLOCK_GET_TIME, &p, 8);
  main_slots.navi_scene_ptr = (uintptr_t)navi_scene_mem;
  main_slots.navi_scene_holder_ptr = (uintptr_t)&main_slots.navi_scene_ptr;

  /* GameDataMgr + desc0 + typed fields */
  memset(gd_mgr_mem, 0, sizeof(gd_mgr_mem));
  memset(gd_desc0_mem, 0, sizeof(gd_desc0_mem));
  uint32_t count1 = 1; memcpy(gd_mgr_mem + GD_MGR_FILE_COUNT, &count1, 4);
  p = (uintptr_t)gd_desc0_mem; memcpy(gd_mgr_mem + GD_MGR_FILES_PTR, &p, 8);
  uint32_t buf_cap = 1024; memcpy(gd_desc0_mem + GD_DESC_BUF_SIZE, &buf_cap, 4);

  memset(gd_tbl_int, 0, sizeof(gd_tbl_int));
  memset(gd_tbl_bool, 0, sizeof(gd_tbl_bool));
  memset(gd_tbl_float, 0, sizeof(gd_tbl_float));
  memset(gd_tbl_enum, 0, sizeof(gd_tbl_enum));
  memset(gd_tbl_str, 0, sizeof(gd_tbl_str));
  memset(gd_tbl_uint, 0, sizeof(gd_tbl_uint));
  memset(gd_tbl_vec3, 0, sizeof(gd_tbl_vec3));
  memset(gd_obj_int, 0, sizeof(gd_obj_int));
  memset(gd_obj_bool, 0, sizeof(gd_obj_bool));
  memset(gd_obj_float, 0, sizeof(gd_obj_float));
  memset(gd_obj_enum, 0, sizeof(gd_obj_enum));
  memset(gd_obj_str, 0, sizeof(gd_obj_str));
  memset(gd_str_bufs, 0, sizeof(gd_str_bufs));
  memset(gd_obj_uint, 0, sizeof(gd_obj_uint));
  memset(gd_obj_vec3, 0, sizeof(gd_obj_vec3));

  type_int   = (gd_type_t){ 1, 4, 0, (uint8_t *)gd_obj_int,   gd_tbl_int,   0, 8 };
  type_bool  = (gd_type_t){ 2, 8, 0, (uint8_t *)gd_obj_bool,  gd_tbl_bool,  0, 8 };
  type_float = (gd_type_t){ 3, 1, 0, (uint8_t *)gd_obj_float, gd_tbl_float, 0, 4 };
  type_enum  = (gd_type_t){ 4, 1, 0, (uint8_t *)gd_obj_enum,  gd_tbl_enum,  0, 4 };
  type_str   = (gd_type_t){ 5, 6, 0, (uint8_t *)gd_obj_str,   gd_tbl_str,   0, 8 };

  gd_type_t *type_uint = gd_type_at(GD_UINT_TYPE);
  gd_type_t *type_vec3 = gd_type_at(GD_VEC3_TYPE);
  *type_uint = (gd_type_t){ 6, 2, 0, (uint8_t *)gd_obj_uint, gd_tbl_uint, 0, 4 };
  *type_vec3 = (gd_type_t){ 7, 2, 0, (uint8_t *)gd_obj_vec3, gd_tbl_vec3, 0, 4 };

  add_gd_entry(&type_int, 0x28, 0, F_MAGIC, 0);
  add_gd_entry(&type_int, 0x28, 1, F_RESET_GIMMICK_OPT, -1);
  add_gd_entry(&type_int, 0x28, 2, H_SESSION_INT, -1);
  add_gd_entry(&type_int, 0x28, 3, 0x11111111u, 0); /* dummy saved int */

  add_gd_entry(&type_bool, 0x18, 0, F_IS_LOADING_BANC, -1);
  add_gd_entry(&type_bool, 0x18, 1, F_IS_CHILDHOOD, 0);
  add_gd_entry(&type_bool, 0x18, 2, F_REQ_RESET_GIMMICK, -1);
  add_gd_entry(&type_bool, 0x18, 3, F_ALREADY_RESET_LOAD, -1);
  add_gd_entry(&type_bool, 0x18, 4, F_SET_CHILD_MANUALLY, -1);
  add_gd_entry(&type_bool, 0x18, 5, F_OVERRIDE_TRAVERSE, -1);
  add_gd_entry(&type_bool, 0x18, 6, F_IS_GAME_OVER, -1);
  add_gd_entry(&type_bool, 0x18, 7, H_SESSION_BOOL, -1);

  for (uint32_t i = 0; i < 6; i++) {
    char *sptr = gd_str_bufs[i];
    uint32_t cap = GD_STR_LEN;
    memcpy(gd_obj_str[i] + Z64HD_STR_VALUE, &sptr, sizeof(sptr));
    memcpy(gd_obj_str[i] + Z64HD_STR_VALUE + sizeof(uintptr_t), &cap, sizeof(cap));
  }
  add_gd_entry(&type_str, Z64HD_STR_STRIDE, 0, GD_STR_MAP, 0);
  add_gd_entry(&type_str, Z64HD_STR_STRIDE, 1, GD_STR_SPAWN, 0);
  add_gd_entry(&type_str, Z64HD_STR_STRIDE, 2, GD_STR_EVENT, -1);
  add_gd_entry(&type_str, Z64HD_STR_STRIDE, 3, GD_STR_STARTER, -1);
  add_gd_entry(&type_str, Z64HD_STR_STRIDE, 4, GD_STR_NEXT_MAP, -1);
  add_gd_entry(&type_str, Z64HD_STR_STRIDE, 5, H_SESSION_STR, -1);

  add_gd_entry(type_uint, GD_UINT_STRIDE, 0, GD_UINT_KNOWN, 0);
  add_gd_entry(type_uint, GD_UINT_STRIDE, 1, H_SESSION_UINT, -1);
  add_gd_entry(type_vec3, GD_VEC3_STRIDE, 0, GD_VEC3_KNOWN, 0);
  add_gd_entry(type_vec3, GD_VEC3_STRIDE, 1, F_OVERRIDE_TRAVERSE_POS, -1);

  init_camera_chain(cam_state_off);
  register_default_regions();

  sim_link_actor = (uintptr_t)cam_actor_mem; /* non-zero dummy */
  sim_hp = 12; sim_max_hp = 20;
  sim_pos = (vec3f_t){ 1, 2, 3 };
  sim_fx = 0; sim_fz = 1;
  sim_time = 21600;
  sim_teleport_ok = true;
  sim_warp_pending = sim_warp_requested = sim_warp_abandoned = sim_ev_running = false;
  sim_silent_warp_accept = true;
  allow_req_save = allow_req_load = true;
  wm_save_calls = wm_load_calls = navi_load_calls = gear_calls = quests_resyncs = 0;
  req_save_calls = req_load_calls = teleport_calls = warp_silent_calls = warp_cancel_calls = cam_snap_calls = cam_arrive_calls = 0;
  gd_set_str(GD_STR_MAP, "HyruleField_Adult");
  gd_set_str(GD_STR_SPAWN, "StartPos_00");
  gd_poke_int(F_MAGIC, 48);
  gd_set_bool(F_IS_CHILDHOOD, false);
}

static void complete_mock_save(uint32_t magic, uint32_t written, const char *payload_tag)
{
  assert(last_req_save_buf);
  memcpy(last_req_save_buf, &magic, 4);
  if (payload_tag)
    snprintf((char *)last_req_save_buf + 8, 64, "%s", payload_tag);
  memcpy(gd_desc0_mem + GD_DESC_WRITTEN, &written, 4);
  gd_desc0_mem[GD_DESC_FLAGS] = 0;
  gd_mgr_mem[GD_MGR_SYNC_BUSY] = 0;
  int32_t zero = 0;
  memcpy(gd_mgr_mem + GD_MGR_ACTIVE_SAVES, &zero, 4);
}

static void complete_mock_load(bool ok)
{
  gd_desc0_mem[GD_DESC_FLAGS] = 0;
  gd_desc0_mem[GD_DESC_LOAD_OK] = (uint8_t)(ok ? 1 : 0);
}

/* --- Test 1: async save & staging isolation ---------------------------------------------------- */

static void test_async_save_and_staging(void)
{
  init_env(0x5B8);
  gd_poke_int(H_SESSION_INT, 77);
  gd_set_bool(H_SESSION_BOOL, true);
  gd_set_str(H_SESSION_STR, "sess_val_1");
  uint32_t u = 0xCAFEBABEu;
  memcpy(gd_obj_uint[1] + GD_UINT_VALUE, &u, 4);

  /* Busy first -> L_SAVING_REQ retries until accepted */
  allow_req_save = false;
  assert(memfile_save(0) && phase == L_SAVING_REQ && memfile_busy());
  memfile_frame();
  assert(phase == L_SAVING_REQ && req_save_calls == 2);
  allow_req_save = true;
  memfile_frame();
  assert(phase == L_SAVING_WAIT && wm_save_calls == 1);

  complete_mock_save(GD_SAVE_MAGIC, 128, "SLOT0_V1");
  memfile_frame();
  assert(phase == L_IDLE && memfiles[0].used && memfiles[0].save_size == 128);
  assert(strcmp((const char *)(memfiles[0].save_buf + 8), "SLOT0_V1") == 0);
  uint8_t *committed_v1_buf = memfiles[0].save_buf;

  /* Bad magic on overwrite must preserve slot 0 intact */
  assert(memfile_save(0) && phase == L_SAVING_WAIT);
  complete_mock_save(0xDEADBEEFu, 128, "BAD_MAGIC");
  memfile_frame();
  assert(phase == L_IDLE && memfiles[0].used && memfiles[0].save_buf == committed_v1_buf);
  assert(strcmp((const char *)(memfiles[0].save_buf + 8), "SLOT0_V1") == 0);
  assert(!staging.save_buf && !inflight_save_buf);

  /* Timeout while worker still owns buffer -> parked in inflight_save_buf, never freed early */
  assert(memfile_save(0) && phase == L_SAVING_WAIT);
  gd_desc0_mem[GD_DESC_FLAGS] = GD_FLAG_SAVE_WORK;
  for (int i = 0; i <= SAVE_WAIT_TIMEOUT + 1; i++)
    memfile_frame();
  assert(phase == L_IDLE && inflight_save_buf != 0);
  assert(memfiles[0].save_buf == committed_v1_buf);
  assert(!memfile_save(0)); /* blocked while inflight worker holds buffer */

  /* Worker finishes -> retired automatically on next frame */
  gd_desc0_mem[GD_DESC_FLAGS] = 0;
  memfile_frame();
  assert(inflight_save_buf == 0);
  assert(memfile_save(0));
  complete_mock_save(GD_SAVE_MAGIC, 96, "SLOT0_V2");
  memfile_frame();
  assert(phase == L_IDLE && memfiles[0].save_size == 96);

  puts("PASS: async save retry, staging preservation on failure, inflight worker buffer retirement");
}

/* --- Test 2: full reload touches zero live GameData before warp accepted/started --------------- */

static void test_full_reload_no_early_corruption(void)
{
  init_env(0x5B8);
  assert(memfile_save(0));
  complete_mock_save(GD_SAVE_MAGIC, 128, "SAVED");
  memfile_frame();
  assert(memfiles[0].used);

  /* Move to different map with distinct live values */
  gd_set_str(GD_STR_MAP, "Market_Child");
  gd_poke_int(H_SESSION_INT, 999);

  /* Case A: event request immediately refused */
  sim_silent_warp_accept = false;
  assert(!memfile_load(0) && phase == L_IDLE && req_load_calls == 0);
  int32_t val = 0;
  assert(gd_get_int(H_SESSION_INT, &val) && val == 999);
  bool fl = true;
  assert(gd_get_bool(F_ALREADY_RESET_LOAD, &fl) && !fl);
  assert(gd_get_bool(F_OVERRIDE_TRAVERSE, &fl) && !fl);
  assert(gd_get_bool(F_SET_CHILD_MANUALLY, &fl) && !fl);

  /* Case B: event request accepted but reservation times out */
  sim_silent_warp_accept = true;
  assert(memfile_load(0) && phase == L_WAIT_LOADING);
  sim_warp_requested = false;
  sim_warp_pending = false;
  sim_warp_abandoned = true;
  memfile_frame();
  assert(phase == L_IDLE && req_load_calls == 0 && warp_cancel_calls == 0);
  assert(gd_get_int(H_SESSION_INT, &val) && val == 999);
  assert(gd_get_bool(F_OVERRIDE_TRAVERSE, &fl) && !fl);

  /* Case C: reservation accepted but loading never starts within LOAD_START_TIMEOUT */
  assert(memfile_load(0) && phase == L_WAIT_LOADING);
  sim_warp_pending = false; /* reservation finished, still haven't entered loading screen */
  for (int i = 0; i <= LOAD_START_TIMEOUT + 1; i++)
    memfile_frame();
  assert(phase == L_IDLE && req_load_calls == 0);
  assert(gd_get_int(H_SESSION_INT, &val) && val == 999);
  assert(gd_get_bool(F_OVERRIDE_TRAVERSE, &fl) && !fl);

  puts("PASS: full reload refusal/timeout touches zero live GameData and clears overrides");
}

/* --- Test 3: full reload happy path ------------------------------------------------------------ */

static void test_full_reload_happy_path(void)
{
  init_env(0x5B8);
  gd_set_str(GD_STR_MAP, "DekuTree");
  gd_set_str(GD_STR_SPAWN, "StartPos_01");
  gd_set_bool(F_IS_CHILDHOOD, true);
  sim_pos = (vec3f_t){ 10.0f, 20.0f, 30.0f };
  sim_fx = 1.0; sim_fz = 0.0;
  sim_hp = 16; sim_max_hp = 20;
  gd_poke_int(F_MAGIC, 32);
  sim_time = 36000;
  gd_poke_int(H_SESSION_INT, 321);
  gd_set_str(H_SESSION_STR, "deku_talked");
  uint8_t *cam_st = cam_comp_mem + 0x5B8;
  float saved_eye[3] = { 12.0f, 22.0f, 35.0f };
  memcpy(cam_st + 0x450, saved_eye, 12);
  memcpy(cam_glob_mem + 0x38, saved_eye, 12);

  assert(memfile_save(0));
  complete_mock_save(GD_SAVE_MAGIC, 256, "DEKU_SAVE");
  memfile_frame();
  assert(memfiles[0].used && memfiles[0].child == 1);

  /* Change current game state so full reload is chosen */
  uintptr_t old_link = (uintptr_t)cam_actor_mem;
  sim_link_actor = old_link;
  gd_set_str(GD_STR_MAP, "HyruleField_Child");
  gd_set_str(GD_STR_EVENT, "OldBootEvent");
  gd_set_str(GD_STR_STARTER, "OldStarter");
  gd_poke_int(H_SESSION_INT, 0);
  gd_set_str(H_SESSION_STR, "");
  sim_hp = 4; gd_poke_int(F_MAGIC, 0);
  sim_pos = (vec3f_t){ 0, 0, 0 }; sim_fx = 0; sim_fz = 1;
  sim_time = 0;

  assert(memfile_load(0) && phase == L_WAIT_LOADING && warp_silent_calls == 1);
  /* Reservation completes */
  sim_warp_pending = false;
  memfile_frame();
  bool b = false;
  assert(gd_get_bool(F_ALREADY_RESET_LOAD, &b) && b);
  assert(gd_get_bool(F_SET_CHILD_MANUALLY, &b) && b);
  assert(gd_get_bool(F_OVERRIDE_TRAVERSE, &b) && b);
  float ov_pos[3] = { 0 };
  read_vec3(F_OVERRIDE_TRAVERSE_POS, ov_pos);
  assert(ov_pos[0] == 10.0f && ov_pos[1] == 20.0f && ov_pos[2] == 30.0f);
  assert(strcmp(gd_get_str(GD_STR_EVENT), "") == 0);
  assert(strcmp(gd_get_str(GD_STR_STARTER), "") == 0);
  assert(req_load_calls == 0); /* still wait for loading */

  /* Engine starts loading -> GD load submitted */
  gd_set_bool(F_IS_LOADING_BANC, true);
  sim_link_actor = 0;
  memfile_frame();
  assert(req_load_calls == 1 && phase == L_FULL_WAIT_GD);

  /* Still pending 1 frame */
  memfile_frame();
  assert(phase == L_FULL_WAIT_GD);

  /* Deserializer completes & restores saved map/scalars */
  gd_set_str(GD_STR_MAP, "DekuTree");
  gd_set_bool(F_IS_CHILDHOOD, true);
  gd_poke_int(F_MAGIC, 32);
  complete_mock_load(true);
  memfile_frame();
  assert(phase == L_WAIT_ARRIVAL);
  int32_t si = 0;
  assert(gd_get_int(H_SESSION_INT, &si) && si == 321);
  assert(strcmp(gd_get_str(H_SESSION_STR), "deku_talked") == 0);
  assert(wm_load_calls == 1 && navi_load_calls == 1 && quests_resyncs == 1 && sim_time == 36000);

  /* Map finishes loading with new actor */
  gd_set_bool(F_IS_LOADING_BANC, false);
  sim_link_actor = old_link + 8; /* note: must remain in mapped memory? memfile.c checks actor+0x270 in camera only, or wait_arrival checks link_actor() != old_actor */
  memfile_frame();
  assert(phase == L_WAIT_SETTLE);
  assert(gd_get_bool(F_OVERRIDE_TRAVERSE, &b) && !b);
  assert(gd_get_bool(F_SET_CHILD_MANUALLY, &b) && !b);
  assert(gd_get_bool(F_ALREADY_RESET_LOAD, &b) && !b);

  /* QUIET_FRAMES */
  for (int i = 0; i < QUIET_FRAMES; i++)
    memfile_frame();
  assert(phase == L_MOVING && teleport_calls == 1 && gear_calls == 1 && sim_hp == 16);

  /* Advance 2 frames in L_MOVING -> reaches L_CAMERA */
  memfile_frame();
  assert(phase == L_MOVING); /* frame 1 rejected */
  memfile_frame();
  assert(phase == L_CAMERA); /* frame 2 accepted */

  /* Camera phase */
  sim_link_actor = (uintptr_t)cam_actor_mem; /* back to mapped actor for camera chain */
  memcpy(cam_st + 0x450, saved_eye, 12);
  for (int i = 0; i < CAMERA_WRITE_FRAMES; i++)
    memfile_frame();
  /* written; then watched, since a fresh map can turn the camera back to its default */
  assert(phase == L_CAMERA && camera_guard == 1 && status[0] == 0 && cam_snap_calls == CAMERA_WRITE_FRAMES);
  assert(cam_arrive_calls == 0); /* a map's arrival did it already */
  memfile_frame();
  assert(phase == L_CAMERA && camera_guard == 2); /* still the saved angle: nothing to do */

  /* the map turns it 90 degrees: put back again at once, the writes run again */
  float turned_eye[3] = { saved_eye[2], saved_eye[1], -saved_eye[0] };
  memcpy(cam_glob_mem + 0x38, turned_eye, 12);
  memfile_frame();
  assert(phase == L_CAMERA && camera_guard == 0 && camera_reapplies == 1 && camera_frames == 1);
  assert(cam_snap_calls == CAMERA_WRITE_FRAMES + 1);
  memcpy(cam_glob_mem + 0x38, saved_eye, 12); /* the writes took */
  for (int i = 1; i < CAMERA_WRITE_FRAMES; i++)
    memfile_frame();
  assert(phase == L_CAMERA && camera_guard == 1 && cam_snap_calls == 2 * CAMERA_WRITE_FRAMES);
  for (int i = 0; i < CAMERA_GUARD; i++)
    memfile_frame();
  assert(phase == L_IDLE && camera_reapplies == 1 && cam_snap_calls == 2 * CAMERA_WRITE_FRAMES);

  puts("PASS: silent full reload happy path through GD, session, world/navi, link, and camera");
}

/* --- Test 4: fast same-map ResetGimmick path + childhood parity + fallback triggers ------------- */

static void test_fast_same_map_reset_gimmick(void)
{
  for (int child_flag = 0; child_flag <= 1; child_flag++) {
    init_env(0x5B8);
    gd_set_str(GD_STR_MAP, "KokiriForest");
    gd_set_bool(F_IS_CHILDHOOD, child_flag != 0);
    sim_pos = (vec3f_t){ 5.0f, 1.0f, -4.0f };
    sim_fx = 0.0; sim_fz = 1.0;
    sim_hp = 14; sim_max_hp = 20;
    gd_poke_int(F_MAGIC, 24);
    gd_poke_int(H_SESSION_INT, 555);
    sim_time = 43200;

    assert(memfile_save(0));
    complete_mock_save(GD_SAVE_MAGIC, 200, "FAST_SLOT");
    memfile_frame();
    assert(memfiles[0].used && memfiles[0].child == child_flag);

    /* Mutate live state on same map & same age */
    sim_pos = (vec3f_t){ 99.0f, 50.0f, 99.0f };
    sim_hp = 6;
    gd_poke_int(F_MAGIC, 4);
    gd_poke_int(H_SESSION_INT, 1);
    sim_time = 1000;

    assert(memfile_load(0) && phase == L_FAST_WAIT_GD && warp_silent_calls == 0);
    gd_poke_int(F_MAGIC, 24);
    complete_mock_load(true);
    memfile_frame();
    assert(phase == L_FAST_MOVE_PRE && wm_load_calls == 1 && navi_load_calls == 1 && quests_resyncs == 1);
    assert(gear_calls == 1 && sim_hp == 14 && sim_time == 43200);
    int32_t si = 0;
    assert(gd_get_int(H_SESSION_INT, &si) && si == 555);

    /* Wait 2 frames in L_FAST_MOVE_PRE -> triggers ResetGimmick */
    memfile_frame();
    assert(phase == L_FAST_MOVE_PRE);
    memfile_frame();
    assert(phase == L_FAST_WAIT_RESET);
    bool req = false;
    int32_t opt = -1;
    assert(gd_get_bool(F_REQ_RESET_GIMMICK, &req) && req);
    assert(gd_get_int(F_RESET_GIMMICK_OPT, &opt) && opt == 0);

    /* Simulate ResetGimmickImpl briefly nudging MP before clearing req */
    gd_poke_int(F_MAGIC, 0);
    memfile_frame();
    gd_set_bool(F_REQ_RESET_GIMMICK, false);
    memfile_frame();
    /* L_FAST_WAIT_RESET must re-apply vitals/gear and enter L_MOVING */
    int32_t mp = 0;
    assert(gd_get_int(F_MAGIC, &mp) && mp == 24);
    assert(gear_calls == 2 && phase == L_MOVING);

    memfile_frame();
    memfile_frame();
    assert(phase == L_CAMERA);
    /* in place: no handover to wait for, so the writes start at once (the drawn eye isn't set up here) */
    for (int i = 0; i < CAMERA_WRITE_FRAMES - 1; i++)
      memfile_frame();
    assert(phase == L_CAMERA);
    memfile_frame();
    assert(phase == L_IDLE && cam_arrive_calls == 1); /* in place: the target reset as on arrival, once */
    cam_arrive_calls = 0;

    /* Verify fallback to full reload for the 4 disqualifiers; a cutscene (why 1) refuses the load instead */
    for (int why = 0; why < 5; why++) {
      phase = L_IDLE;
      memfile_fast_reload = true;
      gd_set_str(GD_STR_MAP, "KokiriForest");
      gd_set_bool(F_IS_CHILDHOOD, child_flag != 0);
      gd_set_bool(F_IS_GAME_OVER, false);
      sim_ev_running = false;
      sim_warp_pending = sim_warp_requested = sim_warp_abandoned = false;
      sim_hp = 12;
      warp_silent_calls = 0;
      if (why == 0) gd_set_bool(F_IS_CHILDHOOD, child_flag == 0);
      if (why == 1) sim_ev_running = true;
      if (why == 2) memfile_fast_reload = false;
      if (why == 3) gd_set_bool(F_IS_GAME_OVER, true);
      if (why == 4) sim_hp = 0;
      if (why == 1) {
        sim_ev_skip_accept = false;
        assert(!memfile_load(0) && phase == L_IDLE && warp_silent_calls == 0);
        sim_ev_skip_accept = true;
      } else
        assert(memfile_load(0) && phase == L_WAIT_LOADING && warp_silent_calls == 1);
    }

    /* refused outright: saving or loading while paused, saving in a cutscene or a game over */
    phase = L_IDLE;
    sim_ev_running = false;
    gd_set_bool(F_IS_GAME_OVER, false);
    sim_paused = true;
    assert(!memfile_load(0) && phase == L_IDLE);
    assert(!memfile_save(1) && phase == L_IDLE);
    sim_paused = false;
    sim_ev_running = true;
    assert(!memfile_save(1) && phase == L_IDLE);
    sim_ev_running = false;
    gd_set_bool(F_IS_GAME_OVER, true);
    assert(!memfile_save(1) && phase == L_IDLE);
    gd_set_bool(F_IS_GAME_OVER, false);
  }

  puts("PASS: fast same-map ResetGimmick (child & adult), 4 full-reload fallbacks, cutscene/pause refusals");
}

/* --- Test 5: link_at frame delay, position tolerance, facing dot & fallback -------------------- */

static void test_link_at_tolerance_and_facing(void)
{
  init_env(0x5B8);
  memfile_t st = { .x = 10.0, .y = 20.0, .z = 30.0, .fx = 0.0, .fz = 1.0 };

  sim_pos = (vec3f_t){ 10.0f, 20.0f, 30.0f };
  sim_fx = 0.0; sim_fz = 1.0;
  assert(!link_at(&st, 0));
  assert(!link_at(&st, 1)); /* always rejects frame 1 */
  assert(link_at(&st, 2));

  /* Wrong yaw rejected before frame 12, accepted on frame >= 12 */
  sim_fx = 1.0; sim_fz = 0.0;
  assert(!link_at(&st, 2));
  assert(!link_at(&st, 11));
  assert(link_at(&st, 12));

  /* Near boundary within 0.25u & dot >= 0.98 */
  sim_pos = (vec3f_t){ 10.24f, 19.76f, 30.24f };
  sim_fx = 0.1; sim_fz = 0.994987; /* dot = 0.995 >= 0.98 */
  assert(link_at(&st, 2));
  sim_pos = (vec3f_t){ 10.26f, 20.0f, 30.0f };
  assert(!link_at(&st, 2));
  assert(!link_at(&st, 12));

  /* Missing saved facing ignores yaw check */
  st.fx = 0.0; st.fz = 0.0;
  sim_pos = (vec3f_t){ 10.0f, 20.0f, 30.0f };
  sim_fx = -1.0; sim_fz = 0.0;
  assert(link_at(&st, 2));

  puts("PASS: link_at frame gate, 0.25u tolerance, yaw dot check, and frame-12 fallback");
}

/* --- Test 6: direct camera AI action chain, fallback scan & non-0x5B8 state offset ------------ */

static void test_camera_ai_chain(void)
{
  const uint32_t state_off = 0x600;
  init_env(state_off);

  /* Populate distinct plain floats in both same-vtable nodes and state+CAMERA_TURN_OFF (0x600+0x7F0) */
  uint8_t *turn = cam_comp_mem + state_off + CAMERA_TURN_OFF;
  for (uint32_t o = 0; o < CAMERA_TURN_BYTES; o += 4) {
    float v = 10.0f + (float)(o / 4);
    memcpy(turn + o, &v, 4);
  }
  for (size_t r = 0; r < sizeof(action_ranges) / sizeof(*action_ranges); r++) {
    for (uint32_t o = action_ranges[r].begin; o < action_ranges[r].end; o += 4) {
      float v1 = 100.0f + (float)o;
      float v2 = 200.0f + (float)o;
      memcpy(node_ok_mem + o, &v1, 4);
      memcpy(node_ok2_mem + o, &v2, 4);
    }
  }

  memfile_t snap = { 0 };
  snap.have_camera = camera_take(snap.camera);
  snap.have_component = camera_take_component(&snap);
  camera_take_actions(&snap);
  assert(snap.have_camera && snap.have_component && snap.camera_actions == 2);
  assert(snap.action_node_idx[0] == 10 && snap.action_node_idx[1] == 25);

  /* Corrupt live component turn + both node floats to different plain floats, then restore */
  for (uint32_t o = 0; o < CAMERA_TURN_BYTES; o += 4) {
    float z = 999.0f;
    memcpy(turn + o, &z, 4);
  }
  for (size_t r = 0; r < sizeof(action_ranges) / sizeof(*action_ranges); r++) {
    for (uint32_t o = action_ranges[r].begin; o < action_ranges[r].end; o += 4) {
      float z = -555.0f;
      memcpy(node_ok_mem + o, &z, 4);
      memcpy(node_ok2_mem + o, &z, 4);
    }
  }

  camera_put_component(&snap);
  camera_put_actions(&snap, true);
  assert(camera_put(snap.camera) && cam_snap_calls == 1);

  for (uint32_t o = 0; o < CAMERA_TURN_BYTES; o += 4) {
    float v = 0;
    memcpy(&v, turn + o, 4);
    assert(v == 10.0f + (float)(o / 4));
  }
  for (size_t r = 0; r < sizeof(action_ranges) / sizeof(*action_ranges); r++) {
    for (uint32_t o = action_ranges[r].begin; o < action_ranges[r].end; o += 4) {
      float v1 = 0, v2 = 0;
      memcpy(&v1, node_ok_mem + o, 4);
      memcpy(&v2, node_ok2_mem + o, 4);
      assert(v1 == 100.0f + (float)o);
      assert(v2 == 200.0f + (float)o);
    }
  }

  /* Safety against corrupted back-pointers / unmapped nodes */
  uintptr_t out_actions[CAMERA_ACTIONS];
  uintptr_t wrong = 0x1234;
  memcpy(unit_ok_mem + 0x08, &wrong, 8);
  assert(camera_find_actions(out_actions) == 0);
  uintptr_t ok_ctrl = (uintptr_t)ctrl_ok_mem;
  memcpy(unit_ok_mem + 0x08, &ok_ctrl, 8);
  assert(camera_find_actions(out_actions) == 2);
  memcpy(ctrl_ok_mem + 0x1F0, &wrong, 8);
  assert(camera_find_actions(out_actions) == 0);

  /* Fallback bounded scan finds an action in cam_region when direct AIComponent pointer is missing */
  memset(cam_comps_mem + 0x48, 0, 8);
  uintptr_t act_vt = z64hd_main + 0x20000;
  uintptr_t ok_state = (uintptr_t)cam_comp_mem + state_off;
  uint16_t scan_idx = 42;
  memcpy(cam_region.scan_node, &act_vt, 8);
  memcpy(cam_region.scan_node + 0x1C, &scan_idx, 2);
  memcpy(cam_region.scan_node + 0x20, &ok_state, 8);
  for (size_t r = 0; r < sizeof(action_ranges) / sizeof(*action_ranges); r++) {
    for (uint32_t o = action_ranges[r].begin; o < action_ranges[r].end; o += 4) {
      float v = 300.0f + (float)o;
      memcpy(cam_region.scan_node + o, &v, 4);
    }
  }
  assert(camera_find_actions(out_actions) == 1 && out_actions[0] == (uintptr_t)cam_region.scan_node);
  memfile_t scan_snap = { 0 };
  camera_take_actions(&scan_snap);
  assert(scan_snap.camera_actions == 1 && scan_snap.action_node_idx[0] == 42);
  for (size_t r = 0; r < sizeof(action_ranges) / sizeof(*action_ranges); r++) {
    for (uint32_t o = action_ranges[r].begin; o < action_ranges[r].end; o += 4) {
      float z = -777.0f;
      memcpy(cam_region.scan_node + o, &z, 4);
    }
  }
  camera_put_actions(&scan_snap, true);
  for (size_t r = 0; r < sizeof(action_ranges) / sizeof(*action_ranges); r++) {
    for (uint32_t o = action_ranges[r].begin; o < action_ranges[r].end; o += 4) {
      float v = 0;
      memcpy(&v, cam_region.scan_node + o, 4);
      assert(v == 300.0f + (float)o);
    }
  }
  /* Scan rejects unavailable / read-only query cleanly */
  block_scan_query = true;
  assert(camera_find_actions(out_actions) == 0);
  block_scan_query = false;

  puts("PASS: camera AI node traversal, node-idx match, scan fallback, non-0x5B8 turn, and safety");
}

static void fill_test_memfile(memfile_t *st)
{
  *st = (memfile_t){ 0 };
  st->save_size = st->save_cap = 4096;
  st->save_buf = alloc16(st->save_size);
  for (uint32_t i = 0; i < st->save_size; i++)
    st->save_buf[i] = (uint8_t)(i * 7u);
  uint32_t magic = GD_SAVE_MAGIC;
  memcpy(st->save_buf, &magic, 4);
  st->s_scalar_count = 2;
  st->s_scalars = alloc16(2 * sizeof(session_scalar_t));
  st->s_scalars[0] = (session_scalar_t){ H_SESSION_INT, K_INT, 4, { 5, 0, 0, 0 } };
  st->s_scalars[1] = (session_scalar_t){ H_SESSION_BOOL, K_BOOL, 1, { 1 } };
  st->s_str_count = 1;
  st->s_strs = alloc16(sizeof(session_str_t));
  st->s_strs[0].hash = H_SESSION_STR;
  snprintf(st->s_strs[0].val, GD_STR_LEN, "hello");
  snprintf(st->map, GD_STR_LEN, "DekuTree");
  snprintf(st->start, GD_STR_LEN, "Start_00");
  st->x = 1.5, st->y = -2.25, st->z = 300.125, st->fx = 0.6, st->fz = 0.8;
  st->have_pos = st->have_life = st->have_time = st->have_nayru = true;
  st->life = 48, st->magic = 0, st->time = 43200, st->child = 1;
  st->nayru_left = 600.0f, st->nayru_total = 1200.0f;
  st->have_camera = st->have_component = true;
  for (uint32_t i = 0; i < CAMERA_BYTES; i++)
    st->camera[i] = (uint8_t)i;
  st->comp_head[0x130] = 9;
  st->comp_turn[4] = 3;
  st->camera_actions = 2;
  st->action_vtable[0] = 0x123450, st->action_vtable[1] = 0x678900;
  st->action_node_idx[0] = 4, st->action_node_idx[1] = 11;
  st->action[1][0x150] = 0x42;
  for (int i = 0; i < 6; i++)
    st->view[i] = (float)i * 1.5f;
  st->used = true;
}

static uint8_t *sd_data(const char *name, size_t *size)
{
  char path[256];
  snprintf(path, sizeof(path), "%s/%s%s", SZM_DIR, name, MEMFILE_EXT);
  int i = sd_find(path);
  assert(i >= 0);
  *size = sd_files[i].size;
  return sd_files[i].data;
}

static void test_szm_export_import(void)
{
  test_build.version = "1.0.0";
  phase = L_IDLE;
  for (int i = 0; i < MEMFILE_SLOTS; i++)
    free_memfile(&memfiles[i]);
  fill_test_memfile(&memfiles[4]);

  /* empty slot / no card / bad name refuse */
  assert(!memfile_export(0, "x") && !strcmp(last_notice, "that memfile is empty"));
  sd_mounted = false;
  assert(!memfile_export(4, "x"));
  sd_mounted = true;
  assert(!memfile_export(4, " ..") && !strcmp(last_notice, "a memfile needs a name to export"));

  /* export, cleaning the name; the same name again refuses */
  assert(memfile_export(4, "  Deku: skip?. "));
  assert(memfile_export(4, "beta"));
  assert(!memfile_export(4, "beta") && strstr(last_notice, "already"));
  size_t size;
  uint8_t *data = sd_data("Deku_ skip_", &size);
  szm_header_t h;
  memcpy(&h, data, sizeof(h));
  assert(!memcmp(h.magic, "SZMF", 4) && h.format == SZM_FORMAT && !strcmp(h.game, "1.0.0") &&
         !strcmp(h.sloperina, BUILD_VERSION) && !strcmp(h.map, "DekuTree") && h.child == 1);

  /* the listing: sorted, without the extension; a file that isn't .szm is left out */
  assert(fs_create_file(SZM_DIR "/notes.txt", 4) == 0);
  assert(memfile_list_files() == 2);
  assert(!strcmp(memfile_file_name(0), "beta") && !strcmp(memfile_file_name(1), "Deku_ skip_"));

  /* import round trip into slot 2 */
  assert(memfile_import(1, 1));
  const memfile_t *a = &memfiles[4], *b = &memfiles[1];
  assert(b->used && b->save_size == a->save_size && !memcmp(b->save_buf, a->save_buf, a->save_size));
  assert(b->s_scalar_count == 2 && b->s_scalars[0].hash == H_SESSION_INT && b->s_scalars[0].val[0] == 5 &&
         b->s_scalars[1].kind == K_BOOL && b->s_scalars[1].width == 1);
  assert(b->s_str_count == 1 && !strcmp(b->s_strs[0].val, "hello"));
  assert(!strcmp(b->map, "DekuTree") && !strcmp(b->start, "Start_00"));
  assert(b->x == a->x && b->y == a->y && b->z == a->z && b->fx == a->fx && b->fz == a->fz);
  assert(b->have_pos && b->have_life && !b->have_magic && b->have_time && b->have_nayru);
  assert(b->life == 48 && b->time == 43200 && b->child == 1 && b->nayru_left == 600.0f && b->nayru_total == 1200.0f);
  assert(b->have_camera && b->have_component && !memcmp(b->camera, a->camera, CAMERA_BYTES));
  assert(b->comp_head[0x130] == 9 && b->comp_turn[4] == 3 && !memcmp(b->view, a->view, sizeof(a->view)));
  assert(b->camera_actions == 2 && b->action_vtable[1] == 0x678900 && b->action_node_idx[1] == 11 &&
         b->action[1][0x150] == 0x42);

  /* damaged, another game version, a newer format, not a memfile: refused, the slot left as it was */
  data[sizeof(szm_header_t) + 20] ^= 1;
  assert(!memfile_import(1, 2) && !strcmp(last_notice, "that memfile is damaged") && !memfiles[2].used);
  data[sizeof(szm_header_t) + 20] ^= 1;
  test_build.version = "1.1.1";
  assert(!memfile_import(1, 2) && strstr(last_notice, "another version") && !memfiles[2].used);
  test_build.version = "1.0.0";
  h.format = SZM_FORMAT + 1;
  memcpy(data, &h, sizeof(h));
  assert(!memfile_import(1, 2) && strstr(last_notice, "newer sloperina"));
  h.format = SZM_FORMAT;
  memcpy(data, &h, sizeof(h));
  data[0] = 'X';
  assert(!memfile_import(1, 2) && strstr(last_notice, "isn't a memfile"));
  data[0] = 'S';

  /* a later version's file: a longer header and a section this one doesn't know, both skipped over */
  {
    size_t n = size + 8 + 16;
    uint8_t *f = calloc(1, n);
    szm_header_t later = h;
    later.header_size = (uint16_t)(sizeof(h) + 8);
    memcpy(f, &later, sizeof(later));
    memcpy(f + later.header_size, data + sizeof(h), size - sizeof(h));
    uint32_t tag = SZM_TAG('X', 'T', 'R', 'A'), tag_size = 8;
    memcpy(f + n - 16, &tag, 4);
    memcpy(f + n - 12, &tag_size, 4);
    later.body_size = (uint32_t)(n - later.header_size);
    later.body_crc = crc32(f + later.header_size, later.body_size);
    memcpy(f, &later, sizeof(later));
    assert(fs_create_file(SZM_DIR "/zeta" MEMFILE_EXT, (int64_t)n) == 0);
    memcpy(sd_files[sd_find(SZM_DIR "/zeta" MEMFILE_EXT)].data, f, n);
    free(f);
    assert(memfile_list_files() == 3 && !strcmp(memfile_file_name(2), "zeta"));
    assert(memfile_import(2, 2) && memfiles[2].used && memfiles[2].save_size == a->save_size &&
           memfiles[2].camera_actions == 2 && memfiles[2].x == a->x);
  }

  /* busy refuses */
  phase = L_WAIT_LOADING;
  assert(!memfile_import(0, 3) && !memfile_export(4, "gamma"));
  phase = L_IDLE;

  for (int i = 0; i < MEMFILE_SLOTS; i++)
    free_memfile(&memfiles[i]);
  for (int i = 0; i < SD_FILES; i++)
    if (sd_files[i].used)
      fs_delete_file(sd_files[i].path);
  puts("PASS: .szm export/import round trip, name cleaning, listing, version/format/damage refusals");
}

/* --- Test 8: WarpMgr and Main.module through the scene's components, no heap walk ----------------- */
static uintptr_t chain_main_slot;                    /* main+scene_mgr_ptr */
static uintptr_t chain_warp_vt[8], chain_listener_vt[1];
static int       chain_warp_changed;
static void fake_warp_changed(uintptr_t mgr, const void *bits) { (void)mgr; (void)bits; chain_warp_changed++; }
static _Alignas(16) uint8_t chain_scene_mgr[0x200], chain_scene[0x60], chain_warp[0xD0], chain_seq[0x100];
static uintptr_t chain_components[23], chain_holder_list[1], chain_defs[2];
static _Alignas(16) uint8_t chain_holders[0x20], chain_holder[0x20], chain_ctrl[0x180], chain_def_other[0x20],
  chain_def_main[0x20], chain_other_file[0x40], chain_main_file[MAIN_AINB_SIZE];

static void put_ptr(void *at, const void *p) { uintptr_t v = (uintptr_t)p; memcpy(at, &v, 8); }

static void test_scene_chains(void)
{
  clear_regions();
  uintptr_t lo = (uintptr_t)&chain_main_slot;
  if ((uintptr_t)chain_warp_vt < lo) lo = (uintptr_t)chain_warp_vt;
  if ((uintptr_t)chain_listener_vt < lo) lo = (uintptr_t)chain_listener_vt;
  if ((uintptr_t)fake_warp_changed < lo) lo = (uintptr_t)fake_warp_changed;
  z64hd_main = lo - 0x1000u;
  test_build = (struct z64hd_build){
    .scene_mgr_ptr = (uint32_t)((uintptr_t)&chain_main_slot - z64hd_main),
    .vt_warp_mgr = (uint32_t)((uintptr_t)chain_warp_vt - z64hd_main),
  };
  chain_warp_vt[6] = (uintptr_t)fake_warp_changed;
  assert(chain_warp_vt[6] - z64hd_main < 0x3C80000u && (uintptr_t)chain_listener_vt - z64hd_main < 0x5000000u);

  /* the scene: component 0 Sequence, 22 WarpMgr */
  chain_main_slot = (uintptr_t)chain_scene_mgr;
  put_ptr(chain_scene_mgr + SCENE_CURRENT, chain_scene);
  uint32_t n = 23; memcpy(chain_scene + SCENE_COUNT, &n, 4);
  put_ptr(chain_scene + SCENE_COMPONENTS, chain_components);
  chain_components[0] = (uintptr_t)chain_seq;
  chain_components[SCENE_WARP_MGR] = (uintptr_t)chain_warp;
  put_ptr(chain_warp, chain_warp_vt);
  put_ptr(chain_warp + 0xB8, chain_listener_vt);

  /* Sequence -> holders -> holder -> controller -> definitions (a decoy, then Main.module) -> files */
  put_ptr(chain_seq + SEQ_HOLDERS, chain_holders);
  uint32_t one = 1; memcpy(chain_holders, &one, 4);
  put_ptr(chain_holders + 8, chain_holder_list);
  chain_holder_list[0] = (uintptr_t)chain_holder;
  put_ptr(chain_holder + HOLDER_CONTROLLER, chain_ctrl);
  uint32_t two = 2; memcpy(chain_ctrl + CTRL_DEF_COUNT, &two, 4);
  put_ptr(chain_ctrl + CTRL_DEFS, chain_defs);
  chain_defs[0] = (uintptr_t)chain_def_other;
  chain_defs[1] = (uintptr_t)chain_def_main;
  put_ptr(chain_def_other + DEF_FILE, chain_other_file);
  put_ptr(chain_def_main + DEF_FILE, chain_main_file);
  memcpy(chain_other_file, main_ainb_head, 8); /* same magic, another file */
  memset(chain_main_file, 0, sizeof(chain_main_file));
  memcpy(chain_main_file, main_ainb_head, sizeof(main_ainb_head));
  memcpy(chain_main_file + MAIN_AINB_RESET_TO, main_ainb_reset_plug, sizeof(main_ainb_reset_plug));

  const uint32_t rw = SYS_PERM_R | SYS_PERM_W;
  add_region(&chain_main_slot, sizeof(chain_main_slot), SYS_PERM_R);
  add_region(chain_warp_vt, sizeof(chain_warp_vt), SYS_PERM_R);
  add_region(chain_scene_mgr, sizeof(chain_scene_mgr), rw);
  add_region(chain_scene, sizeof(chain_scene), rw);
  add_region(chain_components, sizeof(chain_components), rw);
  add_region(chain_warp, sizeof(chain_warp), rw);
  add_region(chain_seq, sizeof(chain_seq), rw);
  add_region(chain_holders, sizeof(chain_holders), rw);
  add_region(chain_holder_list, sizeof(chain_holder_list), rw);
  add_region(chain_holder, sizeof(chain_holder), rw);
  add_region(chain_ctrl, sizeof(chain_ctrl), rw);
  add_region(chain_defs, sizeof(chain_defs), rw);
  add_region(chain_def_other, sizeof(chain_def_other), rw);
  add_region(chain_def_main, sizeof(chain_def_main), rw);
  add_region(chain_other_file, sizeof(chain_other_file), rw);
  add_region(chain_main_file, sizeof(chain_main_file), rw);

  /* WarpMgr: found as component 22 and told GameData changed */
  uintptr_t vt = MAIN(test_build.vt_warp_mgr);
  assert(find_warp_mgr(vt) == (uintptr_t)chain_warp);
  memfile_t st = { 0 };
  chain_warp_changed = 0;
  refresh_warp_mgr(&st);
  assert(chain_warp_changed == 1);
  /* not a WarpMgr there, or too few components: nothing */
  put_ptr(chain_warp, chain_listener_vt);
  assert(!find_warp_mgr(vt));
  put_ptr(chain_warp, chain_warp_vt);
  n = SCENE_WARP_MGR; memcpy(chain_scene + SCENE_COUNT, &n, 4);
  assert(!find_warp_mgr(vt));
  n = 23; memcpy(chain_scene + SCENE_COUNT, &n, 4);

  /* Main.module: the decoy is passed over, the transition patched to the root (once; again is harmless) */
  patch_main_module();
  assert(chain_main_file[MAIN_AINB_RESET_TO] == 23);
  patch_main_module();
  assert(chain_main_file[MAIN_AINB_RESET_TO] == 23);
  /* a plug that isn't the expected transition is left alone */
  chain_main_file[MAIN_AINB_RESET_TO] = 15;
  chain_main_file[MAIN_AINB_RESET_TO + 4] = 8;
  patch_main_module();
  assert(chain_main_file[MAIN_AINB_RESET_TO] == 15);
  /* an unknown build (no scene manager) finds nothing and doesn't crash */
  test_build.scene_mgr_ptr = 0;
  patch_main_module();
  assert(!find_warp_mgr(vt));
  printf("PASS: WarpMgr and Main.module through the scene's components, decoy file, refusals\n");
}

/* --- Test 9: .szm files from later and earlier sloperina versions ------------------------------- */
/* a file as a later version might write it: every growable part grown, an unknown section; or (short_camera) the
 * camera's bytes shorter than this version's */
static uint8_t *later_szm(const memfile_t *st, bool short_camera, uint16_t format, uint32_t *size)
{
  const uint32_t extra = 8, hdr = (uint32_t)sizeof(szm_header_t) + extra;
  szm_out_t o = { calloc(1, 1u << 20), hdr, 1u << 20, true };
  uint8_t pad[16] = { 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE };
  uint32_t at = out_begin(&o, SZM_GAMEDATA);
  out_bytes(&o, st->save_buf, st->save_size);
  out_end(&o, at);
  at = out_begin(&o, SZM_SCALARS);
  uint32_t record = SZM_SCALAR_SIZE + extra;
  OUT(&o, record);
  for (uint32_t i = 0; i < st->s_scalar_count; i++) {
    const session_scalar_t *e = &st->s_scalars[i];
    OUT(&o, e->hash);
    OUT(&o, e->kind);
    OUT(&o, e->width);
    out_bytes(&o, e->val, sizeof(e->val));
    out_bytes(&o, pad, extra);
  }
  out_end(&o, at);
  at = out_begin(&o, SZM_STRINGS);
  record = SZM_STRING_SIZE + extra;
  OUT(&o, record);
  for (uint32_t i = 0; i < st->s_str_count; i++) {
    OUT(&o, st->s_strs[i].hash);
    out_bytes(&o, st->s_strs[i].val, GD_STR_LEN);
    out_bytes(&o, pad, extra);
  }
  out_end(&o, at);
  at = out_begin(&o, SZM_TAG('N', 'E', 'W', '!'));
  out_bytes(&o, pad, sizeof(pad));
  out_end(&o, at);
  at = out_begin(&o, SZM_STATE);
  uint8_t flags = STATE_POS | STATE_LIFE | STATE_TIME | STATE_NAYRU;
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
  out_bytes(&o, pad, extra); /* a field added later */
  out_end(&o, at);
  at = out_begin(&o, SZM_CAMERA);
  uint8_t cam = CAM_STATE | CAM_COMPONENT, actions = (uint8_t)st->camera_actions;
  OUT(&o, cam);
  uint8_t camera[CAMERA_BYTES + 8];
  memcpy(camera, st->camera, CAMERA_BYTES);
  memset(camera + CAMERA_BYTES, 0xEE, 8);
  out_blob(&o, camera, short_camera ? CAMERA_BYTES - 1 : CAMERA_BYTES + 8);
  out_blob(&o, st->comp_head, CAMERA_COMP_HEAD);
  out_blob(&o, st->comp_turn, CAMERA_TURN_BYTES);
  OUT(&o, st->view);
  OUT(&o, actions);
  for (int i = 0; i < actions; i++) {
    uint32_t entry = out_sized(&o);
    OUT(&o, st->action_vtable[i]);
    OUT(&o, st->action_node_idx[i]);
    out_blob(&o, st->action[i], ACTION_BYTES);
    out_bytes(&o, pad, extra); /* a field added later */
    out_sized_end(&o, entry);
  }
  out_bytes(&o, pad, extra); /* a field added later */
  out_end(&o, at);
  assert(o.ok);
  szm_header_t h = { 0 };
  memcpy(h.magic, SZM_MAGIC, 4);
  h.format = format;
  h.header_size = (uint16_t)hdr;
  h.body_size = o.n - hdr;
  h.body_crc = crc32(o.p + hdr, h.body_size);
  h.child = (int8_t)st->child;
  copy_str(h.game, sizeof(h.game), "1.0.0");
  copy_str(h.sloperina, sizeof(h.sloperina), "v9.9.9-later");
  copy_str(h.map, sizeof(h.map), st->map);
  memcpy(o.p, &h, sizeof(h));
  memset(o.p + sizeof(h), 0xEE, extra); /* a header field added later */
  *size = o.n;
  return o.p;
}

static void test_szm_versions(void)
{
  test_build.version = "1.0.0";
  memfile_t a = { 0 }, b = { 0 };
  fill_test_memfile(&a);
  uint32_t n;

  /* a later version's file, everything grown: read as far as this version knows */
  uint8_t *f = later_szm(&a, false, SZM_FORMAT, &n);
  assert(!szm_decode(f, n, &b));
  assert(b.save_size == a.save_size && !memcmp(b.save_buf, a.save_buf, a.save_size));
  assert(b.s_scalar_count == a.s_scalar_count && b.s_scalars[0].hash == a.s_scalars[0].hash &&
         !memcmp(b.s_scalars[0].val, a.s_scalars[0].val, sizeof(a.s_scalars[0].val)) &&
         b.s_scalars[1].kind == a.s_scalars[1].kind);
  assert(b.s_str_count == a.s_str_count && !strcmp(b.s_strs[0].val, a.s_strs[0].val));
  assert(b.x == a.x && b.z == a.z && b.life == a.life && b.time == a.time && b.child == a.child && b.have_nayru);
  assert(b.have_camera && b.have_component && !memcmp(b.camera, a.camera, CAMERA_BYTES));
  assert(b.camera_actions == 2 && b.action_vtable[1] == a.action_vtable[1] &&
         b.action_node_idx[1] == a.action_node_idx[1] && b.action[1][0x150] == 0x42);
  assert(!strcmp(b.map, a.map));
  free_memfile(&b);
  free(f);

  /* camera bytes shorter than this version's: loads, without the camera */
  f = later_szm(&a, true, SZM_FORMAT, &n);
  assert(!szm_decode(f, n, &b) && b.used && b.have_pos && !b.have_camera && !b.camera_actions);
  free_memfile(&b);
  free(f);

  /* format 1 (test builds) and a newer format: refused */
  f = later_szm(&a, false, 1, &n);
  const char *why = szm_decode(f, n, &b);
  assert(why && strstr(why, "test build"));
  free_memfile(&b);
  free(f);
  f = later_szm(&a, false, SZM_FORMAT + 1, &n);
  why = szm_decode(f, n, &b);
  assert(why && strstr(why, "newer sloperina"));
  free_memfile(&b);
  free(f);
  free_memfile(&a);
  puts("PASS: .szm from later versions (grown header, records, blobs, entries, sections), short camera, formats");
}

/* --- Test 10: memfile files in folders --------------------------------------------------------- */
static void test_memfile_folders(void)
{
  test_build.version = "1.0.0";
  phase = L_IDLE;
  sd_clear();
  for (int i = 0; i < MEMFILE_SLOTS; i++)
    free_memfile(&memfiles[i]);
  fill_test_memfile(&memfiles[0]);
  memfile_browse_top();

  /* folders: made, cleaned; the same name again, no name: refused */
  assert(memfile_make_folder("Bosses") && sd_find_dir(SZM_DIR "/Bosses") >= 0);
  assert(!memfile_make_folder("Bosses") && strstr(last_notice, "already a folder"));
  assert(!memfile_make_folder("  ..") && !strcmp(last_notice, "no name: no folder made"));
  assert(memfile_make_folder("a/b:c") && sd_find_dir(SZM_DIR "/a_b_c") >= 0);
  assert(!memfile_delete_folder() && !strcmp(last_notice, "the top folder can't be deleted"));

  /* the top: folders first (A to Z), then the files */
  assert(memfile_export(0, "zz root"));
  assert(memfile_list_files() == 3 && memfile_list_folders() == 2);
  assert(!strcmp(memfile_file_name(0), "a_b_c") && !strcmp(memfile_file_name(1), "Bosses") &&
         !strcmp(memfile_file_name(2), "zz root"));
  assert(!memfile_import(0, 1)); /* a folder isn't a file */

  /* into a folder: exports go there, imports come from there */
  assert(memfile_browse_into(1) && !strcmp(memfile_browsing(), "Bosses"));
  assert(memfile_export(0, "ganon") && sd_find(SZM_DIR "/Bosses/ganon.szm") >= 0);
  assert(memfile_make_folder("Phase 2") && sd_find_dir(SZM_DIR "/Bosses/Phase 2") >= 0);
  assert(memfile_list_files() == 2 && memfile_list_folders() == 1);
  assert(!strcmp(memfile_file_name(0), "Phase 2") && !strcmp(memfile_file_name(1), "ganon"));
  assert(memfile_import(1, 2) && memfiles[2].used && memfiles[2].x == memfiles[0].x);

  /* delete one file: only a file, from the folder looked at; the list without it */
  assert(memfile_export(0, "spare") && memfile_list_files() == 3 && !strcmp(memfile_file_name(2), "spare"));
  assert(!memfile_delete_file(0) && sd_find_dir(SZM_DIR "/Bosses/Phase 2") >= 0); /* a folder: no */
  assert(memfile_delete_file(2) && sd_find(SZM_DIR "/Bosses/spare.szm") < 0 && sd_find(SZM_DIR "/Bosses/ganon.szm") >= 0);
  assert(memfile_list_files() == 2 && !strcmp(memfile_file_name(1), "ganon") && !memfile_delete_file(2));

  /* up stops at the top */
  assert(memfile_browse_into(0) && !strcmp(memfile_browsing(), "Bosses/Phase 2"));
  memfile_browse_up();
  assert(!strcmp(memfile_browsing(), "Bosses"));
  memfile_browse_up();
  assert(!strcmp(memfile_browsing(), ""));
  memfile_browse_up();
  assert(!strcmp(memfile_browsing(), ""));

  /* delete a folder: it and everything in it, then up a folder */
  assert(memfile_list_files() == 3 && memfile_browse_into(1));
  assert(memfile_delete_folder() && !strcmp(memfile_browsing(), ""));
  assert(sd_find_dir(SZM_DIR "/Bosses") < 0 && sd_find_dir(SZM_DIR "/Bosses/Phase 2") < 0 &&
         sd_find(SZM_DIR "/Bosses/ganon.szm") < 0 && sd_find(SZM_DIR "/zz root.szm") >= 0);
  assert(memfile_list_files() == 2 && memfile_list_folders() == 1);

  /* too deep: a folder past MEMFILE_FOLDER_LEN isn't made */
  const char *long_name = "abcdefghijklmnopqrstuvwxyz0123"; /* MEMFILE_NAME_MAX */
  int depth = 0;
  for (;; depth++) {
    if (!memfile_make_folder(long_name))
      break;
    assert(memfile_list_files() >= 1 && memfile_browse_into(0));
  }
  assert(depth == MEMFILE_FOLDER_LEN / (MEMFILE_NAME_MAX + 1) && strstr(last_notice, "too deep"));
  memfile_browse_top();

  sd_clear();
  for (int i = 0; i < MEMFILE_SLOTS; i++)
    free_memfile(&memfiles[i]);
  puts("PASS: memfile folders: make, list, export/import in one, delete a file, up to the top, delete a tree, depth");
}

/* --- simulated scene actors for Phase 1 tests --------------------------------------------------- */

typedef struct {
  _Alignas(8) uint8_t actor[0x4C0];
  uint8_t comps[0x300];
  uint8_t phys[0x40];
  uint8_t life[0x1C30];
  uint8_t param_cur[0x10];
  uint8_t param_max[0x10];
  uint8_t preactor[0xA0];
  uint8_t entry[0x40];
  char    name[32];
  int     force_pos_calls;
  int     delete_calls;
  bool    force_pos_ok;
} sim_actor_t;

#define N_SIM_ACTORS 12
static sim_actor_t sim_actors[N_SIM_ACTORS];
static uint8_t     sim_actor_mgr[0x50];

static bool fake_actor_force_set_pos(uintptr_t phys, const float pos[3], const float rot[9], bool immediate)
{
  assert(immediate);
  for (int i = 0; i < N_SIM_ACTORS; i++) {
    if ((uintptr_t)sim_actors[i].phys == phys) {
      sim_actors[i].force_pos_calls++;
      if (!sim_actors[i].force_pos_ok)
        return false;
      memcpy(sim_actors[i].actor + A_POS, pos, 12);
      uint32_t w0 = 0;
      memcpy(&w0, rot, 4);
      if (w0 != 0x7FC00000u)
        memcpy(sim_actors[i].actor + A_ROT, rot, 36);
      return true;
    }
  }
  assert(!"unknown physics component");
  return false;
}

static int preactor_update_calls;

static void fake_preactor_update_flags(void *node)
{
  uint64_t *n = (uint64_t *)node;
  for (int i = 4; i <= 8; i++) {
    const uint8_t *flag = (const uint8_t *)(uintptr_t)n[i];
    assert(flag && *flag == 1);
  }
  preactor_update_calls++;
}

static void fake_actor_request_delete(uintptr_t actor, uint32_t reason)
{
  assert(reason == 0);
  for (int i = 0; i < N_SIM_ACTORS; i++) {
    if ((uintptr_t)sim_actors[i].actor == actor) {
      sim_actors[i].delete_calls++;
      uint32_t dead = 9u;
      memcpy(sim_actors[i].actor + A_STATE, &dead, 4);
      return;
    }
  }
  assert(!"unknown actor for delete");
}

static void setup_sim_actor(sim_actor_t *a, const char *name, uint64_t id, uint32_t state,
                            const float pos[3], const float rot[9], bool has_phys, bool has_life,
                            int32_t hp, int32_t max_hp)
{
  memset(a, 0, sizeof(*a));
  snprintf(a->name, sizeof(a->name), "%s", name);
  a->force_pos_ok = true;
  uintptr_t np = (uintptr_t)a->name;
  memcpy(a->actor + A_NAME, &np, 8);
  memcpy(a->actor + A_STATE, &state, 4);
  memcpy(a->actor + A_POS, pos, 12);
  memcpy(a->actor + A_ROT, rot, 36);
  uint32_t flags = 0x8000u;
  memcpy(a->actor + A_FLAGS, &flags, 4);
  uintptr_t cp = (uintptr_t)a->comps;
  memcpy(a->actor + A_COMPONENTS, &cp, 8);
  if (has_phys) {
    uintptr_t pp = (uintptr_t)a->phys;
    memcpy(a->comps + ACOMP_PHYSICS, &pp, 8);
  }
  if (has_life) {
    uintptr_t lp = (uintptr_t)a->life, curp = (uintptr_t)a->param_cur, maxp = (uintptr_t)a->param_max;
    uintptr_t vt = z64hd_main + 0x1000u;
    memcpy(a->param_cur, &vt, 8);
    memcpy(a->param_cur + APARAM_VAL, &hp, 4);
    memcpy(a->param_max, &vt, 8);
    memcpy(a->param_max + APARAM_VAL, &max_hp, 4);
    memcpy(a->life + ALIFE_CUR, &curp, 8);
    memcpy(a->life + ALIFE_MAX, &maxp, 8);
    memcpy(a->comps + ACOMP_LIFE, &lp, 8);
  }
  if (id) {
    uintptr_t prep = (uintptr_t)a->preactor;
    memcpy(a->preactor + PREACTOR_INSTANCE_ID, &id, 8);
    memcpy(a->actor + A_PREACTOR, &prep, 8);
  }
  uintptr_t ap = (uintptr_t)a->actor;
  memcpy(a->entry + AENTRY_ACTOR, &ap, 8);
  add_region(a, sizeof(*a), SYS_PERM_R | SYS_PERM_W);
}

static void link_tree_node(sim_actor_t *parent, sim_actor_t *left, sim_actor_t *right, sim_actor_t *next)
{
  uintptr_t l = left ? (uintptr_t)(left->entry + AENTRY_NODE) : 0;
  uintptr_t r = right ? (uintptr_t)(right->entry + AENTRY_NODE) : 0;
  uintptr_t n = next ? (uintptr_t)next->entry : 0;
  memcpy(parent->entry + AENTRY_NODE + ANODE_LEFT, &l, 8);
  memcpy(parent->entry + AENTRY_NODE + ANODE_RIGHT, &r, 8);
  memcpy(parent->entry + AENTRY_NEXT, &n, 8);
}

static void test_scene_actors(void)
{
  init_env(0x5B8);
  test_build.version = "1.1.1";
  memfile_actors = true;
  memfile_fast_reload = true;
  memset(sim_actor_mgr, 0, sizeof(sim_actor_mgr));
  add_region(sim_actor_mgr, sizeof(sim_actor_mgr), SYS_PERM_R | SYS_PERM_W);
  main_slots.actor_mgr_ptr = (uintptr_t)sim_actor_mgr;
  test_build.actor_mgr_ptr = (uint32_t)((uintptr_t)&main_slots.actor_mgr_ptr - z64hd_main);
  test_build.actor_force_set_pos = OFF_ACTOR_FORCE_POS;
  test_build.actor_request_delete = OFF_ACTOR_REQ_DELETE;
  test_build.preactor_update_flags = OFF_PREACTOR_UPDATE;
  preactor_update_calls = 0;

  const float rot_a[9] = { 0, 0, 1, 0, 1, 0, -1, 0, 0 };
  const float rot_b[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
  const float pos_npc[3] = { 12.5f, 3.0f, -4.5f };
  const float pos_enemy[3] = { 20.0f, 1.0f, 8.0f };
  const float pos_pot[3] = { 5.0f, 2.0f, 3.0f };
  const float pos_dying[3] = { 15.0f, 1.0f, 5.0f };
  const float pos_static[3] = { 0.0f, 0.0f, 0.0f };
  const float pos_dyn[3] = { 1.0f, 1.0f, 1.0f };
  const float pos_killed_near[3] = { 18.0f, 2.0f, 10.0f };  /* within 30 m of Link (10,2,3) */
  const float pos_killed_far[3] = { 200.0f, 2.0f, 200.0f }; /* > 30 m from Link */

  /* At save time: 0=Npc, 1=Enemy(hp 5/18), 2=Obj pot(no phys), 3=Enemy(dying state 8),
   * 4=DgnObj_Wall(phys, no life: ignored), 5=dyn(no id: ignored),
   * 8=Enemy_Sentinel(id=UINT64_MAX: ignored), 9 & 10=two actors sharing colliding id 0x9999 (both poisoned!) */
  setup_sim_actor(&sim_actors[0], "Npc_Kokiri_Mido", 0x1001ULL, 4, pos_npc, rot_a, true, false, 0, 0);
  setup_sim_actor(&sim_actors[1], "Enemy_Wolfos",    0x1002ULL, 4, pos_enemy, rot_a, true, true, 5, 18);
  setup_sim_actor(&sim_actors[2], "Obj_Pot",         0x1003ULL, 4, pos_pot, rot_a, false, true, 1, 1);
  setup_sim_actor(&sim_actors[3], "Enemy_Stalfos",   0x1004ULL, 8, pos_dying, rot_b, true, true, 0, 18);
  setup_sim_actor(&sim_actors[4], "DgnObj_Wall",     0x1005ULL, 4, pos_static, rot_b, true, false, 0, 0);
  setup_sim_actor(&sim_actors[5], "Enemy_Dynamic",   0ULL,      4, pos_dyn, rot_b, true, true, 4, 4);
  setup_sim_actor(&sim_actors[8], "Enemy_Sentinel",  UINT64_MAX, 4, pos_dyn, rot_b, true, true, 4, 4);
  setup_sim_actor(&sim_actors[9], "Enemy_DupA",      0x9999ULL, 4, pos_npc, rot_a, true, true, 4, 4);
  setup_sim_actor(&sim_actors[10], "FldObj_DupB",    0x9999ULL, 4, pos_static, rot_b, false, false, 0, 0);

  /* Build tree: root=1, left=0 (next=2->8->9->10), right=3 (left=4, right=5) */
  link_tree_node(&sim_actors[0], 0, 0, &sim_actors[2]);
  link_tree_node(&sim_actors[2], 0, 0, &sim_actors[8]);
  link_tree_node(&sim_actors[8], 0, 0, &sim_actors[9]);
  link_tree_node(&sim_actors[9], 0, 0, &sim_actors[10]);
  link_tree_node(&sim_actors[10], 0, 0, 0);
  link_tree_node(&sim_actors[4], 0, 0, 0);
  link_tree_node(&sim_actors[5], 0, 0, 0);
  link_tree_node(&sim_actors[3], &sim_actors[4], &sim_actors[5], 0);
  link_tree_node(&sim_actors[1], &sim_actors[0], &sim_actors[3], 0);
  uintptr_t root = (uintptr_t)(sim_actors[1].entry + AENTRY_NODE);
  memcpy(sim_actor_mgr + AMGR_TREE_ROOT, &root, 8);

  sim_pos = (vec3f_t){ 10.0f, 2.0f, 3.0f };
  assert(memfile_save(0));
  complete_mock_save(GD_SAVE_MAGIC, 256, "actors");
  memfile_frame();
  assert(phase == L_IDLE && memfiles[0].used && memfiles[0].have_actors && !memfiles[0].actors_truncated);
  assert(memfiles[0].actor_count == 4); /* Npc, Wolfos, Pot, dying Stalfos (Wall, dynamic, sentinel, dup poisoned) */

  /* .szm export/import round-trip preserves actors */
  sd_clear();
  assert(memfile_export(0, "actors_test") && memfile_list_files() == 1 && memfile_import(0, 1));
  assert(memfiles[1].have_actors && memfiles[1].actor_count == 4 && !memfiles[1].actors_truncated);

  /* Simulate ResetGimmick: actors respawn at default positions, full HP, alive, plus 2 enemies that were killed
   * before save (one within 30 m, one 200 m away), plus an actor with a mismatched name sharing Mido's ID. */
  const float def_pos[3] = { 0.0f, 0.0f, 0.0f };
  setup_sim_actor(&sim_actors[0], "Npc_Kokiri_Mido", 0x1001ULL, 4, def_pos, rot_b, true, false, 0, 0);
  setup_sim_actor(&sim_actors[1], "Enemy_Wolfos",    0x1002ULL, 4, def_pos, rot_b, true, true, 18, 18);
  setup_sim_actor(&sim_actors[2], "Obj_Pot",         0x1003ULL, 4, def_pos, rot_b, false, true, 1, 1);
  setup_sim_actor(&sim_actors[3], "Enemy_Stalfos",   0x1004ULL, 4, def_pos, rot_b, true, true, 18, 18);
  setup_sim_actor(&sim_actors[6], "Enemy_KeeseNear", 0x1006ULL, 4, pos_killed_near, rot_b, true, true, 2, 2);
  setup_sim_actor(&sim_actors[7], "Enemy_KeeseFar",  0x1007ULL, 4, pos_killed_far, rot_b, true, true, 2, 2);
  setup_sim_actor(&sim_actors[11], "DgnObj_Door",    0x1001ULL, 4, def_pos, rot_b, true, false, 0, 0);
  link_tree_node(&sim_actors[0], 0, 0, &sim_actors[2]);
  link_tree_node(&sim_actors[2], 0, 0, &sim_actors[11]);
  link_tree_node(&sim_actors[11], 0, 0, 0);
  link_tree_node(&sim_actors[6], 0, 0, 0);
  link_tree_node(&sim_actors[7], 0, 0, 0);
  link_tree_node(&sim_actors[3], &sim_actors[6], &sim_actors[7], 0);
  link_tree_node(&sim_actors[1], &sim_actors[0], &sim_actors[3], 0);
  root = (uintptr_t)(sim_actors[1].entry + AENTRY_NODE);
  memcpy(sim_actor_mgr + AMGR_TREE_ROOT, &root, 8);

  /* Fast load slot 1 (imported from .szm) */
  assert(memfile_load(1));
  complete_mock_load(true);
  for (int i = 0; i < 60 && phase != L_IDLE; i++) {
    memfile_frame();
    if (phase == L_FAST_WAIT_RESET)
      gd_set_bool(F_REQ_RESET_GIMMICK, false);
  }
  assert(phase == L_IDLE);

  /* Verify PreActor dead/disable flags were reset before ResetGimmick */
  assert(preactor_update_calls == 1);
  /* Verify NPC moved & rotated via physics setter (pass 1 moves; pass 2 sees matching transform and skips) */
  assert(sim_actors[0].force_pos_calls == 1);
  assert(bytes_equal(sim_actors[0].actor + A_POS, pos_npc, 12));
  assert(bytes_equal(sim_actors[0].actor + A_ROT, rot_a, 36));
  /* Verify mismatched-name door sharing Mido's ID was NOT touched */
  assert(sim_actors[11].force_pos_calls == 0);
  assert(bytes_equal(sim_actors[11].actor + A_POS, def_pos, 12));
  /* Verify Enemy moved, rotated, and HP restored from 18 -> 5 */
  assert(sim_actors[1].force_pos_calls == 1);
  assert(bytes_equal(sim_actors[1].actor + A_POS, pos_enemy, 12));
  assert(*(int32_t *)(sim_actors[1].param_cur + APARAM_VAL) == 5);
  /* Verify non-physics Obj moved via fallback (A_FORCE_POS, flag 0x8000 cleared, A_POS, A_ROT) */
  assert(sim_actors[2].force_pos_calls == 0);
  assert(bytes_equal(sim_actors[2].actor + A_FORCE_POS, pos_pot, 12));
  assert(bytes_equal(sim_actors[2].actor + A_POS, pos_pot, 12));
  assert((*(uint32_t *)(sim_actors[2].actor + A_FLAGS) & 0x8000u) == 0);
  /* Verify dying Stalfos deleted, missing nearby Keese deleted, far Keese untouched */
  assert(sim_actors[3].delete_calls == 1);
  assert(sim_actors[6].delete_calls == 1);
  assert(sim_actors[7].delete_calls == 0);

  /* Toggle off: memfile_actors = false leaves respawned actors untouched */
  memfile_actors = false;
  setup_sim_actor(&sim_actors[1], "Enemy_Wolfos", 0x1002ULL, 4, def_pos, rot_b, true, true, 18, 18);
  link_tree_node(&sim_actors[1], 0, 0, 0);
  root = (uintptr_t)(sim_actors[1].entry + AENTRY_NODE);
  memcpy(sim_actor_mgr + AMGR_TREE_ROOT, &root, 8);
  assert(memfile_load(1));
  complete_mock_load(true);
  for (int i = 0; i < 60 && phase != L_IDLE; i++) {
    memfile_frame();
    if (phase == L_FAST_WAIT_RESET)
      gd_set_bool(F_REQ_RESET_GIMMICK, false);
  }
  assert(phase == L_IDLE);
  assert(sim_actors[1].force_pos_calls == 0);
  assert(*(int32_t *)(sim_actors[1].param_cur + APARAM_VAL) == 18);
  memfile_actors = true;

  sd_clear();
  for (int i = 0; i < MEMFILE_SLOTS; i++)
    free_memfile(&memfiles[i]);
  puts("PASS: scene actors: capture, .szm round-trip, transform/fallback, HP, dead/missing delete, toggle");
}

static void test_phase2_bb_vel_savepos(void)
{
  init_env(0x5B8);
  test_build.version = "1.1.1";
  memfile_actors = true;
  memfile_fast_reload = true;
  memset(sim_actor_mgr, 0, sizeof(sim_actor_mgr));
  add_region(sim_actor_mgr, sizeof(sim_actor_mgr), SYS_PERM_R | SYS_PERM_W);
  main_slots.actor_mgr_ptr = (uintptr_t)sim_actor_mgr;
  test_build.actor_mgr_ptr = (uint32_t)((uintptr_t)&main_slots.actor_mgr_ptr - z64hd_main);
  test_build.actor_force_set_pos = OFF_ACTOR_FORCE_POS;
  test_build.actor_request_delete = OFF_ACTOR_REQ_DELETE;
  test_build.preactor_update_flags = OFF_PREACTOR_UPDATE;

  /* Link actor + player/base components for fall history, JS height, restart point */
  static _Alignas(8) uint8_t link_mem[0x400], link_comps[0x400], player_comp[0x1920], base_comp[0x18B0];
  memset(link_mem, 0, sizeof(link_mem));
  memset(link_comps, 0, sizeof(link_comps));
  memset(player_comp, 0, sizeof(player_comp));
  memset(base_comp, 0, sizeof(base_comp));
  uintptr_t lp = (uintptr_t)link_mem, lcp = (uintptr_t)link_comps, pcp = (uintptr_t)player_comp, bcp = (uintptr_t)base_comp;
  memcpy(link_mem + 0x270, &lcp, 8);
  memcpy(link_comps + PLAYER_COMPONENT, &pcp, 8);
  memcpy(link_comps + 0x2B8, &bcp, 8);
  memcpy(player_comp + 0x18, &lp, 8);
  memcpy(base_comp + 0x1340, &lp, 8);
  add_region(link_mem, sizeof(link_mem), SYS_PERM_R | SYS_PERM_W);
  add_region(link_comps, sizeof(link_comps), SYS_PERM_R | SYS_PERM_W);
  add_region(player_comp, sizeof(player_comp), SYS_PERM_R | SYS_PERM_W);
  add_region(base_comp, sizeof(base_comp), SYS_PERM_R | SYS_PERM_W);
  sim_link_actor = lp;

  /* AutoSaveMgr at scene component 12 */
  static _Alignas(8) uint8_t smgr[0x200], scene[0x80], scomps[24 * 8], autosave[0x200];
  memset(smgr, 0, sizeof(smgr)); memset(scene, 0, sizeof(scene));
  memset(scomps, 0, sizeof(scomps)); memset(autosave, 0, sizeof(autosave));
  uintptr_t sc_p = (uintptr_t)scene, scs_p = (uintptr_t)scomps, as_p = (uintptr_t)autosave;
  uint32_t sc_cnt = 24;
  memcpy(smgr + SCENE_CURRENT, &sc_p, 8);
  memcpy(scene + SCENE_COUNT, &sc_cnt, 4);
  memcpy(scene + SCENE_COMPONENTS, &scs_p, 8);
  memcpy(scomps + SCENE_AUTOSAVE * 8, &as_p, 8);
  uintptr_t vt_as = z64hd_main + 0x2200u;
  memcpy(autosave, &vt_as, 8);
  add_region(smgr, sizeof(smgr), SYS_PERM_R | SYS_PERM_W);
  add_region(scene, sizeof(scene), SYS_PERM_R | SYS_PERM_W);
  add_region(scomps, sizeof(scomps), SYS_PERM_R | SYS_PERM_W);
  add_region(autosave, sizeof(autosave), SYS_PERM_R | SYS_PERM_W);
  static uintptr_t smgr_slot;
  smgr_slot = (uintptr_t)smgr;
  add_region(&smgr_slot, sizeof(smgr_slot), SYS_PERM_R | SYS_PERM_W);
  test_build.scene_mgr_ptr = (uint32_t)((uintptr_t)&smgr_slot - z64hd_main);
  test_build.vt_autosave = 0x2200u;

  /* Actor 0 with AIControllerBase -> Blackboard (S32, F32, Bool, Vec3f, U32) */
  const float pos0[3] = { 5.0f, 1.0f, 2.0f }, rot0[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
  setup_sim_actor(&sim_actors[0], "Enemy_Boss", 0x2001ULL, 4, pos0, rot0, true, true, 12, 20);
  static _Alignas(8) uint8_t aic[0x40], cset[0x20], centries[8], centry[0x20], cbase[0x210], bb[0x5A0];
  static int32_t bb_s32[2] = { 42, -7 };
  static float bb_f32[2] = { 3.5f, 1.25f };
  static uint8_t bb_bool[3] = { 1, 0, 1 };
  static float bb_vec3[3] = { 9.0f, 8.0f, 7.0f };
  static uint32_t bb_u32[1] = { 0xCAFEBABEu };
  memset(aic, 0, sizeof(aic)); memset(cset, 0, sizeof(cset)); memset(centry, 0, sizeof(centry));
  memset(cbase, 0, sizeof(cbase)); memset(bb, 0, sizeof(bb));
  uintptr_t p_aic = (uintptr_t)aic, p_cset = (uintptr_t)cset, p_carr = (uintptr_t)centries;
  uintptr_t p_ce = (uintptr_t)centry, p_cb = (uintptr_t)cbase, p_bb = (uintptr_t)bb, p_act = (uintptr_t)sim_actors[0].actor;
  uint32_t one = 1, two = 2, three = 3;
  memcpy(sim_actors[0].comps + 0x48, &p_aic, 8);
  memcpy(aic + 0x28, &p_cset, 8);
  memcpy(cset, &one, 4); memcpy(cset + 8, &p_carr, 8);
  memcpy(centries, &p_ce, 8); memcpy(centry + 0x18, &p_cb, 8);
  memcpy(cbase + 0x1F0, &p_act, 8); memcpy(cbase + 0x1F8, &p_bb, 8);
  uintptr_t ps32 = (uintptr_t)bb_s32, pf32 = (uintptr_t)bb_f32, pbool = (uintptr_t)bb_bool;
  uintptr_t pvec3 = (uintptr_t)bb_vec3, pu32 = (uintptr_t)bb_u32;
  memcpy(bb + 0x0A8, &two, 4);   memcpy(bb + 0x0B0, &ps32, 8);
  memcpy(bb + 0x120, &two, 4);   memcpy(bb + 0x128, &pf32, 8);
  memcpy(bb + 0x198, &three, 4); memcpy(bb + 0x1A0, &pbool, 8);
  memcpy(bb + 0x210, &one, 4);   memcpy(bb + 0x218, &pvec3, 8);
  memcpy(bb + 0x558, &one, 4);   memcpy(bb + 0x560, &pu32, 8);
  add_region(aic, sizeof(aic), SYS_PERM_R | SYS_PERM_W);
  add_region(cset, sizeof(cset), SYS_PERM_R | SYS_PERM_W);
  add_region(centries, sizeof(centries), SYS_PERM_R | SYS_PERM_W);
  add_region(centry, sizeof(centry), SYS_PERM_R | SYS_PERM_W);
  add_region(cbase, sizeof(cbase), SYS_PERM_R | SYS_PERM_W);
  add_region(bb, sizeof(bb), SYS_PERM_R | SYS_PERM_W);
  add_region(bb_s32, sizeof(bb_s32), SYS_PERM_R | SYS_PERM_W);
  add_region(bb_f32, sizeof(bb_f32), SYS_PERM_R | SYS_PERM_W);
  add_region(bb_bool, sizeof(bb_bool), SYS_PERM_R | SYS_PERM_W);
  add_region(bb_vec3, sizeof(bb_vec3), SYS_PERM_R | SYS_PERM_W);
  add_region(bb_u32, sizeof(bb_u32), SYS_PERM_R | SYS_PERM_W);
  link_tree_node(&sim_actors[0], 0, 0, 0);
  uintptr_t root = (uintptr_t)(sim_actors[0].entry + AENTRY_NODE);
  memcpy(sim_actor_mgr + AMGR_TREE_ROOT, &root, 8);

  /* Set Link velocity, fall/JS state, restart point, and SavePos slots A & B */
  sim_pos = (vec3f_t){ 10.0f, 5.0f, 3.0f };
  sim_vel = (vec3f_t){ 4.5f, -12.25f, 6.75f };
  set_vel_calls = 0;
  *(float *)(player_comp + PLAYER_FALL_ORIG) = 15.0f;
  *(float *)(player_comp + PLAYER_FALL_PEAK) = 18.5f;
  *(float *)(player_comp + PLAYER_JS_HEIGHT) = 2.75f;
  *(float *)(player_comp + PLAYER_FALL_DIST) = 10.0f;
  *(float *)(base_comp + BASE_FALL_ORIG) = 15.0f;
  *(float *)(base_comp + BASE_FALL_DIST) = 10.0f;
  memset(player_comp + PLAYER_RESTART, 0x5A, PLAYER_RESTART_BYTES);
  memset(autosave + AUTOSAVE_SLOT_A, 0xA1, SAVEPOS_SLOT_BYTES);
  memset(autosave + AUTOSAVE_SLOT_B, 0xB2, SAVEPOS_SLOT_BYTES);

  assert(memfile_save(0));
  complete_mock_save(GD_SAVE_MAGIC, 256, "phase2");
  memfile_frame();
  assert(phase == L_IDLE && memfiles[0].have_vel && memfiles[0].have_fall &&
         memfiles[0].have_savepos && memfiles[0].have_restart && memfiles[0].actor_bb_size > 0);

  /* Round-trip through .szm into slot 1 */
  sd_clear();
  assert(memfile_export(0, "phase2_test") && memfile_list_files() == 1 && memfile_import(0, 1));
  assert(memfiles[1].have_vel && memfiles[1].have_fall && memfiles[1].have_savepos &&
         memfiles[1].have_restart && memfiles[1].actor_bb_size == memfiles[0].actor_bb_size);

  /* Mutate everything before loading */
  sim_vel = (vec3f_t){ 0, 0, 0 };
  *(float *)(player_comp + PLAYER_JS_HEIGHT) = 0.0f;
  *(float *)(player_comp + PLAYER_FALL_DIST) = 0.0f;
  memset(player_comp + PLAYER_RESTART, 0, PLAYER_RESTART_BYTES);
  memset(autosave + AUTOSAVE_SLOT_A, 0, SAVEPOS_SLOT_BYTES);
  memset(autosave + AUTOSAVE_SLOT_B, 0, SAVEPOS_SLOT_BYTES);
  bb_s32[0] = 0; bb_s32[1] = 0;
  bb_f32[0] = 0; bb_f32[1] = 0;
  bb_bool[0] = 0; bb_bool[1] = 1; bb_bool[2] = 0;
  bb_vec3[0] = bb_vec3[1] = bb_vec3[2] = 0;
  bb_u32[0] = 0;

  assert(memfile_load(1));
  complete_mock_load(true);
  for (int i = 0; i < 60 && phase != L_IDLE; i++) {
    memfile_frame();
    if (phase == L_FAST_WAIT_RESET)
      gd_set_bool(F_REQ_RESET_GIMMICK, false);
  }
  assert(phase == L_IDLE);

  /* Verify Link velocity, fall/JS height, restart point, and SavePos restored */
  assert(set_vel_calls == 1 && last_set_vel.x == 4.5f && last_set_vel.y == -12.25f && last_set_vel.z == 6.75f);
  assert(*(float *)(player_comp + PLAYER_JS_HEIGHT) == 2.75f);
  assert(*(float *)(player_comp + PLAYER_FALL_DIST) == 10.0f);
  assert(*(float *)(base_comp + BASE_FALL_ORIG) == 15.0f);
  assert(player_comp[PLAYER_RESTART] == 0x5A && autosave[AUTOSAVE_SLOT_A] == 0xA1 && autosave[AUTOSAVE_SLOT_B] == 0xB2);
  /* Verify Actor Blackboard primitives restored */
  assert(bb_s32[0] == 42 && bb_s32[1] == -7);
  assert(bb_f32[0] == 3.5f && bb_f32[1] == 1.25f);
  assert(bb_bool[0] == 1 && bb_bool[1] == 0 && bb_bool[2] == 1);
  assert(bb_vec3[0] == 9.0f && bb_vec3[1] == 8.0f && bb_vec3[2] == 7.0f);
  assert(bb_u32[0] == 0xCAFEBABEu);

  sd_clear();
  for (int i = 0; i < MEMFILE_SLOTS; i++)
    free_memfile(&memfiles[i]);
  puts("PASS: phase 2: Link velocity, fall/JS height, AutoSaveMgr SavePos, actor Blackboard + .szm round-trip");
}

static int timer_resync_calls, minigame_resync_calls;
static void fake_timer_gd_changed(uintptr_t mgr, const void *bits)
{
  (void)mgr;
  const uint8_t *b = (const uint8_t *)bits;
  for (int i = 0; i < 0x20; i++)
    assert(b[i] == 0);
  timer_resync_calls++;
}
static void fake_minigame_gd_changed(uintptr_t mgr, const void *bits)
{
  (void)mgr;
  const uint8_t *b = (const uint8_t *)bits;
  for (int i = 0; i < 0x20; i++)
    assert(b[i] == 0);
  minigame_resync_calls++;
}

static void test_phase3_rng_mgrs_cutscene(void)
{
  init_env(0x5B8);
  test_build.version = "1.1.1";
  memfile_fast_reload = true;
  sim_ev_skip_accept = true;
  ev_skip_calls = timer_resync_calls = minigame_resync_calls = 0;

  /* GlobalRandom singleton */
  static uint32_t sim_rng[4] = { 0x11112222u, 0x33334444u, 0x55556666u, 0x77778888u };
  static uintptr_t rng_ptr, rng_holder;
  rng_ptr = (uintptr_t)sim_rng;
  rng_holder = (uintptr_t)&rng_ptr;
  add_region(sim_rng, sizeof(sim_rng), SYS_PERM_R | SYS_PERM_W);
  add_region(&rng_ptr, sizeof(rng_ptr), SYS_PERM_R | SYS_PERM_W);
  add_region(&rng_holder, sizeof(rng_holder), SYS_PERM_R | SYS_PERM_W);
  test_build.global_random_ptr = (uint32_t)((uintptr_t)&rng_holder - z64hd_main);

  /* TimerMgr (#19) and MinigameMgr (#18) in scene components */
  static _Alignas(8) uint8_t smgr[0x200], scene[0x80], scomps[24 * 8], tmgr[0xB0], mmgr[0x80];
  static uintptr_t vt_tmgr[8], vt_mmgr[8], smgr_slot;
  memset(smgr, 0, sizeof(smgr)); memset(scene, 0, sizeof(scene)); memset(scomps, 0, sizeof(scomps));
  memset(tmgr, 0, sizeof(tmgr)); memset(mmgr, 0, sizeof(mmgr));
  memset(vt_tmgr, 0, sizeof(vt_tmgr)); memset(vt_mmgr, 0, sizeof(vt_mmgr));
  vt_tmgr[6] = (uintptr_t)fake_timer_gd_changed;
  vt_mmgr[6] = (uintptr_t)fake_minigame_gd_changed;
  uintptr_t p_vtt = (uintptr_t)vt_tmgr, p_vtm = (uintptr_t)vt_mmgr;
  memcpy(tmgr, &p_vtt, 8);
  memcpy(mmgr, &p_vtm, 8);
  uintptr_t p_sc = (uintptr_t)scene, p_scs = (uintptr_t)scomps, p_tm = (uintptr_t)tmgr, p_mm = (uintptr_t)mmgr;
  uint32_t sc_cnt = 24;
  memcpy(smgr + SCENE_CURRENT, &p_sc, 8);
  memcpy(scene + SCENE_COUNT, &sc_cnt, 4);
  memcpy(scene + SCENE_COMPONENTS, &p_scs, 8);
  memcpy(scomps + SCENE_TIMER * 8, &p_tm, 8);
  memcpy(scomps + SCENE_MINIGAME * 8, &p_mm, 8);
  smgr_slot = (uintptr_t)smgr;
  add_region(smgr, sizeof(smgr), SYS_PERM_R | SYS_PERM_W);
  add_region(scene, sizeof(scene), SYS_PERM_R | SYS_PERM_W);
  add_region(scomps, sizeof(scomps), SYS_PERM_R | SYS_PERM_W);
  add_region(tmgr, sizeof(tmgr), SYS_PERM_R | SYS_PERM_W);
  add_region(mmgr, sizeof(mmgr), SYS_PERM_R | SYS_PERM_W);
  add_region(vt_tmgr, sizeof(vt_tmgr), SYS_PERM_R);
  add_region(vt_mmgr, sizeof(vt_mmgr), SYS_PERM_R);
  add_region(&smgr_slot, sizeof(smgr_slot), SYS_PERM_R | SYS_PERM_W);
  test_build.scene_mgr_ptr = (uint32_t)((uintptr_t)&smgr_slot - z64hd_main);
  test_build.vt_timer_mgr = (uint32_t)((uintptr_t)vt_tmgr - z64hd_main);
  test_build.vt_minigame_mgr = (uint32_t)((uintptr_t)vt_mmgr - z64hd_main);

  /* Save slot 0 and export/import into slot 1 */
  assert(memfile_save(0));
  complete_mock_save(GD_SAVE_MAGIC, 256, "phase3");
  memfile_frame();
  assert(phase == L_IDLE && memfiles[0].have_rng);
  sd_clear();
  assert(memfile_export(0, "phase3_test") && memfile_list_files() == 1 && memfile_import(0, 1));
  assert(memfiles[1].have_rng && memfiles[1].rng[0] == 0x11112222u && memfiles[1].rng[3] == 0x77778888u);

  /* Scramble RNG and start a cutscene, then load slot 1! */
  sim_rng[0] = sim_rng[1] = sim_rng[2] = sim_rng[3] = 1u;
  sim_ev_known = true;
  sim_ev_running = true;
  assert(memfile_load(1));
  assert(phase == L_CANCEL_EVENT && ev_skip_calls == 1);
  memfile_frame();
  assert(phase == L_CANCEL_EVENT); /* still waiting while cutscene runs */
  sim_ev_running = false;          /* cutscene ends */
  memfile_frame();
  assert(phase == L_FAST_WAIT_GD);
  complete_mock_load(true);
  for (int i = 0; i < 60 && phase != L_IDLE; i++) {
    memfile_frame();
    if (phase == L_FAST_WAIT_RESET)
      gd_set_bool(F_REQ_RESET_GIMMICK, false);
  }
  assert(phase == L_IDLE);
  assert(sim_rng[0] == 0x11112222u && sim_rng[1] == 0x33334444u &&
         sim_rng[2] == 0x55556666u && sim_rng[3] == 0x77778888u);
  assert(timer_resync_calls == 1 && minigame_resync_calls == 1);

  sd_clear();
  for (int i = 0; i < MEMFILE_SLOTS; i++)
    free_memfile(&memfiles[i]);
  puts("PASS: phase 3: GlobalRandom RNG, TimerMgr/MinigameMgr resync, load-during-cutscene auto-cancel");
}

/* --- Test 14: BSS pool, in-place save_buf shrinking, fallback promotion, coalescing & errors --- */

static void test_bss_pool_and_error_reporting(void)
{
  init_env(0x5B8);
  test_build.version = "1.0.0";
  sd_clear();
  const size_t full_free = MEMFILE_POOL_SIZE - sizeof(pool_hdr_t);
  assert(pool_free_bytes() == full_free);

  /* Large GD_DESC_BUF_SIZE (2 MiB) allocates in memfile_pool and shrinks IN PLACE to written (256 B) */
  uint32_t big_cap = 2u << 20;
  memcpy(gd_desc0_mem + GD_DESC_BUF_SIZE, &big_cap, 4);
  assert(memfile_save(0) && phase == L_SAVING_WAIT);
  uint8_t *staged_ptr = staging.save_buf;
  assert(in_pool(staged_ptr) && staging.save_cap == big_cap);
  complete_mock_save(GD_SAVE_MAGIC, 256, "SHRUNK_V1");
  memfile_frame();
  assert(phase == L_IDLE && memfiles[0].used);
  assert(memfiles[0].save_buf == staged_ptr); /* shrunk in place, no copy */
  assert(memfiles[0].save_size == 256 && memfiles[0].save_cap == 256);
  assert(strcmp((const char *)(memfiles[0].save_buf + 8), "SHRUNK_V1") == 0);
  /* Shrunk storage should use only a few KiB, not 2 MiB */
  assert(full_free - pool_free_bytes() < 4096u);

  /* Non-OOM capture_session failure reports game data not readable, NOT out of memory */
  uint32_t saved_bool_cnt = type_bool.count;
  type_bool.count = 0;
  assert(!memfile_save(1));
  assert(strcmp(last_notice, "can't save a memfile: game data not readable") == 0);
  type_bool.count = saved_bool_cnt;

  /* Tight-pool fallback + promotion: pin almost the whole 4 MiB pool so a 2 MiB staging buffer
   * falls back to main's heap, then finish_save promotes the shrunk 1 MiB snapshot into the BSS pool
   * by reclaiming the overwritten slot 0 */
  free_memfile(&memfiles[0]);
  assert(pool_free_bytes() == full_free);
  assert(memfile_save(0));
  complete_mock_save(GD_SAVE_MAGIC, 1u << 20, "BIG_V1");
  memfile_frame();
  assert(phase == L_IDLE && in_pool(memfiles[0].save_buf) && memfiles[0].save_size == (1u << 20));
  void *pin = pool_alloc(MEMFILE_POOL_SIZE - (1u << 20) - (512u << 10));
  assert(pin != 0 && in_pool(pin));
  assert(pool_free_bytes() < (1u << 20));
  assert(memfile_save(0) && phase == L_SAVING_WAIT);
  assert(!in_pool(staging.save_buf)); /* 2 MiB staging fell back cleanly to CRT heap */
  complete_mock_save(GD_SAVE_MAGIC, 1u << 20, "BIG_V2");
  memfile_frame();
  assert(phase == L_IDLE && memfiles[0].used && memfiles[0].save_size == (1u << 20));
  assert(in_pool(memfiles[0].save_buf)); /* promoted back into BSS pool after reclaiming old slot 0 */
  assert(strcmp((const char *)(memfiles[0].save_buf + 8), "BIG_V2") == 0);

  /* Freeing everything coalesces completely back to a single free block */
  free16(pin);
  for (int i = 0; i < MEMFILE_SLOTS; i++)
    free_memfile(&memfiles[i]);
  assert(pool_free_bytes() == full_free);
  puts("PASS: BSS pool, in-place save_buf shrinking, fallback promotion, coalescing, and non-OOM errors");
}

/* --- Test 15: Held actor (Banc Cucco + spawned pot) & Actor Blackboard WarpPos restore --- */

static void test_held_actor_and_cucco_state(void)
{
  init_env(0x5B8);
  test_build.version = "1.1.1";
  test_build.actor_link_set = OFF_ACTOR_LINK_SET;
  test_build.event_req_sub_dtor = OFF_ACTOR_LINK_DTOR;
  test_build.lifter_drop = OFF_LIFTER_DROP;
  test_build.lifter_force_lift = OFF_LIFTER_FORCE_LIFT;
  test_build.player_lift_anim = OFF_PLAYER_LIFT_ANIM;
  test_build.bsa_set_action_verb = OFF_BSA_SET_VERB;
  memfile_fast_reload = true;
  memfile_actors = true;
  link_set_calls = link_dtor_calls = lifter_drop_calls = lifter_force_calls = lift_anim_calls = bsa_verb_calls = 0;
  sim_spawn_calls = sim_spawn_cancel_calls = 0;
  sim_spawn_result_actor = 0;

  /* Link actor with PlayerComp (+0x358), PlayerLifterComponent (+0x2C8), BSAComponent (+0x188) */
  static _Alignas(8) uint8_t link_act[0x400], link_comps[0x400], link_player[0x1A00], link_base[0x1A00];
  static _Alignas(8) uint8_t link_lifter[0x300], link_bsa[0x240], link_handle_ent[0x60];
  memset(link_act, 0, sizeof(link_act));
  memset(link_comps, 0, sizeof(link_comps));
  memset(link_player, 0, sizeof(link_player));
  memset(link_base, 0, sizeof(link_base));
  memset(link_lifter, 0, sizeof(link_lifter));
  memset(link_bsa, 0, sizeof(link_bsa));
  memset(link_handle_ent, 0, sizeof(link_handle_ent));

  /* Banc Cucco actor with Actor Blackboard [[comps+0]+0x10] (NO AIComponent at comps+0x48!) + LiftableComponent */
  static _Alignas(8) uint8_t amgr[0x80], ent_cucco[AENTRY_SIZE], pre_cucco[0x100];
  static _Alignas(8) uint8_t cucco[0x540], cucco_comps[0x400], cucco_bb_comp[0x20], cucco_bb[BB_STRUCT_LEN];
  static _Alignas(8) uint8_t cucco_liftable[0x300];
  static const char cucco_name[] = "Animal_Cucco";
  static const char cucco_pack[] = "Animal_Cucco";
  static const char *vec_names[2] = { "LandingPos", "WarpPos" };
  static const char *s32_names[1] = { "HitCount" };
  static const char *bool_names[1] = { "IsSafeLandedFromDrown" };
  static float bb_vecs[6] = { 10.0f, 20.0f, 30.0f, 111.0f, 222.0f, 333.0f };
  static int32_t bb_s32[1] = { 2 };
  static uint8_t bb_bools[1] = { 1 };
  memset(amgr, 0, sizeof(amgr));
  memset(ent_cucco, 0, sizeof(ent_cucco));
  memset(pre_cucco, 0, sizeof(pre_cucco));
  memset(cucco, 0, sizeof(cucco));
  memset(cucco_comps, 0, sizeof(cucco_comps));
  memset(cucco_bb_comp, 0, sizeof(cucco_bb_comp));
  memset(cucco_bb, 0, sizeof(cucco_bb));
  memset(cucco_liftable, 0, sizeof(cucco_liftable));

  /* Wire Actor Blackboard tables */
  uint32_t cnt1 = 1, cnt2 = 2;
  uintptr_t p;
  p = (uintptr_t)s32_names; memcpy(cucco_bb + 0x098, &cnt1, 4); memcpy(cucco_bb + 0x0A0, &p, 8);
  p = (uintptr_t)bb_s32;    memcpy(cucco_bb + 0x0A8, &cnt1, 4); memcpy(cucco_bb + 0x0B0, &p, 8);
  p = (uintptr_t)bool_names; memcpy(cucco_bb + 0x188, &cnt1, 4); memcpy(cucco_bb + 0x190, &p, 8);
  p = (uintptr_t)bb_bools;   memcpy(cucco_bb + 0x198, &cnt1, 4); memcpy(cucco_bb + 0x1A0, &p, 8);
  p = (uintptr_t)vec_names;  memcpy(cucco_bb + 0x200, &cnt2, 4); memcpy(cucco_bb + 0x208, &p, 8);
  p = (uintptr_t)bb_vecs;    memcpy(cucco_bb + 0x210, &cnt2, 4); memcpy(cucco_bb + 0x218, &p, 8);
  p = (uintptr_t)cucco_bb;   memcpy(cucco_bb_comp + 0x10, &p, 8);
  p = (uintptr_t)cucco_bb_comp; memcpy(cucco_comps + 0x00, &p, 8);
  p = (uintptr_t)cucco_liftable; memcpy(cucco_comps + ACOMP_LIFTABLE, &p, 8);

  uint64_t cucco_id = 0x55AA123456789001ull;
  memcpy(pre_cucco + PREACTOR_INSTANCE_ID, &cucco_id, 8);
  p = (uintptr_t)cucco_name; memcpy(cucco + A_NAME, &p, 8);
  p = (uintptr_t)cucco_pack; memcpy(cucco + 0x260, &p, 8);
  uint32_t st4 = 4; memcpy(cucco + A_STATE, &st4, 4);
  p = (uintptr_t)cucco_comps; memcpy(cucco + A_COMPONENTS, &p, 8);
  p = (uintptr_t)pre_cucco;   memcpy(cucco + A_PREACTOR, &p, 8);
  float cpos[3] = { 10.0f, 3.5f, 30.0f }, chome[3] = { 4.0f, 5.0f, 6.0f };
  float crot[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
  memcpy(cucco + A_POS, cpos, 12);
  memcpy(cucco + A_ROT, crot, 36);
  memcpy(cucco + A_HOME_POS, chome, 12);

  /* Put Cucco into ActorMgr tree */
  p = (uintptr_t)cucco; memcpy(ent_cucco + AENTRY_ACTOR, &p, 8);
  p = (uintptr_t)(ent_cucco + AENTRY_NODE); memcpy(amgr + AMGR_TREE_ROOT, &p, 8);
  static uintptr_t amgr_slot;
  amgr_slot = (uintptr_t)amgr;

  /* Wire Link holding Cucco via PlayerLifterComponent */
  p = (uintptr_t)link_comps;  memcpy(link_act + A_COMPONENTS, &p, 8);
  p = (uintptr_t)link_base;   memcpy(link_comps + 0x2B8, &p, 8);
  p = (uintptr_t)link_player; memcpy(link_comps + PLAYER_COMPONENT, &p, 8);
  p = (uintptr_t)link_act;    memcpy(link_player + 0x18, &p, 8);
  p = (uintptr_t)link_act;    memcpy(link_base + 0x1340, &p, 8);
  p = (uintptr_t)link_lifter; memcpy(link_comps + ACOMP_LIFTER, &p, 8);
  p = (uintptr_t)link_bsa;    memcpy(link_comps + ACOMP_BSA, &p, 8);
  uint32_t gen = 7, lstate = 3;
  p = (uintptr_t)cucco; memcpy(link_handle_ent + 0x40, &p, 8);
  memcpy(link_handle_ent + 0x48, &gen, 4);
  p = (uintptr_t)link_handle_ent; memcpy(link_lifter + LIFTER_TARGET_LINK + 8, &p, 8);
  memcpy(link_lifter + LIFTER_TARGET_LINK + 0x10, &gen, 4);
  memcpy(link_lifter + LIFTER_STATE, &lstate, 4);

  sim_link_actor = (uintptr_t)link_act;
  add_region(link_act, sizeof(link_act), SYS_PERM_R | SYS_PERM_W);
  add_region(link_comps, sizeof(link_comps), SYS_PERM_R | SYS_PERM_W);
  add_region(link_player, sizeof(link_player), SYS_PERM_R | SYS_PERM_W);
  add_region(link_base, sizeof(link_base), SYS_PERM_R | SYS_PERM_W);
  add_region(link_lifter, sizeof(link_lifter), SYS_PERM_R | SYS_PERM_W);
  add_region(link_bsa, sizeof(link_bsa), SYS_PERM_R | SYS_PERM_W);
  add_region(link_handle_ent, sizeof(link_handle_ent), SYS_PERM_R | SYS_PERM_W);
  add_region(amgr, sizeof(amgr), SYS_PERM_R | SYS_PERM_W);
  add_region(&amgr_slot, sizeof(amgr_slot), SYS_PERM_R | SYS_PERM_W);
  add_region(ent_cucco, sizeof(ent_cucco), SYS_PERM_R | SYS_PERM_W);
  add_region(pre_cucco, sizeof(pre_cucco), SYS_PERM_R | SYS_PERM_W);
  add_region(cucco, sizeof(cucco), SYS_PERM_R | SYS_PERM_W);
  add_region(cucco_comps, sizeof(cucco_comps), SYS_PERM_R | SYS_PERM_W);
  add_region(cucco_bb_comp, sizeof(cucco_bb_comp), SYS_PERM_R | SYS_PERM_W);
  add_region(cucco_bb, sizeof(cucco_bb), SYS_PERM_R | SYS_PERM_W);
  add_region(cucco_liftable, sizeof(cucco_liftable), SYS_PERM_R | SYS_PERM_W);
  add_region(cucco_name, sizeof(cucco_name) + 16, SYS_PERM_R);
  add_region(cucco_pack, sizeof(cucco_pack) + 16, SYS_PERM_R);
  add_region(vec_names, sizeof(vec_names), SYS_PERM_R);
  add_region(s32_names, sizeof(s32_names), SYS_PERM_R);
  add_region(bool_names, sizeof(bool_names), SYS_PERM_R);
  add_region(bb_vecs, sizeof(bb_vecs), SYS_PERM_R | SYS_PERM_W);
  add_region(bb_s32, sizeof(bb_s32), SYS_PERM_R | SYS_PERM_W);
  add_region(bb_bools, sizeof(bb_bools), SYS_PERM_R | SYS_PERM_W);
  test_build.actor_mgr_ptr = (uint32_t)((uintptr_t)&amgr_slot - z64hd_main);

  /* Save slot 0 and export/import into slot 1 */
  assert(memfile_save(0));
  complete_mock_save(GD_SAVE_MAGIC, 256, "held_cucco");
  memfile_frame();
  assert(phase == L_IDLE && memfiles[0].held.valid);
  assert(memfiles[0].held.instance_id == cucco_id);
  assert(strcmp(memfiles[0].held.pack_name, "Animal_Cucco") == 0);
  assert(memfiles[0].held.actor_bb != 0 && memfiles[0].held.actor_bb_size > 0);
  assert(memfiles[0].held.flags & HELD_F_CUCCO);

  sd_clear();
  assert(memfile_export(0, "held_cucco_szm") && memfile_list_files() == 1 && memfile_import(0, 1));
  assert(memfiles[1].held.valid && memfiles[1].held.instance_id == cucco_id);
  assert(strcmp(memfiles[1].held.pack_name, "Animal_Cucco") == 0);
  assert(memfiles[1].held.actor_bb_size == memfiles[0].held.actor_bb_size);

  /* Corrupt WarpPos, HitCount, home_pos, and keep Link holding something before fast reload */
  bb_vecs[3] = bb_vecs[4] = bb_vecs[5] = 0.0f;
  bb_s32[0] = 0;
  bb_bools[0] = 0;
  float z3[3] = { 0, 0, 0 };
  memcpy(cucco + A_HOME_POS, z3, 12);
  *(uint32_t *)(link_player + PLAYER_LIFT_FLAGS) = 0;

  assert(memfile_load(1));
  complete_mock_load(true);
  bool clobbered = false;
  for (int i = 0; i < 60 && phase != L_IDLE; i++) {
    memfile_frame();
    if (phase == L_FAST_WAIT_RESET)
      gd_set_bool(F_REQ_RESET_GIMMICK, false);
    /* Simulate InterruptStruggle clobbering WarpPos on frame 1 post-bind -> held_reapply restores it */
    if (!clobbered && lifter_force_calls == 1) {
      bb_vecs[3] = -99.0f;
      clobbered = true;
    }
  }
  assert(phase == L_IDLE && clobbered);

  assert(lifter_drop_calls == 1);
  assert(link_set_calls == 1 && last_link_set_actor == (uintptr_t)cucco);
  assert(lifter_force_calls == 1 && last_lift_target == (uintptr_t)cucco);
  assert(lift_anim_calls == 1 && last_lift_anim_actor == (uintptr_t)cucco);
  assert(link_dtor_calls == 1);
  assert((*(uint32_t *)(link_player + PLAYER_LIFT_FLAGS) & 2u) != 0);
  assert(bsa_verb_calls >= 1 && last_bsa_target == (uintptr_t)link_bsa &&
         last_bsa_verb == ACTION_VERB_LIFT && last_bsa_slot == 1u);
  assert(bb_vecs[3] == 111.0f && bb_vecs[4] == 222.0f && bb_vecs[5] == 333.0f);
  assert(bb_s32[0] == 2 && bb_bools[0] == 1);
  assert(memcmp(cucco + A_HOME_POS, chome, 12) == 0);

  /* Now test dynamic / non-Banc held pot fallback -> spawns via actor_spawn_handle and binds */
  static _Alignas(8) uint8_t pot[0x400], pot_comps[0x300], pot_liftable[0x300];
  static const char pot_name[] = "Obj_Pot_A_01";
  memset(pot, 0, sizeof(pot)); memset(pot_comps, 0, sizeof(pot_comps)); memset(pot_liftable, 0, sizeof(pot_liftable));
  p = (uintptr_t)pot_name; memcpy(pot + A_NAME, &p, 8); memcpy(pot + 0x260, &p, 8);
  memcpy(pot + A_STATE, &st4, 4);
  p = (uintptr_t)pot_liftable; memcpy(pot_comps + ACOMP_LIFTABLE, &p, 8);
  p = (uintptr_t)pot_comps;    memcpy(pot + A_COMPONENTS, &p, 8);
  memcpy(pot + A_ROT, crot, 36);
  add_region(pot, sizeof(pot), SYS_PERM_R | SYS_PERM_W);
  add_region(pot_comps, sizeof(pot_comps), SYS_PERM_R | SYS_PERM_W);
  add_region(pot_liftable, sizeof(pot_liftable), SYS_PERM_R | SYS_PERM_W);
  add_region(pot_name, sizeof(pot_name) + 16, SYS_PERM_R);

  /* Link holds non-Banc pot (no PreActor) */
  p = (uintptr_t)pot; memcpy(link_handle_ent + 0x40, &p, 8);
  memcpy(link_lifter + LIFTER_STATE, &lstate, 4);
  assert(memfile_save(2));
  complete_mock_save(GD_SAVE_MAGIC, 256, "held_pot");
  memfile_frame();
  assert(phase == L_IDLE && memfiles[2].held.valid && memfiles[2].held.instance_id == 0);
  assert(strcmp(memfiles[2].held.pack_name, "Obj_Pot_A_01") == 0);

  /* Clear tree and spawn the pot asynchronously during load */
  sim_spawn_result_actor = (uintptr_t)pot;
  sim_spawn_wait_frames = 2;
  lifter_force_calls = 0;
  assert(memfile_load(2));
  complete_mock_load(true);
  for (int i = 0; i < 60 && phase != L_IDLE; i++) {
    memfile_frame();
    if (phase == L_FAST_WAIT_RESET)
      gd_set_bool(F_REQ_RESET_GIMMICK, false);
  }
  assert(phase == L_IDLE);
  assert(sim_spawn_calls == 1 && strcmp(sim_spawn_last_name, "Obj_Pot_A_01") == 0);
  assert(lifter_force_calls == 1 && last_lift_target == (uintptr_t)pot);

  sd_clear();
  clear_post_load_jobs();
  for (int i = 0; i < MEMFILE_SLOTS; i++)
    free_memfile(&memfiles[i]);
  puts("PASS: held actor capture/restore (Banc Cucco + WarpPos + non-Banc pot spawn fallback + .szm)");
}

/* --- Test 16: Cucco drown timer, fade timer, respawn timer, and respawn position --- */

static void test_cucco_drown_fade_respawn_timers(void)
{
  init_env(0x5B8);
  test_build.version = "1.1.1";
  test_build.bsa_set_action_verb = OFF_BSA_SET_VERB;
  test_build.frame_counter_node_vtable = 0x100u; /* within z64hd_main */
  memfile_fast_reload = true;
  memfile_actors = true;
  bsa_verb_calls = 0;

  /* Fake AINB file for Kokko.action.interruptdrown.module */
  static _Alignas(8) uint8_t aib_file[0x100];
  memset(aib_file, 0, sizeof(aib_file));
  memcpy(aib_file, "AIB ", 4);
  uint32_t pool_off = 0x80u;
  memcpy(aib_file + 0x24, &pool_off, 4);
  strcpy((char *)(aib_file + 0x80), "Kokko.action.interruptdrown.module");

  /* Two Cuccos:
   *  C1: sinking/fading out (CUCCO_NODE_SINK=14, elapsed 22.5/60, alpha 0.35, WarpPos=(123,45,-67))
   *  C2: mid-jump (CUCCO_NODE_JUMP=77, elapsed 48.0/600, JUMP_AIR=119 elapsed 4.5/10, LandingWaterCount=2) */
  static _Alignas(8) uint8_t amgr[0x80], ent1[AENTRY_SIZE], ent2[AENTRY_SIZE];
  static _Alignas(8) uint8_t pre1[0x100], pre2[0x100];
  static _Alignas(8) uint8_t c1[0x540], c1_comps[0x400], c1_bb_c[0x20], c1_bb[BB_STRUCT_LEN];
  static _Alignas(8) uint8_t c1_bsa[0x200], c1_hold[0xA0], c1_ctrl[0x220], c1_ctrl_bb[BB_STRUCT_LEN];
  static _Alignas(8) uint8_t c1_def[0x20], c1_unit[0xA0], c1_model[0x150], c1_vis[0xC0];
  static _Alignas(8) uint8_t c1_nodes_mem[6][0x60];
  static uintptr_t c1_defs[1], c1_units[3], c1_node_ptrs[140];
  static uint32_t  c1_bmp[5];

  static _Alignas(8) uint8_t c2[0x540], c2_comps[0x400], c2_bsa[0x200], c2_hold[0xA0], c2_ctrl[0x220], c2_ctrl_bb[BB_STRUCT_LEN];
  static _Alignas(8) uint8_t c2_unit[0xA0], c2_nodes_mem[6][0x60];
  static uintptr_t c2_units[3], c2_node_ptrs[140];
  static uint32_t  c2_bmp[5];

  static const char cname[] = "Animal_Cucco";
  static const char s_warp[] = "WarpPos";
  static const char s_lwc[] = "LandingWaterCount";
  static const char s_fw[] = "IsForceWarp";
  static const char *warp_names[1] = { s_warp };
  static const char *ctrl_s32_names[1] = { s_lwc };
  static const char *ctrl_bool_names[1] = { s_fw };
  static float c1_warp[3] = { 123.0f, 45.0f, -67.0f };
  static int32_t c1_lwc[1] = { 3 }, c2_lwc[1] = { 2 };
  static uint8_t c1_fw[1] = { 0 }, c2_fw[1] = { 0 };

  memset(amgr, 0, sizeof(amgr)); memset(ent1, 0, sizeof(ent1)); memset(ent2, 0, sizeof(ent2));
  memset(pre1, 0, sizeof(pre1)); memset(pre2, 0, sizeof(pre2));
  memset(c1, 0, sizeof(c1)); memset(c1_comps, 0, sizeof(c1_comps)); memset(c1_bb_c, 0, sizeof(c1_bb_c));
  memset(c1_bb, 0, sizeof(c1_bb)); memset(c1_bsa, 0, sizeof(c1_bsa)); memset(c1_hold, 0, sizeof(c1_hold));
  memset(c1_ctrl, 0, sizeof(c1_ctrl)); memset(c1_ctrl_bb, 0, sizeof(c1_ctrl_bb));
  memset(c1_def, 0, sizeof(c1_def)); memset(c1_unit, 0, sizeof(c1_unit));
  memset(c1_model, 0, sizeof(c1_model)); memset(c1_vis, 0, sizeof(c1_vis));
  memset(c1_nodes_mem, 0, sizeof(c1_nodes_mem)); memset(c1_node_ptrs, 0, sizeof(c1_node_ptrs)); memset(c1_bmp, 0, sizeof(c1_bmp));
  memset(c2, 0, sizeof(c2)); memset(c2_comps, 0, sizeof(c2_comps)); memset(c2_bsa, 0, sizeof(c2_bsa));
  memset(c2_hold, 0, sizeof(c2_hold)); memset(c2_ctrl, 0, sizeof(c2_ctrl)); memset(c2_ctrl_bb, 0, sizeof(c2_ctrl_bb));
  memset(c2_unit, 0, sizeof(c2_unit)); memset(c2_nodes_mem, 0, sizeof(c2_nodes_mem));
  memset(c2_node_ptrs, 0, sizeof(c2_node_ptrs)); memset(c2_bmp, 0, sizeof(c2_bmp));

  uintptr_t p;
  uint32_t cnt1 = 1, st4 = 4, n140 = 140, verb_drown = ACTION_VERB_DROWN;
  uint64_t vt_fc = MAIN(test_build.frame_counter_node_vtable);
  static const uint16_t idxs[6] = { CUCCO_NODE_IN_WATER, CUCCO_NODE_LOOK, CUCCO_NODE_JUMP,
                                    CUCCO_NODE_JUMP_AIR, CUCCO_NODE_SINK, CUCCO_NODE_FADE_IN };

  /* Shared def */
  p = (uintptr_t)aib_file; memcpy(c1_def + 0x10, &p, 8);
  c1_defs[0] = (uintptr_t)c1_def;

  /* Wire C1 */
  uint64_t id1 = 0xC0CC000000000001ull, id2 = 0xC0CC000000000002ull;
  memcpy(pre1 + PREACTOR_INSTANCE_ID, &id1, 8);
  memcpy(pre2 + PREACTOR_INSTANCE_ID, &id2, 8);
  p = (uintptr_t)warp_names; memcpy(c1_bb + 0x200, &cnt1, 4); memcpy(c1_bb + 0x208, &p, 8);
  p = (uintptr_t)c1_warp;    memcpy(c1_bb + 0x210, &cnt1, 4); memcpy(c1_bb + 0x218, &p, 8);
  p = (uintptr_t)c1_bb;      memcpy(c1_bb_c + 0x10, &p, 8);
  p = (uintptr_t)c1_bb_c;    memcpy(c1_comps + 0x00, &p, 8);
  p = (uintptr_t)c1_vis;     memcpy(c1_comps + ACOMP_VIS, &p, 8);
  p = (uintptr_t)c1_bsa;     memcpy(c1_comps + ACOMP_BSA, &p, 8);
  p = (uintptr_t)c1_model;   memcpy(c1_comps + ACOMP_MODEL, &p, 8);
  p = (uintptr_t)cname;      memcpy(c1 + A_NAME, &p, 8);
  memcpy(c1 + A_STATE, &st4, 4);
  p = (uintptr_t)c1_comps;   memcpy(c1 + A_COMPONENTS, &p, 8);
  p = (uintptr_t)pre1;       memcpy(c1 + A_PREACTOR, &p, 8);
  float c1_home[3] = { 1.0f, 2.0f, 3.0f }, rot_id[9] = { 1,0,0, 0,1,0, 0,0,1 };
  memcpy(c1 + A_HOME_POS, c1_home, 12);
  memcpy(c1 + A_ROT, rot_id, 36);

  /* C1 model fade state: mid-fade out */
  float alpha = 0.35f, step = 1.0f / 6.0f;
  uint32_t fflags = 0xC8u;
  memcpy(c1_model + MODEL_ALPHA, &alpha, 4);
  memcpy(c1_model + MODEL_FADE_STEP, &step, 4);
  memcpy(c1_model + MODEL_FADE_FLAGS, &fflags, 4);
  c1_model[MODEL_FADE_ACTIVE] = 1;
  c1_vis[VIS_FADE_FLAGS] = 0x04;

  /* C1 BSA controller + unit + nodes */
  memcpy(c1_bsa + BSA_CUR_VERB, &verb_drown, 4);
  p = (uintptr_t)c1_hold; memcpy(c1_bsa + BSA_ACTION_HOLDER, &p, 8);
  p = (uintptr_t)c1_ctrl; memcpy(c1_hold + 0x80, &p, 8);
  p = (uintptr_t)c1;      memcpy(c1_ctrl + 0x1F0, &p, 8);
  p = (uintptr_t)c1_ctrl_bb; memcpy(c1_ctrl + 0x1F8, &p, 8);
  memcpy(c1_ctrl + 0x168, &cnt1, 4); p = (uintptr_t)c1_defs; memcpy(c1_ctrl + 0x170, &p, 8);
  c1_units[0] = (uintptr_t)c1_unit;
  memcpy(c1_ctrl + 0x18, &cnt1, 4); p = (uintptr_t)c1_units; memcpy(c1_ctrl + 0x20, &p, 8);
  p = (uintptr_t)c1_def; memcpy(c1_unit + 0x10, &p, 8);
  memcpy(c1_unit + 0x60, &n140, 4); p = (uintptr_t)c1_node_ptrs; memcpy(c1_unit + 0x68, &p, 8);
  memcpy(c1_unit + 0x80, &n140, 4); p = (uintptr_t)c1_bmp; memcpy(c1_unit + 0x88, &p, 8);
  p = (uintptr_t)ctrl_s32_names;  memcpy(c1_ctrl_bb + 0x098, &cnt1, 4); memcpy(c1_ctrl_bb + 0x0A0, &p, 8);
  p = (uintptr_t)c1_lwc;          memcpy(c1_ctrl_bb + 0x0A8, &cnt1, 4); memcpy(c1_ctrl_bb + 0x0B0, &p, 8);
  p = (uintptr_t)ctrl_bool_names; memcpy(c1_ctrl_bb + 0x188, &cnt1, 4); memcpy(c1_ctrl_bb + 0x190, &p, 8);
  p = (uintptr_t)c1_fw;           memcpy(c1_ctrl_bb + 0x198, &cnt1, 4); memcpy(c1_ctrl_bb + 0x1A0, &p, 8);
  for (int i = 0; i < 6; i++) {
    uint8_t *nd = c1_nodes_mem[i];
    memcpy(nd, &vt_fc, 8);
    p = (uintptr_t)c1_unit; memcpy(nd + 8, &p, 8);
    memcpy(nd + 0x1C, &idxs[i], 2);
    nd[0x58] = 0;
    c1_node_ptrs[idxs[i]] = (uintptr_t)nd;
  }
  /* Mark C1 node 14 (SINK) counting at 22.5 */
  uint32_t nf_on = 2u; float sink_v = 22.5f;
  memcpy(c1_nodes_mem[4] + 0x18, &nf_on, 4);
  memcpy(c1_nodes_mem[4] + 0x20, &sink_v, 4);
  c1_bmp[CUCCO_NODE_SINK >> 5] |= 1u << (CUCCO_NODE_SINK & 31);

  /* Wire C2 (jumping, node 77 = 48.0, node 119 = 4.5, LandingWaterCount = 2) */
  p = (uintptr_t)c2_bsa;   memcpy(c2_comps + ACOMP_BSA, &p, 8);
  p = (uintptr_t)cname;    memcpy(c2 + A_NAME, &p, 8);
  memcpy(c2 + A_STATE, &st4, 4);
  p = (uintptr_t)c2_comps; memcpy(c2 + A_COMPONENTS, &p, 8);
  p = (uintptr_t)pre2;     memcpy(c2 + A_PREACTOR, &p, 8);
  memcpy(c2 + A_ROT, rot_id, 36);
  memcpy(c2_bsa + BSA_CUR_VERB, &verb_drown, 4);
  p = (uintptr_t)c2_hold; memcpy(c2_bsa + BSA_ACTION_HOLDER, &p, 8);
  p = (uintptr_t)c2_ctrl; memcpy(c2_hold + 0x80, &p, 8);
  p = (uintptr_t)c2;      memcpy(c2_ctrl + 0x1F0, &p, 8);
  p = (uintptr_t)c2_ctrl_bb; memcpy(c2_ctrl + 0x1F8, &p, 8);
  memcpy(c2_ctrl + 0x168, &cnt1, 4); p = (uintptr_t)c1_defs; memcpy(c2_ctrl + 0x170, &p, 8);
  c2_units[0] = (uintptr_t)c2_unit;
  memcpy(c2_ctrl + 0x18, &cnt1, 4); p = (uintptr_t)c2_units; memcpy(c2_ctrl + 0x20, &p, 8);
  p = (uintptr_t)c1_def; memcpy(c2_unit + 0x10, &p, 8);
  memcpy(c2_unit + 0x60, &n140, 4); p = (uintptr_t)c2_node_ptrs; memcpy(c2_unit + 0x68, &p, 8);
  memcpy(c2_unit + 0x80, &n140, 4); p = (uintptr_t)c2_bmp; memcpy(c2_unit + 0x88, &p, 8);
  p = (uintptr_t)ctrl_s32_names;  memcpy(c2_ctrl_bb + 0x098, &cnt1, 4); memcpy(c2_ctrl_bb + 0x0A0, &p, 8);
  p = (uintptr_t)c2_lwc;          memcpy(c2_ctrl_bb + 0x0A8, &cnt1, 4); memcpy(c2_ctrl_bb + 0x0B0, &p, 8);
  p = (uintptr_t)ctrl_bool_names; memcpy(c2_ctrl_bb + 0x188, &cnt1, 4); memcpy(c2_ctrl_bb + 0x190, &p, 8);
  p = (uintptr_t)c2_fw;           memcpy(c2_ctrl_bb + 0x198, &cnt1, 4); memcpy(c2_ctrl_bb + 0x1A0, &p, 8);
  for (int i = 0; i < 6; i++) {
    uint8_t *nd = c2_nodes_mem[i];
    memcpy(nd, &vt_fc, 8);
    p = (uintptr_t)c2_unit; memcpy(nd + 8, &p, 8);
    memcpy(nd + 0x1C, &idxs[i], 2);
    nd[0x58] = 0;
    c2_node_ptrs[idxs[i]] = (uintptr_t)nd;
  }
  float jmp_v = 48.0f, air_v = 4.5f;
  memcpy(c2_nodes_mem[2] + 0x18, &nf_on, 4); memcpy(c2_nodes_mem[2] + 0x20, &jmp_v, 4);
  memcpy(c2_nodes_mem[3] + 0x18, &nf_on, 4); memcpy(c2_nodes_mem[3] + 0x20, &air_v, 4);
  c2_bmp[CUCCO_NODE_JUMP >> 5] |= 1u << (CUCCO_NODE_JUMP & 31);
  c2_bmp[CUCCO_NODE_JUMP_AIR >> 5] |= 1u << (CUCCO_NODE_JUMP_AIR & 31);

  /* Put both Cuccos in ActorMgr tree */
  p = (uintptr_t)c1; memcpy(ent1 + AENTRY_ACTOR, &p, 8);
  p = (uintptr_t)c2; memcpy(ent2 + AENTRY_ACTOR, &p, 8);
  p = (uintptr_t)(ent2 + AENTRY_NODE); memcpy(ent1 + AENTRY_NODE + ANODE_RIGHT, &p, 8);
  p = (uintptr_t)(ent1 + AENTRY_NODE); memcpy(amgr + AMGR_TREE_ROOT, &p, 8);
  static uintptr_t amgr_slot2;
  amgr_slot2 = (uintptr_t)amgr;

  add_region(aib_file, sizeof(aib_file), SYS_PERM_R);
  add_region(amgr, sizeof(amgr), SYS_PERM_R | SYS_PERM_W);
  add_region(&amgr_slot2, sizeof(amgr_slot2), SYS_PERM_R | SYS_PERM_W);
  add_region(ent1, sizeof(ent1), SYS_PERM_R | SYS_PERM_W);
  add_region(ent2, sizeof(ent2), SYS_PERM_R | SYS_PERM_W);
  add_region(pre1, sizeof(pre1), SYS_PERM_R | SYS_PERM_W);
  add_region(pre2, sizeof(pre2), SYS_PERM_R | SYS_PERM_W);
  add_region(c1, sizeof(c1), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_comps, sizeof(c1_comps), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_bb_c, sizeof(c1_bb_c), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_bb, sizeof(c1_bb), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_bsa, sizeof(c1_bsa), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_hold, sizeof(c1_hold), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_ctrl, sizeof(c1_ctrl), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_ctrl_bb, sizeof(c1_ctrl_bb), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_def, sizeof(c1_def), SYS_PERM_R);
  add_region(c1_defs, sizeof(c1_defs), SYS_PERM_R);
  add_region(c1_units, sizeof(c1_units), SYS_PERM_R);
  add_region(c1_unit, sizeof(c1_unit), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_node_ptrs, sizeof(c1_node_ptrs), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_bmp, sizeof(c1_bmp), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_nodes_mem, sizeof(c1_nodes_mem), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_model, sizeof(c1_model), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_vis, sizeof(c1_vis), SYS_PERM_R | SYS_PERM_W);
  add_region(c2, sizeof(c2), SYS_PERM_R | SYS_PERM_W);
  add_region(c2_comps, sizeof(c2_comps), SYS_PERM_R | SYS_PERM_W);
  add_region(c2_bsa, sizeof(c2_bsa), SYS_PERM_R | SYS_PERM_W);
  add_region(c2_hold, sizeof(c2_hold), SYS_PERM_R | SYS_PERM_W);
  add_region(c2_ctrl, sizeof(c2_ctrl), SYS_PERM_R | SYS_PERM_W);
  add_region(c2_ctrl_bb, sizeof(c2_ctrl_bb), SYS_PERM_R | SYS_PERM_W);
  add_region(c2_units, sizeof(c2_units), SYS_PERM_R);
  add_region(c2_unit, sizeof(c2_unit), SYS_PERM_R | SYS_PERM_W);
  add_region(c2_node_ptrs, sizeof(c2_node_ptrs), SYS_PERM_R | SYS_PERM_W);
  add_region(c2_bmp, sizeof(c2_bmp), SYS_PERM_R | SYS_PERM_W);
  add_region(c2_nodes_mem, sizeof(c2_nodes_mem), SYS_PERM_R | SYS_PERM_W);
  add_region(cname, sizeof(cname) + 16, SYS_PERM_R);
  add_region(s_warp, sizeof(s_warp), SYS_PERM_R);
  add_region(s_lwc, sizeof(s_lwc), SYS_PERM_R);
  add_region(s_fw, sizeof(s_fw), SYS_PERM_R);
  add_region(warp_names, sizeof(warp_names), SYS_PERM_R);
  add_region(ctrl_s32_names, sizeof(ctrl_s32_names), SYS_PERM_R);
  add_region(ctrl_bool_names, sizeof(ctrl_bool_names), SYS_PERM_R);
  add_region(c1_warp, sizeof(c1_warp), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_lwc, sizeof(c1_lwc), SYS_PERM_R | SYS_PERM_W);
  add_region(c2_lwc, sizeof(c2_lwc), SYS_PERM_R | SYS_PERM_W);
  add_region(c1_fw, sizeof(c1_fw), SYS_PERM_R | SYS_PERM_W);
  add_region(c2_fw, sizeof(c2_fw), SYS_PERM_R | SYS_PERM_W);
  test_build.actor_mgr_ptr = (uint32_t)((uintptr_t)&amgr_slot2 - z64hd_main);

  /* Save slot 0 and export/import into slot 1 */
  assert(memfile_save(0));
  complete_mock_save(GD_SAVE_MAGIC, 256, "cucco_drown");
  memfile_frame();
  assert(phase == L_IDLE && memfiles[0].cucco_count == 2);
  sd_clear();
  assert(memfile_export(0, "cucco_drown_szm") && memfile_list_files() == 1 && memfile_import(0, 1));
  assert(memfiles[1].cucco_count == 2);

  /* Reset both Cuccos to fresh non-drowning state before reload */
  uint32_t zero32 = 0; float one = 1.0f, zero_f = 0.0f;
  memcpy(c1_bsa + BSA_CUR_VERB, &zero32, 4);
  memcpy(c2_bsa + BSA_CUR_VERB, &zero32, 4);
  memset(c1_bmp, 0, sizeof(c1_bmp)); memset(c2_bmp, 0, sizeof(c2_bmp));
  for (int i = 0; i < 6; i++) {
    memcpy(c1_nodes_mem[i] + 0x18, &zero32, 4); memcpy(c1_nodes_mem[i] + 0x20, &zero_f, 4);
    memcpy(c2_nodes_mem[i] + 0x18, &zero32, 4); memcpy(c2_nodes_mem[i] + 0x20, &zero_f, 4);
  }
  memcpy(c1_model + MODEL_ALPHA, &one, 4);
  memcpy(c1_model + MODEL_FADE_FLAGS, &zero32, 4);
  c1_model[MODEL_FADE_ACTIVE] = 0;
  c1_vis[VIS_FADE_FLAGS] = 0;
  c1_warp[0] = c1_warp[1] = c1_warp[2] = 0.0f;
  c1_lwc[0] = 0; c2_lwc[0] = 0; c1_fw[0] = 0; c2_fw[0] = 0;
  memset(c1 + A_HOME_POS, 0, 12);

  assert(memfile_load(1));
  complete_mock_load(true);
  for (int i = 0; i < 40; i++) {
    memfile_frame();
    if (phase == L_FAST_WAIT_RESET)
      gd_set_bool(F_REQ_RESET_GIMMICK, false);
    /* Simulate Cucco 1 entering InterruptDrown -> IsForceWarp sends it straight to SINK (14) */
    if (*(uint32_t *)(c1_bsa + BSA_CUR_VERB) == ACTION_VERB_DROWN &&
        !(c1_bmp[CUCCO_NODE_SINK >> 5] & (1u << (CUCCO_NODE_SINK & 31)))) {
      c1_bmp[CUCCO_NODE_SINK >> 5] |= 1u << (CUCCO_NODE_SINK & 31);
      memcpy(c1_nodes_mem[4] + 0x18, &nf_on, 4);
    }
    /* Simulate Cucco 2 advancing 35 -> 125 -> 77 + 119 as step_drown_job fast-forwards each counter */
    if (*(uint32_t *)(c2_bsa + BSA_CUR_VERB) == ACTION_VERB_DROWN) {
      if (!(c2_bmp[CUCCO_NODE_IN_WATER >> 5] & (1u << (CUCCO_NODE_IN_WATER & 31))) &&
          !(c2_bmp[CUCCO_NODE_LOOK >> 5] & (1u << (CUCCO_NODE_LOOK & 31))) &&
          !(c2_bmp[CUCCO_NODE_JUMP >> 5] & (1u << (CUCCO_NODE_JUMP & 31)))) {
        c2_bmp[CUCCO_NODE_IN_WATER >> 5] |= 1u << (CUCCO_NODE_IN_WATER & 31);
        memcpy(c2_nodes_mem[0] + 0x18, &nf_on, 4);
      } else if ((c2_bmp[CUCCO_NODE_IN_WATER >> 5] & (1u << (CUCCO_NODE_IN_WATER & 31))) &&
                 *(float *)(c2_nodes_mem[0] + 0x20) >= 30.0f) {
        c2_bmp[CUCCO_NODE_IN_WATER >> 5] &= ~(1u << (CUCCO_NODE_IN_WATER & 31));
        c2_bmp[CUCCO_NODE_LOOK >> 5] |= 1u << (CUCCO_NODE_LOOK & 31);
        memcpy(c2_nodes_mem[1] + 0x18, &nf_on, 4);
      } else if ((c2_bmp[CUCCO_NODE_LOOK >> 5] & (1u << (CUCCO_NODE_LOOK & 31))) &&
                 *(float *)(c2_nodes_mem[1] + 0x20) >= 120.0f) {
        c2_bmp[CUCCO_NODE_LOOK >> 5] &= ~(1u << (CUCCO_NODE_LOOK & 31));
        c2_bmp[CUCCO_NODE_JUMP >> 5] |= 1u << (CUCCO_NODE_JUMP & 31);
        c2_bmp[CUCCO_NODE_JUMP_AIR >> 5] |= 1u << (CUCCO_NODE_JUMP_AIR & 31);
        memcpy(c2_nodes_mem[2] + 0x18, &nf_on, 4);
        memcpy(c2_nodes_mem[3] + 0x18, &nf_on, 4);
      }
    }
  }
  assert(phase == L_IDLE);

  /* Verify C1: respawn pos (WarpPos + home), respawn timer (node 14), fade timer (ModelComponent) */
  assert(c1_warp[0] == 123.0f && c1_warp[1] == 45.0f && c1_warp[2] == -67.0f);
  assert(memcmp(c1 + A_HOME_POS, c1_home, 12) == 0);
  assert(c1_fw[0] == 1 && c1_lwc[0] == 4);
  assert(*(float *)(c1_nodes_mem[4] + 0x20) == 22.5f);
  assert(*(float *)(c1_model + MODEL_ALPHA) == 0.35f);
  assert(fabsf(*(float *)(c1_model + MODEL_FADE_STEP) - (1.0f / 6.0f)) < 1e-5f);
  assert((*(uint32_t *)(c1_model + MODEL_FADE_FLAGS) & 0xC8u) == 0xC8u);
  assert(c1_model[MODEL_FADE_ACTIVE] == 1 && c1_vis[VIS_FADE_FLAGS] == 0x04);

  /* Verify C2: drown timer fast-forwarded 35 -> 125 -> 77 + 119 + LandingWaterCount */
  assert(c2_lwc[0] == 2);
  assert(*(float *)(c2_nodes_mem[2] + 0x20) == 48.0f);
  assert(*(float *)(c2_nodes_mem[3] + 0x20) == 4.5f);

  sd_clear();
  clear_post_load_jobs();
  for (int i = 0; i < MEMFILE_SLOTS; i++)
    free_memfile(&memfiles[i]);
  assert(pool_free_bytes() == MEMFILE_POOL_SIZE - sizeof(pool_hdr_t));
  puts("PASS: Cucco respawn pos (WarpPos/home), respawn timer, fade timer, drown timer + .szm round-trip");
}

int main(void)
{
  test_async_save_and_staging();
  test_full_reload_no_early_corruption();
  test_full_reload_happy_path();
  test_fast_same_map_reset_gimmick();
  test_link_at_tolerance_and_facing();
  test_camera_ai_chain();
  test_szm_export_import();
  test_scene_chains();
  test_szm_versions();
  test_memfile_folders();
  test_scene_actors();
  test_phase2_bb_vel_savepos();
  test_phase3_rng_mgrs_cutscene();
  test_bss_pool_and_error_reporting();
  test_held_actor_and_cucco_state();
  test_cucco_drown_fade_respawn_timers();
  return 0;
}
