/* Compat sys/utsname.h — tinyfiledialogs.c sniffs the OS with uname();
 * report failure and it takes its generic path.
 */
#ifndef PIZZA_COMPAT_UTSNAME_H
#define PIZZA_COMPAT_UTSNAME_H

struct utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
};

static inline int uname(struct utsname *buf)
{
	(void)buf;
	return -1;
}

#endif
