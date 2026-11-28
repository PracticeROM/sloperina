/* Enemy health: a bar with "life/max" over each living enemy near Link, handed to the render thread as
 * bars (overlayui.c, on the GPU) or as rectangles (nvn.c, gz's style), like the air walls. */
#ifndef ENEMYHP_H
#define ENEMYHP_H

#include <stdbool.h>
#include <stdint.h>

#include "airwall.h" /* aw_dot_t */

extern bool enemy_hp_shown;
extern bool enemy_hp_xray; /* also for enemies behind the scenery (else their bars fade out) */

/* an enemy, as actors.c reads it */
typedef struct {
  uintptr_t id;        /* its actor: the same enemy from frame to frame */
  float pos[3];        /* where it stands (its feet), world space */
  float height;        /* how far above pos to draw the bar */
  int32_t life, max_life;
  bool invincible;     /* its Life component holds an invincibility now */
} enemy_t;

/* actors.c: the enemies in the scene, up to max */
int enemies_read(enemy_t *out, int max);
/* actors.c: the actors with this name ("Shootable_Hook"), any state, up to max; their state is at +0x2C, their
 * position at +0x31C */
int actors_named(const char *name, uintptr_t *out, int max);

/* actors.c: the running actors within `radius` metres of Link (not Link), nearest first, up to max; game thread */
typedef struct {
  const char *name; /* the game's own, e.g. "Enemy_Wolfos" */
  float distance;
  uintptr_t actor;  /* for actor_read: only good while the actor's still there */
  float pos[3];
  uint32_t state;
} actor_near_t;
int actors_near(float radius, actor_near_t *out, int max);
/* the same round a point (distance: from it); game thread */
int actors_near_point(const float point[3], float radius, actor_near_t *out, int max);
/* actors.c: every actor in the actor manager, any state (not Link), in the manager's order, up to max (`more`: those
 * that didn't fit); distance 0 where Link's position isn't known; game thread */
int actors_all(actor_near_t *out, int max, int *more);

/* actors.c: an actor found earlier, read again if it's still in the actor manager (false: gone); game thread */
typedef struct {
  const char *name;
  float pos[3];
  float distance;       /* from Link, if distance_known */
  bool distance_known;
  uint32_t state;       /* 4 and 5 running, 6 not yet appeared, 7 to 11 dying or gone (FINDINGS 9.5) */
  bool running;
  bool has_life;        /* life and max_life read from its Life component */
  int32_t life, max_life;
  bool killable;        /* has_life, a Death component, and not made invincible (an NPC's default 100 life) */
  bool invincible;      /* its Life component holds an invincibility now */
} actor_read_t;
bool actor_read(uintptr_t actor, actor_read_t *out);
/* actors.c: its Life component holds an invincibility now (any reason); game thread */
bool actor_invincible(uintptr_t actor);
/* actors.c: the same, and whether it can be killed (actor_read_t's killable); game thread */
void actor_combat(uintptr_t actor, bool *invincible, bool *killable);
/* actors.c: a string from its entry in the map's layout (its Dynamic: "Drop__DropActor", what a chest holds or a pot
 * drops), read from the layout file the game keeps loaded (FINDINGS 9.46); false where it has none (spawned actors,
 * the key not there, not a string); game thread */
bool actor_layout_string(uintptr_t actor, const char *key, char *out, int len);
/* actors.c: the name of the pack it was made from (the layout's Gyaml: Obj_TBox_A_01), where its own name (+0x20,
 * actor_read's) is its ActorName (TBox_Field_Iron, shared by every field chest); 0 if unreadable; game thread */
const char *actor_pack_name(uintptr_t actor);
/* actors.c: Banc PreActor InstanceId ([[actor+0x3D8]+0x88], false for dynamically spawned actors) and lookup of a
 * currently running actor by that ID (0 if not loaded); game thread */
bool actor_instance_id(uintptr_t actor, uint64_t *id_out);
uintptr_t actor_find_instance(uint64_t instance_id);

/* actors.c: kill an actor found earlier, the game's way (FINDINGS 9.27): a death request, reason FallIntoAbyss (the
 * void's), to its Death component; with none, an NPC or a thing (Npc_, Obj_, Item_) is deleted as the AI's delete
 * node does. Never Link. Game thread, outside any walk. */
typedef enum {
  KILL_DEATH,          /* the death request was taken: it dies as it would in a void */
  KILL_DEATH_REFUSED,  /* its own rules ignore the reason (bosses), or it's already dying */
  KILL_DELETED,        /* no Death component: deleted */
  KILL_DELETE_REFUSED, /* the delete was refused (already going) */
  KILL_NOT_ALLOWED,    /* Link, or no Death component and not an NPC or a thing */
  KILL_GONE,           /* not in the actor manager any more */
  KILL_UNKNOWN,        /* this game version's offsets aren't known */
} actor_kill_t;
actor_kill_t actor_kill(uintptr_t actor);
/* actors.c: despawn it: the engine's delete request (as above), for an enemy, NPC, animal or thing whatever its death
 * says: no death, drop or respawn flag. Never Link, scenery or the game's system actors. Game thread, outside any walk. */
actor_kill_t actor_despawn(uintptr_t actor);

/* actors.c: ask the game to make an actor by name at pos, turned yaw degrees about Y (FINDINGS 9.29): the actor
 * manager's create request, which loads its pack itself; it appears some frames later, or not (no handle is
 * kept). False, with the game's result code, if the request wasn't queued. Game thread, while the game runs. */
bool actor_spawn(const char *name, const float pos[3], float yaw_degrees, uint32_t *result);

/* actors.c: the same with the request's handle, as the game's own create node keeps one, so the actor made is known
 * exactly (actor_spawn_poll); *handle must stay where it is until the poll says made or failed. Queue 1, as the
 * node. False (and *handle 0) if this version's handle functions aren't known or the request wasn't queued. */
bool actor_spawn_handle(const char *name, const float pos[3], float yaw_degrees, uintptr_t *handle, uint32_t *result);
/* each frame after it: SPAWN_WAITING, SPAWN_MADE (*actor is it, the handle done with) or SPAWN_FAILED */
typedef enum { SPAWN_WAITING, SPAWN_MADE, SPAWN_FAILED } spawn_poll_t;
spawn_poll_t actor_spawn_poll(uintptr_t *handle, uintptr_t *actor);
void actor_spawn_cancel(uintptr_t *handle); /* gives the request up */

/* actors.c: delete an actor found earlier, whatever it is (the game's delete request, as the AI's delete node; no
 * death, no drop). Never Link. False if it's gone or the request was refused. Game thread, outside any walk. */
bool actor_remove(uintptr_t actor);

/* actors.c: a horse made by actor_spawn as one's own (FINDINGS 9.34): registered as the owned horse (the one Epona's
 * Song calls), and its rodeo bit cleared so it doesn't buck the rider off. actor_own_horse is false if this game
 * version's addresses aren't known or the game didn't take it; actor_calm_horse is true if the bit was set (the
 * horse's setup sets it again from its parameters, so call it for a while after the horse appears). */
bool actor_own_horse(uintptr_t horse);
bool actor_calm_horse(uintptr_t horse);

/* the colours of the rectangles (nvn.c) */
enum { EH_BACK, EH_FILL, EH_LOW, EH_TEXT, EH_NCOLORS };

/* an enemy's bar for the GPU (the game's style): where over its head (0..1 across the visible picture),
 * its life, and how far it is from the camera (metres); and points up the enemy (feet to bar) on the
 * picture, each with the depth the scene must be nearer than for it to count as hidden there (the
 * point's own, moved toward the camera by a margin; scenedepth.c) */
#define EH_PROBES 4
typedef struct {
  uintptr_t id;
  float x, y, distance;
  int32_t life, max_life;
  int n_probes;
  float probe_x[EH_PROBES], probe_y[EH_PROBES], probe_depth[EH_PROBES];
} eh_bar_t;

/* game thread, once per frame: bars for the GPU, or (gpu false) rectangles */
void enemyhp_frame(bool gpu);
/* render thread: once a frame, before the two after it, takes the newest frame the game thread made (publish.h);
 * enemyhp_release when done with it */
void enemyhp_take(void);
const aw_dot_t *enemyhp_published(int *n);
const eh_bar_t *enemyhp_bars(int *n);
void enemyhp_release(void);

#endif
