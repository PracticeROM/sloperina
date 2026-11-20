#ifndef CCT_H
#define CCT_H

#include "link.h"

/* Temporary collision bypass for Link only. All operations resolve the current
 * controller again; a lost/replaced controller fails closed without stale writes. */
bool cct_noclip_begin(void);
bool cct_noclip_move(const vec3f_t *position);
void cct_noclip_end(void);
bool cct_noclip_active(void);

/* Resolve on every call. These work with collision enabled as well. */
/* Put Link's feet at `position` through the controller's own setter (OneShotPhysicsCctChangePosition's), with
 * collision on, and clear his vertical velocity; with `rotation` (a row-major 3x3, as the actor's at +0x328),
 * turn him too (OneShotCctChangeRotation's setter). No heap search: the controller is reached from his actor. */
bool cct_teleport(const vec3f_t *position, const float *rotation);
bool cct_reset_vertical_velocity(uintptr_t expected_actor);
bool cct_set_vertical_velocity(uintptr_t expected_actor, float y);
bool cct_velocity(uintptr_t expected_actor, vec3f_t *out); /* the body's applied velocity (units/s) */
/* Any physics body of the controller body's type (a ragdoll bone's too): add dy to its velocity through the
 * game's setter; *before (if given) gets the velocity it had (the queued one if a change is pending). */
bool cct_body_add_vertical_velocity(uintptr_t body, float dy, vec3f_t *before);
bool cct_body_velocity(uintptr_t body, vec3f_t *out);
bool cct_on_ground(uintptr_t expected_actor, bool *grounded);
bool cct_clear_fall_distance(uintptr_t expected_actor);

/* The handle of Link's body in the physics world's body entries; 0 while it isn't known. */
uint64_t cct_body_handle(void);

#endif
