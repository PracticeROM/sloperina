/* Setting a quest's stage: see quests.h. */
#include "quests.h"

#include "gamedata.h"
#include "sys.h"
#include "z64hd.h"

/* The ChallengeMgr's set-step, the one the game's OneShotChallengeSetStep calls (FINDINGS 9.19):
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

/* the manager's flags (FINDINGS 9.19) */
#define MGR_READY        0x70 /* u8: 1 once it's set up; its update does nothing before */
#define MGR_RESYNC       0x72 /* u8: 1: the next update re-reads every quest's stage from GameData, then clears it */
#define MGR_SAVE_LOADING 0x73 /* u8: stage changes are made quietly; cleared with MGR_RESYNC */
#define MGR_NO_RESYNC    0x75 /* bit 0: the game's own resync is skipped */

typedef void (*select_saved_fn)(void *node);

bool quests_on_load(void)
{
  uint8_t *mgr = challenge_mgr();
  if (!mgr || !sys_mapped((uintptr_t)mgr, MGR_NO_RESYNC + 1, SYS_PERM_R | SYS_PERM_W) || mgr[MGR_READY] != 1)
    return false;
  /* a save load's order: the selected quest (SetSavedSelectedChallenge), SetChallengeMgrSaveLoading, then
   * SeqOneShotChallengeSyncToGameData */
  if (z64hd_game->challenge_select_saved) {
    uint64_t node[8] = { 0 }; /* the node: only its done bit (+0x18) is written */
    MAIN_FN(select_saved_fn, z64hd_game->challenge_select_saved)(node);
  }
  mgr[MGR_SAVE_LOADING] = 1;
  if (mgr[MGR_NO_RESYNC] & 1)
    return false;
  mgr[MGR_RESYNC] = 1;
  return true;
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
