/* What the game shows the player, for remote control: the message window's state (FINDINGS §9.48). */
#ifndef NAVSTATE_H
#define NAVSTATE_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  DLG_UNKNOWN,        /* this build's offsets aren't known, or the window wasn't found */
  DLG_CLOSED,         /* 12 */
  DLG_TYPING,         /* 2: the text is still appearing */
  DLG_WAITING,        /* 3: all shown, waiting for A */
  DLG_TURNING,        /* 6: going to the next page after A */
  DLG_CHOICE_OPENING, /* 5: the choices are appearing */
  DLG_CHOICE,         /* 4: a choice is up; the cursor says which */
  DLG_OTHER,          /* a state not seen yet: raw says which */
} dialog_state_t;

typedef struct {
  dialog_state_t state;
  int raw;      /* the game's own value, or -1 */
  int cursor;   /* the choice cursor (0 = the first), -1 when no choice is up */
  char msg[96]; /* "Npc_Hylia_Soldier_008_Child:Talk_22" (EventFlowMsg/ left off), "" when not known */
} dialog_t;

bool dialog_read(dialog_t *out); /* false (state DLG_UNKNOWN) where the window can't be read */
const char *dialog_state_name(dialog_state_t state); /* "closed", "typing", "waiting"... */
bool dialog_open(const dialog_t *d); /* any state but closed and unknown */
/* the item wheel's phase: 0 closed, 1 opening, 2 open (takes the stick), -1 not known */
int wheel_phase(void);

#endif
