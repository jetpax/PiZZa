/* POSIX stubs for calls that stay unimplemented on the shim. The real
 * file API (open/read/write/lseek/close/fstat/stat/access) lives in
 * dosbox_assets.c, backed by baked assets.
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

int chmod(const char *path, mode_t mode)
{
	(void)path; (void)mode;
	errno = ENOENT;
	return -1;
}

int rmdir(const char *path)
{
	(void)path;
	errno = ENOENT;
	return -1;
}

int ftruncate(int fd, off_t length)
{
	(void)fd; (void)length;
	errno = EBADF;
	return -1;
}

int futimens(int fd, const void *times)
{
	(void)fd; (void)times;
	errno = EBADF;
	return -1;
}

int fcntl(int fd, int cmd, ...)
{
	(void)fd; (void)cmd;
	errno = EBADF;
	return -1;
}

int flock(int fd, int operation)
{
	(void)fd; (void)operation;
	return 0;
}

char *realpath(const char *path, char *resolved)
{
	(void)path; (void)resolved;
	errno = ENOENT;
	return NULL;
}

int isatty(int fd)
{
	(void)fd;
	return 0;
}

FILE *popen(const char *command, const char *type)
{
	(void)command; (void)type;
	errno = ENOSYS;
	return NULL;
}

int pclose(FILE *stream)
{
	(void)stream;
	return -1;
}

char *getcwd(char *buf, size_t size)
{
	if (buf == NULL || size < 2) {
		errno = ERANGE;
		return NULL;
	}
	buf[0] = '/';
	buf[1] = '\0';
	return buf;
}

int gethostname(char *name, size_t len)
{
	(void)name; (void)len;
	errno = ENOSYS;
	return -1;
}

#include <zephyr/kernel.h>
#include <time.h>

int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
	(void)clock_id;
	if (tp == NULL) {
		errno = EINVAL;
		return -1;
	}

	int64_t ms = k_uptime_get();

	tp->tv_sec = ms / 1000;
	tp->tv_nsec = (ms % 1000) * 1000000L;
	return 0;
}
