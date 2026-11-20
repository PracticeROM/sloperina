/* Setting a quest's stage: see quests.h. */
#include "quests.h"

#include "gamedata.h"
#include "sys.h"
#include "z64hd.h"

/* The ChallengeMgr's set-step, the one the game's OneShotChallengeSetStep calls (FINDINGS 9.12):
 *   mgr    the manager, *[main+challenge_mgr_ptr]
 *   quest  the quest's key in the manager (quest_t.key: murmur3 of its name without "Step_")
 *   stage  the new stage's hash
 *   flag   the node's own flag (it passes 0 here)
 *   reset  resets the stage's sub-entries (the node passes 1)
 *   force  also takes a quest that is at NotReady or Complete, which it otherwise refuses
 * It queues the stage; the manager's next update changes it and writes the GameData Enum. False when the manager
 * has no quest with that key (or refuses it). */
typedef bool (*set_step_fn)(void *mgr, uint32_t quest, uint32_t stage, bool flag, bool reset, bool force);

bool quests_through_manager(void)
{
  return z64hd_game && z64hd_game->challenge_mgr_ptr && z64hd_game->challenge_set_step;
}

/* the manager, NULL while there is none (or this build's isn't known) */
static void *challenge_mgr(void)
{
  if (!quests_through_manager() || !sys_mapped(MAIN(z64hd_game->challenge_mgr_ptr), 8, SYS_PERM_R))
    return NULL;
  return *(void *volatile *)MAIN(z64hd_game->challenge_mgr_ptr);
}

void quest_set_stage(const quest_t *quest, uint32_t stage)
{
  void *mgr = challenge_mgr();
  if (mgr && MAIN_FN(set_step_fn, z64hd_game->challenge_set_step)(mgr, quest->key, stage, false, true, true))
    return; /* the manager has it, and writes GameData itself */
  if (mgr) /* not expected: every quest in data_quests has an entry in the manager */
    log_printf("story: the quest manager refused %s, writing GameData only", quest->label);
  gd_set_enum(quest->hash, stage); /* the stage reaches the rest of the game after a save load */
}
