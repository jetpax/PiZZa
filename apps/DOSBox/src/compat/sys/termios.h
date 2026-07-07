/* Compat sys/termios.h — tinyfiledialogs.c (force-included by
 * dos_programs.cpp/menu_callback.cpp) compiles console-dialog paths
 * that never run on the shim; give it an inert termios surface.
 */
#ifndef PIZZA_COMPAT_TERMIOS_H
#define PIZZA_COMPAT_TERMIOS_H

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

#define NCCS 32
struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_cc[NCCS];
};

#define ICANON  0x0002
#define ECHO    0x0008
#define TCSANOW 0
#define VMIN    16
#define VTIME   17

static inline int tcgetattr(int fd, struct termios *t)
{
	(void)fd; (void)t;
	return -1;
}
static inline int tcsetattr(int fd, int actions, const struct termios *t)
{
	(void)fd; (void)actions; (void)t;
	return -1;
}

#endif
