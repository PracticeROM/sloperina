/* The menu tree: the native port of ootmenu.pyw's build_menus. Everything here runs on the
 * game's thread (from the frame hook), so it can read and write GameData directly; changes
 * that need the game's own code go through pouch.c's jobs. */
#include "menus.h"

#include "cheats.h"
#include "data.h"
#include "gamedata.h"
#include "airwall.h"
#include "enemyhp.h"
#include "flaglog.h"
#include "savebackup.h"
#include "memview.h"
#include "binds.h"
#include "osk.h"
#include "swkbd.h"
#include "build_info.h" /* made by the Makefile: BUILD_WHEN, BUILD_VERSION */
#include "age.h"
#include "gfx.h"
#include "gameimage.h"
#include "levitate.h"
#include "lock.h"
#include "position.h"
#include "savepos.h"
#include "voidmap.h"
#include "ragdoll.h"
#include "hsstore.h"
#include "eventskip.h"
#include "link.h"
#include "menu.h"
#include "input.h"
#include "job.h"
#include "pouch.h"
#include "lang.h"
#include "sys.h"
#include "divetest.h"
#include "warp.h"
#include "watches.h"
#include "z64hd.h"

#define ARRAY_LEN(array) ((int)(sizeof(array) / sizeof((array)[0])))

/* GameData fields (murmur3 hashes of their names) */
#define F_RUPEES     0xA77921D7u
#define F_WALLET_CAP 0xE0010A50u
#define F_MAGIC      0x76B18200u
#define F_MAGIC_CAP  0x12D63A97u

/* the most choices one of the item switches below can have ("none" and 15 items) */
#define MAX_CHOICES 16

menu_t menu_root;

/* --- small helpers -------------------------------------------------------------------------- */

static int clamp_int(int value, int low, int high)
{
  return value < low ? low : value > high ? high : value;
}

/* the furthest a list of `count` lines, `rows` at a time, can be scrolled */
static int max_top(int count, int rows)
{
  return count > rows ? count - rows : 0;
}

static bool strings_equal(const char *a, const char *b)
{
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

/* what follows `prefix` in `text`, or NULL if `text` doesn't start with it */
static const char *after_prefix(const char *text, const char *prefix)
{
  while (*prefix && *prefix == *text) {
    prefix++;
    text++;
  }
  return *prefix ? NULL : text;
}

/* the about page's build date, as the splash says it */
static void text_build_date(menu_item_t *it, char *buf, int len)
{
  (void)it;
  snprintf(buf, len, TF("Build Date: %s"), BUILD_WHEN);
}

/* an empty read-only line */
static void text_static(menu_item_t *it, char *buf, int len);
static void add_blank_line(menu_t *menu)
{
  menu_add_text(menu, text_static, (intptr_t) "");
}

/* --- field helpers -------------------------------------------------------------------------- */

/* A field of any of the simple types, as a number (a bool is 0 or 1). */
static bool field_get(field_t field, int32_t *value)
{
  bool flag;
  uint32_t choice;
  switch (field.type) {
    case FT_INT:
      return gd_get_int(field.hash, value);
    case FT_BOOL:
      if (!gd_get_bool(field.hash, &flag))
        return false;
      *value = flag;
      return true;
    case FT_ENUM:
      if (!gd_get_enum(field.hash, &choice))
        return false;
      *value = (int32_t)choice;
      return true;
    default:
      return false;
  }
}

static void field_set(field_t field, int32_t value)
{
  switch (field.type) {
    case FT_INT:
      gd_set_int(field.hash, value);
      break;
    case FT_BOOL:
      gd_set_bool(field.hash, value != 0);
      break;
    case FT_ENUM:
      gd_set_enum(field.hash, (uint32_t)value);
      break;
    default:
      break;
  }
}

/* sets an item's IsGet flag (0: it has none) */
static void mark_acquired(uint32_t is_get)
{
  if (is_get)
    gd_set_bool(is_get, true);
}

static int32_t int_or_unknown(uint32_t hash)
{
  int32_t value;
  return gd_get_int(hash, &value) ? value : NUM_UNKNOWN;
}

static uint32_t stock_hash(const char *item)
{
  char field_name[128];
  snprintf(field_name, sizeof(field_name), "PouchData.%s.StockNum", item);
  return murmur3(field_name);
}

static uint32_t is_get_hash(const char *item)
{
  char field_name[128];
  snprintf(field_name, sizeof(field_name), "IsGet.%s", item);
  return murmur3(field_name);
}

/* --- link ------------------------------------------------------------------------------------ */

static menu_t m_link;

static bool get_lock_x(menu_item_t *it) { return lock_is_set(LOCK_X); }
static bool get_lock_y(menu_item_t *it) { return lock_is_set(LOCK_Y); }
static bool get_lock_z(menu_item_t *it) { return lock_is_set(LOCK_Z); }
static void set_lock_axis(lock_axis_t axis, bool on)
{
  if (!lock_available()) {
    gfx_notice("locks need the 1.1.1 game");
    return;
  }
  lock_set(axis, on);
}
static void set_lock_x(menu_item_t *it, bool on) { set_lock_axis(LOCK_X, on); }
static void set_lock_y(menu_item_t *it, bool on) { set_lock_axis(LOCK_Y, on); }
static void set_lock_z(menu_item_t *it, bool on) { set_lock_axis(LOCK_Z, on); }
static bool get_ragdoll(menu_item_t *it) { return ragdoll_active(); }
static void set_ragdoll(menu_item_t *it, bool on) { ragdoll_set(on); }
static bool get_hs_force(menu_item_t *it) { return hsstore_force_enabled(); }
static void set_hs_force(menu_item_t *it, bool on) { hsstore_force_set(on); }
static bool get_hs_shown(menu_item_t *it) { return hsstore_shown; }
static void set_hs_shown(menu_item_t *it, bool on) { hsstore_shown = on; }

#define QUARTERS_PER_HEART 4
#define F_MAX_HEARTS 0xFBE01DA1u /* quarter hearts (game.py INT_HASH) */
#define F_LIFE       0x31AB5580u /* PlayerStatus.Life, quarter hearts */

static void hearts_text(int32_t quarters, char *buf, int len)
{
  static const char *const fractions[] = { "", ".25", ".5", ".75" };
  snprintf(buf, len, "%d%s", (int)(quarters / QUARTERS_PER_HEART), fractions[quarters & 3]);
}

static int32_t get_health(menu_item_t *it)
{
  int32_t *current, *maximum;
  return link_health(&current, &maximum) ? *current : NUM_UNKNOWN;
}

static int32_t max_health(menu_item_t *it)
{
  int32_t *current, *maximum;
  return link_health(&current, &maximum) ? *maximum : NUM_UNKNOWN;
}

static void set_health(menu_item_t *it, int32_t value)
{
  int32_t *current, *maximum;
  if (link_health(&current, &maximum))
    *current = value;
}

static void fill_health(menu_item_t *it)
{
  int32_t *current, *maximum;
  if (link_health(&current, &maximum)) {
    *current = *maximum;
    gfx_notice("health refilled");
  }
}

static void health_text(menu_item_t *it, char *buf, int len)
{
  hearts_text(get_health(it), buf, len);
}

/* Heart containers: Link's max health (his status object), the save fields (max, and current
 * health when it's above the new max), then the HUD redrawn. Held for a second in case the game
 * puts its own values back, which is logged (so we learn which copy it reads). */
#define K_CONTAINERS (0x20u << 24) /* the job's key */
#define MAX_HEART_CONTAINERS 20
#define CONTAINER_HOLD_FRAMES 30
#define CONTAINER_LOG_FRAMES 4 /* frames on which the game's values are logged if they differ from ours */
#define CONTAINER_HUD_FRAME 2  /* the frame (counting from Link found) the HUD is first redrawn */

static int pending_containers; /* shown while the job is queued */

static int32_t get_containers(menu_item_t *it)
{
  int32_t *current, *maximum;
  if (job_pending(K_CONTAINERS))
    return pending_containers;
  return link_health(&current, &maximum) ? *maximum / QUARTERS_PER_HEART : NUM_UNKNOWN;
}

static int32_t max_containers(menu_item_t *it)
{
  return MAX_HEART_CONTAINERS;
}

/* the object a health value is in: two words before it, four from it */
static void log_health_object(const char *what, const int32_t *value)
{
  const uint32_t *words = (const uint32_t *)((const uint8_t *)value - 8);
  log_printf("hearts: %s object %p: %08x %08x | %08x %08x %08x %08x", what, (void *)words, words[0], words[1],
             words[2], words[3], words[4], words[5]);
}

/* job->i counts the frames on which Link was found */
static bool job_containers(job_t *job)
{
  int32_t target = (int32_t)job->arg[0]; /* quarter hearts */
  int32_t *current, *maximum;
  int32_t field, life;
  if (!link_health(&current, &maximum)) {
    if (job->step == 0)
      log_printf("hearts: Link's health not found; will keep trying for a second");
    return job->step > CONTAINER_HOLD_FRAMES;
  }
  bool have_field = gd_get_int(F_MAX_HEARTS, &field);
  bool have_life = gd_get_int(F_LIFE, &life);
  if (job->i == 0) { /* the first frame Link is found */
    log_health_object("current", current);
    log_health_object("max", maximum);
    log_printf("hearts: containers %d -> %d (save field %d), health %d (save field %d)",
               (int)(*maximum / QUARTERS_PER_HEART), (int)(target / QUARTERS_PER_HEART), have_field ? (int)field : -1,
               (int)*current, have_life ? (int)life : -1);
  } else if ((*maximum != target || *current > target || (have_field && field != target) ||
              (have_life && life > target)) &&
             job->i < CONTAINER_LOG_FRAMES) {
    log_printf("hearts: frame %d: the game has max %d, field %d, health %d, field %d (we set %d)", job->step,
               (int)*maximum, have_field ? (int)field : -1, (int)*current, have_life ? (int)life : -1, (int)target);
  }
  job->i++;
  /* written directly (value and its copy), not through the game's SetInt queue: queued sets
   * while scrolling left an earlier value behind */
  *maximum = target;
  if (*current > target)
    *current = target;
  if (!have_field || field != target)
    gd_poke_int(F_MAX_HEARTS, target);
  if (have_life && life > target)
    gd_poke_int(F_LIFE, target);
  if (job->i == CONTAINER_HUD_FRAME || job->step == CONTAINER_HOLD_FRAMES)
    hud_refresh_now();
  return job->step >= CONTAINER_HOLD_FRAMES;
}

static void set_containers(menu_item_t *it, int32_t containers)
{
  if (containers < 1)
    containers = 1;
  pending_containers = containers;
  job_submit(K_CONTAINERS, job_containers, containers * QUARTERS_PER_HEART, 0, 0, 0);
}

static int32_t get_rupees(menu_item_t *it)
{
  return int_or_unknown(F_RUPEES);
}

static int32_t max_rupees(menu_item_t *it)
{
  return int_or_unknown(F_WALLET_CAP);
}

static void set_rupees(menu_item_t *it, int32_t value)
{
  gd_set_int(F_RUPEES, value);
}

static void fill_rupees(menu_item_t *it)
{
  int32_t wallet_cap = max_rupees(it);
  if (wallet_cap != NUM_UNKNOWN) {
    gd_set_int(F_RUPEES, wallet_cap);
    gfx_notice("wallet filled");
  }
}

static const char *const age_names[] = { "child", "adult" };

static int get_age(menu_item_t *it)
{
  return age_wanted();
}

static void set_age(menu_item_t *it, int age)
{
  age_want(age);
}

static void act_end_cutscene(menu_item_t *it)
{
  if (eventskip_start())
    menu_close(); /* so the skip can be watched */
}

static void act_reload_map(menu_item_t *it)
{
  gfx_logf("reload map (%s)", age_wanted() == AGE_ADULT ? "adult" : "child");
  age_reload_map();
  menu_close(); /* so the reload can be watched */
}

/* read-only lines; it->data is the text */
static void text_static(menu_item_t *it, char *buf, int len)
{
  snprintf(buf, len, "%s", (const char *)it->data);
}

/* it->data: 0 x, 1 y, 2 z */
static void text_axis(menu_item_t *it, char *buf, int len)
{
  static const char axis_names[] = "xyz";
  vec3f_t pos;
  if (link_drawn_pos(&pos)) {
    float value = it->data == 0 ? pos.x : it->data == 1 ? pos.y : pos.z;
    snprintf(buf, len, "%c: %.2f", axis_names[it->data], (double)value);
  } else {
    snprintf(buf, len, "%c: -", axis_names[it->data]);
  }
}

static void text_age(menu_item_t *it, char *buf, int len)
{
  int age = age_current();
  snprintf(buf, len, TF("current age: %s"), T(age == AGE_ADULT ? "adult" : age == AGE_CHILD ? "child" : "-"));
}

static void text_hs_storage(menu_item_t *it, char *buf, int len)
{
  int have = hsstore_have();
  snprintf(buf, len, TF("hookshot storage: %s"), T(have > 0 ? "yes" : have == 0 ? "no" : "-"));
}

/* the map as the warps menu names it, with the age */
static void text_map(menu_item_t *it, char *buf, int len)
{
  const char *map_name = gd_get_str(GD_STR_MAP);
  if (!map_name || !map_name[0]) {
    snprintf(buf, len, TF("map: -"));
    return;
  }
  for (int i = 0; i < data_n_warp_maps; i++) {
    const warp_map_t *warp_map = &data_warp_maps[i];
    if (strings_equal(warp_map->name, map_name)) {
      snprintf(buf, len, TF("map: %s%s"), T(warp_map->label),
               warp_map->category == WC_CHILD ? " (child)" : warp_map->category == WC_ADULT ? " (adult)" : "");
      return;
    }
  }
  snprintf(buf, len, TF("map: %s"), map_name);
}

static void act_save_position(menu_item_t *it)
{
  position_save();
}

static void act_load_position(menu_item_t *it)
{
  position_load();
}

static void build_link(void)
{
  menu_init(&m_link, "link", 20);
  /* what can be changed first */
  menu_item_t *health = menu_add_number(&m_link, "health", get_health, set_health, max_health, fill_health, 0);
  if (health) {
    health->text = health_text;
    health->hint = "d-left/right: change by a quarter heart   L: fill";
  }
  menu_item_t *containers =
    menu_add_number(&m_link, "heart containers", get_containers, set_containers, max_containers, 0, 0);
  if (containers)
    containers->num_min = 1;
  menu_add_number(&m_link, "rupees", get_rupees, set_rupees, max_rupees, fill_rupees, 0);
  menu_item_t *age = menu_add_option(&m_link, "age", ARRAY_LEN(age_names), age_names, get_age, set_age, 0);
  if (age)
    age->hint = "takes effect when the map reloads (reload map, below)";
  menu_item_t *reload = menu_add_action(&m_link, "reload map", act_reload_map, 0);
  if (reload)
    reload->hint = "L: reload this map at the age above";
  menu_item_t *save = menu_add_action(&m_link, "save position ({savepos})", act_save_position, 0);
  if (save) {
    save->hint = "L: remember where Link is and which way he faces";
    save->log_label = "save position";
  }
  menu_item_t *load = menu_add_action(&m_link, "load position ({loadpos})", act_load_position, 0);
  if (load) {
    load->hint = "L: put Link back there (same map only)";
    load->log_label = "load position";
  }
  menu_item_t *lock_x = menu_add_check(&m_link, "lock X", get_lock_x, set_lock_x, 0);
  if (lock_x)
    lock_x->hint = "Hold Link's X (map North/South) where he is; off after a map change";
  menu_item_t *lock_y = menu_add_check(&m_link, "lock Y", get_lock_y, set_lock_y, 0);
  if (lock_y)
    lock_y->hint = "Hold Link's Y (height) where he is; off after a map change";
  menu_item_t *lock_z = menu_add_check(&m_link, "lock Z", get_lock_z, set_lock_z, 0);
  if (lock_z)
    lock_z->hint = "Hold Link's Z (map East/West) where he is; off after a map change";
  menu_item_t *ragdoll = menu_add_check(&m_link, "ragdoll ({ragdoll})", get_ragdoll, set_ragdoll, 0);
  if (ragdoll) {
    ragdoll->hint = "knock Link over, as a bomb does; he gets up by himself";
    ragdoll->log_label = "ragdoll:";
  }
  menu_item_t *hs_force = menu_add_check(&m_link, "force hookshot storage", get_hs_force, set_hs_force, 0);
  if (hs_force)
    hs_force->hint = "a bomb's medium hit the moment a hookshot shot starts (1.0)";
  menu_item_t *hs_shown = menu_add_check(&m_link, "show hookshot hook", get_hs_shown, set_hs_shown, 0);
  if (hs_shown)
    hs_shown->hint = "gold box: the hook out; grey cross: where a hook was put away; red: the shot's aim";
  menu_add_return(&m_link);
  /* what can only be read, under return after a blank line */
  add_blank_line(&m_link);
  menu_add_heading(&m_link, "position");
  for (int axis = 0; axis < 3; axis++)
    menu_add_text(&m_link, text_axis, axis);
  menu_end_group(&m_link);
  menu_add_text(&m_link, text_age, 0);
  menu_add_text(&m_link, text_map, 0);
  menu_add_text(&m_link, text_hs_storage, 0);
}

/* --- warps ------------------------------------------------------------------------------------- */

#define MAX_WARP_MAPS 96
static menu_t m_warps, m_places;
static menu_t m_warp_category[WC_COUNT];
static menu_t m_warp_map[MAX_WARP_MAPS];

static void text_warp_current(menu_item_t *it, char *buf, int len)
{
  const char *map_name = gd_get_str(GD_STR_MAP);
  snprintf(buf, len, TF("current  %s"), map_name ? map_name : "-");
}

static void text_warp_state(menu_item_t *it, char *buf, int len)
{
  snprintf(buf, len, TF("warp     %s"), warp_state());
}

/* it->data: the spawn (data_spawns), it->data2: its map (data_warp_maps) */
static void act_warp(menu_item_t *it)
{
  gfx_logf("warp to %s", data_warp_maps[it->data2].label);
  warp_arm(&data_warp_maps[it->data2], &data_spawns[it->data]);
  menu_close();
}

static void act_cancel_warp(menu_item_t *it)
{
  warp_cancel();
}

static bool get_arrival(menu_item_t *it)
{
  return warp_arrival_cutscenes;
}

static void set_arrival(menu_item_t *it, bool on)
{
  warp_arrival_cutscenes = on;
}

/* places > a category (child, adult, dungeons ...) > a map > its spawns */
static void build_places(void)
{
  menu_init(&m_places, "places", WC_COUNT + 1);
  int maps_in_category[WC_COUNT] = { 0 };
  for (int i = 0; i < data_n_warp_maps; i++)
    maps_in_category[data_warp_maps[i].category]++;
  for (int cat = 0; cat < WC_COUNT; cat++) {
    menu_init(&m_warp_category[cat], data_warp_categories[cat], maps_in_category[cat] + 1);
    if (maps_in_category[cat])
      menu_add_submenu(&m_places, data_warp_categories[cat], &m_warp_category[cat]);
  }
  menu_add_return(&m_places);
  for (int i = 0; i < data_n_warp_maps && i < MAX_WARP_MAPS; i++) {
    const warp_map_t *warp_map = &data_warp_maps[i];
    menu_init(&m_warp_map[i], warp_map->label, warp_map->n + 1);
    for (int spawn = warp_map->first; spawn < warp_map->first + warp_map->n; spawn++) {
      menu_item_t *item = menu_add_action(&m_warp_map[i], data_spawns[spawn].label, act_warp, spawn);
      if (item)
        item->data2 = i;
    }
    menu_add_return(&m_warp_map[i]);
    menu_add_submenu(&m_warp_category[warp_map->category], warp_map->label, &m_warp_map[i]);
  }
  for (int cat = 0; cat < WC_COUNT; cat++)
    menu_add_return(&m_warp_category[cat]);
}

static void build_warps(void)
{
  menu_init(&m_warps, "warps", 7);
  menu_add_submenu(&m_warps, "places", &m_places);
  menu_add_action(&m_warps, "cancel warp", act_cancel_warp, 0);
  menu_add_check(&m_warps, "arrival cutscenes", get_arrival, set_arrival, 0);
  menu_add_return(&m_warps);
  /* what can only be read, under return after a blank line */
  add_blank_line(&m_warps);
  menu_add_text(&m_warps, text_warp_current, 0);
  menu_add_text(&m_warps, text_warp_state, 0);

  build_places();
}

/* --- cheats ------------------------------------------------------------------------------------ */

static menu_t m_cheats;

static bool get_cheat(menu_item_t *it)
{
  return cheats[it->data];
}

static void set_cheat(menu_item_t *it, bool on)
{
  cheats[it->data] = on;
}

/* --- the game's pictures of the items, before their labels in the item menus (the game's style) ------- */

static const struct { const char *label; uint8_t picture; } item_pictures[] = {
  { "energy", GAMEPIC_HEART }, { "rupees", GAMEPIC_RUPEE }, { "magic", GAMEPIC_POTION_GREEN },
  { "magic meter", GAMEPIC_MAGIC }, { "deku sticks", GAMEPIC_STICK }, { "deku nuts", GAMEPIC_NUT },
  { "deku seeds", GAMEPIC_SEED }, { "bombs", GAMEPIC_BOMB }, { "arrows", GAMEPIC_ARROW },
  { "bombchus", GAMEPIC_BOMBCHU }, { "magic beans", GAMEPIC_BEAN }, { "boomerang", GAMEPIC_BOOMERANG },
  { "fairy slingshot", GAMEPIC_SLINGSHOT }, { "fairy bow", GAMEPIC_BOW }, { "megaton hammer", GAMEPIC_HAMMER },
  { "fire arrow", GAMEPIC_ARROW_FIRE }, { "ice arrow", GAMEPIC_ARROW_ICE }, { "light arrow", GAMEPIC_ARROW_LIGHT },
  { "hookshot", GAMEPIC_HOOKSHOT }, { "din's fire", GAMEPIC_DIN }, { "farore's wind", GAMEPIC_FARORE },
  { "nayru's love", GAMEPIC_NAYRU }, { "ocarina", GAMEPIC_OCARINA_TIME }, { "fairy ocarina", GAMEPIC_OCARINA_FAIRY },
  { "ocarina of time", GAMEPIC_OCARINA_TIME }, { "bottle a", GAMEPIC_BOTTLE }, { "bottle b", GAMEPIC_BOTTLE },
  { "bottle c", GAMEPIC_BOTTLE }, { "bottle d", GAMEPIC_BOTTLE }, { "bottle e", GAMEPIC_BOTTLE },
  { "kokiri sword", GAMEPIC_SWORD_KOKIRI }, { "master sword", GAMEPIC_SWORD_MASTER },
  { "big sword", GAMEPIC_SWORD_GIANT }, { "deku shield", GAMEPIC_SHIELD_DEKU },
  { "hylian shield", GAMEPIC_SHIELD_HYLIAN }, { "mirror shield", GAMEPIC_SHIELD_MIRROR },
  { "goron tunic", GAMEPIC_TUNIC_GORON }, { "zora tunic", GAMEPIC_TUNIC_ZORA }, { "iron boots", GAMEPIC_BOOTS_IRON },
  { "hover boots", GAMEPIC_BOOTS_HOVER }, { "lenses of truth", GAMEPIC_LENS }, { "echo gem", GAMEPIC_AGONY },
  { "gerudo card", GAMEPIC_GERUDO_CARD }, { "double defense", GAMEPIC_DEFENSE },
  { "goron's bracelet", GAMEPIC_BRACELET }, { "gauntlets", GAMEPIC_GAUNTLETS }, { "scale", GAMEPIC_SCALE },
  { "skulltulas", GAMEPIC_SKULLTULA }, { "deku stick bag", GAMEPIC_STICK_BAG }, { "deku nut bag", GAMEPIC_NUT_BAG },
  { "deku seed bag", GAMEPIC_BULLET_BAG }, { "bomb bag", GAMEPIC_BOMB_BAG }, { "quiver", GAMEPIC_QUIVER },
  { "wallet", GAMEPIC_WALLET }, { "business scrub seed", GAMEPIC_SCRUB_SEED },
  { "business scrub seeds", GAMEPIC_SCRUB_SEED }, { "mask slot", GAMEPIC_MASK_KEATON },
  { "amiibo mask", GAMEPIC_MASK_FAIRY_KID }, { "child trade item", GAMEPIC_WEIRD_EGG },
  { "adult trade item", GAMEPIC_POCKET_EGG },
};

static bool same_text(const char *a, const char *b)
{
  while (*a && *a == *b)
    a++, b++;
  return *a == *b;
}

/* the options whose picture is the chosen item's (as many as their choices; "none" shows the first, or
 * nothing where it's 0: its room kept) */
static const uint8_t ocarina_pictures[] = { GAMEPIC_OCARINA_FAIRY, GAMEPIC_OCARINA_FAIRY, GAMEPIC_OCARINA_TIME };
static const uint8_t hookshot_pictures[] = { GAMEPIC_HOOKSHOT, GAMEPIC_HOOKSHOT, GAMEPIC_LONGSHOT };
static const uint8_t big_sword_pictures[] = { GAMEPIC_SWORD_GIANT, GAMEPIC_SWORD_GIANT, GAMEPIC_SWORD_BROKEN,
                                              GAMEPIC_SWORD_BIGGORON };
static const uint8_t gauntlet_pictures[] = { GAMEPIC_GAUNTLETS, GAMEPIC_GAUNTLETS, GAMEPIC_GAUNTLETS_GOLD };
static const uint8_t scale_pictures[] = { GAMEPIC_SCALE, GAMEPIC_SCALE, GAMEPIC_SCALE_GOLD };
static const uint8_t magic_pictures[] = { GAMEPIC_MAGIC, GAMEPIC_MAGIC, GAMEPIC_MAGIC_DOUBLE };
/* data_masks' and data_amiibo_masks' order (gendata.py) */
static const uint8_t mask_pictures[] = { 0, GAMEPIC_MASK_KEATON, GAMEPIC_MASK_SKULL, GAMEPIC_MASK_SPOOKY,
                                         GAMEPIC_MASK_BUNNY, GAMEPIC_MASK_GORON, GAMEPIC_MASK_ZORA,
                                         GAMEPIC_MASK_GERUDO, GAMEPIC_MASK_TRUTH };
static const uint8_t amiibo_mask_pictures[] = { 0, GAMEPIC_MASK_FAIRY_KID, GAMEPIC_MASK_PRINCESS };
static const struct { const char *label; const uint8_t *pictures; int n; } choice_pictures[] = {
  { "ocarina", ocarina_pictures, ARRAY_LEN(ocarina_pictures) },
  { "hookshot", hookshot_pictures, ARRAY_LEN(hookshot_pictures) },
  { "big sword", big_sword_pictures, ARRAY_LEN(big_sword_pictures) },
  { "gauntlets", gauntlet_pictures, ARRAY_LEN(gauntlet_pictures) },
  { "scale", scale_pictures, ARRAY_LEN(scale_pictures) },
  { "magic meter", magic_pictures, ARRAY_LEN(magic_pictures) },
  { "mask slot", mask_pictures, ARRAY_LEN(mask_pictures) },
  { "amiibo mask", amiibo_mask_pictures, ARRAY_LEN(amiibo_mask_pictures) },
};

/* the items of an item menu: their pictures, by their labels (an option's by its choice, when it has a
 * table of them as long as its choices); in a menu with checkboxes, the others' two cells in, so all its
 * pictures are in a column after the checkboxes */
/* the game's pictures of the menu's items (their labels line up round them: menu.c label_pad) */
static void add_pictures(menu_t *m)
{
  for (int i = 0; i < m->n_items; i++) {
    for (unsigned k = 0; k < ARRAY_LEN(item_pictures); k++) {
      if (m->items[i].label && same_text(m->items[i].label, item_pictures[k].label)) {
        m->items[i].picture = item_pictures[k].picture;
        break;
      }
    }
    for (unsigned k = 0; k < ARRAY_LEN(choice_pictures); k++)
      if (m->items[i].picture && m->items[i].kind == MI_OPTION && m->items[i].n_choices == choice_pictures[k].n &&
          same_text(m->items[i].label, choice_pictures[k].label))
        m->items[i].choice_pictures = choice_pictures[k].pictures;
  }
}

static void build_cheats(void)
{
  menu_init(&m_cheats, "cheats", CHEAT_COUNT + 3);
  menu_add_heading(&m_cheats, "infinite");
  for (int i = 0; i < CHEAT_COUNT; i++) {
    /* cheat_t's order (append only), with the scrub seeds after the beans */
    int cheat = i <= CHEAT_BEANS ? i : i == CHEAT_BEANS + 1 ? CHEAT_SCRUB_SEEDS : i - 1;
    if (cheat == CHEAT_SPEED)
      menu_add_heading(&m_cheats, "player");
    menu_add_check(&m_cheats, cheat_names[cheat], get_cheat, set_cheat, cheat);
  }
  menu_add_return(&m_cheats);
  add_pictures(&m_cheats);
}

/* --- inventory: owned items and gear ------------------------------------------------------------- */

/* it->data: the inv_item_t */
static bool get_owned(menu_item_t *it)
{
  const inv_item_t *item = (const inv_item_t *)it->data;
  int32_t owned;
  return field_get(item->owned, &owned) && owned > 0;
}

/* Ticked: owned + acquired (the item screen needs both). Unticked: also taken off Link if he
 * wears it, and off the item buttons (FINDINGS §5.5, §5.6). */
static void set_owned(menu_item_t *it, bool on)
{
  const inv_item_t *item = (const inv_item_t *)it->data;
  field_set(item->owned, on ? 1 : 0);
  if (on) {
    mark_acquired(item->acquired);
    if (item->button)
      pouch_clear_buttons_cancel(item->button);
  } else {
    if (item->worn_slot)
      pouch_unequip(item->worn_slot, item->worn_item);
    if (item->button)
      pouch_clear_buttons(item->button);
  }
}

/* The business scrub seed (the item screen's slot after the bottles, PouchTable's ItemArray): one in the pouch,
 * StockNum 1 and IsGet, as a trade item */
#define SCRUB_SEED "Item_ScrubBusinessSeed"

static bool get_scrub_seed(menu_item_t *it)
{
  int32_t stock;
  return gd_get_int(stock_hash(SCRUB_SEED), &stock) && stock > 0;
}

static void set_scrub_seed(menu_item_t *it, bool on)
{
  gd_set_int(stock_hash(SCRUB_SEED), on ? 1 : 0);
  gd_set_bool(is_get_hash(SCRUB_SEED), on);
}

static bool get_acquired(menu_item_t *it)
{
  bool acquired;
  return gd_get_bool(((const inv_item_t *)it->data)->acquired, &acquired) && acquired;
}

static void set_acquired(menu_item_t *it, bool on)
{
  gd_set_bool(((const inv_item_t *)it->data)->acquired, on);
}

/* The four item buttons: Enum fields whose names weren't recovered (FINDINGS §1, "Item buttons"). */
static const uint32_t item_button_fields[] = { 0x6BEE602D, 0x76F85E6D, 0xA0029837, 0xBC32CE24 };

/* Every item button holding one of `old_items` (their button names' hashes) gets `replacement`.
 * True if any did. */
static bool replace_on_item_buttons(const uint32_t *old_items, int n_old, uint32_t replacement)
{
  bool changed = false;
  for (int slot = 0; slot < ARRAY_LEN(item_button_fields); slot++) {
    uint32_t held;
    for (int k = 0; k < n_old; k++)
      if (gd_get_enum(item_button_fields[slot], &held) && held == old_items[k]) {
        gd_set_enum(item_button_fields[slot], replacement);
        changed = true;
      }
  }
  return changed;
}

/* Trade slots and masks: one item at a time; trading switches an item button holding the old one.
 * it->data: the slot's items (pouch_one_t), it->data2: how many. Choice 0 is "none". */
static const char *trade_names[2][MAX_CHOICES]; /* [0] child, [1] adult */
/* ... each choice's picture (choice_pictures; "none": nothing, its room kept), by the item's name */
static uint8_t trade_pictures[2][MAX_CHOICES];
static const struct { const char *item; uint8_t picture; } trade_item_pictures[] = {
  { "Item_WeirdEgg", GAMEPIC_WEIRD_EGG }, { "Item_AlarmCucco", GAMEPIC_CUCCO },
  { "Item_ZeldaLetter", GAMEPIC_ZELDA_LETTER }, { "Item_PocketEgg", GAMEPIC_POCKET_EGG },
  { "Item_PocketCucco", GAMEPIC_POCKET_CUCCO }, { "Item_CojiroCucco", GAMEPIC_COJIRO },
  { "Item_OddMushroom", GAMEPIC_ODD_MUSHROOM }, { "Item_OddPowder", GAMEPIC_ODD_POTION },
  { "Item_PoacherSaw", GAMEPIC_POACHER_SAW }, { "Item_BrokenSword", GAMEPIC_BROKEN_SWORD },
  { "Item_Prescription", GAMEPIC_PRESCRIPTION }, { "Item_EyeballFrog", GAMEPIC_EYEBALL_FROG },
  { "Item_EyeDrop", GAMEPIC_EYE_DROPS }, { "Item_ClaimCheck", GAMEPIC_CLAIM_CHECK },
};
static const char *mask_names[MAX_CHOICES], *amiibo_mask_names[MAX_CHOICES];

static int get_trade(menu_item_t *it)
{
  const pouch_one_t *slot_items = (const pouch_one_t *)it->data;
  int n_items = (int)it->data2;
  for (int i = 0; i < n_items; i++) {
    int32_t stock;
    if (gd_get_int(stock_hash(slot_items[i].item), &stock) && stock > 0)
      return i + 1;
  }
  return 0;
}

static void set_trade(menu_item_t *it, int choice)
{
  const pouch_one_t *slot_items = (const pouch_one_t *)it->data;
  int n_items = (int)it->data2;
  uint32_t old_buttons[MAX_CHOICES];
  int n_old = 0;
  for (int i = 0; i < n_items; i++) {
    int32_t stock;
    if (i + 1 != choice && gd_get_int(stock_hash(slot_items[i].item), &stock) && stock > 0) {
      gd_set_int(stock_hash(slot_items[i].item), 0);
      gd_set_bool(is_get_hash(slot_items[i].item), false);
      if (slot_items[i].button)
        old_buttons[n_old++] = murmur3(slot_items[i].button);
    }
  }
  const pouch_one_t *chosen = choice > 0 ? &slot_items[choice - 1] : 0;
  if (chosen) {
    gd_set_int(stock_hash(chosen->item), 1);
    gd_set_bool(is_get_hash(chosen->item), true);
  }
  uint32_t replacement = murmur3(chosen && chosen->button ? chosen->button : "Empty");
  if (n_old && replace_on_item_buttons(old_buttons, n_old, replacement))
    pouch_refresh_screen(z64hd_game->vt_hud);
}

/* Owned items that are one choice (hookshot / longshot, the big swords): it->data = the group,
 * it->data2 = its size. One that goes is also taken off Link if he wears it. */
static int get_owned_group(menu_item_t *it)
{
  const inv_item_t *const *group = (const inv_item_t *const *)it->data;
  for (int i = (int)it->data2 - 1; i >= 0; i--) { /* the best one held */
    int32_t owned;
    if (field_get(group[i]->owned, &owned) && owned > 0)
      return i + 1;
  }
  return 0;
}

static void set_owned_group(menu_item_t *it, int choice)
{
  const inv_item_t *const *group = (const inv_item_t *const *)it->data;
  int n_group = (int)it->data2;
  const inv_item_t *chosen = choice > 0 && choice <= n_group ? group[choice - 1] : 0;
  uint32_t replacement = murmur3(chosen && chosen->button ? chosen->button : "Empty");
  bool changed = false;
  for (int i = 0; i < n_group; i++) {
    const inv_item_t *item = group[i];
    int32_t owned;
    bool had = field_get(item->owned, &owned) && owned > 0;
    field_set(item->owned, item == chosen);
    if (item == chosen) {
      mark_acquired(item->acquired);
      if (item->button)
        pouch_clear_buttons_cancel(item->button);
      continue;
    }
    if (had && item->worn_slot)
      pouch_unequip(item->worn_slot, item->worn_item);
    if (had && item->button) {
      /* an item button holding the old one gets the new one (or nothing), as with trade items */
      pouch_clear_buttons_cancel(item->button);
      uint32_t old_button = murmur3(item->button);
      if (replace_on_item_buttons(&old_button, 1, replacement))
        changed = true;
    }
  }
  if (changed)
    pouch_refresh_screen(z64hd_game->vt_hud);
}

static const inv_item_t *hookshots[2];
static const char *const hookshot_names[] = { "none", "hookshot", "longshot" };
static const inv_item_t *big_swords[3];
static const char *const big_sword_names[] = { "none", "giant's knife", "broken giant's knife", "biggoron's sword" };

/* The Stone of Agony is a pouch item here, Item_AgonyStone, worn in the head row of the equipment
 * screen beside the Lens of Truth (PouchTable: Pa_HeadEquip_00); it detects Gold Skulltulas. Held
 * like the lens: its pouch stock and IsGet. (The "Stone of Agony equipped" flag did nothing.) */
static const inv_item_t agony_stone = {
  "echo gem", IK_OWNED, { 0xF3B14CE2u /* PouchData.Item_AgonyStone.StockNum */, FT_INT },
  0xDD587ED8u /* IsGet.Item_AgonyStone */, NULL, -1, NULL,
};

#define N_OCARINA_CHOICES 3 /* ocarina_names: none, fairy ocarina, ocarina of time */

static int get_ocarina(menu_item_t *it)
{
  return pouch_ocarina();
}

static void set_ocarina(menu_item_t *it, int choice)
{
  /* the D-pad (where the ocarina goes) only exists once the Navi tutorial has been seen */
  if (choice > 0)
    gd_set_bool(data_action_guide_navi, true);
  pouch_set_ocarina(choice);
}

/* --- inventory: amounts ------------------------------------------------------------------------- */

/* it->data: the ammo_t */
static int32_t get_ammo(menu_item_t *it)
{
  return int_or_unknown(((const ammo_t *)it->data)->count);
}

static int32_t max_ammo(menu_item_t *it)
{
  const ammo_t *ammo = (const ammo_t *)it->data;
  return ammo->cap ? int_or_unknown(ammo->cap) : ammo->fixed;
}

static void set_ammo(menu_item_t *it, int32_t value)
{
  const ammo_t *ammo = (const ammo_t *)it->data;
  gd_set_int(ammo->count, value);
  if (value > 0)
    mark_acquired(ammo->acquired);
}

static void fill_ammo(menu_item_t *it)
{
  int32_t cap = max_ammo(it);
  if (cap != NUM_UNKNOWN)
    set_ammo(it, cap);
}

/* it->data: the number_t; it->data2: the maximum, for max_from_data2 */
static int32_t get_number(menu_item_t *it)
{
  return int_or_unknown(((const number_t *)it->data)->hash);
}

static void set_number(menu_item_t *it, int32_t value)
{
  gd_set_int(((const number_t *)it->data)->hash, value);
}

/* The tokens' count, and (any at all) that they were got: without IsGet the collection screen shows the slot
 * empty, whatever the count (1.0.0) */
static void set_skulltulas(menu_item_t *it, int32_t value)
{
  set_number(it, value);
  if (value > 0)
    mark_acquired(is_get_hash("Item_Token_Gold"));
}

static int32_t max_from_data2(menu_item_t *it)
{
  return (int32_t)it->data2;
}

static int32_t get_magic(menu_item_t *it)
{
  return int_or_unknown(F_MAGIC);
}

static int32_t max_magic(menu_item_t *it)
{
  return int_or_unknown(F_MAGIC_CAP);
}

static void set_magic(menu_item_t *it, int32_t value)
{
  magic_set(value);
}

static void fill_magic(menu_item_t *it)
{
  int32_t cap = max_magic(it);
  if (cap != NUM_UNKNOWN && cap > 0)
    magic_set(cap);
}

/* --- inventory: upgrades, bottles, flags ---------------------------------------------------------- */

/* the level last asked for if still queued, else the pouch's */
static int upgrade_now(upgrade_t upgrade)
{
  int pending = upgrade_pending_level(upgrade);
  return pending >= 0 ? pending : upgrade_level(upgrade);
}

/* it->data: the upgrade_t */
static int get_upgrade(menu_item_t *it)
{
  return upgrade_now((upgrade_t)it->data);
}

/* Link/Abilities/Strength (None, Low, Middle, High): the best of the bracelet and the gauntlets */
static void write_strength(int bracelet, int gauntlets)
{
  int strength = gauntlets > 0 ? 1 + gauntlets : bracelet > 0 ? 1 : 0;
  gd_set_enum(data_strength.hash, data_strength_values[strength]);
}

static void set_upgrade(menu_item_t *it, int level)
{
  upgrade_t upgrade = (upgrade_t)it->data;
  upgrade_set(upgrade, level);
  /* the ability fields game.py also wrote (level < 3: within data.c's tables) */
  if (upgrade == UPG_GAUNTLETS && level < 3)
    write_strength(upgrade_now(UPG_BRACELET), level);
  else if (upgrade == UPG_SCALE && level < 3)
    gd_set_float(data_diving_depth.hash, data_scale_depths[level]);
}

static bool get_bracelet(menu_item_t *it)
{
  return upgrade_now(UPG_BRACELET) > 0;
}

static void set_bracelet(menu_item_t *it, bool on)
{
  upgrade_set(UPG_BRACELET, on);
  write_strength(on, upgrade_now(UPG_GAUNTLETS));
}

static bool get_defense(menu_item_t *it)
{
  return upgrade_now(UPG_DEFENSE) > 0;
}

static void set_defense(menu_item_t *it, bool on)
{
  field_set(data_double_defense.f[0], on);
  upgrade_set(UPG_DEFENSE, on);
}

/* The Great Fairy of Power's stronger spin attack. Her event gives no item (the other fairies give an
 * Item_FairyBless_*): it plays its spin attack tutorial, and the only thing it leaves in the save is that it
 * was played, which is what the game asks before she grants it again. So this is that flag. */
#define HASH_SPIN_ATTACK_UPGRADE 0x8FA2608Eu /* HavePlayedEvent.Dm_FairyFountain_Power */

static bool get_spin_attack(menu_item_t *it)
{
  bool on = false;
  gd_get_bool(HASH_SPIN_ATTACK_UPGRADE, &on);
  return on;
}

static void set_spin_attack(menu_item_t *it, bool on)
{
  gd_set_bool(HASH_SPIN_ATTACK_UPGRADE, on);
}

/* it->data: the bottle (data_bottles); choice 0 is "none", then data_bottle_names */
static const char *bottle_choices[MAX_CHOICES];
/* each choice's picture, as the bottle's own (choice_pictures): "none" shows the empty bottle */
static uint8_t bottle_pictures[MAX_CHOICES];
static const struct { const char *name; uint8_t picture; } bottle_content_pictures[] = {
  { "empty", GAMEPIC_BOTTLE }, { "red potion", GAMEPIC_BOTTLE_RED_POTION },
  { "green potion", GAMEPIC_POTION_GREEN }, { "blue potion", GAMEPIC_BOTTLE_BLUE_POTION },
  { "blue fire", GAMEPIC_BOTTLE_BLUE_FIRE }, { "fish", GAMEPIC_BOTTLE_FISH }, { "letter", GAMEPIC_BOTTLE_LETTER },
  { "big poe", GAMEPIC_BOTTLE_BIG_POE }, { "poe", GAMEPIC_BOTTLE_POE }, { "bug", GAMEPIC_BOTTLE_BUG },
  { "fairy", GAMEPIC_BOTTLE_FAIRY }, { "max milk", GAMEPIC_BOTTLE_MILK }, { "half milk", GAMEPIC_BOTTLE_HALF_MILK },
};

static int get_bottle(menu_item_t *it)
{
  const bottle_t *bottle = &data_bottles[it->data];
  int32_t owned;
  uint32_t contents;
  if (!field_get(bottle->own, &owned))
    return -1;
  if (!owned)
    return 0;
  if (!gd_get_enum(bottle->contents, &contents))
    return -1;
  for (int i = 0; i < data_n_bottle_contents; i++)
    if (data_bottle_values[i] == contents)
      return i + 1;
  return -1;
}

static void set_bottle(menu_item_t *it, int choice)
{
  const bottle_t *bottle = &data_bottles[it->data];
  field_set(bottle->own, choice ? 1 : 0);
  gd_set_bool(bottle->available, choice != 0);
  if (choice) {
    gd_set_enum(bottle->contents, data_bottle_values[choice - 1]);
    mark_acquired(data_acquired_bottle);
    mark_acquired(data_bottle_acquired[choice - 1]);
  }
}

/* a toggle over one or two fields; it->data: the flag_t, it->data2: a screen to refresh afterwards */
static bool get_flag(menu_item_t *it)
{
  int32_t value;
  return field_get(((const flag_t *)it->data)->f[0], &value) && value;
}

static void set_flag(menu_item_t *it, bool on)
{
  const flag_t *flag = (const flag_t *)it->data;
  for (int i = 0; i < ARRAY_LEN(flag->f); i++)
    if (flag->f[i].type != FT_NONE)
      field_set(flag->f[i], on);
  if (on)
    mark_acquired(flag->acquired);
  if (it->data2)
    pouch_refresh_screen((uintptr_t)it->data2);
}

static menu_item_t *add_flag(menu_t *menu, const flag_t *flag, uintptr_t refresh_screen)
{
  menu_item_t *item = menu_add_check(menu, flag->label, get_flag, set_flag, (intptr_t)flag);
  if (item)
    item->data2 = (intptr_t)refresh_screen;
  return item;
}

/* --- inventory: the menus ------------------------------------------------------------------------- */

#define MAX_DUNGEONS 16
#define N_SPIRITUAL_STONES 3 /* data_quest_items: the stones, then the medallions */
#define MAX_SKULLTULAS 100
#define MAX_SMALL_KEYS 9

static menu_t m_inventory, m_items, m_child_items, m_adult_items, m_universal, m_ammo, m_equipment, m_collection,
  m_magic, m_quest, m_songs, m_dungeon_items, m_dungeon[MAX_DUNGEONS];

/* An item by its data label (data_items, then data_gear). */
static const inv_item_t *find_item(const char *label)
{
  for (int i = 0; i < data_n_items; i++)
    if (strings_equal(data_items[i].label, label))
      return &data_items[i];
  for (int i = 0; i < data_n_gear; i++)
    if (strings_equal(data_gear[i].label, label))
      return &data_gear[i];
  log_printf("menus: no item \"%s\"", label);
  return 0;
}

/* Add the item with data label `label`, shown as `shown`: a checkbox (acquired / owned), or a switch
 * (trade slot, ocarina). */
static void add_item(menu_t *menu, const char *shown, const char *label)
{
  const inv_item_t *item = find_item(label);
  if (!item)
    return;
  switch (item->kind) {
    case IK_ACQUIRED:
      menu_add_check(menu, shown, get_acquired, set_acquired, (intptr_t)item);
      break;
    case IK_OWNED:
      menu_add_check(menu, shown, get_owned, set_owned, (intptr_t)item);
      break;
    case IK_TRADE_CHILD:
    case IK_TRADE_ADULT: {
      int adult = item->kind == IK_TRADE_ADULT;
      int n_items = adult ? data_n_trade_adult : data_n_trade_child;
      menu_item_t *trade = menu_add_option(menu, shown, n_items + 1, trade_names[adult], get_trade, set_trade,
                                           (intptr_t)(adult ? data_trade_adult : data_trade_child));
      if (trade) {
        trade->data2 = n_items;
        trade->choice_pictures = trade_pictures[adult]; /* the picture follows the item held */
      }
      break;
    }
    case IK_OCARINA:
      menu_add_option(menu, shown, N_OCARINA_CHOICES, ocarina_names, get_ocarina, set_ocarina, 0);
      break;
  }
}

/* items: child, adult, universal */
static void build_items(void)
{
  menu_init(&m_items, "items", 4);
  menu_add_submenu(&m_items, "child items", &m_child_items);
  menu_add_submenu(&m_items, "adult items", &m_adult_items);
  menu_add_submenu(&m_items, "universal", &m_universal);
  menu_add_return(&m_items);

  menu_init(&m_child_items, "child items", 6);
  add_item(&m_child_items, "deku sticks", "deku sticks");
  add_item(&m_child_items, "boomerang", "boomerang");
  add_item(&m_child_items, "fairy slingshot", "fairy slingshot");
  add_item(&m_child_items, "magic beans", "magic bean");
  add_item(&m_child_items, "child trade item", "child trade");
  menu_add_return(&m_child_items);

  menu_init(&m_adult_items, "adult items", 8);
  add_item(&m_adult_items, "fairy bow", "fairy bow");
  add_item(&m_adult_items, "megaton hammer", "hammer");
  add_item(&m_adult_items, "fire arrow", "arrow fire");
  add_item(&m_adult_items, "ice arrow", "arrow ice");
  add_item(&m_adult_items, "light arrow", "arrow light");
  hookshots[0] = find_item("hookshot");
  hookshots[1] = find_item("longshot");
  menu_item_t *hookshot = menu_add_option(&m_adult_items, "hookshot", ARRAY_LEN(hookshot_names), hookshot_names,
                                          get_owned_group, set_owned_group, (intptr_t)hookshots);
  if (hookshot)
    hookshot->data2 = hookshots[1] ? 2 : 1;
  add_item(&m_adult_items, "adult trade item", "adult trade");
  menu_add_return(&m_adult_items);

  bottle_choices[0] = "none";
  bottle_pictures[0] = GAMEPIC_BOTTLE;
  for (int i = 0; i < data_n_bottle_contents && i < MAX_CHOICES - 1; i++) {
    bottle_choices[i + 1] = data_bottle_names[i];
    bottle_pictures[i + 1] = GAMEPIC_BOTTLE; /* contents without a picture of their own: the bottle */
    for (unsigned k = 0; k < ARRAY_LEN(bottle_content_pictures); k++)
      if (same_text(data_bottle_names[i], bottle_content_pictures[k].name))
        bottle_pictures[i + 1] = bottle_content_pictures[k].picture;
  }
  static const char *const bottle_labels[] = { "bottle a", "bottle b", "bottle c", "bottle d", "bottle e" };
  menu_init(&m_universal, "universal", 15);
  add_item(&m_universal, "bombs", "bombs");
  add_item(&m_universal, "bombchus", "bombchus");
  add_item(&m_universal, "deku nuts", "deku nuts");
  add_item(&m_universal, "din's fire", "din's fire");
  add_item(&m_universal, "farore's wind", "farore's wind");
  add_item(&m_universal, "nayru's love", "nayru's love");
  add_item(&m_universal, "ocarina", "ocarina");
  for (int bottle = 0; bottle < ARRAY_LEN(bottle_labels); bottle++) {
    menu_item_t *item = menu_add_option(&m_universal, bottle_labels[bottle], data_n_bottle_contents + 1,
                                         bottle_choices, get_bottle, set_bottle, bottle);
    if (item)
      item->choice_pictures = bottle_pictures; /* the picture follows what's in it (add_pictures gives the bottle) */
  }
  menu_item_t *seed = menu_add_check(&m_universal, "business scrub seed", get_scrub_seed, set_scrub_seed, 0);
  if (seed)
    seed->log_label = "business scrub seed:";
  menu_add_return(&m_universal);
}

static void build_ammo(void)
{
  menu_init(&m_ammo, "ammo", data_n_ammo + 2);
  for (int i = 0; i < data_n_ammo; i++)
    menu_add_number(&m_ammo, data_ammo[i].label, get_ammo, set_ammo, max_ammo, fill_ammo,
                    (intptr_t)&data_ammo[i]);
  menu_add_return(&m_ammo);
}

static void build_equipment(void)
{
  mask_names[0] = amiibo_mask_names[0] = "none";
  for (int i = 0; i < data_n_masks && i < MAX_CHOICES - 1; i++)
    mask_names[i + 1] = data_masks[i].label;
  for (int i = 0; i < data_n_amiibo_masks && i < MAX_CHOICES - 1; i++)
    amiibo_mask_names[i + 1] = data_amiibo_masks[i].label;
  menu_init(&m_equipment, "equipment items", 22);
  menu_add_heading(&m_equipment, "swords");
  add_item(&m_equipment, "kokiri sword", "kokiri sword");
  add_item(&m_equipment, "master sword", "master sword");
  big_swords[0] = find_item("giant's knife");
  big_swords[1] = find_item("broken giant's knife");
  big_swords[2] = find_item("biggoron's sword");
  if (big_swords[0] && big_swords[1] && big_swords[2]) {
    menu_item_t *big_sword = menu_add_option(&m_equipment, "big sword", ARRAY_LEN(big_sword_names), big_sword_names,
                                             get_owned_group, set_owned_group, (intptr_t)big_swords);
    if (big_sword)
      big_sword->data2 = ARRAY_LEN(big_swords);
  }
  menu_add_heading(&m_equipment, "shields");
  add_item(&m_equipment, "deku shield", "deku shield");
  add_item(&m_equipment, "hylian shield", "hylian shield");
  add_item(&m_equipment, "mirror shield", "mirror shield");
  menu_add_heading(&m_equipment, "tunics");
  add_item(&m_equipment, "goron tunic", "goron tunic");
  add_item(&m_equipment, "zora tunic", "zora tunic");
  menu_add_heading(&m_equipment, "boots");
  add_item(&m_equipment, "iron boots", "iron boots");
  add_item(&m_equipment, "hover boots", "hover boots");
  menu_add_heading(&m_equipment, "other");
  add_item(&m_equipment, "lenses of truth", "lens of truth");
  menu_add_check(&m_equipment, "echo gem", get_owned, set_owned, (intptr_t)&agony_stone);
  menu_item_t *masks = menu_add_option(&m_equipment, "mask slot", data_n_masks + 1, mask_names, get_trade, set_trade,
                                       (intptr_t)data_masks);
  if (masks)
    masks->data2 = data_n_masks; /* one mask at a time, like the trade slots */
  /* the amiibo masks: a slot of their own on the equipment screen, one at a time too */
  menu_item_t *amiibo = menu_add_option(&m_equipment, "amiibo mask", data_n_amiibo_masks + 1, amiibo_mask_names,
                                        get_trade, set_trade, (intptr_t)data_amiibo_masks);
  if (amiibo)
    amiibo->data2 = data_n_amiibo_masks;
  menu_add_return(&m_equipment);
}

static void add_upgrade(menu_t *menu, const char *label, upgrade_t upgrade)
{
  menu_add_option(menu, label, upgrades[upgrade].n_levels, upgrades[upgrade].level_name, get_upgrade, set_upgrade,
                  upgrade);
}

static void build_collection(void)
{
  menu_init(&m_collection, "collection", 22);
  /* quest items, strength, upgrades (the scale, the bags' and wallet's sizes), the great fairies' upgrades, then
   * the tokens */
  menu_add_heading(&m_collection, "quest rewards");
  add_item(&m_collection, "fairy ocarina", "fairy ocarina"); /* owned flag: the collection screen */
  add_item(&m_collection, "ocarina of time", "ocarina of time");
  add_item(&m_collection, "gerudo card", "gerudo membership card");
  menu_add_heading(&m_collection, "strength upgrades");
  menu_add_check(&m_collection, "goron's bracelet", get_bracelet, set_bracelet, 0);
  add_upgrade(&m_collection, "gauntlets", UPG_GAUNTLETS);
  menu_add_heading(&m_collection, "capacity upgrades");
  add_upgrade(&m_collection, "scale", UPG_SCALE);
  for (int upgrade = UPG_STICK; upgrade <= UPG_WALLET; upgrade++)
    add_upgrade(&m_collection, upgrades[upgrade].name, upgrade);
  menu_add_heading(&m_collection, "fairy upgrades");
  menu_add_check(&m_collection, "double defense", get_defense, set_defense, 0);
  menu_item_t *spin = menu_add_check(&m_collection, "spin attack", get_spin_attack, set_spin_attack, 0);
  if (spin)
    spin->hint = "the great fairy of power's: a stronger spin attack";
  menu_add_submenu(&m_collection, "magic", &m_magic);
  menu_add_heading(&m_collection, "collectibles");
  menu_item_t *skulltulas = menu_add_number(&m_collection, "skulltulas", get_number, set_skulltulas, max_from_data2,
                                            0, (intptr_t)&data_skulltulas);
  if (skulltulas)
    skulltulas->data2 = MAX_SKULLTULAS;
  menu_end_group(&m_collection);
  menu_add_return(&m_collection);

  menu_init(&m_magic, "magic", 4);
  menu_item_t *magic = menu_add_number(&m_magic, "magic", get_magic, set_magic, max_magic, fill_magic, 0);
  if (magic)
    magic->hint = "d-left/right: change   L: fill (set the meter first)";
  add_upgrade(&m_magic, upgrades[UPG_MAGIC].name, UPG_MAGIC);
  menu_add_return(&m_magic);
}

static void build_quest_and_songs(void)
{
  menu_init(&m_quest, "quest items", data_n_quest_items + 3);
  for (int i = 0; i < data_n_quest_items; i++) {
    if (i == 0)
      menu_add_heading(&m_quest, "spiritual stones");
    else if (i == N_SPIRITUAL_STONES)
      menu_add_heading(&m_quest, "medallions");
    add_flag(&m_quest, &data_quest_items[i], 0);
  }
  menu_add_return(&m_quest);

  menu_init(&m_songs, "songs", data_n_songs + 3);
  menu_add_heading(&m_songs, "normal songs");
  for (int i = 0; i < data_n_songs; i++) {
    if (strings_equal(data_songs[i].label, "minuet of forest")) /* data.c: the warp songs follow */
      menu_add_heading(&m_songs, "warp songs");
    add_flag(&m_songs, &data_songs[i], 0);
  }
  menu_add_return(&m_songs);
}

/* The dungeon's small keys, if it has any. Their fields are named a little differently from the
 * dungeons. */
static void add_small_keys(menu_t *menu, const char *dungeon)
{
  static const struct { const char *keys, *dungeon; } key_names[] = {
    { "forest temple", "forest temple" }, { "fire temple", "fire temple" }, { "water temple", "water temple" },
    { "shadow temple", "shadow temple" }, { "spirit temple", "spirit temple" },
    { "bottom of the well", "bottom of well" }, { "gerudo training ground", "gerudo training" },
    { "inside ganon's castle", "ganon's castle" },
  };
  for (int j = 0; j < ARRAY_LEN(key_names); j++) {
    if (!strings_equal(key_names[j].dungeon, dungeon))
      continue;
    for (int i = 0; i < data_n_small_keys; i++)
      if (strings_equal(data_small_keys[i].label, key_names[j].keys)) {
        menu_item_t *keys = menu_add_number(menu, "small keys", get_number, set_number, max_from_data2, 0,
                                            (intptr_t)&data_small_keys[i]);
        if (keys)
          keys->data2 = MAX_SMALL_KEYS;
      }
  }
}

/* a menu for each dungeon: its map, compass and boss key, then its small keys */
static void build_dungeon_items(void)
{
  menu_init(&m_dungeon_items, "dungeon items", data_n_dungeons + 1);
  for (int d = 0; d < data_n_dungeons && d < MAX_DUNGEONS; d++) {
    menu_init(&m_dungeon[d], data_dungeons[d].label, 5);
    for (int k = 0; k < ARRAY_LEN(data_dungeons[d].items); k++)
      add_flag(&m_dungeon[d], &data_dungeons[d].items[k], z64hd_game->vt_map_screen);
    add_small_keys(&m_dungeon[d], data_dungeons[d].label);
    menu_add_return(&m_dungeon[d]);
    menu_add_submenu(&m_dungeon_items, data_dungeons[d].label, &m_dungeon[d]);
  }
  menu_add_return(&m_dungeon_items);
}

static void build_inventory(void)
{
  menu_init(&m_inventory, "inventory", 11);
  menu_item_t *items = menu_add_submenu(&m_inventory, "items", &m_items);
  if (items)
    items->hint = "the game's L/R quick menus need 2+ items";
  menu_add_submenu(&m_inventory, "ammo", &m_ammo);
  menu_add_submenu(&m_inventory, "equipment items", &m_equipment);
  menu_add_submenu(&m_inventory, "collection", &m_collection);
  menu_add_submenu(&m_inventory, "quest items", &m_quest);
  menu_add_submenu(&m_inventory, "songs", &m_songs);
  menu_add_submenu(&m_inventory, "dungeon items", &m_dungeon_items);
  menu_add_return(&m_inventory);

  trade_names[0][0] = "none";
  trade_names[1][0] = "none";
  for (int adult = 0; adult < 2; adult++) {
    const pouch_one_t *items = adult ? data_trade_adult : data_trade_child;
    int n = adult ? data_n_trade_adult : data_n_trade_child;
    for (int i = 0; i < n && i < MAX_CHOICES - 1; i++) {
      trade_names[adult][i + 1] = items[i].label;
      for (unsigned k = 0; k < ARRAY_LEN(trade_item_pictures); k++)
        if (same_text(items[i].item, trade_item_pictures[k].item))
          trade_pictures[adult][i + 1] = trade_item_pictures[k].picture;
    }
  }

  /* the order matters: menu_init reserves each menu's items from one shared pool */
  build_items();
  build_ammo();
  build_equipment();
  build_collection();
  build_quest_and_songs();
  build_dungeon_items();
  menu_t *pictured[] = { &m_child_items, &m_adult_items, &m_universal, &m_ammo, &m_equipment, &m_collection,
                         &m_magic };
  for (unsigned i = 0; i < ARRAY_LEN(pictured); i++)
    add_pictures(pictured[i]);
}

/* --- equips ------------------------------------------------------------------------------------- */

static menu_t m_equips;

/* The game's item names as the inventory menus call them. */
static const struct { const char *game, *shown; } worn_names[] = {
  { "Weapon_Sword_Kokiri", "kokiri sword" }, { "Weapon_Sword_Master", "master sword" },
  { "Weapon_Sword_Giant", "giant's knife" }, { "Weapon_Sword_BrokenGiant", "broken giant's knife" },
  { "Weapon_Sword_Biggoron", "biggoron's sword" }, { "Weapon_Shield_Deku", "deku shield" },
  { "Weapon_Shield_Hylian", "hylian shield" }, { "Weapon_Shield_Mirror", "mirror shield" },
  { "Armor_Adult_Goron", "goron tunic" }, { "Armor_Adult_Zora", "zora tunic" },
  { "Armor_Adult_HeavyBoot", "iron boots" }, { "Armor_Adult_HoverBoot", "hover boots" },
  { "Item_Ocarina_Fairy", "fairy ocarina" }, { "Item_Ocarina_Time", "ocarina of time" },
  { "Item_Arrow_Fire", "fire arrow" }, { "Item_Arrow_Ice", "ice arrow" }, { "Item_Arrow_Light", "light arrow" },
};

/* the lines of the equips menu: what Link wears in each slot (it->data indexes both) */
static const char *const worn_slot_names[] = { "sword", "shield", "tunic", "boots", "arrow", "ocarina" };
static const uint32_t worn_slot_fields[] = { GD_STR_SWORD, GD_STR_SHIELD, GD_STR_TUNIC, GD_STR_BOOTS, GD_STR_ARROW,
                                        GD_STR_OCARINA };

/* Anything else: drop the Weapon_/Armor_/Item_ prefix and its category, split the words, lower case. */
static void tidy_name(const char *game_name, char *buf, int len)
{
  static const char *const prefixes[] = { "Weapon_Sword_", "Weapon_Shield_", "Weapon_", "Armor_Adult_",
                                          "Armor_Child_", "Armor_", "Item_" };
  for (int i = 0; i < ARRAY_LEN(prefixes); i++) {
    const char *rest = after_prefix(game_name, prefixes[i]);
    if (rest) {
      game_name = rest;
      break;
    }
  }
  int n = 0;
  for (int i = 0; game_name[i] && n < len - 2; i++) { /* room for a space, the letter and the end */
    char c = game_name[i];
    if (c == '_') {
      c = ' ';
    } else if (c >= 'A' && c <= 'Z') {
      if (i && n && buf[n - 1] != ' ') /* a capital starts a new word */
        buf[n++] = ' ';
      c += 'a' - 'A';
    }
    buf[n++] = c;
  }
  buf[n] = 0;
}

static void text_worn(menu_item_t *it, char *buf, int len)
{
  const char *game_name = gd_get_str(worn_slot_fields[it->data]);
  char shown[48];
  if (!game_name || !game_name[0]) {
    snprintf(shown, sizeof(shown), "none");
  } else {
    shown[0] = 0;
    for (int i = 0; i < ARRAY_LEN(worn_names) && !shown[0]; i++)
      if (strings_equal(game_name, worn_names[i].game))
        snprintf(shown, sizeof(shown), "%s", worn_names[i].shown);
    if (!shown[0])
      tidy_name(game_name, shown, sizeof(shown));
  }
  snprintf(buf, len, "%-9s %s", worn_slot_names[it->data], shown);
}

/* what Link wears (owned gear is in inventory > equipment items) */
static void build_equips(void)
{
  menu_init(&m_equips, "equips", 7);
  for (int slot = 0; slot < ARRAY_LEN(worn_slot_names); slot++)
    menu_add_text(&m_equips, text_worn, slot);
  menu_add_return(&m_equips);
}

/* --- story ------------------------------------------------------------------------------------------ */

static menu_t m_story;

/* it->data: the quest (data_quests); a choice is a stage */
static int get_stage(menu_item_t *it)
{
  const quest_t *quest = &data_quests[it->data];
  uint32_t value;
  if (!gd_get_enum(quest->hash, &value))
    return -1;
  for (int stage = 0; stage < quest->n_stages; stage++)
    if (murmur3(quest->stages[stage]) == value)
      return stage;
  return -1;
}

static void set_stage(menu_item_t *it, int stage)
{
  const quest_t *quest = &data_quests[it->data];
  gd_set_enum(quest->hash, murmur3(quest->stages[stage]));
}

/* the stage's name, lower case */
static void stage_text(menu_item_t *it, int stage, char *buf, int len)
{
  const char *name = data_quests[it->data].stages[stage];
  int n = 0;
  for (; name[n] && n < len - 1; n++)
    buf[n] = (name[n] >= 'A' && name[n] <= 'Z') ? name[n] + ('a' - 'A') : name[n];
  buf[n] = 0;
}

static void act_tutorials(menu_item_t *it)
{
  int marked = 0;
  for (int i = 0; i < data_n_action_guides; i++)
    marked += gd_set_bool(data_action_guides[i], true);
  char message[48];
  snprintf(message, sizeof(message), "%d tutorials marked seen", marked);
  gfx_notice(message);
}

static void build_story(void)
{
  menu_init(&m_story, "story", data_n_quests + 4);
  menu_add_action(&m_story, "all tutorials seen", act_tutorials, 0);
  for (int i = 0; i < data_n_quests; i++) {
    menu_item_t *quest = menu_add_option(&m_story, data_quests[i].label, data_quests[i].n_stages, 0, get_stage,
                                         set_stage, i);
    if (quest) {
      quest->choice_text = stage_text;
      quest->hint = "d-left/right or L: change stage (applies after a loading zone)";
    }
  }
  menu_add_return(&m_story);
}

/* --- watches: the box ---------------------------------------------------------------------------- */

static menu_t m_watches, m_watch_add;
#define WATCH_ROWS 5 /* watches shown at once; gz's arrows scroll the rest */
static int watch_top;                /* the first watch shown */
static int watch_first_row;          /* the menu item showing it */
static uint32_t watches_built = ~0u; /* the watch_version the menu shows; ~0u: rebuild it */

static bool get_shown(menu_item_t *it)
{
  return watches_shown;
}

static void set_shown(menu_item_t *it, bool on)
{
  watches_shown = on;
}

static bool get_header(menu_item_t *it)
{
  return watches_header;
}

static void set_header(menu_item_t *it, bool on)
{
  watches_header = on;
}

static int get_corner(menu_item_t *it)
{
  return watches_corner;
}

static void set_corner(menu_item_t *it, int corner)
{
  watches_corner = corner;
}

/* move: the watches (or the input display) go to "custom" and the d-pad moves them, shown live; L leaves
 * them there. They glide while the d-pad is held (in thousandths of the picture per frame, faster the longer
 * it's held; a tap moves them one), rather than jumping on the d-pad's repeat. */
#define WATCHES_POS_MAX 1000 /* positions are thousandths of the picture */
#define WATCHES_ROUNDING 3   /* how far nvn.c's rounded copy of the position can drift from ours */
static int move_held;        /* frames the d-pad has been held */

/* what a move item moves (its it->data) */
enum { MOVE_WATCHES, MOVE_INPUT };
static const struct {
  int *corner, *x, *y;
  bool *moving; /* shown while the menu is open */
  int panel;    /* its gfx panel, for where nvn.c really put it */
  const char *done;
} movers[] = {
  [MOVE_WATCHES] = { &watches_corner, &watches_x, &watches_y, &watches_moving, GFX_PANEL_HUD, "watches moved" },
  [MOVE_INPUT] = { &input_corner, &input_x, &input_y, &input_moving, GFX_PANEL_INPUT, "input display moved" },
};

/* how far the watches move this frame */
static int glide_step(int frames_held)
{
  if (frames_held == 0)
    return 0;
  if (frames_held < 10)
    return 1;
  if (frames_held < 30)
    return 3;
  if (frames_held < 60)
    return 6;
  return 10;
}

static bool move_panel(menu_item_t *it)
{
  int *x = movers[it->data].x, *y = movers[it->data].y, panel = movers[it->data].panel;
  *movers[it->data].moving = true;
  /* nvn.c keeps it whole on screen: follow that only when it really pushed it back (its value is rounded,
   * so taking it every frame made it creep) */
  if (gfx_custom_x[panel] >= 0 &&
      (gfx_custom_x[panel] - *x > WATCHES_ROUNDING || *x - gfx_custom_x[panel] > WATCHES_ROUNDING))
    *x = gfx_custom_x[panel];
  if (gfx_custom_y[panel] >= 0 &&
      (gfx_custom_y[panel] - *y > WATCHES_ROUNDING || *y - gfx_custom_y[panel] > WATCHES_ROUNDING))
    *y = gfx_custom_y[panel];
  if (cmd_pressed(CMD_SELECT)) {
    move_held = 0;
    gfx_logf("%s", movers[it->data].done);
    return false;
  }
  int dx = cmd_held(CMD_RIGHT) - cmd_held(CMD_LEFT);
  int dy = cmd_held(CMD_DOWN) - cmd_held(CMD_UP);
  move_held = dx || dy ? move_held + 1 : 0;
  int step = glide_step(move_held);
  *x = clamp_int(*x + dx * step, 0, WATCHES_POS_MAX);
  *y = clamp_int(*y + dy * step, 0, WATCHES_POS_MAX);
  return true;
}

static void act_move_panel(menu_item_t *it)
{
  int *corner = movers[it->data].corner;
  if (*corner != GFX_CUSTOM) { /* start near the corner it's in */
    static const int16_t corner_start[GFX_NPLACES][2] = {
      { 0, 0 }, { 1000, 0 }, { 0, 1000 }, { 1000, 1000 }, { 400, 0 }, { 400, 1000 }, { 0, 0 },
    };
    *movers[it->data].x = corner_start[*corner][0];
    *movers[it->data].y = corner_start[*corner][1];
    *corner = GFX_CUSTOM;
    gfx_custom_x[movers[it->data].panel] = -1;
  }
  *movers[it->data].moving = true;
  move_held = 0;
  menu_edit(it);
}

static void act_timer_toggle(menu_item_t *it)
{
  timer_toggle();
}

static void act_timer_reset(menu_item_t *it)
{
  timer_reset();
}

static void act_lag_reset(menu_item_t *it)
{
  lag_reset();
}

/* --- watches: the list ------------------------------------------------------------------------------ */

static void act_js_height_reset(menu_item_t *it)
{
  gfx_notice(js_height_reset() ? "jump height reset to maximum" : "jump height unavailable or read-only");
}

static bool get_js_height_lock(menu_item_t *it) { return js_height_lock; }
static void set_js_height_lock(menu_item_t *it, bool on)
{
  js_height_lock = on;
  if (on && !js_height_reset())
    gfx_notice("jump height lock: waiting for writable player");
}

/* A row of the list, gz's buttons: the red cross (remove); the wrench (show the value in debug >
 * memory) when it has an address; the label (rename it with gz's keyboard); the type switch; then
 * the value and the arrows that move it up or down the list. d-left/right picks a button, L presses
 * it (the type: the next one). it->data = the row's index in watch_list.
 * The checkbox after the cross shows or hides the watch on screen without taking it off the list.
 * Columns in the line: cross 0, checkbox 2, wrench 5, label 7, type 20, value 28, arrows. */
enum { PART_REMOVE, PART_SHOW, PART_WRENCH, PART_LABEL, PART_TYPE, PART_UP, PART_DOWN };
#define MAX_ROW_PARTS 8
#define WATCH_LABEL_SIZE 24
#define WATCH_LABEL_W 12 /* the label's column ("%-12.12s" in watch_row) */
#define ROW_VALUE_W 20
#define COL_SHOW 2       /* the checkbox (two cells): shown in the on-screen box or not */
#define COL_WRENCH 5
#define COL_LABEL 7
#define COL_VALUE 28
#define COL_UP (COL_VALUE + ROW_VALUE_W + 1)
#define COL_DOWN (COL_UP + 2)

/* the buttons this watch's row has, in order; returns how many */
static int row_parts(const watch_entry_t *entry, int *parts)
{
  int n_types, current;
  int count = 0;
  watch_entry_types(entry, &n_types, &current);
  parts[count++] = PART_REMOVE;
  parts[count++] = PART_SHOW;
  if (watch_entry_has_address(entry))
    parts[count++] = PART_WRENCH;
  parts[count++] = PART_LABEL;
  if (n_types)
    parts[count++] = PART_TYPE;
  parts[count++] = PART_UP;
  parts[count++] = PART_DOWN;
  return count;
}

/* the button chosen in a row (a PART_*) */
static int row_part(const menu_item_t *it)
{
  int parts[MAX_ROW_PARTS];
  int count = row_parts(&watch_list[it->data], parts);
  return parts[it->part < count ? it->part : count - 1];
}

static void watch_row(menu_item_t *it, char *buf, int len)
{
  if (it->data >= watch_count) {
    snprintf(buf, len, "-");
    return;
  }
  const watch_entry_t *entry = &watch_list[it->data];
  char label[WATCH_LABEL_SIZE], value[48];
  int n_types, current;
  const char *const *types = watch_entry_types(entry, &n_types, &current);
  watch_entry_display_label(entry, label, sizeof(label));
  watch_entry_text(entry, value, sizeof(value));
  snprintf(buf, len, "%c%c %c %-12.12s %s%-3s%s %-*.*s %c %c", entry->hidden ? GFX_CHECK_OFF : GFX_CHECK_ON,
           GFX_WIDE_REST, watch_entry_has_address(entry) ? GFX_ICON_WRENCH : ' ', label, n_types ? "< " : "  ",
           n_types ? types[current] : "", n_types ? " >" : "  ", ROW_VALUE_W, ROW_VALUE_W, value, GFX_ICON_UP,
           GFX_ICON_DOWN);
}

/* where the chosen button is in the line (the line starts with the cross: "x " + the row text) */
static bool watch_cursor(menu_item_t *it, const char *line, int *start, int *len)
{
  switch (row_part(it)) {
    case PART_REMOVE:
      *start = 0;
      *len = 1;
      return true;
    case PART_SHOW:
      *start = COL_SHOW;
      *len = 2;
      return true;
    case PART_WRENCH:
      *start = COL_WRENCH;
      *len = 1;
      return true;
    case PART_LABEL: {
      char label[WATCH_LABEL_SIZE];
      watch_entry_display_label(&watch_list[it->data], label, sizeof(label));
      *start = COL_LABEL;
      *len = clamp_int((int)strlen(label), 1, WATCH_LABEL_W);
      return true;
    }
    case PART_UP:
      *start = COL_UP;
      *len = 1;
      return true;
    case PART_DOWN:
      *start = COL_DOWN;
      *len = 1;
      return true;
    default: { /* PART_TYPE: the "< ... >" after the label */
      int open = COL_LABEL + WATCH_LABEL_W;
      while (line[open] && line[open] != '<')
        open++;
      int close = open;
      while (line[close] && line[close] != '>')
        close++;
      *start = open;
      *len = line[close] ? close - open + 1 : 0;
      return true;
    }
  }
}

/* the on-screen keyboard's answer; an empty label gives the watch its own name back */
static void watch_renamed(const char *text, intptr_t index)
{
  char before[WATCH_LABEL_SIZE];
  watch_entry_label(&watch_list[index], before, sizeof(before));
  watch_set_label((int)index, text);
  if (text[0])
    gfx_logf("renamed %s to %s", before, text);
  else
    gfx_logf("%s: its own name again", before);
}

/* moves the watch at `index` up (-1) or down (1) the list; the window and cursor follow it */
static void watch_move_row(menu_item_t *it, int index, int dir)
{
  int new_index = index + dir;
  if (new_index < 0 || new_index >= watch_count)
    return;
  /* the window follows the row, and the cursor goes with it, on the same arrow */
  if (new_index < watch_top)
    watch_top = new_index;
  else if (new_index >= watch_top + WATCH_ROWS)
    watch_top = new_index - WATCH_ROWS + 1;
  int row = watch_first_row + (new_index - watch_top);
  if (row < m_watches.n_items) {
    m_watches.items[row].part = it->part;
    m_watches.selected = row;
  }
  watch_move(index, dir);
}

static void watch_press(menu_item_t *it, int part)
{
  int index = (int)it->data;
  if (index >= watch_count)
    return;
  watch_entry_t *entry = &watch_list[index];
  char label[WATCH_LABEL_SIZE];
  watch_entry_display_label(entry, label, sizeof(label));
  int button = row_part(it);
  switch (button) {
    case PART_REMOVE:
      gfx_logf("removed watch %s", label);
      watch_remove(index);
      break;
    case PART_SHOW:
      watch_set_hidden(index, !entry->hidden);
      gfx_logf("%s %s", label, entry->hidden ? "hidden" : "shown");
      break;
    case PART_WRENCH: {
      uintptr_t address;
      int type;
      if (watch_entry_address(entry, &address, &type))
        memview_open(address, type);
      else
        gfx_logf("%s: not in memory right now", label);
      break;
    }
    case PART_LABEL:
      osk_open("watch label", entry->label[0] ? entry->label : label, OSK_MAX, watch_renamed, index);
      break;
    case PART_UP:
    case PART_DOWN:
      watch_move_row(it, index, button == PART_UP ? -1 : 1);
      break;
    default: { /* PART_TYPE: the next type */
      int n_types, current;
      const char *const *types = watch_entry_types(entry, &n_types, &current);
      if (n_types) {
        watch_entry_set_type(index, current + 1);
        types = watch_entry_types(entry, &n_types, &current);
        gfx_logf("%s: %s", label, types[current]);
      }
      break;
    }
  }
}

/* gz's arrows when there are more watches than WATCH_ROWS: up over the list (with which of them show), down
 * under it. it->data: -1 up, 1 down */
static void watch_arrow(menu_item_t *it, char *buf, int len)
{
  int last = watch_top + WATCH_ROWS < watch_count ? watch_top + WATCH_ROWS : watch_count;
  if (it->data < 0)
    snprintf(buf, len, "%c  %d-%d / %d", GFX_ICON_UP, watch_top + 1, last, watch_count);
  else
    snprintf(buf, len, "%c", GFX_ICON_DOWN);
}

/* the cursor on the arrow only */
static bool arrow_cursor(menu_item_t *it, const char *line, int *start, int *len)
{
  *start = 0;
  *len = 1;
  return true;
}

static void watch_scroll(menu_item_t *it, int part)
{
  int top = clamp_int(watch_top + (int)it->data, 0, max_top(watch_count, WATCH_ROWS));
  if (top != watch_top) {
    watch_top = top;
    watches_built = ~0u; /* the rows show other watches: rebuilt */
  }
}

static void add_watch_arrow(int dir)
{
  menu_item_t *arrow = menu_add_action(&m_watches, "", 0, dir);
  if (!arrow)
    return;
  arrow->text = watch_arrow;
  arrow->cursor = arrow_cursor;
  arrow->press = watch_scroll;
  arrow->press_repeat = true;
  arrow->hint = dir < 0 ? "L: scroll the list up (held: keeps going)" : "L: scroll the list down (held: keeps going)";
}

/* --- watches: adding them -------------------------------------------------------------------------- */

/* the folder: gz's import list, here the predefined watches. Choosing one adds it and goes back. */
static void watch_add_state(menu_item_t *it, char *buf, int len)
{
  snprintf(buf, len, "%s", watch_has((watch_t)it->data) ? "(in the list)" : "");
}

static void act_watch_add(menu_item_t *it)
{
  if (!watch_add((watch_t)it->data)) {
    gfx_notice("that watch is already in the list");
    return;
  }
  gfx_logf("added watch %s", watch_labels[it->data]);
  menu_back();
}

/* gz's +: a memory watch at an address typed in (hex digits, as gz's address field). It starts
 * from the last memory watch's address and type (as gz does), else main's start as a word. */
#define CUSTOM_DIGITS 10
static uintptr_t custom_addr;
static int custom_digit; /* the digit being changed, 0 = the leftmost; CUSTOM_CANCEL: on the x */
#define CUSTOM_CANCEL (-1)
static int custom_type = MW_X32;

/* The row under the list, gz's buttons with no names (the hint says what they are): the folder
 * (known watches) and the + (custom watch). While typing an address it follows the +, and the folder's
 * place is an x (in the rows' x column) that gives up the watch. */
enum { TOOL_KNOWN, TOOL_CUSTOM };
#define COL_TOOL_CUSTOM 3  /* the +, after the folder (two cells) and a space */
#define COL_TOOL_DIGITS 6  /* the address, after "F+ +  " */
static const char *const tool_hints[] = { "known watches: the built-in ones (L: open)",
                                          "custom watch: a memory address of your choosing (L: type it)" };

static void tools_text(menu_item_t *it, char *buf, int len)
{
  if (menu_editing == it)
    snprintf(buf, len, "%c  %c  %0*lx  %s", GFX_ICON_REMOVE, GFX_ICON_ADD, CUSTOM_DIGITS,
             (unsigned long)custom_addr, memory_type_names[custom_type]);
  else
    snprintf(buf, len, "%c%c %c", GFX_ICON_FOLDER, GFX_WIDE_REST, GFX_ICON_ADD);
}

static bool tools_cursor(menu_item_t *it, const char *line, int *start, int *len)
{
  if (menu_editing == it) {
    *start = custom_digit == CUSTOM_CANCEL ? 0 : COL_TOOL_DIGITS + custom_digit;
    *len = 1;
  } else if (it->part == TOOL_KNOWN) {
    *start = 0;
    *len = 2;
  } else {
    *start = COL_TOOL_CUSTOM;
    *len = 1;
  }
  return true;
}

/* typing the address: d-left/right picks a digit (left of the first, the x), d-up/down changes it, L adds
 * the watch (on the x: gives it up, as B does) */
static const char *const custom_hint = "d-left/right: digit   d-up/down: change   L: add   B: cancel";
static const char *const cancel_hint = "L: cancel, no watch added";

static bool custom_watch_edit(menu_item_t *it)
{
  if (cmd_repeat(CMD_LEFT)) {
    custom_digit = custom_digit > CUSTOM_CANCEL ? custom_digit - 1 : CUSTOM_CANCEL;
  } else if (cmd_repeat(CMD_RIGHT)) {
    custom_digit = custom_digit < CUSTOM_DIGITS - 1 ? custom_digit + 1 : CUSTOM_DIGITS - 1;
  } else if (custom_digit == CUSTOM_CANCEL && cmd_pressed(CMD_SELECT)) {
    gfx_logf("custom watch cancelled");
    return false;
  } else if (custom_digit == CUSTOM_CANCEL) {
    /* on the x: nothing to change */
  } else if (cmd_repeat(CMD_UP) || cmd_repeat(CMD_DOWN)) {
    int shift = (CUSTOM_DIGITS - 1 - custom_digit) * 4;
    int dir = cmd_repeat(CMD_UP) ? 1 : -1;
    /* the digit goes round, f <-> 0, without carrying into the next */
    uintptr_t digit = ((custom_addr >> shift & 0xF) + (uintptr_t)(dir + 16)) & 0xF;
    custom_addr = (custom_addr & ~((uintptr_t)0xF << shift)) | digit << shift;
  } else if (cmd_pressed(CMD_SELECT)) {
    if (watch_add_memory(custom_addr, custom_type))
      gfx_logf("added watch %0*lx", CUSTOM_DIGITS, (unsigned long)custom_addr);
    else
      gfx_notice("that address is already watched");
    return false;
  }
  it->edit_hint = custom_digit == CUSTOM_CANCEL ? cancel_hint : custom_hint;
  return true;
}

static void act_custom_watch(menu_item_t *it)
{
  custom_addr = z64hd_main;
  custom_type = MW_X32;
  for (int i = watch_count - 1; i >= 0; i--)
    if (watch_list[i].id == W_MEMORY) {
      custom_addr = watch_list[i].addr;
      custom_type = watch_list[i].type;
      break;
    }
  custom_digit = CUSTOM_DIGITS - 1;
  it->edit_hint = custom_hint;
  menu_edit(it);
}

static void tools_press(menu_item_t *it, int part)
{
  if (part == TOOL_KNOWN)
    menu_goto(&m_watch_add);
  else
    act_custom_watch(it);
}

/* --- watches: the menu ----------------------------------------------------------------------------- */

static void add_watches_box_items(void)
{
  menu_add_check(&m_watches, "show on screen", get_shown, set_shown, 0);
  menu_add_heading(&m_watches, "watches box");
  menu_add_option(&m_watches, "position", GFX_NPLACES, watches_corner_names, get_corner, set_corner, 0);
  menu_item_t *move = menu_add_action(&m_watches, "move watches position", act_move_panel, MOVE_WATCHES);
  if (move) {
    move->edit = move_panel;
    move->edit_alone = true;
    move->hint = "L: move the watches around the screen";
    move->edit_hint = "watches: d-pad moves them   L: done";
  }
}

/* a row for each watch in the window, between the scroll arrows (if needed), with a scroll bar beside them */
static void add_watch_rows(void)
{
  watch_top = clamp_int(watch_top, 0, max_top(watch_count, WATCH_ROWS));
  bool scrolls = watch_count > WATCH_ROWS;
  if (scrolls)
    add_watch_arrow(-1);
  watch_first_row = m_watches.n_items;
  for (int i = watch_top; i < watch_count && i < watch_top + WATCH_ROWS; i++) {
    menu_item_t *row = menu_add_action(&m_watches, "", 0, i);
    if (!row)
      continue;
    int parts[MAX_ROW_PARTS];
    row->text = watch_row;
    /* on gz's grid: right-aligned, its "< type > value" moved with every change in the live value's width
     * and the type's when the cursor made it bold */
    row->grid = true;
    row->icon = GFX_ICON_REMOVE;
    row->cursor = watch_cursor;
    row->press = watch_press;
    row->n_parts = row_parts(&watch_list[i], parts);
    row->hint = "d-left/right: pick a button   L: press it";
  }
  m_watches.inner_rows = 0;
  if (scrolls) {
    m_watches.inner_first = watch_first_row;
    m_watches.inner_rows = m_watches.n_items - watch_first_row;
    m_watches.inner_items = watch_count;
    m_watches.inner_top = watch_top;
    add_watch_arrow(1);
  }
}

/* the folder and + row; `reselect` puts the cursor back on it, on button `part` */
static void add_watch_tools(bool reselect, int part)
{
  menu_item_t *tools = menu_add_action(&m_watches, "", 0, 0);
  if (!tools)
    return;
  tools->text = tools_text;
  tools->cursor = tools_cursor;
  tools->grid = true; /* its typed address: the cursor's digit doesn't move the others */
  tools->press = tools_press;
  tools->n_parts = 2;
  tools->part_hints = tool_hints;
  tools->edit = custom_watch_edit;
  tools->edit_hint = custom_hint;
  if (reselect) {
    m_watches.selected = (int)(tools - m_watches.items);
    tools->part = part;
  }
}

/* The watches menu, rebuilt whenever the list changes (watches_sync). */
static void fill_watches(void)
{
  /* the cursor stays on the folder when a watch is added from it */
  bool on_tools = m_watches.n_items && m_watches.items[m_watches.selected].text == tools_text;
  int tools_part = on_tools ? m_watches.items[m_watches.selected].part : 0;
  int part = m_watches.n_items ? m_watches.items[m_watches.selected].part : 0;
  menu_reset(&m_watches);
  add_watches_box_items();
  menu_add_heading(&m_watches, "watches");
  add_watch_rows();
  add_watch_tools(on_tools, tools_part);
  menu_add_heading(&m_watches, "jump height");
  menu_item_t *reset_js = menu_add_action(&m_watches, "reset jump height", act_js_height_reset, 0);
  if (reset_js)
    reset_js->hint = "L: set the threshold to maximum once (also {reset_jump})";
  menu_item_t *lock_js = menu_add_check(&m_watches, "lock jump height", get_js_height_lock, set_js_height_lock, 0);
  if (lock_js)
    lock_js->hint = "Keep maximum height each frame; off after restarting";
  menu_add_heading(&m_watches, "timer");
  menu_add_action(&m_watches, "start/stop timer", act_timer_toggle, 0);
  menu_add_action(&m_watches, "reset timer", act_timer_reset, 0);
  menu_add_action(&m_watches, "reset lag counter", act_lag_reset, 0);
  menu_add_return(&m_watches);
  menu_refresh(&m_watches);
  /* the cursor keeps to the same button of whatever row it's now on */
  menu_item_t *selected = &m_watches.items[m_watches.selected];
  if (selected->n_parts > 1)
    selected->part = part < selected->n_parts ? part : selected->n_parts - 1;
  watches_built = watch_version;
}

void watches_sync(void)
{
  if (watches_built != watch_version)
    fill_watches();
}

static void build_watches(void)
{
  menu_init(&m_watches, "watches", WATCH_ROWS + 19);  /* the watches, their 2 arrows, the rest */
  fill_watches();
  menu_init(&m_watch_add, "known watches", W_COUNT + 1);
  m_watch_add.parent = &m_watches; /* opened from the folder button */
  for (int w = 0; w < W_COUNT; w++) {
    menu_item_t *known = menu_add_action(&m_watch_add, watch_labels[w], act_watch_add, w);
    if (known) {
      known->text = watch_add_state;
      known->hint = "L: add to the list";
    }
  }
  menu_add_return(&m_watch_add);
}

/* --- scene ------------------------------------------------------------------------------------ */

static menu_t m_scene, m_collision;

static bool get_air_walls_shown(menu_item_t *it)
{
  return airwalls_shown;
}

static void set_air_walls_shown(menu_item_t *it, bool on)
{
  airwalls_shown = on;
}

static bool get_air_walls_push(menu_item_t *it)
{
  return airwalls_push;
}

static void set_air_walls_push(menu_item_t *it, bool on)
{
  airwalls_push = on;
}

static bool get_air_walls_block(menu_item_t *it)
{
  return airwalls_block;
}

static void set_air_walls_block(menu_item_t *it, bool on)
{
  airwalls_block = on;
}

static bool get_air_walls_all(menu_item_t *it)
{
  return airwalls_all;
}

static void set_air_walls_all(menu_item_t *it, bool on)
{
  airwalls_all = on;
}

static bool get_air_walls_xray(menu_item_t *it)
{
  return airwalls_xray;
}

static void set_air_walls_xray(menu_item_t *it, bool on)
{
  airwalls_xray = on;
}

static bool get_enemy_hp_xray(menu_item_t *it)
{
  return enemy_hp_xray;
}

static void set_enemy_hp_xray(menu_item_t *it, bool on)
{
  enemy_hp_xray = on;
}

static bool get_enemy_hp(menu_item_t *it)
{
  return enemy_hp_shown;
}

static void set_enemy_hp(menu_item_t *it, bool on)
{
  enemy_hp_shown = on;
}

static void text_air_wall_summary(menu_item_t *it, char *buf, int len)
{
  char summary[64];
  airwall_summary(summary, sizeof(summary));
  snprintf(buf, len, TF("count  %s"), summary);
}

static void text_air_wall_link(menu_item_t *it, char *buf, int len)
{
  char text[64];
  airwall_link_text(text, sizeof(text));
  snprintf(buf, len, TF("link   %s"), text);
}

static bool get_savepos_shown(menu_item_t *it)
{
  return savepos_shown;
}

static void set_savepos_shown(menu_item_t *it, bool on)
{
  savepos_shown = on;
}

static void text_savepos_slot(menu_item_t *it, char *buf, int len)
{
  savepos_text((int)it->data, buf, len);
}

static bool get_savepos_load(menu_item_t *it)
{
  return savepos_load;
}

static void set_savepos_load(menu_item_t *it, bool on)
{
  savepos_load = on;
}

static void text_savepos_load(menu_item_t *it, char *buf, int len)
{
  savepos_load_text(buf, len);
}

static bool get_voids_shown(menu_item_t *it)
{
  return voidmap_shown;
}

static void set_voids_shown(menu_item_t *it, bool on)
{
  voidmap_shown = on;
}

static bool get_voids_ground(menu_item_t *it)
{
  return voidmap_ground;
}

static void set_voids_ground(menu_item_t *it, bool on)
{
  voidmap_ground = on;
}

static bool get_voids_deadly(menu_item_t *it)
{
  return voidmap_deadly;
}

static void set_voids_deadly(menu_item_t *it, bool on)
{
  voidmap_deadly = on;
}

static bool get_voids_ring(menu_item_t *it)
{
  return voidmap_ring;
}

static void set_voids_ring(menu_item_t *it, bool on)
{
  voidmap_ring = on;
}

static bool get_voids_areas(menu_item_t *it)
{
  return voidmap_areas;
}

static void set_voids_areas(menu_item_t *it, bool on)
{
  voidmap_areas = on;
}

static bool get_voids_xray(menu_item_t *it)
{
  return voidmap_xray;
}

static void set_voids_xray(menu_item_t *it, bool on)
{
  voidmap_xray = on;
}

static void text_voids_link(menu_item_t *it, char *buf, int len)
{
  char text[64];
  voidmap_text(text, sizeof(text));
  snprintf(buf, len, TF("link   %s"), text);
}

static menu_t m_voids;

static void build_voids(void)
{
  menu_init(&m_voids, "void out", 8);
  menu_item_t *show = menu_add_check(&m_voids, "show", get_voids_shown, set_voids_shown, 0);
  if (show) {
    show->hint = "where the game's ground map voids link, round link";
    show->log_label = "void out:";
  }
  menu_item_t *ground = menu_add_check(&m_voids, "edge of the ground map", get_voids_ground, set_voids_ground, 0);
  if (ground) {
    ground->hint = "white: past it the game finds nothing under link";
    ground->log_label = "void out, ground edge:";
    ground->indent = 2; /* under show */
  }
  menu_item_t *deadly = menu_add_check(&m_voids, "deadly cells", get_voids_deadly, set_voids_deadly, 0);
  if (deadly) {
    deadly->hint = "red: anywhere in one of these columns link voids at once";
    deadly->log_label = "void out, deadly cells:";
    deadly->indent = 2;
  }
  menu_item_t *ring = menu_add_check(&m_voids, "8 unit fence", get_voids_ring, set_voids_ring, 0);
  if (ring) {
    ring->hint = "orange: off the map link voids on leaving it";
    ring->log_label = "void out, fence:";
    ring->indent = 2;
  }
  menu_item_t *areas = menu_add_check(&m_voids, "void areas", get_voids_areas, set_voids_areas, 0);
  if (areas) {
    areas->hint = "purple: placed in the map, link voids inside one   red: link is in it";
    areas->log_label = "void out, areas:";
    areas->indent = 2;
  }
  menu_item_t *xray = menu_add_check(&m_voids, "through the scenery", get_voids_xray, set_voids_xray, 0);
  if (xray) {
    xray->hint = "also where the scenery is in front of them (the game's style)";
    xray->log_label = "void out through the scenery:";
    xray->indent = 2;
  }
  menu_add_return(&m_voids);
  /* link now: under the list, not part of it */
  menu_add_text(&m_voids, text_voids_link, 0);
  m_voids.footer = 1;
}

static void text_savepos_here(menu_item_t *it, char *buf, int len)
{
  savepos_here_text(buf, len);
}

static menu_t m_savepos;

static void build_savepos(void)
{
  menu_init(&m_savepos, "save position", 8);
  menu_item_t *show = menu_add_check(&m_savepos, "show slots", get_savepos_shown, set_savepos_shown, 0);
  if (show) {
    show->hint = "pink: slot a  green: slot b  cross: the one a save keeps  dark green: where the ocarina would move b";
    show->log_label = "save slots:";
  }
  menu_item_t *load = menu_add_check(&m_savepos, "where a load puts link", get_savepos_load, set_savepos_load, 0);
  if (load) {
    load->hint = "yellow: the game's search round the kept slot, run now";
    load->log_label = "load position:";
    load->indent = 2; /* under show slots */
  }
  menu_add_return(&m_savepos);
  /* the slots now: under the list, not part of it */
  menu_add_text(&m_savepos, text_savepos_slot, SAVEPOS_A);
  menu_add_text(&m_savepos, text_savepos_slot, SAVEPOS_B);
  menu_add_text(&m_savepos, text_savepos_load, 0);
  menu_add_text(&m_savepos, text_savepos_here, 0);
  m_savepos.footer = 4;
}

static void build_scene(void)
{
  build_savepos();
  build_voids();
  menu_init(&m_scene, "scene", 11);
  menu_item_t *reload = menu_add_action(&m_scene, "reload map", act_reload_map, 0);
  if (reload)
    reload->hint = "L: reload this map (at the age set in the link menu)";
  menu_item_t *end_cutscene = menu_add_action(&m_scene, "end cutscene", act_end_cutscene, 0);
  if (end_cutscene)
    end_cutscene->hint = "L: end the cutscene or event that's playing";
  menu_add_heading(&m_scene, "collision");
  menu_add_submenu(&m_scene, "environment", &m_collision);
  menu_add_submenu(&m_scene, "void out", &m_voids);
  menu_add_heading(&m_scene, "save and load");
  menu_add_submenu(&m_scene, "save position", &m_savepos);
  menu_add_heading(&m_scene, "enemies");
  menu_item_t *enemy_hp = menu_add_check(&m_scene, "display enemy health", get_enemy_hp, set_enemy_hp, 0);
  if (enemy_hp) {
    enemy_hp->hint = "a bar and life/max over each enemy";
    enemy_hp->log_label = "enemy health:";
  }
  menu_item_t *hp_xray = menu_add_check(&m_scene, "display through walls", get_enemy_hp_xray, set_enemy_hp_xray, 0);
  if (hp_xray) {
    hp_xray->hint = "also the bars of enemies behind the scenery (the game's style)";
    hp_xray->log_label = "enemy health through walls:";
    hp_xray->indent = 2; /* under enemy health */
  }
  menu_add_return(&m_scene);

  menu_init(&m_collision, "environment", 9);
  menu_add_heading(&m_collision, "air walls");
  menu_item_t *show = menu_add_check(&m_collision, "show", get_air_walls_shown, set_air_walls_shown, 0);
  if (show) {
    show->hint = "the kinds ticked under it   red: link is inside it";
    show->log_label = "air walls:";
  }
  /* show's options, nested under it */
  menu_item_t *push = menu_add_check(&m_collision, "that push link back", get_air_walls_push, set_air_walls_push, 0);
  if (push) {
    push->hint = "amber: he can't stand on them and is pushed off (forest temple ledge)";
    push->log_label = "air walls that push link back:";
    push->indent = 2;
  }
  menu_item_t *block = menu_add_check(&m_collision, "that stop link", get_air_walls_block, set_air_walls_block, 0);
  if (block) {
    block->hint = "cyan: invisible walls he can't go through (rocks in the lost woods)";
    block->log_label = "air walls that stop link:";
    block->indent = 2;
  }
  menu_item_t *all_kinds = menu_add_check(&m_collision, "other kinds", get_air_walls_all, set_air_walls_all, 0);
  if (all_kinds) {
    all_kinds->hint = "violet: walls for enemies, NPCs, animals, Epona, the camera, climbing";
    all_kinds->log_label = "other air walls:";
    all_kinds->indent = 2;
  }
  menu_item_t *xray = menu_add_check(&m_collision, "through link", get_air_walls_xray, set_air_walls_xray, 0);
  if (xray) {
    xray->hint = "also where link is in front of them (the game's style)";
    xray->log_label = "air walls through link:";
    xray->indent = 2;
  }
  menu_add_return(&m_collision);
  /* the map's walls and link's: under the list, not part of it */
  menu_add_text(&m_collision, text_air_wall_summary, 0);
  menu_add_text(&m_collision, text_air_wall_link, 0);
  m_collision.footer = 2;
}

/* --- settings --------------------------------------------------------------------------------- */

static menu_t m_settings, m_input_display, m_levitate, m_frame_advance;

/* menu settings */
#define OPACITY_STEP 10 /* percent per choice */
static const char *const opacity_names[] = { "0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%",
                                             "90%", "100%" };
static const char *const size_names[] = { "auto", "small (2x)", "medium (2.5x)", "large (3x)" };

static int get_size(menu_item_t *it)
{
  return gfx_menu_scale;
}

static void set_size(menu_item_t *it, int size)
{
  gfx_menu_scale = size;
}

static int get_opacity(menu_item_t *it)
{
  return gfx_opacity / OPACITY_STEP;
}

static void set_opacity(menu_item_t *it, int choice)
{
  gfx_opacity = choice * OPACITY_STEP;
}

/* the languages are found on the SD card after the menus are built: the switch reads them each time */
static int get_lang(menu_item_t *it)
{
  it->n_choices = lang_count();
  it->choices = lang_names();
  return lang_current();
}

static void set_lang(menu_item_t *it, int choice)
{
  lang_select(choice);
}

/* input display */
static const char *const input_style_names[] = { "row", "controller", "minimal" };
static const char *const input_size_names[] = { "small", "medium", "large" };
static const char *const input_attach_names[] = { "off", "above watches", "below watches" }; /* GFX_ATTACH_* */

static int get_input_attach(menu_item_t *it)
{
  return input_attach;
}

static void set_input_attach(menu_item_t *it, int choice)
{
  input_attach = choice;
}

static bool get_input_display(menu_item_t *it)
{
  return input_display;
}

static void set_input_display(menu_item_t *it, bool on)
{
  input_display = on;
}

static bool get_input_bg(menu_item_t *it)
{
  return input_display_bg;
}

static void set_input_bg(menu_item_t *it, bool on)
{
  input_display_bg = on;
}

static int get_input_style(menu_item_t *it)
{
  return input_style;
}

static void set_input_style(menu_item_t *it, int style)
{
  input_style = style;
}

static int get_input_corner(menu_item_t *it)
{
  return input_corner;
}

static void set_input_corner(menu_item_t *it, int corner)
{
  input_corner = corner;
}

static int get_input_size(menu_item_t *it)
{
  return input_size;
}

static void set_input_size(menu_item_t *it, int size)
{
  input_size = size;
}

static bool get_input_left(menu_item_t *it)
{
  return input_show_left;
}

static void set_input_left(menu_item_t *it, bool on)
{
  input_show_left = on;
}

static bool get_input_right(menu_item_t *it)
{
  return input_show_right;
}

static void set_input_right(menu_item_t *it, bool on)
{
  input_show_right = on;
}

static bool get_input_buttons(menu_item_t *it)
{
  return input_show_buttons;
}

static void set_input_buttons(menu_item_t *it, bool on)
{
  input_show_buttons = on;
}

/* levitate */
static const char *const lev_speed_names[] = { "0", "2", "4", "6", "8", "12", "20" };
static const int lev_speeds[] = { 0, 2, 4, 6, 8, 12, 20 };
static const char *const lev_move_names[] = { "2", "4", "6", "8", "12", "20", "30" };
static const int lev_moves[] = { 2, 4, 6, 8, 12, 20, 30 };

static bool get_levitate(menu_item_t *it)
{
  return levitate_enabled;
}

static void set_levitate(menu_item_t *it, bool on)
{
  levitate_enabled = on;
}

static bool get_walls(menu_item_t *it)
{
  return levitate_through_walls;
}

static void set_walls(menu_item_t *it, bool on)
{
  levitate_through_walls = on;
}

static int get_lev_speed(menu_item_t *it)
{
  for (int i = 0; i < ARRAY_LEN(lev_speeds); i++)
    if (lev_speeds[i] == levitate_speed)
      return i;
  return -1;
}

static void set_lev_speed(menu_item_t *it, int choice)
{
  levitate_speed = lev_speeds[choice];
}

static int get_lev_move(menu_item_t *it)
{
  for (int i = 0; i < ARRAY_LEN(lev_moves); i++)
    if (lev_moves[i] == levitate_move_speed)
      return i;
  return -1;
}

static void set_lev_move(menu_item_t *it, int choice)
{
  levitate_move_speed = lev_moves[choice];
}

static void text_levitate(menu_item_t *it, char *buf, int len)
{
  snprintf(buf, len, TF("status  %s"), levitate_state());
}

/* frame advance */

static bool get_fa(menu_item_t *it)
{
  return frame_advance;
}

static void set_fa(menu_item_t *it, bool on)
{
  frame_advance = on;
}

static bool get_fa_in_pause(menu_item_t *it)
{
  return frame_advance_in_pause;
}

static void set_fa_in_pause(menu_item_t *it, bool on)
{
  frame_advance_in_pause = on;
}

static bool get_hide_in_game_menus(menu_item_t *it)
{
  return hide_in_game_menus;
}

static void set_hide_in_game_menus(menu_item_t *it, bool on)
{
  hide_in_game_menus = on;
}

static bool get_hide_over_item_wheel(menu_item_t *it)
{
  return hide_over_item_wheel;
}

static void set_hide_over_item_wheel(menu_item_t *it, bool on)
{
  hide_over_item_wheel = on;
}

/* save management (savebackup.h): the backups after each save, archives, restoring */
static menu_t m_save_backups;

static bool get_save_backups(menu_item_t *it)
{
  return save_backup_enabled;
}

static void set_save_backups(menu_item_t *it, bool on)
{
  save_backup_enabled = on;
}

static bool get_save_backup_location(menu_item_t *it)
{
  return save_backup_location;
}

static void set_save_backup_location(menu_item_t *it, bool on)
{
  save_backup_location = on;
}

static const char *const save_kind_names[] = { "autosaves", "hard saves", "both" };

static bool get_hard_numbered(menu_item_t *it)
{
  return save_backup_hard_numbered;
}

static void set_hard_numbered(menu_item_t *it, bool on)
{
  save_backup_hard_numbered = on;
}

static int get_save_kinds(menu_item_t *it)
{
  return save_backup_kinds;
}

static void set_save_kinds(menu_item_t *it, int choice)
{
  save_backup_kinds = choice;
}

static void text_save_backups(menu_item_t *it, char *buf, int len)
{
  savebackup_status_text(buf, len);
}

static void archive_named(const char *text, intptr_t ctx)
{
  savebackup_archive_now(text);
}

/* archive now's line: the folder it puts them in, unless it's save-archives/ itself */
static void text_archive_now_to(menu_item_t *it, char *buf, int len)
{
  const char *folder = savebackup_archive_folder();
  if (folder[0])
    snprintf(buf, len, TF("to %s"), folder);
  else
    buf[0] = 0;
}

/* archive now: name it first (the location to start with), then copy */
static void act_archive_now(menu_item_t *it)
{
  osk_open("archive name", savebackup_location_name(), SAVE_ARCHIVE_NAME_MAX, archive_named, 0);
}

static int32_t get_archive_number(menu_item_t *it)
{
  return savebackup_next_number();
}

static void set_archive_number(menu_item_t *it, int32_t v)
{
  savebackup_set_next_number(v);
}

static int32_t max_archive_number(menu_item_t *it)
{
  return SAVE_ARCHIVE_NUMBER_MAX;
}

/* save management > manage archives: a small folder browser. Its two folders: named archives (save-archives/,
 * with folders of its own to make, open, delete, and choose as where archive now puts them) and automatic
 * archives (save-backups/). L on an archive opens it: restore or delete, each L then L again. */
static menu_t m_manage, m_named, m_automatic, m_archive, m_delete_folder;
static int named_count, named_folders; /* what the folder looked at holds (its last list) */
static int archive_open = -1;  /* the archive m_archive is for: its index in the list */
static int archive_armed = -1; /* the action armed by a first L: ARCHIVE_* */
enum { ARCHIVE_RESTORE, ARCHIVE_DELETE };

static void fill_named(void);
static void fill_automatic(void);

/* the folder's path, as it's shown: "save-archives/a/b" */
static void folder_text(char *buf, int len, const char *rel)
{
  snprintf(buf, len, rel[0] ? "save-archives/%s" : "save-archives", rel); /* not menu text */
}

/* armed: the cursor still on it (else disarmed); its text with "L again: " before it */
static bool armed_text(menu_t *m, menu_item_t *it, int action, const char *what, const char *armed_what, char *buf,
                       int len)
{
  if (archive_armed == action && &m->items[m->selected] != it)
    archive_armed = -1; /* the cursor moved off it */
  char expanded[96];
  snprintf(buf, len, "%s", binds_expand(T(archive_armed == action ? armed_what : what), expanded, sizeof(expanded)));
  return archive_armed == action;
}

/* first L arms it, a second does it */
static bool second_press(int action)
{
  if (archive_armed != action) {
    archive_armed = action;
    return false;
  }
  archive_armed = -1;
  return true;
}

/* --- an archive: restore, delete --- */

static void text_archive_name(menu_item_t *it, char *buf, int len)
{
  snprintf(buf, len, "%s", savebackup_archive_name(archive_open));
}

static void text_archive_action(menu_item_t *it, char *buf, int len)
{
  if (it->data == ARCHIVE_RESTORE)
    armed_text(&m_archive, it, ARCHIVE_RESTORE, "restore", "L again: restore", buf, len);
  else
    armed_text(&m_archive, it, ARCHIVE_DELETE, "delete", "L again: delete", buf, len);
}

static void act_archive_action(menu_item_t *it)
{
  if (!second_press((int)it->data))
    return;
  menu_t *list = m_archive.parent;
  if (it->data == ARCHIVE_RESTORE) {
    if (savebackup_restore(archive_open)) {
      menu_goto(&m_save_backups); /* done: back to save management */
      return;
    }
  } else {
    savebackup_delete(archive_open);
  }
  if (list == &m_named) /* the list has changed (or may have) */
    fill_named();
  else
    fill_automatic();
  menu_goto(list);
}

static void open_archive(menu_t *from, int i)
{
  archive_open = i;
  archive_armed = -1;
  m_archive.parent = from;
  m_archive.selected = 1; /* restore */
  menu_goto(&m_archive);
}

static void text_list_entry(menu_item_t *it, char *buf, int len)
{
  snprintf(buf, len, "%s", savebackup_archive_name((int)it->data));
}

static void text_empty_folder(menu_item_t *it, char *buf, int len)
{
  snprintf(buf, len, "%s", T("(empty)"));
}

/* --- named archives: a folder of them --- */

static void text_named_path(menu_item_t *it, char *buf, int len)
{
  folder_text(buf, len, savebackup_browsing());
}

static void act_named_entry(menu_item_t *it)
{
  int i = (int)it->data;
  if (i < savebackup_list_folders()) {
    if (savebackup_browse_into(i)) {
      fill_named();
      m_named.selected = 0;
      menu_refresh(&m_named);
    }
    return;
  }
  open_archive(&m_named, i);
}

static void act_folder_up(menu_item_t *it)
{
  savebackup_browse_up();
  fill_named();
}

static void folder_named(const char *text, intptr_t ctx)
{
  if (savebackup_make_folder(text))
    fill_named();
}

static void act_new_folder(menu_item_t *it)
{
  osk_open("folder name", "", SAVE_ARCHIVE_NAME_MAX, folder_named, 0);
}

static void act_archive_here(menu_item_t *it)
{
  savebackup_archive_here();
}

static void archived_into_folder(const char *text, intptr_t ctx)
{
  savebackup_archive_here_now(text);
  fill_named(); /* the new archive in the list */
}

/* archive now, from the folder's buttons: into the folder looked at */
static void act_archive_into_folder(menu_item_t *it)
{
  osk_open("archive name", savebackup_location_name(), SAVE_ARCHIVE_NAME_MAX, archived_into_folder, 0);
}

/* delete this folder: a screen of its own, the warning and no / yes, no first (back: no) */
#define DELETE_NO_ITEM 4 /* m_delete_folder's: three lines of warning, a blank, then no */

static void text_delete_warning(menu_item_t *it, char *buf, int len)
{
  char where[SAVE_FOLDER_LEN + 16];
  int archives = named_count - named_folders;
  switch (it->data) {
    case 0:
      folder_text(where, sizeof(where), savebackup_browsing());
      snprintf(buf, len, TF("delete %s"), where);
      break;
    case 1:
      snprintf(buf, len, TF("and everything in it: %d archive%s, %d folder%s (with what they hold)"), archives,
               archives == 1 ? "" : "s", named_folders, named_folders == 1 ? "" : "s");
      break;
    default:
      snprintf(buf, len, "%s", T("this can't be undone"));
      break;
  }
}

static void act_delete_no(menu_item_t *it)
{
  gfx_logf("%s", T("folder kept"));
  menu_goto(&m_named);
}

static void act_delete_yes(menu_item_t *it)
{
  savebackup_delete_folder(); /* then up a folder, or (failed) still on it; its notice says which */
  fill_named();
  m_named.selected = 0;
  menu_refresh(&m_named);
  menu_goto(&m_named);
}

static void act_delete_folder(menu_item_t *it)
{
  m_delete_folder.selected = DELETE_NO_ITEM;
  menu_goto(&m_delete_folder);
}

/* the folder's buttons, one row of icons (as the watches' folder and +), each said in the hint: up a folder,
 * new folder, save new archives here, delete this folder (up and delete not at the top). Picked with
 * left/right, pressed with select. */
enum { FOLDER_UP, FOLDER_NEW, FOLDER_ARCHIVE, FOLDER_HERE, FOLDER_DELETE, FOLDER_BUTTONS };
static int folder_buttons[FOLDER_BUTTONS], n_folder_buttons;
static const char *folder_hints[FOLDER_BUTTONS];
static const char *const folder_button_hint[FOLDER_BUTTONS] = {
  [FOLDER_UP] = "up a folder: to the one this is in",
  [FOLDER_NEW] = "new folder: L names a folder, made in this one",
  [FOLDER_ARCHIVE] = "archive now: L names it, then copies the save into this folder (its next number)",
  [FOLDER_HERE] = "save new archives here: L, and archive now and numbered hard saves put them in this folder",
  [FOLDER_DELETE] = "delete this folder: asks first, then deletes it and every archive and folder in it",
};
static const char *const here_now_hint = "save new archives here: they go here now";
static const char folder_icons[FOLDER_BUTTONS] = {
  [FOLDER_UP] = GFX_ICON_FOLDER_UP, [FOLDER_NEW] = GFX_ICON_FOLDER_ADD, [FOLDER_ARCHIVE] = GFX_ICON_SAVE_ADD,
  [FOLDER_HERE] = GFX_ICON_SAVE,
  [FOLDER_DELETE] = GFX_ICON_FOLDER_REMOVE,
};

/* new archives go to the folder looked at */
static bool archives_go_here(void)
{
  const char *a = savebackup_archive_folder(), *b = savebackup_browsing();
  while (*a && *a == *b)
    a++, b++;
  return *a == *b;
}

static void folder_row_text(menu_item_t *it, char *buf, int len)
{
  int n = 0;
  bool here = archives_go_here();
  for (int i = 0; i < n_folder_buttons && n + 4 < len; i++) {
    int button = folder_buttons[i];
    folder_hints[i] = button == FOLDER_HERE && here ? here_now_hint : folder_button_hint[button];
    char icon = button == FOLDER_HERE && here ? GFX_ICON_SAVE_HERE : folder_icons[button];
    n += snprintf(buf + n, len - n, "%s%c%c", i ? " " : "", icon, GFX_WIDE_REST);
  }
  it->hint = n_folder_buttons == 1 ? folder_hints[0] : 0; /* one button: no parts, its hint is the item's */
}

static bool folder_row_cursor(menu_item_t *it, const char *line, int *start, int *len)
{
  *start = 3 * (it->part < n_folder_buttons ? it->part : 0); /* each two cells and a space */
  *len = 2;
  return true;
}

static void folder_row_press(menu_item_t *it, int part)
{
  switch (part < n_folder_buttons ? folder_buttons[part] : FOLDER_NEW) {
    case FOLDER_UP: act_folder_up(it); break;
    case FOLDER_ARCHIVE: act_archive_into_folder(it); break;
    case FOLDER_HERE: act_archive_here(it); break;
    case FOLDER_DELETE: act_delete_folder(it); break;
    default: act_new_folder(it); break;
  }
}

/* the folder looked at: where it is; what to do with it; then what's in it */
static void fill_named(void)
{
  int n = savebackup_list_named(), folders = savebackup_list_folders();
  named_count = n, named_folders = folders;
  bool top = !savebackup_browsing()[0];
  archive_armed = -1;
  menu_reset(&m_named);
  menu_item_t *path = menu_add_text(&m_named, text_named_path, 0);
  if (path)
    path->plain = true;
  n_folder_buttons = 0;
  if (!top)
    folder_buttons[n_folder_buttons++] = FOLDER_UP;
  folder_buttons[n_folder_buttons++] = FOLDER_NEW;
  folder_buttons[n_folder_buttons++] = FOLDER_ARCHIVE;
  folder_buttons[n_folder_buttons++] = FOLDER_HERE;
  if (!top)
    folder_buttons[n_folder_buttons++] = FOLDER_DELETE;
  menu_item_t *buttons = menu_add_action(&m_named, "", 0, 0);
  if (buttons) {
    buttons->text = folder_row_text;
    buttons->cursor = folder_row_cursor;
    buttons->press = folder_row_press;
    buttons->n_parts = n_folder_buttons;
    buttons->part = top ? 0 : 1; /* on new folder */
    buttons->part_hints = folder_hints;
    buttons->grid = true; /* the icons on gz's grid, so the cursor's columns are theirs */
    buttons->log_label = "";
    for (int i = 0; i < n_folder_buttons; i++)
      folder_hints[i] = folder_button_hint[folder_buttons[i]];
    buttons->hint = folder_hints[0];
  }
  menu_add_heading(&m_named, "in it");
  for (int i = 0; i < n; i++) {
    menu_item_t *row = menu_add_action(&m_named, "", act_named_entry, i);
    if (!row)
      break;
    row->text = text_list_entry;
    row->log_label = "";
    if (i < folders) {
      row->icon = GFX_ICON_FOLDER;
      row->hint = "L: open this folder";
    } else {
      row->hint = "L: restore or delete this archive";
    }
  }
  if (!n)
    menu_add_text(&m_named, text_empty_folder, 0);
  menu_end_group(&m_named);
  menu_add_return(&m_named);
  /* the folder's path and buttons stay at the top and its return at the bottom; what's in it scrolls between,
   * with the list's scroll bar beside it */
  m_named.pinned = 3; /* the path, the buttons, "in it" */
  m_named.pinned_end = 1;
  m_named.scroll = 0;
  menu_refresh(&m_named);
}

/* --- automatic archives --- */

static void act_automatic_entry(menu_item_t *it)
{
  open_archive(&m_automatic, (int)it->data);
}

static void fill_automatic(void)
{
  int n = savebackup_list_automatic();
  menu_reset(&m_automatic);
  menu_item_t *path = menu_add_text(&m_automatic, text_static, (intptr_t) "save-backups, newest first");
  if (path)
    path->plain = true;
  for (int i = 0; i < n; i++) {
    menu_item_t *row = menu_add_action(&m_automatic, "", act_automatic_entry, i);
    if (!row)
      break;
    row->text = text_list_entry;
    row->hint = "L: restore or delete this archive";
    row->log_label = "";
  }
  if (!n)
    menu_add_text(&m_automatic, text_empty_folder, 0);
  menu_add_return(&m_automatic);
  m_automatic.pinned = 1; /* its line at the top, its return at the bottom; the archives scroll between */
  m_automatic.pinned_end = 1;
  m_automatic.scroll = 0;
  menu_refresh(&m_automatic);
}

/* --- manage archives: its two folders --- */

static void act_open_named(menu_item_t *it)
{
  savebackup_browse_top();
  fill_named();
  m_named.selected = 0;
  menu_refresh(&m_named);
  menu_goto(&m_named);
}

static void act_open_automatic(menu_item_t *it)
{
  fill_automatic();
  m_automatic.selected = 0;
  menu_refresh(&m_automatic);
  menu_goto(&m_automatic);
}

static void text_archives_go(menu_item_t *it, char *buf, int len)
{
  char where[SAVE_FOLDER_LEN + 16];
  folder_text(where, sizeof(where), savebackup_archive_folder());
  snprintf(buf, len, TF("new archives go to: %s"), where);
}

static void act_open_archives(menu_item_t *it)
{
  menu_goto(&m_manage);
}

static void build_archive_menus(void)
{
  menu_init(&m_delete_folder, "delete this folder?", 8);
  m_delete_folder.parent = &m_named; /* back: no */
  for (int line = 0; line < 3; line++) {
    menu_item_t *warning = menu_add_text(&m_delete_folder, text_delete_warning, line);
    if (warning)
      warning->plain = true;
  }
  add_blank_line(&m_delete_folder);
  menu_item_t *no = menu_add_action(&m_delete_folder, "no, keep it", act_delete_no, 0);
  if (no) {
    no->hint = "L: back to the folder, nothing deleted";
    no->log_label = "";
  }
  menu_item_t *yes = menu_add_action(&m_delete_folder, "yes, delete it", act_delete_yes, 0);
  if (yes) {
    yes->hint = "L: delete the folder and everything in it from the SD card";
    yes->log_label = "";
  }

  menu_init(&m_manage, "manage archives", 5);
  m_manage.parent = &m_save_backups;
  menu_item_t *named = menu_add_action(&m_manage, "named archives", act_open_named, 0);
  if (named) {
    named->icon = GFX_ICON_FOLDER;
    named->hint = "L: open save-archives (archive now's, in folders of your own)";
    named->log_label = "";
  }
  menu_item_t *automatic = menu_add_action(&m_manage, "automatic archives", act_open_automatic, 0);
  if (automatic) {
    automatic->icon = GFX_ICON_FOLDER;
    automatic->hint = "L: open save-backups (the automatic ones after each save, the newest 100)";
    automatic->log_label = "";
  }
  menu_add_return(&m_manage);
  menu_add_text(&m_manage, text_archives_go, 0);
  m_manage.footer = 1;

  /* the list of a folder: its path, 2 headings, 3 actions, "..", what's in it, return */
  menu_init(&m_named, "named archives", SAVE_ARCHIVES_LISTED + 9);
  m_named.parent = &m_manage;
  menu_init(&m_automatic, "automatic archives", SAVE_ARCHIVES_LISTED + 3);
  m_automatic.parent = &m_manage;

  menu_init(&m_archive, "archive", 5);
  m_archive.parent = &m_named;
  menu_item_t *name = menu_add_text(&m_archive, text_archive_name, 0);
  if (name)
    name->plain = true;
  menu_item_t *restore = menu_add_action(&m_archive, "", act_archive_action, ARCHIVE_RESTORE);
  if (restore) {
    restore->text = text_archive_action;
    restore->hint = "L, then L again: archive the save as it is, then put this one in its place (options kept); "
                    "then load it from the game's Load screen";
    restore->log_label = ""; /* savebackup.c logs it */
  }
  menu_item_t *del = menu_add_action(&m_archive, "", act_archive_action, ARCHIVE_DELETE);
  if (del) {
    del->text = text_archive_action;
    del->hint = "L, then L again: delete this archive from the SD card (it can't be undone)";
    del->log_label = "";
  }
  menu_add_return(&m_archive);
}

/* save management > automatic save archive: the copies made after the game's saves */
static menu_t m_auto_archive;

static void build_auto_archive(void)
{
  menu_init(&m_auto_archive, "automatic save archive", 7);
  menu_add_heading(&m_auto_archive, "automatic save archive options");
  menu_item_t *backups = menu_add_check(&m_auto_archive, "enable", get_save_backups, set_save_backups, 0);
  if (backups) {
    backups->hint = "after a save, copy the game's save folder to save-backups/ on the SD card (the newest 100 kept)";
    backups->log_label = "automatic save archive:";
  }
  menu_item_t *kinds = menu_add_option(&m_auto_archive, "back up option", ARRAY_LEN(save_kind_names),
                                       save_kind_names, get_save_kinds, set_save_kinds, 0);
  if (kinds) {
    kinds->indent = 2; /* under enable */
    kinds->hint = "autosaves (loading zones, events), hard saves (the save screen), or both";
    kinds->log_label = "automatic save archive of";
  }
  menu_item_t *numbered = menu_add_check(&m_auto_archive, "hard saves numbered", get_hard_numbered,
                                         set_hard_numbered, 0);
  if (numbered) {
    numbered->indent = 2;
    numbered->hint = "each hard save becomes the next \"#008 - <location> - <date>\" where new archives go (as "
                     "archive now); off: dated, with the autosaves";
    numbered->log_label = "hard saves numbered:";
  }
  menu_item_t *where = menu_add_check(&m_auto_archive, "location in folder name", get_save_backup_location,
                                      set_save_backup_location, 0);
  if (where) {
    where->indent = 2;
    where->hint = "the dated ones: the save's location after the date, e.g. \"<date> - Temple of Time\"";
    where->log_label = "location in folder name:";
  }
  menu_end_group(&m_auto_archive);
  menu_add_return(&m_auto_archive);
}

static void build_save_backups(void)
{
  menu_init(&m_save_backups, "save management", 10);
  build_auto_archive();
  menu_item_t *automatic = menu_add_submenu(&m_save_backups, "automatic save archive", &m_auto_archive);
  if (automatic)
    automatic->hint = "the copies made after each of the game's saves: on, which saves, numbered hard saves";
  menu_add_heading(&m_save_backups, "save archives");
  menu_item_t *manage = menu_add_action(&m_save_backups, "manage archives >", act_open_archives, 0);
  if (manage) {
    manage->hint = "the archives on the SD card: restore one in place of the game's save, or delete it";
    manage->log_label = "";
  }
  menu_item_t *now = menu_add_action(&m_save_backups, "archive now", act_archive_now, 0);
  if (now) {
    now->text = text_archive_now_to;
    now->hint = "name it, then copy the save folder as the game last saved it to save-archives/ (never deleted)";
    now->log_label = ""; /* savebackup_frame reports it */
  }
  menu_item_t *number = menu_add_number(&m_save_backups, "next number", get_archive_number, set_archive_number,
                                        max_archive_number, 0, 0);
  if (number) {
    number->num_min = 1;
    number->indent = 2; /* archive now's */
    number->hint = "the next archive's #: one more than the highest so far, then up by one each time";
    number->log_label = "next archive number";
  }
  menu_end_group(&m_save_backups);
  menu_add_return(&m_save_backups);
  menu_add_text(&m_save_backups, text_save_backups, 0);
  m_save_backups.footer = 1;

  build_archive_menus();
}

static bool get_pause_display(menu_item_t *it)
{
  return pause_display;
}

static void set_pause_display(menu_item_t *it, bool on)
{
  pause_display = on;
}

static void build_frame_advance(void)
{
  menu_init(&m_frame_advance, "frame advance", 5);
  menu_add_heading(&m_frame_advance, "frame advance options");
  menu_item_t *enabled = menu_add_check(&m_frame_advance, "enabled", get_fa, set_fa, 0);
  if (enabled) {
    enabled->hint = "in game, tap {advance}: pause; tap: a frame; hold: frames; {run}: run (settings > rebind commands)";
    enabled->log_label = "frame advance";
  }
  menu_item_t *in_pause = menu_add_check(&m_frame_advance, "in pause screen", get_fa_in_pause, set_fa_in_pause, 0);
  if (in_pause) {
    in_pause->hint = "lets {advance} start frame advance in the game's pause screen (off: the game gets it)";
    in_pause->log_label = "frame advance in pause screen";
  }
  menu_item_t *pause_icon = menu_add_check(&m_frame_advance, "pause icon", get_pause_display, set_pause_display, 0);
  if (pause_icon)
    pause_icon->hint = "gz's pause display at the top left while paused";
  menu_end_group(&m_frame_advance);
  menu_add_return(&m_frame_advance);
}

static void build_input_display(void)
{
  menu_init(&m_input_display, "input display", 13);
  menu_add_heading(&m_input_display, "display options");
  menu_item_t *show = menu_add_check(&m_input_display, "show", get_input_display, set_input_display, 0);
  if (show) {
    show->hint = "the sticks and the buttons held (hidden while the menu is open)";
    show->log_label = "input display";
  }
  /* show's options, under it */
  int first_option = m_input_display.n_items;
  menu_item_t *background = menu_add_check(&m_input_display, "background", get_input_bg, set_input_bg, 0);
  if (background)
    background->hint = "behind the whole input display, as the watches have";
  menu_add_check(&m_input_display, "left stick", get_input_left, set_input_left, 0);
  menu_add_check(&m_input_display, "right stick", get_input_right, set_input_right, 0);
  menu_add_check(&m_input_display, "buttons", get_input_buttons, set_input_buttons, 0);
  menu_item_t *style = menu_add_option(&m_input_display, "style", ARRAY_LEN(input_style_names), input_style_names,
                                       get_input_style, set_input_style, 0);
  if (style) {
    style->hint = "row: gz's line   controller: a Pro Controller   minimal: its outlines in one short line";
    style->log_label = "input display style";
  }
  menu_item_t *size = menu_add_option(&m_input_display, "size", ARRAY_LEN(input_size_names), input_size_names,
                                      get_input_size, set_input_size, 0);
  if (size) {
    size->hint = "small, medium, large: half, three quarters or all of the menu's text size";
    size->log_label = "input display size";
  }
  menu_item_t *attach = menu_add_option(&m_input_display, "attach to watches", ARRAY_LEN(input_attach_names),
                                        input_attach_names, get_input_attach, set_input_attach, 0);
  if (attach) {
    attach->hint = "join it to the watches box, just above or below it at the same width: the two go where the "
                   "watches go (while the watches show)";
    attach->log_label = "input display attached";
  }
  menu_item_t *position = menu_add_option(&m_input_display, "position", GFX_NPLACES, watches_corner_names,
                                          get_input_corner, set_input_corner, 0);
  if (position) {
    position->hint = "where it goes on its own (not attached, or with the watches hidden)";
    position->log_label = "input display position";
  }
  menu_item_t *move = menu_add_action(&m_input_display, "move input display position", act_move_panel, MOVE_INPUT);
  if (move) {
    move->edit = move_panel;
    move->edit_alone = true;
    move->hint = "L: move the input display around the screen";
    move->edit_hint = "input display: d-pad moves it   L: done";
  }
  for (int i = first_option; i < m_input_display.n_items; i++)
    m_input_display.items[i].indent = 2;
  menu_end_group(&m_input_display);
  menu_item_t *back = menu_add_return(&m_input_display);
  if (back)
    back->indent = 1; /* level with show */
}

static void build_levitate(void)
{
  menu_init(&m_levitate, "levitate", 9);
  menu_add_heading(&m_levitate, "levitation options");
  menu_item_t *lv = menu_add_check(&m_levitate, "enabled", get_levitate, set_levitate, 0);
  if (lv)
    lv->hint = "{levitate}: toggle   {rise}: up   {sink}: down   {hover} or release: hover", lv->log_label = "levitate";
  /* enabled's options, under it */
  menu_item_t *walls = menu_add_check(&m_levitate, "through walls", get_walls, set_walls, 0);
  if (walls)
    walls->indent = 2, walls->hint = "Disable Link collision while floating; B descends through floors";
  menu_item_t *rise = menu_add_option(&m_levitate, "vertical speed", 7, lev_speed_names, get_lev_speed, set_lev_speed, 0);
  if (rise)
    rise->indent = 2, rise->hint = "{rise}/{sink} speed; legacy 0 uses 4. Release to hover";
  menu_item_t *move = menu_add_option(&m_levitate, "move speed", 7, lev_move_names, get_lev_move, set_lev_move, 0);
  if (move)
    move->indent = 2;
  menu_end_group(&m_levitate);
  menu_add_return(&m_levitate);
  /* what can only be read, under return after a blank line */
  menu_add_text(&m_levitate, text_static, (intptr_t) "");
  menu_add_text(&m_levitate, text_levitate, 0);
}

/* settings > rebind commands (gz's commands): each command and its bind (binds.h); L on one records a new bind */
static menu_t m_commands;

static void text_command(menu_item_t *it, char *buf, int len)
{
  command_t cmd = (command_t)it->data;
  char name[48];
  if (binds_recording(cmd))
    snprintf(name, sizeof(name), "%s", binds_record_text());
  else
    bind_name(binds[cmd], name, sizeof(name));
  snprintf(buf, len, "< %s >", name);
}

static bool edit_command(menu_item_t *it)
{
  bool going = binds_record_frame();
  it->edit_hint = binds_record_hint(); /* the step it's at */
  return going;
}

static void act_command(menu_item_t *it)
{
  binds_record_start((command_t)it->data);
  menu_edit(it);
}

static void act_commands_default(menu_item_t *it)
{
  binds_reset();
  gfx_notice("commands: back to their defaults");
}

static void build_commands(void)
{
  menu_init(&m_commands, "rebind commands", CMD_COUNT + CMD_GROUPS + 2);
  for (int group = 0; group < CMD_GROUPS; group++) {
    menu_add_heading(&m_commands, command_group_names[group]);
    for (int c = 0; c < CMD_COUNT; c++) {
      if (command_info[c].group != group)
        continue;
      menu_item_t *row = menu_add_action(&m_commands, command_info[c].name, act_command, c);
      if (!row)
        continue;
      row->text = text_command;
      row->edit = edit_command;
      row->edit_alone = false;
      row->log_label = ""; /* binds.c logs the new bind */
      row->hint = command_info[c].required
                    ? "L: rebind (press the new buttons in order, let go, then L to keep it; needed: can't be unbound)"
                    : "L: rebind (press the new buttons in order, let go, then L to keep it)   L held: unbound";
      row->edit_hint = binds_record_hint();
    }
  }
  menu_end_group(&m_commands);
  menu_item_t *defaults = menu_add_action(&m_commands, "restore defaults", act_commands_default, 0);
  if (defaults)
    defaults->hint = "L: every command back to its default (gz's buttons)";
  menu_add_return(&m_commands);
}

/* settings > menu settings */
static menu_t m_menu_settings;

static bool get_switch_keyboard(menu_item_t *it)
{
  return swkbd_enabled;
}

static void set_switch_keyboard(menu_item_t *it, bool on)
{
  swkbd_enabled = on;
}

static void build_menu_settings(void)
{
  menu_init(&m_menu_settings, "menu settings", 9);
  menu_item_t *language = menu_add_option(&m_menu_settings, "language", 1, lang_names(), get_lang, set_lang, 0);
  if (language)
    language->hint = "files in sd:/sloperinaGZ/lang (see lang/template.json)";
  menu_item_t *size = menu_add_option(&m_menu_settings, "size", ARRAY_LEN(size_names), size_names, get_size, set_size, 0);
  if (size)
    size->hint = "text size of the menu, watches and log (auto: for the screen)";
  menu_item_t *opacity = menu_add_option(&m_menu_settings, "background opacity", ARRAY_LEN(opacity_names), opacity_names,
                                         get_opacity, set_opacity, 0);
  if (opacity)
    opacity->hint = "the backdrop's opacity";
  menu_item_t *hide = menu_add_check(&m_menu_settings, "hide in game menus", get_hide_in_game_menus,
                                     set_hide_in_game_menus, 0);
  if (hide)
    hide->hint = "no watches, input display or log over the game's pause screen and map";
  menu_item_t *wheel = menu_add_check(&m_menu_settings, "hide over item wheel", get_hide_over_item_wheel,
                                      set_hide_over_item_wheel, 0);
  if (wheel)
    wheel->hint = "no watches, input display or log while the game's item wheel is up";
  menu_item_t *keyboard = menu_add_check(&m_menu_settings, "switch keyboard", get_switch_keyboard,
                                         set_switch_keyboard, 0);
  if (keyboard) {
    keyboard->hint = "name things with the Switch's own keyboard (stick, L/R cursor, B delete, + done; the game "
                     "waits meanwhile); off: sloperinaGZ's";
    keyboard->log_label = "switch keyboard:";
  }
  menu_item_t *header = menu_add_check(&m_menu_settings, "watch box header", get_header, set_header, 0);
  if (header) {
    header->hint = "a small \"watches\" header over the watches box, underlined";
    header->log_label = "watch box header";
  }
  menu_add_return(&m_menu_settings);
}

static void build_settings(void)
{
  menu_init(&m_settings, "settings", 7);
  menu_add_submenu(&m_settings, "menu settings", &m_menu_settings);
  menu_add_submenu(&m_settings, "input display", &m_input_display);
  menu_add_submenu(&m_settings, "levitate", &m_levitate);
  menu_add_submenu(&m_settings, "frame advance", &m_frame_advance);
  menu_add_submenu(&m_settings, "rebind commands", &m_commands);
  menu_add_return(&m_settings);

  build_menu_settings();
  build_commands();
  build_frame_advance();
  build_input_display();
  build_levitate();
}

/* --- debug (gz's debug menu) ----------------------------------------------------------------------- */

static menu_t m_debug, m_tests;

/* debug > heap: gz shows its game's arena; here, what the console says of the game's memory
 * (svcGetInfo) and how much of the heap region is mapped (svcQueryMemory, refreshed every second). */
static menu_t m_heap;

/* svcGetInfo's info types, and svcQueryMemory's MemoryType for heap */
#define INFO_HEAP_REGION_ADDRESS 4
#define INFO_HEAP_REGION_SIZE    5
#define INFO_TOTAL_MEMORY_SIZE   6
#define INFO_USED_MEMORY_SIZE    7
#define MEMORY_TYPE_HEAP         5
#define HEAP_REFRESH_FRAMES      30

static struct {
  uint64_t start, size;     /* the heap region */
  uint64_t mapped, blocks;  /* how much of it is mapped, in how many blocks */
  uint64_t total, used;     /* the game's memory */
  uint32_t updated_at;      /* frame_counter() */
  bool ok;                  /* the region is known */
} heap;

static void heap_update(void)
{
  uint32_t now = frame_counter();
  if (heap.ok && now - heap.updated_at < HEAP_REFRESH_FRAMES)
    return;
  heap.updated_at = now;
  heap.ok = sys_get_info(INFO_HEAP_REGION_ADDRESS, 0, &heap.start) &&
            sys_get_info(INFO_HEAP_REGION_SIZE, 0, &heap.size);
  if (!sys_get_info(INFO_TOTAL_MEMORY_SIZE, 0, &heap.total))
    heap.total = 0;
  if (!sys_get_info(INFO_USED_MEMORY_SIZE, 0, &heap.used))
    heap.used = 0;
  heap.mapped = 0;
  heap.blocks = 0;
  for (uint64_t addr = heap.start; heap.ok && addr < heap.start + heap.size;) {
    sys_meminfo_t info;
    if (sys_query_memory(&info, addr) != 0 || info.size == 0)
      break;
    if ((info.type & 0xFF) == MEMORY_TYPE_HEAP) {
      heap.mapped += info.size;
      heap.blocks++;
    }
    addr = info.addr + info.size;
  }
}

/* bytes as MiB with one decimal */
static void format_mib(uint64_t bytes, char *buf, int len)
{
  snprintf(buf, len, "%llu.%01llu MiB", (unsigned long long)(bytes >> 20),
           (unsigned long long)((bytes & 0xFFFFF) * 10 >> 20));
}

/* it->data: which line */
enum { HEAP_LINE_REGION, HEAP_LINE_MAPPED, HEAP_LINE_MEMORY, HEAP_LINE_FREE, N_HEAP_LINES };

static void text_heap(menu_item_t *it, char *buf, int len)
{
  heap_update();
  char first[24], second[24];
  switch (it->data) {
    case HEAP_LINE_REGION:
      if (!heap.ok) {
        snprintf(buf, len, "%s", T("heap region   unknown"));
      } else {
        format_mib(heap.size, first, sizeof(first));
        snprintf(buf, len, TF("heap region   %010llx  %s"), (unsigned long long)heap.start, first);
      }
      break;
    case HEAP_LINE_MAPPED:
      format_mib(heap.mapped, first, sizeof(first));
      snprintf(buf, len, TF("heap in use   %s in %d blocks"), first, (int)heap.blocks);
      break;
    case HEAP_LINE_MEMORY:
      format_mib(heap.total, first, sizeof(first));
      format_mib(heap.used, second, sizeof(second));
      snprintf(buf, len, TF("memory        %s of %s used"), second, first);
      break;
    default: /* HEAP_LINE_FREE */
      format_mib(heap.total > heap.used ? heap.total - heap.used : 0, first, sizeof(first));
      snprintf(buf, len, TF("memory free   %s"), first);
      break;
  }
}

static void act_heap_memory(menu_item_t *it)
{
  heap_update();
  if (heap.ok)
    memview_open((uintptr_t)heap.start, MW_X64);
}

/* debug > pause research: the engine's own pause state (Sequence_* GameData), to find how the game
 * stops its world while still drawing (its pause screen), for frame advance. Shows the values, lets them
 * be changed, and can log every change (also with the menu closed: menus_research_frame). */
static menu_t m_pause_research;
static const struct { const char *name; uint32_t hash; bool is_int; } pause_fields[] = {
  { "IsPause", 0x604AEBE1u, false }, { "GamePauseRequestCount", 0x64A61098u, true },
  { "PreGamePauseRequestCount", 0xEF935C2Du, true }, { "PostGamePauseRequestCount", 0xE8D033EAu, true },
  { "IsPreGamePause", 0xBF5BF25Fu, false }, { "IsPostGamePause", 0xC2F987DBu, false },
  { "IsSceneTransitionPause", 0x9A9506E7u, false }, { "IsEventBancChangePause", 0xF96989B6u, false },
  { "IsChangingBancFromRequestToPause", 0xF8633C7Fu, false },
};
#define N_PAUSE_FIELDS ARRAY_LEN(pause_fields)
static bool pause_log;
static int32_t pause_last[N_PAUSE_FIELDS];
static bool pause_seen; /* pause_last holds values (logging is on and a frame has passed) */

static int32_t pause_value(int i)
{
  if (pause_fields[i].is_int) {
    int32_t value;
    return gd_get_int(pause_fields[i].hash, &value) ? value : NUM_UNKNOWN;
  }
  bool flag;
  return gd_get_bool(pause_fields[i].hash, &flag) ? flag : NUM_UNKNOWN;
}

/* Logged with them, not in the menu: what might tell the item wheel (ShortCut) from the pause screen and map,
 * which raise the same pause (FINDINGS §9.1). 'b' Bool, 'e' Enum (its murmur3 hash), 'f' Float. */
static const struct { const char *name; uint32_t hash; char type; } research_extra[] = {
  { "UI_IsNeedOpenShortCut", 0xE596D2BBu, 'b' }, { "UI_IsShortCutBCancel", 0x16BF1503u, 'b' },
  { "UI_CurrentShortCutCategory", 0x2852C25Cu, 'e' }, { "Sequence_ShortCutOpenHoldCounter", 0x28CB0DE7u, 'f' },
  { "Sequence_IsDisableTransitionPlusMenu", 0x9B24B37Cu, 'b' }, { "menu screen (0x578D41E1)", 0x578D41E1u, 'e' },
};
#define N_RESEARCH_EXTRA ARRAY_LEN(research_extra)
static uint32_t extra_last[N_RESEARCH_EXTRA]; /* the raw bits (a float's too); NUM_UNKNOWN unreadable */

static uint32_t extra_value(int i)
{
  bool flag;
  uint32_t value;
  float f;
  switch (research_extra[i].type) {
    case 'b': return gd_get_bool(research_extra[i].hash, &flag) ? flag : (uint32_t)NUM_UNKNOWN;
    case 'e': return gd_get_enum(research_extra[i].hash, &value) ? value : (uint32_t)NUM_UNKNOWN;
    default:
      if (!gd_get_float(research_extra[i].hash, &f))
        return (uint32_t)NUM_UNKNOWN;
      memcpy(&value, &f, sizeof(value));
      return value;
  }
}

static void extra_text(int i, uint32_t raw, char *buf, int len)
{
  float f;
  if (raw == (uint32_t)NUM_UNKNOWN)
    snprintf(buf, len, "??");
  else if (research_extra[i].type == 'e')
    snprintf(buf, len, "%08x", (unsigned)raw);
  else if (research_extra[i].type == 'f')
    memcpy(&f, &raw, sizeof(f)), snprintf(buf, len, "%.2f", f);
  else
    snprintf(buf, len, "%u", (unsigned)raw);
}

void menus_research_frame(void)
{
  if (!pause_log)
    return;
  for (int i = 0; i < N_PAUSE_FIELDS; i++) {
    int32_t value = pause_value(i);
    if (pause_seen && value != pause_last[i])
      log_printf("pause research: frame %u: %s %d -> %d", (unsigned)frame_counter(), pause_fields[i].name,
                 (int)pause_last[i], (int)value);
    pause_last[i] = value;
  }
  for (int i = 0; i < N_RESEARCH_EXTRA; i++) {
    uint32_t value = extra_value(i);
    if (pause_seen && value != extra_last[i]) {
      char was[24], now[24];
      extra_text(i, extra_last[i], was, sizeof(was));
      extra_text(i, value, now, sizeof(now));
      log_printf("pause research: frame %u: %s %s -> %s", (unsigned)frame_counter(), research_extra[i].name, was, now);
    }
    extra_last[i] = value;
  }
  pause_seen = true;
}

static bool get_pause_log(menu_item_t *it)
{
  return pause_log;
}

static void set_pause_log(menu_item_t *it, bool on)
{
  pause_log = on;
  pause_seen = false;
}

/* it->data: the field (pause_fields) */
static int32_t get_pause_int(menu_item_t *it)
{
  return pause_value((int)it->data);
}

static void set_pause_int(menu_item_t *it, int32_t value)
{
  gd_set_int(pause_fields[it->data].hash, value);
}

static bool get_pause_bool(menu_item_t *it)
{
  return pause_value((int)it->data) == 1;
}

static void set_pause_bool(menu_item_t *it, bool on)
{
  gd_set_bool(pause_fields[it->data].hash, on);
}

static void build_pause_research(void)
{
  menu_init(&m_pause_research, "pause research", N_PAUSE_FIELDS + 4);
  menu_item_t *log_changes = menu_add_check(&m_pause_research, "log changes", get_pause_log, set_pause_log, 0);
  if (log_changes)
    log_changes->hint = "every change of these to the log, also with the menu closed (open the game's pause screen)";
  menu_add_heading(&m_pause_research, "Sequence_");
  for (int i = 0; i < N_PAUSE_FIELDS; i++) {
    const char *name = pause_fields[i].name;
    menu_item_t *field;
    if (pause_fields[i].is_int)
      field = menu_add_number(&m_pause_research, name, get_pause_int, set_pause_int, 0, 0, i);
    else
      field = menu_add_check(&m_pause_research, name, get_pause_bool, set_pause_bool, i);
    if (field)
      field->hint = "the game's own value; changing it is an experiment";
  }
  menu_end_group(&m_pause_research);
  menu_add_return(&m_pause_research);
}

/* debug > flag logging: saved flag changes to CSV files on the SD card, three kinds with a switch each (flaglog.h) */
static menu_t m_flaglog;

/* it->data: which switch */
enum { FLAGLOG_FLAGS, FLAGLOG_STATUS, FLAGLOG_STORY };

static bool *flaglog_switch(menu_item_t *it)
{
  return it->data == FLAGLOG_STATUS ? &flaglog_status_enabled
       : it->data == FLAGLOG_STORY ? &flaglog_story_enabled : &flaglog_enabled;
}

static bool get_flaglog(menu_item_t *it)
{
  return *flaglog_switch(it);
}

static void set_flaglog(menu_item_t *it, bool on)
{
  *flaglog_switch(it) = on;
}

static void text_flaglog(menu_item_t *it, char *buf, int len)
{
  if (it->data == FLAGLOG_STATUS)
    flaglog_status_text(buf, len);
  else if (it->data == FLAGLOG_STORY)
    flaglog_story_text(buf, len);
  else
    flaglog_file_text(buf, len);
}

static void build_flaglog(void)
{
  menu_init(&m_flaglog, "flag logging", 7);
  menu_item_t *record = menu_add_check(&m_flaglog, "record flag changes", get_flaglog, set_flaglog, FLAGLOG_FLAGS);
  if (record) {
    record->hint = "named flag changes, added to the newest flaglog/flags-NNN.csv";
    record->log_label = "flag logging:";
  }
  menu_item_t *status = menu_add_check(&m_flaglog, "record player status", get_flaglog, set_flaglog, FLAGLOG_STATUS);
  if (status) {
    status->hint = "health, magic, rupees, equipment, bottles to flaglog/PlayerStatusChanges.csv";
    status->log_label = "player status log:";
  }
  menu_item_t *story = menu_add_check(&m_flaglog, "record story events", get_flaglog, set_flaglog, FLAGLOG_STORY);
  if (story) {
    story->hint = "quest stages the game changes, and why, to flaglog/StoryEvents.csv";
    story->log_label = "story events log:";
  }
  menu_add_return(&m_flaglog);
  menu_add_text(&m_flaglog, text_flaglog, FLAGLOG_FLAGS);
  menu_add_text(&m_flaglog, text_flaglog, FLAGLOG_STATUS);
  menu_add_text(&m_flaglog, text_flaglog, FLAGLOG_STORY);
  m_flaglog.footer = 3;
}

static void act_dive_test(menu_item_t *it)
{
  divetest_toggle((int)it->data);
  if (divetest_running())
    menu_close(); /* it waits while the menu is open */
}

static menu_t m_log; /* (defined with the log's code below) debug > log */

static void build_debug(void)
{
  build_pause_research();
  build_flaglog();
  menu_init(&m_debug, "debug", 7);
  menu_add_submenu(&m_debug, "tests", &m_tests);
  menu_add_submenu(&m_debug, "heap", &m_heap);
  menu_add_submenu(&m_debug, "memory", &memview_menu);
  menu_add_submenu(&m_debug, "pause research", &m_pause_research);
  menu_add_submenu(&m_debug, "flag logging", &m_flaglog);
  menu_add_submenu(&m_debug, "log", &m_log);
  menu_add_return(&m_debug);
  memview_build(&m_watches);

  menu_init(&m_tests, "tests", 4);
  menu_add_heading(&m_tests, "dive tests");
  menu_item_t *dive = menu_add_action(&m_tests, "from heights", act_dive_test, DIVETEST_HEIGHTS);
  if (dive) {
    dive->hint = "Link swimming at the surface: drops him 1 to 192 up, twice each, and logs how deep he goes "
                 "(L again: stop)";
    dive->log_label = "dive test from heights";
  }
  menu_item_t *moving = menu_add_action(&m_tests, "while moving", act_dive_test, DIVETEST_MOVING);
  if (moving) {
    moving->hint = "as from heights, from 16 and 96 up, moving 0 to 13.6 u/s the way he faces (face open water)";
    moving->log_label = "dive test while moving";
  }
  menu_add_return(&m_tests);

  menu_init(&m_heap, "heap", 7);
  menu_item_t *view = menu_add_action(&m_heap, "view the heap in memory", act_heap_memory, 0);
  if (view)
    view->hint = "L: debug > memory at the start of the heap";
  menu_add_return(&m_heap);
  add_blank_line(&m_heap);
  for (int line = 0; line < N_HEAP_LINES; line++)
    menu_add_text(&m_heap, text_heap, line);
}

/* --- log ------------------------------------------------------------------------------------------ */

static menu_t m_log;

/* The log menu: the module's last LOG_LINES messages, newest first, filtered by type (the word(s)
 * before a message's ':', e.g. "warp", "settings"; or "problems") and scrolled with gz's arrows, up over them
 * and down under them, with a scroll bar beside them (as the watches). */
#define LOG_ROWS 5 /* messages shown at once; the arrows scroll the rest */
#define MAX_LOG_TYPES 24
#define LOG_TYPE_SIZE 20
#define LOG_TYPE_MAX_LEN 18 /* a ':' further in isn't after a type */

/* the filter's choices: all, problems, event flags (the flag log's own list, flaglog_line), then each type seen */
enum { LOG_FILTER_ALL, LOG_FILTER_PROBLEMS, LOG_FILTER_FLAGS, LOG_FILTER_FIRST_TYPE };
/* the flag log's messages: under event flags, not a type of their own */
#define FLAG_LOG_TYPE "event flag" /* not menu text */

static char log_types[MAX_LOG_TYPES][LOG_TYPE_SIZE];
static const char *log_type_names[MAX_LOG_TYPES + LOG_FILTER_FIRST_TYPE];
static int n_log_types;
static int log_filter;
static int log_top; /* how many matching messages are scrolled past */

/* a message's type: what's before its ':' (letters and spaces, a short way in), or "other" */
static void log_type(const char *message, char *out, int len)
{
  int n = 0;
  while (message[n] && n < LOG_TYPE_MAX_LEN && ((message[n] >= 'a' && message[n] <= 'z') || message[n] == ' '))
    n++;
  if (message[n] == ':' && n > 0)
    snprintf(out, len, "%.*s", n, message);
  else
    snprintf(out, len, "other");
}

/* a message saying something couldn't be done */
static bool log_problem(const char *message)
{
  static const char *const words[] = { "couldn't", "can't", "fail", "refused", "not found", "error", "abandoned" };
  for (int w = 0; w < ARRAY_LEN(words); w++)
    for (const char *p = message; *p; p++)
      if (after_prefix(p, words[w]))
        return true;
  return false;
}

/* the types present, for the filter switch */
static void log_scan_types(void)
{
  n_log_types = 0;
  for (int i = 0; i < LOG_LINES; i++) {
    const char *message = log_line(i);
    if (!message || !message[0])
      continue;
    char type[LOG_TYPE_SIZE];
    log_type(message, type, sizeof(type));
    if (strings_equal(type, FLAG_LOG_TYPE))
      continue;
    int known = 0;
    while (known < n_log_types && !strings_equal(log_types[known], type))
      known++;
    if (known == n_log_types && n_log_types < MAX_LOG_TYPES)
      snprintf(log_types[n_log_types++], sizeof(log_types[0]), "%s", type);
  }
  log_type_names[LOG_FILTER_ALL] = "all";
  log_type_names[LOG_FILTER_PROBLEMS] = "problems";
  log_type_names[LOG_FILTER_FLAGS] = "event flags";
  for (int k = 0; k < n_log_types; k++)
    log_type_names[LOG_FILTER_FIRST_TYPE + k] = log_types[k];
}

static bool log_matches(const char *message)
{
  if (log_filter == LOG_FILTER_ALL)
    return true;
  if (log_filter == LOG_FILTER_PROBLEMS)
    return log_problem(message);
  if (log_filter == LOG_FILTER_FLAGS)
    return true;
  char type[LOG_TYPE_SIZE];
  log_type(message, type, sizeof(type));
  int type_index = log_filter - LOG_FILTER_FIRST_TYPE;
  return type_index < n_log_types && strings_equal(type, log_types[type_index]);
}

/* the n-th message (newest first) that passes the filter, or NULL; *count: how many pass */
static const char *log_nth(int n, int *count)
{
  const char *found = 0;
  int matching = 0;
  bool flags = log_filter == LOG_FILTER_FLAGS; /* the flag log's own list: every one matches */
  for (int i = 0; i < LOG_LINES; i++) {
    const char *message = flags ? flaglog_line(i) : log_line(i);
    if (!message || !message[0] || !log_matches(message))
      continue;
    if (matching == n)
      found = message;
    matching++;
  }
  if (count)
    *count = matching;
  return found;
}

static int log_count(void)
{
  int count;
  log_nth(0, &count);
  return count;
}

static int get_log_filter(menu_item_t *it)
{
  log_scan_types();
  it->n_choices = n_log_types + LOG_FILTER_FIRST_TYPE;
  it->choices = log_type_names;
  if (log_filter >= it->n_choices)
    log_filter = LOG_FILTER_ALL;
  /* the hint: "which messages:", then what this filter shows, indented under it */
  static char hint[160];
  char what[100];
  if (log_filter == LOG_FILTER_ALL)
    snprintf(what, sizeof(what), "%s", T("every message, newest first (all also in sd:/sloperinaGZ/log.txt)"));
  else if (log_filter == LOG_FILTER_PROBLEMS)
    snprintf(what, sizeof(what), "%s", T("anything that couldn't be done"));
  else if (log_filter == LOG_FILTER_FLAGS)
    snprintf(what, sizeof(what), "%s", T("flag changes recorded by flag logging (debug > flag logging), newest first"));
  else
    snprintf(what, sizeof(what), TF("messages about %s"), log_type_names[log_filter]);
  snprintf(hint, sizeof(hint), "%s\n  %s: %s", T("which messages:"), T(log_type_names[log_filter]), what);
  it->hint = hint;
  return log_filter;
}

static void set_log_filter(menu_item_t *it, int filter)
{
  log_filter = filter;
  log_top = 0;
}

/* the window on the matching messages kept in range, and the scroll bar beside it told where it is; returns how
 * many match */
static int log_window(void)
{
  int count = log_count();
  log_top = clamp_int(log_top, 0, max_top(count, LOG_ROWS));
  m_log.inner_items = count > LOG_ROWS ? count : LOG_ROWS;
  m_log.inner_top = log_top;
  return count;
}

/* it->data: -1 the up arrow (with which messages show), 1 the down arrow */
static void log_arrow(menu_item_t *it, char *buf, int len)
{
  int count = log_window();
  int last = log_top + LOG_ROWS < count ? log_top + LOG_ROWS : count;
  if (it->data < 0)
    snprintf(buf, len, "%c  %d-%d / %d", GFX_ICON_UP, count ? log_top + 1 : 0, last, count);
  else
    snprintf(buf, len, "%c", GFX_ICON_DOWN);
}

static void log_scroll(menu_item_t *it, int part)
{
  log_top = clamp_int(log_top + (int)it->data, 0, max_top(log_count(), LOG_ROWS));
  log_window();
}

static void add_log_arrow(int dir)
{
  menu_item_t *arrow = menu_add_action(&m_log, "", 0, dir);
  if (!arrow)
    return;
  arrow->text = log_arrow;
  arrow->cursor = arrow_cursor;
  arrow->press = log_scroll;
  arrow->press_repeat = true;
  arrow->indent = 1; /* as the messages, the scroll bar left of them */
  arrow->hint = dir < 0 ? "L: scroll to newer (held: keeps going)" : "L: scroll to older (held: keeps going)";
}

/* it->data: the row */
/* a log message, cut at a word with "..." when it's longer than a row holds */
#define LOG_LINE_W (GFX_COLS - 4) /* menu.c's ITEM_COL, and the messages' indent */
static void text_log(menu_item_t *it, char *buf, int len)
{
  if (it->data == 0)
    log_window();
  const char *message = log_nth(log_top + (int)it->data, 0);
  snprintf(buf, len, "%s", message ? message : "");
  int n = (int)strlen(buf);
  if (n > LOG_LINE_W && len > LOG_LINE_W) {
    int cut = LOG_LINE_W - 3;
    while (cut > LOG_LINE_W / 2 && buf[cut] != ' ')
      cut--;
    if (buf[cut] != ' ')
      cut = LOG_LINE_W - 3;
    snprintf(buf + cut, (size_t)(len - cut), "...");
  }
}

static void build_log(void)
{
  menu_init(&m_log, "log", LOG_ROWS + 6);
  menu_add_heading(&m_log, "log history");
  menu_item_t *filter = menu_add_option(&m_log, "filter", 1, log_type_names, get_log_filter, set_log_filter, 0);
  if (filter)
    filter->log_label = ""; /* the list shows the filter: no log message for it */
  add_log_arrow(-1);
  m_log.inner_first = m_log.n_items;
  for (int row = 0; row < LOG_ROWS; row++) {
    menu_item_t *line = menu_add_text(&m_log, text_log, row); /* grey: they can't be selected */
    if (line)
      line->indent = 1; /* room for the scroll bar left of them */
  }
  m_log.inner_rows = m_log.n_items - m_log.inner_first;
  m_log.inner_items = LOG_ROWS;
  add_log_arrow(1);
  menu_add_return(&m_log);
}

/* --- about, splash, root ------------------------------------------------------------------------- */

static menu_t m_about;

#define SPLASH_FRAMES (8 * GAME_FPS) /* as gz's */

/* gz's start-up splash (bottom right): name and version, the build date under it, the website, how to
 * open the menu */
void menus_splash(void)
{
  static char title[64], date[64];
  snprintf(title, sizeof(title), "sloperinaGZ - %s", BUILD_VERSION);
  snprintf(date, sizeof(date), TF("Build Date: %s"), BUILD_WHEN);
  static char open_with[96];
  char bind[48];
  bind_name(binds[CMD_MENU], bind, sizeof(bind)); /* the settings are loaded by now */
  snprintf(open_with, sizeof(open_with), TF("%s: open the menu"), bind);
  const char *lines[] = { title, date, "practicerom.com/oothd", 0, open_with };
  static const uint8_t styles[] = { GFX_DIM, GFX_DIM, GFX_LOG, GFX_LOG, GFX_NORMAL };
  static const uint8_t indents[] = { 0, 0, 0, 0, 0 };
  gfx_splash(lines, styles, indents, ARRAY_LEN(lines), SPLASH_FRAMES);
}

/* the about page: the name, version and build date, then the website */
static void build_about(void)
{
  menu_init(&m_about, "about", 10);
  static char title[64];
  snprintf(title, sizeof(title), "sloperinaGZ - %s", BUILD_VERSION);
  menu_add_heading(&m_about, title);
  menu_item_t *date = menu_add_text(&m_about, text_build_date, 0);
  if (date)
    date->indent = 0; /* in line with the heading, not under it */
  menu_end_group(&m_about);
  add_blank_line(&m_about);
  menu_add_text(&m_about, text_static, (intptr_t) "practicerom.com/oothd");
  add_blank_line(&m_about);
  menu_add_return(&m_about);
}

static void build_root(void)
{
  menu_init(&menu_root, "sloperinaGZ", 15);
  menu_add_submenu(&menu_root, "link", &m_link);
  menu_add_submenu(&menu_root, "warps", &m_warps);
  menu_add_submenu(&menu_root, "scene", &m_scene);
  menu_add_submenu(&menu_root, "cheats", &m_cheats);
  menu_add_submenu(&menu_root, "inventory", &m_inventory);
  menu_add_submenu(&menu_root, "equips", &m_equips);
  menu_add_submenu(&menu_root, "watches", &m_watches);
  menu_add_submenu(&menu_root, "save management", &m_save_backups);
  menu_add_submenu(&menu_root, "story", &m_story);
  menu_add_submenu(&menu_root, "debug", &m_debug);
  menu_add_submenu(&menu_root, "settings", &m_settings);
  menu_add_submenu(&menu_root, "about", &m_about);
  menu_add(&menu_root, MI_BACK, "close");
}

/* the order matters: menu_init reserves each menu's items from one shared pool */
void menus_build(void)
{
  build_link();
  build_warps();
  build_cheats();
  build_inventory();
  build_equips();
  build_story();
  build_watches();
  build_scene();
  build_settings();
  build_save_backups();
  build_debug();
  osk_build();
  build_log();
  build_about();
  build_root();
}
