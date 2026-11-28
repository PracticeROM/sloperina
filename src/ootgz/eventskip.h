/* End the running event (a cutscene, a dialogue, a chest): a timeline is skipped as the game's
 * SeqExecuteEventForceSkipAndWaitFinish node does, anything else cancelled as the event manager does (FINDINGS 9.10). */
#ifndef EVENTSKIP_H
#define EVENTSKIP_H

#include <stdbool.h>
#include <stddef.h>

bool eventskip_known(void);   /* this build's event layout is known */
bool eventskip_running(void); /* an event is running */
#define EVENTSKIP_NAME_MAX 64
/* the name of the running event's current flow (its flowchart); false (out "") when none is known */
bool eventskip_flow_name(char out[EVENTSKIP_NAME_MAX]);
/* game thread: skip or cancel the running event; false if none runs or it couldn't be done */
bool eventskip_start(void);
/* game thread: end the running event at once, quietly (title.c): the manager's cancel, or failing that the skip of
 * its timelines; false if none runs or neither could be done */
bool eventskip_end_now(void);
/* game thread, once a frame: repeats a skip while the event (or the next one in its chain) still runs */
void eventskip_frame(void);

#endif
