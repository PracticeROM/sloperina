/* Levitate and 2x speed: both move Link through his physics record.
 *
 * Link's position lives in a Havok hknpMotion record: doubles (x, y, z) 0.585 above his drawn position,
 * followed by 48 bytes of motion state the game ignores the position without (FINDINGS §5.9-§5.15).
 * cct_motion_record() resolves that record directly from Link's character controller body through
 * phive::PhysicsMgr and hknpWorld (no heap scan or test-lifting).
 *
 * Levitate (as ootmenu, game.py _float, once per frame): L3 starts floating, L3 again drops him, R3
 * held keeps the height; X rises, B descends, neither hovers. The left stick moves him
 * relative to the camera. Walls: the game pushes
 * Link back out of them between our writes; a push-back is a move against his heading (after he has
 * held it for a moment, or the momentum of a turn looks like one), and it marks the wall for a
 * moment so we stop pushing into it (FINDINGS §5.10). "through walls" now requests the
 * controller's ForceNoHit mode and sets position through its own API (cct.c), rather
 * than only undoing push-back. Other movement retains the floor/wall correction logic.
 *
 * 2x speed: while Link walks or runs on the stick, add the distance the game moved him this frame
 * again (the game's collision still pushes him back out of walls).
 *
 * Walk through walls: once the stick has pushed Link the same way for a moment while he barely moves,
 * his controller's collision is switched off (cct.c, the game's ForceNoHit, as levitate's through
 * walls) and he's walked on that way (relative to the camera, as levitate steers) at his running speed
 * and at the height he was at, through the controller's own position setter, until the stick is let
 * go or turned; then his collision comes back and his landing is protected, as after a float. Writing
 * the physics record alone doesn't work: the game's collision puts him back at the wall every frame
 * (FINDINGS §8). Floors don't hold him up meanwhile, hence the held height. */
#include "levitate.h"

#include "cheats.h"
#include "cct.h"
#include "data.h"
#include "gamedata.h"
#include "gfx.h"
#include "binds.h"
#include "input.h"
#include "link.h"
#include "sys.h"
#include "z64hd.h"

float hypotf(float x, float y); /* from sdk */
float sinf(float x);
float cosf(float x);

bool levitate_enabled;
bool levitate_through_walls = true;
int levitate_speed = 4;       /* rise, units/s */
int levitate_move_speed = 6;  /* stick, units/s */

/* the physics record: position (3 doubles), then motion state (12 words) */
#define RECORD_POS_SIZE 24
#define MOTION_SIZE 48
#define MOTION_WORDS 12
#define RECORD_SIZE (RECORD_POS_SIZE + MOTION_SIZE)
#define RECORD_REQUESTED_VEL 0x24 /* requested velocity (z, x, y floats) */
#define RECORD_COLLIDED_VEL 0x3C  /* the same after collision */
/* A turn: a float the game takes as a yaw rate. Written once, Link turns by value / 60 radians (the way his facing
 * counts up) and stays turned; measured on 1.1.1 (94.25 turned him 90.0 degrees, 188.5 turned him 179.9, -9.34
 * turned him -8.9). The captured motion below has 9.34 here, which turned him 8.9 degrees on every write of it. */
#define RECORD_TURN 0x34

#define PHYS_Y 0.585            /* the record's height above Link's drawn position */
#define FOLLOW_TOLERANCE 3      /* per axis: the record still belongs to Link */
#define RISE_RAMP 0.35          /* s to reach the rise speed (game.py LEV_RISE_RAMP) */
#define MOVE_RAMP 0.15
#define PUSHBACK_MIN 0.01       /* units: a push-back this big marks a wall */
#define WALL_MEMORY 0.15        /* s a wall stays marked */
#define STEADY_TIME 0.1         /* s Link must keep his heading before a push-back counts */
#define SAME_WAY_MIN 0.7        /* cosine of the angle within which two directions are the same way */
#define FROZEN_FRAMES 3         /* frames with neither the record nor the drawn position moving */
#define FROZEN_AFTER 0.3        /* s of floating before a freeze can be told from the float starting */
#define STILL_SPEED 0.05        /* below this Link isn't moving (and keeps his heading) */
#define TINY_VELOCITY 0.01      /* the velocity written when he isn't moving, so he keeps facing */
#define SPEED_MAX_STEP_SQ 4.0   /* 2x speed: a bigger move (squared) is a warp or a respawn */
#define RUN_SPEED 4.4           /* units/s: Link's full-tilt run (the requested velocity mid-run) */
#define HELD_BACK 0.3           /* walls: asking for and moving at under this much of the stick's speed */
#define HELD_BACK_TIME 0.2      /* s held back, stick steady, before he's walked on through */
#define FREE_AGAIN 0.8          /* walls: the game asking for this much of the stick's speed hands him back */
#define MAX_DT 0.1              /* s: longer frames count as this */
#define STICK_DEAD 7849
#define STICK_MAX 32767.0
#define FLOAT_TRACE_FRAMES 40   /* frames of each float that are logged */
#define SPEED_TRACE_FRAMES 12   /* frames of 2x speed that are logged (once) */
#define WALLS_TRACE_FRAMES 24   /* frames of walking through a wall that are logged (each time) */

#define FLOAT_EXPONENT_MASK 0x7F800000u
#define MOTION_FLOAT_MAX 10000.0f

/* Link's actor: his facing as cos and sin (0 = +z) */
#define LINK_FACING_COS 0x328
#define LINK_FACING_SIN 0x330

/* the game's camera */
#define CAM_POSITION 0x38
#define CAM_LOOK_AT 0x44

/* motion state captured from Link mid-run (game.py WAKE) */
static const uint8_t captured_motion[MOTION_SIZE] = {
  0x00, 0x00, 0x00, 0x00, 0x25, 0x67, 0x15, 0x41, 0x00, 0x00, 0x00, 0x00, 0xce, 0xcf, 0x8d, 0x40,
  0xfb, 0xf9, 0x9f, 0x3e, 0x26, 0x3c, 0xa1, 0xbf, 0x00, 0x00, 0x00, 0x00, 0x24, 0x67, 0x15, 0x41,
  0x00, 0x00, 0x00, 0x00, 0xcc, 0xcf, 0x8d, 0x40, 0xfc, 0xf9, 0x9f, 0x3e, 0x24, 0x3c, 0xa1, 0xbf,
};

static uintptr_t record;
static bool record_has_motion;
/* the record API (levitate.h): frames in which a move made through it may still be settling
 * (the record ahead of the drawn position isn't "lost") */
static int hold_frames;
static uint64_t last_tick;

static double abs_double(double v)
{
  return v < 0 ? -v : v;
}

/* The 48 bytes after Link's position are motion state: all zero while he stands, otherwise floats
 * of modest size (velocities). */
static bool plausible_motion(uintptr_t motion, bool *all_zero)
{
  *all_zero = true;
  for (int i = 0; i < MOTION_WORDS; i++) {
    uint32_t word = ((volatile uint32_t *)motion)[i];
    if (!word)
      continue;
    *all_zero = false;
    float value;
    memcpy(&value, &word, 4);
    if (!(value == value) || value > MOTION_FLOAT_MAX || value < -MOTION_FLOAT_MAX ||
        (word & FLOAT_EXPONENT_MASK) == 0) /* NaN, huge, denormal */
      return false;
  }
  return true;
}

/* Position, then (if write_motion) the motion state with our velocity: requested velocity (z, x, y)
 * at record +0x24 and the same after collision at +0x3C (FINDINGS §5.10); the rest as captured
 * (game.py WAKE). */
static void write_record(uintptr_t addr, double x, double y, double z, float vx, float vy, float vz,
                         bool write_motion)
{
  volatile double *pos = (volatile double *)addr;
  pos[0] = x;
  pos[1] = y;
  pos[2] = z;
  if (!write_motion)
    return;
  /* The game turns Link to face his velocity; a zero velocity has no direction and set him
   * spinning. Not moving: a tiny one along the way he already faces. */
  double fx, fz;
  if (vx * vx + vz * vz < STILL_SPEED * STILL_SPEED && link_facing(&fx, &fz)) {
    vx = (float)(fx * TINY_VELOCITY);
    vz = (float)(fz * TINY_VELOCITY);
  }
  uint8_t motion[sizeof(captured_motion)];
  memcpy(motion, captured_motion, sizeof(motion));
  const float velocity[3] = { vz, vx, vy }, no_turn = 0;
  memcpy(motion + (RECORD_REQUESTED_VEL - RECORD_POS_SIZE), velocity, sizeof(velocity));
  memcpy(motion + (RECORD_TURN - RECORD_POS_SIZE), &no_turn, sizeof(no_turn)); /* a move doesn't turn him */
  memcpy(motion + (RECORD_COLLIDED_VEL - RECORD_POS_SIZE), velocity, sizeof(velocity));
  memcpy((void *)(addr + RECORD_POS_SIZE), motion, sizeof(motion));
}

static bool record_at_follows_link(uintptr_t addr)
{
  vec3f_t drawn;
  if (!addr || (addr & 7) || !link_drawn_pos(&drawn) ||
      !sys_mapped(addr, RECORD_SIZE, SYS_PERM_R | SYS_PERM_W))
    return false;
  const volatile double *pos = (const volatile double *)addr;
  return abs_double(pos[0] - drawn.x) < FOLLOW_TOLERANCE &&
         abs_double(pos[2] - drawn.z) < FOLLOW_TOLERANCE &&
         abs_double(pos[1] - drawn.y - PHYS_Y) < FOLLOW_TOLERANCE;
}

static bool record_follows_link(void)
{
  return record_at_follows_link(record);
}

static uintptr_t ensure_record(void)
{
  uintptr_t current = cct_motion_record();
  if (!current || !sys_mapped(current, RECORD_SIZE, SYS_PERM_R | SYS_PERM_W)) {
    record = 0;
    return 0;
  }
  if (current == record)
    return record;
  if (!record_at_follows_link(current)) {
    record = 0;
    return 0;
  }
  record = current;
  record_has_motion = true;
  log_printf("levitate: Link's physics record is %#lx", (unsigned long)record);
  return record;
}

/* --- shared helpers ----------------------------------------------------------------------------- */

/* v moved towards target by at most step */
static double approach(double v, double target, double step)
{
  if (v < target)
    return v + step < target ? v + step : target;
  return v - step > target ? v - step : target;
}

static bool same_way(double ax, double az, double bx, double bz)
{
  double lengths = (double)hypotf((float)ax, (float)az) * hypotf((float)bx, (float)bz);
  return lengths > 0 && (ax * bx + az * bz) / lengths > SAME_WAY_MIN;
}

/* left stick, -1..1, dead zone applied */
static void stick(double *sx, double *sy)
{
  int32_t lx, ly, rx, ry;
  input_sticks(&lx, &ly, &rx, &ry);
  double length = hypotf((float)lx, (float)ly);
  if (length < STICK_DEAD) {
    *sx = 0;
    *sy = 0;
    return;
  }
  double scale = (length - STICK_DEAD) / (STICK_MAX - STICK_DEAD) / length;
  if (scale * length > 1)
    scale = 1 / length;
  *sx = lx * scale;
  *sy = ly * scale;
}

/* the camera's view direction on the ground, as a unit (x, z) */
static bool camera_forward(double *fx, double *fz)
{
  uintptr_t cam = camera();
  if (!cam || !sys_mapped(cam + CAM_POSITION, 24, SYS_PERM_R))
    return false;
  float *pos = (float *)(cam + CAM_POSITION);
  float *look = (float *)(cam + CAM_LOOK_AT);
  double dx = look[0] - pos[0];
  double dz = look[2] - pos[2];
  double length = hypotf((float)dx, (float)dz);
  if (length < 1e-3)
    return false;
  *fx = dx / length;
  *fz = dz / length;
  return true;
}

/* --- levitate --------------------------------------------------------------------------------- */

static double controller_y_offset; /* physics centre relative to drawn feet */
static uintptr_t float_actor, landing_actor;
static unsigned landed_frames;

static bool floating;              /* L3 turned it on */
static bool phasing;               /* walk through walls has Link's collision off (cct.c); not floating */
static bool moving_link;           /* we're writing Link's position */
static double pos_x, pos_y, pos_z; /* our position for him */
static double vel_x, vel_y, vel_z; /* and velocity */
static double heading_x, heading_z;            /* last direction he moved in */
static double wall_x, wall_z, wall_until;      /* a wall last pushed him this way, until this time */
static double past_headings[2][2];             /* his heading a moment ago and the moment before */
static double next_heading_sample;
static double float_clock;                     /* s of gameplay since the float started */
static double last_written[3];
static vec3f_t last_drawn;
static int frozen_frames;
static int float_trace; /* frames of float logged (diagnostics) */

static void drop(void)
{
  if (!floating) {
    if (!phasing) /* walk through walls owns it then */
      cct_noclip_end();
    return;
  }
  floating = false;
  if (moving_link) {
    bool reset = cct_reset_vertical_velocity(float_actor);
    cct_clear_fall_distance(float_actor);
    landing_actor = float_actor;
    landed_frames = 0;
    log_printf("levitate: release controller velocity reset %s; landing protection on",
               reset ? "queued" : "unavailable");
    /* Holding the position does not clear the game's falling velocity. Start release
     * from rest once, then let normal gravity take over. Also clear the legacy
     * record's two vertical fields; the controller API above is essential.
     * Never change position, horizontal motion or the full WAKE search_state here.
     * Revalidate here: L3 is handled before this frame's normal record-loss check. */
    bool zero;
    if (record && record_has_motion && record_follows_link() &&
        plausible_motion(record + 24, &zero)) {
      *(volatile float *)(record + 0x2C) = 0; /* requested y velocity */
      *(volatile float *)(record + 0x44) = 0; /* post-collision y velocity */
    }
    moving_link = false;
  }
  cct_noclip_end(); /* Restore collision only after requesting the velocity reset. */
}

static void float_start(void)
{
  float_actor = link_actor();
  landing_actor = 0;
  landed_frames = 0;
  volatile double *r = (volatile double *)record;
  pos_x = r[0], pos_y = r[1], pos_z = r[2];
  vel_x = vel_y = vel_z = heading_x = heading_z = wall_x = wall_z = 0;
  wall_until = next_heading_sample = float_clock = 0;
  memset(past_headings, 0, sizeof(past_headings));
  frozen_frames = 0;
  link_drawn_pos(&last_drawn);
  controller_y_offset = pos_y - last_drawn.y;
  last_written[0] = pos_x, last_written[1] = pos_y, last_written[2] = pos_z;
  moving_link = true;
  float_trace = 0;
}

/* Gameplay frozen (pause screen, item menus; the frame counter keeps going there): neither has the
 * record moved off what we wrote nor Link been redrawn (game.py LEV_PAUSED_AFTER). */
static void track_frozen(double cx, double cy, double cz, vec3f_t drawn)
{
  bool moved = cx != last_written[0] || cy != last_written[1] || cz != last_written[2] ||
               drawn.x != last_drawn.x || drawn.y != last_drawn.y || drawn.z != last_drawn.z;
  frozen_frames = moved ? 0 : frozen_frames + 1;
  last_drawn = drawn;
}

/* The rise and sink commands (X/B unless changed in settings > rebind commands) control height; neither, both or the
 * hover command (R3) hold the current height. */
static int vertical_input(void)
{
  return cmd_held(CMD_HOVER) ? 0 : cmd_held(CMD_RISE) - cmd_held(CMD_SINK);
}

static void steer(double dt)
{
  float_clock += dt;
  int vertical = vertical_input();
  double rise = levitate_speed > 0 ? levitate_speed : 4;
  double move = levitate_move_speed;
  if (!vertical || vel_y * vertical < 0)
    vel_y = 0;
  if (vertical)
    vel_y = approach(vel_y, vertical * rise, rise / RISE_RAMP * dt);
  double sx, sy, fx, fz;
  double target_x = 0, target_z = 0;
  stick(&sx, &sy);
  if ((sx || sy) && camera_forward(&fx, &fz)) {
    double rx = -fz, rz = fx; /* camera's right */
    target_x = (fx * sy + rx * sx) * move;
    target_z = (fz * sy + rz * sx) * move;
  }
  double step = move / MOVE_RAMP * dt;
  vel_x = approach(vel_x, target_x, step);
  vel_z = approach(vel_z, target_z, step);
  pos_y += vel_y * dt;
}

/* (cx, cz): where the record is now. Accept the game's push-backs out of walls and stop pushing into
 * a wall that has just pushed back. */
static void respect_walls(double cx, double cz)
{
  double game_dx = cx - pos_x, game_dz = cz - pos_z; /* how the game moved him since our last write */
  bool wall = float_clock < wall_until;
  if (float_clock >= next_heading_sample) {
    past_headings[1][0] = past_headings[0][0];
    past_headings[1][1] = past_headings[0][1];
    past_headings[0][0] = heading_x;
    past_headings[0][1] = heading_z;
    next_heading_sample = float_clock + STEADY_TIME;
  }
  bool steady = same_way(heading_x, heading_z, past_headings[0][0], past_headings[0][1]) &&
                same_way(heading_x, heading_z, past_headings[1][0], past_headings[1][1]);
  bool against_heading = game_dx * heading_x + game_dz * heading_z < 0;
  if (steady && against_heading && !(wall && game_dx * wall_x + game_dz * wall_z < 0)) {
    /* the game pushed him out of a wall: keep that */
    pos_x = cx;
    pos_z = cz;
    double length = hypotf((float)game_dx, (float)game_dz);
    if (length > PUSHBACK_MIN) {
      wall_x = game_dx / length;
      wall_z = game_dz / length;
      wall_until = float_clock + WALL_MEMORY;
      wall = true;
    }
  }
  if (wall) {
    double into = vel_x * wall_x + vel_z * wall_z;
    if (into < 0) {
      vel_x -= into * wall_x;
      vel_z -= into * wall_z;
    }
  }
}

static void float_step(double dt, bool menu_open)
{
  volatile double *pos = (volatile double *)record;
  double cx = pos[0], cy = pos[1], cz = pos[2];
  vec3f_t drawn;
  link_drawn_pos(&drawn);
  track_frozen(cx, cy, cz, drawn);
  int vertical = vertical_input();
  bool descend_through_floor = !menu_open && levitate_through_walls && vertical < 0;
  if (cy > pos_y && !descend_through_floor && !cct_noclip_active())
    pos_y = cy;
  if (float_trace < FLOAT_TRACE_FRAMES) { /* what the game did with our last write */
    double fx = 0, fz = 0;
    link_facing(&fx, &fz);
    log_printf("levitate f%d: record y %.3f (we wrote %.3f) x %.3f z %.3f | drawn y %.3f | facing %.3f,%.3f | "
               "frozen %d clock %.2f", float_trace, cy, last_written[1], cx, cz, (double)drawn.y, fx, fz,
               frozen_frames, float_clock);
    float_trace++;
  }
  if ((frozen_frames >= FROZEN_FRAMES && float_clock > FROZEN_AFTER) || menu_open) {
    vel_x = vel_y = vel_z = 0;
  } else {
    steer(dt);
  }
  if (!levitate_through_walls)
    respect_walls(cx, cz);
  pos_x += vel_x * dt;
  pos_z += vel_z * dt;
  if (hypotf((float)vel_x, (float)vel_z) > STILL_SPEED) {
    heading_x = vel_x;
    heading_z = vel_z;
  }
  if (cct_noclip_active()) {
    vec3f_t position = { (float)pos_x, (float)(pos_y - controller_y_offset), (float)pos_z };
    if (!cct_noclip_move(&position)) {
      drop();
      gfx_notice("levitate: controller unavailable; stopped");
      return;
    }
  }
  /* Position every frame; the motion state only when the game has cleared it (it ignores the
   * position then), as game.py did. Rewriting all of it every frame kept Link spinning. */
  bool cleared = true;
  for (int i = 0; i < MOTION_WORDS; i++)
    cleared &= ((volatile uint32_t *)(record + RECORD_POS_SIZE))[i] == 0;
  write_record(record, pos_x, pos_y, pos_z, (float)vel_x, (float)vel_y, (float)vel_z, record_has_motion && cleared);
  if (!cct_reset_vertical_velocity(float_actor) || !cct_clear_fall_distance(float_actor)) {
    drop();
    gfx_notice("levitate: fall protection unavailable; stopped");
    return;
  }
  last_written[0] = pos_x;
  last_written[1] = pos_y;
  last_written[2] = pos_z;
}

/* --- 2x speed ------------------------------------------------------------------------------------ */

static bool speed_known;
static double speed_x, speed_z; /* where we put him last frame */
static int speed_trace;

static void speed_step(void)
{
  volatile double *pos = (volatile double *)record;
  double sx, sy;
  stick(&sx, &sy);
  if (!(sx || sy)) {
    speed_known = false;
    return;
  }
  double cx = pos[0], cz = pos[2];
  if (speed_known) {
    double dx = cx - speed_x, dz = cz - speed_z; /* how far the game moved him this frame */
    if (speed_trace < SPEED_TRACE_FRAMES) {
      log_printf("speed f%d: game moved him %.3f, %.3f (record %.3f, %.3f; we put him %.3f, %.3f)", speed_trace, dx,
                 dz, cx, cz, speed_x, speed_z);
      speed_trace++;
    }
    if (dx * dx + dz * dz < SPEED_MAX_STEP_SQ) { /* not a warp or a respawn */
      pos[0] = cx + dx;
      pos[2] = cz + dz;
      cx += dx;
      cz += dz;
    }
  }
  speed_x = cx;
  speed_z = cz;
  speed_known = true;
}

/* --- walk through walls --------------------------------------------------------------------------- */

static bool walls_known;
static double walls_x, walls_z;  /* where Link was (or we put him) last frame */
static bool walls_wrote;         /* we moved him last frame */
static vec3f_t walls_drawn;      /* his drawn position last frame */
static double walls_dir_x, walls_dir_z; /* the way the stick pushed last frame */
static double held_back_for;     /* s the game has been holding him back */
static bool walking_through;     /* we're walking him on (phasing: with his collision off) */
static double phase_y;           /* the height (his feet) he's held at meanwhile */
static int walls_trace;          /* frames logged since he started walking through */

/* The game didn't run last frame (the emulator paused, frame advance holding): it neither touched the
 * position we wrote nor redrew Link there. Without this, a held stick would carry him on. */
static bool walls_frozen(double cx, double cz, vec3f_t drawn)
{
  return walls_wrote && cx == walls_x && cz == walls_z && drawn.x == walls_drawn.x && drawn.y == walls_drawn.y &&
         drawn.z == walls_drawn.z;
}

/* Where the stick pushes, on the ground relative to the camera: a unit (x, z) and how far (0..1). */
static bool stick_way(double *ux, double *uz, double *tilt)
{
  double sx, sy, fx, fz;
  stick(&sx, &sy);
  *tilt = hypotf((float)sx, (float)sy);
  if (*tilt <= 0 || !camera_forward(&fx, &fz))
    return false;
  double rx = -fz, rz = fx; /* camera's right */
  *ux = (fx * sy + rx * sx) / *tilt;
  *uz = (fz * sy + rz * sx) / *tilt;
  return true;
}

/* Hand Link back to the game (logged by the caller when walking_through was set). */
static void walls_stop(void)
{
  walking_through = false;
  held_back_for = 0;
  if (phasing) {
    /* as a float's release: from rest, with the landing protected, then collision back */
    uintptr_t actor = link_actor();
    cct_reset_vertical_velocity(actor);
    cct_clear_fall_distance(actor);
    landing_actor = actor;
    landed_frames = 0;
    cct_noclip_end();
    phasing = false;
  }
}

static void walls_step(double dt, bool double_speed)
{
  volatile double *pos = (volatile double *)record;
  const volatile float *requested = (const volatile float *)(record + RECORD_REQUESTED_VEL); /* z, x, y */
  double cx = pos[0], cz = pos[2];
  vec3f_t drawn;
  link_drawn_pos(&drawn);
  if (walls_frozen(cx, cz, drawn) || gd_game_paused(0, 0))
    return; /* keep where we put him, for when the game runs again */
  walls_drawn = drawn;
  walls_wrote = false;

  double ux, uz, tilt;
  double game_dx = cx - walls_x, game_dz = cz - walls_z; /* how the game moved him since last frame */
  bool steering = stick_way(&ux, &uz, &tilt);
  bool same_dir = steering && same_way(ux, uz, walls_dir_x, walls_dir_z);
  bool warped = game_dx * game_dx + game_dz * game_dz >= SPEED_MAX_STEP_SQ; /* or a respawn */
  if (!walls_known || !steering || dt <= 0 || warped) {
    if (walking_through)
      log_printf("walls: handed back (known %d, stick %.2f, camera %d, dt %.3f, warped %d)", (int)walls_known,
                 tilt, (int)(tilt > 0 && !steering), dt, (int)warped);
    walls_stop();
  } else {
    if (!same_dir) {
      if (walking_through)
        log_printf("walls: handed back, the stick turned");
      walls_stop();
    }
    double stick_speed = tilt * RUN_SPEED;
    double asked = requested[1] * ux + requested[0] * uz; /* the game's requested velocity, the stick's way */
    double moved = (game_dx * ux + game_dz * uz) / dt;    /* and how fast it actually moved him that way */
    if (walking_through && !phasing && asked >= FREE_AGAIN * stick_speed) {
      log_printf("walls: handed back, running freely (asked %.2f)", asked);
      walls_stop();
    }
    if (!walking_through) {
      bool held = asked < HELD_BACK * stick_speed && moved < HELD_BACK * stick_speed;
      held_back_for = held ? held_back_for + dt : 0;
      if (held_back_for >= HELD_BACK_TIME) {
        walking_through = true;
        walls_trace = 0;
        phasing = cct_noclip_begin();
        phase_y = drawn.y;
        log_printf("walls: held back %.2f s (asked %.2f, moved %.2f of %.2f), through, collision off %d",
                   held_back_for, asked, moved, stick_speed, (int)phasing);
      }
    }
    double x = cx, z = cz;
    if (walking_through) {
      double speed = stick_speed * (double_speed ? 2 : 1);
      x = walls_x + ux * speed * dt;
      z = walls_z + uz * speed * dt;
      if (walls_trace < WALLS_TRACE_FRAMES) {
        log_printf("walls f%d: game moved %.3f, %.3f asked %.2f | record %.3f, %.3f, %.3f -> %.3f, %.3f, %.3f",
                   walls_trace, game_dx, game_dz, asked, cx, pos[1], cz, x, phasing ? phase_y + PHYS_Y : pos[1], z);
        walls_trace++;
      }
      if (phasing) { /* as levitate's through walls: the controller's own setter, at the held height */
        vec3f_t to = { (float)x, (float)phase_y, (float)z };
        if (!cct_noclip_move(&to)) {
          log_printf("walls: handed back, controller unavailable");
          walls_stop();
        } else {
          cct_reset_vertical_velocity(link_actor()); /* no falling meanwhile */
          walls_wrote = true;
        }
      }
    } else if (double_speed) { /* as 2x speed: the game's move again */
      x = cx + game_dx;
      z = cz + game_dz;
    }
    if (!phasing && (x != cx || z != cz)) {
      pos[0] = x;
      pos[2] = z;
      walls_wrote = true;
    }
    cx = x;
    cz = z;
  }
  walls_x = cx;
  walls_z = cz;
  walls_dir_x = steering ? ux : 0;
  walls_dir_z = steering ? uz : 0;
  walls_known = true;
}

/* --- frame --------------------------------------------------------------------------------------- */

/* seconds since the last frame, at most MAX_DT */
static double frame_seconds(void)
{
  uint64_t now = sys_tick();
  double dt = last_tick ? (double)(now - last_tick) / (double)sys_tick_freq() : 0;
  last_tick = now;
  if (dt > MAX_DT)
    dt = MAX_DT;
  return dt;
}

/* Clear fall history through the first supported landing. */
static void protect_landing(bool menu_open)
{
  if (landing_actor) {
    uintptr_t actor = link_actor();
    if (actor && actor != landing_actor) {
      landing_actor = 0; /* A reload/replacement must not protect a different actor. */
    } else if (cct_clear_fall_distance(landing_actor)) {
      bool grounded;
      if (cct_on_ground(landing_actor, &grounded)) {
        landed_frames = grounded ? landed_frames + !menu_open : 0;
        if (landed_frames >= 6) {
          landing_actor = 0;
          log_printf("levitate: landed; normal fall damage restored");
        }
      } else {
        landed_frames = 0;
      }
    }
  }
}

/* Loading zones: taking an exit writes the destination to Sequence_ReservedNextBanc (GD_STR_NEXT_MAP, FINDINGS
 * §5.18), which the game clears again later. From then until the map changes or the game clears it (or
 * MAP_CHANGE_FRAMES) levitate is off and L3 doesn't start it, so Link goes through as he would on foot. */
#define MAP_CHANGE_FRAMES 600 /* ~10 s: a reservation that never leads to a new map (or the same map) */

static char seen_next[GD_STR_LEN], seen_map[GD_STR_LEN];
static uint32_t map_change_frame; /* when the last exit was taken; 0: none pending */

static bool copy_if_changed(char *seen, const char *now)
{
  int i = 0;
  while (i < GD_STR_LEN - 1 && now[i] && now[i] == seen[i])
    i++;
  if (seen[i] == (i < GD_STR_LEN - 1 ? now[i] : 0))
    return false;
  for (i = 0; i < GD_STR_LEN - 1 && now[i]; i++)
    seen[i] = now[i];
  seen[i] = 0;
  return true;
}

/* true while Link is going through a loading zone */
static bool changing_map(void)
{
  const char *next = gd_get_str(GD_STR_NEXT_MAP);
  const char *map = gd_get_str(GD_STR_MAP);
  if (next && copy_if_changed(seen_next, next)) {
    if (seen_next[0]) {
      map_change_frame = frame_counter() | 1;
      log_printf("levitate: exit taken to %s; off until the map loads", seen_next);
    } else {
      map_change_frame = 0; /* the game is done with it (the same map: the Lost Woods' loops) */
    }
  }
  if (map && copy_if_changed(seen_map, map))
    map_change_frame = 0;
  if (map_change_frame && frame_counter() - map_change_frame > MAP_CHANGE_FRAMES)
    map_change_frame = 0;
  return map_change_frame != 0;
}

/* Loading zone triggers only see Link with his collision on, and through walls (and walk through walls) switch
 * it off: he'd float through a zone without it firing. So inside one (data_exits, tools/exits.py: the trigger
 * volumes, placed like air walls: world = t + R (s * local), R = Rz Ry Rx) levitate ends and collision comes
 * back, and the game takes the exit as it would on foot. */
#define EXIT_MARGIN 0.5f   /* units: Link's body reaches this far round the point tested */
#define EXIT_BODY_UP 0.8f  /* the point tested: this far above his feet (his middle) */

static const airmap_t *exit_map;
static char exit_map_name[64];

static bool same_name(const char *a, const char *b)
{
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

static const airmap_t *current_exit_map(void)
{
  const char *name = gd_get_str(GD_STR_MAP);
  if (!name)
    return 0;
  if (!same_name(name, exit_map_name)) {
    int i = 0;
    for (; i < (int)sizeof(exit_map_name) - 1 && name[i]; i++)
      exit_map_name[i] = name[i];
    exit_map_name[i] = 0;
    exit_map = 0;
    for (int m = 0; m < data_n_exitmaps; m++)
      if (same_name(data_exitmaps[m].map, exit_map_name))
        exit_map = &data_exitmaps[m];
  }
  return exit_map;
}

/* p in the exit's own unscaled space: R^T (p - t) / s */
static void exit_local(const airwall_t *zone, const float p[3], float out[3])
{
  float sx = sinf(zone->r[0]), cx = cosf(zone->r[0]);
  float sy = sinf(zone->r[1]), cy = cosf(zone->r[1]);
  float sz = sinf(zone->r[2]), cz = cosf(zone->r[2]);
  float R[9] = { cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz,
                 cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz,
                 -sy,     sx * cy,                cx * cy };
  float d[3] = { p[0] - zone->t[0], p[1] - zone->t[1], p[2] - zone->t[2] };
  for (int i = 0; i < 3; i++) {
    float v = R[i] * d[0] + R[3 + i] * d[1] + R[6 + i] * d[2];
    out[i] = zone->s[i] != 0 ? v / zone->s[i] : 0;
  }
}

static float absf(float v)
{
  return v < 0 ? -v : v;
}

/* the exit Link is in (its destination is logged), or 0 */
static const airwall_t *exit_at_link(void)
{
  const airmap_t *map = current_exit_map();
  vec3f_t feet;
  if (!map || !link_drawn_pos(&feet))
    return 0;
  float p[3] = { feet.x, feet.y + EXIT_BODY_UP, feet.z };
  for (int i = 0; i < map->n; i++) {
    const airwall_t *zone = &data_exits[map->first + i];
    float local[3], margin[3];
    exit_local(zone, p, local);
    for (int k = 0; k < 3; k++)
      margin[k] = zone->s[k] != 0 ? EXIT_MARGIN / absf(zone->s[k]) : 0;
    for (int s = 0; s < zone->n_shapes; s++) {
      const airshape_t *shape = &data_exitshapes[zone->first_shape + s];
      bool inside;
      if (shape->type == SHAPE_BOX) {
        inside = absf(local[0] - shape->a[0]) <= shape->b[0] + margin[0] &&
                 absf(local[1] - shape->a[1]) <= shape->b[1] + margin[1] &&
                 absf(local[2] - shape->a[2]) <= shape->b[2] + margin[2];
      } else { /* a cylinder standing upright between a and b */
        float low = shape->a[1] < shape->b[1] ? shape->a[1] : shape->b[1];
        float high = shape->a[1] < shape->b[1] ? shape->b[1] : shape->a[1];
        float r = shape->radius + (margin[0] > margin[2] ? margin[0] : margin[2]);
        float dx = local[0] - shape->a[0], dz = local[2] - shape->a[2];
        inside = local[1] >= low - margin[1] && local[1] <= high + margin[1] && dx * dx + dz * dz <= r * r;
      }
      if (inside)
        return zone;
    }
  }
  return 0;
}

static const airwall_t *in_exit; /* this frame's exit_at_link, while Link's collision is (or would be) off */

/* the levitate command (L3 unless changed): start / stop */
static void handle_l3(bool menu_open)
{
  in_exit = (floating || phasing || (levitate_enabled && cmd_pressed(CMD_LEVITATE)) || cheats[CHEAT_WALLS])
              ? exit_at_link() : 0;
  if (in_exit && floating) {
    log_printf("levitate: in a loading zone (%s map, zone %d); collision back on", exit_map_name,
               (int)(in_exit - &data_exits[exit_map->first]));
    gfx_notice("levitate off: loading zone");
    drop();
    return;
  }
  if (changing_map()) {
    if (floating)
      gfx_notice("levitate off: loading zone");
    drop();
  } else if (!levitate_enabled) {
    drop();
  } else if (!menu_open && cmd_pressed(CMD_LEVITATE)) {
    if (floating) {
      drop();
      gfx_notice("levitate off");
    } else if (in_exit) {
      gfx_notice("levitate: not in a loading zone");
    } else {
      floating = true;
      gfx_notice("levitate on");
    }
  }
}

/* --- Link's record, for position.c ------------------------------------------------------------- */

link_record_status_t link_record_status(void)
{
  return ensure_record() ? REC_READY : REC_NONE;
}

/* One write of the record's turn (RECORD_TURN): Link turns by that much and stays turned. */
bool link_record_turn(double radians)
{
  if (!ensure_record() || !record_has_motion)
    return false;
  *(volatile float *)(record + RECORD_TURN) = (float)(radians * 60.0);
  return true;
}

/* Position, plus the motion state: the game ignores a new position while Link stands unless that's
 * written too, and standing leaves small leftovers in it, so it's rarely all zero (game.py's move
 * wrote it every time too). */
bool link_record_move(double x, double y, double z, float face_vx, float face_vz, int settle_frames)
{
  if (!ensure_record())
    return false;
  write_record(record, x, y + PHYS_Y, z, face_vx, 0, face_vz, record_has_motion);
  if (moving_link) { /* floating: carry on from there */
    pos_x = x;
    pos_y = y + PHYS_Y;
    pos_z = z;
    vel_x = vel_y = vel_z = 0;
    last_written[0] = pos_x;
    last_written[1] = pos_y;
    last_written[2] = pos_z;
  }
  speed_known = false;
  if (settle_frames > hold_frames)
    hold_frames = settle_frames;
  return true;
}

void link_record_resync(void)
{
  vec3f_t drawn;
  if (!ensure_record() || !link_drawn_pos(&drawn))
    return;
  volatile double *pos = (volatile double *)record;
  pos[0] = drawn.x;
  pos[1] = drawn.y + PHYS_Y;
  pos[2] = drawn.z;
  if (moving_link) {
    pos_x = drawn.x;
    pos_y = drawn.y + PHYS_Y;
    pos_z = drawn.z;
    last_written[0] = pos_x;
    last_written[1] = pos_y;
    last_written[2] = pos_z;
  }
  hold_frames = 0;
}

/* --- frame --------------------------------------------------------------------------------------- */

static void frame(bool menu_open)
{
  if (hold_frames > 0)
    hold_frames--;
  double dt = frame_seconds();
  protect_landing(menu_open);
  handle_l3(menu_open);

  uintptr_t current = cct_motion_record();
  bool current_ok = current && sys_mapped(current, RECORD_SIZE, SYS_PERM_R | SYS_PERM_W);
  /* Unmapped or moved away is lost even during a hold or a freeze: the next write would land in freed
   * memory or in another body. */
  bool mapped = record && current == record && current_ok;
  bool moved_away = record && current_ok && current != record;
  if (record && (!mapped || moved_away || (!hold_frames && !record_follows_link() && !(moving_link && frozen_frames)))) {
    log_printf(moved_away ? "levitate: Link's body moved to another entry (respawn?)"
                          : "levitate: lost Link's record (new area?)");
    if (floating)
      gfx_notice("levitate off: Link was moved");
    record = 0;
    moving_link = false;
    floating = false;
    speed_known = false;
    walls_known = false;
    walls_stop();
  }

  bool want_speed = cheats[CHEAT_SPEED] && !floating && !menu_open;
  bool want_walls = cheats[CHEAT_WALLS] && !floating && !menu_open && !in_exit;
  if (!record && current_ok && record_at_follows_link(current)) {
    record = current;
    record_has_motion = true;
    log_printf("levitate: Link's physics record is %#lx", (unsigned long)record);
  }
  if (!record) {
    if (floating) {
      floating = false;
      gfx_notice("levitate: Link's position not found, try again");
    }
    speed_known = false;
    walls_known = false;
    walls_stop();
    return;
  }
  if (!want_walls) {
    if (walking_through)
      log_printf("walls: handed back, cheat off, floating or menu open");
    walls_known = false;
    walls_stop();
  }
  if (floating) {
    if (levitate_through_walls) {
      if (!cct_noclip_begin()) {
        drop();
        gfx_notice("levitate: controller unavailable; stopped");
        return;
      }
    } else {
      cct_noclip_end();
      if (cct_noclip_active()) {
        drop();
        return;
      }
    }
    if (!moving_link)
      float_start();
    float_step(dt, menu_open);
    speed_known = false;
  } else if (want_walls) {
    walls_step(dt, want_speed); /* does 2x speed too */
    speed_known = false;
  } else if (want_speed) {
    speed_step();
  } else {
    speed_known = false;
  }
}

void levitate_frame(bool menu_open)
{
  frame(menu_open);
  if (!floating && !phasing)
    cct_noclip_end();
  /* Update after toggles, failed searches and scene changes as well as normal steps.
   * Our input reads remain raw; only the game's HID wrappers mask these buttons. */
  uint32_t toggle = bind_length(binds[CMD_LEVITATE]) == 1 ? cmd_mask(CMD_LEVITATE) : 0; /* a combo's stay */
  input_hide_from_game = (levitate_enabled ? toggle : 0) |
                         (floating ? cmd_mask(CMD_HOVER) | cmd_mask(CMD_RISE) | cmd_mask(CMD_SINK) : 0);
}

bool levitate_floating(void)
{
  return floating;
}

const char *levitate_state(void)
{
  if (moving_link)
    return "floating";
  if (floating)
    return "starting";
  return link_record_status() == REC_READY ? "ready" : "off";
}
