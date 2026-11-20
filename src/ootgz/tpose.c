/* The ocarina-breath T-pose on demand (FINDINGS 9.13).
 *
 * Link's AS (romfs/AS/Player_*.root.asb) blends 9 layers (slots): Lower, Upper, RightArm, LeftArm, Face, Finger,
 * Mouth, MaskChange, MaskEquip. Each limits itself to a bone group. Holding a note on the ocarina for about
 * 4.5 s plays DemoOcarinaBreath in the Face layer, and that graph switches the layer's group from Face_NoMouth
 * (Root out, Face_Root in, Mouth_Root out) to OcarinaBreath (Root out, Spine_2 in, both weapons and the ten
 * finger roots out): the whole upper body, arms included. Nothing on the way out of the ocarina puts it back;
 * only a Face animation that names its own group does (idle fidgets, attacks, boots, events). Until then the
 * Face layer drives Link's chest and arms, and running with a bomb shows his arms in the bind pose.
 *
 * Where it is: slots S = [[[[actor+0x270]+0x40]+0x18]+0x18], count S+0x18, entries S+0x20 of 0x48 bytes, each
 * starting with the slot's object O. A layer's mask is a small table T = [O+0x17198] (the Face layer's sits in
 * the object before the Face slot's; it is found by its contents): [T+8] = I, I+8 capacity, [I+0x10] one byte
 * per entry (0x80 the bone and its children are in, 0x83 out), [I+0x28] u32 per entry with the skeleton's bone
 * index (bfres order) in the top 16 bits; I+0x1C counts the entries in use. Face_NoMouth uses two (Root out,
 * Face_Root in; a third, Mouth_Root, is often left over past the count), OcarinaBreath fourteen. Rewriting entry 1
 * (Face_Root) as Spine_2 is enough for the T-pose; the game rebuilds the table on its next mask change, so it is
 * written again every frame. Face_Root is the same bone in both skeletons, so the age comes from the game.
 * 1.1.1 checked in game; the walk is the same code in 1.0.0 (main+0x5754D4), and if the table isn't at +0x17198
 * there the slot objects are searched once for it. */
#include "tpose.h"

#include <stddef.h>
#include <stdint.h>

#include "age.h"
#include "link.h"
#include "sys.h"

#define ACTOR_COMPONENTS 0x270
#define COMPONENTS_AS 0x40
#define AS_NEXT 0x18          /* twice: [[components+0x40]+0x18]+0x18 */
#define SLOTS_COUNT 0x18      /* s32 */
#define SLOTS_ENTRIES 0x20
#define SLOT_ENTRY_SIZE 0x48
#define MAX_SLOTS 16
#define TABLE_PTR_GUESS 0x17198 /* 1.1.1 */
#define SLOT_SEARCH 0x18000     /* how far into a slot object to look for the table elsewhere */
#define TABLE_INNER 0x08
#define INNER_CAPACITY 0x08
#define INNER_FLAGS 0x10
#define INNER_BONES 0x28
#define MIN_CAPACITY 2
#define MAX_CAPACITY 64
#define FLAG_IN 0x80
#define FLAG_OUT 0x83
#define BONE_HALF 2 /* the bone is the u16 at +2 of each u32 entry */

#define FACE_ROOT 0x6B /* both skeletons */
#define SPINE_2_CHILD 0x0B
#define SPINE_2_ADULT 0x06

static bool enabled;
static long table_ptr = TABLE_PTR_GUESS; /* where a slot object keeps its mask table's pointer */
static uintptr_t searched_for;           /* the actor whose slots were searched for it (once) */

static uintptr_t next(uintptr_t p, size_t off)
{
  return p && sys_mapped(p + off, 8, SYS_PERM_R) ? sys_ptr(p + off) : 0;
}

typedef struct { volatile uint16_t *b1; bool glitched; } face_t;

/* the bone half of entry i */
static volatile uint16_t *bone(uintptr_t bones, int i)
{
  return (volatile uint16_t *)(bones + i * 4 + BONE_HALF);
}

/* table T is the Face layer's, with either mask (spine_2: this age's Spine_2) */
static bool face_table(uintptr_t t, uint16_t spine_2, face_t *out)
{
  uintptr_t inner = next(t, TABLE_INNER);
  if (!inner || !sys_mapped(inner, INNER_BONES + 8, SYS_PERM_R))
    return false;
  uint64_t capacity = *(const uint64_t *)(inner + INNER_CAPACITY);
  if (capacity < MIN_CAPACITY || capacity > MAX_CAPACITY)
    return false;
  uintptr_t flags = sys_ptr(inner + INNER_FLAGS), bones = sys_ptr(inner + INNER_BONES);
  if (!flags || !bones || (bones & 3) || !sys_mapped(flags, 2, SYS_PERM_R) ||
      !sys_mapped(bones, 2 * 4, SYS_PERM_R | SYS_PERM_W))
    return false;
  const volatile uint8_t *f = (const volatile uint8_t *)flags;
  if (f[0] != FLAG_OUT || f[1] != FLAG_IN || *bone(bones, 0) != 0)
    return false;
  uint16_t b1 = *bone(bones, 1);
  if (b1 != FACE_ROOT && b1 != spine_2)
    return false;
  *out = (face_t){ bone(bones, 1), b1 == spine_2 };
  return true;
}

/* the Face layer's mask table and this age's Spine_2, or false */
static bool find_face(face_t *out, uint16_t *spine_2)
{
  int age = age_current();
  if (age < 0)
    return false;
  *spine_2 = age == AGE_CHILD ? SPINE_2_CHILD : SPINE_2_ADULT;
  uintptr_t actor = link_actor();
  uintptr_t slots = next(next(next(next(actor, ACTOR_COMPONENTS), COMPONENTS_AS), AS_NEXT), AS_NEXT);
  if (!slots || !sys_mapped(slots + SLOTS_COUNT, 4, SYS_PERM_R))
    return false;
  int32_t count = *(const int32_t *)(slots + SLOTS_COUNT);
  uintptr_t entries = next(slots, SLOTS_ENTRIES);
  if (count <= 0 || count > MAX_SLOTS || !entries || !sys_mapped(entries, count * SLOT_ENTRY_SIZE, SYS_PERM_R))
    return false;
  for (int i = 0; i < count; i++)
    if (face_table(next(sys_ptr(entries + i * SLOT_ENTRY_SIZE), table_ptr), *spine_2, out))
      return true;
  if (searched_for == actor) /* a build where the table is elsewhere: look once per Link */
    return false;
  searched_for = actor;
  for (int i = 0; i < count; i++) {
    uintptr_t obj = sys_ptr(entries + i * SLOT_ENTRY_SIZE);
    if (!obj || !sys_mapped(obj, SLOT_SEARCH, SYS_PERM_R))
      continue;
    for (long off = 0; off < SLOT_SEARCH; off += 8) {
      uintptr_t p = *(const uintptr_t *)(obj + off);
      if (p > obj && p < obj + SLOT_SEARCH && face_table(p, *spine_2, out)) {
        log_printf("tpose: the mask table's pointer is at slot +%#lx (was +%#lx)", off, table_ptr);
        table_ptr = off;
        return true;
      }
    }
  }
  log_raw("tpose: no Face mask table found"); /* not menu text */
  return false;
}

bool tpose_enabled(void)
{
  return enabled;
}

void tpose_set(bool on)
{
  enabled = on;
  face_t face;
  uint16_t spine_2;
  if (!find_face(&face, &spine_2))
    return;
  if (on)
    *face.b1 = spine_2;
  else if (face.glitched)
    *face.b1 = FACE_ROOT;
}

void tpose_frame(void)
{
  if (!enabled)
    return;
  face_t face;
  uint16_t spine_2;
  if (find_face(&face, &spine_2) && !face.glitched)
    *face.b1 = spine_2;
}
