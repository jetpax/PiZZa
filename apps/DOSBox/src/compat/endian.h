/* Little-endian <endian.h> for picolibc builds of DOSBox-X.
 * include/byteorder.h falls through to <endian.h> on non-Windows
 * platforms; picolibc has no such header. AArch64 here is LE-only.
 */
#ifndef PIZZA_COMPAT_ENDIAN_H
#define PIZZA_COMPAT_ENDIAN_H

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
