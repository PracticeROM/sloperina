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
bool cct_reset_vertical_velocity(uintptr_t expected_actor);
bool cct_set_vertical_velocity(uintptr_t expected_actor, float y);
bool cct_velocity(uintptr_t expected_actor, vec3f_t *out); /* the body's applied velocity (units/s) */
/* Any physics body of the controller body's type (a ragdoll bone's too): add dy to its velocity through the
 * game's setter; *before (if given) gets the velocity it had (the queued one if a change is pending). */
bool cct_body_add_vertical_velocity(uintptr_t body, float dy, vec3f_t *before);
bool cct_body_velocity(uintptr_t body, vec3f_t *out);
bool cct_on_ground(uintptr_t expected_actor, bool *grounded);
bool cct_clear_fall_distance(uintptr_t expected_actor);

/* Link's live Havok motion record (hknpMotion + 0x10: position doubles + 48-byte motion state); 0 if unavailable. */
uintptr_t cct_motion_record(void);

#endif
