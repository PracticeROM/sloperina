/* Main offsets for program 0400486024DEC000, shared by the module and mkips. */
#ifndef Z64HD_BUILDS_H
#define Z64HD_BUILDS_H

#include "z64hd.h"

/* The stores that write Link's position which i found by abusing lichii's cheat engine
 * X and Y share the stp (pair); the variants per lock are the trainer's XY_VARIANTS. Z is one str.
 * The collision resolver's two stores freeze with any lock */
static const struct z64hd_pair pair_1_1_1 = {
  0x180EA24, 0x6D0106E0, /* stp d0, d1, [x23, #0x10] */
  0xFD000EE1,            /* str d1, [x23, #0x18]: X held, Y moves */
  0xFD000AE0,            /* str d0, [x23, #0x10]: Y held, X moves */
};

static const struct z64hd_store stores_1_1_1[] = {
  { 0x18D2C18, 0xA9002688, LOCK_BIT_COLLISION },
  { 0x18D2C20, 0xA9002668, LOCK_BIT_COLLISION },
  { 0x180EA2C, 0xFD0012E0, LOCK_BIT_Z },       /* str d0, [x23, #0x20] */
};

/* 1.0.0's: the same instructions at the same places in v131072's functions (65 of 65 masked words round each
 * matched, nowhere else), so the variants and bits are the same */
static const struct z64hd_pair pair_1_0_0 = {
  0x17C8D24, 0x6D0106E0, /* stp d0, d1, [x23, #0x10] */
  0xFD000EE1,
  0xFD000AE0,
};

static const struct z64hd_store stores_1_0_0[] = {
  { 0x188D3B8, 0xA9002688, LOCK_BIT_COLLISION },
  { 0x188D3C0, 0xA9002668, LOCK_BIT_COLLISION },
  { 0x17C8D2C, 0xFD0012E0, LOCK_BIT_Z },       /* str d0, [x23, #0x20] */
};

static const struct z64hd_build z64hd_builds[] = {
  {
    .version = "1.0.0",
    .build_id = "33FA6C2C23848282A77E8B88A0A93F4887C55920",
    .revision = 65536,
    /* the base game's item event returns from AddPouch without the separate notification */
    .absent = "pouch_notify pouch_notify_ptr",
    .nnmain = 0x11C1FB0,
    .mod0 = 0x3BE705C,
    .cave_start = 0x3BE6A00,
    .cave_end = 0x3BE7000,
    .hook_slot = 0x4E2A070,
    /* the word after the hook slot, as v131072's: past MOD0's bss end (0x4E2A068), nothing refers to it, 0 in game */
    .lock_slot = 0x4E2A078,
    .pair = &pair_1_0_0,
    .stores = stores_1_0_0,
    .store_count = sizeof(stores_1_0_0) / sizeof(stores_1_0_0[0]),
    .frame_site = 0x3AF2A6C,
    .gd_unlock = 0x3BE2B20,
    .gd_mgr_ptr = 0x4C184D8,
    .gd_getint = 0x420D50,
    .gd_setint = 0x4211F0,
    .vt_int_field = 0x4BBAB10,
    .vt_bool_type = 0x4BB9538,
    .vt_float_type = 0x4BB97B8,
    .vt_enum_type = 0x4BB98F8,
    .pouch_mgr_ptr = 0x4C18428,
    .pouch_add = 0x11378F0,
    .pouch_remove = 0x33456F0,
    .equip_slot_fn = 0x2DA26F0,
    .equip_all_fn = 0xEE0840,
    .vt_equip = 0x4AE4690,
    .vt_equip_owner = 0x4B7AA20,
    .ui_mgr_ptr = 0x4C183F0,
    .vt_dpad = 0x4B7BC20,
    .vt_hud = 0x4B85EB0,
    .vt_map_screen = 0x4B82AA0,
    .event_mgr_ptr = 0x4C183E8,
    /* remote control's starter and dialog state (z64hd.h): the same offsets as 1.1.1, read in 1.0.0's code (FINDINGS 9.48-9.49) */
    .actor_handle_entry = 0x1B0,
    .event_req_starter_mtx = 0x1918,
    .ui_screen_name = 0x178,
    .msgdlg_state = 0x2F8,
    .msgdlg_cursor = 0x1AC,
    .msgdlg_message = 0x4B8,
    .ui_screen_phase = 0x1A4,
    .event_req_ctor = 0xD3C9A0,
    .event_req_submit = 0xD3D2B0,
    .event_req_sub_dtor = 0x27C630,
    .event_req_sub_vt = 0x49101E8,
    .event_flows = 0xEB8,
    .vt_linkobj = 0x4AC0E30,
    .vt_param = 0x4AFE430,
    .disp_ptr = 0x4C18A98,
    .camera_ptr = 0x4E27C08,
    .frame_counter = 0x4E14740,
    /* made at 0xCCE6B0 (0x490 bytes; constructor 0xCCE730: a mutex at +0x00, +0x28 / +0x40 zeroed, the
     * actor tree's root at +0x38 null), as v131072's; the actors' life and position fields are the same */
    .actor_mgr_ptr = 0x4E20930,
    /* v131072's vtable 0x4B4C460 matched slot by slot (15 of 15 matchable) */
    .vt_life = 0x4AE7458,
    /* v131072's cct functions matched by masked signature (identical code but for ADRP/BL) and confirmed
     * by their callers: ForceNoHit's executor (0x290CD80) calls 0x1A9C00, the gravity reducer (0x2738FE0)
     * calls 0x1ED800. The property vtable is the one with SetPosition at +0xE8 and 0x6D74E0 at +0x250;
     * the body vtable is v131072's entries mapped over, 0x8B8 before it as in v131072. The controller
     * chain, IsOnGround (0x2A59600) and the fall-history fields cct.c writes are at the same offsets. */
    .vt_cct_property = 0x4BCF308,
    .vt_cct_body = 0x4BCEA50,
    /* the ragdoll bones' bodies (logged in game); 0x3268 after vt_cct_body in both builds */
    .vt_rigid_body = 0x4BD1CB8,
    .cct_force_no_hit = 0x1A9C00,
    .cct_set_position = 0x6D7730,
    .cct_set_velocity = 0x1ED800,
    .phive_mgr_ptr = 0x4C18258,
    .vt_autosave = 0x4B3B5F8,
    /* v131072's vtable matched (its entries 0x5F080 below v131072's, slot 8 returning "GameSafetyMgr"); the code
     * that packs its flags (0x3226AD0) and sets the minigame bit (0x2C9D3B0) matched whole, with +0x50 / +0x54 */
    .vt_safety_mgr = 0x4B3BE08,
    .hs_hit_vt = 0x4910168, /* from a bomb's DamageM, recorded live (FINDINGS 9.16) */
    /* v131072's 0x5F88C0, found through ExecuteEventPlayerImmediateRagdollDown's enter (identical in both);
     * the holder chain, the hit component and level, the controller's reaction / state and the player's
     * fields that ragdoll.c reads are at the same offsets as v131072's */
    .ragdoll_controller = 0x587460,
    /* save position (FINDINGS 9.7), from a memory dump of 1.0.0 (main at 0x80fc1000 in Lichii): AutoSaveMgr's
     * update matched v131072's 0x32762B0 by masked signature, in its vtable (vt_autosave above), which has
     * v131072's slots in the same places; the safety search is v131072's 0x945310 but for its GOT load (both
     * callers matched too). The scene manager and event state slots are 0x70460 below v131072's like the other bss
     * slots: in game the scene's entry 12 was AutoSaveMgr with slot B set, and the event state is the event
     * manager, as in v131072. */
    .scene_mgr_ptr = 0x4E271E8,
    .safety_search = 0x9444A0,
    .abyss_check = 0x818640, /* v131072's 0x82C510: the same code but for branch targets and its global's offsets */
    .event_state_ptr = 0x4E243E0,
    /* Found the same way (FINDINGS 9.8, 9.11): data through the code that loads it (v131072's loads matched, 9 to 12
     * each, all agreeing), functions by masked signature, vtables slot by slot. The ground map's record formats are
     * byte for byte v131072's; in game its manager chain led to a loaded area's chunks. The captions' functions
     * use the same fields (caption_empty also clears +0x91); caption_update is what the matched caller
     * (0x33A95D0, v131072's 0x340A460) calls. The captions' values agree with #38's, found separately. */
    .voxel_mgr_ptr = 0x4C18258,
    .voxel_formats = 0x3E89420,
    /* Phive world raycast (v131072's 0x5FF750 matched uniquely over 48 words) and its hit/filter vtables */
    .phive_raycast = 0x582D00,
    .vt_raycast_hit = 0x491B278,
    .vt_raycast_filter = 0x4BCB868,
    .caption_mgr_ptr = 0x4E08A40,
    .caption_empty = 0x963FA0,
    .caption_request = 0xF41000,
    .caption_update = 0x4E9760,
    .caption_empty_entry = 0x963EA0,
    .vt_load_list = 0x4B89340,
    .load_list_hide_rows = 0x3710A70,
    .vt_save_load_confirm = 0x4B89FE0,
    /* the quest manager (FINDINGS 9.13): its GOT slot 0x4C19D48 through the code that loads it (10 of 10 agreeing),
     * the set-step by masked signature (unique over 64 words) */
    .challenge_mgr_ptr = 0x4DB9998,
    .challenge_set_step = 0x2CA16F0,
    .gd_request_save = 0x36FED60,
    .gd_request_load = 0x36FEDD0,
    /* scene_mgr_ptr (above): the slot at navi_scene_ptr holds main+it (in both builds); WarpMgr's getter
     * main+0x27C2AD0 */
    .challenge_select_saved = 0x3289D20,
    .wm_ptr = 0x4C18BF0,
    .wm_set_time = 0x33F1580,
    .wm_on_save = 0x33F1410,
    .wm_on_load = 0x33F1300,
    .navi_scene_ptr = 0x4C184D0,
    .navi_get = 0x945E50,
    .navi_on_load = 0x3230F70,
    .player_camera_holder = 0x4C18B70,
    .player_camera_component = 0x360,
    .player_camera_state = 0x5B8,
    .player_camera_snap = 0x2E2D5D0,
    .player_camera_arrive = 0x2E2D590, /* called at main+0xE8F208 */
    .player_nayru_reset = 0x2E23670, /* from AINB node ResetNayruLove (main+0x28B8C40) */
    .vt_warp_mgr = 0x4B3CFF8, /* built at main+0xE0FE60; slot 6 main+0x465210 */
    .warp_mgr_place = 0x3251430, /* called at main+0x2886F98 */
    /* ExecuteActorForceSetPos (main+0x661F80) -> PhysicsComponent::forceSetPosAndRot;
     * OneShotActorHolderRequestDelete (main+0xE63BE0 -> main+0x944AE0) -> ActorBase::requestDelete;
     * SeqExecuteUpdatePreActorFlag (vt 0x4B43BF8, slot +0xF0) */
    .actor_force_set_pos = 0x665790,
    .actor_request_delete = 0xC4FA0,
    .preactor_update_flags = 0x3266A00,
    /* sead::GlobalRandom GOT slot (main+0x10BC); TimerMgr #19 (slot 6 0x324EA40) & MinigameMgr #18 (0x10DF7A0) */
    .global_random_ptr = 0x4C18260,
    .vt_timer_mgr = 0x4B3CEF8,
    .vt_minigame_mgr = 0x4B3C268,
    /* remote control's player state (z64hd.h): structure offsets, the same as 1.1.1's. Checked in Lichii
     * (24th November 2026): velocity against the position's change, on-ground and the action ids (40, 1, 33, 32,
     * 65) through walk, fall, landing and ZL; lock 3 with nothing to target; hand, ZR and weapon read -1, -1 and 0
     * on a save with no sword or items */
    .link_velocity = 0x388,
    .player_zr_item = 0x7D0,
    .player_held_kind = 0x7D8,
    .player_lock_state = 0x1C74,
    .player_on_ground = 0x13D0,
    .player_actions_1 = 0x18,
    .player_actions_2 = 0x270,
    .player_actions_3 = 0x188,
    .action_main_slot = 0x1C0,
    .action_weapon_slot = 0x1C8,
    .action_id = 0x4C,
    .death_request = 0x2CB69B0, /* 1.1.1 0x2D0A540, matched over 24 and 40 instructions (FINDINGS 9.27) */
    .actor_delete = 0xC4FA0,    /* 1.1.1 0xA6490, matched over 32 and 64 instructions */
    .actor_create_mgr_ptr = 0x4E21428, /* 1.1.1 0x4E91888 - 0x70460, from the matched caller (FINDINGS 9.29) */
    .actor_create = 0x97FEB0,          /* 1.1.1 0x372A70, matched over 24 and 48 instructions */
    /* 1.1.1's 0xB3CC40 and 0x329D990, through their callers (5 and 2 call sites agree); 29 of 30 and 39 of 40
     * masked words the same (a scene field offset inside the getter, a register choice) */
    /* the create node's update (vtable 0x493BF88 slot 11, 0x2696070) and leave call these, as 1.1.1's */
    .actor_handle_take = 0x3A4F580,
    .actor_handle_cancel = 0xA1F060,
    .actor_handle_release = 0x3A4F550,
    .owned_horse_mgr_get = 0x26AD4E0,
    .owned_horse_register = 0x323E840,
    .gd_set_string16 = 0xF83410, /* SetString16: 1.1.1 0xF69FF0, matched over 64 instructions (FINDINGS 9.26) */
    /* security guards (FINDINGS 9.36): 1.1.1's matched by masked signature, each unique over 24, 48 and 96 words; the
     * on-duty query through its matched AI node (1.1.1 0x2BF580 -> 1.0.0 0x2AFB80, which calls it) */
    .guard_param = 0x28A4DA0,
    .guard_alert_mode = 0x28A4E70,
    .guard_in_dark = 0x2CA5010,
    .guard_on_duty = 0x2AFC60,
    .guard_los_range = 0x2CA5790,
    .guard_los_below = 0x2CA59A0,
    .guard_los_above = 0x2CA5BA0,
    .guard_los_angle = 0x2CA5DA0,
    .bone_find = 0x7C6DA0,
    .door_close_node_vtable = 0x4A96CF8,
    .frame_counter_node_vtable = 0x4A98280,
    .generic_counter_node_vtable = 0x493A8D8,
    .pause_mgr_ptr = 0x4E279D8, /* 1.1.1's less 0x70460, as the camera's: checked by pausemgr.c's table check */
    .link_paths = {
      { 0x4DC7210, 0x480 }, { 0x4E243F8, 0x2E0 }, { 0x4E23158, 0xEF8 },
      { 0x4E24400, 0x328 }, { 0x4E23F08, 0xD70 }, /* the last checked in game: Link's object */
    },
  },
  {
    .version = "1.1.1",
    .build_id = "E0F3CCFAF50065AE4EA01EB5AEB13B1A45DA3348",
    .revision = 131072,
    /* hookshot storage: the pull lets go when nothing is hooked, and the recorded hit's layout isn't checked here.
     * The vtable itself is 0x4974A90 (FINDINGS 9.16) */
    .absent = "hs_hit_vt",
    .nnmain = 0x1202CD0,
    .mod0 = 0x3C4705C,
    .cave_start = 0x3C46400,
    .cave_end = 0x3C47000,
    .hook_slot = 0x4E9A4D0,
    /* the word after the hook slot: unverified (BUILDING.md: watch it before relying on it) */
    .lock_slot = 0x4E9A4D8,
    .pair = &pair_1_1_1,
    .stores = stores_1_1_1,
    .store_count = sizeof(stores_1_1_1) / sizeof(stores_1_1_1[0]),
    .frame_site = 0x3B500DC,
    .gd_unlock = 0x3C42470,
    .gd_mgr_ptr = 0x4C7E180,
    .gd_getint = 0x163D20,
    .gd_setint = 0x163B70,
    .vt_int_field = 0x4C20718,
    .vt_bool_type = 0x4C1F140,
    .vt_float_type = 0x4C1F3C0,
    .vt_enum_type = 0x4C1F500,
    .pouch_mgr_ptr = 0x4C7E0D0,
    .pouch_add = 0x110D570,
    .pouch_notify = 0x110D0B0,
    .pouch_notify_ptr = 0x4E6B718,
    .pouch_remove = 0x33A4FA0,
    .equip_slot_fn = 0x2DF6C80,
    .equip_all_fn = 0xF201B0,
    .vt_equip = 0x4B49698,
    .vt_equip_owner = 0x4BDFE68,
    .ui_mgr_ptr = 0x4C7E098,
    .vt_dpad = 0x4BE1068,
    .vt_hud = 0x4BEB710,
    .vt_map_screen = 0x4BE8300,
    .event_mgr_ptr = 0x4C7E090,
    .actor_handle_entry = 0x1B0,
    .event_req_starter_mtx = 0x1918,
    .event_req_ctor = 0xD79F10,
    .event_req_submit = 0xD7A820,
    .event_req_sub_dtor = 0x20B6F0,
    .event_req_sub_vt = 0x4974B10,
    .vt_linkobj = 0x4B25E18,
    .vt_param = 0x4B63438,
    .disp_ptr = 0x4C7E740,
    .camera_ptr = 0x4E98068,
    .frame_counter = 0x4E84BA0,
    .actor_mgr_ptr = 0x4E90D90,
    .vt_life = 0x4B4C460,
    .vt_cct_property = 0x4C34F10,
    .vt_cct_body = 0x4C34658,
    .vt_rigid_body = 0x4C378C0,
    .cct_force_no_hit = 0x158D40,
    .cct_set_position = 0x6B2530,
    .cct_set_velocity = 0x12DBE0,
    .phive_mgr_ptr = 0x4C7DF00,
    .scene_mgr_ptr = 0x4E97648,
    .vt_autosave = 0x4BA0848,
    .vt_safety_mgr = 0x4BA1058,
    .safety_search = 0x945310,
    .voxel_mgr_ptr = 0x4C7DF00,
    .voxel_formats = 0x3EE8840,
    .phive_raycast = 0x5FF750,
    .vt_raycast_hit = 0x497FBA0,
    .vt_raycast_filter = 0x4C31470,
    .event_state_ptr = 0x4E94840,
    .event_flows = 0xEB8,
    .ragdoll_controller = 0x5F88C0,
    .caption_mgr_ptr = 0x4E78E38,
    .caption_empty = 0xBB0010,
    .abyss_check = 0x82C510,
    .caption_request = 0xF6DA40,
    .caption_update = 0x4E5420,
    .caption_empty_entry = 0xBAFF10,
    .vt_load_list = 0x4BEEBA0,
    .load_list_hide_rows = 0x3773490,
    .vt_save_load_confirm = 0x4BEF840,
    .challenge_mgr_ptr = 0x4E29950,
    .challenge_set_step = 0x2CF54A0,
    .gd_request_save = 0x37617B0,
    .gd_request_load = 0x3761820,
    .wm_ptr = 0x4C7E898,
    .wm_set_time = 0x34522D0,
    .wm_on_save = 0x3452160,
    .wm_on_load = 0x3452050,
    .navi_scene_ptr = 0x4C7E178,
    .navi_get = 0xCAF800,
    .navi_on_load = 0x3290060,
    .player_camera_holder = 0x4C7E818,
    .player_camera_component = 0x360,
    .player_camera_state = 0x5B8,
    .player_camera_snap = 0x2E821D0,
    .player_camera_arrive = 0x2E82190,
    .player_nayru_reset = 0x2E78050,
    .vt_warp_mgr = 0x4BA2248, /* slot 6 main+0x57F4F0 */
    .warp_mgr_place = 0x32B0750,
    .challenge_select_saved = 0x32E8D50,
    /* ExecuteActorForceSetPos (main+0x4C8300) -> PhysicsComponent::forceSetPosAndRot;
     * OneShotActorHolderRequestDelete (main+0xE91290 -> main+0x945E80) -> ActorBase::requestDelete;
     * SeqExecuteUpdatePreActorFlag (vt 0x4BA8E48, slot +0xF0) */
    .actor_force_set_pos = 0x4C6630,
    .actor_request_delete = 0xA6490,
    .preactor_update_flags = 0x32C58C0,
    /* sead::GlobalRandom GOT slot (main+0x105C); TimerMgr #19 (slot 6 0x32ADB40) & MinigameMgr #18 (0x1127420) */
    .global_random_ptr = 0x4C7DF08,
    .vt_timer_mgr = 0x4BA2148,
    .vt_minigame_mgr = 0x4BA14B8,
    /* remote control's player state (z64hd.h), read in the running game */
    .link_velocity = 0x388,
    .player_zr_item = 0x7D0,
    .player_held_kind = 0x7D8,
    .player_lock_state = 0x1C74,
    .player_on_ground = 0x13D0,
    .player_actions_1 = 0x18,
    .player_actions_2 = 0x270,
    .player_actions_3 = 0x188,
    .action_main_slot = 0x1C0,
    .action_weapon_slot = 0x1C8,
    .action_id = 0x4C,
    .death_request = 0x2D0A540, /* FINDINGS 9.27 */
    .actor_delete = 0xA6490,
    .actor_create_mgr_ptr = 0x4E91888, /* FINDINGS 9.29 */
    .actor_create = 0x372A70,
    /* QueryHorseIsOwnedHorse compares actor+0x18 with the manager's +0x80; register is what the horse component
     * (0x2E3D298) and the map's horse setup (0x2F62228) call */
    /* ExecuteGenericRequestCreateActor's update 0x26E6400 (take through 0x26E6500) and leave 0x26E6480 */
    .actor_handle_take = 0x3AAEFD0,
    .actor_handle_cancel = 0xB2FA30,
    .actor_handle_release = 0xB2FBC0,
    .owned_horse_mgr_get = 0xB3CC40,
    .owned_horse_register = 0x329D990,
    .gd_set_string16 = 0xF69FF0, /* SetString16 (FINDINGS 9.26) */
    /* security guards (FINDINGS 9.36): the guards' AI nodes' (QueryNpcSecurityGuard*, SelectorNpcSecurityGuardCheck-
     * CustomLOSDistance) and QueryModelGetBoneMatrix34f's bone lookup */
    .guard_param = 0x28F72D0,
    .guard_alert_mode = 0x28F73A0,
    .guard_in_dark = 0x2CF8DD0,
    .guard_on_duty = 0x2BF660,
    .guard_los_range = 0x2CF9550,
    .guard_los_below = 0x2CF9760,
    .guard_los_above = 0x2CF9960,
    .guard_los_angle = 0x2CF9B60,
    .bone_find = 0x1B3620,
    .door_close_node_vtable = 0x4AFBCC8,
    .frame_counter_node_vtable = 0x4AFD250,
    .generic_counter_node_vtable = 0x499F200,
    .pause_mgr_ptr = 0x4E97E38,
    /* remote control's dialog state (z64hd.h), read in the running game (MessageDialog_00's vtable main+0x4BE91F8) */
    .ui_screen_name = 0x178,
    .msgdlg_state = 0x2F8,
    .msgdlg_cursor = 0x1AC,
    .msgdlg_message = 0x4B8,
    .ui_screen_phase = 0x1A4,
    .link_paths = {
      { 0x4E371D0, 0x480 }, { 0x4E94858, 0x2E0 }, { 0x4E935B8, 0xEF8 },
      { 0x4E94860, 0x328 }, { 0x4E94368, 0xD70 },
    },
  },
};

#endif
