/* Link, the camera and other game objects (port of game.py). */
#ifndef LINK_H
#define LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { float x, y, z; } vec3f_t;

uintptr_t link_actor(void);
bool      link_health(int32_t **cur, int32_t **max); /* quarter hearts; pointers into the game */
bool      link_drawn_pos(vec3f_t *out);              /* read-only; follows the physics record */
bool      link_facing(double *fx, double *fz);       /* the way he faces, a unit (x, z); 0 = +z */
uintptr_t camera(void);                              /* +0x38 position, +0x44 look-at */
uint32_t  frame_counter(void);

/* Link's equipment owner ([*[main+pouch_mgr_ptr]], vtable vt_equip_owner) and component
 * (owner+0x470, vtable vt_equip, +0x18 = Link), resolved directly. */
uintptr_t link_equip_component(void);
uintptr_t link_equip_owner(void);

#endif
