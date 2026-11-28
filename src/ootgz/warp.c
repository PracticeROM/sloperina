/* Warps.
 *
 * Instant (warp_arm tries this first): request the same event an exit requests, with the same
 * parameters. Exits are FieldAndDungeonEntranceEventRequestActor; they request "Dm_SY_BancChangeGo"
 * with NextBanc / NextStartPos / NextStartPosAir / BootEventName / IsCheckAir, and that flow's
 * GameSystemActor.EventReserveBancChange (romfs GameSystemActor.pack, ReserveBancChange.module)
 * writes the Sequence_Reserved* GameData and sets Sequence_ControlInEvent = ChangeBancOnEvent, which
 * the Event sequence turns into the map change. The request is built and submitted the way the
 * SeqOneShotRequestEvent node does it (main+0x32db1a0), with params as QueryBancChangeEventRequestArg
 * (main+0x29bf380) fills them. So the warp gets the game's own fade, sound and arrival.
 *
 * Fallback, if the request is refused (game.py's method): clear Sequence_ReservedNextBanc, wait
 * for the game to fill it (Link entered an exit), then swap in our destination. Exits that lead
 * into a dungeon are left alone: swapping the map under their door transition left the screen
 * black for good (FINDINGS §5.16). */
#include "warp.h"

#include "gamedata.h"
#include "gfx.h"
#include "link.h"
#include "sys.h"
#include "z64hd.h"

#define STANDARD_ARRIVAL "Dm_SY_BancChangeArrive"
/* Spawns no exit leads to (warp-song pads, blue warps, boss rooms...) have no event starter for the
 * standard arrival, which crashed (FINDINGS §5.18). The game enters the warp-song pads with no starter
 * and this arrival (Dm_SY_Ocarina › OcarinaWarp's EventReserveBancChange); it needs no starter. */
#define NO_STARTER_ARRIVAL "OcarinaWarp_Arrive"

#define WARP_TIMEOUT_FRAMES (300 * GAME_FPS) /* how long an armed fallback waits for an exit */
#define REQUEST_TIMEOUT_FRAMES (3 * GAME_FPS) /* the event writes the reserved map within this, or we fall back */

bool warp_arrival_cutscenes;

static bool silent_arrival;
static const warp_map_t *target_map;
static const spawn_t *target_spawn;
static enum {
  WARP_NONE,
  WARP_ARMED,      /* fallback: waiting for Link to take an exit */
  WARP_DONE,
  WARP_SKIPPED,
  WARP_CANCELLED,
  WARP_TIMED_OUT,
  WARP_REQUESTED,  /* instant: waiting for the event to reserve our map */
} state;
static uint32_t deadline_frame;
static char state_text[96];

static bool str_equal(const char *a, const char *b)
{
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

static bool deadline_passed(void)
{
  return (int32_t)(frame_counter() - deadline_frame) > 0;
}

static const char *arrival_event(const spawn_t *spawn)
{
  if (!spawn->starter)
    return NO_STARTER_ARRIVAL;
  return warp_arrival_cutscenes ? spawn->event : STANDARD_ARRIVAL;
}

/* After the game has reserved our map: what the event's own reservation got wrong for spawns
 * without a starter (Dm_SY_BancChangeGo always names the spawn as the starter), or when a memfile
 * requests silent arrival without any arrival event/camera. */
static void fix_reservation(const spawn_t *spawn)
{
  if (silent_arrival) {
    gd_set_str(GD_STR_STARTER, "");
    gd_set_str(GD_STR_EVENT, "");
    return;
  }
  if (spawn->starter)
    return;
  gd_set_str(GD_STR_STARTER, "");
  gd_set_str(GD_STR_EVENT, NO_STARTER_ARRIVAL);
}

static bool is_dungeon(const char *name)
{
  for (int i = 0; i < data_n_warp_maps; i++)
    if (data_warp_maps[i].dungeon && str_equal(data_warp_maps[i].name, name))
      return true;
  return false;
}

/* --- instant: request the exit's event ------------------------------------------------------ */

typedef void (*req_ctor_t)(void *req);
typedef bool (*req_submit_t)(void *mgr, void *req);
typedef void (*req_dtor_t)(void *sub);

/* The game's strings here are a char * buffer followed by its u32 capacity. */
#define STRING_CAPACITY 0x8
#define REQ_PARAMS_CAPACITY 0x0  /* u32: how many param entries the array holds */
#define PARAM_TYPE 0x0           /* u32 */
#define PARAM_NAME 0x8           /* string */
#define PARAM_VALUE 0x98         /* u8 for a bool, else a pointer */
#define VTABLE_FIRST_ENTRY 0x10  /* an object's vtable pointer skips the offset-to-top and type info */

static _Alignas(16) uint8_t request[Z64HD_EVENT_REQ_SIZE]; /* game thread only */
static char pos_air[GD_STR_LEN + 8];

/* Copy src into the game's string buffer of `capacity` bytes, cut short if need be. */
static void copy_str(char *dst, uint32_t capacity, const char *src)
{
  uint32_t i = 0;
  for (; capacity && i < capacity - 1 && src[i]; i++)
    dst[i] = src[i];
  if (capacity)
    dst[i] = 0;
}

static void set_game_string(uint8_t *string_field, const char *src)
{
  copy_str(*(char **)string_field, *(uint32_t *)(string_field + STRING_CAPACITY), src);
}

static bool add_param(const char *name, uint32_t type, const void *value)
{
  uint32_t *count = (uint32_t *)(request + Z64HD_EVENT_REQ_NPARAMS);
  uint32_t capacity = *(uint32_t *)(request + REQ_PARAMS_CAPACITY);
  uint8_t *array = *(uint8_t **)(request + Z64HD_EVENT_REQ_PARAMS);
  if (!array || *count >= capacity)
    return false;
  uint8_t *entry = array + (size_t)*count * Z64HD_EVENT_PARAM_SIZE;
  set_game_string(entry + PARAM_NAME, name);
  *(uint32_t *)(entry + PARAM_TYPE) = type;
  if (type == Z64HD_EVENT_PARAM_BOOL)
    *(uint8_t *)(entry + PARAM_VALUE) = *(const bool *)value;
  else
    *(const void **)(entry + PARAM_VALUE) = value;
  ++*count;
  return true;
}

/* Destroy the request as the node does: base vtable into both sub-objects, then their destructor. */
static void destroy_request(void)
{
  uintptr_t base_vtable = MAIN(z64hd_game->event_req_sub_vt) + VTABLE_FIRST_ENTRY;
  *(uintptr_t *)(request + Z64HD_EVENT_REQ_SUB_B) = base_vtable;
  MAIN_FN(req_dtor_t, z64hd_game->event_req_sub_dtor)(request + Z64HD_EVENT_REQ_SUB_B);
  *(uintptr_t *)(request + Z64HD_EVENT_REQ_SUB_A) = base_vtable;
  MAIN_FN(req_dtor_t, z64hd_game->event_req_sub_dtor)(request + Z64HD_EVENT_REQ_SUB_A);
}

/* What the manager's acceptance (1.1.1 main+0xD7A9D0) refuses on: M+0xB8 bit 0, the running event's name at [M+0xC0]
 * (capacity at M+0xC8) other than the request's, or any running event ([M+0x68]) for a request that isn't a sub-event. */
static void log_refusal(uintptr_t manager)
{
  uint32_t flags = 0, cap = 0;
  char pending[64] = "?";
  sys_read32(manager + 0xB8, &flags);
  uintptr_t name = sys_ptr(manager + 0xC0);
  if (name && sys_read32(manager + 0xC8, &cap) && cap && sys_mapped(name, 1, SYS_PERM_R)) {
    size_t n = 0;
    size_t max = cap < sizeof(pending) ? cap : sizeof(pending);
    while (n + 1 < max && sys_mapped(name + n, 1, SYS_PERM_R) && ((const volatile char *)name)[n]) {
      pending[n] = ((const volatile char *)name)[n];
      n++;
    }
    pending[n] = 0;
  }
  log_printf("event request refused with manager flags %x, running event \"%s\", event running %d", (unsigned)flags,
             pending, sys_ptr(manager + 0x68) != 0);
}

bool warp_event_running(void)
{
  uintptr_t manager_holder = sys_ptr(MAIN(z64hd_game->event_mgr_ptr));
  uintptr_t manager = manager_holder ? sys_ptr(manager_holder) : 0;
  return manager && sys_ptr(manager + 0x68) != 0; /* the running event, as the acceptance checks (log_refusal) */
}

/* Start a request for the event `name` in `request`, as the node does; the event manager, or 0 (nothing built). */
static void *begin_request(const char *name)
{
  uintptr_t manager_holder = sys_ptr(MAIN(z64hd_game->event_mgr_ptr));
  void *manager = (void *)(manager_holder ? sys_ptr(manager_holder) : 0);
  if (!manager)
    return 0;
  memset(request, 0, sizeof(request));
  MAIN_FN(req_ctor_t, z64hd_game->event_req_ctor)(request);
  set_game_string(request + Z64HD_EVENT_REQ_NAME, name);
  return manager;
}

/* Submit the request begun (if its params went in: ok) and destroy it. */
static bool finish_request(void *manager, bool ok)
{
  if (ok) {
    ok = MAIN_FN(req_submit_t, z64hd_game->event_req_submit)(manager, request);
    if (!ok)
      log_refusal((uintptr_t)manager);
  }
  destroy_request();
  return ok;
}

static bool request_warp_event(const warp_map_t *map, const spawn_t *spawn)
{
  void *manager = begin_request("Dm_SY_BancChangeGo");
  if (!manager)
    return false;
  snprintf(pos_air, sizeof(pos_air), "%s_Air", spawn->pos);
  static const bool no = false;
  bool ok = add_param("NextBanc", Z64HD_EVENT_PARAM_STRING, map->name) &&
            add_param("NextStartPos", Z64HD_EVENT_PARAM_STRING, spawn->pos) &&
            add_param("NextStartPosAir", Z64HD_EVENT_PARAM_STRING, pos_air) &&
            add_param("BootEventName", Z64HD_EVENT_PARAM_STRING, arrival_event(spawn)) &&
            add_param("IsCheckAir", Z64HD_EVENT_PARAM_BOOL, &no);
  return finish_request(manager, ok);
}

bool warp_request_event(const char *name)
{
  void *manager = begin_request(name);
  return manager && finish_request(manager, true);
}

bool warp_request_event_from(const char *name, const float transform[12], uintptr_t actor)
{
  /* the starter's handle (+0x38) is the request's SUB_A, which destroy_request destroys */
  if (!z64hd_game->event_req_starter_mtx || !z64hd_game->actor_handle_entry ||
      z64hd_game->event_req_starter_mtx + 0x38 != Z64HD_EVENT_REQ_SUB_A)
    return false;
  void *manager = begin_request(name);
  if (!manager)
    return false;
  /* The transform, then the handle: its entry (+8) and id (+0x10) only. Its owned flag (+0x15) and count (+0x16)
   * stay 0 as the constructor left them, so the handle doesn't hold the actor: its destructor (1.1.1
   * main+0x20B6F0) then just sets the id back to -1 and lets go of nothing. */
  uint8_t *starter = request + z64hd_game->event_req_starter_mtx;
  memcpy(starter, transform, 12 * sizeof(float));
  uintptr_t at = actor + z64hd_game->actor_handle_entry;
  uintptr_t entry = actor && sys_mapped(at, 8, SYS_PERM_R) ? sys_ptr(at) : 0;
  uint32_t id;
  if (entry && sys_read32(entry + 0x48, &id)) {
    memcpy(starter + 0x38 + 0x08, &entry, sizeof(entry));
    memcpy(starter + 0x38 + 0x10, &id, sizeof(id));
  }
  return finish_request(manager, true);
}

/* --- fallback: swap the destination of the next exit Link takes ---------------------------------- */

static void arm_exit_swap(const warp_map_t *map, const spawn_t *spawn)
{
  if (!gd_set_str(GD_STR_NEXT_MAP, "")) {
    gfx_notice("can't warp: game data not found");
    return;
  }
  target_map = map;
  target_spawn = spawn;
  state = WARP_ARMED;
  deadline_frame = frame_counter() + WARP_TIMEOUT_FRAMES;
  log_printf("warp armed: %s / %s", map->name, spawn->pos);
  gfx_notice("warp ready: walk through any exit or loading zone");
}

const spawn_t *warp_last_target(const warp_map_t **map)
{
  *map = target_map;
  return target_spawn;
}

void warp_arm(const warp_map_t *map, const spawn_t *spawn)
{
  if (state == WARP_ARMED)
    state = WARP_CANCELLED;
  silent_arrival = false;
  if (request_warp_event(map, spawn)) {
    gd_set_str(GD_STR_NEXT_MAP, "");
    target_map = map;
    target_spawn = spawn;
    state = WARP_REQUESTED;
    deadline_frame = frame_counter() + REQUEST_TIMEOUT_FRAMES;
    log_printf("warp: requested Dm_SY_BancChangeGo to %s / %s", map->name, spawn->pos);
    return;
  }
  log_printf("warp: the game refused the event request; falling back to the next exit");
  arm_exit_swap(map, spawn);
}

bool warp_arm_silent(const warp_map_t *map, const spawn_t *spawn)
{
  if (state == WARP_ARMED)
    state = WARP_CANCELLED;
  silent_arrival = true;
  if (request_warp_event(map, spawn)) {
    gd_set_str(GD_STR_NEXT_MAP, "");
    target_map = map;
    target_spawn = spawn;
    state = WARP_REQUESTED;
    deadline_frame = frame_counter() + REQUEST_TIMEOUT_FRAMES;
    log_printf("warp: requested Dm_SY_BancChangeGo to %s / %s (silent)", map->name, spawn->pos);
    return true;
  }
  silent_arrival = false;
  return false;
}

void warp_cancel(void)
{
  if (state == WARP_ARMED || state == WARP_REQUESTED) {
    state = WARP_CANCELLED;
    silent_arrival = false;
    gfx_notice("warp cancelled");
  } else {
    gfx_notice("no warp armed");
  }
}

/* --- frame -------------------------------------------------------------------------------------- */

/* Instant warp: wait for the event's EventReserveBancChange to write our map into
 * Sequence_ReservedNextBanc. */
static void wait_for_reservation(void)
{
  const char *next = gd_get_str(GD_STR_NEXT_MAP);
  if (next && str_equal(next, target_map->name)) {
    fix_reservation(target_spawn);
    state = WARP_DONE;
    log_printf("warp: the game reserved %s%s", next,
               silent_arrival ? " (silent arrival)" : /* not menu text */
               target_spawn->starter ? "" : " (no starter: warp-song arrival)"); /* not menu text */
    silent_arrival = false;
  } else if (deadline_passed()) {
    if (silent_arrival) {
      silent_arrival = false;
      state = WARP_TIMED_OUT;
      log_printf("warp: silent reservation timed out");
    } else {
      log_printf("warp: the event didn't start; falling back to the next exit");
      arm_exit_swap(target_map, target_spawn);
    }
  }
}

/* Fallback: once Link takes an exit (the game fills the next map in), put our destination there. */
static void swap_exit_destination(void)
{
  if (deadline_passed()) {
    state = WARP_TIMED_OUT;
    gfx_notice("warp timed out");
    return;
  }
  const char *next = gd_get_str(GD_STR_NEXT_MAP);
  if (!next || !next[0])
    return;
  if (is_dungeon(next)) {
    state = WARP_SKIPPED;
    log_printf("warp skipped: the exit leads into %s", next);
    gfx_notice("warp skipped: that exit is a dungeon door; warp again from there");
    return;
  }
  gd_set_str(GD_STR_NEXT_MAP, target_map->name);
  gd_set_str(GD_STR_SPAWN, target_spawn->pos);
  gd_set_str(GD_STR_STARTER, target_spawn->starter ? target_spawn->pos : "");
  gd_set_str(GD_STR_EVENT, arrival_event(target_spawn));
  state = WARP_DONE;
  log_printf("warp: %s replaced by %s / %s", next, target_map->name, target_spawn->pos);
}

void warp_frame(void)
{
  if (state == WARP_REQUESTED)
    wait_for_reservation();
  else if (state == WARP_ARMED)
    swap_exit_destination();
}

bool warp_pending(void) { return state == WARP_ARMED || state == WARP_REQUESTED; }
bool warp_requested(void) { return state == WARP_REQUESTED || state == WARP_DONE; }
bool warp_abandoned(void) { return state == WARP_CANCELLED || state == WARP_TIMED_OUT; }

const char *warp_state(void)
{
  static const char *const state_names[] = { "none", "armed", "done", "skipped (dungeon door)", "cancelled",
                                             "timed out", "starting" };
  if (state == WARP_NONE)
    return "none";
  snprintf(state_text, sizeof(state_text), "%s: %s", state_names[state], target_map->label);
  return state_text;
}
