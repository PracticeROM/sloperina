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

struct z64hd_build {
  const char *version;
  const char *build_id;
  uint32_t revision;
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
  /* bool (actor, const float pos[3], float out[3], const void *riding, bool, int): the load's search for a safe
   * place round SavePos (FINDINGS 9.7); 0: not known for this build */
  uint32_t safety_search;
  /* the ground map the void check reads (voidmap.c, FINDINGS 9.8): the slot its owner is reached from, and the
   * table of its record formats; 0: not known for this build */
  uint32_t voxel_mgr_ptr;
  uint32_t voxel_formats;
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
