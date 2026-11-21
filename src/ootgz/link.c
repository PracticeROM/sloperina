#include "link.h"

#include "sys.h"
#include "z64hd.h"

/* Link's actor */
#define ACTOR_STATE_PTR 0x8       /* actor +8: an object holding... */
#define STATE_PARAMS_PTR 0x6A0    /* ...at +0x6A0, the parameter objects */
#define PARAMS_HEALTH_CUR 0x1C10  /* parameter object: current health */
#define PARAMS_HEALTH_MAX 0x1C18  /* parameter object: maximum health */
#define PARAM_VALUE 0x8           /* a parameter object's value */

/* the drawn-position object */
#define DISP_POSITION 0x29C

/* the camera manager's active camera */
#define CAMERA_MANAGER_CAMERA 0x68

/* equipment */
#define EQUIP_COMPONENT_LINK 0x18 /* component +0x18: its owner actor (Link) */
#define EQUIP_OWNER_COMPONENT 0x470

uintptr_t link_actor(void)
{
  const struct z64hd_link_path *paths = z64hd_game->link_paths;
  for (unsigned i = 0; i < sizeof(z64hd_game->link_paths) / sizeof(*paths) && paths[i].global; i++) {
    uintptr_t holder = sys_ptr(MAIN(paths[i].global));
    uintptr_t actor = holder ? sys_ptr(holder + paths[i].member) : 0;
    if (actor && sys_ptr(actor) == MAIN(z64hd_game->vt_linkobj))
      return actor;
  }
  return 0;
}

/* Health: [[link+8]+0x6A0] holds parameter objects (vtable main+VT_PARAM) at +0x1C10 (current)
 * and +0x1C18 (max); the value is at +8 (oot26-tools). */
bool link_health(int32_t **cur, int32_t **max)
{
  uintptr_t actor = link_actor();
  uintptr_t state = actor ? sys_ptr(actor + ACTOR_STATE_PTR) : 0;
  uintptr_t params = state ? sys_ptr(state + STATE_PARAMS_PTR) : 0;
  if (!params)
    return false;
  uintptr_t cur_param = sys_ptr(params + PARAMS_HEALTH_CUR);
  uintptr_t max_param = sys_ptr(params + PARAMS_HEALTH_MAX);
  if (!cur_param || !max_param || sys_ptr(cur_param) != MAIN(z64hd_game->vt_param) ||
      sys_ptr(max_param) != MAIN(z64hd_game->vt_param))
    return false;
  *cur = (int32_t *)(cur_param + PARAM_VALUE);
  *max = (int32_t *)(max_param + PARAM_VALUE);
  return true;
}

bool link_drawn_pos(vec3f_t *out)
{
  uintptr_t disp = sys_ptr(MAIN(z64hd_game->disp_ptr));
  disp = disp ? sys_ptr(disp) : 0;
  if (!disp || !sys_mapped(disp + DISP_POSITION, 12, SYS_PERM_R))
    return false;
  *out = *(vec3f_t *)(disp + DISP_POSITION);
  return true;
}

/* Link's facing as a unit (x, z): actor +0x328 cos, +0x330 sin (oot26-tools; 0 = +z) */
bool link_facing(double *fx, double *fz)
{
  uintptr_t actor = link_actor();
  if (!actor || !sys_mapped(actor + 0x328, 12, SYS_PERM_R))
    return false;
  *fz = *(float *)(actor + 0x328);
  *fx = *(float *)(actor + 0x330);
  return true;
}

uintptr_t camera(void)
{
  uintptr_t manager = sys_ptr(MAIN(z64hd_game->camera_ptr));
  return manager ? sys_ptr(manager + CAMERA_MANAGER_CAMERA) : 0;
}

uint32_t frame_counter(void)
{
  return *(volatile uint32_t *)MAIN(z64hd_game->frame_counter);
}

/* --- equipment ---------------------------------------------------------------------------- */

static uintptr_t pouch_equip_owner(void)
{
  if (!z64hd_game || !z64hd_game->pouch_mgr_ptr || !z64hd_game->vt_equip_owner)
    return 0;
  uintptr_t holder = sys_ptr(MAIN(z64hd_game->pouch_mgr_ptr));
  uintptr_t owner = holder ? sys_ptr(holder) : 0;
  return owner && sys_ptr(owner) == MAIN(z64hd_game->vt_equip_owner) ? owner : 0;
}

uintptr_t link_equip_component(void)
{
  uintptr_t link = link_actor();
  if (!link || !z64hd_game || !z64hd_game->vt_equip)
    return 0;
  uintptr_t owner = pouch_equip_owner();
  uintptr_t component = owner ? sys_ptr(owner + EQUIP_OWNER_COMPONENT) : 0;
  if (!component || sys_ptr(component) != MAIN(z64hd_game->vt_equip) ||
      sys_ptr(component + EQUIP_COMPONENT_LINK) != link)
    return 0;
  return component;
}

uintptr_t link_equip_owner(void)
{
  return link_equip_component() ? pouch_equip_owner() : 0;
}
