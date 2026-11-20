/* Memfiles (memfiles_v2): save and load snapshots using the game's own GameData binary serializer,
 * ResetGimmick (fast same-map reload), clean silent area reload, and direct AI camera traversal. */
#ifndef OOTGZ_MEMFILE_H
#define OOTGZ_MEMFILE_H

#include <stdbool.h>

#define MEMFILE_SLOTS     5
#define MEMFILE_NAME_MAX  30 /* an exported memfile's name (its file: <name>.szm) */
#define MEMFILE_FILES_MAX 128 /* folders and .szm files listed, in one folder */
#define MEMFILE_FOLDER_LEN 128 /* a folder's path in sloperinaGZ/memfiles (folders in folders) */
#define MEMFILE_EXT       ".szm"
#define MEMFILE_DIR_NAME  "memfiles" /* in sloperinaGZ/ on the SD card */

extern bool memfile_session;     /* restore unsaved session fields (memfilefields.h) too */
extern bool memfile_fast_reload; /* reset actors in place via ResetGimmick on same map & age */

int         memfile_slot(void);
void        memfile_set_slot(int s);
bool        memfile_save(int s);
bool        memfile_load(int s); /* true when the load started */
void        memfile_frame(void); /* frame hook (after age_frame) */
void        memfile_binds_frame(bool menu_open); /* the memfile commands (binds.h), before memfile_frame */
bool        memfile_busy(void);
void        memfile_slot_text(int s, char *buf, int len); /* for the memfiles menu */
const char *memfile_status(void);                         /* current save/load line, or "" */

/* sharing memfiles as .szm files in sloperinaGZ/memfiles and folders made in it (only when asked: nothing is
 * written on its own). One folder is looked at at a time: exports go into it, imports come from it. */
bool        memfile_used(int s);
const char *memfile_slot_map(int s);                      /* the slot's map (the export's first name), or "" */
bool        memfile_export(int s, const char *name);      /* write slot s as <name>.szm in the folder looked at */
int         memfile_list_files(void);  /* list the folder looked at: its folders, then its .szm files; how many */
int         memfile_list_folders(void);                   /* how many of the last list's first entries are folders */
const char *memfile_file_name(int i);                     /* from the last list (a file's without .szm) */
bool        memfile_import(int i, int s);                 /* read listed file i into slot s */
bool        memfile_delete_file(int i);                   /* delete listed file i from the SD card; lists again */
const char *memfile_browsing(void);  /* the folder looked at, in sloperinaGZ/memfiles ("a/b"; "": that folder) */
bool        memfile_browse_into(int i);                   /* into folder i of the last list */
void        memfile_browse_up(void);                      /* stops at sloperinaGZ/memfiles */
void        memfile_browse_top(void);
bool        memfile_make_folder(const char *name);        /* a folder in the one looked at */
bool        memfile_delete_folder(void); /* the one looked at and all in it (not the top); then up a folder */

#endif
