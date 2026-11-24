/* Warps (port of game.py arm_warp): the next exit Link takes leads to the chosen place. */
#ifndef WARP_H
#define WARP_H

#include <stdbool.h>
#include <stdint.h>

#include "data.h"

extern bool warp_arrival_cutscenes; /* use the spawn's own arrival instead of the standard one */

void warp_arm(const warp_map_t *map, const spawn_t *spawn);
/* Request Dm_SY_BancChangeGo without fallback, clearing Sequence_ReservedBootEventName / Starter so arrival
 * skips cutscenes & arrival camera (memfile.c). False immediately if refused (no exit-swap fallback). */
bool warp_arm_silent(const warp_map_t *map, const spawn_t *spawn);
void warp_cancel(void);
void warp_frame(void);              /* frame hook */
const char *warp_state(void);       /* for the warps menu */
/* where the last warp_arm / warp_arm_silent led (0 and a null map: none yet): the game keeps no start point for an
 * arrival by warp, so a reload of that map can come back to it */
const spawn_t *warp_last_target(const warp_map_t **map);
bool warp_pending(void);            /* requested or armed, not yet happened */
bool warp_abandoned(void);          /* cancelled or timed out before Link left the map */
/* the last warp_arm/warp_arm_silent went through the game's event (instant), not the exit fallback */
bool warp_requested(void);
/* Request the event `name` with no parameters, as the SeqOneShotRequestEvent node does (title.c); false if the
 * game refused it (another event runs or is being started) */
bool warp_request_event(const char *name);
/* The same with a starter (remote.c's event line): the event puts its StarterPos actors at `transform` (row-major
 * 3x4, the place in the last column) and takes `actor` (0: none) as the starter; false also where this build's
 * starter fields aren't known */
bool warp_request_event_from(const char *name, const float transform[12], uintptr_t actor);
/* an event runs (an arrival, a cutscene, a talk): the game refuses a warp's event request until it's over */
bool warp_event_running(void);

#endif
