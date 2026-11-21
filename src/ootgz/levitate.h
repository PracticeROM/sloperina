/* Levitate: L3 toggles floating, X/B rise/descend, neither or R3 holds height;
 * left stick moves horizontally. Through-walls mode leases the controller's
 * collision bypass. The game does not see L3 while enabled, nor X/B/R3 while
 * floating. Also the 2x speed cheat (CHEAT_SPEED), using the physics record. */
#ifndef LEVITATE_H
#define LEVITATE_H

#include <stdbool.h>

extern bool levitate_enabled;
extern bool levitate_through_walls;
extern int levitate_speed;        /* X/B units/s; legacy 0 uses 4, idle always hovers */
extern int levitate_move_speed;   /* left stick, units per second */

void levitate_frame(bool menu_open);
const char *levitate_state(void); /* for watches/menus */
bool levitate_floating(void);     /* L3 has him floating (R3 then holds height) */

/* Link's physics record (where his position really lives), resolved directly from his controller;
 * save / load position (position.c) moves him through it too. Positions are drawn ones (the record sits 0.585 above). */
typedef enum { REC_NONE, REC_READY } link_record_status_t;
link_record_status_t link_record_status(void);
/* move Link there now; v (x, z) is the velocity written with it (it doesn't turn him: link_record_turn does).
 * The record may then lag behind the drawn position for settle_frames without counting as lost. False if it
 * isn't known. */
bool link_record_move(double x, double y, double z, float face_vx, float face_vz, int settle_frames);
/* turn Link by that many radians (under a half turn either way; the way his facing counts up), at once.
 * False if the record isn't known. */
bool link_record_turn(double radians);
void link_record_resync(void);    /* back to where Link is drawn (a move that didn't take) */

#endif
