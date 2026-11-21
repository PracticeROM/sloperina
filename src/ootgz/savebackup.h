/* settings > back up saves: after every save the game commits (autosave or manual), copy its whole save
 * mount to sd:/sloperinaGZ/save-backups/<YYYY-MM-DD_HH-MM-SS>[ - <location>]/, keeping the newest
 * SAVE_BACKUPS_KEPT. */
#ifndef SAVEBACKUP_H
#define SAVEBACKUP_H

#include <stdbool.h>
#include <stdint.h>

#define SAVE_BACKUPS_KEPT 100

enum { SAVE_BACKUP_AUTO, SAVE_BACKUP_HARD, SAVE_BACKUP_BOTH };

extern bool save_backup_enabled;
extern int save_backup_kinds;     /* which saves: SAVE_BACKUP_* */
extern bool save_backup_location; /* " - <location>" after the date in the folder names */

/* At module init: hook main's nn::fs::CommitSaveData. Returns 0, or what went wrong. */
const char *savebackup_hook_game(uintptr_t main_base);
/* Once a frame, on the game's thread: report backups made since (the copy runs inside the game's commit). */
void savebackup_frame(void);
/* For the menu: the last backup, or why there's none. */
void savebackup_status_text(char *buf, int len);


/* Savestates: saves in sd:/sloperinaGZ/savestates/<category>/<name>/slot_NN/ (progress.sav + caption.sav), copied
 * into ONE slot of the game's save folder (slot_01) on request, or dumped from it. */
#define SAVESTATES_MAX 512      /* saves per category */
#define SAVESTATES_CATS_MAX 32  /* categories */
#define SAVESTATES_PAGE_MAX 8   /* most saves shown on one menu page (fewer when the menu is short) */
#define SAVESTATES_PAGE_MIN 3   /* fewest: the menu scrolls rather than show fewer */
void savestates_scan(void);            /* list the folders (also done once when the SD card is up) */
int savestates_count(void);            /* saves in the current category */
const char *savestates_name(int i);
int savestates_category_count(void);
int savestates_category_index(void);   /* the current category, -1 when there is none */
const char *savestates_category_name(int i);
void savestates_select_category(int i);
int savestates_page_size(void);        /* saves per menu page now */
void savestates_set_page_size(int n);  /* the first save on screen stays on screen */
int savestates_page_first(void);       /* index of the first save of the current menu page */
int savestates_page(void);
int savestates_pages(void);
void savestates_select_page(int i);
int savestates_generation(void);       /* changes when the list does: the menu rebuilds itself */
const char *savestates_apply(int i);   /* 0 when done, else what went wrong (also in savestates_result) */
/* slot_01 -> a new save called name (empty: save_NNN); 0 when done, else what went wrong. experimental: the new
 * save's progress.sav is then brought up to date from the live game data and Link's position (EXPERIMENTAL) */
const char *savestates_dump(const char *name, bool experimental);
const char *savestates_result(void);   /* what the last apply/dump did, or why it couldn't */
/* For reverse engineering: writes main's memory to sd:/sloperinaGZ/main.bin and its load address (hex text) to
 * sd:/sloperinaGZ/main.base; 0 when done, else what went wrong (also in savestates_result) */
const char *savestates_dump_main(void);
void savestates_status_text(char *buf, int len);

#endif
