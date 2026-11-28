/* Interaction ranges (FINDINGS 9.37): where Link can talk to or act on the actors near him.
 *
 * What A does near an actor comes from its attention clients (data_interact_*, tools/interactions.py): each an action
 * (Talk, Check, OpenDoor...) and areas that must all hold. A fan "from Link" is in front of him: the actor has to be
 * within its radius and its angle of the way he faces, and within its height band (top over him, bottom under); seen
 * round the actor that's a disc he has to stand in, facing it. A fan "from the actor" is in front of the actor
 * (its angle the whole of it); a box is in the actor's own axes; a sphere is round it. Most are "sized": the actor's
 * own model box, read live as the game's checks read it, is counted in (a box is grown out from it, a fan's reach
 * gets its widest half): the same files give a broad shopkeeper a much bigger box than a thin one.
 *
 * Drawn round each actor: where all of a client's areas hold on the ground, sampled out from the actor along rays
 * (the shapes are worked out in the actor's own frame, so turning it needs nothing new); filled for what A does
 * there, an outline for what only counts while Z-targeting (an NPC's further Talk). Cyan talking, lilac anything
 * else; brighter while Link is in range and facing as it asks, the game's own test as near as the files tell it
 * (walls between aren't counted). Once it has his attention each reach holds as far again as its margin (0.5 m for most),
 * and a lock-on stops checking the angle: dashed, where it breaks as he walks away; the brightness holds to there. */
#include "interactview.h"

#include <string.h>

#include "data.h"
#include "enemyhp.h" /* actors_near, actor_read */
#include "gfx.h"
#include "guardview.h" /* bone matrices */
#include "lang.h"
#include "link.h"
#include "memfile.h" /* memfile_loading */
#include "placer.h" /* the actor browser: its spot and actor */
#include "sys.h"
#include "z64hd.h"

float sinf(float x), cosf(float x), atan2f(float y, float x); /* from sdk */

bool interactview_shown;
bool interactview_parts[IV_PARTS] = { true, true, true, true };

#define RANGE 20.0f       /* metres from Link: actors further aren't drawn */
#define MAX_ACTORS 24     /* the nearest this many with interactions */
#define NEAR_READ 96      /* actors_near's buffer */
#define SCAN_FRAMES 15
#define MAX_CLIENTS 4     /* an actor's clients drawn (the first ones: the game lists its main action first) */
#define RAYS 40           /* round an actor, for its regions */
#define STEP 0.05f        /* metres along a ray between samples */
#define MAX_REACH 12.0f   /* metres: no region goes further */
#define LIFT 0.06f
#define DEG (3.14159265f / 180.0f)
#define PI 3.14159265f
#define ACTOR_ROT 0x328   /* 3x3, rows */
#define ACTOR_MODEL_BOX 0x3B8 /* min x y z, then max x y z: its model's bounds in its own axes (both builds) */

enum { IA_FAN, IA_CYLINDER_FAN, IA_SPHERE, IA_BOX };

typedef struct {
  float inner[RAYS], outer[RAYS]; /* along each ray from the actor (ray k at 2 pi k / RAYS round from its front): where
                                     the region starts and ends, outer < 0 for none */
} region_t;

/* the actor's model box, as its attention checks read it (main+0x2C565D0 box, main+0x2C54430 fan): a box area is
 * grown out from it, a fan's radius gets its widest half (the largest of its x and z bounds, either way) */
typedef struct { float lo[3], hi[3], half; } model_t;

typedef struct {
  uintptr_t actor;
  const interact_actor_t *row;
  model_t model;
  float pos[3], front[2], side[2]; /* its feet, and its own axes flattened (+z front, +x side) */
  int n_clients;
  region_t regions[MAX_CLIENTS]; /* where it's taken up */
  region_t held[MAX_CLIENTS];    /* where it holds once taken up: walking away, it breaks past here */
  bool in_range[MAX_CLIENTS];   /* the files' estimate (kept for the target break line's hold, not shown) */
  bool off[MAX_CLIENTS];        /* its action switched off now (a drowning cucco can't be lifted): not drawn */
  bool target;                  /* the game's own answer: A acts on it now (its sender reserved by Link) */
  bool candidate;               /* and what ZL would take for an action (a Z-targeted Talk) */
  bool holds_door; /* a door, and Link is in the half it's held open by (it won't close behind him) */
} shown_t;

static shown_t shown[MAX_ACTORS];

/* Whether the actor's AI has switched an action off (OneShotAttentionSenderChangeClientValid, main+0x88B4B0): its
 * attention sender [comps+0x160] keeps a byte per DoActionType at +0x3C, a bit per who switched it off (0 on). A
 * cucco's drowning switches Lift off till it's out (FINDINGS 9.47). DoActionType's names, in order: */
#define SENDER_OFF 0x3C
static int compare(const char *a, const char *b);
static const char *const do_actions[] = {
  "None", "Talk", "Awake", "Lift", "OpenDoor", "OpenShutter", "OpenTreasureBox", "PickUp", "Take", "Sleep", "Check",
  "Buy", "Ride", "PullOut", "Crawl", "Grab", "BringAlong", "UseBeansHole", "Catch", "PutIn", "OpenForEvent",
  "Rematch", "Touch", "GrabSword", "OpenChainLock", "BlueFireTarget", "FatalBlow", "BlueFishTarget",
  "DiggingHoleTour", /* not menu text */
};
#define N_DO_ACTIONS (int)(sizeof(do_actions) / sizeof(do_actions[0]))

static bool action_off(uintptr_t actor, const char *action)
{
  int type = -1;
  for (int i = 0; action && i < N_DO_ACTIONS && type < 0; i++)
    if (!compare(action, do_actions[i]))
      type = i;
  uintptr_t comps = type > 0 ? sys_ptr(actor + 0x270) : 0;
  uintptr_t sender = comps && !(comps & 7) && sys_mapped(comps, 0x168, SYS_PERM_R) ? sys_ptr(comps + 0x160) : 0;
  if (!sender || (sender & 7) || !sys_mapped(sender + SENDER_OFF, N_DO_ACTIONS, SYS_PERM_R))
    return false;
  return *(const uint8_t *)(sender + SENDER_OFF + type) != 0;
}
static int n_shown, scan_wait;
static uintptr_t found[MAX_ACTORS];
static int n_found;
static actor_near_t near_buf[NEAR_READ];

/* an actor's own switch (the actor browser's options), off until turned on */
#define MAX_CHOICES 16
static struct { uintptr_t actor; bool shown[IV_PARTS]; } choices[MAX_CHOICES];
static int n_choices;

/* --- the table ------------------------------------------------------------------------------------- */

static int compare(const char *a, const char *b)
{
  while (*a && *a == *b)
    a++, b++;
  return (unsigned char)*a - (unsigned char)*b;
}

static const interact_actor_t *row_of(const char *name)
{
  int lo = 0, hi = data_n_interact_actors - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, c = compare(name, data_interact_actors[mid].actor);
    if (!c)
      return &data_interact_actors[mid];
    if (c < 0)
      hi = mid - 1;
    else
      lo = mid + 1;
  }
  return 0;
}

/* an actor found just now (name: its own, as read): the data's by its pack's name, chests running as TBox_Field_Iron */
static const interact_actor_t *row_of_named(uintptr_t actor, const char *name)
{
  const char *pack = actor_pack_name(actor);
  const interact_actor_t *row = pack ? row_of(pack) : 0;
  return row ? row : name ? row_of(name) : 0;
}

static const interact_actor_t *row_of_actor(uintptr_t actor)
{
  actor_read_t a;
  return actor && actor_read(actor, &a) ? row_of_named(actor, a.name) : 0;
}

/* --- choices ------------------------------------------------------------------------------------- */

static int choice_of(uintptr_t actor)
{
  for (int i = 0; i < n_choices; i++)
    if (choices[i].actor == actor)
      return i;
  return -1;
}

bool interactview_has(uintptr_t actor) { return row_of_actor(actor) != 0; }

bool interactview_has_part(uintptr_t actor, int part)
{
  const interact_actor_t *row = row_of_actor(actor);
  if (!row)
    return false;
  if (part == IV_DOOR)
    return row->door_radius > 0;
  if (part == IV_BREAK)
    return row->n > 0;
  for (int c = 0; c < row->n; c++) /* A's reach: its plain clients; Z-targeting's: its lock-on ones */
    if (data_interact_clients[row->first + c].lock_on == (part == IV_ZTARGET))
      return true;
  return false;
}

bool interactview_is_door(uintptr_t actor)
{
  const interact_actor_t *row = row_of_actor(actor);
  return row && row->door_radius > 0;
}

/* A door's close countdown (FINDINGS 9.38). A SelectorDoorCanClose node keeps a float at node+0x20: CloseWaitFrame
 * (135) while anyone holds the door open, counted down once nobody does; the door closes at 0. A door has two: one in
 * its own AI (a module made the first time the door opens, then kept: it runs when an NPC opens the door) and one in
 * the event Link's opening runs (FieldDoor.event: made for that event). Nothing of the door's leads to the event's, so
 * both are looked for by their class: the vtable at +0, the unit at +8 (the unit's +8 its controller, whose +0x1F0 is
 * the door). The search goes through the heap a slice a frame while a door is watched or being shut, starting where
 * the last one was found, and keeps going (an event's node is made anew each time). The watch shows whichever node is
 * counting (its value changed lately), else the event's, else the door's own. */
#define DOOR_NODE_TIMER 0x20
#define DOOR_NODE_RADIUS 0x40     /* -> DoorRadius (an input: a pointer) */
#define DOOR_SCAN_SLICE (2u << 20) /* bytes a frame (the heap search: a fallback) */
#define DOOR_WALK_FRAMES 6         /* its controllers' nodes are looked through this often */
#define DOOR_CTRL_FRAMES 120       /* and which controllers it has */
#define UNIT_NODES_SPAN 0x4000     /* bytes after a unit its nodes are looked for in */
#define DOOR_SCAN_NEAR (64u << 20) /* a search starts this far before the last node found */
#define DOOR_NODES 4

static float absf(float v) { return v < 0 ? -v : v; }

#define DOOR_REBIND_FRAMES 60
static uintptr_t watched_door, door_hint;
static uint64_t watched_door_id;
static int watched_door_rebind;
static uintptr_t scan_at, scan_start, scan_heap_lo, scan_heap_hi, scan_region_end;
static bool scan_wrapped;
static struct {
  uintptr_t node;
  float last;    /* its count last frame */
  int quiet;     /* frames since it changed */
  bool own;      /* the door's own AI's (its AIComponent's controller), not an event's */
} door_nodes[DOOR_NODES];
static int n_door_nodes;
static uintptr_t nodes_door; /* the door door_nodes are for */

/* the door's own close node at this address (its class, and through its unit and controller, this door) */
static bool door_node_of(uintptr_t node, uintptr_t door)
{
  uint32_t vt = z64hd_game ? z64hd_game->door_close_node_vtable : 0;
  if (!vt || !node || (node & 7) || !sys_mapped(node, 0x48, SYS_PERM_R) ||
      *(uintptr_t *)node != z64hd_main + vt)
    return false;
  uintptr_t unit = sys_ptr(node + 8);
  uintptr_t ctrl = unit && !(unit & 7) ? sys_ptr(unit + 8) : 0;
  return ctrl && !(ctrl & 7) && sys_mapped(ctrl + 0x1F0, 8, SYS_PERM_R) && sys_ptr(ctrl + 0x1F0) == door;
}

/* the controller of the door's AIComponent (actor+0x270 -> +0x48 -> +0x28 -> first entry +0x18, FINDINGS 9.18) */
static uintptr_t own_controller(uintptr_t door)
{
  uintptr_t comps = sys_ptr(door + 0x270);
  uintptr_t ai = comps && !(comps & 7) ? sys_ptr(comps + 0x48) : 0;
  uintptr_t set = ai && !(ai & 7) ? sys_ptr(ai + 0x28) : 0;
  uintptr_t entries = set && !(set & 7) ? sys_ptr(set + 8) : 0;
  uintptr_t entry = entries && !(entries & 7) ? sys_ptr(entries) : 0;
  return entry && !(entry & 7) ? sys_ptr(entry + 0x18) : 0;
}

/* the door's AI controllers (their +0x1F0 the door): its AIComponent's, and the others its components hold, one or
 * two pointers in or through a holder's +0x18 (the event performer's: [[comps+0x88]+0x50] for Link's walk-through
 * event, whose close module is made partway through the event) */
#define DOOR_CTRLS 6
static uintptr_t door_ctrls[DOOR_CTRLS];
static int n_door_ctrls;

static bool is_ctrl(uintptr_t p, uintptr_t actor)
{
  return p && !(p & 7) && sys_mapped(p + 0x1F0, 8, SYS_PERM_R) && sys_ptr(p + 0x1F0) == actor;
}

static void add_ctrl(uintptr_t c)
{
  for (int i = 0; i < n_door_ctrls; i++)
    if (door_ctrls[i] == c)
      return;
  if (n_door_ctrls < DOOR_CTRLS)
    door_ctrls[n_door_ctrls++] = c;
}

static void find_controllers(uintptr_t door)
{
  n_door_ctrls = 0;
  uintptr_t own = own_controller(door);
  if (is_ctrl(own, door))
    add_ctrl(own);
  uintptr_t comps = sys_ptr(door + 0x270);
  if (!comps || (comps & 7))
    return;
  for (int k = 0; k < 0x400; k += 8) {
    uintptr_t c = sys_ptr(comps + k);
    if (!c || (c & 7) || !sys_mapped(c, 0x100, SYS_PERM_R))
      continue;
    if (is_ctrl(c, door))
      add_ctrl(c);
    for (int j = 0; j < 0x100; j += 8) {
      uintptr_t d = sys_ptr(c + j);
      if (!d || (d & 7) || d == door || d == comps)
        continue;
      if (is_ctrl(d, door))
        add_ctrl(d);
      else if (sys_mapped(d + 0x18, 8, SYS_PERM_R) && is_ctrl(sys_ptr(d + 0x18), door))
        add_ctrl(sys_ptr(d + 0x18));
    }
  }
}

/* its controllers' units' nodes looked through for close nodes; each found goes to found() */
static void walk_controllers(uintptr_t door, void (*found)(uintptr_t node, uintptr_t door))
{
  for (int c = 0; c < n_door_ctrls; c++) {
    uintptr_t ctrl = door_ctrls[c];
    uint32_t count = 0;
    if (!is_ctrl(ctrl, door) || !sys_read32(ctrl + 0x18, &count) || count > 64)
      continue;
    uintptr_t units = sys_ptr(ctrl + 0x20);
    for (uint32_t j = 0; units && !(units & 7) && j < count; j++) {
      uintptr_t unit = sys_ptr(units + 0x18 * j);
      uint32_t n = 0;
      if (!unit || (unit & 7) || sys_ptr(unit + 8) != ctrl || !sys_read32(unit + 0x28, &n) || n > 1024)
        continue;
      uintptr_t nodes = sys_ptr(unit + 0x30);
      if (nodes && !(nodes & 7) && sys_mapped(nodes, (size_t)n * 0x18, SYS_PERM_R))
        for (uint32_t k = 0; k < n; k++) {
          uintptr_t node = sys_ptr(nodes + 0x18 * k);
          if (door_node_of(node, door))
            found(node, door);
        }
      /* a module's nodes aren't in that list, but they're made just after their unit (the shop door's close node
       * at its unit +0xAE0): the memory after it looked through for their class */
      uint32_t vt = z64hd_game ? z64hd_game->door_close_node_vtable : 0;
      if (!vt || !sys_mapped(unit, UNIT_NODES_SPAN, SYS_PERM_R))
        continue;
      const uint64_t want = z64hd_main + vt;
      for (const uint64_t *w = (const uint64_t *)unit; (uintptr_t)w < unit + UNIT_NODES_SPAN; w++)
        if (*w == want && door_node_of((uintptr_t)w, door))
          found((uintptr_t)w, door);
    }
  }
}

static void scan_restart(void)
{
  uint64_t start = 0, size = 0;
  if (!sys_get_info(4, 0, &start) || !sys_get_info(5, 0, &size) || !size) { /* the heap region */
    scan_heap_lo = scan_heap_hi = 0;
    return;
  }
  scan_heap_lo = (uintptr_t)start, scan_heap_hi = (uintptr_t)(start + size);
  uintptr_t from = door_hint > scan_heap_lo + DOOR_SCAN_NEAR ? door_hint - DOOR_SCAN_NEAR : scan_heap_lo;
  scan_at = scan_start = from < scan_heap_hi ? from & ~(uintptr_t)7 : scan_heap_lo;
  scan_region_end = scan_at;
  scan_wrapped = false;
}

/* one slice of the search; a node found (the search goes on from after it next time), or 0 */
static uintptr_t scan_step(uintptr_t door)
{
  uint32_t vt = z64hd_game ? z64hd_game->door_close_node_vtable : 0;
  if (!vt)
    return 0;
  if (scan_heap_hi <= scan_heap_lo)
    scan_restart();
  if (scan_heap_hi <= scan_heap_lo)
    return 0;
  const uint64_t want = z64hd_main + vt;
  size_t budget = DOOR_SCAN_SLICE;
  while (budget) {
    if (scan_wrapped && scan_at >= scan_start) { /* round once: again */
      scan_restart();
      return 0;
    }
    if (scan_at >= scan_heap_hi) {
      scan_at = scan_region_end = scan_heap_lo;
      scan_wrapped = true;
      continue;
    }
    if (scan_at >= scan_region_end) { /* the next readable, writable region */
      sys_meminfo_t info;
      if (sys_query_memory(&info, scan_at) != 0 || !info.size) {
        scan_at = scan_heap_hi;
        continue;
      }
      scan_region_end = (uintptr_t)(info.addr + info.size);
      if ((info.perm & 3) != 3 || info.type == 0) {
        scan_at = scan_region_end;
        continue;
      }
    }
    uintptr_t stop = scan_region_end < scan_heap_hi ? scan_region_end : scan_heap_hi;
    if (scan_wrapped && stop > scan_start && scan_at < scan_start)
      stop = scan_start;
    if (stop - scan_at > budget)
      stop = scan_at + budget;
    budget -= stop - scan_at;
    for (const uint64_t *w = (const uint64_t *)scan_at; (uintptr_t)w < stop; w++)
      if (*w == want && door_node_of((uintptr_t)w, door)) {
        scan_at = (uintptr_t)(w + 1);
        return (uintptr_t)w;
      }
    scan_at = stop;
  }
  return 0;
}

static float node_count(uintptr_t node)
{
  float v;
  memcpy(&v, (const void *)(node + DOOR_NODE_TIMER), sizeof(v));
  return v;
}

/* the node to show: counting lately, else an event's, else the door's own; -1 with none */
static int live_node(void)
{
  int best = -1;
  for (int i = 0; i < n_door_nodes; i++) {
    if (best < 0)
      best = i;
    else if (door_nodes[i].quiet < door_nodes[best].quiet)
      best = i;
    else if (door_nodes[i].quiet == door_nodes[best].quiet && !door_nodes[i].own && door_nodes[best].own)
      best = i;
  }
  return best;
}

static void add_door_node(uintptr_t node, uintptr_t door)
{
  for (int i = 0; i < n_door_nodes; i++)
    if (door_nodes[i].node == node)
      return;
  if (n_door_nodes == DOOR_NODES) /* the oldest makes room */
    memmove(&door_nodes[0], &door_nodes[1], sizeof(door_nodes[0]) * --n_door_nodes);
  uintptr_t ctrl = sys_ptr(sys_ptr(node + 8) + 8);
  door_nodes[n_door_nodes].node = node;
  door_nodes[n_door_nodes].last = node_count(node);
  door_nodes[n_door_nodes].quiet = 1000;
  door_nodes[n_door_nodes].own = ctrl == own_controller(door);
  n_door_nodes++;
  door_hint = node;
  log_printf("door close watch: %#lx's node %#lx (%s)", (unsigned long)door, (unsigned long)node,
             door_nodes[n_door_nodes - 1].own ? "its own AI" : "an event's");
}

/* --- the door's actions: open it by itself, shut it now ---------------------------------------------------- */

/* Opening: what an NPC does. The door's own AI opens it while its attention sender ([[door+0x270]+0x160]) is
 * reserved for its do-action by someone other than Link (SelectorAttentionSenderIsPlayerReservedDoAction,
 * main+0x749340: sender+0xAC 1, +0xB0 0x9AB0000 + the action, OpenDoor 4; the reserver through the handle at +0xB8,
 * {+8 its actor-table entry, +0x10 the entry's generation}: Link (a player component) runs his walk-through event,
 * anyone else just opens it). The door's own handle stands in as the reserver for a moment, then the sender is put
 * back as it was; nobody near, it shuts CloseWaitFrame frames later as for an NPC. */
#define SENDER_RESERVED 0xAC
#define SENDER_ACTION 0xB0
#define SENDER_HANDLE 0xB8
#define RESERVE_OPEN_DOOR (0x9AB0000u + 4)
#define OPEN_HOLD 20              /* frames the reservation is kept */
/* Shutting: its count set to 0, and DoorRadius pointed at a 0 of ours for a while, so nobody near holds it (the game
 * puts the count back to CloseWaitFrame while anyone is within DoorRadius on its swing side) */
#define CLOSE_HOLD 90             /* frames the shutting is kept up at most (it stops once the door is shut) */
#define CLOSE_LOOK_MAX 240        /* frames to look for the nodes of a door to shut */

static const float no_radius = 0;
static uintptr_t close_door;
static int close_look, close_hold;
static struct { uintptr_t node, radius, door; } held[DOOR_NODES];
static bool walk_now; /* look through its controllers this frame (a shut asked for) */
static int n_held;
static uintptr_t open_door, open_sender;
static int open_hold;
static uint8_t open_saved[0x28]; /* sender +0xA8..+0xD0 as it was */

static uintptr_t searched_door(void) { return close_door ? close_door : watched_door; }

void interactview_door_watch(uintptr_t door)
{
  if (door == watched_door && (!door || watched_door_rebind == 0))
    return;
  watched_door = door;
  watched_door_id = 0;
  watched_door_rebind = 0;
  if (door) {
    actor_instance_id(door, &watched_door_id);
    log_printf("door close watch: %#lx (id %#lx)", (unsigned long)door, (unsigned long)watched_door_id);
  }
}

uintptr_t interactview_door_watched(void) { return watched_door; }

/* the door's slot in the actor table: an entry ([actor+..]) whose +0x40 is the actor, its generation at +0x48 */
static bool door_handle(uintptr_t door, uintptr_t *entry, uint32_t *generation)
{
  for (uint32_t o = 0; o < 0x400; o += 8) {
    uintptr_t e = sys_ptr(door + o);
    if (!e || (e & 7) || (e >= door && e < door + 0x1000) || !sys_mapped(e + 0x40, 0x10, SYS_PERM_R))
      continue;
    if (sys_ptr(e + 0x40) == door) {
      *entry = e;
      *generation = *(const uint32_t *)(e + 0x48);
      return true;
    }
  }
  return false;
}

bool interactview_door_open(uintptr_t door)
{
  if (!interactview_is_door(door))
    return false;
  if (open_sender || interactview_door_is_open(door) == 1) /* opening already, or open */
    return true;
  uintptr_t comps = sys_ptr(door + 0x270);
  uintptr_t sender = comps && !(comps & 7) ? sys_ptr(comps + 0x160) : 0;
  uintptr_t entry;
  uint32_t generation;
  if (!sender || (sender & 7) || !sys_mapped(sender + 0xA8, sizeof(open_saved), SYS_PERM_R | SYS_PERM_W) ||
      !door_handle(door, &entry, &generation)) {
    gfx_notice(T("open door: this door can't be opened from here"));
    return false;
  }
  if (*(const uint8_t *)(sender + SENDER_RESERVED)) /* someone's opening it already (Link, an NPC) */
    return true;
  memcpy(open_saved, (const void *)(sender + 0xA8), sizeof(open_saved));
  *(uintptr_t *)(sender + SENDER_HANDLE + 8) = entry;
  *(uint32_t *)(sender + SENDER_HANDLE + 0x10) = generation;
  *(uint32_t *)(sender + SENDER_ACTION) = RESERVE_OPEN_DOOR;
  *(uint8_t *)(sender + SENDER_RESERVED) = 1;
  open_door = door, open_sender = sender, open_hold = OPEN_HOLD;
  log_printf("open door: %#lx: its sender %#lx reserved by itself (entry %#lx, %u)", (unsigned long)door,
             (unsigned long)sender, (unsigned long)entry, (unsigned)generation);
  return true;
}

/* Whether it's open: the pose of its Root bone, which its open and shut animations (Obj_Base_Door_A's Door_Push_* /
 * Door_Close_*) swing round its hinge: open while it's turned more than DOOR_OPEN_DEG from the door's own frame,
 * however it was opened. Its AI's blackboard bool IsOpenDoor ([[door+0x270]+0]+0x10: names at +0x190, count +0x188,
 * values a byte each at +0x1A0) isn't used: it stays set after a door Link went through has shut (his event shuts it,
 * not the door's AI), so it's only the fallback for a door with no Root bone. */
#define DOOR_OPEN_DEG 6.0f
#define ACTOR_BLACKBOARD 0x10
#define BB_BOOL_COUNT 0x188
#define BB_BOOL_NAMES 0x190
#define BB_BOOL_VALUES 0x1A0

static int blackboard_open(uintptr_t door)
{
  uintptr_t comps = door ? sys_ptr(door + 0x270) : 0;
  uintptr_t first = comps && !(comps & 7) ? sys_ptr(comps) : 0;
  uintptr_t bb = first && !(first & 7) ? sys_ptr(first + ACTOR_BLACKBOARD) : 0;
  uint32_t n = 0;
  if (!bb || (bb & 7) || !sys_read32(bb + BB_BOOL_COUNT, &n) || !n || n > 256)
    return -1;
  uintptr_t names = sys_ptr(bb + BB_BOOL_NAMES), values = sys_ptr(bb + BB_BOOL_VALUES);
  if (!names || (names & 7) || !values || !sys_mapped(names, n * 8, SYS_PERM_R) || !sys_mapped(values, n, SYS_PERM_R))
    return -1;
  for (uint32_t i = 0; i < n; i++) {
    uintptr_t name = sys_ptr(names + 8 * i);
    if (name && sys_mapped(name, 11, SYS_PERM_R) && !memcmp((const void *)name, "IsOpenDoor", 11))
      return *(const uint8_t *)(values + i) ? 1 : 0;
  }
  return -1;
}

/* how far its panel is turned from shut, in degrees (0..180), or -1 without a Root bone */
float interactview_door_angle(uintptr_t door)
{
  float m[12];
  if (!door || !guardview_bone_matrix(door, "Root", m) || !sys_mapped(door + ACTOR_ROT, 36, SYS_PERM_R))
    return -1;
  const float *rot = (const float *)(door + ACTOR_ROT);
  float bx = m[0], bz = m[8], ax = rot[0], az = rot[6]; /* the bone's +x and the door's, flattened */
  float bl = __builtin_sqrtf(bx * bx + bz * bz), al = __builtin_sqrtf(ax * ax + az * az);
  if (bl < 1e-3f || al < 1e-3f)
    return -1;
  float c = (bx * ax + bz * az) / (bl * al), sn = (bx * az - bz * ax) / (bl * al);
  float deg = atan2f(sn, c) / DEG;
  return deg < 0 ? -deg : deg;
}

int interactview_door_is_open(uintptr_t door)
{
  float angle = interactview_door_angle(door);
  if (angle >= 0)
    return angle > DOOR_OPEN_DEG ? 1 : 0;
  return blackboard_open(door);
}

static void close_release(void);

void interactview_door_close_now(uintptr_t door)
{
  if (!interactview_is_door(door))
    return;
  close_release();
  close_door = door, close_look = CLOSE_LOOK_MAX, close_hold = 0;
  walk_now = true; /* its controllers looked through at once */
  log_printf("close door: %#lx (turned %.1f deg)", (unsigned long)door, (double)interactview_door_angle(door));
}

static void open_step(void)
{
  if (!open_sender || --open_hold > 0)
    return;
  actor_read_t a;
  if (actor_read(open_door, &a) && a.running && sys_mapped(open_sender + 0xA8, sizeof(open_saved), SYS_PERM_W))
    memcpy((void *)(open_sender + 0xA8), open_saved, sizeof(open_saved)); /* as it was */
  log_printf("open door: %#lx: its sender let go", (unsigned long)open_door);
  open_sender = 0;
}

static void close_release(void)
{
  for (int i = 0; i < n_held; i++)
    if (door_node_of(held[i].node, held[i].door) &&
        sys_ptr(held[i].node + DOOR_NODE_RADIUS) == (uintptr_t)&no_radius)
      *(uintptr_t *)(held[i].node + DOOR_NODE_RADIUS) = held[i].radius;
  n_held = 0;
}

/* once a frame: opening and shutting, and the search for the nodes of the door watched or being shut */
static void door_frame(void)
{
  open_step();
  static bool was_loading;
  bool loading = memfile_loading();
  bool in_load = loading || was_loading;
  if (!watched_door && watched_door_id && !loading) {
    uintptr_t rebound = actor_find_instance(watched_door_id);
    if (rebound && interactview_is_door(rebound)) {
      watched_door = rebound;
      watched_door_rebind = 0;
      n_door_nodes = 0, nodes_door = 0, n_door_ctrls = 0;
      log_printf("door close watch: rebound %#lx (id %#lx)", (unsigned long)rebound, (unsigned long)watched_door_id);
    } else if (--watched_door_rebind <= 0) {
      watched_door_id = 0;
      watched_door_rebind = 0;
    }
  }
  uintptr_t door = searched_door();
  if (!door) {
    n_door_nodes = 0, nodes_door = 0;
    was_loading = loading;
    return;
  }
  actor_read_t a;
  if (!actor_read(door, &a) || !a.running) { /* gone (a map change) */
    n_held = 0, n_door_nodes = 0, nodes_door = 0;
    if (door == close_door)
      close_door = 0;
    else if (in_load && watched_door_id) {
      watched_door = 0;
      watched_door_rebind = DOOR_REBIND_FRAMES;
    } else {
      interactview_door_watch(0);
    }
    was_loading = loading;
    return;
  }
  was_loading = loading;
  if (nodes_door != door) {
    n_door_nodes = 0, nodes_door = door, n_door_ctrls = 0;
    scan_restart();
  }
  int kept = 0;
  for (int i = 0; i < n_door_nodes; i++) { /* still there, and whether they're counting */
    if (!door_node_of(door_nodes[i].node, door)) {
      log_printf("door close watch: %#lx's node %#lx gone", (unsigned long)door, (unsigned long)door_nodes[i].node);
      continue;
    }
    float v = node_count(door_nodes[i].node);
    door_nodes[i].quiet = v != door_nodes[i].last ? 0 : door_nodes[i].quiet + 1;
    door_nodes[i].last = v;
    door_nodes[kept++] = door_nodes[i];
  }
  n_door_nodes = kept;
  static int walk_wait, ctrl_wait;
  bool shutting = door == close_door;
  if (--ctrl_wait <= 0 || walk_now) {
    find_controllers(door);
    ctrl_wait = DOOR_CTRL_FRAMES;
  }
  if (--walk_wait <= 0 || walk_now || shutting) { /* every frame while shutting: its event's nodes come and go */
    walk_controllers(door, add_door_node);
    walk_wait = DOOR_WALK_FRAMES;
  }
  walk_now = false;
  uintptr_t found = scan_step(door);
  if (found)
    add_door_node(found, door);
  if (!shutting)
    return;
  if (!n_door_nodes) { /* none found yet */
    if (--close_look <= 0) {
      gfx_notice(T("close door: its count wasn't found (open it first)"));
      close_door = 0;
    }
    return;
  }
  /* every frame until it's shut (or CLOSE_HOLD frames): every node it has, the ones found meanwhile too, counts 0 and
   * nobody near */
  if (close_hold == 0)
    gfx_notice(T("closing the door"));
  for (int i = 0; i < n_door_nodes; i++) {
    uintptr_t node = door_nodes[i].node;
    bool is_held = false;
    for (int k = 0; k < n_held; k++)
      is_held |= held[k].node == node;
    if (!is_held && n_held < DOOR_NODES) {
      held[n_held].node = node, held[n_held].radius = sys_ptr(node + DOOR_NODE_RADIUS), held[n_held].door = door;
      n_held++;
      *(uintptr_t *)(node + DOOR_NODE_RADIUS) = (uintptr_t)&no_radius;
    }
    const float zero = 0;
    memcpy((void *)(node + DOOR_NODE_TIMER), &zero, sizeof(zero));
  }
  float angle = interactview_door_angle(door);
  if (++close_hold >= CLOSE_HOLD || (close_hold > 10 && angle >= 0 && angle < 1.0f)) {
    log_printf("close door: %#lx: %d nodes held at 0 for %d frames, turned %.1f deg now", (unsigned long)door, n_held,
               close_hold, (double)angle);
    close_release();
    close_door = 0, close_hold = 0;
  }
}

/* the watch's value: frames left before the door shuts (CloseWaitFrame while it's held open), or why there's none */
void interactview_door_text(char *buf, int len)
{
  if (!watched_door) {
    snprintf(buf, len, "-");
    return;
  }
  int i = nodes_door == watched_door ? live_node() : -1;
  if (i < 0) { /* none yet: a door that hasn't been opened has none */
    snprintf(buf, len, "%s", T("closed"));
    return;
  }
  snprintf(buf, len, "%.0f", (double)node_count(door_nodes[i].node));
}

bool interactview_own(uintptr_t actor, int part)
{
  int c = choice_of(actor);
  return c >= 0 && choices[c].shown[part];
}

bool interactview_shows(uintptr_t actor, int part)
{
  return interactview_own(actor, part) || (interactview_shown && interactview_parts[part]);
}

/* any of its parts drawn */
static bool shows_any(uintptr_t actor)
{
  for (int part = 0; part < IV_PARTS; part++)
    if (interactview_shows(actor, part))
      return true;
  return false;
}

void interactview_toggle(uintptr_t actor, int part)
{
  bool now = !interactview_own(actor, part);
  int c = choice_of(actor);
  if (c < 0) {
    if (n_choices >= MAX_CHOICES)
      memmove(&choices[0], &choices[1], sizeof(choices[0]) * --n_choices);
    c = n_choices++;
    choices[c].actor = actor;
    for (int k = 0; k < IV_PARTS; k++)
      choices[c].shown[k] = false;
  }
  choices[c].shown[part] = now;
  static const char *const part_names[IV_PARTS] = { "radius", "z-target", "target break line" };
  log_printf("interaction ranges: %#lx %s %s", (unsigned long)actor, part_names[part], now ? "on" : "off");
}

bool interactview_active(void)
{
  if (interactview_shown)
    return true;
  for (int i = 0; i < n_choices; i++)
    for (int part = 0; part < IV_PARTS; part++)
      if (choices[i].shown[part])
        return true;
  return false;
}

static void forget_gone(void)
{
  int kept = 0;
  for (int i = 0; i < n_choices; i++) {
    actor_read_t a;
    if (actor_read(choices[i].actor, &a) && a.running)
      choices[kept++] = choices[i];
  }
  n_choices = kept;
}

static bool contains(const char *text, const char *word)
{
  for (; *text; text++) {
    int k = 0;
    while (word[k] && text[k] == word[k])
      k++;
    if (!word[k])
      return true;
  }
  return false;
}

/* "talk radius", "z-target talk", "target break line": by its first action ("check radius" for a sign, "open door
 * prompt radius" for a door) */
const char *interactview_part_label(uintptr_t actor, int part)
{
  static char labels[IV_PARTS][40];
  if (part == IV_BREAK && interactview_has_part(actor, IV_ZTARGET))
    return T("target break line");
  if (part == IV_DOOR)
    return T("door open/close radius");
  char what[40];
  interactview_actions(actor, what, sizeof(what));
  for (int k = 0; what[k]; k++)
    if (what[k] == ',')
      what[k] = 0; /* the first */
  if (!what[0])
    snprintf(what, sizeof(what), "%s", T("interaction"));
  if (part == IV_BREAK) /* none to Z-target: only where its A prompt goes, walking away ("open prompt break line") */
    snprintf(labels[part], sizeof(labels[part]), TF("%s prompt break line"),
             !compare(what, "open treasure box") ? T("open") : what);
  else if (part == IV_RADIUS && !compare(what, "open door")) /* its A prompt's reach, not the door's own (door close) */
    snprintf(labels[part], sizeof(labels[part]), TF("%s prompt radius"), what);
  else
    snprintf(labels[part], sizeof(labels[part]), part == IV_RADIUS ? TF("%s radius") : TF("z-target %s"), what);
  return labels[part];
}

/* "talk", "check, open door": an actor's actions, lower case with spaces, the lock-on ones left out */
void interactview_actions(uintptr_t actor, char *buf, int len)
{
  const interact_actor_t *row = row_of_actor(actor);
  int n = 0;
  buf[0] = 0;
  for (int c = 0; row && c < row->n; c++) {
    const interact_client_t *client = &data_interact_clients[row->first + c];
    if (client->lock_on)
      continue;
    char word[40];
    int w = 0;
    for (const char *s = client->action; *s && w < (int)sizeof(word) - 2; s++) {
      if (*s >= 'A' && *s <= 'Z' && s != client->action)
        word[w++] = ' ';
      word[w++] = *s >= 'A' && *s <= 'Z' ? *s - 'A' + 'a' : *s;
    }
    word[w] = 0;
    if (contains(buf, word))
      continue;
    n += snprintf(buf + n, len > n ? len - n : 0, "%s%s", n ? ", " : "", word);
  }
}

/* --- regions --------------------------------------------------------------------------------------- */

/* its model box; one the game wouldn't take (inside out) counts as none: a box of nothing, a fan 0.01 m */
static void read_model(uintptr_t actor, model_t *m)
{
  memset(m, 0, sizeof(*m));
  m->half = 0.01f;
  if (!sys_mapped(actor + ACTOR_MODEL_BOX, 24, SYS_PERM_R))
    return;
  const float *box = (const float *)(actor + ACTOR_MODEL_BOX);
  for (int i = 0; i < 6; i++)
    if (!(box[i] > -50.0f && box[i] < 50.0f)) /* not a number, or not a model's */
      return;
  for (int i = 0; i < 3; i++)
    if (box[i] > box[3 + i])
      return;
  memcpy(m->lo, box, sizeof(m->lo));
  memcpy(m->hi, box + 3, sizeof(m->hi));
  float half = 0;
  for (int i = 0; i < 3; i += 2) {
    half = half > absf(m->lo[i]) ? half : absf(m->lo[i]);
    half = half > absf(m->hi[i]) ? half : absf(m->hi[i]);
  }
  m->half = half > 0 ? half : 0.01f;
}

static bool same_model(const model_t *a, const model_t *b)
{
  for (int i = 0; i < 3; i++)
    if (absf(a->lo[i] - b->lo[i]) > 0.02f || absf(a->hi[i] - b->hi[i]) > 0.02f)
      return false;
  return true;
}

/* the area as the game checks it for this actor: its model box counted in (sized) */
static interact_area_t sized_area(const interact_area_t *a, const model_t *m)
{
  interact_area_t out = *a;
  if (!a->sized)
    return out;
  if (a->shape == IA_BOX)
    for (int i = 0; i < 3; i++)
      out.lo[i] += m->lo[i], out.hi[i] += m->hi[i];
  else
    out.radius += m->half;
  return out;
}

/* the point (x across, z ahead) in the actor's frame, in one area as the ground sees it (heights aside) */
static bool in_area(const interact_area_t *a, float x, float z, bool held)
{
  float d2 = x * x + z * z, radius = a->radius + (held ? a->margin : 0);
  switch (a->shape) {
    case IA_BOX: { /* held: grown by its margin */
      float m = held ? a->margin : 0;
      return x >= a->lo[0] - m && x <= a->hi[0] + m && z >= a->lo[2] - m && z <= a->hi[2] + m;
    }
    case IA_SPHERE:
      return d2 < radius * radius;
    default: /* a fan: from Link, only how far; from the actor, also within its angle of the actor's front */
      if (d2 >= radius * radius)
        return false;
      if (a->from_link || a->angle >= 360)
        return true;
      float off = atan2f(x, z) / DEG;
      return (off < 0 ? -off : off) <= a->angle; /* either way of its front */
  }
}

static bool in_client(const interact_client_t *c, const model_t *m, float x, float z, bool held)
{
  for (int i = 0; i < c->n_areas; i++) {
    interact_area_t a = sized_area(&data_interact_areas[c->first_area + i], m);
    if (!in_area(&a, x, z, held))
      return false;
  }
  return true;
}

static float reach_of(const interact_client_t *c, const model_t *m)
{
  float reach = MAX_REACH;
  for (int i = 0; i < c->n_areas; i++) { /* every area must hold: the nearest bound */
    interact_area_t sized = sized_area(&data_interact_areas[c->first_area + i], m);
    const interact_area_t *a = &sized;
    float r = a->shape == IA_BOX ? 0 : a->radius + a->margin;
    if (a->shape == IA_BOX)
      for (int k = 0; k < 3; k += 2) {
        r = r > -a->lo[k] + a->margin ? r : -a->lo[k] + a->margin;
        r = r > a->hi[k] + a->margin ? r : a->hi[k] + a->margin;
      }
    if (a->shape == IA_BOX)
      r *= 1.42f; /* its corners */
    reach = reach < r ? reach : r;
  }
  return reach;
}

/* held: as far as it holds once it has Link's attention (each reach and its margin) */
static void make_region(const interact_client_t *c, const model_t *m, region_t *r, bool held)
{
  float reach = reach_of(c, m);
  for (int k = 0; k < RAYS; k++) {
    float a = 2 * PI * k / RAYS, sx = sinf(a), cz = cosf(a);
    r->inner[k] = 0, r->outer[k] = -1;
    for (float d = STEP / 2; d <= reach + STEP; d += STEP)
      if (in_client(c, m, sx * d, cz * d, held)) {
        if (r->outer[k] < 0)
          r->inner[k] = d - STEP / 2 < 0 ? 0 : d - STEP / 2;
        r->outer[k] = d;
      } else if (r->outer[k] >= 0) {
        break;
      }
  }
}

/* --- Link in range ----------------------------------------------------------------------------------- */

/* held: it has his attention already, so each reach holds as far as its margin; a lock-on's angle isn't checked any
 * more (IsAngleCheckIgnoreWhileLockOn) */
static bool link_in_area(const shown_t *s, const interact_area_t *a, const vec3f_t *link, double fx, double fz, bool held,
                         bool lock_on)
{
  float radius = a->radius + (held ? a->margin : 0);
  float dx = s->pos[0] - link->x, dy = s->pos[1] - link->y, dz = s->pos[2] - link->z; /* Link to the actor */
  float x = -(dx * s->side[0] + dz * s->side[1]), z = -(dx * s->front[0] + dz * s->front[1]); /* Link, actor's frame */
  float across = __builtin_sqrtf(dx * dx + dz * dz);
  switch (a->shape) {
    case IA_BOX:
      return in_area(a, x, z, held) && -dy >= a->lo[1] && -dy <= a->hi[1];
    case IA_SPHERE:
      return dx * dx + dy * dy + dz * dz < radius * radius;
    default:
      if (across >= radius)
        return false;
      if (a->from_link) { /* the actor in Link's fan: ahead of him within half the angle, and in the height band */
        if (across > 1e-3f && !(held && lock_on)) {
          float c = (float)((dx * fx + dz * fz) / across);
          float off = atan2f((float)(dx * fz - dz * fx) / across, c) / DEG;
          if ((off < 0 ? -off : off) > a->angle) /* either way of his facing (seen in game: 54 deg off, 60) */
            return false;
        }
        return a->shape == IA_CYLINDER_FAN || (dy <= a->top && dy >= -a->bottom);
      }
      return in_area(a, x, z, held) && dy <= a->bottom && -dy <= a->top;
  }
}

static bool link_in_client(const shown_t *s, const interact_client_t *c, const vec3f_t *link, double fx, double fz,
                           bool held)
{
  for (int i = 0; i < c->n_areas; i++) {
    interact_area_t a = sized_area(&data_interact_areas[c->first_area + i], &s->model);
    if (!link_in_area(s, &a, link, fx, fz, held, c->lock_on))
      return false;
  }
  return true;
}

/* --- the game's own answer: what A acts on now ------------------------------------------------------- */

/* Link's components [link+0x270] hold at +0x578 an object whose +0x1A70 points to the do-action target's record (the
 * actor at +8; the same actor at +0x98 and +0x128): set while the A prompt shows, cleared when it goes. Its +0x1A60
 * points to the attention client ZL would take for an action (a lock-on client's record: the actor at +8, its checks
 * at +0x60), whether ZL is held or not; with none it points back at +0x1A70. Found in game (FINDINGS 9.37): searching
 * Link's components for a townsperson's address with the prompt up and not (remote's findref), then moving Link round
 * a shopkeeper. */
#define LINK_ATTENTION 0x578
#define ATTENTION_DO_ACTION 0x1A70
#define ATTENTION_LOCK_ON 0x1A60
#define TARGET_ACTOR 0x08

static bool readable_ptr(uintptr_t addr) { return addr && !(addr & 7) && sys_mapped(addr, 8, SYS_PERM_R); }

/* the actor in Link's attention slot at this offset (the A prompt's, or ZL's), or 0 */
static uintptr_t attention_actor(uint32_t slot)
{
  uintptr_t link = link_actor();
  if (!readable_ptr(link + 0x270))
    return 0;
  uintptr_t components = *(uintptr_t *)(link + 0x270);
  if (!readable_ptr(components + LINK_ATTENTION))
    return 0;
  uintptr_t attention = *(uintptr_t *)(components + LINK_ATTENTION);
  if (!readable_ptr(attention + slot))
    return 0;
  uintptr_t record = *(uintptr_t *)(attention + slot);
  if (record == attention + ATTENTION_DO_ACTION) /* ZL's, with none */
    return 0;
  return readable_ptr(record + TARGET_ACTOR) ? *(uintptr_t *)(record + TARGET_ACTOR) : 0;
}

/* --- each frame ------------------------------------------------------------------------------------- */

static void add_found(uintptr_t actor)
{
  for (int k = 0; k < n_found; k++)
    if (found[k] == actor)
      return;
  if (n_found < MAX_ACTORS)
    found[n_found++] = actor;
}

/* the actors near Link, and near the actor browser's spot while it's on (the one it's on first) */
static void scan(void)
{
  n_found = 0;
  forget_gone();
  uintptr_t selected = placer_selected();
  if (selected && row_of_actor(selected) && shows_any(selected))
    add_found(selected);
  float spot[3];
  for (int pass = 0; pass < 2; pass++) {
    int n = pass == 0 ? actors_near(RANGE, near_buf, NEAR_READ)
                      : placer_spot(spot) ? actors_near_point(spot, RANGE, near_buf, NEAR_READ) : 0;
    for (int i = 0; i < n && n_found < MAX_ACTORS; i++)
      if (near_buf[i].name && row_of_named(near_buf[i].actor, near_buf[i].name) && shows_any(near_buf[i].actor))
        add_found(near_buf[i].actor);
  }
  for (int i = 0; i < n_choices && n_found < MAX_ACTORS; i++) { /* one chosen in the browser, wherever it is */
    bool listed = false;
    for (int k = 0; k < n_found; k++)
      listed |= found[k] == choices[i].actor;
    if (!listed && shows_any(choices[i].actor))
      found[n_found++] = choices[i].actor;
  }
}

void interactview_frame(void)
{
  door_frame(); /* whether the ranges are drawn or not */
  vec3f_t link;
  if (!interactview_active() || !link_drawn_pos(&link)) {
    n_shown = 0;
    scan_wait = 0;
    return;
  }
  static uintptr_t selected_before;
  uintptr_t selected = placer_selected();
  if (--scan_wait <= 0 || selected != selected_before) {
    scan();
    scan_wait = SCAN_FRAMES;
    selected_before = selected;
  }
  double fx = 0, fz = 1;
  link_facing(&fx, &fz);
  uintptr_t target = attention_actor(ATTENTION_DO_ACTION); /* the game's own answer */
  uintptr_t candidate = attention_actor(ATTENTION_LOCK_ON);
  static shown_t was[MAX_ACTORS];
  int n_was = n_shown;
  memcpy(was, shown, sizeof(shown_t) * n_was);
  n_shown = 0;
  for (int i = 0; i < n_found; i++) {
    actor_read_t a;
    if (!actor_read(found[i], &a) || !a.running || !shows_any(found[i]))
      continue;
    const interact_actor_t *row = row_of_named(found[i], a.name);
    if (!row)
      continue;
    shown_t *s = &shown[n_shown];
    const shown_t *before = 0;
    for (int k = 0; k < n_was; k++)
      if (was[k].actor == found[i] && was[k].row == row)
        before = &was[k];
    model_t model;
    read_model(found[i], &model);
    if (before && same_model(&before->model, &model)) {
      *s = *before; /* its regions, made before */
    } else {
      if (before)
        *s = *before; /* its state; new regions for its new model box */
      s->actor = found[i];
      s->row = row;
      s->model = model;
      s->n_clients = row->n < MAX_CLIENTS ? row->n : MAX_CLIENTS;
      for (int c = 0; c < s->n_clients; c++) {
        make_region(&data_interact_clients[row->first + c], &s->model, &s->regions[c], false);
        make_region(&data_interact_clients[row->first + c], &s->model, &s->held[c], true);
      }
    }
    memcpy(s->pos, a.pos, sizeof(s->pos));
    const float *rot = (const float *)(found[i] + ACTOR_ROT); /* rows: column 0 its side, column 2 its front */
    float zx = rot[2], zz = rot[8], len = __builtin_sqrtf(zx * zx + zz * zz);
    if (len < 1e-3f)
      zx = 0, zz = 1, len = 1;
    s->front[0] = zx / len, s->front[1] = zz / len;
    /* its +X: the matrix's own column 0, as the game reads it (main+0x5E58A0); worked out from the front it came out
     * mirrored (a shopkeeper's talk box drawn on his wrong side) */
    float xx = rot[0], xz = rot[6], xl = __builtin_sqrtf(xx * xx + xz * xz);
    if (xl < 1e-3f)
      xx = s->front[1], xz = -s->front[0], xl = 1;
    s->side[0] = xx / xl, s->side[1] = xz / xl;
    if (row->door_radius > 0) { /* a door: Link within its radius, on its -X side (SelectorDoorCanClose) */
      float dx = link.x - s->pos[0], dy = link.y - s->pos[1], dz = link.z - s->pos[2];
      s->holds_door = dx * dx + dy * dy + dz * dz < row->door_radius * row->door_radius &&
                      dx * s->side[0] + dz * s->side[1] < 0;
    }
    for (int c = 0; c < s->n_clients; c++)
      s->off[c] = action_off(found[i], data_interact_clients[row->first + c].action);
    for (int c = 0; c < s->n_clients; c++) /* in range: taken up, then held as he walks away until past its margin */
      s->in_range[c] = link_in_client(s, &data_interact_clients[row->first + c], &link, fx, fz,
                                      before && before->in_range[c]);
    s->target = found[i] == target;
    s->candidate = found[i] == candidate;
    if (s->target != (before && before->target))
      log_printf("interaction ranges: %s %#lx %s Link's A target", a.name, (unsigned long)found[i],
                 s->target ? "is" : "isn't");
    n_shown++;
  }
}

/* --- drawing ------------------------------------------------------------------------------------------ */

static struct {
  float (*ends)[2][3];
  uint8_t *colours;
  int n, max;
} out_lines;

static void line(const float a[3], const float b[3], uint8_t colour)
{
  if (out_lines.n >= out_lines.max)
    return;
  memcpy(out_lines.ends[out_lines.n][0], a, sizeof(float) * 3);
  memcpy(out_lines.ends[out_lines.n][1], b, sizeof(float) * 3);
  out_lines.colours[out_lines.n++] = colour;
}

/* the point d along ray k from the actor */
static void ray_point(const shown_t *s, int k, float d, float p[3])
{
  float a = 2 * PI * k / RAYS, x = sinf(a) * d, z = cosf(a) * d;
  p[0] = s->pos[0] + s->side[0] * x + s->front[0] * z;
  p[1] = s->pos[1] + LIFT;
  p[2] = s->pos[2] + s->side[1] * x + s->front[1] * z;
}

static uint8_t colour_of(const shown_t *s, int c)
{
  const interact_client_t *client = &data_interact_clients[s->row->first + c];
  bool talk = !compare(client->action, "Talk");
  if (client->lock_on)
    return talk ? AW_TALK_LOCK : AW_ACT_LOCK;
  if (s->target) /* the game's answer, not the files' estimate */
    return talk ? AW_TALK_IN : AW_ACT_IN;
  return talk ? AW_TALK : AW_ACT;
}

/* where it breaks: the held region's outer edge, dashed (every other piece of it, each piece halved) */
static void held_lines(const shown_t *s, int c, uint8_t colour)
{
  const region_t *h = &s->held[c], *r = &s->regions[c];
  for (int k = 0; k < RAYS; k++) {
    int j = (k + 1) % RAYS;
    if (h->outer[k] < 0 || h->outer[j] < 0 || (h->outer[k] - r->outer[k] < 0.02f && h->outer[j] - r->outer[j] < 0.02f))
      continue;
    for (int half = 0; half < 2; half++) { /* two dashes a piece */
      float a[3], b[3], m[3];
      ray_point(s, k, h->outer[k], a);
      ray_point(s, j, h->outer[j], b);
      for (int q = 0; q < 3; q++)
        m[q] = (a[q] + b[q]) / 2;
      float from[3], to[3];
      for (int q = 0; q < 3; q++) { /* the first or second quarter of each half */
        const float *p0 = half ? m : a, *p1 = half ? b : m;
        from[q] = p0[q];
        to[q] = p0[q] + (p1[q] - p0[q]) * 0.55f;
      }
      line(from, to, colour);
    }
  }
}

/* which of its parts a client is: the reach while Z-targeting, or the plain one */
static int part_of(const shown_t *s, int c) { return data_interact_clients[s->row->first + c].lock_on ? IV_ZTARGET : IV_RADIUS; }

/* a door's: the half disc on its -X side where anyone holds it open, ray k of RAYS round from its front */
#define DOOR_SLICES (RAYS / 2)
static void door_point(const shown_t *s, int k, float p[3])
{
  ray_point(s, RAYS / 2 + k, s->row->door_radius, p); /* from pi (behind it) round its -X side to 2 pi (in front) */
}

/* where it pushes: what its panel sweeps shutting, a half disc as wide as the door (its model box's x) on the side
 * it swings to */
static float door_width(const shown_t *s)
{
  float w = absf(s->model.lo[0]) > absf(s->model.hi[0]) ? absf(s->model.lo[0]) : absf(s->model.hi[0]);
  return w > 0.2f && w < 4.0f ? w : 0;
}

static void push_lines(const shown_t *s)
{
  float w = door_width(s), a[3], b[3];
  if (w <= 0)
    return;
  for (int k = 0; k <= DOOR_SLICES; k++) {
    ray_point(s, RAYS / 2 + k, w, b);
    if (k > 0)
      line(a, b, AW_GUARD_CAUGHT);
    memcpy(a, b, sizeof(a));
  }
}

static void door_lines(const shown_t *s)
{
  push_lines(s);
  uint8_t colour = s->holds_door ? AW_DOOR_IN : AW_DOOR;
  float a[3], b[3];
  for (int k = 0; k < DOOR_SLICES; k++) {
    door_point(s, k, a);
    door_point(s, k + 1, b);
    line(a, b, colour);
  }
  door_point(s, 0, a);
  door_point(s, DOOR_SLICES, b);
  line(a, b, colour); /* its straight side, through the door */
}

/* over the actor A acts on now: a diamond on a stem, from the game's own answer */
/* a diamond over it: A acts on it now (bright), or ZL would take it to (its Z-target colour) */
static void target_lines(const shown_t *s)
{
  const float h = 2.2f, r = 0.3f;
  uint8_t colour = s->target ? AW_TALK_IN : AW_TALK_LOCK;
  float top[3] = { s->pos[0], s->pos[1] + h + 2 * r, s->pos[2] }, bottom[3] = { s->pos[0], s->pos[1] + h, s->pos[2] };
  float mid = s->pos[1] + h + r;
  float side[4][3] = { { s->pos[0] + r, mid, s->pos[2] }, { s->pos[0], mid, s->pos[2] + r },
                       { s->pos[0] - r, mid, s->pos[2] }, { s->pos[0], mid, s->pos[2] - r } };
  for (int k = 0; k < 4; k++) {
    line(top, side[k], colour);
    line(bottom, side[k], colour);
    line(side[k], side[(k + 1) % 4], colour);
  }
  float foot[3] = { s->pos[0], s->pos[1] + LIFT, s->pos[2] };
  line(foot, bottom, colour);
}

int interactview_lines(float ends[][2][3], uint8_t colours[], int max)
{
  out_lines.ends = ends, out_lines.colours = colours, out_lines.n = 0, out_lines.max = max;
  for (int i = 0; i < n_shown; i++) /* the game's own answer first: no budget cuts it */
    if (shown[i].target || shown[i].candidate)
      target_lines(&shown[i]);
  for (int i = 0; i < n_shown; i++) {
    const shown_t *s = &shown[i];
    if (s->row->door_radius > 0 && interactview_shows(s->actor, IV_DOOR))
      door_lines(s);
    for (int c = 0; c < s->n_clients; c++) {
      if (s->off[c]) /* switched off by its AI: it can't be done now */
        continue;
      const region_t *r = &s->regions[c];
      uint8_t colour = colour_of(s, c);
      /* its target break line: with the part it's of, or every one when the target break line is on alone */
      bool alone = !interactview_shows(s->actor, IV_RADIUS) && !interactview_shows(s->actor, IV_ZTARGET);
      if (interactview_shows(s->actor, IV_BREAK) && (alone || interactview_shows(s->actor, part_of(s, c))))
        held_lines(s, c, colour);
      if (!interactview_shows(s->actor, part_of(s, c)))
        continue;
      for (int k = 0; k < RAYS; k++) {
        int j = (k + 1) % RAYS;
        float a[3], b[3];
        if (r->outer[k] < 0)
          continue;
        if (r->outer[j] >= 0) { /* its outer edge, and its inner one where it doesn't start at the actor */
          ray_point(s, k, r->outer[k], a);
          ray_point(s, j, r->outer[j], b);
          line(a, b, colour);
          if (r->inner[k] > 0.1f || r->inner[j] > 0.1f) {
            ray_point(s, k, r->inner[k], a);
            ray_point(s, j, r->inner[j], b);
            line(a, b, colour);
          }
        }
        if (r->outer[j] < 0 || r->outer[(k + RAYS - 1) % RAYS] < 0) { /* a side */
          ray_point(s, k, r->inner[k], a);
          ray_point(s, k, r->outer[k], b);
          line(a, b, colour);
        }
      }
    }
  }
  return out_lines.n;
}

static void triangle(aw_face_t faces[], int *n, int max, const float a[3], const float b[3], const float c[3],
                     uint8_t colour)
{
  if (*n >= max)
    return;
  aw_face_t *f = &faces[(*n)++];
  for (int j = 0; j < 3; j++) {
    f->corner[j] = a[j];
    f->u[j] = b[j] - a[j];
    f->v[j] = c[j] - a[j];
  }
  f->color = colour;
  f->scene = true;
  f->triangle = true;
}

int interactview_faces(aw_face_t faces[], int max)
{
  int n = 0;
  for (int i = 0; i < n_shown; i++) {
    const shown_t *s = &shown[i];
    if (s->row->door_radius > 0 && interactview_shows(s->actor, IV_DOOR)) { /* a door's half disc */
      float centre[3] = { s->pos[0], s->pos[1] + LIFT, s->pos[2] }, a[3], b[3];
      for (int k = 0; k < DOOR_SLICES; k++) {
        door_point(s, k, a);
        door_point(s, k + 1, b);
        triangle(faces, &n, max, centre, a, b, s->holds_door ? AW_DOOR_IN : AW_DOOR);
      }
    }
    for (int c = 0; c < s->n_clients; c++) {
      if (s->off[c] || data_interact_clients[s->row->first + c].lock_on || !interactview_shows(s->actor, IV_RADIUS))
        continue; /* (lock-on ones: their outline only) */
      const region_t *r = &s->regions[c];
      uint8_t colour = colour_of(s, c);
      for (int k = 0; k < RAYS; k++) {
        int j = (k + 1) % RAYS;
        if (r->outer[k] < 0 || r->outer[j] < 0)
          continue;
        float ik[3], ok[3], ij[3], oj[3];
        ray_point(s, k, r->inner[k], ik);
        ray_point(s, k, r->outer[k], ok);
        ray_point(s, j, r->inner[j], ij);
        ray_point(s, j, r->outer[j], oj);
        triangle(faces, &n, max, ik, ok, oj, colour);
        if (r->inner[k] > 0.01f || r->inner[j] > 0.01f)
          triangle(faces, &n, max, ik, oj, ij, colour);
      }
    }
  }
  return n;
}

void interactview_text(char *buf, int len)
{
  if (!interactview_shown) {
    snprintf(buf, len, TF("off"));
    return;
  }
  const char *target = 0, *candidate = 0;
  for (int i = 0; i < n_shown; i++) {
    if (shown[i].target)
      target = shown[i].row->actor;
    if (shown[i].candidate)
      candidate = shown[i].row->actor;
  }
  if (!n_shown)
    snprintf(buf, len, TF("nothing to talk to near"));
  else if (target)
    snprintf(buf, len, TF("%d near   A: %s"), n_shown, target);
  else if (candidate)
    snprintf(buf, len, TF("%d near   ZL: %s"), n_shown, candidate);
  else
    snprintf(buf, len, TF("%d near"), n_shown);
}
