/* Hand-authored config.h for DOSBox-X on Zephyr/AArch64 (PiZZa).
 *
 * Replaces the autotools-generated header; macro choices per
 * apps/DOSBox/notes/P2_SCOPE.md. Modeled on vs/config.h (upstream's
 * hand-maintained non-autotools config), corrected for LP64 GCC.
 */
#ifndef PIZZA_DOSBOX_CONFIG_H
#define PIZZA_DOSBOX_CONFIG_H

/* every DOSBox-X header assumes the fixed-width types are in scope */
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <alloca.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#ifndef F_OK
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

/* picolibc leaves PATH_MAX to the platform */
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

/* POSIX file/dir calls picolibc's headers omit; the shim's posix layer
 * and the RAM-fd layer implement (or stub) them at link time.
 */
#ifdef __cplusplus
extern "C" {
#endif
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int rmdir(const char *path);
int unlink(const char *path);
off_t lseek(int fd, off_t offset, int whence);
ssize_t write(int fd, const void *buf, size_t count);
ssize_t read(int fd, void *buf, size_t count);
int close(int fd);
int isatty(int fd);
int gethostname(char *name, size_t len);
int access(const char *path, int mode);
int ftruncate(int fd, off_t length);
FILE *popen(const char *command, const char *type);
int pclose(FILE *stream);
#ifdef __cplusplus
}
#endif

/* SDL1-era decoration macros expected by the bundled compat_SDL_cdrom.h */
#ifndef DECLSPEC
#define DECLSPEC
#endif
#ifndef SDLCALL
#define SDLCALL
#endif

/* ── CPU core ─────────────────────────────────────────────────────── */
/* AArch64: portable dynamic recompiler with the ARMv8 LE backend.
 * C_DYNAMIC_X86 and C_FPU_X86 are x86-host-only and stay off.
 */
#define C_TARGETCPU ARMV8LE
#define C_DYNREC 1

/* ── FPU ──────────────────────────────────────────────────────────── */
/* HAS_LONG_DOUBLE deliberately NOT defined: on AArch64 long double is
 * software binary128; the double-based fpu_instructions.h is the same
 * path vanilla DOSBox uses on non-x86 hosts.
 */
#define C_FPU 1

#define C_UNALIGNED_MEMORY 1
#define C_SDL2 1

/* vendored vs/zlib + vs/libpng: bios.cpp's boot splash decodes PNG
 * unconditionally, so these are part of the floor (P2_SCOPE item 1
 * resolved: libpng cannot drop out). C_SSHOT stays off.
 */
#define C_LIBZ 1
#define C_LIBPNG 1

/* ── compiler features (GCC) ──────────────────────────────────────── */
#define C_ATTRIBUTE_ALWAYS_INLINE 1
#define C_HAS_ATTRIBUTE 1
#define C_HAS_BUILTIN_EXPECT 1

/* ── everything desktop-optional stays OFF (undefined):
 * C_OPENGL C_DIRECT3D C_D3DSHADERS C_XBRZ C_SURFACE_POSTRENDER_ASPECT
 * C_SSHOT C_LIBPNG C_LIBZ C_SDL_NET C_MODEM C_IPX C_PCAP C_SLIRP
 * C_PRINTER C_DIRECTLPT C_DIRECTSERIAL C_MT32 C_FLUIDSYNTH C_FREETYPE
 * C_DEBUG C_HEAVY_DEBUG C_GAMELINK C_X11_XKB C_SDL1 HAVE_ALSA
 * C_SET_PRIORITY C_HAVE_MPROTECT C_HAVE_MMAP HAS_LONG_DOUBLE
 */

/* ── libc surface (picolibc) ──────────────────────────────────────── */
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define STDC_HEADERS 1

/* ── LP64 AArch64 sizes ───────────────────────────────────────────── */
#define SIZEOF_INT_P 8
#define SIZEOF_UNSIGNED_CHAR 1
#define SIZEOF_UNSIGNED_SHORT 2
#define SIZEOF_UNSIGNED_INT 4
#define SIZEOF_UNSIGNED_LONG 8
#define SIZEOF_UNSIGNED_LONG_LONG 8

/* little-endian: WORDS_BIGENDIAN undefined */

/* include/byteorder.h's platform ladder (linux/apple/bsd/haiku/msvc)
 * has no branch for bare-metal; supply the LE macros here since
 * config.h is every TU's first include.
 */
#ifndef htole16
#define htole16(x) ((uint16_t)(x))
#define le16toh(x) ((uint16_t)(x))
#define htole32(x) ((uint32_t)(x))
#define le32toh(x) ((uint32_t)(x))
#define htole64(x) ((uint64_t)(x))
#define le64toh(x) ((uint64_t)(x))
#define htobe16(x) __builtin_bswap16(x)
#define be16toh(x) __builtin_bswap16(x)
#define htobe32(x) __builtin_bswap32(x)
#define be32toh(x) __builtin_bswap32(x)
#define htobe64(x) __builtin_bswap64(x)
#define be64toh(x) __builtin_bswap64(x)
#endif

#ifndef CONST
#define CONST const
#endif

#if C_ATTRIBUTE_ALWAYS_INLINE
#define INLINE inline __attribute__((always_inline))
#else
#define INLINE inline
#endif

/* fastcall is x86-only */
#define DB_FASTCALL

#if C_HAS_ATTRIBUTE
#define GCC_ATTRIBUTE(x) __attribute__ ((x))
#else
#define GCC_ATTRIBUTE(x)
#endif

#if C_HAS_BUILTIN_EXPECT
#define GCC_UNLIKELY(x) __builtin_expect((x),0)
#define GCC_LIKELY(x) __builtin_expect((x),1)
#else
#define GCC_UNLIKELY(x) (x)
#define GCC_LIKELY(x) (x)
#endif

typedef double Real64;

typedef unsigned char      Bit8u;
typedef   signed char      Bit8s;
typedef unsigned short     Bit16u;
typedef   signed short     Bit16s;
typedef unsigned int       Bit32u;
typedef   signed int       Bit32s;
typedef unsigned long long Bit64u;
typedef   signed long long Bit64s;

#if SIZEOF_INT_P == 4
typedef Bit32u Bitu;
typedef Bit32s Bits;
#else
typedef Bit64u Bitu;
typedef Bit64s Bits;
#endif

#include "config_package.h"

#endif /* PIZZA_DOSBOX_CONFIG_H */
