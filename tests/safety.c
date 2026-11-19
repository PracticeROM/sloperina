/* Host regression tests: the real safety.c over a mocked main, scene and GameSafetyMgr. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "safety.h"
#include "sys.h"
#include "z64hd.h"

_Static_assert(sizeof(uintptr_t) == 8, "requires a 64-bit host");

/* One block stands for everything mapped: "main" (a slot, code, strings) and the heap's objects. */
#define MEM_SIZE 0x8000
static _Alignas(0x1000) uint8_t mem[MEM_SIZE];
static uintptr_t hole, hole_size; /* a range that isn't readable */
static int reads;

#define AT(off) ((uintptr_t)mem + (off))

uintptr_t z64hd_main;
static struct z64hd_build build;
const struct z64hd_build *z64hd_game = &build;

bool sys_mapped(uintptr_t addr, size_t size, uint32_t perm)
{
  assert(perm == SYS_PERM_R);
  if (addr < AT(0) || addr + size > AT(MEM_SIZE))
    return false;
  return !(hole_size && addr < hole + hole_size && addr + size > hole);
}
uintptr_t sys_ptr(uintptr_t addr)
{
  uintptr_t value;
  reads++;
  if ((addr & 7) || !sys_mapped(addr, 8, SYS_PERM_R))
    return 0;
  memcpy(&value, (const void *)addr, 8);
  return value;
}
bool sys_read32(uintptr_t addr, uint32_t *out)
{
  reads++;
  if ((addr & 3) || !sys_mapped(addr, 4, SYS_PERM_R))
    return false;
  memcpy(out, (const void *)addr, 4);
  return true;
}
void log_printf(const char *fmt, ...) { (void)fmt; }

static void put32(uintptr_t addr, uint32_t value) { memcpy((void *)addr, &value, 4); }
static void put64(uintptr_t addr, uint64_t value) { memcpy((void *)addr, &value, 8); }
static void put_text(uintptr_t addr, const char *text) { memcpy((void *)addr, text, strlen(text) + 1); }

/* the layout */
#define SCENE_MGR_SLOT 0x100 /* main+: the scene manager's pointer */
#define STR_BEFORE 0x800     /* a name on the page before the code (adrp backwards) */
#define CODE 0x1000
#define STR_AFTER 0x2010     /* a name on the page after it */
#define SCENE_MGR 0x3000
#define SCENE_A 0x3400
#define SCENE_B 0x3600
#define COMPONENTS 0x3800    /* 64 pointers */
#define OBJECTS 0x4000       /* a component each 0x80: 64 of them end where the vtables start */
#define OBJECT_SIZE 0x80
#define OBJECT(i) AT(OBJECTS + (uint32_t)(i) * OBJECT_SIZE)
#define VT_SAFETY 0x6000
#define VT_OTHER 0x6200
#define VT_PLAIN 0x6400
#define FN_SAFETY (CODE + 0x00)
#define FN_OTHER (CODE + 0x10)
#define FN_PLAIN (CODE + 0x20)
#define FN_NEARLY (CODE + 0x30)

/* adrp x0, <page of target>; add x0, x0, #<offset in it>; ret */
static void put_getter(uintptr_t fn, uintptr_t target)
{
  int64_t pages = (int64_t)(target >> 12) - (int64_t)(fn >> 12);
  uint32_t imm = (uint32_t)pages & 0x1FFFFF;
  put32(fn, 0x90000000u | (imm & 3) << 29 | (imm >> 2) << 5);
  put32(fn + 4, 0x91000000u | (uint32_t)(target & 0xFFF) << 10);
  put32(fn + 8, 0xD65F03C0u);
}

static void put_vtable(uint32_t vtable, uint32_t name_fn, int name_slot)
{
  for (int slot = 0; slot < 32; slot++)
    put64(AT(vtable + slot * 8), AT(slot == name_slot ? name_fn : FN_PLAIN));
}

/* a scene of `count` components, all plain but for GameSafetyMgr at `entry` (-1: none) */
static uintptr_t put_scene(uint32_t scene, uint32_t count, int entry)
{
  memset(mem + COMPONENTS, 0, 64 * 8);
  for (uint32_t i = 0; i < count && i < 64; i++) {
    uintptr_t object = OBJECT(i);
    memset((void *)object, 0, OBJECT_SIZE);
    put64(object, AT((int)i == entry ? VT_SAFETY : i % 2 ? VT_OTHER : VT_PLAIN));
    if (i != 3) /* an empty entry, as the game's scenes have */
      put64(AT(COMPONENTS + i * 8), object);
  }
  put32(AT(scene + 0x50), count);
  put64(AT(scene + 0x58), AT(COMPONENTS));
  put64(AT(SCENE_MGR + 0x1F0), AT(scene));
  return entry >= 0 ? OBJECT(entry) : 0;
}

static void setup(void)
{
  memset(mem, 0, sizeof(mem));
  hole_size = 0;
  z64hd_main = AT(0);
  build.scene_mgr_ptr = SCENE_MGR_SLOT;
  put64(AT(SCENE_MGR_SLOT), AT(SCENE_MGR));
  put_text(AT(STR_AFTER), "GameSafetyMgr");
  put_text(AT(STR_BEFORE), "AutoSaveMgr");
  put_text(AT(STR_BEFORE + 0x20), "GameSafetyMgrEx"); /* a longer name that starts the same */
  put_getter(AT(FN_SAFETY), AT(STR_AFTER));
  put_getter(AT(FN_OTHER), AT(STR_BEFORE));
  put_getter(AT(FN_NEARLY), AT(STR_BEFORE + 0x20));
  put32(AT(FN_PLAIN), 0xD503201Fu); /* nop; ret: not a name getter */
  put32(AT(FN_PLAIN + 4), 0xD65F03C0u);
  put_vtable(VT_SAFETY, FN_SAFETY, 8);
  put_vtable(VT_OTHER, FN_OTHER, 8);
  put_vtable(VT_PLAIN, FN_PLAIN, -1);
}

static uint32_t murmur3(const char *text)
{
  size_t len = strlen(text);
  uint32_t h = 0;
  for (size_t i = 0; i + 4 <= len; i += 4) {
    uint32_t k;
    memcpy(&k, text + i, 4);
    k *= 0xCC9E2D51u, k = k << 15 | k >> 17, k *= 0x1B873593u;
    h ^= k, h = h << 13 | h >> 19, h = h * 5 + 0xE6546B64u;
  }
  uint32_t k = 0;
  for (size_t i = len & 3; i > 0; i--)
    k = k << 8 | (uint8_t)text[(len & ~(size_t)3) + i - 1];
  if (len & 3) {
    k *= 0xCC9E2D51u, k = k << 15 | k >> 17, k *= 0x1B873593u;
    h ^= k;
  }
  h ^= (uint32_t)len;
  h ^= h >> 16, h *= 0x85EBCA6Bu, h ^= h >> 13, h *= 0xC2B2AE35u, h ^= h >> 16;
  return h;
}

static void test_names(void)
{
  static const char *const names[] = {
    "Common_DisableWarp", "Common_ShutterEnemy", "Default", "Defeat_Ganon", "Defeat_Ganon_InBattle", "DoorOfTime",
    "Get_Epona", "Get_StoneOfFire", "Get_StoneOfForest", "Get_StoneOfWater", "Get_StoneOfWater_MiddleBoss",
    "Market_Item_Bottle_TutorialEvent", "MiniGame", "MiniGame_PossibleWarp", "MiniGame_PossibleWarp_DiggingHole",
    "MiniGame_TimeStop", "SageOfFire", "SageOfForest", "SageOfForest_Step06", "SageOfShadow",
    "SageOfShadow_BattleOnShip", "SageOfShadow_BeforeBossRoom", "SageOfSpirit", "SageOfWater",
    "SageOfWater_MiddleBoss", "SubEvent_AllBossBattle", "SubEvent_GetStormSong",
    "SubEvent_GetStormSong_BeforeBossRoom",
  };
  assert(sizeof(names) / sizeof(*names) == 28);
  assert(murmur3("MiniGame_TimeStop") == 0xFAE899F0u && murmur3("MiniGame") == 0x3408E01Bu);
  for (unsigned i = 0; i < sizeof(names) / sizeof(*names); i++) {
    const char *name = safety_setting_name(murmur3(names[i]));
    assert(name && !strcmp(name, names[i])); /* each hash in the table is its name's */
    assert(strlen(name) <= 40);              /* fits a watch's value */
  }
  assert(!safety_setting_name(0) && !safety_setting_name(0xFAE899F1u));
  puts("PASS: all 28 setting names by MurmurHash3, unknown hashes");
}

static void test_flag_words(void)
{
  char text[96], small[8];
  safety_flags_text(0x14C8, text, sizeof(text)); /* MiniGame_TimeStop, read in game */
  assert(!strcmp(text, "on save warp grab time"));
  safety_flags_text(0, text, sizeof(text));
  assert(!strcmp(text, "0"));
  safety_flags_text(SAFETY_MINIGAME | SAFETY_NO_TEMP_SAVE_ERROR, text, sizeof(text));
  assert(!strcmp(text, "mini err"));
  safety_flags_text(0xFFFF, text, sizeof(text));
  assert(!strcmp(text, "mini on area save warp place box grab boot time end pause err +0x16"));
  safety_flags_text(0x0004, text, sizeof(text));
  assert(!strcmp(text, "+0x4"));
  memset(small, 'x', sizeof(small));
  safety_flags_text(0xFFFF, small, 6); /* cut, terminated, nothing past the end */
  assert(!strcmp(small, "mini ") && small[6] == 'x');
  puts("PASS: flag words, unknown bits, none, truncation");
}

static bool state(uint16_t *flags, uint32_t *hash)
{
  *flags = 0xAAAA, *hash = 0xAAAAAAAAu;
  return safety_state(flags, hash);
}

static void test_find(void)
{
  uint16_t flags;
  uint32_t hash;
  uintptr_t flags_addr, hash_addr;

  setup();
  build.scene_mgr_ptr = 0; /* a build whose scene manager isn't known */
  put_scene(SCENE_A, 41, 28);
  reads = 0;
  assert(!state(&flags, &hash) && !safety_addresses(&flags_addr, &hash_addr) && reads == 0);

  setup();
  uintptr_t mgr = put_scene(SCENE_A, 41, 28);
  put32(mgr + 0x50, 0xBEEF14C8u); /* only the low 16 bits are the flags */
  put32(mgr + 0x54, 0xFAE899F0u);
  assert(state(&flags, &hash) && flags == 0x14C8 && hash == 0xFAE899F0u);
  assert(safety_addresses(&flags_addr, &hash_addr) && flags_addr == mgr + 0x50 && hash_addr == mgr + 0x54);
  reads = 0;
  assert(state(&flags, &hash) && reads <= 6); /* kept: no search while the scene and manager stay */
  put32(mgr + 0x50, 0x0048), put32(mgr + 0x54, 0x3408E01Bu); /* a live change shows at once */
  assert(state(&flags, &hash) && flags == 0x0048 && hash == 0x3408E01Bu);

  /* another scene, the manager at another entry */
  mgr = put_scene(SCENE_B, 20, 5);
  put32(mgr + 0x50, 0x0008), put32(mgr + 0x54, 0x48DB8265u);
  assert(state(&flags, &hash) && flags == 0x0008 && hash == 0x48DB8265u);
  assert(!strcmp(safety_setting_name(hash), "Default"));

  /* the same scene, the manager replaced by something else: looked for again, not read stale */
  put64(mgr, AT(VT_PLAIN));
  assert(!state(&flags, &hash));
  puts("PASS: found at entry 28 and elsewhere, low 16 bits, kept, live changes, scene change, replaced");
}

static void test_absent(void)
{
  uint16_t flags;
  uint32_t hash;

  setup();
  put_scene(SCENE_A, 41, -1);
  assert(!state(&flags, &hash));
  reads = 0;
  assert(!state(&flags, &hash) && reads <= 2); /* not searched again at once */
  uintptr_t mgr = put_scene(SCENE_A, 41, 30);  /* it turns up later in the same scene */
  put32(mgr + 0x50, 0x0080), put32(mgr + 0x54, 0xEDB12E71u);
  bool found = false;
  for (int call = 0; call < 40 && !found; call++)
    found = state(&flags, &hash);
  assert(found && flags == 0x0080 && hash == 0xEDB12E71u);

  setup();
  put_scene(SCENE_B, 41, -1);
  put64(OBJECT(28), AT(VT_OTHER)); /* a different component's name */
  put_vtable(VT_OTHER, FN_NEARLY, 8);            /* and one whose name only starts the same */
  assert(!state(&flags, &hash));

  setup();
  put64(AT(SCENE_MGR + 0x1F0), 0); /* no current scene */
  assert(!state(&flags, &hash));
  setup();
  put64(AT(SCENE_MGR_SLOT), 0); /* no scene manager */
  assert(!state(&flags, &hash));
  setup();
  put_scene(SCENE_A, 65, 28); /* a count that can't be right */
  assert(!state(&flags, &hash));
  setup();
  put_scene(SCENE_B, 41, 28);
  put64(AT(SCENE_B + 0x58), 0xDEAD0000); /* the components aren't mapped */
  assert(!state(&flags, &hash));
  setup();
  put_scene(SCENE_A, 41, 28);
  hole = AT(STR_AFTER & ~0xFFFu), hole_size = 0x1000; /* the name's page isn't readable */
  assert(!state(&flags, &hash));
  setup();
  mgr = put_scene(SCENE_B, 41, 28);
  put_vtable(VT_SAFETY, FN_SAFETY, 24); /* the getter past the slots looked at */
  assert(!state(&flags, &hash));
  (void)mgr;
  puts("PASS: absent, retried later, other names, null/unmapped/implausible chains");
}

int main(void)
{
  test_names();
  test_flag_words();
  test_find();
  test_absent();
  return 0;
}
