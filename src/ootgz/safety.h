/* The game's safety section: which GameSafetySetting GameSafetyMgr has applied, and its flags (FINDINGS 9.12).
 * Read-only. */
#ifndef SAFETY_H
#define SAFETY_H

#include <stdbool.h>
#include <stdint.h>

/* GameSafetyMgr's packed flags: what the applied setting turns off */
enum {
  SAFETY_MINIGAME = 1u << 0,                 /* a Challenge is in an IsMiniGame step */
  SAFETY_ACTIVE = 1u << 3,                   /* a safety section is active */
  SAFETY_CHECK_AREA = 1u << 5,               /* IsCheckSafetyArea */
  SAFETY_DISABLE_SAVE = 1u << 6,             /* IsDisableSave */
  SAFETY_DISABLE_WARP = 1u << 7,             /* IsDisableWarp */
  SAFETY_DISABLE_AUTO_PLACEMENT = 1u << 8,   /* IsDisableAutoPlacement */
  SAFETY_DISABLE_TREASURE_BOX = 1u << 9,     /* IsDisableOpenTreasureBox */
  SAFETY_DISABLE_GRAB = 1u << 10,            /* IsDisableGrab */
  SAFETY_DISABLE_BOOT = 1u << 11,            /* IsDisableBoot */
  SAFETY_DISABLE_TIME = 1u << 12,            /* IsDisableTimeProgress */
  SAFETY_SAVE_WHEN_END = 1u << 13,           /* DoSaveWhenEnd */
  SAFETY_DISABLE_LOADING_PAUSE = 1u << 14,   /* IsDisableLoadingPause */
  SAFETY_NO_TEMP_SAVE_ERROR = 1u << 15,      /* IsNoDispErrorForTemporarySave */
};

/* The current scene's GameSafetyMgr: its flags and its setting's name hash. False while it can't be found
 * (no scene, or a build whose scene manager isn't known). */
bool safety_state(uint16_t *flags, uint32_t *name_hash);
/* where those two are in memory, for the wrench (debug > memory) */
bool safety_addresses(uintptr_t *flags, uintptr_t *name_hash);
/* A GameSafetySetting's name ("MiniGame_TimeStop") from its hash, or NULL if it isn't one of the game's. */
const char *safety_setting_name(uint32_t name_hash);
/* The flags as short words in bit order ("on save warp grab time"); "0" for none. */
void safety_flags_text(uint16_t flags, char *buf, int len);

#endif
