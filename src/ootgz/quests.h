/* Setting a quest's stage (the story menu; FINDINGS 9.19).
 *
 * A stage lives in two places: the GameData Enum Step_<name>, and the game's ChallengeMgr, which keeps its own
 * copy and reads GameData back only after a save load. Writing the Enum alone reaches only what reads GameData
 * (dialogue choices); NPC schedules and the stage's own events ask the manager and keep the old stage. So where
 * this build's manager is known, the stage is set through it, the way the game's events do, and the game writes
 * GameData itself. */
#ifndef QUESTS_H
#define QUESTS_H

#include <stdbool.h>
#include "data.h"

/* true when the stage is set through the ChallengeMgr on this build (z64hd_game has its addresses) */
bool quests_through_manager(void);

/* Gives the quest that stage (a stage hash, murmur3 of its name): through the ChallengeMgr where possible, else
 * by writing GameData only, which the rest of the game sees after a save load. */
void quest_set_stage(const quest_t *quest, uint32_t stage);

/* After GameData was restored without a save load (memfiles): has the ChallengeMgr re-read every quest's stage
 * from GameData on its next update, quietly, and take GameData's selected quest, as a save load does. False when
 * this build's manager isn't known or doesn't exist yet. */
bool quests_on_load(void);

#endif
