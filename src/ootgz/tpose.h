/* The ocarina-breath T-pose on demand (FINDINGS 9.13): keep the Face animation layer's bone mask at the
 * upper-body set the ocarina's breath leaves behind, so Link T-poses while he runs holding a bomb. */
#ifndef TPOSE_H
#define TPOSE_H

#include <stdbool.h>

bool tpose_enabled(void);
/* game thread. Off puts the Face layer's own mask back (which also clears the glitch the ocarina left) */
void tpose_set(bool on);
/* game thread, once a frame: while enabled, writes the mask again (the game rewrites it on idle fidgets,
 * attacks, ...) */
void tpose_frame(void);

#endif
