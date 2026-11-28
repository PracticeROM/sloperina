/* The command file (remote.c): reading a script, its lines, menu items by name, waiting, pausing and stepping,
 * and what out.txt gets; no game. The SD card, the menu tree and the game's state are stand-ins. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../src/ootgz/binds.c"
#include "../src/ootgz/remote.c"

/* --- stand-ins ------------------------------------------------------------------------------- */

static uint32_t now; /* the game's frame counter */
uint32_t frame_counter(void) { return now; }
uint32_t input_held(void) { return 0; }
uint32_t input_pressed(void) { return 0; }
const char *T(const char *english) { return english; }
void gfx_logf(const char *fmt, ...) {}
void log_printf(const char *fmt, ...) {}
bool settings_sd(void) { return true; }
static float test_moved_x; /* a warp or teleport moves Link this far in x */
static bool test_follow;    /* a load position puts Link where it says (else he stays put) */
static vec3f_t test_placed_at;
static bool test_was_placed;
bool link_drawn_pos(vec3f_t *out)
{
  *out = test_was_placed ? test_placed_at : (vec3f_t){ 1.5f + test_moved_x, 2, -3 };
  return true;
}
bool gd_ready(void) { return true; }
static char test_str[64] = "KokiriForest";
const char *gd_get_str(uint32_t hash) { return test_str; }
bool gd_set_str(uint32_t hash, const char *v) { snprintf(test_str, sizeof(test_str), "%s", v); return true; }
bool gd_game_paused(bool *is_pause, int32_t *pause_requests) { return false; }
/* flag: one Bool field, 0x39d7877d, and nothing else */
static bool test_flag;
uint32_t murmur3(const char *s) { return s[0] == 'M' ? 0x39d7877du : 1u; }
bool gd_get_bool(uint32_t hash, bool *out) { if (hash != 0x39d7877du) return false; *out = test_flag; return true; }
bool gd_set_bool(uint32_t hash, bool value) { if (hash != 0x39d7877du) return false; test_flag = value; return true; }
bool gd_get_int(uint32_t hash, int32_t *out) { return false; }
bool gd_poke_int(uint32_t hash, int32_t value) { return false; }
static const char *test_screen = "None";
const char *gd_menu_screen_name(void) { return test_screen; }
const char *warp_state(void) { return ""; }
bool warp_request_event(const char *name) { return (void)name, false; }
bool warp_request_event_from(const char *name, const float transform[12], uintptr_t actor) { return (void)name, (void)transform, (void)actor, false; }
/* Link's actor and the player object, as status and trace read them at 1.1.1's offsets (z64hd.h): his velocity at
 * +0x388, the item on ZR and in hand at the player object's +0x7D0 and +0x7D8 (the jump height's address is that
 * object + 0x18E8), and his actions through three pointers to the action table. A build whose offsets aren't
 * known (all 0) reads none of them. */
static const struct z64hd_build build_1_1_1 = {
  .link_velocity = 0x388, .player_zr_item = 0x7D0, .player_held_kind = 0x7D8, .player_lock_state = 0x1C74,
  .player_on_ground = 0x13D0, .player_actions_1 = 0x18, .player_actions_2 = 0x270, .player_actions_3 = 0x188,
  .action_main_slot = 0x1C0, .action_weapon_slot = 0x1C8, .action_id = 0x4C,
};
static const struct z64hd_build build_unknown = { .version = "1.0.0" };
const struct z64hd_build *z64hd_game = &build_1_1_1;
uintptr_t z64hd_main;
static _Alignas(8) uint8_t actor_mem[0x400], player_mem[0x1D00];
static float facing_x = 1, facing_z = 0;
uintptr_t link_actor(void) { return (uintptr_t)actor_mem; }
static _Alignas(8) uint8_t hop1[0x280], hop2[0x190], action_table[0x1D0], main_action[0x50], weapon_action[0x50];
uintptr_t sys_ptr(uintptr_t addr)
{
  uintptr_t p;
  memcpy(&p, (const void *)addr, 8);
  return p;
}
bool sys_read32(uintptr_t addr, uint32_t *out)
{
  memcpy(out, (const void *)addr, 4);
  return true;
}
bool sys_mapped(uintptr_t addr, size_t size, uint32_t perm) { return true; }
bool link_facing(double *fx, double *fz)
{
  *fx = facing_x, *fz = facing_z;
  return true;
}
bool js_height_address(uintptr_t *addr)
{
  *addr = (uintptr_t)player_mem + 0x18E8;
  return true;
}
/* load position's side: whether Link's record is known, and where a place put the saved position */
static link_record_status_t record = REC_READY;
static int position_loads;
static double placed[4];
link_record_status_t link_record_status(void) { return record; }
bool position_busy(void) { return false; }
void position_load(void)
{
  position_loads++;
  if (test_follow) {
    test_placed_at = (vec3f_t){ (float)placed[0], (float)placed[1], (float)placed[2] };
    test_was_placed = true;
  }
}
void position_set(double x, double y, double z, double facing)
{
  placed[0] = x, placed[1] = y, placed[2] = z, placed[3] = facing;
}
static actor_near_t actors_there[3] = { { .name = "Npc_Kokiri", .distance = 2.5f, .pos = { 1.5f, 2, -0.5f } },
                                        { .name = "Obj_Sign", .distance = 4.25f, .pos = { 5.75f, 2, -3 } },
                                        { .name = "Npc_Far", .distance = 30, .pos = { 31.5f, 2, -3 } } };
/* spawn's side: the names offered, and the game's create, whose actor is made spawn_delay frames after the request */
const char *const actor_names[] = { "Enemy_Wolfos", "Obj_Pot_A_01" };
const int actor_names_count = 2;
static int spawned_now;
static uint32_t spawn_delay = 3, spawn_at;
static uint32_t spawn_refused; /* the game's result code when it refuses; 0: queued */
bool actor_spawn_handle(const char *name, const float pos[3], float yaw_degrees, uintptr_t *handle, uint32_t *result)
{
  *result = spawn_refused;
  *handle = 0;
  if (spawn_refused)
    return false;
  *handle = 1;
  spawn_at = now + spawn_delay;
  return true;
}
spawn_poll_t actor_spawn_poll(uintptr_t *handle, uintptr_t *actor)
{
  *actor = 0;
  if (now < spawn_at)
    return SPAWN_WAITING;
  *handle = 0;
  *actor = 0x1000 + (uintptr_t)spawned_now++;
  return SPAWN_MADE;
}
void actor_spawn_cancel(uintptr_t *handle) { *handle = 0; }
bool actor_read(uintptr_t actor, actor_read_t *out)
{
  *out = (actor_read_t){ .name = "made", .running = true, .state = 4 };
  return true;
}

/* the message window and the running event, as the test sets them */
static dialog_t test_dialog = { DLG_CLOSED, 12, -1, "" };
static const char *test_event; /* 0: none runs */
bool dialog_read(dialog_t *out)
{
  *out = test_dialog;
  return true;
}
const char *dialog_state_name(dialog_state_t state)
{
  static const char *const names[] = { "?", "closed", "typing", "waiting", "turning", "choices", "choice", "other" };
  return names[state];
}
bool dialog_open(const dialog_t *d) { return d->state != DLG_CLOSED && d->state != DLG_UNKNOWN; }
bool eventskip_running(void) { return test_event != 0; }
static int test_wheel; /* 0 closed, 1 opening, 2 open */
int wheel_phase(void) { return test_wheel; }
bool eventskip_flow_name(char out[EVENTSKIP_NAME_MAX])
{
  snprintf(out, EVENTSKIP_NAME_MAX, "%s", test_event ? test_event : "");
  return test_event != 0;
}
int actors_near(float radius, actor_near_t *out, int max)
{
  int n = 0;
  for (int i = 0; i < 3 && n < max; i++)
    if (actors_there[i].distance <= radius)
      out[n++] = actors_there[i];
  return n;
}

/* return to title's side: why it can't start (0: it starts), and how it goes once started */
static const char *title_problem;
static title_status_t title_now = TITLE_IDLE;
static const char *title_text = "";
static int title_starts;
const char *title_start(void)
{
  if (title_problem)
    return title_problem;
  title_starts++;
  title_now = TITLE_PENDING;
  title_text = "waiting for the game to start the event";
  return 0;
}
title_status_t title_status(const char **text)
{
  *text = title_text;
  return title_now;
}
bool input_remote_active;
uint32_t input_remote_buttons;
bool input_remote_sticks[2];
int32_t input_remote_stick[4];

/* the SD card: cmd.txt (there or not) and out.txt */
static char cmd_file[SCRIPT_MAX * 2];
static size_t cmd_size;
static bool cmd_there, cmd_locked;
static char out_file[1 << 16];
static size_t out_size;
static int opened;
enum { H_CMD = 1, H_OUT = 2 };

nn_result_t fs_create_dir(const char *path) { return 0; }
nn_result_t fs_create_file(const char *path, int64_t size) { return 0; }
nn_result_t fs_delete_file(const char *path)
{
  if (strcmp(path, CMD_FILE) || cmd_locked)
    return 1;
  cmd_there = false;
  return 0;
}
nn_result_t fs_open(fs_file_t *out, const char *path, int mode)
{
  bool is_cmd = !strcmp(path, CMD_FILE);
  if (is_cmd && !cmd_there)
    return 1;
  out->handle = (void *)(intptr_t)(is_cmd ? H_CMD : H_OUT);
  opened++;
  return 0;
}
nn_result_t fs_size(int64_t *out, fs_file_t f)
{
  *out = (intptr_t)f.handle == H_CMD ? (int64_t)cmd_size : (int64_t)out_size;
  return 0;
}
nn_result_t fs_read(size_t *read, fs_file_t f, int64_t offset, void *buf, size_t size)
{
  memcpy(buf, cmd_file + offset, size);
  *read = size;
  return 0;
}
nn_result_t fs_write(fs_file_t f, int64_t offset, const void *buf, size_t size, const fs_write_option_t *opt)
{
  assert((intptr_t)f.handle == H_OUT && (size_t)offset == out_size && out_size + size < sizeof(out_file));
  memcpy(out_file + out_size, buf, size);
  out_size += size;
  out_file[out_size] = 0;
  return 0;
}
void fs_close(fs_file_t f) { opened--; }

/* the menu: root > cheats (a checkbox, an option, a number, two actions of one name) and a line of text */
menu_t menu_root, *menu_current;
menu_item_t *menu_editing;
static menu_t m_cheats;
static menu_item_t root_items[4], cheat_items[9];
static bool magic;
static int speed = 1, rupees = 10, reloads, closes;
static const char *const speeds[] = { "slow", "normal", "fast" };

void menu_open(menu_t *root) { menu_current = root; }
void menu_back(void) {}
uintptr_t z64hd_main;
int actors_named(const char *name, uintptr_t *out, int max) { return (void)name, (void)out, (void)max, 0; }
int guardview_rails_of(uintptr_t actor) { return (void)actor, 0; }
void interactview_door_watch(uintptr_t door) { (void)door; }
void interactview_door_text(char *buf, int len) { snprintf(buf, len, "closed"); }
void drownview_watch(uintptr_t actor) { (void)actor; }
void drownview_text(int which, char *buf, int len) { (void)which; snprintf(buf, len, "-"); }
int drownview_respawn(uintptr_t actor, float out[3]) { (void)actor; (void)out; return 0; }
void drownview_dump(uintptr_t actor) { (void)actor; }
void drownview_toggle(uintptr_t actor) { (void)actor; }
int drownview_kind(uintptr_t actor) { (void)actor; return 0; }
int interactview_door_is_open(uintptr_t door) { return (void)door, -1; }
float interactview_door_angle(uintptr_t door) { return (void)door, -1; }
void menu_goto(menu_t *m) { menu_current = m; }
/* debugging commands' (hitlist, hitparts, pausetype, equip, frametrace, physdump, texdump, hudtest): not run here */
void frametrace_arm(void) {}
int frametrace_targets(uintptr_t *tex, uint32_t *sets, int max) { return (void)tex, (void)sets, (void)max, 0; }
void frametrace_write(void) {}
int hitview_list(char *out, int len) { return (void)len, out[0] = 0, 0; }
int hitview_list_all(const char *name, char *out, int len) { return (void)name, (void)len, out[0] = 0, 0; }
uint8_t hitview_parts_of(uintptr_t actor) { return (void)actor, 0; }
int nvn_hud_bind_log(char *lines, int len) { return (void)len, lines[0] = 0, 0; }
int nvn_hud_binds(uintptr_t *cmds, int *binds, int max) { return (void)cmds, (void)binds, (void)max, 0; }
volatile bool nvn_hud_test;
volatile uintptr_t nvn_hud_test_target;
volatile int nvn_hud_test_bind;
volatile uintptr_t nvn_hud_test_cmd;
void nvn_texdump(uintptr_t texture, int bytes_per_pixel) { (void)texture, (void)bytes_per_pixel; }
void nvn_texdump_write(void) {}
void nvn_texture_info(uintptr_t texture, int *w, int *h, int *format) { (void)texture, *w = *h = *format = 0; }
bool pausemgr_add(uint32_t id, int delta) { return (void)id, (void)delta, false; }
int pausemgr_count(uint32_t id) { return (void)id, -1; }
uint32_t pausemgr_id(const char *name_or_hex) { return (void)name_or_hex, 0; }
int pausemgr_list(char *out, int len) { return (void)len, out[0] = 0, 0; }
bool pausemgr_set_masks(uint32_t id, const uint32_t masks[3], uint32_t was[3]) { return (void)id, (void)masks, (void)was, false; }
bool physdump_write(const char *name, int *records, int64_t *bytes) { return (void)name, (void)records, (void)bytes, false; }
bool pouch_button(int slot, const char *choice, uint32_t *was) { return (void)slot, (void)choice, (void)was, false; }
bool pouch_equip_shield_now(const char *item) { return (void)item, false; }
void menu_item_line(menu_item_t *it, char *buf, int len) { snprintf(buf, len, "%s", it->label ? it->label : ""); }

void menu_close(void)
{
  menu_current = 0;
  menu_editing = 0;
  closes++;
}
static bool get_magic(menu_item_t *it) { return magic; }
static void set_magic(menu_item_t *it, bool on) { magic = on; }
static int get_speed(menu_item_t *it) { return speed; }
static void set_speed(menu_item_t *it, int choice) { speed = choice; }
static int32_t get_rupees(menu_item_t *it) { return rupees; }
static void set_rupees(menu_item_t *it, int32_t v) { rupees = v; }
static int32_t max_rupees(menu_item_t *it) { return 999; }
static void act_reload(menu_item_t *it) { reloads += (int)it->data; }
static void act_keyboard(menu_item_t *it) { menu_editing = it; } /* opens something to type in */
static void text_where(menu_item_t *it, char *buf, int len) { snprintf(buf, len, "\x01 Link  at  12.5"); }

static void build_menu(void)
{
  menu_root = (menu_t){ .title = "sloperinaGZ", .items = root_items, .n_items = 3 };
  root_items[0] = (menu_item_t){ .kind = MI_SUBMENU, .label = "cheats", .submenu = &m_cheats };
  root_items[1] = (menu_item_t){ .kind = MI_TEXT, .text = text_where };
  root_items[2] = (menu_item_t){ .kind = MI_BACK, .label = "close" };
  m_cheats = (menu_t){ .title = "cheats", .items = cheat_items, .n_items = 9, .parent = &menu_root };
  cheat_items[0] = (menu_item_t){ .kind = MI_CHECK, .label = "Infinite  Magic", .get_check = get_magic,
                                  .set_check = set_magic };
  cheat_items[1] = (menu_item_t){ .kind = MI_OPTION, .label = "speed", .n_choices = 3, .choices = speeds,
                                  .get_option = get_speed, .set_option = set_speed };
  cheat_items[2] = (menu_item_t){ .kind = MI_NUMBER, .label = "rupees", .get_num = get_rupees,
                                  .set_num = set_rupees, .num_max = max_rupees };
  cheat_items[3] = (menu_item_t){ .kind = MI_ACTION, .label = "reload map ({savepos})", .action = act_reload,
                                  .data = 1 };
  cheat_items[4] = (menu_item_t){ .kind = MI_ACTION, .label = "reload map", .action = act_reload, .data = 10 };
  cheat_items[5] = (menu_item_t){ .kind = MI_ACTION, .label = "name it", .action = act_keyboard };
  cheat_items[6] = (menu_item_t){ .kind = MI_ACTION, .label = "start/stop timer", .action = act_reload, .data = 100 };
  cheat_items[7] = (menu_item_t){ .kind = MI_ACTION, .label = "start", .action = act_reload, .data = 1000 };
  cheat_items[8] = (menu_item_t){ .kind = MI_BACK, .label = "return" };
}

/* --- the test's own ------------------------------------------------------------------------- */

static void send(const char *text)
{
  cmd_size = strlen(text);
  memcpy(cmd_file, text, cmd_size);
  cmd_there = true;
}

/* one frame of the game: the controller's read (nothing held), then the script */
static void frame(void)
{
  now++;
  binds_update(0, 0, 0);
  remote_frame(false);
}

/* frames until the script is taken and done (not past a wait it's still in) */
static void run_script(const char *text)
{
  out_size = 0;
  out_file[0] = 0;
  send(text);
  for (int i = 0; i < 4 * POLL_CALLS && (cmd_there || running); i++)
    frame();
}

static void out_has(const char *want)
{
  if (!strstr(out_file, want)) {
    printf("out.txt has no \"%s\":\n%s", want, out_file);
    assert(0);
  }
}

int main(void)
{
  binds_reset();
  build_menu();
  remote_enabled = false;
  send("echo no\n");
  for (int i = 0; i < 20; i++)
    frame();
  assert(cmd_there && !out_size); /* off: the file is left alone */
  remote_enabled = true;

  /* a script is taken (deleted), its lines run in order, each with a result, then "end" */
  run_script("# a comment\n\nid t1\necho hello  there \r\nnonsense 1\n");
  assert(!cmd_there && !running && !opened);
  out_has(" t1 4 ok hello  there\n");
  out_has(" t1 5 err not a command: nonsense\n");
  out_has(" t1 end\n");
  assert(!strncmp(out_file, "26 t1 4 ok", 10)); /* the frame it ran on: the sixth call since it was on */

  /* fire: the command reads as pressed that frame, as held for the frames asked, then not */
  out_size = 0;
  send("fire savepos\nwait 2\nfire rise 3\n");
  for (int i = 0; i < POLL_CALLS - 1; i++)
    frame();
  assert(!cmd_pressed(CMD_SAVEPOS));
  frame();
  assert(cmd_pressed(CMD_SAVEPOS) && running && !cmd_there);
  frame();
  assert(!cmd_pressed(CMD_SAVEPOS) && !cmd_active(CMD_SAVEPOS) && !cmd_pressed(CMD_RISE));
  frame(); /* the wait's second frame: the next line runs */
  assert(cmd_pressed(CMD_RISE) && cmd_held(CMD_RISE) && !running);
  for (int i = 0; i < 3; i++) {
    frame();
    assert(!cmd_pressed(CMD_RISE) && cmd_held(CMD_RISE));
  }
  frame();
  assert(!cmd_held(CMD_RISE));
  out_has(" 1 ok fire savepos\n");
  out_has(" 3 ok fire rise\n");
  run_script("fire nothing\nfire rise x\n");
  out_has("1 err fire: not a command");
  out_has("2 err fire: a number of frames");

  /* menu items by name: any case and spacing, a label's bind left out, #n and @n */
  run_script("set cheats/infinite magic = on\nget CHEATS / Infinite Magic\ndo cheats/infinite magic\n");
  out_has(" 1 ok check = on\n");
  out_has(" 2 ok check = on\n");
  out_has(" 3 ok check = off\n");
  assert(!magic);
  run_script("set cheats/speed = fast\nget cheats/speed\nset cheats/speed = 0\ndo cheats/speed\nset cheats/speed = warp\n");
  out_has(" 1 ok option = 2 (fast)\n");
  out_has(" 3 ok option = 0 (slow)\n");
  out_has(" 4 ok option = 1 (normal)\n");
  out_has(" 5 err set: not one of its choices");
  assert(speed == 1);
  run_script("set cheats/rupees = 250\nset cheats/rupees = 1000\nset cheats/rupees = lots\nget cheats/@2\n");
  out_has(" 1 ok number = 250\n");
  out_has(" 2 err set: out of its range\n");
  out_has(" 3 err set: a number\n");
  out_has(" 4 ok number = 250\n");
  assert(rupees == 250);
  run_script("do cheats/reload map\ndo cheats/reload map#2\ndo cheats/reload map#3\n");
  assert(reloads == 11);
  out_has(" 1 ok action\n");
  out_has(" 3 err do: no \"reload map#3\" in cheats\n");
  run_script("do cheats\ndo cheats/speed/slow\ndo nowhere/x\nget @1\ndo cheats/name it\nset cheats/rupees\n");
  out_has(" 1 err do: a menu");
  out_has(" 2 err do: \"speed\" isn't a menu\n");
  out_has(" 3 err do: no \"nowhere/x\" in sloperinaGZ\n");
  out_has(" 4 ok text = link at 12.5\n");
  out_has(" 5 err do: it opened something to type or pick in");
  out_has(" 6 err set: <menu path> = <value>\n");
  assert(!menu_editing && closes == 1);
  /* names with a "/" in them: the longest that fits, with or without spaces round the path's own "/" */
  reloads = 0;
  run_script("do cheats/start/stop timer\ndo cheats / start\ndo cheats/start/stop\ndo cheats/@3\ndo cheats/@30\n");
  assert(reloads == 1101);
  out_has(" 3 err do: \"start\" isn't a menu\n");
  out_has(" 5 err do: no \"@30\" in cheats\n");
  run_script("list\nlist cheats\nlist cheats/speed\n");
  out_has(" 1 | @0 menu cheats\n");
  out_has(" 1 | @1 text  = link at 12.5\n");
  out_has(" 1 ok 3 items in sloperinaGZ\n");
  out_has(" 2 | @1 option speed = 1 (normal)\n");
  out_has(" 2 | @3 action reload map\n");
  out_has(" 3 err list: not a menu\n");

  /* status: the frame, the map, where Link is and which way he faces, how he moves (nothing yet), the game held */
  run_script("status\n");
  out_has(" 1 ok screen=None wheel=closed dialog=closed msg=- event=- frame=");
  out_has(" map=KokiriForest link=yes x=1.500 y=2.000 z=-3.000 facing=90.0 vy=0.000 hs=0.000 hand=0 zr=0 lock=0 "
          "ground=0 act=-1 wpn=-1 held=0 menu=0 game_paused=0 warp=-\n");

  /* pause, step and run, as frame advance takes them (ootgz.c), with input and a fire for the frames let through */
  out_size = 0;
  out_file[0] = 0;
  send("pause\ninput A+ZL ls 100 -200 for 2\nfire loadpos\nwait 1\nstep 3\nstatus\nrun\necho after\n");
  for (int i = 0; i < POLL_CALLS; i++)
    frame();
  assert(remote_take_pause() && !remote_take_pause() && running);
  frame();
  assert(!strstr(out_file, "paused") && !input_remote_active); /* not held yet: the pause waits */
  remote_frame(true);                                           /* held: on to the lines after it */
  out_has(" 1 ok paused\n");
  out_has(" 2 ok input 0x101 for 2\n");
  out_has(" 4 err wait: the game is held");
  assert(input_remote_active && input_remote_buttons == (BTN_A | BTN_ZL) && input_remote_sticks[0] &&
         !input_remote_sticks[1] && input_remote_stick[0] == 100 && input_remote_stick[1] == -200);
  assert(!cmd_pressed(CMD_LOADPOS)); /* fired while held: on the first frame let through */
  assert(!strstr(out_file, "status") && !remote_take_run());
  assert(remote_take_step());
  frame();
  assert(cmd_pressed(CMD_LOADPOS) && input_remote_active);
  assert(remote_take_step());
  frame();
  assert(!cmd_pressed(CMD_LOADPOS) && !input_remote_active); /* the input's two frames are over */
  assert(remote_take_step() && !remote_take_step());
  frame();
  assert(!strstr(out_file, "stepped")); /* the third frame ran; held again before the step is done */
  remote_frame(true);
  out_has(" 5 ok stepped\n");
  out_has(" 6 ok screen=None wheel=closed dialog=closed msg=- event=- frame=");
  out_has(" map=KokiriForest link=yes x=1.500 y=2.000 z=-3.000 facing=90.0 vy=0.000 hs=0.000 ");
  out_has(" act=-1 wpn=-1 held=1 menu=0 game_paused=0 warp=-\n");
  assert(remote_take_run() && !remote_take_run() && running);
  remote_frame(true);
  assert(!strstr(out_file, "after")); /* run waits until the game runs */
  frame();
  out_has(" 7 ok running\n");
  out_has(" 8 ok after\n");
  assert(!running);
  run_script("step\n"); /* a step with the game running pauses first */
  assert(remote_take_pause() && remote_take_step() && !remote_take_step() && running);
  remote_frame(true);
  out_has(" 1 ok stepped\n");
  run_script("input off\ninput 0x3 for 0\ninput A+Q\ninput A ls 1\ninput - rs 0 40000\n");
  out_has(" 2 ok input 0x3 for 0\n");
  out_has(" 3 err input: buttons");
  out_has(" 4 err input: a stick");
  out_has(" 5 err input: a stick");
  for (int i = 0; i < 5; i++)
    frame();
  assert(input_remote_active && input_remote_buttons == 3); /* for 0: until the next input */
  run_script("input off\n");
  assert(!input_remote_active);
  /* turning remote control off lets go of a script's input, "for 0" and "for n" alike */
  run_script("input A for 0\n");
  assert(input_remote_active);
  remote_enabled = false;
  frame();
  assert(!input_remote_active && !input_remote_buttons);
  remote_enabled = true;
  run_script("input B for 50\n");
  assert(input_remote_active);
  remote_enabled = false;
  frame();
  assert(!input_remote_active);
  remote_enabled = true;

  /* status has how Link moves and what he holds; trace gives a line a frame, without holding the script up;
   * actors lists who is near */
  float velocity[3] = { 3, 6.125f, -4 };
  int32_t zr_item = 0, held = -1;
  memcpy(actor_mem + 0x388, velocity, sizeof(velocity));
  memcpy(player_mem + 0x7D0, &zr_item, 4);
  memcpy(player_mem + 0x7D8, &held, 4);
  uint32_t lock_state = 2, on_ground = 0x101;
  memcpy(player_mem + 0x1C74, &lock_state, 4);
  memcpy(player_mem + 0x13D0, &on_ground, 4);
  uintptr_t chain[5] = { (uintptr_t)hop2, (uintptr_t)action_table, (uintptr_t)main_action, (uintptr_t)weapon_action,
                         (uintptr_t)hop1 };
  memcpy(player_mem + 0x18, &chain[4], 8);
  memcpy(hop1 + 0x270, &chain[0], 8);
  memcpy(hop2 + 0x188, &chain[1], 8);
  memcpy(action_table + 0x1C0, &chain[2], 8);
  memcpy(action_table + 0x1C8, &chain[3], 8);
  uint32_t walk = 1, drawn = 57;
  memcpy(main_action + 0x4C, &walk, 4);
  memcpy(weapon_action + 0x4C, &drawn, 4);
  run_script("status\n");
  out_has(" facing=90.0 vy=6.125 hs=5.000 hand=-1 zr=0 lock=2 ground=1 act=1 wpn=57 held=0 menu=0 game_paused=0 "
          "warp=-\n");
  out_size = 0;
  send("trace 3\necho now\nwait 5\necho later\n");
  for (int i = 0; i < POLL_CALLS; i++)
    frame();
  out_has(" 1 ok trace 3\n");
  out_has(" 2 ok now\n");
  assert(!strstr(out_file, " t y=")); /* from the next frame on */
  held = 3;
  memcpy(player_mem + 0x7D8, &held, 4);
  for (int i = 0; i < 6; i++)
    frame();
  out_has(" 4 ok later\n");
  int traced = 0;
  for (const char *p = out_file; (p = strstr(p, " t y=2.000 vy=6.125 hs=5.000 x=1.500 z=-3.000 hand=3 zr=0 paused=0 lock=2 ground=1 act=1 wpn=57\n")); p++)
    traced++;
  assert(traced == 3);
  run_script("trace 100\ntrace off\nwait 2\ntrace x\n");
  assert(!strstr(out_file, " t y="));
  out_has(" 4 err trace: a number of frames, or off\n");
  /* where the build's offsets aren't known (1.0.0's), or no build is: none of it is read, and it says so */
  for (int i = 0; i < 2; i++) {
    z64hd_game = i ? 0 : &build_unknown;
    run_script("status\ntrace 1\nwait 1\n");
    out_has(" facing=90.0 vy=- hs=- hand=-1 zr=-1 lock=-1 ground=-1 act=-1 wpn=-1 held=0 ");
    out_has(" t y=2.000 vy=- hs=- x=1.500 z=-3.000 hand=-1 zr=-1 paused=0 lock=-1 ground=-1 act=-1 wpn=-1\n");
  }
  z64hd_game = &build_1_1_1;
  run_script("actors\nactors 3\nactors far\n");
  out_has(" 1 | 2.50 Npc_Kokiri 1.50 2.00 -0.50\n");
  out_has(" 1 | 4.25 Obj_Sign 5.75 2.00 -3.00\n");
  out_has(" 1 ok 2 actors within 6 m\n");
  out_has(" 2 ok 1 actors within 3 m\n");
  out_has(" 3 err actors: a distance in metres\n");

  /* spawn: only the names the game has, any case; 2 m (or as said) in front of Link, facing him; the
   * line ends when the actor is there; the game's refusal is passed on */
  run_script("spawn enemy_wolfos\nspawn Obj_Pot_A_01 3.5\nspawn Enemy_Nope\nspawn\nspawn Enemy_Wolfos x\n");
  out_has(" 1 ok spawn Enemy_Wolfos: made after 3 frames\n");
  out_has(" 2 ok spawn Obj_Pot_A_01: made after 3 frames\n");
  out_has(" 3 err spawn: Enemy_Nope isn't one of the 2 actors the game has\n");
  out_has(" 4 err spawn: an actor's name");
  out_has(" 5 err spawn: an actor's name");
  assert(spawned_now == 2);
  spawn_refused = 6;
  run_script("spawn Enemy_Wolfos\n");
  out_has(" 1 err spawn Enemy_Wolfos: the game refused it (6)\n");
  spawn_refused = 0;

  /* place: the saved position set and loaded; facing as now unless given; refused while Link's record isn't known */
  test_follow = true; /* he goes where load position says at once */
  run_script("place -12.45 110.36 4.66 250\nplace 1 2.5 -3\nplace 1 2\nplace 1 2 x\n");
  out_has(" 1 ok place -12.450 110.360 4.660 facing 250.0 after 1 frames\n");
  out_has(" 2 ok place 1.000 2.500 -3.000 facing 90.0 after 1 frames\n");
  out_has(" 3 err place: x y z");
  out_has(" 4 err place: x y z");
  assert(position_loads == 2 && placed[0] == 1 && placed[1] == 2.5 && placed[2] == -3 && placed[3] > 89.9);
  record = REC_NONE;
  run_script("place 1 2 3\n");
  out_has(" 1 err place: Link not found\n");
  assert(position_loads == 2);
  record = REC_READY;
  /* the first load only finds his record: the line loads again every 20 frames until he's there */
  test_follow = false;
  test_was_placed = false;
  out_size = 0;
  out_file[0] = 0;
  send("place 7 2 7\n");
  for (int i = 0; i < POLL_CALLS + 30; i++)
    frame();
  assert(running && position_loads == 4); /* the first, and one 20 frames on */
  test_follow = true;
  for (int i = 0; i < 25 && (cmd_there || running); i++)
    frame();
  out_has(" 1 ok place 7.000 2.000 7.000 facing ");
  assert(position_loads == 5 && placed[0] == 7);
  /* never there: an error after 120 frames */
  test_follow = false;
  test_was_placed = false;
  out_size = 0;
  out_file[0] = 0;
  send("place 9 2 9\n");
  for (int i = 0; i < POLL_CALLS + 130 && (cmd_there || running); i++)
    frame();
  out_has(" 1 err place: Link not there after 120 frames (at 1.50 2.00 -3.00)\n");

  /* place near: the given metres from the nearest actor of that name, on Link's side of it, facing it. Link is at
   * (1.5, -3): 1 m from Npc_Kokiri (1.5, -0.5) towards him is (1.5, -1.5), facing +z (0 degrees) */
  test_follow = true;
  run_script("place near Npc_Kokiri 1\n");
  out_has(" 1 ok place near Npc_Kokiri (1.50 2.00 -0.50): 1.500 2.000 -1.500 facing 0.0 after 1 frames\n");
  test_was_placed = false;
  run_script("place near Obj_Sign\n");
  out_has(" 1 ok place near Obj_Sign (5.75 2.00 -3.00): 4.550 2.000 -3.000 facing 90.0 after 1 frames\n");
  assert(placed[0] > 4.549 && placed[0] < 4.551 && placed[2] == -3 && placed[3] > 89.9 && placed[3] < 90.1);
  test_was_placed = false;
  test_follow = false;
  run_script("place near Nobody\nplace near\n");
  out_has(" 1 err place near: no Nobody within 60 m\n");
  out_has(" 2 err place near: an actor's name");

  /* status: the message window's state and message, and the running event */
  test_dialog = (dialog_t){ DLG_CHOICE, 4, 1, "Npc_Soldier:Talk_03" };
  test_event = "Npc_Soldier";
  run_script("status\n");
  out_has(" 1 ok screen=None wheel=closed dialog=choice:1 msg=Npc_Soldier:Talk_03 event=Npc_Soldier frame=");

  /* wait until: met at once, met later, never met (max), and wrong lines */
  run_script("wait until choice\nwait until actor Npc_Kokiri within 3\nwait until noevent max 5\necho after\n");
  out_has(" 1 ok until met after 0 frames\n");
  out_has(" 2 ok until met after 0 frames\n");
  out_has(" 3 err until: not met after 5 frames\n");
  out_has(" 4 ok after\n");
  out_size = 0;
  out_file[0] = 0;
  send("wait until control max 100\necho in control\n");
  for (int i = 0; i < POLL_CALLS + 3; i++)
    frame();
  assert(running && !strstr(out_file, "met"));
  test_dialog = (dialog_t){ DLG_CLOSED, 12, -1, "" };
  test_event = 0;
  for (int i = 0; i < 3; i++)
    frame();
  out_has(" 1 ok until met after ");
  out_has(" 2 ok in control\n");
  run_script("wait until\nwait until map\nwait until actor X within\nwait until dialog max 0\nwait untilx\n");
  out_has(" 1 err wait until: control, dialog");
  out_has(" 2 err wait until:");
  out_has(" 3 err wait until:");
  out_has(" 4 err wait until:");
  out_has(" 5 err wait: a number of frames, or until ...\n");
  /* control and map need the game in play: no Loading screen */
  test_screen = "Loading";
  run_script("wait until control max 3\nwait until map KokiriForest max 3\n");
  out_has(" 1 err until: not met after 3 frames\n");
  out_has(" 2 err until: not met after 3 frames\n");
  test_screen = "None";
  /* moved: Link somewhere else (over the distance, 1 m unless said) and in control */
  out_size = 0;
  out_file[0] = 0;
  send("wait until moved max 60\nwait until moved 5 max 3\nwait until moved x\n");
  for (int i = 0; i < POLL_CALLS + 3; i++)
    frame();
  assert(running && !strstr(out_file, "met")); /* not moved yet */
  test_dialog = (dialog_t){ DLG_WAITING, 3, -1, "Npc:Talk_1" }; /* moved, but a window is up: no control */
  test_moved_x = 2;
  for (int i = 0; i < 3; i++)
    frame();
  assert(!strstr(out_file, "met"));
  test_dialog = (dialog_t){ DLG_CLOSED, 12, -1, "" };
  for (int i = 0; i < 10 && (cmd_there || running); i++)
    frame();
  out_has(" 1 ok until met after ");
  out_has(" 2 err until: not met after 3 frames\n"); /* 2 m from where its wait began is fine: 0 from there */
  out_has(" 3 err wait until:");
  test_moved_x = 0;

  /* talk: A while the text types or waits, nothing while a page turns; each A here moves the window on: typing to
   * waiting, waiting to the next message (three, then a choice, or closed) */
  {
    static const char *const msgs[] = { "Npc:Talk_1", "Npc:Talk_2", "Npc:Talk_3" };
    for (int ending = 0; ending < 2; ending++) {
      int at = 0, presses = 0;
      bool was_down = false;
      test_dialog = (dialog_t){ DLG_TYPING, 2, -1, "" };
      snprintf(test_dialog.msg, sizeof(test_dialog.msg), "%s", msgs[0]);
      test_event = "Npc";
      out_size = 0;
      out_file[0] = 0;
      send("talk max 400\necho done\n");
      for (int i = 0; i < 400 && (cmd_there || running); i++) {
        frame();
        bool down = input_remote_active && (input_remote_buttons & 1u) != 0;
        if (down && !was_down) { /* a press */
          presses++;
          if (test_dialog.state == DLG_TYPING)
            test_dialog.state = DLG_WAITING;
          else if (test_dialog.state == DLG_WAITING && ++at < 3) {
            test_dialog = (dialog_t){ DLG_TYPING, 2, -1, "" };
            snprintf(test_dialog.msg, sizeof(test_dialog.msg), "%s", msgs[at]);
          } else if (test_dialog.state == DLG_WAITING && ending == 0) {
            test_dialog = (dialog_t){ DLG_CHOICE, 4, 0, "Npc:Talk_3" };
          } else if (test_dialog.state == DLG_WAITING) {
            test_dialog = (dialog_t){ DLG_CLOSED, 12, -1, "" };
            test_event = 0;
          }
        }
        was_down = down;
      }
      if (ending == 0)
        out_has(" 1 ok talk: choice at Npc:Talk_3, cursor 0, after ");
      else
        out_has(" 1 ok talk: over after ");
      out_has(" 3 messages");
      out_has(" 2 ok done\n");
      assert(presses == 6);
    }
    /* choose: Down until the cursor is on the option, then A; talk started on that choice doesn't stop at it */
    uint32_t down_mask = 0, a_mask = 0, bind;
    if (bind_parse("d-down", &bind))
      down_mask = bind_mask(bind) & 0xFFFF;
    if (bind_parse("A", &bind))
      a_mask = bind_mask(bind) & 0xFFFF;
    test_dialog = (dialog_t){ DLG_CHOICE, 4, 0, "Npc:Talk_3" };
    test_event = "Npc";
    out_size = 0;
    out_file[0] = 0;
    send("choose 2\ntalk max 300\nchoose 1\nchoose x\n");
    int downs = 0, after_a = -1;
    bool was = false;
    for (int i = 0; i < 400 && (cmd_there || running); i++) {
      frame();
      bool down = input_remote_active && input_remote_buttons != 0;
      if (down && !was) {
        if (input_remote_buttons == down_mask)
          test_dialog.cursor++, downs++;
        else if (input_remote_buttons == a_mask && test_dialog.state == DLG_CHOICE)
          after_a = 0; /* the window stays on the choice a few frames after A */
        else if (input_remote_buttons == a_mask && test_dialog.state == DLG_WAITING) {
          test_dialog = (dialog_t){ DLG_CLOSED, 12, -1, "" };
          test_event = 0;
        }
      }
      was = down;
      if (after_a >= 0 && ++after_a == 8)
        test_dialog = (dialog_t){ DLG_WAITING, 3, -1, "Npc:Talk_4" };
    }
    assert(downs == 2);
    out_has(" 1 ok choose: took option 2 after ");
    out_has(" 2 ok talk: over after ");
    out_has(" 3 err choose: no choice is up (dialog closed)\n");
    out_has(" 4 err choose: the option, 0 for the first\n");
    test_dialog = (dialog_t){ DLG_TURNING, 6, -1, "Npc:Talk_9" };
    test_event = "Npc";
    out_size = 0;
    out_file[0] = 0;
    send("talk max 20\ntalk max\n");
    for (int i = 0; i < 60 && (cmd_there || running); i++)
      frame();
    out_has(" 1 err talk: still going after 20 frames (dialog turning, event running), last Npc:Talk_9\n");
    out_has(" 2 err talk: max <frames>");
    test_dialog = (dialog_t){ DLG_CLOSED, 12, -1, "" };
    test_event = 0;
  }

  /* still: control and Link at rest for 8 frames in a row (his velocity is the actor's, at +0x388) */
  {
    float v[3] = { 0.5f, 0, 0 };
    memcpy(actor_mem + 0x388, v, sizeof(v));
    out_size = 0;
    out_file[0] = 0;
    send("wait until still max 60\necho rest\n");
    for (int i = 0; i < POLL_CALLS + 10; i++)
      frame();
    assert(running && !strstr(out_file, "met")); /* still sliding */
    v[0] = 0;
    memcpy(actor_mem + 0x388, v, sizeof(v));
    for (int i = 0; i < 6; i++)
      frame();
    assert(!strstr(out_file, "met")); /* at rest, but not 8 frames yet */
    for (int i = 0; i < 6 && (cmd_there || running); i++)
      frame();
    out_has(" 1 ok until met after ");
    out_has(" 2 ok rest\n");
  }

  /* value: a menu item's value (its number, or its name in brackets); repeat until: rounds until met */
  run_script("set cheats/rupees = 250\nwait until value cheats/rupees = 250 max 3\nwait until value cheats/rupees = 9 max 3\n"
             "wait until value cheats/rupees max 3\n");
  out_has(" 2 ok until met after 0 frames\n");
  out_has(" 3 err until: not met after 3 frames\n");
  out_has(" 4 err wait until: control");
  run_script("set cheats/speed = fast\nwait until value cheats/speed = fast max 2\nwait until value cheats/speed = Fast max 2\n");
  out_has(" 2 ok until met after 0 frames\n");
  out_has(" 3 ok until met after 0 frames\n");
  run_script("set cheats/rupees = 1\nrepeat until value cheats/rupees = 4 max 10\nset cheats/rupees = 2\nset cheats/rupees = 4\n"
             "end\necho after\nrepeat until value cheats/rupees = 7 max 3\necho round\nend\nrepeat until nonsense\n");
  out_has(" 5 ok until met after 1 rounds\n");
  out_has(" 6 ok after\n");
  out_has(" 9 err until: not met after 3 rounds\n");
  out_has(" 10 err repeat until: control");

  /* wheel: met once the item wheel is open (phase 2), not while it opens */
  test_wheel = 1;
  out_size = 0;
  out_file[0] = 0;
  send("wait until wheel max 20\nstatus\n");
  for (int i = 0; i < POLL_CALLS + 3; i++)
    frame();
  assert(running && !strstr(out_file, "met"));
  test_wheel = 2;
  for (int i = 0; i < 3 && (cmd_there || running); i++)
    frame();
  out_has(" 1 ok until met after ");
  out_has(" 2 ok screen=None wheel=open dialog=");
  test_wheel = 0;

  /* event: refused where the build's functions aren't known (the test build has none) */
  run_script("event Some_Event\nevent\n");
  out_has(" 1 err event: not known for this game version\n");
  out_has(" 2 err event: an event's name\n");
  /* event with a starter: wrong forms, and refused where the build's starter fields aren't known */
  run_script("event E actor\nevent E at 1 2\nevent E there\nevent E at 1 2 3\n");
  out_has(" 1 err event: actor <name>\n");
  out_has(" 2 err event: at <x> <y> <z> [facing]\n");
  out_has(" 3 err event: <name> [here | actor <name> | at <x> <y> <z> [facing]]\n");
  out_has(" 4 err event: a starter isn't known for this game version\n");

  /* flag: read a Bool, set it, by name or hash; no such field; bad hash or value */
  run_script("flag Market_A\nflag Market_A 1\nflag 0x39d7877d\nflag 0x39d7877d 0\nflag Other\nflag 0xZZ\n"
             "flag Market_A x\nflag\n");
  out_has(" 1 ok Market_A 0x39d7877d bool 0\n");
  out_has(" 2 ok Market_A 0x39d7877d bool 1\n");
  out_has(" 3 ok 0x39d7877d 0x39d7877d bool 1\n");
  out_has(" 4 ok 0x39d7877d 0x39d7877d bool 0\n");
  out_has(" 5 err flag: no Bool or Int field Other (0x00000001)\n");
  out_has(" 6 err flag: 0xZZ is not a hash\n");
  out_has(" 7 err flag: the value is a number\n");
  out_has(" 8 err flag: a field name");

  /* held by a hand (frame advance's buttons): a wait until just pauses, and goes on when the game does */
  out_size = 0;
  out_file[0] = 0;
  test_event = "Npc";
  send("wait until noevent max 100\necho after\n");
  for (int i = 0; i < POLL_CALLS + 3; i++)
    frame();
  for (int i = 0; i < 200; i++)
    remote_frame(true); /* more than its max: held frames don't count */
  assert(running && !strstr(out_file, " 1 "));
  test_event = 0;
  for (int i = 0; i < 3; i++)
    frame();
  out_has(" 1 ok until met after ");
  out_has(" 2 ok after\n");
  /* held by the script's own pause: nothing would let the game go, so it ends with an error */
  out_size = 0;
  out_file[0] = 0;
  test_event = "Npc";
  send("pause\nwait until noevent max 100\necho after\n");
  for (int i = 0; i < POLL_CALLS + 3 && !pause_wanted; i++)
    frame();
  assert(remote_take_pause());
  remote_frame(true);
  out_has(" 1 ok paused\n");
  out_has(" 2 err until: the script holds the game (step or run lets it go)\n");
  out_has(" 3 ok after\n");
  test_event = 0;
  frame(); /* the game runs again */

  /* menu: closed, or where the menu on screen is and the item under the cursor */
  menu_current = 0;
  run_script("menu\n");
  out_has(" 1 ok menu closed\n");
  m_cheats.selected = 2;
  menu_current = &m_cheats;
  run_script("menu\n");
  out_has(" 1 ok menu open path=[cheats] cursor=@2 kind=number editing=0 line=[rupees]\n");
  menu_current = 0;
  m_cheats.selected = 0;

  /* str: read, set, empty */
  run_script("str Sequence_CurrentBanc\nstr Sequence_CurrentBanc Market_Child\nstr X -\nstr X KokiriForest\n");
  out_has(" 1 ok Sequence_CurrentBanc 0x00000001 = \"KokiriForest\"\n");
  out_has(" 2 ok Sequence_CurrentBanc 0x00000001 = \"Market_Child\"\n");
  out_has(" 3 ok X 0x00000001 = \"\"\n");

  /* onerr stop: the first failing line ends the script (and what it waits for); onerr go: carries on */
  run_script("onerr stop\necho a\nwait until\necho b\n");
  out_has(" 1 ok onerr stop\n");
  out_has(" 3 err wait until:");
  out_has("end (onerr stop)\n");
  assert(!strstr(out_file, " ok b\n"));
  out_size = 0;
  out_file[0] = 0;
  send("onerr stop\nwait until noevent max 3\necho b\n");
  test_event = "Npc";
  for (int i = 0; i < POLL_CALLS + 10 && (cmd_there || running); i++)
    frame();
  out_has(" 2 err until: not met after 3 frames\n");
  out_has("end (onerr stop)\n");
  assert(!strstr(out_file, " ok b\n"));
  test_event = 0;
  run_script("wait until\necho c\nonerr x\n"); /* a new script starts with onerr go */
  out_has(" 2 ok c\n");
  out_has(" 3 err onerr: stop");

  /* title: refused at once with the reason; otherwise the script waits until Link is gone (ok) or it failed (err),
   * and goes on after it */
  title_problem = "already at the title screen";
  run_script("title\ntitle now\necho next\n");
  out_has(" 1 err title: already at the title screen\n");
  out_has(" 2 err title: nothing goes after it\n");
  out_has(" 3 ok next\n");
  assert(!title_starts);
  title_problem = 0;
  out_size = 0;
  out_file[0] = 0;
  send("title\necho after\n");
  for (int i = 0; i < POLL_CALLS + 3; i++)
    frame();
  assert(title_starts == 1 && running && !strstr(out_file, "title")); /* not there yet */
  title_now = TITLE_DONE;
  title_text = "at the title screen";
  frame();
  out_has(" 1 ok title: at the title screen\n");
  out_has(" 2 ok after\n");
  assert(!running);
  send("title\n");
  for (int i = 0; i < POLL_CALLS; i++)
    frame();
  title_now = TITLE_FAILED;
  title_text = "can't return to title screen: the game didn't leave play within a minute";
  frame();
  out_has(" 1 err title: can't return to title screen: the game didn't leave play within a minute\n");
  assert(title_starts == 2 && !running);
  run_script("pause\ntitle\nrun\n"); /* held: it starts all the same (frame advance lets the game go) */
  assert(remote_take_pause());
  remote_frame(true);
  assert(title_starts == 3 && !strstr(out_file, " 2 "));
  title_now = TITLE_DONE;
  title_text = "at the title screen";
  remote_frame(true);
  out_has(" 2 ok title: at the title screen\n");
  assert(remote_take_run());
  frame();
  out_has(" 3 ok running\n");
  assert(!running);

  /* repeat ... end: the lines between again and again, inside one another, with waits; a line can be run twice */
  run_script("repeat 3\necho a  b\nrepeat 2\necho c\nend\nend\necho d\nend\nrepeat x\n");
  int as = 0, cs = 0;
  for (const char *p = out_file; (p = strstr(p, " 2 ok a  b\n")); p++)
    as++;
  for (const char *p = out_file; (p = strstr(p, " 4 ok c\n")); p++)
    cs++;
  assert(as == 3 && cs == 6);
  out_has(" 7 ok d\n");
  out_has(" 8 err end: no repeat to end\n");
  out_has(" 9 err repeat: how many times\n");
  out_size = 0;
  send("repeat 3\ninput A for 1\nwait 2\nend\necho done\n");
  for (int i = 0; i < POLL_CALLS; i++)
    frame();
  int on = 0;
  for (int i = 0; i < 8 && running; i++) {
    on += input_remote_active;
    frame();
  }
  assert(on == 3 && !running); /* A on one frame of every two, three times */
  out_has(" 5 ok done\n");

  /* a file too long isn't run; one that can't be deleted isn't run, and is said once */
  out_size = 0;
  memset(cmd_file, 'x', SCRIPT_MAX);
  cmd_size = SCRIPT_MAX;
  cmd_there = true;
  for (int i = 0; i < 2 * POLL_CALLS; i++)
    frame();
  assert(!cmd_there && !running);
  out_has(" - 0 err cmd.txt is over 65535 bytes: not run\n");
  out_size = 0;
  cmd_locked = true;
  send("echo twice\n");
  for (int i = 0; i < 5 * POLL_CALLS; i++)
    frame();
  out_has("err cmd.txt can't be deleted: not run\n");
  assert(!strstr(strstr(out_file, "err") + 1, "err")); /* once */
  assert(!strstr(out_file, "twice") && !opened);
  cmd_locked = false;
  cmd_there = false; /* the sender takes it away: the next is run again */
  for (int i = 0; i < 2 * POLL_CALLS; i++)
    frame();

  /* a long script: at most LINES_PER_CALL lines a frame, all of them in the end */
  static char many[SCRIPT_MAX];
  size_t len = 0;
  for (int i = 0; i < 200; i++)
    len += (size_t)snprintf(many + len, sizeof(many) - len, "echo line %d\n", i);
  cmd_there = false;
  run_script(many);
  out_has(" 200 ok line 199\n");
  out_has(" - end\n");

  printf("remote: ok\n");
  return 0;
}
