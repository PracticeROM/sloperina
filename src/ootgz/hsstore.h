/* Hookshot storage (1.0): see whether Link has it, and get it on purpose by knocking him over the moment a shot
 * starts, as a bomb landing on that frame does. */
#ifndef HSSTORE_H
#define HSSTORE_H

#include <stdbool.h>
#include <stdint.h>

extern bool hsstore_shown; /* draw the hooks and the shot's aim in the world */

/* -1: no hookshot record (not in game, no hookshot yet); 0: no storage; 1: storage */
int hsstore_have(void);
bool hsstore_force_enabled(void);
void hsstore_force_set(bool on);
/* game thread, once a frame, before ragdoll_frame */
void hsstore_frame(void);

/* game thread: the hooks (Shootable_Hook actors) near Link and each weapon's aim while it's in use or stored, as
 * world lines (from, to) with a colour (airwall.h AW_HOOK*); how many. A hook out (state 4 or 5): a gold box; a hook
 * put away: a grey cross where it was left; the record's start (+0xA0) to aimed spot (+0xAC): red. */
#define HSSTORE_MAX_LINES 96
int hsstore_lines(float ends[][2][3], uint8_t colours[], int max);

#endif
