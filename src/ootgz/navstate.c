/* The message window's state. The UI manager ([main+ui_mgr_ptr], FINDINGS §4) keeps every screen all the time;
 * the message window is the one whose layout name (char * at screen+ui_screen_name) is "MessageDialog_00". Its
 * state, choice cursor and message id are at the build's msgdlg_* offsets (z64hd.h), measured on 1.1.1 by
 * comparing the screen while text typed, waited, offered a choice and closed. */
#include "navstate.h"

#include <stdint.h>

#include "sys.h"
#include "z64hd.h"

#define UI_MGR_SCREEN_COUNT 0x8
#define UI_MGR_SCREENS      0x10
#define UI_MAX_SCREENS      256
#define NAME_MAX            40
#define MSG_PREFIX          "EventFlowMsg/"

static const char WINDOW[] = "MessageDialog_00";
static const char WHEEL[] = "ShortCut_00";

static bool known(void)
{
  return z64hd_game && z64hd_game->ui_mgr_ptr && z64hd_game->ui_screen_name && z64hd_game->msgdlg_state &&
         z64hd_game->msgdlg_cursor && z64hd_game->msgdlg_message;
}

/* copies the C string at p (at most cap-1 characters) into out; false if it can't be read */
static bool read_str(uintptr_t p, char *out, size_t cap)
{
  size_t n = 0;
  if (!p || !cap)
    return false;
  for (; n + 1 < cap && sys_mapped(p + n, 1, SYS_PERM_R); n++) {
    char c = ((const volatile char *)p)[n];
    if (!c)
      break;
    out[n] = c;
  }
  out[n] = 0;
  return n > 0;
}

static bool named(uintptr_t screen, const char *want, size_t want_size);
static bool is_window(uintptr_t screen) { return named(screen, WINDOW, sizeof(WINDOW)); }
static bool is_wheel(uintptr_t screen) { return named(screen, WHEEL, sizeof(WHEEL)); }

static bool named(uintptr_t screen, const char *want, size_t want_size)
{
  char name[NAME_MAX];
  uintptr_t p = 0;
  if (!sys_mapped(screen + z64hd_game->ui_screen_name, 8, SYS_PERM_R))
    return false;
  p = sys_ptr(screen + z64hd_game->ui_screen_name);
  if (!read_str(p, name, sizeof(name)))
    return false;
  for (size_t i = 0; i < want_size; i++)
    if (name[i] != want[i])
      return false;
  return true;
}

/* the first screen that pick accepts, or 0 */
static uintptr_t find_screen(bool (*pick)(uintptr_t))
{
  uintptr_t holder = sys_ptr(MAIN(z64hd_game->ui_mgr_ptr));
  uintptr_t mgr = holder ? sys_ptr(holder) : 0;
  uint32_t count;
  if (!mgr || !sys_read32(mgr + UI_MGR_SCREEN_COUNT, &count))
    return 0;
  uintptr_t screens = sys_ptr(mgr + UI_MGR_SCREENS);
  for (uint32_t i = 0; screens && i < count && i < UI_MAX_SCREENS; i++) {
    uintptr_t screen = sys_mapped(screens + 8 * i, 8, SYS_PERM_R) ? sys_ptr(screens + 8 * i) : 0;
    if (screen && pick(screen))
      return screen;
  }
  return 0;
}

/* the message window, cached while it stays the same screen */
static uintptr_t window(void)
{
  static uintptr_t cached;
  if (!(cached && sys_mapped(cached, 8, SYS_PERM_R) && is_window(cached)))
    cached = find_screen(is_window);
  return cached;
}

int wheel_phase(void)
{
  static uintptr_t cached;
  uint32_t v;
  if (!z64hd_game || !z64hd_game->ui_mgr_ptr || !z64hd_game->ui_screen_name || !z64hd_game->ui_screen_phase)
    return -1;
  if (!(cached && sys_mapped(cached, 8, SYS_PERM_R) && is_wheel(cached)))
    cached = find_screen(is_wheel);
  if (!cached || !sys_read32(cached + z64hd_game->ui_screen_phase, &v))
    return -1;
  return (int)(v >> 16);
}

bool dialog_read(dialog_t *out)
{
  *out = (dialog_t){ DLG_UNKNOWN, -1, -1, "" };
  if (!known())
    return false;
  uintptr_t w = window();
  uint32_t raw, cursor;
  if (!w || !sys_read32(w + z64hd_game->msgdlg_state, &raw) || !sys_read32(w + z64hd_game->msgdlg_cursor, &cursor))
    return false;
  out->raw = (int)raw;
  switch (raw) {
  case 12: out->state = DLG_CLOSED; break;
  case 2: out->state = DLG_TYPING; break;
  case 3: out->state = DLG_WAITING; break;
  case 6: out->state = DLG_TURNING; break;
  case 5: out->state = DLG_CHOICE_OPENING; break;
  case 4: out->state = DLG_CHOICE; break;
  default: out->state = DLG_OTHER; break;
  }
  if (out->state == DLG_CHOICE)
    out->cursor = (int)(cursor & 0xFF);
  char full[sizeof(out->msg) + sizeof(MSG_PREFIX)];
  uintptr_t m = sys_mapped(w + z64hd_game->msgdlg_message, 8, SYS_PERM_R) ? sys_ptr(w + z64hd_game->msgdlg_message) : 0;
  if (out->state != DLG_CLOSED && read_str(m, full, sizeof(full))) {
    const char *s = full;
    size_t p = 0;
    while (MSG_PREFIX[p] && s[p] == MSG_PREFIX[p])
      p++;
    if (!MSG_PREFIX[p])
      s += p;
    size_t n = 0;
    for (; s[n] && n + 1 < sizeof(out->msg); n++)
      out->msg[n] = s[n];
    out->msg[n] = 0;
  }
  return true;
}

const char *dialog_state_name(dialog_state_t state)
{
  static const char *const names[] = { "?", "closed", "typing", "waiting", "turning", "choices", "choice", "other" };
  return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "?";
}

bool dialog_open(const dialog_t *d)
{
  return d->state != DLG_CLOSED && d->state != DLG_UNKNOWN;
}
