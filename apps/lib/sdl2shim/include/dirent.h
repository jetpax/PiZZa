/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * PiZZa SDL2 shim -- minimal <dirent.h>. picolibc has no dirent;
 * SDLPoP only lists directories to enumerate mods and replays.
 * The shim implementation (shim_posix.c) reports "no entries", so
 * those menus are simply empty. Backing this with Zephyr fs_opendir
 * is a phase-5+ option if mod browsing ever matters on-device.
 */

#ifndef PIZZA_SHIM_DIRENT_H
#define PIZZA_SHIM_DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* d_type values (DOSBox-X drive_local checks d_type == DT_REG) */
#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

struct dirent {
	unsigned char d_type;
	char d_name[256];
};

typedef struct s2s_dir DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif /* PIZZA_SHIM_DIRENT_H */
