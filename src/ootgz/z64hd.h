/* Ocarina of Time (Switch 2): relocatable main offsets and shared structure layouts. */
#ifndef Z64HD_H
#define Z64HD_H

#include <stdbool.h>
#include <stdint.h>

struct z64hd_link_path {
  uint32_t global;
  uint32_t member;
};

struct z64hd_store {
  uint32_t off;
  uint32_t original;
  uint32_t bit;
};

/* X and Y share one store so it has its own stub with a bit for each axis lol */
struct z64hd_pair {
  uint32_t off;
  uint32_t original;  /* stp d0, d1, [x23, #0x10] */
  uint32_t x_held;    /* what the store becomes while X is held: str d1, [x23, #0x18] (Y only) */
  uint32_t y_held;    /* while Y is held: str d0, [x23, #0x10] (X only) */
};

#define LOCK_BIT_X         0u /* body X (horizontal) */
#define LOCK_BIT_Y         1u /* body Y (height) */
#define LOCK_BIT_Z         2u /* body Z (horizontal) */
#define LOCK_BIT_COLLISION 3u /* the collision resolver's two stores; set whenever an axis is held */
#define PATCH_BIT_NO_VOID  4u /* the same word: the void-out decision returns at once (link > no void outs) */

/* One entry per supported game build (z64hd_builds.h). Every offset below is meant to be filled in for every
 * build: a feature reads its offsets from the running build's entry and stays off where one is 0, so a gap would
 * quietly turn a feature off on one version. mkips (run by every build) fails when an offset is 0 and the build's
 * `absent` list doesn't name it, so a field is only left out on purpose (the game doesn't have it). Every uint32_t
 * member is checked (the Makefile lists them for mkips); pair, stores and link_paths are checked by name. */
struct z64hd_build {
  const char *version;
  const char *build_id;
  uint32_t revision;
  const char *absent; /* offsets this game doesn't have, space-separated field names; NULL: none */
  uint32_t nnmain;
  uint32_t mod0;
  uint32_t cave_start;
  uint32_t cave_end;
  uint32_t hook_slot;
  uint32_t lock_slot;               /* 8-byte word in main's bss */
  const struct z64hd_pair *pair;    /* NULL when the build has no stores */
  const struct z64hd_store *stores;
  uint32_t store_count;
  uint32_t frame_site;
  uint32_t gd_unlock;
  uint32_t gd_mgr_ptr;
  uint32_t gd_getint;
  uint32_t gd_setint;
  uint32_t vt_int_field;
  uint32_t vt_bool_type;
  uint32_t vt_float_type;
  uint32_t vt_enum_type;
  uint32_t pouch_mgr_ptr;
  uint32_t pouch_add;
  uint32_t pouch_notify;
  uint32_t pouch_notify_ptr;
  uint32_t pouch_remove;
  uint32_t equip_slot_fn;
  uint32_t equip_all_fn;
  uint32_t vt_equip;
  uint32_t vt_equip_owner;
  uint32_t ui_mgr_ptr;
  uint32_t vt_dpad;
  uint32_t vt_hud;
  uint32_t vt_map_screen;
  uint32_t event_mgr_ptr;
  uint32_t event_req_ctor;
  uint32_t event_req_submit;
  uint32_t event_req_sub_dtor;
  uint32_t event_req_sub_vt;
  uint32_t vt_linkobj;
  uint32_t vt_param;
  uint32_t disp_ptr;
  uint32_t camera_ptr;
  uint32_t frame_counter;
  uint32_t actor_mgr_ptr; /* the actor manager (actors.c); 0: not known for this build */
  uint32_t vt_life;       /* the Life component's vtable */
  /* Link's character controller (cct.c; FINDINGS "Native collision bypass"); 0: not known for this build */
  uint32_t vt_cct_property;
  uint32_t vt_cct_body;
  uint32_t vt_rigid_body;     /* a ragdoll bone's body: a sibling of the controller body (same base fields) */
  uint32_t cct_force_no_hit;  /* void (body, bool): queued, preserves filter state */
  uint32_t cct_set_position;  /* void (property, const vec3f_t *): updates attached bodies */
  uint32_t cct_set_velocity;  /* void (body, const vec3f_t *): queues linear velocity */
  /* [main+this]: phive::PhysicsMgr* holder (cct.c: direct hknpMotion lookup); 0: not known */
  uint32_t phive_mgr_ptr;
  /* the scene manager (its current scene's components, FINDINGS 9.3) and AutoSaveMgr's vtable
   * (scene component 12, savepos.c, FINDINGS 9.7); 0: not known for this build */
  uint32_t scene_mgr_ptr;
  uint32_t vt_autosave;
  /* GameSafetyMgr's vtable (safety.c, FINDINGS 9.14): the scene component holding the applied safety setting */
  uint32_t vt_safety_mgr;
  /* the vtable a bomb's medium hit leaves in Link's hit component (hsstore.c, FINDINGS 9.16): link > force hookshot
   * storage writes that hit. 0 where the trick doesn't work, which hides the option */
  uint32_t hs_hit_vt;
  /* bool (actor, const float pos[3], float out[3], const void *riding, bool, int): the load's search for a safe
   * place round SavePos (FINDINGS 9.7); 0: not known for this build */
  uint32_t safety_search;
  /* void (player component): the void-out decision, the only store of a non-zero abyss type (FINDINGS 9.25). The
   * patch gates its first instruction (pacibsp) with PATCH_BIT_NO_VOID; 0: not known for this build */
  uint32_t abyss_check;
  /* the ground map the void check reads (voidmap.c, FINDINGS 9.8): the slot its owner is reached from, and the
   * table of its record formats; 0: not known for this build */
  uint32_t voxel_mgr_ptr;
  uint32_t voxel_formats;
  /* bool (const float from[3], const float to[3], void *hit, const void *ignore, int layer, int): Phive world
   * raycast for sampling ground heights (voidmap.c), and the two vtables its 0x180-byte hit struct uses */
  uint32_t phive_raycast;
  uint32_t vt_raycast_hit;
  uint32_t vt_raycast_filter;
  /* [main+this]: the event state the restart point update checks (+0x68: an event is running, FINDINGS 9.7);
   * 0: not known for this build */
  uint32_t event_state_ptr;
  /* [[main+event_mgr_ptr]+0x68] +this: the running event's array of flows (eventskip.c, FINDINGS 9.10);
   * 0: not known for this build */
  uint32_t event_flows;
  /* Link's ragdoll (ragdoll.c, FINDINGS 9.9); 0: not known for this build */
  uint32_t ragdoll_controller;   /* controller (object): Link's ragdoll controller, NULL if he has none */
  /* the saves' captions the Load list shows (savebackup.c, FINDINGS 9.11): [main+caption_mgr_ptr] the CaptionMgr;
   * caption_empty (mgr): empties its entries, state 0; caption_request (mgr): the game's load request (checks
   * its save reader is free, then sets +0x8D bit 0). 0: not known for this build (the list waits for a restart) */
  uint32_t caption_mgr_ptr;
  uint32_t caption_empty;
  uint32_t caption_request;
  uint32_t caption_update; /* (mgr): the CaptionMgr's per-frame update, which reads the captions a step at a time */
  uint32_t caption_empty_entry; /* (entry, index in its list, bool master quest list): empties one entry, state 0 */
  /* the pause screen's Load list, to refresh it in place (FINDINGS 9.11): its screen's vtable (UISystemLoadList,
   * +0x2B0 its list), the list's "hide every row" (void (list)), and the "Load this save?" screen's vtable; 0: not
   * known (the list shows the restored saves once it's opened again) */
  uint32_t vt_load_list;
  uint32_t load_list_hide_rows;
  uint32_t vt_save_load_confirm;
  /* the quests' stages (quests.c, FINDINGS 9.13): [main+challenge_mgr_ptr] the ChallengeMgr; challenge_set_step its
   * set-step. A build without them writes GameData only (stages reach the game after a save load) */
  uint32_t challenge_mgr_ptr;
  uint32_t challenge_set_step;
  /* GameDataMgr in-memory binary save/load (memfile.c): bool (gd_mgr, buf, int file_idx); 0: unknown */
  uint32_t gd_request_save;
  uint32_t gd_request_load;
  /* WorldMgr [[main+wm_ptr]]: [wm+0x578] clock (vfunc +0x30 -> seconds); on_save flushes live clock/weather
   * into GameData before serialize, on_load reloads from GameData after deserialize; set_time(wm, seconds) */
  uint32_t wm_ptr;
  uint32_t wm_set_time;
  uint32_t wm_on_save;
  uint32_t wm_on_load;
  /* NaviMgr reload hook: navi = navi_get([[main+navi_scene_ptr]]); if (navi) navi_on_load(navi); */
  uint32_t navi_scene_ptr;
  uint32_t navi_get;
  uint32_t navi_on_load;
  /* Player camera: actor handle at [[main+player_camera_holder]]+0x38, component [[actor+0x270]+component],
   * view state at component+state, snap(component); arrive(component): what a map's arrival calls, rebuilding the
   * camera's target from the actor it follows ([component+0x38]) and asking for a snap (state +0x7E4) */
  uint32_t player_camera_holder;
  uint32_t player_camera_component;
  uint32_t player_camera_state;
  uint32_t player_camera_snap;
  uint32_t player_camera_arrive;
  /* Nayru's Love: the player component [[actor+0x270]+0x358] keeps the barrier's time left (float, frames) at
   * +0x1634 and its length at +0x1638; a frame update turns the barrier on while the time left is above 0.
   * nayru_reset(component) ends it like taking it off (time 0, its GameData mirror and an anim value too). */
  uint32_t player_nayru_reset;
  /* WarpMgr, the game component behind Farore's Wind's sparkle (made once, no static pointer to it): its vtable;
   * slot 6 (mgr, change bits) re-reads WarpPoint_FaroreWind when told GameData changed (all-zero bits: everything) */
  uint32_t vt_warp_mgr;
  /* WarpMgr::place(mgr, x, y, z): moves the sparkle to a point (Link's feet; it adds the height), as casting
   * does (AINB node OneShotFaroreWindSetActorPos) */
  uint32_t warp_mgr_place;
  /* SetSavedSelectedChallenge's work (node): selects the quest GameData names (UInt 0x2029D3D8) in the manager, or
   * none; it only marks node+0x18 done besides. A save load runs it (FINDINGS 9.24). 0: not known for this build */
  uint32_t challenge_select_saved;
  /* Scene actor restore (memfile.c): forceSetPosAndRot(physics_comp, pos, rot_mat33, immediate) called by
   * ExecuteActorForceSetPos on [[actor+0x270]+0x50]; requestDelete(actor, reason) called by
   * OneShotActorHolderRequestDelete; preactor_update_flags(node) is SeqExecuteUpdatePreActorFlag's update
   * (clears PreActor dead/disable flags and resyncs GameData-backed dead flags before ResetGimmick). 0: unknown */
  uint32_t actor_force_set_pos;
  uint32_t actor_request_delete;
  uint32_t preactor_update_flags;
  /* Phase 3 memfiles: sead::GlobalRandom GOT slot ([[main+this]] -> u32 state[4]), and TimerMgr (scene #19) /
   * MinigameMgr (scene #18) vtables whose slot 6 is onGameDataChanged(mgr, change_bits). 0: unknown */
  uint32_t global_random_ptr;
  uint32_t vt_timer_mgr;
  uint32_t vt_minigame_mgr;
  /* Link's state for remote control's status and trace (remote.c): his velocity (3 floats) at actor+link_velocity;
   * in the player object ([[actor+0x270]+0x358]) the item on ZR and in hand (s32s), the lock-on state (2 with a
   * target, 3 with none) and the on-ground flag (bit 0); his action table at [[[player+player_actions_1]
   * +player_actions_2]+player_actions_3], whose main and weapon slots point at actions with their id at +action_id.
   * Structure offsets, not main's. 0: not known for this build (status and trace print - or -1) */
  uint32_t link_velocity;
  uint32_t player_zr_item;
  uint32_t player_held_kind;
  uint32_t player_lock_state;
  uint32_t player_on_ground;
  uint32_t player_actions_1;
  uint32_t player_actions_2;
  uint32_t player_actions_3;
  uint32_t action_main_slot;
  uint32_t action_weapon_slot;
  uint32_t action_id;
  /* GameData SetString16 (manager, const char *const *value, hash): queued like SetInt, what the AINB node
   * (Seq)OneShotSetNonStructGameDataString16 calls; title.c writes Sequence_EventResult with it (FINDINGS 9.26) */
  uint32_t gd_set_string16;
  /* bool (void **death, u32 reason, u32 sub): a death request to an actor's Death component [[actor+0x270]+0x1C0],
   * checked, what the AI node OneShotDeathRequestDeathToTargetActor calls; refused by the actor's own
   * DeathReasonProcedures where it ignores the reason (bosses for FallIntoAbyss) (FINDINGS 9.27) */
  uint32_t death_request;
  /* bool (actor, u32 reason): the engine's request to delete an actor, what OneShotGenericDeleteThisActor calls
   * (reason 0); takes the manager's lock itself (FINDINGS 9.27) */
  uint32_t actor_delete;
  /* the actor manager that creates actors (a slot: sys_ptr(MAIN(it))), not the tree's actor_mgr_ptr; and its create
   * request (mgr, const char *const *name, arg, handle, queue, ...) -> bool, what ExecuteGenericRequestCreateActor
   * calls: loads the actor's pack itself and makes it on a worker (FINDINGS 9.29) */
  uint32_t actor_create_mgr_ptr;
  uint32_t actor_create;
  /* the owned horse (FINDINGS 9.34): get(scene manager [main+scene_mgr_ptr]) -> OwnedHorseMgr (its slot 17), whose
   * u32 at +0x80 is the owned horse's id (actor+0x18); register(mgr, horse) makes a horse the owned one, the one
   * Epona's Song calls (it does nothing for a horse whose kind isn't 1 or 2) */
  /* the create request's handle (FINDINGS 9.34), as ExecuteGenericRequestCreateActor's update uses it: its status's
   * u16 is 3 once the actor is made, 4 if the request failed; take(&handle, 1, 0) -> the actor, cancel(&handle, 1),
   * release(&handle) after a failure */
  uint32_t actor_handle_take;
  uint32_t actor_handle_cancel;
  uint32_t actor_handle_release;
  uint32_t owned_horse_mgr_get;
  uint32_t owned_horse_register;
  /* security guards (FINDINGS 9.36), what the guards' AI nodes call: param(actor) -> its SecurityGuardParam or 0
   * (looked up in its parameter table, any actor); alert_mode(param) -> the AlertMode object; in_dark(actor) and
   * on_duty(actor) -> bool (QueryNpcSecurityGuardIsInDarkArea, ...IsSecurityMode); the custom line of sight's
   * los_range / los_below / los_above / los_angle(actor, const bool *alert) -> float, Light, Dark or PlayerRidingHorse
   * as it applies now; bone_find(model, const char *const *name) -> {u16 part, u16 bone} returned through x8 */
  uint32_t guard_param;
  uint32_t guard_alert_mode;
  uint32_t guard_in_dark;
  uint32_t guard_on_duty;
  uint32_t guard_los_range;
  uint32_t guard_los_below;
  uint32_t guard_los_above;
  uint32_t guard_los_angle;
  uint32_t bone_find;
  /* a swinging door's close check (FINDINGS 9.38): the vtable of its AINB node, SelectorDoorCanClose (calc at slot 39:
   * main+0x5E58A0 / 1.0.0 main+0x593380); made while the door's event runs */
  uint32_t door_close_node_vtable;
  /* the AINB counter nodes (FINDINGS 9.47), drowning's timers: SelectorGenericFrameCounter (frames gone by at +0x20, its
   * length through the pointer at +0x50) and ExecuteGenericCounter (frames left at +0x20, its length at +0x2C) */
  uint32_t frame_counter_node_vtable;
  uint32_t generic_counter_node_vtable;
  /* [main+this]: the pause manager (FINDINGS 9.1: its pause types and their request counts; pausemgr.c) */
  uint32_t pause_mgr_ptr;
  /* Held-actor & BSA action restore (memfile.c): ActorLink::set(link, actor, flags), LifterComponent::drop(lifter,
   * reason, s0, s1), LifterComponent::forceLift(lifter, link, &key, flag), player_lift_anim(link_actor, link), and
   * BSAComponent::setActionVerb(bsa, req_0x60, flag) (FINDINGS 9.22). 0: not known for this build */
  uint32_t actor_link_set;
  uint32_t lifter_drop;
  uint32_t lifter_force_lift;
  uint32_t player_lift_anim;
  uint32_t bsa_set_action_verb;
  struct z64hd_link_path link_paths[5];
};

extern const struct z64hd_build *z64hd_game;
extern uintptr_t z64hd_main;

#define MAIN(off)          (z64hd_main + (uintptr_t)(off))

/* the character controller's, for the running build (0 where it isn't known) */
#define Z64HD_VT_CCT_PROPERTY  (z64hd_game->vt_cct_property)
#define Z64HD_VT_CCT_BODY      (z64hd_game->vt_cct_body)
#define Z64HD_VT_RIGID_BODY    (z64hd_game->vt_rigid_body)
#define Z64HD_CCT_FORCE_NO_HIT (z64hd_game->cct_force_no_hit)
#define Z64HD_CCT_SET_POSITION (z64hd_game->cct_set_position)
#define Z64HD_CCT_SET_VELOCITY (z64hd_game->cct_set_velocity)
#define MAIN_FN(type, off) ((type)MAIN(off))

/* Structure offsets shared by the supported builds. */
#define Z64HD_GD_BOOL_TYPE      0x08        /* manager+: the Bool type entry (see gamedata.h) */
#define Z64HD_GD_INT_TYPE       0xC8        /* manager+: the Int type entry (see gamedata.h) */
#define Z64HD_GD_FLOAT_TYPE     0x188       /* manager+: the Float type entry */
#define Z64HD_GD_ENUM_TYPE      0x248       /* manager+: the Enum type entry */
#define Z64HD_GD_STRING_TYPE    0x608       /* manager+: the String64 type entry */
#define Z64HD_STR_STRIDE        0x108
#define Z64HD_STR_VALUE         0x68        /* char *value, followed by u32 capacity */
#define Z64HD_UI_REFRESH        (0x310 / 8) /* vtable slot 98: "refresh contents" */
#define Z64HD_EVENT_REQ_SIZE    0x1B20
#define Z64HD_EVENT_REQ_PARAMS  0x08        /* param entry array (32 inline entries at +0x18) */
#define Z64HD_EVENT_REQ_NPARAMS 0x10        /* u32 count; capacity (u32) at +0 */
#define Z64HD_EVENT_REQ_SUB_A   0x1950
#define Z64HD_EVENT_REQ_SUB_B   0x1968
#define Z64HD_EVENT_REQ_NAME    0x1A90      /* char *event name; capacity (u32) at +0x1A98 */
#define Z64HD_EVENT_PARAM_SIZE  0xC8        /* entry: u32 type, char *name (+8, cap u32 +0x10), value +0x98 */
#define Z64HD_EVENT_PARAM_STRING 1          /* value: char * */
#define Z64HD_EVENT_PARAM_BOOL   4          /* value: u8 */

#endif
