/* Save backups. Every save the game makes, autosave or from the save screen, ends in nn::fs::CommitSaveData on its
 * save mount. main's GOT slot for it is hooked (got.c); once the game's commit has succeeded, the whole mount is
 * copied to a new folder on the SD card. The copy runs inside the hook, so the game can't write the save again
 * until it's done, and on whatever thread the game saves from: nothing here logs or draws (savebackup_frame
 * reports on the game's thread).
 *
 * The directory functions and DeleteDirectoryRecursively are looked up in the sdk's exports rather than imported:
 * an import rtld couldn't find would stop the game loading. */
#include "savebackup.h"

#include "gfx.h"
#include "gamedata.h"
#include "link.h"
#include "got.h"
#include "lang.h"
#include "sdfs.h"
#include "settings.h"
#include "sys.h"
#include "z64hd.h"

bool save_backup_enabled;
bool save_backup_location;
int save_backup_kinds = SAVE_BACKUP_BOTH;

/* Which kind a commit is: the game's menu screen when it happened (the Enum 0x578D41E1, as gd_game_menu_screen;
 * FINDINGS §9.1). None or Loading: an autosave (a loading zone shows Loading while it saves; checked in Lichii).
 * Option: the options alone, not backed up. Any other (the save screen, game over's save prompt): a hard save. */
#define GD_MENU_SCREEN 0x578D41E1u
#define MENU_SCREEN_NONE 0xB6EEDE09u   /* murmur3("None") */
#define MENU_SCREEN_LOADING 0x9C031F08u /* murmur3("Loading") */
#define MENU_SCREEN_OPTION 0xA60BA395u /* murmur3("Option") */
static uint32_t menu_screen = MENU_SCREEN_NONE; /* as last seen on the game's thread (savebackup_frame) */
enum { KIND_AUTO, KIND_HARD, KIND_OPTIONS };
static const char *const kind_names[] = { "autosave", "hard save", "options" }; /* not menu text */

/* The save caption's location (String64 0xF74E0E8E, kept in caption.sav), e.g. "TimeTemple", and its English
 * name from the game's LocationMsg/Location.msbt (EUen) */
#define GD_STR_SAVE_LOCATION 0xF74E0E8Eu
static const struct { const char *key, *name; } locations[] = {
  { "BottomOfTheWell", "Bottom of the Well" }, { "DeathMountainCrater", "Death Mountain Crater" }, /* not menu text */
  { "DeathMountainTrail", "Death Mountain Trail" }, { "DesertColossus", "Evil Desert Colossus" }, /* not menu text */
  { "DodongoCavern", "Dodongo Cavern" }, { "DreamWorld", "Hall of Fallen Foes" }, { "FireTemple", "Fire Temple" }, /* not menu text */
  { "ForestTemple", "Forest Temple" }, { "GanonCastle", "Ganondorf's Castle" }, /* not menu text */
  { "GerudoFortress", "Gerudo Fortress" }, { "GerudoTrainingGround", "Gerudo Proving Grounds" }, /* not menu text */
  { "GerudoValley", "Gerudo Valley" }, { "GoronCity", "Goron City" }, { "Graveyard", "Graveyard" }, /* not menu text */
  { "HountedWasteland", "Mirage Desert" }, { "HyruleCastle", "Hyrule Castle" }, { "HyruleField", "Hyrule Field" }, /* not menu text */
  { "IceCavern", "Ice Cavern" }, { "IngoRanch", "Ingo Ranch" }, { "InsideGanonCastle", "Inside Ganondorf's Castle" }, /* not menu text */
  { "InsideJabuJabuBelly", "Lord Jabu-Jabu's Belly" }, { "InsideTheDekuTree", "Great Deku Tree" }, /* not menu text */
  { "KakarikoVillage", "Kakariko Village" }, { "KokiriForest", "Kokiri Forest" }, { "LakeHylia", "Lake Hylia" }, /* not menu text */
  { "LonLonRanch", "Lon Lon Ranch" }, { "LostWood", "Lost Woods" }, { "Market", "Hyrule Castle Town" }, /* not menu text */
  { "SacredForestMeadow", "Sacred Grove" }, { "ShadowTemple", "Shadow Temple" }, { "SpiritTemple", "Spirit Temple" }, /* not menu text */
  { "TimeTemple", "Temple of Time" }, { "WaterTemple", "Water Temple" }, { "ZoraDomain", "Zora Domain" }, /* not menu text */
  { "ZoraFountain", "Zora Fountain" }, { "ZoraRiver", "Zora River" }, /* not menu text */
};
#define N_LOCATIONS (sizeof(locations) / sizeof(*locations))
/* the location as last seen on the game's thread (savebackup_frame): the copy may run on another */
static char location_key[64];

#define BACKUP_DIR SD_DIR "/save-backups"
#define MAX_DEPTH 4             /* save mount: slot_NN/ inside its root */
#define MAX_BACKUP_FOLDERS 512  /* listed when trimming to SAVE_BACKUPS_KEPT */
#define COPY_CHUNK (64 * 1024)
#define PATH_LEN 256
#define STAMP_LEN 96            /* "2026-11-15_21-30-05 - Inside Ganondorf's Castle", with room to spare */

/* nn::fs's directory types (the SDK's layout) */
typedef struct { void *handle; } fs_dir_t;
typedef struct {
  char name[769];
  uint8_t pad0[3];
  int8_t type;                  /* 0 directory, 1 file */
  uint8_t pad1[3];
  int64_t size;
} fs_dir_entry_t;
enum { FS_ENTRY_DIRECTORY = 0, FS_ENTRY_FILE = 1, FS_OPEN_DIR_ALL = 3 };

typedef nn_result_t (*commit_fn)(const char *mount);
typedef nn_result_t (*open_dir_fn)(fs_dir_t *out, const char *path, int mode);
typedef nn_result_t (*read_dir_fn)(int64_t *count, fs_dir_entry_t *entries, fs_dir_t dir, int64_t max);
typedef void (*close_dir_fn)(fs_dir_t dir);
typedef nn_result_t (*delete_tree_fn)(const char *path);

static open_dir_fn open_dir;
static read_dir_fn read_dir;
static close_dir_fn close_dir;
static delete_tree_fn delete_tree;

/* main's two commit imports: what each wrapper calls (got_hook) */
static void *next_commit_save, *next_commit;
/* the game's save mount ("<name>"), learned from its first successful commit: savestates are written there */
static char known_mount[32];

/* the console's clock (nn::time, as flaglog.c) */
typedef struct { int64_t value; } nn_posix_time_t;
typedef struct { int16_t year; int8_t month, day, hour, minute, second; } nn_calendar_time_t;
bool nn_time_initialized(void) __asm__("_ZN2nn4time13IsInitializedEv");
uint32_t nn_time_now(nn_posix_time_t *out)
  __asm__("_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE");
uint32_t nn_time_to_calendar(nn_calendar_time_t *out, void *info, const nn_posix_time_t *time)
  __asm__("_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeE");
#define CALENDAR_INFO_SIZE 64

/* the copy's working state (one copy at a time: `busy`) */
static int busy;
static _Alignas(8) uint8_t chunk[COPY_CHUNK];
static fs_dir_entry_t entries[MAX_DEPTH + 1];
static char folder_names[MAX_BACKUP_FOLDERS][STAMP_LEN];
static int files_copied;
static int64_t bytes_copied;
static char error[320];

/* what the last backup did, for savebackup_frame and the menu */
static volatile int reports;          /* backups finished; savebackup_frame reports each new one */
static int reported;
static char last_mount[32], last_stamp[STAMP_LEN], last_error[320];
static int last_files;
static int64_t last_bytes;
static uint64_t last_ticks;
static const char *hook_error = "not hooked yet"; /* not menu text */

static bool ends_with_slash(const char *s)
{
  size_t n = strlen(s);
  return n && s[n - 1] == '/';
}

static void join(char *out, const char *dir, const char *name)
{
  snprintf(out, PATH_LEN, ends_with_slash(dir) ? "%s%s" : "%s/%s", dir, name);
}

static void fail(const char *what, const char *path, nn_result_t r)
{
  if (!error[0])
    snprintf(error, sizeof(error), "%s %s (0x%x)", what, path, (unsigned)r);
}

static void copy_file(const char *from, const char *to)
{
  fs_file_t in, out;
  int64_t size = 0;
  nn_result_t r;
  if ((r = fs_open(&in, from, FS_READ)) != 0) {
    fail("couldn't open", from, r); /* not menu text */
    return;
  }
  fs_size(&size, in);
  fs_delete_file(to);
  if ((r = fs_create_file(to, size)) != 0 || (r = fs_open(&out, to, FS_WRITE)) != 0) {
    fail("couldn't make", to, r); /* not menu text */
    fs_close(in);
    return;
  }
  for (int64_t at = 0; at < size;) {
    size_t want = size - at < COPY_CHUNK ? (size_t)(size - at) : COPY_CHUNK, got = 0;
    if ((r = fs_read(&got, in, at, chunk, want)) != 0 || !got) {
      fail("couldn't read", from, r); /* not menu text */
      break;
    }
    fs_write_option_t flush = { at + (int64_t)got >= size ? FS_WRITE_FLUSH : 0 };
    if ((r = fs_write(out, at, chunk, got, &flush)) != 0) {
      fail("couldn't write", to, r); /* not menu text */
      break;
    }
    at += got;
    bytes_copied += got;
  }
  fs_close(out);
  fs_close(in);
  files_copied++;
}

static void copy_tree(const char *from, const char *to, int depth)
{
  fs_dir_t dir;
  nn_result_t r = open_dir(&dir, from, FS_OPEN_DIR_ALL);
  if (r != 0) {
    fail("couldn't list", from, r); /* not menu text */
    return;
  }
  fs_dir_entry_t *entry = &entries[depth];
  int64_t count;
  char child_from[PATH_LEN], child_to[PATH_LEN];
  while (read_dir(&count, entry, dir, 1) == 0 && count == 1) {
    join(child_from, from, entry->name);
    join(child_to, to, entry->name);
    if (entry->type == FS_ENTRY_DIRECTORY) {
      if (depth >= MAX_DEPTH) {
        fail("too deep:", child_from, 0); /* not menu text */
        continue;
      }
      fs_create_dir(child_to);
      copy_tree(child_from, child_to, depth + 1);
    } else {
      copy_file(child_from, child_to);
    }
  }
  close_dir(dir);
}

static int compare_names(const char *a, const char *b)
{
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

/* Delete the oldest backups (their names sort by date) past SAVE_BACKUPS_KEPT. */
static void trim_backups(void)
{
  if (!delete_tree)
    return;
  fs_dir_t dir;
  if (open_dir(&dir, BACKUP_DIR, FS_OPEN_DIR_ALL) != 0)
    return;
  int n = 0;
  int64_t count;
  while (n < MAX_BACKUP_FOLDERS && read_dir(&count, &entries[0], dir, 1) == 0 && count == 1)
    if (entries[0].type == FS_ENTRY_DIRECTORY && strlen(entries[0].name) < STAMP_LEN)
      snprintf(folder_names[n++], STAMP_LEN, "%s", entries[0].name);
  close_dir(dir);
  for (; n > SAVE_BACKUPS_KEPT; n--) {
    int oldest = 0;
    for (int i = 1; i < n; i++)
      if (compare_names(folder_names[i], folder_names[oldest]) < 0)
        oldest = i;
    char path[PATH_LEN];
    join(path, BACKUP_DIR, folder_names[oldest]);
    delete_tree(path);
    memcpy(folder_names[oldest], folder_names[n - 1], STAMP_LEN);
  }
}

/* the folder name's " - <location>", or nothing */
static void location_suffix(char *out, int len)
{
  out[0] = 0;
  if (!save_backup_location || !location_key[0])
    return;
  const char *name = location_key; /* a location the table doesn't know: its key */
  for (unsigned i = 0; i < N_LOCATIONS; i++)
    if (!compare_names(location_key, locations[i].key))
      name = locations[i].name;
  int n = snprintf(out, len, " - ");
  for (; *name && n < len - 1; name++) /* nothing a folder name can't hold */
    out[n++] = (*name == '/' || *name == '\\' || *name == ':' || *name == '*' || *name == '?' || *name == '"' ||
                *name == '<' || *name == '>' || *name == '|') ? '_' : *name;
  out[n] = 0;
}

/* commits not backed up, for savebackup_frame to log: their kind and the menu screen then */
static volatile int skips;
static int skips_reported, last_skip_kind;
static uint32_t last_skip_screen, last_screen;
static int last_kind;

static void back_up(const char *mount)
{
  if (!save_backup_enabled || !settings_sd() || !open_dir || !read_dir || !close_dir || !mount ||
      strlen(mount) >= sizeof(last_mount) - 1)
    return;
  uint32_t screen = __atomic_load_n(&menu_screen, __ATOMIC_ACQUIRE);
  int kind = screen == MENU_SCREEN_NONE || screen == MENU_SCREEN_LOADING ? KIND_AUTO
           : screen == MENU_SCREEN_OPTION                                  ? KIND_OPTIONS
                                                                           : KIND_HARD;
  bool wanted = kind == KIND_AUTO ? save_backup_kinds != SAVE_BACKUP_HARD
              : kind == KIND_HARD ? save_backup_kinds != SAVE_BACKUP_AUTO : false;
  if (!wanted) {
    last_skip_kind = kind;
    last_skip_screen = screen;
    __atomic_add_fetch(&skips, 1, __ATOMIC_RELEASE);
    return;
  }
  if (__atomic_exchange_n(&busy, 1, __ATOMIC_ACQUIRE))
    return; /* another thread's commit is being copied: that copy is as new */
  last_kind = kind;
  last_screen = screen;
  uint64_t start = sys_tick();
  char stamp[STAMP_LEN] = "undated"; /* not menu text */
  nn_posix_time_t posix;
  nn_calendar_time_t cal;
  static _Alignas(8) uint8_t info[CALENDAR_INFO_SIZE];
  if (nn_time_initialized() && nn_time_now(&posix) == 0 && nn_time_to_calendar(&cal, info, &posix) == 0)
    snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d_%02d-%02d-%02d", cal.year, cal.month, cal.day, cal.hour,
             cal.minute, cal.second);
  char suffix[64];
  location_suffix(suffix, sizeof(suffix));
  size_t at = strlen(stamp);
  snprintf(stamp + at, sizeof(stamp) - at, "%s", suffix);
  char root[40], to[PATH_LEN];
  snprintf(root, sizeof(root), "%s:/", mount);
  fs_create_dir(SD_DIR);
  fs_create_dir(BACKUP_DIR);
  join(to, BACKUP_DIR, stamp);
  /* two saves in one second (an autosave, then the save screen's): the later one replaces it */
  if (delete_tree)
    delete_tree(to);
  error[0] = 0;
  files_copied = 0;
  bytes_copied = 0;
  nn_result_t r = fs_create_dir(to);
  if (r != 0)
    fail("couldn't make", to, r); /* not menu text */
  else
    copy_tree(root, to, 0);
  trim_backups();

  snprintf(last_mount, sizeof(last_mount), "%s", mount);
  memcpy(last_stamp, stamp, sizeof(last_stamp));
  memcpy(last_error, error, sizeof(last_error));
  last_files = files_copied;
  last_bytes = bytes_copied;
  last_ticks = sys_tick() - start;
  __atomic_add_fetch(&reports, 1, __ATOMIC_RELEASE);
  __atomic_store_n(&busy, 0, __ATOMIC_RELEASE);
}

static void note_mount(const char *mount)
{
  if (mount && mount[0] && strlen(mount) < sizeof(known_mount) && compare_names(mount, known_mount) != 0)
    snprintf(known_mount, sizeof(known_mount), "%s", mount);
}

static nn_result_t commit_save_wrap(const char *mount)
{
  nn_result_t r = ((commit_fn)next_commit_save)(mount);
  if (r == 0) {
    note_mount(mount);
    back_up(mount);
  }
  return r;
}

static nn_result_t commit_wrap(const char *mount)
{
  nn_result_t r = ((commit_fn)next_commit)(mount);
  if (r == 0) {
    note_mount(mount);
    back_up(mount);
  }
  return r;
}

const char *savebackup_hook_game(uintptr_t main_base)
{
  uintptr_t sdk = got_module_of((uintptr_t)&fs_open); /* an sdk function this module imports */
  if (!sdk)
    return hook_error = "sdk module not found"; /* not menu text */
  open_dir = (open_dir_fn)got_export(sdk, "_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci");
  read_dir = (read_dir_fn)got_export(sdk, "_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl");
  close_dir = (close_dir_fn)got_export(sdk, "_ZN2nn2fs14CloseDirectoryENS0_15DirectoryHandleE");
  delete_tree = (delete_tree_fn)got_export(sdk, "_ZN2nn2fs26DeleteDirectoryRecursivelyEPKc");
  if (!open_dir || !read_dir || !close_dir)
    return hook_error = "nn::fs directory functions not found in the sdk"; /* not menu text */
  void *commit_save = got_export(sdk, "_ZN2nn2fs14CommitSaveDataEPKc");
  void *commit = got_export(sdk, "_ZN2nn2fs6CommitEPKc");
  const char *e1 = commit_save ? got_hook(main_base, "_ZN2nn2fs14CommitSaveDataEPKc", commit_save_wrap, commit_save,
                                          &next_commit_save)
                               : "CommitSaveData not in the sdk"; /* not menu text */
  const char *e2 = commit ? got_hook(main_base, "_ZN2nn2fs6CommitEPKc", commit_wrap, commit, &next_commit)
                          : "Commit not in the sdk"; /* not menu text */
  if (e1 && e2)
    return hook_error = e1;
  hook_error = 0;
  return 0;
}

static bool states_scanned;

void savebackup_frame(void)
{
  uint32_t screen;
  if (!states_scanned && settings_sd() && open_dir)
    savestates_scan();
  if (save_backup_enabled && gd_get_enum(GD_MENU_SCREEN, &screen))
    __atomic_store_n(&menu_screen, screen, __ATOMIC_RELEASE);
  int skipped = __atomic_load_n(&skips, __ATOMIC_ACQUIRE);
  if (skipped != skips_reported) {
    skips_reported = skipped;
    log_printf("save backup: skipped a commit (%s, menu screen %08x)", kind_names[last_skip_kind],
               (unsigned)last_skip_screen);
  }
  const char *key = save_backup_enabled ? gd_get_str(GD_STR_SAVE_LOCATION) : 0;
  if (key && !__atomic_load_n(&busy, __ATOMIC_ACQUIRE)) {
    char copy[sizeof(location_key)];
    int i = 0;
    for (; i < (int)sizeof(copy) - 1 && key[i]; i++)
      copy[i] = key[i];
    copy[i] = 0;
    memcpy(location_key, copy, sizeof(location_key));
  }
  int now = __atomic_load_n(&reports, __ATOMIC_ACQUIRE);
  if (now == reported || __atomic_load_n(&busy, __ATOMIC_ACQUIRE))
    return;
  reported = now;
  unsigned ms = (unsigned)(last_ticks * 1000 / sys_tick_freq());
  if (last_error[0]) {
    log_printf("save backup %s (%s, menu screen %08x) from %s: %d files, then %s", last_stamp, kind_names[last_kind],
               (unsigned)last_screen, last_mount, last_files, last_error);
    gfx_notice("save backup failed (see the log)");
  } else {
    log_printf("save backup %s (%s, menu screen %08x) from %s: %d files, %lld bytes, %u ms", last_stamp,
               kind_names[last_kind], (unsigned)last_screen, last_mount, last_files, (long long)last_bytes, ms);
    gfx_notice("save backed up");
  }
}

void savebackup_status_text(char *buf, int len)
{
  if (hook_error)
    snprintf(buf, len, TF("unavailable: %s"), hook_error); /* hook_error: not menu text */
  else if (!settings_sd())
    snprintf(buf, len, "%s", T("no SD card access"));
  else if (!reported)
    snprintf(buf, len, "%s", T("no backup yet this session"));
  else if (last_error[0])
    snprintf(buf, len, TF("last: %s failed (see the log)"), last_stamp);
  else
    snprintf(buf, len, TF("last: %s (%s)"), last_stamp, T(kind_names[last_kind]));
}

/* ---- savestates: saves kept on the SD card, by category ----------------------------------------------------
 * sd:/sloperinaGZ/savestates/<category>/<name>/slot_NN/{progress.sav,caption.sav}: the same files the game keeps in
 * its save folder (a Fritchii/Eden export of one slot). Applying one copies its slot over slot_01 of the game's save
 * mount (the "target slot": the one at the top of the load screen) and commits, as the game's own save does; the
 * game then loads it from its load screen like any other save. Dumping does the opposite: slot_01 is copied to the
 * SD card as a new save in the current category, named on the keyboard (empty: save_NNN). */
#define STATES_DIR SD_DIR "/savestates"
#define STATE_NAME_LEN 64

static char cat_names[SAVESTATES_CATS_MAX][STATE_NAME_LEN];
static int cat_count, cur_cat;
static char state_names[SAVESTATES_MAX][STATE_NAME_LEN];
static int state_count, state_page, page_size = SAVESTATES_PAGE_MAX;
#define TARGET_SLOT 1 /* fixed: always slot_01 */
static volatile int state_generation;
static fs_dir_entry_t state_entry;
static char state_result[200];

static void states_changed(void)
{
  __atomic_add_fetch(&state_generation, 1, __ATOMIC_RELEASE);
}

int savestates_count(void)
{
  return state_count;
}

const char *savestates_name(int i)
{
  return i >= 0 && i < state_count ? state_names[i] : "";
}

int savestates_category_count(void)
{
  return cat_count;
}

int savestates_category_index(void)
{
  return cat_count ? cur_cat : -1;
}

const char *savestates_category_name(int i)
{
  return i >= 0 && i < cat_count ? cat_names[i] : "-";
}

int savestates_page_size(void)
{
  return page_size;
}

int savestates_page_first(void)
{
  return state_page * page_size;
}

int savestates_page(void)
{
  return state_page;
}

int savestates_pages(void)
{
  return (state_count + page_size - 1) / page_size;
}

int savestates_generation(void)
{
  return state_generation;
}

const char *savestates_result(void)
{
  return state_result;
}

static void sort_names(char (*names)[STATE_NAME_LEN], int n)
{
  static char swap[STATE_NAME_LEN];
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && compare_names(names[j - 1], names[j]) > 0; j--) {
      memcpy(swap, names[j], STATE_NAME_LEN);
      memcpy(names[j], names[j - 1], STATE_NAME_LEN);
      memcpy(names[j - 1], swap, STATE_NAME_LEN);
    }
}

/* the sub-folders of path, sorted, at most max */
static int list_dirs(const char *path, char (*out)[STATE_NAME_LEN], int max)
{
  int n = 0;
  fs_dir_t dir;
  int64_t count;
  if (open_dir(&dir, path, FS_OPEN_DIR_ALL) == 0) {
    while (n < max && read_dir(&count, &state_entry, dir, 1) == 0 && count == 1)
      if (state_entry.type == FS_ENTRY_DIRECTORY && strlen(state_entry.name) < STATE_NAME_LEN)
        snprintf(out[n++], STATE_NAME_LEN, "%s", state_entry.name);
    close_dir(dir);
  }
  sort_names(out, n);
  return n;
}

/* the current category's saves */
static void scan_states(void)
{
  char path[PATH_LEN];
  state_count = 0;
  if (cat_count) {
    join(path, STATES_DIR, cat_names[cur_cat]);
    state_count = list_dirs(path, state_names, SAVESTATES_MAX);
  }
  int pages = savestates_pages();
  if (state_page >= pages)
    state_page = pages > 0 ? pages - 1 : 0;
}

void savestates_scan(void)
{
  char keep[STATE_NAME_LEN] = "";
  if (cat_count)
    snprintf(keep, sizeof(keep), "%s", cat_names[cur_cat]);
  states_scanned = true;
  cat_count = 0;
  if (settings_sd() && open_dir && read_dir && close_dir) {
    fs_create_dir(SD_DIR);
    fs_create_dir(STATES_DIR);
    cat_count = list_dirs(STATES_DIR, cat_names, SAVESTATES_CATS_MAX);
  }
  cur_cat = 0; /* stay on the same category when it still exists */
  for (int i = 0; i < cat_count; i++)
    if (compare_names(cat_names[i], keep) == 0)
      cur_cat = i;
  scan_states();
  states_changed();
}

void savestates_select_category(int i)
{
  if (i >= 0 && i < cat_count && i != cur_cat) {
    cur_cat = i;
    state_page = 0;
    scan_states();
  }
  state_result[0] = 0;
  states_changed();
}

void savestates_select_page(int i)
{
  if (i >= 0 && i < savestates_pages())
    state_page = i;
  states_changed();
}

void savestates_set_page_size(int n)
{
  n = n < SAVESTATES_PAGE_MIN ? SAVESTATES_PAGE_MIN : n > SAVESTATES_PAGE_MAX ? SAVESTATES_PAGE_MAX : n;
  if (n == page_size)
    return;
  int first = state_page * page_size; /* the same first save stays on the page */
  page_size = n;
  state_page = first / n;
  states_changed();
}

/* a reason the menu shows under the list and in the log */
static const char *refuse(const char *text)
{
  snprintf(state_result, sizeof(state_result), "%s", text);
  return state_result;
}

static bool is_slot_dir(const char *name)
{
  return name[0] == 's' && name[1] == 'l' && name[2] == 'o' && name[3] == 't' && name[4] == '_';
}

/* NULL when done (the result text then says what), else what went wrong */
const char *savestates_apply(int index)
{
  state_result[0] = 0;
  if (!cat_count || index < 0 || index >= state_count)
    return refuse("no such save"); /* not menu text */
  if (!known_mount[0])
    return refuse("save once in the game first (or pass a loading zone)"); /* not menu text */
  if (!settings_sd() || !open_dir || !read_dir || !close_dir)
    return refuse("no SD card access"); /* not menu text */
  if (__atomic_exchange_n(&busy, 1, __ATOMIC_ACQUIRE))
    return refuse("a copy is already running"); /* not menu text */
  error[0] = 0;
  files_copied = 0;
  bytes_copied = 0;
  char cat[PATH_LEN], from[PATH_LEN], src[PATH_LEN], to_root[48], to[PATH_LEN], target[16];
  char slot[STATE_NAME_LEN] = "";
  join(cat, STATES_DIR, cat_names[cur_cat]);
  join(from, cat, state_names[index]);
  snprintf(target, sizeof(target), "slot_%02d", TARGET_SLOT);
  fs_dir_t dir;
  int64_t count;
  nn_result_t r = open_dir(&dir, from, FS_OPEN_DIR_ALL);
  if (r != 0) {
    fail("couldn't list", from, r); /* not menu text */
  } else {
    /* the save's slot folder, whatever its number: it goes into the target slot */
    while (read_dir(&count, &state_entry, dir, 1) == 0 && count == 1)
      if (state_entry.type == FS_ENTRY_DIRECTORY && is_slot_dir(state_entry.name) &&
          strlen(state_entry.name) < STATE_NAME_LEN) {
        snprintf(slot, sizeof(slot), "%s", state_entry.name);
        break;
      }
    close_dir(dir);
    if (!slot[0])
      fail("no slot_NN folder in", from, 0); /* not menu text */
  }
  if (!error[0]) {
    join(src, from, slot);
    snprintf(to_root, sizeof(to_root), "%s:/", known_mount);
    join(to, to_root, target);
    fs_create_dir(to);
    copy_tree(src, to, 1);
  }
  if (!error[0]) {
    void *commit_now = next_commit_save ? next_commit_save : next_commit;
    if (commit_now && (r = ((commit_fn)commit_now)(known_mount)) != 0)
      fail("couldn't commit", known_mount, r); /* not menu text */
  }
  const char *out = 0;
  if (error[0]) {
    snprintf(state_result, sizeof(state_result), "%s", error);
    out = state_result;
  } else {
    snprintf(state_result, sizeof(state_result), "manual save replaced: load it from the load screen"); /* not menu text */
  }
  __atomic_store_n(&busy, 0, __ATOMIC_RELEASE);
  return out;
}

/* ---- main's memory on the SD card (for reverse engineering) ----------------
 * main.bin: the image from main's load address (the code, read-only data, data and bss; 1.0.0's NSO maps 0x4E20000
 * bytes up to the end of its bss), main.base: that address in hex. Pages that aren't mapped are written as zeros,
 * so the file keeps the layout. Runs on the game's thread and takes a few seconds. */
#define DUMP_MAIN_SIZE 0x4E20000u
#define DUMP_MAIN_BIN SD_DIR "/main.bin"
#define DUMP_MAIN_BASE SD_DIR "/main.base"
#define DUMP_PAGE 0x1000u
#define DUMP_CHUNK COPY_CHUNK /* the work buffer's size; it divides DUMP_MAIN_SIZE */

const char *savestates_dump_main(void)
{
  state_result[0] = 0;
  if (!settings_sd())
    return refuse("no SD card access"); /* not menu text */
  if (__atomic_exchange_n(&busy, 1, __ATOMIC_ACQUIRE))
    return refuse("a copy is already running"); /* not menu text */
  error[0] = 0;
  uintptr_t base = MAIN(0);
  fs_create_dir(SD_DIR);
  fs_file_t out;
  nn_result_t r;
  fs_delete_file(DUMP_MAIN_BIN);
  if ((r = fs_create_file(DUMP_MAIN_BIN, DUMP_MAIN_SIZE)) != 0 || (r = fs_open(&out, DUMP_MAIN_BIN, FS_WRITE)) != 0) {
    fail("couldn't make", DUMP_MAIN_BIN, r); /* not menu text */
  } else {
    static _Alignas(8) uint8_t zeros[DUMP_PAGE];
    for (uint32_t at = 0; at < DUMP_MAIN_SIZE && !error[0]; at += DUMP_CHUNK) {
      uint32_t n = DUMP_MAIN_SIZE - at < DUMP_CHUNK ? DUMP_MAIN_SIZE - at : DUMP_CHUNK;
      /* copy page by page into the work buffer: a page that isn't readable becomes zeros */
      for (uint32_t p = 0; p < n; p += DUMP_PAGE) {
        if (sys_mapped(base + at + p, DUMP_PAGE, SYS_PERM_R))
          memcpy(chunk + p, (const void *)(base + at + p), DUMP_PAGE);
        else
          memcpy(chunk + p, zeros, DUMP_PAGE);
      }
      fs_write_option_t flush = { at + n >= DUMP_MAIN_SIZE ? FS_WRITE_FLUSH : 0 };
      if ((r = fs_write(out, at, chunk, n, &flush)) != 0)
        fail("couldn't write", DUMP_MAIN_BIN, r); /* not menu text */
    }
    fs_close(out);
  }
  if (!error[0]) {
    char text[32];
    int len = snprintf(text, sizeof(text), "%llx", (unsigned long long)base);
    fs_delete_file(DUMP_MAIN_BASE);
    if ((r = fs_create_file(DUMP_MAIN_BASE, len)) != 0 || (r = fs_open(&out, DUMP_MAIN_BASE, FS_WRITE)) != 0) {
      fail("couldn't make", DUMP_MAIN_BASE, r); /* not menu text */
    } else {
      fs_write_option_t flush = { FS_WRITE_FLUSH };
      if ((r = fs_write(out, 0, text, (size_t)len, &flush)) != 0)
        fail("couldn't write", DUMP_MAIN_BASE, r); /* not menu text */
      fs_close(out);
    }
  }
  const char *res = 0;
  if (error[0]) {
    snprintf(state_result, sizeof(state_result), "%s", error);
    res = state_result;
  } else {
    snprintf(state_result, sizeof(state_result), "main.bin and main.base are in the sloperinaGZ folder on the SD card"); /* not menu text */
  }
  __atomic_store_n(&busy, 0, __ATOMIC_RELEASE);
  return res;
}

/* ---- EXPERIMENTAL: a progress.sav rebuilt from the live game ----------------
 * progress.sav (FINDINGS "Save files"): a 0x20 header (magic 0x01020304, a word, the end of the table), then
 * (hash, value) pairs up to that end, then the data they point at (String64: 64 bytes; Vector3: 3 floats). The
 * copy of slot_01 already made is the template: each pair of a field the live GameData holds as a Bool / Int /
 * Float / Enum / String64 gets its live value, the restart point (PlayerStatus.SavePos, its yaw, and the two
 * other copies of it) gets Link's position and facing. Fields of other types (UInt, arrays, ...) keep what the
 * template had. Only the copy on the SD card is written, never the game's own save. */
#define PROGRESS_MAGIC 0x01020304u
#define PROGRESS_HEADER 0x20
#define H_SAVEPOS_RADY 0x1EA93BD8u /* PlayerStatus.SavePosRadY (Float) */
static const uint32_t pos_hashes[3] = { 0xC884818Du /* PlayerStatus.SavePos */, 0x8773F02Du /* same point, name unknown */,
                                        0x960AD56Au /* Player_Pos_For_Map */ };
static _Alignas(8) uint8_t str_slot[GD_STR_LEN];

float atan2f(float y, float x); /* from sdk */

static void fail_text(const char *text)
{
  if (!error[0])
    snprintf(error, sizeof(error), "%s", text);
}

/* how many fields were brought up to date; errors go to `error` */
static int refresh_progress(const char *path)
{
  if (!gd_ready()) {
    fail_text("game data not ready (is the game loaded?)"); /* not menu text */
    return 0;
  }
  vec3f_t pos;
  double fx, fz;
  if (!link_drawn_pos(&pos) || !link_facing(&fx, &fz)) {
    fail_text("Link not found (is a scene loaded?)"); /* not menu text */
    return 0;
  }
  float yaw = atan2f((float)fx, (float)fz);
  fs_file_t f;
  nn_result_t r;
  int64_t size = 0;
  int changed = 0;
  uint32_t pos_at[3] = { 0, 0, 0 };
  if ((r = fs_open(&f, path, FS_READ | FS_WRITE)) != 0) {
    fail("couldn't open", path, r); /* not menu text */
    return 0;
  }
  fs_size(&size, f);
  size_t got = 0;
  uint32_t head[PROGRESS_HEADER / 4];
  if (fs_read(&got, f, 0, head, PROGRESS_HEADER) != 0 || got != PROGRESS_HEADER || head[0] != PROGRESS_MAGIC ||
      head[2] < PROGRESS_HEADER || head[2] > size || (head[2] - PROGRESS_HEADER) % 8) {
    fail_text("progress.sav isn't in the format expected"); /* not menu text */
    fs_close(f);
    return 0;
  }
  uint32_t end = head[2];
  fs_write_option_t keep = { 0 };
  for (int64_t at = PROGRESS_HEADER; at < end && !error[0];) {
    size_t len = end - at < COPY_CHUNK ? (size_t)(end - at) : COPY_CHUNK;
    if (fs_read(&got, f, at, chunk, len) != 0 || got != len) {
      fail("couldn't read", path, 0); /* not menu text */
      break;
    }
    uint32_t *pairs = (uint32_t *)chunk;
    bool dirty = false;
    for (size_t i = 0; i + 1 < len / 4; i += 2) {
      uint32_t h = pairs[i], v = pairs[i + 1], nv = v;
      bool is_pos = false;
      for (int k = 0; k < 3; k++)
        if (h == pos_hashes[k]) {
          pos_at[k] = v;
          is_pos = true;
        }
      if (is_pos)
        continue;
      bool b;
      float fl;
      uint32_t en;
      int32_t iv;
      if (h == H_SAVEPOS_RADY) {
        memcpy(&nv, &yaw, 4);
      } else if (gd_get_bool(h, &b)) {
        nv = b ? 1 : 0;
      } else if (gd_get_float(h, &fl)) {
        memcpy(&nv, &fl, 4);
      } else if (gd_get_enum(h, &en)) {
        nv = en;
      } else if (gd_peek_int(h, &iv)) {
        nv = (uint32_t)iv;
      } else {
        const char *s = gd_get_str(h);
        if (s && v >= end && (int64_t)v + GD_STR_LEN <= size) {
          int n = 0;
          for (; n < GD_STR_LEN - 1 && s[n]; n++)
            str_slot[n] = (uint8_t)s[n];
          for (; n < GD_STR_LEN; n++)
            str_slot[n] = 0;
          if (fs_write(f, v, str_slot, GD_STR_LEN, &keep) == 0)
            changed++;
        }
        continue;
      }
      if (nv != v) {
        pairs[i + 1] = nv;
        dirty = true;
        changed++;
      }
    }
    if (dirty && (r = fs_write(f, at, chunk, len, &keep)) != 0)
      fail("couldn't write", path, r); /* not menu text */
    at += len;
  }
  float where[3] = { pos.x, pos.y, pos.z };
  for (int k = 0; k < 3 && !error[0]; k++)
    if (pos_at[k] >= end && (int64_t)pos_at[k] + 12 <= size) {
      fs_write_option_t flush = { k == 2 ? FS_WRITE_FLUSH : 0 };
      if ((r = fs_write(f, pos_at[k], where, sizeof(where), &flush)) != 0)
        fail("couldn't write", path, r); /* not menu text */
      else
        changed++;
    }
  fs_close(f);
  return changed;
}

/* the target slot of the game -> a new save in the current category, called `wanted` (empty: save_NNN) */
const char *savestates_dump(const char *wanted, bool experimental)
{
  state_result[0] = 0;
  if (!known_mount[0])
    return refuse("save once in the game first (or pass a loading zone)"); /* not menu text */
  if (!settings_sd() || !open_dir || !read_dir || !close_dir)
    return refuse("no SD card access"); /* not menu text */
  if (state_count >= SAVESTATES_MAX)
    return refuse("this category is full"); /* not menu text */
  if (__atomic_exchange_n(&busy, 1, __ATOMIC_ACQUIRE))
    return refuse("a copy is already running"); /* not menu text */
  error[0] = 0;
  files_copied = 0;
  bytes_copied = 0;
  char cat[PATH_LEN], st[PATH_LEN], dst[PATH_LEN], src_root[48], src[PATH_LEN], target[16], name[STATE_NAME_LEN];
  fs_create_dir(SD_DIR);
  fs_create_dir(STATES_DIR);
  if (!cat_count) { /* first save ever: it needs a category */
    snprintf(cat_names[0], STATE_NAME_LEN, "default"); /* not menu text */
    cat_count = 1;
    cur_cat = 0;
    state_count = 0;
  }
  join(cat, STATES_DIR, cat_names[cur_cat]);
  fs_create_dir(cat);
  /* the name typed on the keyboard: only characters a folder name can hold, none of them at the ends */
  char base[STATE_NAME_LEN - 8];
  int bl = 0;
  for (const char *c = wanted ? wanted : ""; *c && bl < (int)sizeof(base) - 1; c++)
    if (*c >= ' ' && *c != '/' && *c != '\\' && *c != ':' && *c != '*' && *c != '?' && *c != '"' && *c != '<' &&
        *c != '>' && *c != '|' && !(bl == 0 && *c == ' '))
      base[bl++] = *c;
  while (bl > 0 && (base[bl - 1] == ' ' || base[bl - 1] == '.'))
    bl--;
  base[bl] = 0;
  for (int n = 1; n < 100000; n++) { /* the first name not taken: the typed one, then "name (2)", ..., or save_NNN */
    if (!bl)
      snprintf(name, sizeof(name), "save_%03d", n);
    else if (n == 1)
      snprintf(name, sizeof(name), "%s", base);
    else
      snprintf(name, sizeof(name), "%s (%d)", base, n);
    bool taken = false;
    for (int i = 0; i < state_count; i++)
      if (compare_names(state_names[i], name) == 0)
        taken = true;
    if (!taken)
      break;
  }
  snprintf(target, sizeof(target), "slot_%02d", TARGET_SLOT);
  snprintf(src_root, sizeof(src_root), "%s:/", known_mount);
  join(src, src_root, target);
  fs_dir_t probe;
  if (open_dir(&probe, src, FS_OPEN_DIR_ALL) != 0) { /* nothing there: don't leave an empty save behind */
    __atomic_store_n(&busy, 0, __ATOMIC_RELEASE);
    snprintf(state_result, sizeof(state_result), "no manual save yet: save in the game first"); /* not menu text */
    return state_result;
  }
  close_dir(probe);
  join(st, cat, name);
  join(dst, st, target);
  fs_create_dir(st);
  fs_create_dir(dst);
  copy_tree(src, dst, 1);
  int refreshed = 0;
  if (experimental && !error[0]) {
    char prog[PATH_LEN];
    join(prog, dst, "progress.sav");
    refreshed = refresh_progress(prog);
  }
  const char *out = 0;
  if (error[0]) {
    snprintf(state_result, sizeof(state_result), "%s", error);
    out = state_result;
  } else if (experimental) {
    snprintf(state_result, sizeof(state_result), "stored %s from the live game (%d fields)", name, refreshed); /* not menu text */
  } else {
    snprintf(state_result, sizeof(state_result), "stored %s from your manual save (%d files)", name, files_copied); /* not menu text */
  }
  scan_states();
  states_changed();
  __atomic_store_n(&busy, 0, __ATOMIC_RELEASE);
  return out;
}

void savestates_status_text(char *buf, int len)
{
  if (hook_error)
    snprintf(buf, len, TF("unavailable: %s"), hook_error); /* hook_error: not menu text */
  else if (!settings_sd())
    snprintf(buf, len, "%s", T("no SD card access"));
  else if (state_result[0])
    snprintf(buf, len, "%s", state_result);
  else if (!cat_count)
    snprintf(buf, len, "no save yet: store one from the menu, or add folders on your computer"); /* not menu text */
  else if (!known_mount[0])
    snprintf(buf, len, "save once in the game first (or pass a loading zone)"); /* not menu text */
  else
    snprintf(buf, len, "%d saves in %s", state_count, cat_names[cur_cat]); /* not menu text */
}
